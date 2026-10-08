/* SPDX-License-Identifier: MIT OR GPL-3.0-only */
/* tls_mbedtls.c
** Backend TLS de libstrophe sobre el mbedTLS del motor Godot.
**
** Porta la verificación de cadena + hostname que usa el cliente TLS del
** motor (modules/mbedtls/stream_peer_mbedtls.cpp: authmode REQUIRED +
** mbedtls_ssl_set_hostname + CA chain), en lugar de inventar verificación.
** No soporta SCRAM-PLUS (channel binding): se devuelve "no disponible" y
** libstrophe cae a SCRAM-SHA-1/256 sin -PLUS.
*/

#include <errno.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>

#include "common.h"
#include "sock.h"
#include "tls.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

/* Códigos de error de los callbacks de red. Normalmente vienen de
 * <mbedtls/net_sockets.h>, pero no queremos forzar MBEDTLS_NET_C: definimos
 * los mismos valores si faltan (sólo se usan para abortar el handshake). */
#ifndef MBEDTLS_ERR_NET_RECV_FAILED
#define MBEDTLS_ERR_NET_RECV_FAILED -0x004C
#endif
#ifndef MBEDTLS_ERR_NET_SEND_FAILED
#define MBEDTLS_ERR_NET_SEND_FAILED -0x004E
#endif

enum {
    TLS_SHUTDOWN_MAX_RETRIES = 10,
    TLS_TIMEOUT_SEC = 0,
    TLS_TIMEOUT_USEC = 100000,
};

struct _tls {
    xmpp_ctx_t *ctx;
    xmpp_conn_t *conn;
    sock_t sock;

    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cacert;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;

    int cacert_loaded;
    int lasterror;

    /* Operaciones de socket crudo: sock_read/sock_write necesitan intf->conn. */
    struct conn_interface raw_intf;
};

/* Rutas típicas del bundle de CA del sistema (fallback si no se pasó cafile). */
static const char *default_ca_paths[] = {
    "/etc/ssl/certs/ca-certificates.crt",
    "/etc/pki/tls/certs/ca-bundle.crt",
    "/etc/ssl/cert.pem",
    "/etc/ssl/ca-bundle.pem",
};

void tls_initialize(void) {
}

void tls_shutdown(void) {
}

static int _tls_want(int err) {
    return err == MBEDTLS_ERR_SSL_WANT_READ || err == MBEDTLS_ERR_SSL_WANT_WRITE;
}

static int _bio_send(void *p_ctx, const unsigned char *p_buf, size_t p_len) {
    tls_t *tls = (tls_t *)p_ctx;
    int ret = sock_write(&tls->raw_intf, p_buf, p_len);
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        }
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return ret;
}

static int _bio_recv(void *p_ctx, unsigned char *p_buf, size_t p_len) {
    tls_t *tls = (tls_t *)p_ctx;
    int ret = sock_read(&tls->raw_intf, p_buf, p_len);
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return MBEDTLS_ERR_SSL_WANT_READ;
        }
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    return ret;
}

static void _tls_sock_wait(tls_t *tls, int error) {
    struct timeval tv;
    fd_set rfds;
    fd_set wfds;
    int nfds;
    int ret;

    if (error == 0) {
        return;
    }

    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    if (error == MBEDTLS_ERR_SSL_WANT_READ) {
        FD_SET(tls->sock, &rfds);
    }
    if (error == MBEDTLS_ERR_SSL_WANT_WRITE) {
        FD_SET(tls->sock, &wfds);
    }
    nfds = (error == MBEDTLS_ERR_SSL_WANT_READ || error == MBEDTLS_ERR_SSL_WANT_WRITE)
                   ? tls->sock + 1
                   : 0;
    do {
        tv.tv_sec = TLS_TIMEOUT_SEC;
        tv.tv_usec = TLS_TIMEOUT_USEC;
        ret = select(nfds, &rfds, &wfds, NULL, &tv);
    } while (ret == -1 && errno == EINTR);
}

static void _tls_log_error(tls_t *tls, int err, const char *where) {
    char msg[256];
    mbedtls_strerror(err, msg, sizeof(msg));
    strophe_error(tls->ctx, "tls", "%s: -0x%04x (%s)", where, (unsigned int)-err, msg);
}

