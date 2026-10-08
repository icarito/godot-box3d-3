/*************************************************************************/
/*  xmpp_connection.cpp                                                  */
/*************************************************************************/
/* Módulo xmpp (xat): nodo nativo sobre libstrophe.                      */
/*************************************************************************/

#include "xmpp_connection.h"

#include "core/class_db.h"
#include "core/error_macros.h"
#include "core/os/os.h"
#include "core/io/certs_compressed.gen.h"
#include "core/io/compression.h"
#include "core/os/file_access.h"
#include "core/project_settings.h"

// ---------------------------------------------------------------------------
// Ciclo de vida
// ---------------------------------------------------------------------------

XmppConnection::XmppConnection() {
}

XmppConnection::~XmppConnection() {
	close();
	// El ctx y el sm_state viven toda la vida del nodo (para poder resumir SM
	// entre reconexiones); recién acá, sin hilo, se liberan.
	if (sm_state != nullptr) {
		xmpp_free_sm_state(sm_state);
		sm_state = nullptr;
	}
	if (ctx != nullptr) {
		xmpp_ctx_free(ctx);
		ctx = nullptr;
	}
}

void XmppConnection::_bind_methods() {
	ClassDB::bind_method(D_METHOD("open", "jid", "pass", "host", "port", "cafile"), &XmppConnection::open, DEFVAL(""), DEFVAL(5222), DEFVAL(""));
	ClassDB::bind_method(D_METHOD("close"), &XmppConnection::close);
	ClassDB::bind_method(D_METHOD("send", "xml"), &XmppConnection::send);
	ClassDB::bind_method(D_METHOD("is_open"), &XmppConnection::is_open);

	// Métodos que se invocan con call_deferred desde el hilo de libstrophe.
	ClassDB::bind_method(D_METHOD("_emit_stanza", "xml"), &XmppConnection::_emit_stanza);
	ClassDB::bind_method(D_METHOD("_emit_connected", "bound_jid"), &XmppConnection::_emit_connected);
	ClassDB::bind_method(D_METHOD("_emit_disconnected", "error"), &XmppConnection::_emit_disconnected);
	ClassDB::bind_method(D_METHOD("_emit_log", "level", "msg"), &XmppConnection::_emit_log);
	ClassDB::bind_method(D_METHOD("_on_worker_finished", "generation"), &XmppConnection::_on_worker_finished);

	ADD_SIGNAL(MethodInfo("stanza_received", PropertyInfo(Variant::STRING, "xml")));
	ADD_SIGNAL(MethodInfo("connected", PropertyInfo(Variant::STRING, "bound_jid")));
	ADD_SIGNAL(MethodInfo("disconnected", PropertyInfo(Variant::INT, "error")));
	ADD_SIGNAL(MethodInfo("cert_info", PropertyInfo(Variant::DICTIONARY, "info")));
	ADD_SIGNAL(MethodInfo("log", PropertyInfo(Variant::INT, "level"), PropertyInfo(Variant::STRING, "msg")));
}

// ---------------------------------------------------------------------------
// API pública
// ---------------------------------------------------------------------------

// CA para verificar TLS. En Android/iOS no existe el bundle de /etc, y una
// cadena vacía con verificación obligatoria impide conectar: se cae al bundle
// embebido del motor (como hace StreamPeerSSL). Orden: cafile pedido y
// existente -> bundle del sistema (fallback en tls_mbedtls.c, se devuelve "")
// -> network/ssl/certificates -> certs builtin volcados a user://.
static String _resolve_cafile(const String &p_cafile) {
	if (p_cafile != "" && FileAccess::exists(p_cafile)) {
		return ProjectSettings::get_singleton()->globalize_path(p_cafile);
	}
	static const char *system_paths[] = {
		"/etc/ssl/certs/ca-certificates.crt",
		"/etc/pki/tls/certs/ca-bundle.crt",
		"/etc/ssl/cert.pem",
		"/etc/ssl/ca-bundle.pem",
	};
	for (unsigned int i = 0; i < sizeof(system_paths) / sizeof(system_paths[0]); i++) {
		if (FileAccess::exists(system_paths[i])) {
			return "";
		}
	}
	String project_certs = GLOBAL_GET("network/ssl/certificates");
	if (project_certs != "") {
		return ProjectSettings::get_singleton()->globalize_path(project_certs);
	}
#ifdef BUILTIN_CERTS_ENABLED
	const String bundle = "user://xat_ca_bundle.pem";
	if (!FileAccess::exists(bundle)) {
		PoolByteArray out;
		out.resize(_certs_uncompressed_size);
		PoolByteArray::Write w = out.write();
		Compression::decompress(w.ptr(), _certs_uncompressed_size, _certs_compressed, _certs_compressed_size, Compression::MODE_DEFLATE);
		FileAccess *f = FileAccess::open(bundle, FileAccess::WRITE);
		ERR_FAIL_COND_V_MSG(f == nullptr, "", "xmpp: no se pudo escribir el bundle de CA.");
		f->store_buffer(w.ptr(), _certs_uncompressed_size);
		memdelete(f);
	}
	return ProjectSettings::get_singleton()->globalize_path(bundle);
#else
	return "";
#endif
}

