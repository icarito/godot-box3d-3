/*************************************************************************/
/*  omemo_crypto.cpp                                                     */
/*************************************************************************/
/* Implementación de las primitivas de OMEMO (XEP-0384).                  */
/*************************************************************************/

#include "omemo_crypto.h"

#include <stdlib.h>
#include <string.h>

#include <mbedtls/aes.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/md.h>

#include "monocypher.h"
#include "monocypher-ed25519.h"

namespace OmemoCrypto {

namespace {

const char *RNG_PERSONALIZATION = "xat-omemo";
const uint8_t ZERO_SALT[32] = { 0 };
const char OMEMO_PAYLOAD_INFO[] = "OMEMO Payload";

mbedtls_entropy_context g_entropy;
mbedtls_ctr_drbg_context g_drbg;
bool g_rng_ready = false;

bool ensure_rng() {
	if (g_rng_ready) {
		return true;
	}
	mbedtls_entropy_init(&g_entropy);
	mbedtls_ctr_drbg_init(&g_drbg);
	int ret = mbedtls_ctr_drbg_seed(&g_drbg, mbedtls_entropy_func, &g_entropy,
			(const unsigned char *)RNG_PERSONALIZATION, strlen(RNG_PERSONALIZATION));
	if (ret != 0) {
		return false;
	}
	g_rng_ready = true;
	return true;
}

// Comparación en tiempo constante (evita filtrar por timing).
bool ct_equal(const uint8_t *a, const uint8_t *b, size_t len) {
	uint8_t diff = 0;
	for (size_t i = 0; i < len; i++) {
		diff |= (uint8_t)(a[i] ^ b[i]);
	}
	return diff == 0;
}

size_t varint_len(uint64_t value) {
	size_t n = 1;
	while (value >= 0x80) {
		value >>= 7;
		n++;
	}
	return n;
}

size_t put_varint(uint8_t *out, uint64_t value) {
	size_t i = 0;
	do {
		uint8_t byte = value & 0x7f;
		value >>= 7;
		if (value) {
			byte |= 0x80;
		}
		out[i++] = byte;
	} while (value);
	return i;
}

// Lee un varint. Devuelve false si se corta.
bool read_varint(const uint8_t *buf, size_t len, size_t *offset, uint64_t *out) {
	uint64_t value = 0;
	int shift = 0;
	size_t i = *offset;
	while (i < len && shift < 64) {
		uint8_t byte = buf[i++];
		value |= (uint64_t)(byte & 0x7f) << shift;
		if ((byte & 0x80) == 0) {
			*offset = i;
			*out = value;
			return true;
		}
		shift += 7;
	}
	return false;
}

// Recorre el protobuf y guarda la última aparición de cada campo pedido.
struct PbField {
	const uint8_t *data;
	size_t len;
};

// Devuelve true si encontró al menos un campo; rellena `want[i]` (0 = ignorar).
bool scan_fields(const uint8_t *buf, size_t len, int count, int *fields, PbField *out) {
	size_t i = 0;
	while (i < len) {
		uint64_t tag;
		if (!read_varint(buf, len, &i, &tag)) {
			return false;
		}
		int field = (int)(tag >> 3);
		int wire = (int)(tag & 7);
		if (wire == 0) {
			uint64_t ignored;
			if (!read_varint(buf, len, &i, &ignored)) {
				return false;
			}
		} else if (wire == 2) {
			uint64_t flen;
			if (!read_varint(buf, len, &i, &flen)) {
				return false;
			}
			if (i + flen > len) {
				return false;
			}
			for (int k = 0; k < count; k++) {
				if (fields[k] == field) {
					out[k].data = buf + i;
					out[k].len = (size_t)flen;
				}
			}
			i += (size_t)flen;
		} else {
			return false;
		}
	}
	return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Aleatoriedad
// ---------------------------------------------------------------------------

bool random_bytes(uint8_t *out, size_t len) {
	if (!ensure_rng()) {
		return false;
	}
	return mbedtls_ctr_drbg_random(&g_drbg, out, len) == 0;
}

// ---------------------------------------------------------------------------
// Curva25519
// ---------------------------------------------------------------------------

void x25519_public_key(uint8_t public_key[32], const uint8_t secret_key[32]) {
	crypto_x25519_public_key(public_key, secret_key);
}

void x25519_shared(uint8_t shared[32], const uint8_t secret_key[32], const uint8_t peer_public[32]) {
	crypto_x25519(shared, secret_key, peer_public);
}

void ed25519_key_pair(uint8_t secret_key[64], uint8_t public_key[32], const uint8_t seed[32]) {
	uint8_t local_seed[32];
	memcpy(local_seed, seed, 32);
	crypto_ed25519_key_pair(secret_key, public_key, local_seed);
}

void ed25519_sign(uint8_t signature[64], const uint8_t secret_key[64], const uint8_t *msg, size_t msg_len) {
	crypto_ed25519_sign(signature, secret_key, msg, msg_len);
}

bool ed25519_check(const uint8_t signature[64], const uint8_t public_key[32], const uint8_t *msg, size_t msg_len) {
	return crypto_ed25519_check(signature, public_key, msg, msg_len) == 0;
}

void ed25519_to_x25519(uint8_t x25519[32], const uint8_t ed25519[32]) {
	crypto_eddsa_to_x25519(x25519, ed25519);
}

void ed25519_seed_to_x25519_secret(uint8_t secret[32], const uint8_t seed[32]) {
	uint8_t expanded[64];
	crypto_sha512(expanded, seed, 32);
	memcpy(secret, expanded, 32);
	// Recorte X25519 (RFC 7748).
	secret[0] &= 248;
	secret[31] &= 127;
	secret[31] |= 64;
}

void sha512(uint8_t out[64], const uint8_t *msg, size_t len) {
	crypto_sha512(out, msg, len);
}

// ---------------------------------------------------------------------------
// HKDF / AES-CBC / HMAC
// ---------------------------------------------------------------------------

void hkdf_sha256(uint8_t *out, size_t out_len, const uint8_t *ikm, size_t ikm_len,
		const uint8_t *salt, size_t salt_len, const uint8_t *info, size_t info_len) {
	const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
	mbedtls_hkdf(md, salt, salt_len, ikm, ikm_len, info, info_len, out, out_len);
}

bool aes256cbc_encrypt(uint8_t *out, size_t *out_len, const uint8_t key[32], const uint8_t iv[16], const uint8_t *in, size_t in_len) {
	size_t pad = 16 - (in_len % 16);
	size_t total = in_len + pad;
	memcpy(out, in, in_len);
	for (size_t i = 0; i < pad; i++) {
		out[in_len + i] = (uint8_t)pad;
	}
	mbedtls_aes_context ctx;
	mbedtls_aes_init(&ctx);
	mbedtls_aes_setkey_enc(&ctx, key, 256);
	uint8_t iv_copy[16];
	memcpy(iv_copy, iv, 16);
	int ret = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, total, iv_copy, out, out);
	mbedtls_aes_free(&ctx);
	if (ret != 0) {
		return false;
	}
	*out_len = total;
	return true;
}

bool aes256cbc_decrypt(uint8_t *out, size_t *out_len, const uint8_t key[32], const uint8_t iv[16], const uint8_t *in, size_t in_len) {
	if (in_len == 0 || (in_len % 16) != 0) {
		return false;
	}
	mbedtls_aes_context ctx;
	mbedtls_aes_init(&ctx);
	mbedtls_aes_setkey_dec(&ctx, key, 256);
	uint8_t iv_copy[16];
	memcpy(iv_copy, iv, 16);
	int ret = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT, in_len, iv_copy, in, out);
	mbedtls_aes_free(&ctx);
	if (ret != 0) {
		return false;
	}
	uint8_t pad = out[in_len - 1];
	if (pad == 0 || pad > 16 || pad > in_len) {
		return false;
	}
	for (size_t i = 0; i < pad; i++) {
		if (out[in_len - 1 - i] != pad) {
			return false;
		}
	}
	*out_len = in_len - pad;
	return true;
}

void hmac_sha256(uint8_t out[32], const uint8_t *key, size_t key_len, const uint8_t *msg, size_t msg_len) {
	const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
	mbedtls_md_hmac(md, key, key_len, msg, msg_len, out);
}

// ---------------------------------------------------------------------------
// X3DH (XEP-0384 ap. 4.2, identidad Ed25519)
// ---------------------------------------------------------------------------

namespace {

const char OMEMO_X3DH_INFO[] = "OMEMO X3DH";

// ss = HKDF-SHA-256(salt=0, info="OMEMO X3DH", ikm = 0xFF*32 || dh1..dh4).
void x3dh_derive(uint8_t ss[32], const uint8_t dh[4][32]) {
	uint8_t ikm[32 + 32 * 4];
	memset(ikm, 0xFF, 32);
	for (int i = 0; i < 4; i++) {
		memcpy(ikm + 32 + i * 32, dh[i], 32);
	}
	uint8_t salt[32] = { 0 };
	hkdf_sha256(ss, 32, ikm, sizeof(ikm), salt, sizeof(salt), (const uint8_t *)OMEMO_X3DH_INFO, sizeof(OMEMO_X3DH_INFO) - 1);
}

} // namespace

bool x3dh_active(uint8_t ss[32], uint8_t ad[64], uint8_t header_ek[32], uint8_t ephemeral_priv[32],
		const uint8_t own_seed[32],
		const uint8_t other_ik_ed[32], const uint8_t other_spk[32],
		const uint8_t other_spk_sig[64], const uint8_t other_prekey[32],
		const uint8_t *p_ephemeral) {
	if (!ed25519_check(other_spk_sig, other_ik_ed, other_spk, 32)) {
		return false;
	}
	if (p_ephemeral != nullptr) {
		memcpy(ephemeral_priv, p_ephemeral, 32);
	} else if (!random_bytes(ephemeral_priv, 32)) {
		return false;
	}
	x25519_public_key(header_ek, ephemeral_priv);

	uint8_t own_ik_priv[32], own_ik_ed[32], own_secret[64];
	ed25519_seed_to_x25519_secret(own_ik_priv, own_seed);
	ed25519_key_pair(own_secret, own_ik_ed, own_seed);
	uint8_t other_ik_x[32];
	ed25519_to_x25519(other_ik_x, other_ik_ed);

	uint8_t dh[4][32];
	x25519_shared(dh[0], own_ik_priv, other_spk);
	x25519_shared(dh[1], ephemeral_priv, other_ik_x);
	x25519_shared(dh[2], ephemeral_priv, other_spk);
	x25519_shared(dh[3], ephemeral_priv, other_prekey);
	x3dh_derive(ss, dh);

	memcpy(ad, own_ik_ed, 32);
	memcpy(ad + 32, other_ik_ed, 32);
	return true;
}

bool x3dh_passive(uint8_t ss[32], uint8_t ad[64],
		const uint8_t own_seed[32], const uint8_t own_spk_priv[32], const uint8_t own_prekey_priv[32],
		const uint8_t header_ik[32], const uint8_t header_ek[32]) {
	uint8_t own_ik_priv[32], own_ik_ed[32], own_secret[64];
	ed25519_seed_to_x25519_secret(own_ik_priv, own_seed);
	ed25519_key_pair(own_secret, own_ik_ed, own_seed);
	uint8_t other_ik_x[32];
	ed25519_to_x25519(other_ik_x, header_ik);

	uint8_t dh[4][32];
	x25519_shared(dh[0], own_spk_priv, other_ik_x);
	x25519_shared(dh[1], own_ik_priv, header_ek);
	x25519_shared(dh[2], own_spk_priv, header_ek);
	x25519_shared(dh[3], own_prekey_priv, header_ek);
	x3dh_derive(ss, dh);

	memcpy(ad, header_ik, 32);
	memcpy(ad + 32, own_ik_ed, 32);
	return true;
}

// ---------------------------------------------------------------------------
// Payload OMEMO 2
// ---------------------------------------------------------------------------

bool payload_encrypt_with_key(const uint8_t key32[32], uint8_t *key48, uint8_t **payload, size_t *payload_len,
		const uint8_t *plaintext, size_t plaintext_len) {
	uint8_t okm[80];
	hkdf_sha256(okm, sizeof(okm), key32, 32, ZERO_SALT, sizeof(ZERO_SALT),
			(const uint8_t *)OMEMO_PAYLOAD_INFO, sizeof(OMEMO_PAYLOAD_INFO) - 1);

	uint8_t *ct = (uint8_t *)malloc(plaintext_len + 16);
	if (ct == nullptr) {
		return false;
	}
	size_t ct_len = 0;
	if (!aes256cbc_encrypt(ct, &ct_len, okm, okm + 64, plaintext, plaintext_len)) {
		free(ct);
		return false;
	}

	uint8_t mac[32];
	hmac_sha256(mac, okm + 32, 32, ct, ct_len);

	memcpy(key48, key32, 32);
	memcpy(key48 + 32, mac, 16);
	// XEP-0384 carries only the AES-CBC ciphertext in <payload>. The truncated
	// HMAC travels with the key material inside each ratchet-encrypted <key>.
	*payload = ct;
	*payload_len = ct_len;
	return true;
}

bool payload_encrypt(uint8_t *key48, uint8_t **payload, size_t *payload_len,
		const uint8_t *plaintext, size_t plaintext_len) {
	uint8_t key32[32];
	if (!random_bytes(key32, sizeof(key32))) {
		return false;
	}
	return payload_encrypt_with_key(key32, key48, payload, payload_len, plaintext, plaintext_len);
}

bool payload_decrypt(uint8_t **plaintext, size_t *plaintext_len,
		const uint8_t *payload, size_t payload_len,
		const uint8_t *key, size_t key_len) {
	if (key_len != 48 || payload_len == 0) {
		return false;
	}

	uint8_t okm[80];
	hkdf_sha256(okm, sizeof(okm), key, 32, ZERO_SALT, sizeof(ZERO_SALT),
			(const uint8_t *)OMEMO_PAYLOAD_INFO, sizeof(OMEMO_PAYLOAD_INFO) - 1);

	uint8_t expected[32];
	hmac_sha256(expected, okm + 32, 32, payload, payload_len);
	if (!ct_equal(key + 32, expected, 16)) {
		return false;
	}

	uint8_t *out = (uint8_t *)malloc(payload_len);
	if (out == nullptr) {
		return false;
	}
	size_t out_len = 0;
	if (!aes256cbc_decrypt(out, &out_len, okm, okm + 64, payload, payload_len)) {
		free(out);
		return false;
	}
	*plaintext = out;
	*plaintext_len = out_len;
	return true;
}

// ---------------------------------------------------------------------------
// Protobuf
// ---------------------------------------------------------------------------

void pb_encode_authenticated(uint8_t **out, size_t *out_len,
		const uint8_t *ciphertext, size_t ct_len, const uint8_t mac[16],
		uint32_t n, uint32_t pn, const uint8_t *dh_pub, size_t dh_len) {
	size_t inner_len = (size_t)1 + varint_len(n) + (size_t)1 + varint_len(pn) + (size_t)1 + varint_len(dh_len) + dh_len + (size_t)1 + varint_len(ct_len) + ct_len;
	size_t total = (size_t)1 + varint_len(16) + 16 + (size_t)1 + varint_len(inner_len) + inner_len;
	uint8_t *buf = (uint8_t *)malloc(total);
	if (buf == nullptr) {
		*out = nullptr;
		*out_len = 0;
		return;
	}
	size_t i = 0;
	buf[i++] = (uint8_t)((1 << 3) | 2); // mac
	i += put_varint(buf + i, 16);
	memcpy(buf + i, mac, 16);
	i += 16;
	buf[i++] = (uint8_t)((2 << 3) | 2); // message
	i += put_varint(buf + i, inner_len);
	buf[i++] = (uint8_t)(1 << 3); // n
	i += put_varint(buf + i, n);
	buf[i++] = (uint8_t)(2 << 3); // pn
	i += put_varint(buf + i, pn);
	buf[i++] = (uint8_t)((3 << 3) | 2); // dh_pub
	i += put_varint(buf + i, dh_len);
	if (dh_len) {
		memcpy(buf + i, dh_pub, dh_len);
		i += dh_len;
	}
	buf[i++] = (uint8_t)((4 << 3) | 2); // ciphertext
	i += put_varint(buf + i, ct_len);
	memcpy(buf + i, ciphertext, ct_len);
	i += ct_len;
	*out = buf;
	*out_len = i;
}

bool pb_decode_authenticated(const uint8_t *buf, size_t len,
		const uint8_t **mac, size_t *mac_len,
		const uint8_t **ciphertext, size_t *ct_len,
		uint32_t *n, uint32_t *pn,
		const uint8_t **dh_pub, size_t *dh_len) {
	int outer_fields[2] = { 1, 2 };
	PbField outer[2] = { { nullptr, 0 }, { nullptr, 0 } };
	if (!scan_fields(buf, len, 2, outer_fields, outer) || outer[0].data == nullptr || outer[1].data == nullptr) {
		return false;
	}
	int inner_fields[4] = { 1, 2, 3, 4 };
	PbField inner[4] = { { nullptr, 0 }, { nullptr, 0 }, { nullptr, 0 }, { nullptr, 0 } };
	if (!scan_fields(outer[1].data, outer[1].len, 4, inner_fields, inner) || inner[3].data == nullptr) {
		return false;
	}
	*mac = outer[0].data;
	*mac_len = outer[0].len;
	*ciphertext = inner[3].data;
	*ct_len = inner[3].len;

	// n/pn son varints: hay que releerlos (scan_fields sólo captura bytes).
	uint64_t value = 0;
	size_t i = 0;
	*n = 0;
	*pn = 0;
	while (i < outer[1].len) {
		uint64_t tag;
		if (!read_varint(outer[1].data, outer[1].len, &i, &tag)) {
			break;
		}
		int field = (int)(tag >> 3);
		int wire = (int)(tag & 7);
		if (wire == 0) {
			if (!read_varint(outer[1].data, outer[1].len, &i, &value)) {
				break;
			}
			if (field == 1) {
				*n = (uint32_t)value;
			} else if (field == 2) {
				*pn = (uint32_t)value;
			}
		} else if (wire == 2) {
			uint64_t flen;
			if (!read_varint(outer[1].data, outer[1].len, &i, &flen)) {
				break;
			}
			i += (size_t)flen;
		} else {
			break;
		}
	}

	*dh_pub = inner[2].data;
	*dh_len = inner[2].len;
	return true;
}

} // namespace OmemoCrypto