static int _tls_load_ca_file(tls_t *tls, const char *p_path) {
    int ret = mbedtls_x509_crt_parse_file(&tls->cacert, p_path);
    if (ret != 0) {
        _tls_log_error(tls, ret, "could not parse CA file");
        return -1;
    }
    tls->cacert_loaded = 1;
    return 0;
}

tls_t *tls_new(xmpp_conn_t *conn) {
    tls_t *tls = strophe_alloc(conn->ctx, sizeof(*tls));
    if (tls == NULL) {
        return NULL;
    }
    memset(tls, 0, sizeof(*tls));
    tls->ctx = conn->ctx;
    tls->conn = conn;
    tls->sock = conn->sock;
    tls->raw_intf = sock_intf;
    tls->raw_intf.conn = conn;

    mbedtls_ssl_init(&tls->ssl);
    mbedtls_ssl_config_init(&tls->conf);
    mbedtls_x509_crt_init(&tls->cacert);
    mbedtls_entropy_init(&tls->entropy);
    mbedtls_ctr_drbg_init(&tls->ctr_drbg);

    {
        const char *pers = "xat-xmpp";
        int ret = mbedtls_ctr_drbg_seed(&tls->ctr_drbg, mbedtls_entropy_func,
                                        &tls->entropy, (const unsigned char *)pers,
                                        strlen(pers));
        if (ret != 0) {
            _tls_log_error(tls, ret, "ctr_drbg_seed");
            goto err;
        }
    }

    {
        int ret = mbedtls_ssl_config_defaults(&tls->conf, MBEDTLS_SSL_IS_CLIENT,
                                              MBEDTLS_SSL_TRANSPORT_STREAM,
                                              MBEDTLS_SSL_PRESET_DEFAULT);
        if (ret != 0) {
            _tls_log_error(tls, ret, "ssl_config_defaults");
            goto err;
        }
    }

    mbedtls_ssl_conf_rng(&tls->conf, mbedtls_ctr_drbg_random, &tls->ctr_drbg);
    mbedtls_ssl_conf_authmode(&tls->conf, conn->tls_trust ? MBEDTLS_SSL_VERIFY_NONE
                                                          : MBEDTLS_SSL_VERIFY_REQUIRED);

    /* CA: la que pidió libstrophe; si no, los bundles típicos del sistema. */
    if (conn->tls_cafile != NULL) {
        _tls_load_ca_file(tls, conn->tls_cafile);
    } else {
        size_t n = sizeof(default_ca_paths) / sizeof(default_ca_paths[0]);
        size_t i;
        for (i = 0; i < n; i++) {
            if (_tls_load_ca_file(tls, default_ca_paths[i]) == 0) {
                break;
            }
        }
    }
    mbedtls_ssl_conf_ca_chain(&tls->conf, &tls->cacert, NULL);

    {
        int ret = mbedtls_ssl_setup(&tls->ssl, &tls->conf);
        if (ret != 0) {
            _tls_log_error(tls, ret, "ssl_setup");
            goto err;
        }
    }

    mbedtls_ssl_set_bio(&tls->ssl, tls, _bio_send, _bio_recv, NULL);

    /* SNI + verificación de hostname (contra el dominio del JID). */
    if (conn->domain != NULL) {
        mbedtls_ssl_set_hostname(&tls->ssl, conn->domain);
    }

    return tls;

err:
    mbedtls_x509_crt_free(&tls->cacert);
    mbedtls_ctr_drbg_free(&tls->ctr_drbg);
    mbedtls_entropy_free(&tls->entropy);
    mbedtls_ssl_config_free(&tls->conf);
    mbedtls_ssl_free(&tls->ssl);
    strophe_free(conn->ctx, tls);
    return NULL;
}

void tls_free(tls_t *tls) {
    if (tls == NULL) {
        return;
    }
    /* Cortar el puntero del conn ANTES de liberar: si libstrophe vuelve a
     * rutear una escritura por tls_intf, tls_write/tls_pending verán NULL en
     * vez de memoria liberada (evita use-after-free en el fallo de TLS). */
    if (tls->conn != NULL && tls->conn->tls == tls) {
        tls->conn->tls = NULL;
    }
    mbedtls_ssl_close_notify(&tls->ssl);
    mbedtls_x509_crt_free(&tls->cacert);
    mbedtls_ctr_drbg_free(&tls->ctr_drbg);
    mbedtls_entropy_free(&tls->entropy);
    mbedtls_ssl_config_free(&tls->conf);
    mbedtls_ssl_free(&tls->ssl);
    strophe_free(tls->ctx, tls);
}

