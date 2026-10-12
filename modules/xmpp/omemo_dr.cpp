/*************************************************************************/
/*  omemo_dr.cpp                                                         */
/*************************************************************************/
/* Double Ratchet de OMEMO 2.                                            */
/*************************************************************************/

#include "omemo_dr.h"

#include <stdlib.h>
#include <string.h>

#include "omemo_crypto.h"

namespace OmemoDr {

namespace {

const uint32_t MAX_SKIP = 1000;
const char INFO_ROOT[] = "OMEMO Root Chain";
const char INFO_MESSAGE[] = "OMEMO Message Key Material";

const uint8_t MAGIC[4] = { 'X', 'D', 'R', '1' };

struct SkippedKey {
	uint8_t pub[32];
	uint32_t idx;
	uint8_t mk[32];
};

} // namespace

struct State {
	uint8_t root[32];
	uint8_t own_ratchet_priv[32];
	uint8_t other_ratchet_pub[32];
	bool has_sending;
	uint8_t sending_ck[32];
	uint32_t sending_len;
	bool has_receiving;
	uint8_t receiving_ck[32];
	uint32_t receiving_len;
	uint32_t prev_sending_len;
	SkippedKey *skipped;
	uint32_t skipped_count;
};

// ---------------------------------------------------------------------------
// Protobuf (OMEMOMessage / OMEMOAuthenticatedMessage)
// ---------------------------------------------------------------------------

namespace {

size_t pb_varint_len(uint64_t v) {
	size_t n = 1;
	while (v >= 0x80) {
		v >>= 7;
		n++;
	}
	return n;
}

size_t pb_put_varint(uint8_t *out, uint64_t v) {
	size_t i = 0;
	do {
		uint8_t b = v & 0x7f;
		v >>= 7;
		if (v) {
			b |= 0x80;
		}
		out[i++] = b;
	} while (v);
	return i;
}

bool pb_read_varint(const uint8_t *buf, size_t len, size_t *off, uint64_t *out) {
	uint64_t v = 0;
	int shift = 0;
	size_t i = *off;
	while (i < len && shift < 64) {
		uint8_t b = buf[i++];
		v |= (uint64_t)(b & 0x7f) << shift;
		if ((b & 0x80) == 0) {
			*off = i;
			*out = v;
			return true;
		}
		shift += 7;
	}
	return false;
}

// OMEMOMessage{n=1, pn=2, dh_pub=3, ciphertext=4}
void pb_omemo_message(uint8_t **out, size_t *out_len, uint32_t n, uint32_t pn,
		const uint8_t *dh_pub, size_t dh_len, const uint8_t *ct, size_t ct_len) {
	size_t total = (size_t)1 + pb_varint_len(n) + (size_t)1 + pb_varint_len(pn) +
			(size_t)1 + pb_varint_len(dh_len) + dh_len + (size_t)1 + pb_varint_len(ct_len) + ct_len;
	uint8_t *buf = (uint8_t *)malloc(total);
	size_t i = 0;
	buf[i++] = (uint8_t)(1 << 3);
	i += pb_put_varint(buf + i, n);
	buf[i++] = (uint8_t)(2 << 3);
	i += pb_put_varint(buf + i, pn);
	buf[i++] = (uint8_t)((3 << 3) | 2);
	i += pb_put_varint(buf + i, dh_len);
	if (dh_len) {
		memcpy(buf + i, dh_pub, dh_len);
		i += dh_len;
	}
	buf[i++] = (uint8_t)((4 << 3) | 2);
	i += pb_put_varint(buf + i, ct_len);
	memcpy(buf + i, ct, ct_len);
	i += ct_len;
	*out = buf;
	*out_len = i;
}

// OMEMOAuthenticatedMessage{mac=1, message=2}
void pb_authenticated(uint8_t **out, size_t *out_len, const uint8_t mac[16], const uint8_t *msg, size_t msg_len) {
	size_t total = (size_t)1 + pb_varint_len(16) + 16 + (size_t)1 + pb_varint_len(msg_len) + msg_len;
	uint8_t *buf = (uint8_t *)malloc(total);
	size_t i = 0;
	buf[i++] = (uint8_t)((1 << 3) | 2);
	i += pb_put_varint(buf + i, 16);
	memcpy(buf + i, mac, 16);
	i += 16;
	buf[i++] = (uint8_t)((2 << 3) | 2);
	i += pb_put_varint(buf + i, msg_len);
	memcpy(buf + i, msg, msg_len);
	i += msg_len;
	*out = buf;
	*out_len = i;
}

// Separa OMEMOAuthenticatedMessage en mac y el OMEMOMessage crudo.
bool pb_split_authenticated(const uint8_t *buf, size_t len, const uint8_t **mac, size_t *mac_len,
		const uint8_t **msg, size_t *msg_len) {
	*mac = nullptr;
	*msg = nullptr;
	size_t i = 0;
	while (i < len) {
		uint64_t tag;
		if (!pb_read_varint(buf, len, &i, &tag)) {
			return false;
		}
		int field = (int)(tag >> 3);
		int wire = (int)(tag & 7);
		if (wire != 2) {
			return false;
		}
		uint64_t flen;
		if (!pb_read_varint(buf, len, &i, &flen) || i + flen > len) {
			return false;
		}
		if (field == 1) {
			*mac = buf + i;
			*mac_len = (size_t)flen;
		} else if (field == 2) {
			*msg = buf + i;
			*msg_len = (size_t)flen;
		}
		i += (size_t)flen;
	}
	return *mac != nullptr && *msg != nullptr;
}

// Parsea OMEMOMessage.
bool pb_parse_omemo_message(const uint8_t *buf, size_t len, uint32_t *n, uint32_t *pn,
		const uint8_t **dh_pub, size_t *dh_len, const uint8_t **ct, size_t *ct_len) {
	*n = 0;
	*pn = 0;
	*dh_pub = nullptr;
	*dh_len = 0;
	*ct = nullptr;
	*ct_len = 0;
	size_t i = 0;
	while (i < len) {
		uint64_t tag;
		if (!pb_read_varint(buf, len, &i, &tag)) {
			return false;
		}
		int field = (int)(tag >> 3);
		int wire = (int)(tag & 7);
		if (wire == 0) {
			uint64_t v;
			if (!pb_read_varint(buf, len, &i, &v)) {
				return false;
			}
			if (field == 1) {
				*n = (uint32_t)v;
			} else if (field == 2) {
				*pn = (uint32_t)v;
			}
		} else if (wire == 2) {
			uint64_t flen;
			if (!pb_read_varint(buf, len, &i, &flen) || i + flen > len) {
				return false;
			}
			if (field == 3) {
				*dh_pub = buf + i;
				*dh_len = (size_t)flen;
			} else if (field == 4) {
				*ct = buf + i;
				*ct_len = (size_t)flen;
			}
			i += (size_t)flen;
		} else {
			return false;
		}
	}
	return *ct != nullptr && *dh_len == 32;
}

} // namespace

// ---------------------------------------------------------------------------
// AEAD de mensaje
// ---------------------------------------------------------------------------

bool aead_encrypt(uint8_t **p_out, size_t *p_out_len, const uint8_t p_message_key[32],
		const uint8_t *p_plain, size_t p_plain_len, const uint8_t p_associated_data[64],
		uint32_t p_n, uint32_t p_pn, const uint8_t p_dh_pub[32]) {
	uint8_t okm[80];
	uint8_t zero[32] = { 0 };
	OmemoCrypto::hkdf_sha256(okm, 80, p_message_key, 32, zero, 32, (const uint8_t *)INFO_MESSAGE, sizeof(INFO_MESSAGE) - 1);

	uint8_t *ct = (uint8_t *)malloc(p_plain_len + 16);
	if (ct == nullptr) {
		return false;
	}
	size_t ct_len = 0;
	if (!OmemoCrypto::aes256cbc_encrypt(ct, &ct_len, okm, okm + 64, p_plain, p_plain_len)) {
		free(ct);
		return false;
	}
	uint8_t *om = nullptr;
	size_t om_len = 0;
	pb_omemo_message(&om, &om_len, p_n, p_pn, p_dh_pub, 32, ct, ct_len);
	free(ct);

	uint8_t *mac_in = (uint8_t *)malloc(64 + om_len);
	memcpy(mac_in, p_associated_data, 64);
	memcpy(mac_in + 64, om, om_len);
	uint8_t mac32[32];
	OmemoCrypto::hmac_sha256(mac32, okm + 32, 32, mac_in, 64 + om_len);
	free(mac_in);

	pb_authenticated(p_out, p_out_len, mac32, om, om_len);
	free(om);
	return *p_out != nullptr;
}

bool aead_decrypt(uint8_t **p_plain, size_t *p_plain_len, const uint8_t *p_in, size_t p_in_len,
		const uint8_t p_message_key[32], const uint8_t p_associated_data[64],
		uint32_t *p_n, uint32_t *p_pn, uint8_t p_dh_pub[32]) {
	const uint8_t *mac = nullptr, *msg = nullptr;
	size_t mac_len = 0, msg_len = 0;
	if (!pb_split_authenticated(p_in, p_in_len, &mac, &mac_len, &msg, &msg_len) || mac_len != 16) {
		return false;
	}
	const uint8_t *dh_pub = nullptr, *ct = nullptr;
	size_t dh_len = 0, ct_len = 0;
	if (!pb_parse_omemo_message(msg, msg_len, p_n, p_pn, &dh_pub, &dh_len, &ct, &ct_len)) {
		return false;
	}

	uint8_t okm[80];
	uint8_t zero[32] = { 0 };
	OmemoCrypto::hkdf_sha256(okm, 80, p_message_key, 32, zero, 32, (const uint8_t *)INFO_MESSAGE, sizeof(INFO_MESSAGE) - 1);

	uint8_t *mac_in = (uint8_t *)malloc(64 + msg_len);
	memcpy(mac_in, p_associated_data, 64);
	memcpy(mac_in + 64, msg, msg_len);
	uint8_t expected[32];
	OmemoCrypto::hmac_sha256(expected, okm + 32, 32, mac_in, 64 + msg_len);
	free(mac_in);
	uint8_t diff = 0;
	for (int i = 0; i < 16; i++) {
		diff |= mac[i] ^ expected[i];
	}
	if (diff != 0) {
		return false;
	}

	uint8_t *out = (uint8_t *)malloc(ct_len == 0 ? 1 : ct_len);
	size_t out_len = 0;
	if (!OmemoCrypto::aes256cbc_decrypt(out, &out_len, okm, okm + 64, ct, ct_len)) {
		free(out);
		return false;
	}
	memcpy(p_dh_pub, dh_pub, 32);
	*p_plain = out;
	*p_plain_len = out_len;
	return true;
}

// ---------------------------------------------------------------------------
// Cadenas
// ---------------------------------------------------------------------------

namespace {

void root_step(uint8_t new_root[32], uint8_t chain[32], const uint8_t root[32], const uint8_t dh[32]) {
	uint8_t okm[64];
	OmemoCrypto::hkdf_sha256(okm, 64, dh, 32, root, 32, (const uint8_t *)INFO_ROOT, sizeof(INFO_ROOT) - 1);
	memcpy(new_root, okm, 32);
	memcpy(chain, okm + 32, 32);
}

void msg_step(uint8_t new_ck[32], uint8_t mk[32], const uint8_t ck[32]) {
	uint8_t c02 = 0x02, c01 = 0x01;
	OmemoCrypto::hmac_sha256(new_ck, ck, 32, &c02, 1);
	OmemoCrypto::hmac_sha256(mk, ck, 32, &c01, 1);
}

void dh(uint8_t out[32], const uint8_t priv[32], const uint8_t pub[32]) {
	OmemoCrypto::x25519_shared(out, priv, pub);
}

// Busca una clave salteada; devuelve true y la quita si existe.
bool take_skipped(State *s, const uint8_t pub[32], uint32_t idx, uint8_t mk[32]) {
	for (uint32_t i = 0; i < s->skipped_count; i++) {
		if (s->skipped[i].idx == idx && memcmp(s->skipped[i].pub, pub, 32) == 0) {
			memcpy(mk, s->skipped[i].mk, 32);
			s->skipped[i] = s->skipped[s->skipped_count - 1];
			s->skipped_count--;
			return true;
		}
	}
	return false;
}

void store_skipped(State *s, const uint8_t pub[32], uint32_t idx, const uint8_t mk[32]) {
	if (s->skipped_count >= MAX_SKIP) {
		// Descartar la más vieja (FIFO).
		memmove(s->skipped, s->skipped + 1, sizeof(SkippedKey) * (MAX_SKIP - 1));
		s->skipped_count = MAX_SKIP - 1;
	}
	SkippedKey *k = &s->skipped[s->skipped_count++];
	memcpy(k->pub, pub, 32);
	k->idx = idx;
	memcpy(k->mk, mk, 32);
}

// Deriva la próxima clave del ratchet de mensajes (sending o receiving).
void sending_next(State *s, uint8_t mk[32]) {
	uint8_t new_ck[32];
	msg_step(new_ck, mk, s->sending_ck);
	memcpy(s->sending_ck, new_ck, 32);
	s->sending_len++;
}

void receiving_next(State *s, uint8_t mk[32]) {
	uint8_t new_ck[32];
	msg_step(new_ck, mk, s->receiving_ck);
	memcpy(s->receiving_ck, new_ck, 32);
	s->receiving_len++;
}

} // namespace

// ---------------------------------------------------------------------------
// Ciclo de vida y serialización
// ---------------------------------------------------------------------------

State *state_new() {
	State *s = (State *)calloc(1, sizeof(State));
	s->skipped = (SkippedKey *)calloc(MAX_SKIP, sizeof(SkippedKey));
	return s;
}

void state_free(State *s) {
	if (s == nullptr) {
		return;
	}
	free(s->skipped);
	free(s);
}

size_t state_size(const State *s) {
	size_t n = 4 + 32 + 32 + 32 + 1 + 1 + 4 + 2;
	if (s->has_sending) {
		n += 32 + 4;
	}
	if (s->has_receiving) {
		n += 32 + 4;
	}
	n += (size_t)s->skipped_count * (32 + 4 + 32);
	return n;
}

namespace {

void put_u32(uint8_t *out, uint32_t v) {
	out[0] = v & 0xff;
	out[1] = (v >> 8) & 0xff;
	out[2] = (v >> 16) & 0xff;
	out[3] = (v >> 24) & 0xff;
}

uint32_t get_u32(const uint8_t *in) {
	return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

} // namespace

void state_serialize(const State *s, uint8_t *out) {
	size_t i = 0;
	memcpy(out + i, MAGIC, 4);
	i += 4;
	memcpy(out + i, s->root, 32);
	i += 32;
	memcpy(out + i, s->own_ratchet_priv, 32);
	i += 32;
	memcpy(out + i, s->other_ratchet_pub, 32);
	i += 32;
	out[i++] = s->has_sending ? 1 : 0;
	if (s->has_sending) {
		memcpy(out + i, s->sending_ck, 32);
		i += 32;
		put_u32(out + i, s->sending_len);
		i += 4;
	}
	out[i++] = s->has_receiving ? 1 : 0;
	if (s->has_receiving) {
		memcpy(out + i, s->receiving_ck, 32);
		i += 32;
		put_u32(out + i, s->receiving_len);
		i += 4;
	}
	put_u32(out + i, s->prev_sending_len);
	i += 4;
	out[i++] = s->skipped_count & 0xff;
	out[i++] = (s->skipped_count >> 8) & 0xff;
	for (uint32_t k = 0; k < s->skipped_count; k++) {
		memcpy(out + i, s->skipped[k].pub, 32);
		i += 32;
		put_u32(out + i, s->skipped[k].idx);
		i += 4;
		memcpy(out + i, s->skipped[k].mk, 32);
		i += 32;
	}
}

bool state_deserialize(State *s, const uint8_t *in, size_t len) {
	size_t i = 0;
	if (len < 4 + 32 + 32 + 32 + 1 + 1 + 4 + 2 || memcmp(in, MAGIC, 4) != 0) {
		return false;
	}
	i += 4;
	memcpy(s->root, in + i, 32);
	i += 32;
	memcpy(s->own_ratchet_priv, in + i, 32);
	i += 32;
	memcpy(s->other_ratchet_pub, in + i, 32);
	i += 32;
	s->has_sending = in[i++] != 0;
	if (s->has_sending) {
		memcpy(s->sending_ck, in + i, 32);
		i += 32;
		s->sending_len = get_u32(in + i);
		i += 4;
	}
	s->has_receiving = in[i++] != 0;
	if (s->has_receiving) {
		memcpy(s->receiving_ck, in + i, 32);
		i += 32;
		s->receiving_len = get_u32(in + i);
		i += 4;
	}
	s->prev_sending_len = get_u32(in + i);
	i += 4;
	uint32_t count = (uint32_t)in[i] | ((uint32_t)in[i + 1] << 8);
	i += 2;
	if (count > MAX_SKIP || i + (size_t)count * 68 > len) {
		return false;
	}
	s->skipped_count = count;
	for (uint32_t k = 0; k < count; k++) {
		memcpy(s->skipped[k].pub, in + i, 32);
		i += 32;
		s->skipped[k].idx = get_u32(in + i);
		i += 4;
		memcpy(s->skipped[k].mk, in + i, 32);
		i += 32;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Inicio y mensajes
// ---------------------------------------------------------------------------

bool start_active(State *s, const uint8_t p_shared_secret[32], const uint8_t p_recipient_ratchet_pub[32],
		const uint8_t p_associated_data[64], const uint8_t *p_plain, size_t p_plain_len,
		uint8_t **p_ciphertext, size_t *p_ciphertext_len) {
	memcpy(s->root, p_shared_secret, 32);
	memcpy(s->other_ratchet_pub, p_recipient_ratchet_pub, 32);
	if (!OmemoCrypto::random_bytes(s->own_ratchet_priv, 32)) {
		return false;
	}
	uint8_t dh_out[32];
	dh(dh_out, s->own_ratchet_priv, s->other_ratchet_pub);
	root_step(s->root, s->sending_ck, s->root, dh_out);
	s->has_sending = true;
	s->sending_len = 0;
	s->prev_sending_len = 0;

	uint8_t mk[32];
	uint8_t dh_pub[32];
	OmemoCrypto::x25519_public_key(dh_pub, s->own_ratchet_priv);
	uint32_t n = s->sending_len;
	uint32_t pn = s->prev_sending_len;
	sending_next(s, mk);
	return aead_encrypt(p_ciphertext, p_ciphertext_len, mk, p_plain, p_plain_len, p_associated_data, n, pn, dh_pub);
}

bool start_passive(State *s, const uint8_t p_shared_secret[32], const uint8_t p_own_ratchet_priv[32],
		const uint8_t p_associated_data[64], const uint8_t *p_ciphertext, size_t p_ciphertext_len,
		uint8_t **p_plain, size_t *p_plain_len) {
	memcpy(s->root, p_shared_secret, 32);
	memcpy(s->own_ratchet_priv, p_own_ratchet_priv, 32);

	// Receive chain con el ratchet pub del activo (está dentro del ciphertext).
	uint8_t tmp_plain_key[32]; // no se usa; sólo parseamos el header del ciphertext
	uint32_t n = 0, pn = 0;
	uint8_t dh_pub[32];
	// Necesitamos dh_pub ANTES de descifrar: parseamos el OMEMOMessage.
	const uint8_t *mac = nullptr, *msg = nullptr;
	size_t mac_len = 0, msg_len = 0;
	if (!pb_split_authenticated(p_ciphertext, p_ciphertext_len, &mac, &mac_len, &msg, &msg_len)) {
		return false;
	}
	const uint8_t *dhp = nullptr, *ct = nullptr;
	size_t dhl = 0, ctl = 0;
	if (!pb_parse_omemo_message(msg, msg_len, &n, &pn, &dhp, &dhl, &ct, &ctl)) {
		return false;
	}
	memcpy(dh_pub, dhp, 32);
	memcpy(s->other_ratchet_pub, dh_pub, 32);
	(void)tmp_plain_key;

	uint8_t dh_out[32];
	dh(dh_out, s->own_ratchet_priv, s->other_ratchet_pub);
	root_step(s->root, s->receiving_ck, s->root, dh_out);
	s->has_receiving = true;
	s->receiving_len = 0;

	if (!OmemoCrypto::random_bytes(s->own_ratchet_priv, 32)) {
		return false;
	}
	dh(dh_out, s->own_ratchet_priv, s->other_ratchet_pub);
	root_step(s->root, s->sending_ck, s->root, dh_out);
	s->has_sending = true;
	s->sending_len = 0;
	s->prev_sending_len = 0;

	// El activo reenvía el kex en cada mensaje hasta recibir respuesta, así
	// que el primero que llega puede ser n > 0 si se perdieron los anteriores.
	// Las claves saltadas quedan guardadas por si llegan tarde.
	if (n > MAX_SKIP) {
		return false;
	}
	uint8_t mk[32];
	for (uint32_t k = 0; k < n; k++) {
		receiving_next(s, mk);
		store_skipped(s, s->other_ratchet_pub, s->receiving_len - 1, mk);
	}
	receiving_next(s, mk);
	uint32_t dn = 0, dpn = 0;
	uint8_t ddh[32];
	return aead_decrypt(p_plain, p_plain_len, p_ciphertext, p_ciphertext_len, mk, p_associated_data, &dn, &dpn, ddh);
}

bool encrypt(State *s, const uint8_t p_associated_data[64],
		const uint8_t *p_plain, size_t p_plain_len, uint8_t **p_ciphertext, size_t *p_ciphertext_len) {
	uint8_t mk[32];
	uint8_t dh_pub[32];
	OmemoCrypto::x25519_public_key(dh_pub, s->own_ratchet_priv);
	uint32_t n = s->sending_len;
	uint32_t pn = s->prev_sending_len;
	sending_next(s, mk);
	return aead_encrypt(p_ciphertext, p_ciphertext_len, mk, p_plain, p_plain_len, p_associated_data, n, pn, dh_pub);
}

bool decrypt(State *s, const uint8_t p_associated_data[64],
		const uint8_t *p_ciphertext, size_t p_ciphertext_len, uint8_t **p_plain, size_t *p_plain_len) {
	const uint8_t *mac = nullptr, *msg = nullptr;
	size_t mac_len = 0, msg_len = 0;
	if (!pb_split_authenticated(p_ciphertext, p_ciphertext_len, &mac, &mac_len, &msg, &msg_len)) {
		return false;
	}
	uint32_t n = 0, pn = 0;
	const uint8_t *dhp = nullptr, *ct = nullptr;
	size_t dhl = 0, ctl = 0;
	if (!pb_parse_omemo_message(msg, msg_len, &n, &pn, &dhp, &dhl, &ct, &ctl)) {
		return false;
	}
	uint8_t pub[32];
	memcpy(pub, dhp, 32);

	uint8_t mk[32];
	if (!take_skipped(s, pub, n, mk)) {
		// ¿Hace falta un paso de ratchet DH?
		if (memcmp(pub, s->other_ratchet_pub, 32) != 0) {
			if (s->has_receiving) {
				uint32_t num_skipped = pn > s->receiving_len ? pn - s->receiving_len : 0;
				if (num_skipped > MAX_SKIP) {
					return false;
				}
				for (uint32_t k = 0; k < num_skipped; k++) {
					uint8_t sk[32];
					receiving_next(s, sk);
					store_skipped(s, s->other_ratchet_pub, s->receiving_len - 1, sk);
				}
			}
			memcpy(s->other_ratchet_pub, pub, 32);
			uint8_t dh_out[32];
			dh(dh_out, s->own_ratchet_priv, s->other_ratchet_pub);
			root_step(s->root, s->receiving_ck, s->root, dh_out);
			s->has_receiving = true;
			s->receiving_len = 0;

			s->prev_sending_len = s->sending_len;
			if (!OmemoCrypto::random_bytes(s->own_ratchet_priv, 32)) {
				return false;
			}
			dh(dh_out, s->own_ratchet_priv, s->other_ratchet_pub);
			root_step(s->root, s->sending_ck, s->root, dh_out);
			s->has_sending = true;
			s->sending_len = 0;
		}
		if (n < s->receiving_len) {
			return false; // duplicado
		}
		uint32_t num_skipped = n - s->receiving_len;
		if (num_skipped > MAX_SKIP) {
			return false;
		}
		for (uint32_t k = 0; k < num_skipped; k++) {
			uint8_t sk[32];
			receiving_next(s, sk);
			store_skipped(s, s->other_ratchet_pub, s->receiving_len - 1, sk);
		}
		if (!s->has_receiving) {
			return false;
		}
		receiving_next(s, mk);
	}
	uint32_t dn = 0, dpn = 0;
	uint8_t ddh[32];
	return aead_decrypt(p_plain, p_plain_len, p_ciphertext, p_ciphertext_len, mk, p_associated_data, &dn, &dpn, ddh);
}

} // namespace OmemoDr