int XmppConnection::open(const String &p_jid, const String &p_pass, const String &p_host, int p_port, const String &p_cafile) {
	// Detiene cualquier corrida previa sin liberar ctx/sm_state (los necesita
	// la reconexión con XEP-0198 resume).
	_stop_worker();

	// Si cambia la cuenta, el sm_state viejo no sirve.
	String new_bare = p_jid;
	int slash = p_jid.find("/");
	if (slash >= 0) {
		new_bare = p_jid.substr(0, slash);
	}
	if (sm_state != nullptr && new_bare != last_bare) {
		xmpp_free_sm_state(sm_state);
		sm_state = nullptr;
	}
	last_bare = new_bare;

	jid = p_jid;
	password = p_pass;
	host = p_host;
	cafile = _resolve_cafile(p_cafile);
	port = p_port > 0 ? p_port : 5222;

	if (ctx == nullptr) {
		log.handler = _log_cb;
		log.userdata = this;
		ctx = xmpp_ctx_new(nullptr, &log);
		if (ctx == nullptr) {
			return XMPP_EMEM;
		}
	}

	stop_requested.clear();
	worker_disconnected = false;
	connected_flag.clear();
	generation += 1;

	thread = memnew(Thread);
	thread->start(_thread_trampoline, this);
	worker_active.set();
	return XMPP_EOK;
}

void XmppConnection::_stop_worker() {
	stop_requested.set();
	if (thread != nullptr) {
		thread->wait_to_finish();
		memdelete(thread);
		thread = nullptr;
	}
	worker_active.clear();
	connected_flag.clear();
}

void XmppConnection::close() {
	_stop_worker();
}

int XmppConnection::send(const String &p_xml) {
	if (!worker_active.is_set()) {
		return XMPP_EINVOP;
	}
	outgoing_mutex.lock();
	outgoing.push_back(p_xml);
	outgoing_mutex.unlock();
	return XMPP_EOK;
}

bool XmppConnection::is_open() const {
	return connected_flag.is_set();
}

// ---------------------------------------------------------------------------
// Hilo de trabajo
// ---------------------------------------------------------------------------

void XmppConnection::_thread_trampoline(void *p_user) {
	XmppConnection *self = static_cast<XmppConnection *>(p_user);
	self->_thread_main();
}

void XmppConnection::_drain_outgoing() {
	List<String> local;
	outgoing_mutex.lock();
	while (!outgoing.empty()) {
		local.push_back(outgoing.front()->get());
		outgoing.pop_front();
	}
	outgoing_mutex.unlock();

	for (List<String>::Element *e = local.front(); e; e = e->next()) {
		CharString utf8 = e->get().utf8();
		xmpp_send_raw(conn, utf8.get_data(), utf8.length());
	}
}