int tls_set_credentials(tls_t *tls, const char *cafilename) {
    if (cafilename == NULL) {
        return -1;
    }
    mbedtls_x509_crt_free(&tls->cacert);
    mbedtls_x509_crt_init(&tls->cacert);
    tls->cacert_loaded = 0;
    return _tls_load_ca_file(tls, cafilename);
}

/* Sin cliente certificado ni EXTERNAL: no hay id-on-xmppAddr. */
unsigned int tls_id_on_xmppaddr_num(xmpp_conn_t *conn) {
    UNUSED(conn);
    return 0;
}

char *tls_id_on_xmppaddr(xmpp_conn_t *conn, unsigned int n) {
    UNUSED(conn);
    UNUSED(n);
    return NULL;
}

/* Sin channel binding: SCRAM-PLUS no se anuncia (se usa SCRAM-SHA-1/256). */
int tls_init_channel_binding(tls_t *tls, const char **binding_prefix, size_t *binding_prefix_len) {
    UNUSED(tls);
    if (binding_prefix != NULL) {
        *binding_prefix = NULL;
    }
    if (binding_prefix_len != NULL) {
        *binding_prefix_len = 0;
    }
    return -1;
}

const void *tls_get_channel_binding_data(tls_t *tls, size_t *size) {
    UNUSED(tls);
    if (size != NULL) {
        *size = 0;
    }
    return NULL;
}

xmpp_tlscert_t *tls_peer_cert(xmpp_conn_t *conn) {
    UNUSED(conn);
    return NULL;
}

int tls_start(tls_t *tls) {
    int ret;

    while (1) {
        ret = mbedtls_ssl_handshake(&tls->ssl);
        if (ret == 0) {
            break;
        }
        if (_tls_want(ret)) {
            _tls_sock_wait(tls, ret);
            continue;
        }
        tls->lasterror = ret;
        _tls_log_error(tls, ret, "handshake");
        return 0;
    }

    if (!tls->conn->tls_trust) {
        uint32_t vr = mbedtls_ssl_get_verify_result(&tls->ssl);
        if (vr != 0) {
            char info[512];
            mbedtls_x509_crt_verify_info(info, sizeof(info), "", vr);
            strophe_error(tls->ctx, "tls", "certificate verification failed: %s", info);
            tls->lasterror = MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
            return 0;
        }
    }

    strophe_debug(tls->ctx, "tls", "Certificate verification passed");
    tls->lasterror = 0;
    return 1;
}

int tls_stop(tls_t *tls) {
    int retries = 0;

    while (1) {
        int ret = mbedtls_ssl_close_notify(&tls->ssl);
        if (ret == 0 || !_tls_want(ret) || ++retries >= TLS_SHUTDOWN_MAX_RETRIES) {
            break;
        }
        _tls_sock_wait(tls, ret);
    }
    return 1;
}

int tls_is_recoverable(struct conn_interface *intf, int error) {
    UNUSED(intf);
    return error == 0 || _tls_want(error);
}

int tls_error(struct conn_interface *intf) {
    if (intf->conn->tls == NULL) {
        return -1;
    }
    return intf->conn->tls->lasterror;
}

int tls_pending(struct conn_interface *intf) {
    if (intf->conn->tls == NULL) {
        return 0;
    }
    return (int)mbedtls_ssl_get_bytes_avail(&intf->conn->tls->ssl);
}

int tls_read(struct conn_interface *intf, void *buff, size_t len) {
    tls_t *tls = intf->conn->tls;
    if (tls == NULL) {
        return -1;
    }
    int ret = mbedtls_ssl_read(&tls->ssl, buff, len);
    tls->lasterror = ret < 0 ? ret : 0;
    return ret;
}

int tls_write(struct conn_interface *intf, const void *buff, size_t len) {
    tls_t *tls = intf->conn->tls;
    if (tls == NULL) {
        return -1;
    }
    int ret = mbedtls_ssl_write(&tls->ssl, buff, len);
    tls->lasterror = ret < 0 ? ret : 0;
    return ret;
}

int tls_clear_pending_write(struct conn_interface *intf) {
    UNUSED(intf);
    return 0;
}