void XmppConnection::_thread_main() {
	const int gen = generation;

	CharString jid_u = jid.utf8();
	CharString pass_u = password.utf8();
	CharString host_u = host.utf8();
	CharString cafile_u = cafile.utf8();

	if (ctx == nullptr) {
		call_deferred("_emit_disconnected", (int)XMPP_EINT);
		call_deferred("_on_worker_finished", gen);
		return;
	}

	conn = xmpp_conn_new(ctx);
	if (conn == nullptr) {
		call_deferred("_emit_disconnected", (int)XMPP_EINT);
		call_deferred("_on_worker_finished", gen);
		return;
	}

	xmpp_conn_set_jid(conn, jid_u.get_data());
	xmpp_conn_set_pass(conn, pass_u.get_data());
	if (cafile_u.length() > 0) {
		xmpp_conn_set_cafile(conn, cafile_u.get_data());
	}
	// TLS obligatorio por defecto: xat no acepta texto plano.
	xmpp_conn_set_flags(conn, (long)flags | (long)XMPP_CONN_FLAG_MANDATORY_TLS);
	// Keepalive TCP: evita que NAT/proxies corten sesiones ociosas.
	xmpp_conn_set_keepalive(conn, 60, 60);

	// XEP-0198: si hay estado de una sesión previa, libstrophe intentará
	// <resume> en vez de un bind nuevo. Debe hacerse antes de conectar.
	if (sm_state != nullptr) {
		if (xmpp_conn_set_sm_state(conn, sm_state) == XMPP_EOK) {
			sm_state = nullptr;
		} else {
			// Contexto/cuenta distinta: se descarta y se hace sesión nueva.
			xmpp_free_sm_state(sm_state);
			sm_state = nullptr;
		}
	}

	const char *host_ptr = host_u.length() > 0 ? host_u.get_data() : nullptr;
	int rc = xmpp_connect_client(conn, host_ptr, (unsigned short)port, _conn_event_cb, this);

	if (rc == XMPP_EOK) {
		while (!stop_requested.is_set() && !worker_disconnected) {
			_drain_outgoing();
			xmpp_run_once(ctx, 50);
		}
		if (!worker_disconnected && xmpp_conn_is_connected(conn)) {
			xmpp_disconnect(conn);
			xmpp_run_once(ctx, 0);
		}
	} else {
		call_deferred("_emit_disconnected", rc);
	}

	// Conservar el estado SM para la próxima reconexión (sólo se entrega si el
	// conn quedó desconectado; si no, devuelve NULL y no hay resume).
	xmpp_sm_state_t *resulting = xmpp_conn_get_sm_state(conn);
	if (resulting != nullptr) {
		if (sm_state != nullptr) {
			xmpp_free_sm_state(sm_state);
		}
		sm_state = resulting;
	}

	xmpp_conn_release(conn);
	conn = nullptr;

	call_deferred("_on_worker_finished", gen);
}

// ---------------------------------------------------------------------------
// Callbacks de libstrophe (se ejecutan en el hilo de trabajo)
// ---------------------------------------------------------------------------

void XmppConnection::_conn_event_cb(xmpp_conn_t *p_conn, xmpp_conn_event_t p_event, int p_error, xmpp_stream_error_t *p_stream_error, void *p_userdata) {
	(void)p_stream_error;
	XmppConnection *self = static_cast<XmppConnection *>(p_userdata);
	if (self == nullptr) {
		return;
	}
	switch (p_event) {
		case XMPP_CONN_CONNECT: {
			// Todos los stanzas crudos al hilo principal.
			xmpp_handler_add(p_conn, _stanza_cb, nullptr, nullptr, nullptr, p_userdata);
			const char *bound = xmpp_conn_get_bound_jid(p_conn);
			self->call_deferred("_emit_connected", String::utf8(bound != nullptr ? bound : ""));
		} break;
		case XMPP_CONN_DISCONNECT:
		case XMPP_CONN_FAIL: {
			self->worker_disconnected = true;
			self->call_deferred("_emit_disconnected", p_error);
		} break;
		default:
			break;
	}
}

int XmppConnection::_stanza_cb(xmpp_conn_t *p_conn, xmpp_stanza_t *p_stanza, void *p_userdata) {
	XmppConnection *self = static_cast<XmppConnection *>(p_userdata);
	if (self == nullptr) {
		return 0;
	}
	char *buf = nullptr;
	size_t len = 0;
	if (xmpp_stanza_to_text(p_stanza, &buf, &len) == XMPP_EOK && buf != nullptr) {
		self->call_deferred("_emit_stanza", String::utf8(buf));
		xmpp_free(xmpp_conn_get_context(p_conn), buf);
	}
	return 1;
}

void XmppConnection::_log_cb(void *p_userdata, xmpp_log_level_t p_level, const char *p_area, const char *p_msg) {
	(void)p_area;
	XmppConnection *self = static_cast<XmppConnection *>(p_userdata);
	if (self != nullptr && p_msg != nullptr) {
		self->call_deferred("_emit_log", (int)p_level, String::utf8(p_msg));
	}
}

// ---------------------------------------------------------------------------
// Marshalling al hilo principal
// ---------------------------------------------------------------------------

void XmppConnection::_emit_stanza(const String &p_xml) {
	emit_signal("stanza_received", p_xml);
}

void XmppConnection::_emit_connected(const String &p_bound_jid) {
	connected_flag.set();
	emit_signal("connected", p_bound_jid);
}

void XmppConnection::_emit_disconnected(int p_error) {
	connected_flag.clear();
	emit_signal("disconnected", p_error);
}

void XmppConnection::_emit_log(int p_level, const String &p_msg) {
	emit_signal("log", p_level, p_msg);
}

void XmppConnection::_on_worker_finished(int p_generation) {
	// Sólo limpia si corresponde a la corrida vigente: si ya se reabrió,
	// no debe pisar el estado nuevo.
	if (p_generation == generation) {
		worker_active.clear();
		connected_flag.clear();
	}
}
