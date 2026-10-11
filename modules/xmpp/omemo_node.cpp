/*************************************************************************/
/*  omemo_node.cpp                                                       */
/*************************************************************************/
/* Nodo `XatOmemo` (binding GDScript de omemo_crypto).                   */
/*************************************************************************/

#include "omemo_node.h"

#include <stdlib.h>
#include <string.h>

#include "core/class_db.h"
#include "core/os/memory.h"
#include "core/pool_vector.h"

#include "omemo_crypto.h"
#include "omemo_dr.h"

// ---------------------------------------------------------------------------
// Conversiones PoolByteArray <-> bytes
// ---------------------------------------------------------------------------

static PoolByteArray _to_pba(const uint8_t *p_data, size_t p_len) {
	PoolByteArray out;
	if (p_len == 0) {
		return out;
	}
	out.resize((int)p_len);
	PoolByteArray::Write w = out.write();
	memcpy(w.ptr(), p_data, p_len);
	return out;
}

static bool _copy_in(const PoolByteArray &p_arr, uint8_t *p_out, size_t p_expected) {
	if ((size_t)p_arr.size() != p_expected) {
		return false;
	}
	PoolByteArray::Read r = p_arr.read();
	memcpy(p_out, r.ptr(), p_expected);
	return true;
}

// Extrae un PoolByteArray de tamaño exacto de un Dictionary.
static bool _dict_bytes(const Dictionary &p_dict, const String &p_key, uint8_t *p_out, size_t p_len) {
	if (!p_dict.has(p_key)) {
		return false;
	}
	Variant v = p_dict[p_key];
	if (v.get_type() != Variant::POOL_BYTE_ARRAY) {
		return false;
	}
	return _copy_in(PoolByteArray(v), p_out, p_len);
}

static int _hex_nibble(CharType p_c) {
	if (p_c >= '0' && p_c <= '9') {
		return p_c - '0';
	}
	if (p_c >= 'a' && p_c <= 'f') {
		return p_c - 'a' + 10;
	}
	if (p_c >= 'A' && p_c <= 'F') {
		return p_c - 'A' + 10;
	}
	return -1;
}

static bool _hex_to_bytes(const String &p_hex, uint8_t *p_out, size_t p_len) {
	if (p_hex.length() != (int)(p_len * 2)) {
		return false;
	}
	for (size_t i = 0; i < p_len; i++) {
		int hi = _hex_nibble(p_hex[i * 2]);
		int lo = _hex_nibble(p_hex[i * 2 + 1]);
		if (hi < 0 || lo < 0) {
			return false;
		}
		p_out[i] = (uint8_t)((hi << 4) | lo);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Binding
// ---------------------------------------------------------------------------

void XatOmemo::_bind_methods() {
	ClassDB::bind_method(D_METHOD("self_test"), &XatOmemo::self_test);
	ClassDB::bind_method(D_METHOD("payload_encrypt", "plain"), &XatOmemo::payload_encrypt);
	ClassDB::bind_method(D_METHOD("payload_encrypt_with_key", "key", "plain"), &XatOmemo::payload_encrypt_with_key);
	ClassDB::bind_method(D_METHOD("payload_decrypt", "payload", "key"), &XatOmemo::payload_decrypt);
	ClassDB::bind_method(D_METHOD("x25519_public_key", "secret"), &XatOmemo::x25519_public_key);
	ClassDB::bind_method(D_METHOD("x25519_shared", "secret", "peer"), &XatOmemo::x25519_shared);
	ClassDB::bind_method(D_METHOD("x25519_keypair"), &XatOmemo::x25519_keypair);
	ClassDB::bind_method(D_METHOD("random_bytes", "count"), &XatOmemo::random_bytes);
	ClassDB::bind_method(D_METHOD("ed25519_public_key", "seed"), &XatOmemo::ed25519_public_key);
	ClassDB::bind_method(D_METHOD("ed25519_keypair", "seed"), &XatOmemo::ed25519_keypair);
	ClassDB::bind_method(D_METHOD("ed25519_sign", "seed", "msg"), &XatOmemo::ed25519_sign);
	ClassDB::bind_method(D_METHOD("ed25519_check", "pub", "msg", "sig"), &XatOmemo::ed25519_check);
	ClassDB::bind_method(D_METHOD("ed25519_to_x25519", "ed"), &XatOmemo::ed25519_to_x25519);
	ClassDB::bind_method(D_METHOD("ed25519_to_x25519_secret", "seed"), &XatOmemo::ed25519_to_x25519_secret);
	ClassDB::bind_method(D_METHOD("x3dh_active", "own_seed", "bundle", "ephemeral"), &XatOmemo::x3dh_active);
	ClassDB::bind_method(D_METHOD("x3dh_passive", "own_seed", "spk_priv", "prekey_priv", "header"), &XatOmemo::x3dh_passive);
	ClassDB::bind_method(D_METHOD("dr_start_active", "ss", "recipient_ratchet_pub", "ad", "plain"), &XatOmemo::dr_start_active);
	ClassDB::bind_method(D_METHOD("dr_start_passive", "ss", "own_ratchet_priv", "ad", "ciphertext"), &XatOmemo::dr_start_passive);
	ClassDB::bind_method(D_METHOD("dr_encrypt", "state", "ad", "plain"), &XatOmemo::dr_encrypt);
	ClassDB::bind_method(D_METHOD("dr_decrypt", "state", "ad", "ciphertext"), &XatOmemo::dr_decrypt);
	ClassDB::bind_method(D_METHOD("dr_aead_encrypt", "mk", "plain", "ad", "header"), &XatOmemo::dr_aead_encrypt);
	ClassDB::bind_method(D_METHOD("dr_aead_decrypt", "mk", "ad", "ciphertext"), &XatOmemo::dr_aead_decrypt);
}

// ---------------------------------------------------------------------------
// API GDScript
// ---------------------------------------------------------------------------

Dictionary XatOmemo::payload_encrypt(const PoolByteArray &p_plain) {
	Dictionary out;
	uint8_t key48[48];
	uint8_t *payload = nullptr;
	size_t payload_len = 0;
	PoolByteArray::Read r = p_plain.read();
	bool ok = OmemoCrypto::payload_encrypt(key48, &payload, &payload_len, r.ptr(), (size_t)p_plain.size());
	if (!ok) {
		return out;
	}
	out["key"] = _to_pba(key48, 48);
	out["payload"] = _to_pba(payload, payload_len);
	free(payload);
	return out;
}

PoolByteArray XatOmemo::payload_encrypt_with_key(const PoolByteArray &p_key, const PoolByteArray &p_plain) {
	if (p_key.size() != 32) {
		return PoolByteArray();
	}
	uint8_t key32[32];
	if (!_copy_in(p_key, key32, 32)) {
		return PoolByteArray();
	}
	uint8_t key48[48];
	uint8_t *payload = nullptr;
	size_t payload_len = 0;
	PoolByteArray::Read r = p_plain.read();
	if (!OmemoCrypto::payload_encrypt_with_key(key32, key48, &payload, &payload_len, r.ptr(), (size_t)p_plain.size())) {
		return PoolByteArray();
	}
	PoolByteArray out = _to_pba(payload, payload_len);
	free(payload);
	return out;
}

PoolByteArray XatOmemo::payload_decrypt(const PoolByteArray &p_payload, const PoolByteArray &p_key) {
	if (p_key.size() != 32 && p_key.size() != 48) {
		return PoolByteArray();
	}
	uint8_t *plain = nullptr;
	size_t plain_len = 0;
	PoolByteArray::Read payload = p_payload.read();
	PoolByteArray::Read key = p_key.read();
	if (!OmemoCrypto::payload_decrypt(&plain, &plain_len, payload.ptr(), (size_t)p_payload.size(), key.ptr(), (size_t)p_key.size())) {
		return PoolByteArray();
	}
	PoolByteArray out = _to_pba(plain, plain_len);
	free(plain);
	return out;
}

PoolByteArray XatOmemo::x25519_public_key(const PoolByteArray &p_secret) {
	uint8_t secret[32];
	if (!_copy_in(p_secret, secret, 32)) {
		return PoolByteArray();
	}
	uint8_t pub[32];
	OmemoCrypto::x25519_public_key(pub, secret);
	return _to_pba(pub, 32);
}

PoolByteArray XatOmemo::x25519_shared(const PoolByteArray &p_secret, const PoolByteArray &p_peer) {
	uint8_t secret[32];
	uint8_t peer[32];
	if (!_copy_in(p_secret, secret, 32) || !_copy_in(p_peer, peer, 32)) {
		return PoolByteArray();
	}
	uint8_t shared[32];
	OmemoCrypto::x25519_shared(shared, secret, peer);
	return _to_pba(shared, 32);
}

PoolByteArray XatOmemo::ed25519_public_key(const PoolByteArray &p_seed) {
	uint8_t seed[32];
	if (!_copy_in(p_seed, seed, 32)) {
		return PoolByteArray();
	}
	uint8_t secret[64];
	uint8_t pub[32];
	OmemoCrypto::ed25519_key_pair(secret, pub, seed);
	return _to_pba(pub, 32);
}

PoolByteArray XatOmemo::ed25519_sign(const PoolByteArray &p_seed, const PoolByteArray &p_msg) {
	uint8_t seed[32];
	if (!_copy_in(p_seed, seed, 32)) {
		return PoolByteArray();
	}
	uint8_t secret[64];
	uint8_t pub[32];
	OmemoCrypto::ed25519_key_pair(secret, pub, seed);
	uint8_t sig[64];
	PoolByteArray::Read msg = p_msg.read();
	OmemoCrypto::ed25519_sign(sig, secret, msg.ptr(), (size_t)p_msg.size());
	return _to_pba(sig, 64);
}

bool XatOmemo::ed25519_check(const PoolByteArray &p_pub, const PoolByteArray &p_msg, const PoolByteArray &p_sig) {
	uint8_t pub[32];
	uint8_t sig[64];
	if (!_copy_in(p_pub, pub, 32) || !_copy_in(p_sig, sig, 64)) {
		return false;
	}
	PoolByteArray::Read msg = p_msg.read();
	return OmemoCrypto::ed25519_check(sig, pub, msg.ptr(), (size_t)p_msg.size());
}

PoolByteArray XatOmemo::ed25519_to_x25519(const PoolByteArray &p_ed) {
	uint8_t ed[32];
	if (!_copy_in(p_ed, ed, 32)) {
		return PoolByteArray();
	}
	uint8_t x[32];
	OmemoCrypto::ed25519_to_x25519(x, ed);
	return _to_pba(x, 32);
}

PoolByteArray XatOmemo::ed25519_to_x25519_secret(const PoolByteArray &p_seed) {
	uint8_t seed[32];
	if (!_copy_in(p_seed, seed, 32)) {
		return PoolByteArray();
	}
	uint8_t secret[32];
	OmemoCrypto::ed25519_seed_to_x25519_secret(secret, seed);
	return _to_pba(secret, 32);
}

Dictionary XatOmemo::x25519_keypair() {
	Dictionary out;
	uint8_t secret[32];
	if (!OmemoCrypto::random_bytes(secret, 32)) {
		return out;
	}
	uint8_t pub[32];
	OmemoCrypto::x25519_public_key(pub, secret);
	out["secret"] = _to_pba(secret, 32);
	out["public"] = _to_pba(pub, 32);
	return out;
}

PoolByteArray XatOmemo::random_bytes(int p_count) {
	if (p_count <= 0) {
		return PoolByteArray();
	}
	PoolByteArray out;
	out.resize(p_count);
	PoolByteArray::Write w = out.write();
	if (!OmemoCrypto::random_bytes(w.ptr(), (size_t)p_count)) {
		return PoolByteArray();
	}
	return out;
}

Dictionary XatOmemo::x3dh_active(const PoolByteArray &p_own_seed, const Dictionary &p_bundle, const PoolByteArray &p_ephemeral) {
	Dictionary out;
	uint8_t own_seed[32];
	uint8_t ik_ed[32], spk[32], spk_sig[64], prekey[32];
	if (!_copy_in(p_own_seed, own_seed, 32) ||
			!_dict_bytes(p_bundle, "ik", ik_ed, 32) || !_dict_bytes(p_bundle, "spk", spk, 32) ||
			!_dict_bytes(p_bundle, "spk_sig", spk_sig, 64) || !_dict_bytes(p_bundle, "prekey", prekey, 32)) {
		return out;
	}
	uint8_t ephemeral_in[32];
	const uint8_t *ephem = nullptr;
	if (p_ephemeral.size() == 32) {
		_copy_in(p_ephemeral, ephemeral_in, 32);
		ephem = ephemeral_in;
	} else if (p_ephemeral.size() != 0) {
		return out;
	}
	uint8_t ss[32], ad[64], ek[32], epriv[32];
	if (!OmemoCrypto::x3dh_active(ss, ad, ek, epriv, own_seed, ik_ed, spk, spk_sig, prekey, ephem)) {
		return out;
	}
	out["ss"] = _to_pba(ss, 32);
	out["ad"] = _to_pba(ad, 64);
	out["ik"] = ed25519_public_key(p_own_seed);
	out["ek"] = _to_pba(ek, 32);
	out["spk"] = _to_pba(spk, 32);
	out["prekey"] = _to_pba(prekey, 32);
	return out;
}

Dictionary XatOmemo::x3dh_passive(const PoolByteArray &p_own_seed, const PoolByteArray &p_own_spk_priv, const PoolByteArray &p_own_prekey_priv, const Dictionary &p_header) {
	Dictionary out;
	uint8_t own_seed[32], spk_priv[32], prekey_priv[32], ik[32], ek[32];
	if (!_copy_in(p_own_seed, own_seed, 32) || !_copy_in(p_own_spk_priv, spk_priv, 32) ||
			!_copy_in(p_own_prekey_priv, prekey_priv, 32) ||
			!_dict_bytes(p_header, "ik", ik, 32) || !_dict_bytes(p_header, "ek", ek, 32)) {
		return out;
	}
	uint8_t ss[32], ad[64];
	if (!OmemoCrypto::x3dh_passive(ss, ad, own_seed, spk_priv, prekey_priv, ik, ek)) {
		return out;
	}
	out["ss"] = _to_pba(ss, 32);
	out["ad"] = _to_pba(ad, 64);
	return out;
}

// ---------------------------------------------------------------------------
// Double Ratchet
// ---------------------------------------------------------------------------

static OmemoDr::State *_dr_load(const PoolByteArray &p_blob) {
	OmemoDr::State *s = OmemoDr::state_new();
	if (p_blob.size() > 0) {
		PoolByteArray::Read r = p_blob.read();
		if (!OmemoDr::state_deserialize(s, r.ptr(), (size_t)p_blob.size())) {
			OmemoDr::state_free(s);
			return nullptr;
		}
	}
	return s;
}

static PoolByteArray _dr_dump(OmemoDr::State *p_state) {
	size_t n = OmemoDr::state_size(p_state);
	PoolByteArray out;
	out.resize((int)n);
	PoolByteArray::Write w = out.write();
	OmemoDr::state_serialize(p_state, w.ptr());
	return out;
}

Dictionary XatOmemo::dr_start_active(const PoolByteArray &p_ss, const PoolByteArray &p_recipient_ratchet_pub, const PoolByteArray &p_ad, const PoolByteArray &p_plain) {
	Dictionary out;
	uint8_t ss[32], rp[32], ad[64];
	if (!_copy_in(p_ss, ss, 32) || !_copy_in(p_recipient_ratchet_pub, rp, 32) || !_copy_in(p_ad, ad, 64)) {
		return out;
	}
	OmemoDr::State *s = OmemoDr::state_new();
	uint8_t *ct = nullptr;
	size_t ct_len = 0;
	PoolByteArray::Read pr = p_plain.read();
	if (!OmemoDr::start_active(s, ss, rp, ad, pr.ptr(), (size_t)p_plain.size(), &ct, &ct_len)) {
		OmemoDr::state_free(s);
		return out;
	}
	out["state"] = _dr_dump(s);
	out["ciphertext"] = _to_pba(ct, ct_len);
	free(ct);
	OmemoDr::state_free(s);
	return out;
}

Dictionary XatOmemo::dr_start_passive(const PoolByteArray &p_ss, const PoolByteArray &p_own_ratchet_priv, const PoolByteArray &p_ad, const PoolByteArray &p_ciphertext) {
	Dictionary out;
	uint8_t ss[32], rp[32], ad[64];
	if (!_copy_in(p_ss, ss, 32) || !_copy_in(p_own_ratchet_priv, rp, 32) || !_copy_in(p_ad, ad, 64)) {
		return out;
	}
	OmemoDr::State *s = OmemoDr::state_new();
	uint8_t *plain = nullptr;
	size_t plain_len = 0;
	PoolByteArray::Read ct = p_ciphertext.read();
	if (!OmemoDr::start_passive(s, ss, rp, ad, ct.ptr(), (size_t)p_ciphertext.size(), &plain, &plain_len)) {
		OmemoDr::state_free(s);
		return out;
	}
	out["state"] = _dr_dump(s);
	out["plaintext"] = _to_pba(plain, plain_len);
	free(plain);
	OmemoDr::state_free(s);
	return out;
}

Dictionary XatOmemo::dr_encrypt(const PoolByteArray &p_state, const PoolByteArray &p_ad, const PoolByteArray &p_plain) {
	Dictionary out;
	uint8_t ad[64];
	if (!_copy_in(p_ad, ad, 64)) {
		return out;
	}
	OmemoDr::State *s = _dr_load(p_state);
	if (s == nullptr) {
		return out;
	}
	uint8_t *ct = nullptr;
	size_t ct_len = 0;
	PoolByteArray::Read pr = p_plain.read();
	bool ok = OmemoDr::encrypt(s, ad, pr.ptr(), (size_t)p_plain.size(), &ct, &ct_len);
	if (!ok) {
		OmemoDr::state_free(s);
		return out;
	}
	out["state"] = _dr_dump(s);
	out["ciphertext"] = _to_pba(ct, ct_len);
	free(ct);
	OmemoDr::state_free(s);
	return out;
}

Dictionary XatOmemo::dr_decrypt(const PoolByteArray &p_state, const PoolByteArray &p_ad, const PoolByteArray &p_ciphertext) {
	Dictionary out;
	uint8_t ad[64];
	if (!_copy_in(p_ad, ad, 64)) {
		return out;
	}
	OmemoDr::State *s = _dr_load(p_state);
	if (s == nullptr) {
		return out;
	}
	uint8_t *plain = nullptr;
	size_t plain_len = 0;
	PoolByteArray::Read ct = p_ciphertext.read();
	bool ok = OmemoDr::decrypt(s, ad, ct.ptr(), (size_t)p_ciphertext.size(), &plain, &plain_len);
	if (!ok) {
		OmemoDr::state_free(s);
		return out;
	}
	out["state"] = _dr_dump(s);
	out["plaintext"] = _to_pba(plain, plain_len);
	free(plain);
	OmemoDr::state_free(s);
	return out;
}

PoolByteArray XatOmemo::dr_aead_encrypt(const PoolByteArray &p_mk, const PoolByteArray &p_plain, const PoolByteArray &p_ad, const Dictionary &p_header) {
	uint8_t mk[32], ad[64], dh_pub[32];
	if (!_copy_in(p_mk, mk, 32) || !_copy_in(p_ad, ad, 64) || !_dict_bytes(p_header, "dh_pub", dh_pub, 32)) {
		return PoolByteArray();
	}
	uint32_t n = (uint32_t)int(p_header.get("n", 0));
	uint32_t pn = (uint32_t)int(p_header.get("pn", 0));
	uint8_t *out = nullptr;
	size_t out_len = 0;
	PoolByteArray::Read pr = p_plain.read();
	if (!OmemoDr::aead_encrypt(&out, &out_len, mk, pr.ptr(), (size_t)p_plain.size(), ad, n, pn, dh_pub)) {
		return PoolByteArray();
	}
	PoolByteArray res = _to_pba(out, out_len);
	free(out);
	return res;
}

Dictionary XatOmemo::dr_aead_decrypt(const PoolByteArray &p_mk, const PoolByteArray &p_ad, const PoolByteArray &p_ciphertext) {
	Dictionary out;
	uint8_t mk[32], ad[64];
	if (!_copy_in(p_mk, mk, 32) || !_copy_in(p_ad, ad, 64)) {
		return out;
	}
	uint8_t *plain = nullptr;
	size_t plain_len = 0;
	uint32_t n = 0, pn = 0;
	uint8_t dh_pub[32];
	PoolByteArray::Read ct = p_ciphertext.read();
	if (!OmemoDr::aead_decrypt(&plain, &plain_len, ct.ptr(), (size_t)p_ciphertext.size(), mk, ad, &n, &pn, dh_pub)) {
		return out;
	}
	out["plaintext"] = _to_pba(plain, plain_len);
	out["n"] = (int)n;
	out["pn"] = (int)pn;
	out["dh_pub"] = _to_pba(dh_pub, 32);
	free(plain);
	return out;
}

Dictionary XatOmemo::ed25519_keypair(const PoolByteArray &p_seed) {
	Dictionary out;
	uint8_t seed[32];
	if (!_copy_in(p_seed, seed, 32)) {
		return out;
	}
	uint8_t secret[64];
	uint8_t pub[32];
	OmemoCrypto::ed25519_key_pair(secret, pub, seed);
	out["secret"] = _to_pba(secret, 64);
	out["public"] = _to_pba(pub, 32);
	return out;
}

// ---------------------------------------------------------------------------
// Autotest con vectores conocidos
// ---------------------------------------------------------------------------

String XatOmemo::self_test() {
	// RFC 7748 §6.1: X25519 Alice/Bob.
	uint8_t alice_priv[32], alice_pub_want[32], bob_priv[32], bob_pub_want[32], shared_want[32];
	if (!_hex_to_bytes("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", alice_priv, 32) ||
			!_hex_to_bytes("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", alice_pub_want, 32) ||
			!_hex_to_bytes("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", bob_priv, 32) ||
			!_hex_to_bytes("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", bob_pub_want, 32) ||
			!_hex_to_bytes("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", shared_want, 32)) {
		return "self_test: hex de RFC 7748 inválido";
	}
	uint8_t alice_pub[32], bob_pub[32], shared[32];
	OmemoCrypto::x25519_public_key(alice_pub, alice_priv);
	OmemoCrypto::x25519_public_key(bob_pub, bob_priv);
	if (memcmp(alice_pub, alice_pub_want, 32) != 0) {
		return "self_test: X25519 clave pública de Alice";
	}
	if (memcmp(bob_pub, bob_pub_want, 32) != 0) {
		return "self_test: X25519 clave pública de Bob";
	}
	OmemoCrypto::x25519_shared(shared, alice_priv, bob_pub);
	if (memcmp(shared, shared_want, 32) != 0) {
		return "self_test: X25519 secreto compartido";
	}

	// RFC 8032 §7.1 TEST 1: Ed25519 de mensaje vacío.
	uint8_t seed[32], pub_want[32], sig_want[64];
	if (!_hex_to_bytes("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", seed, 32) ||
			!_hex_to_bytes("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", pub_want, 32) ||
			!_hex_to_bytes("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b", sig_want, 64)) {
		return "self_test: hex de RFC 8032 inválido";
	}
	uint8_t secret[64], pub[32], sig[64];
	OmemoCrypto::ed25519_key_pair(secret, pub, seed);
	if (memcmp(pub, pub_want, 32) != 0) {
		return "self_test: Ed25519 clave pública";
	}
	OmemoCrypto::ed25519_sign(sig, secret, nullptr, 0);
	if (memcmp(sig, sig_want, 64) != 0) {
		return "self_test: Ed25519 firma";
	}
	if (!OmemoCrypto::ed25519_check(sig_want, pub_want, nullptr, 0)) {
		return "self_test: Ed25519 verificación";
	}
	uint8_t tampered[64];
	memcpy(tampered, sig_want, 64);
	tampered[0] ^= 0x01;
	if (OmemoCrypto::ed25519_check(tampered, pub_want, nullptr, 0)) {
		return "self_test: Ed25519 aceptó una firma alterada";
	}

	// Material de llaves: pública Ed25519, conversión a X25519, firma de libxeddsa
	// y DH de la identity key (interop con xeddsa de Syndace).
	uint8_t id_seed[32], id_pub_want[32], id_x_want[32], xeddsa_sig[64], dh_want[32];
	if (!_hex_to_bytes("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", id_seed, 32) ||
			!_hex_to_bytes("03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8", id_pub_want, 32) ||
			!_hex_to_bytes("4701d08488451f545a409fb58ae3e58581ca40ac3f7f114698cd71deac73ca01", id_x_want, 32) ||
			!_hex_to_bytes("4394a30210af2ecaf87ef4b0ee2c86fa65eaa313190038ba0b57b3aed47b9e947dac0832b9b3831c2ee8dffd9b051c017766dbac7ffe437add7c810d2c50710d", xeddsa_sig, 64) ||
			!_hex_to_bytes("9a2e4e03705f0d2058492081b43c824082408e450eb03aba6e3a43ce6976a802", dh_want, 32)) {
		return "self_test: hex de material de llaves inválido";
	}
	uint8_t id_secret[64], id_pub[32], id_x[32];
	OmemoCrypto::ed25519_key_pair(id_secret, id_pub, id_seed);
	if (memcmp(id_pub, id_pub_want, 32) != 0) {
		return "self_test: pública Ed25519 de la identity";
	}
	OmemoCrypto::ed25519_to_x25519(id_x, id_pub);
	if (memcmp(id_x, id_x_want, 32) != 0) {
		return "self_test: conversión Ed25519→X25519";
	}
	const uint8_t signed_prekey_msg[17] = { 'x', 'a', 't', ' ', 's', 'i', 'g', 'n', 'e', 'd', ' ', 'p', 'r', 'e', 'k', 'e', 'y' };
	if (!OmemoCrypto::ed25519_check(xeddsa_sig, id_pub_want, signed_prekey_msg, 17)) {
		return "self_test: no aceptó una firma de libxeddsa";
	}
	uint8_t peer_seed[32];
	memset(peer_seed, 1, sizeof(peer_seed));
	uint8_t peer_secret[64], peer_pub[32], peer_x[32], id_dh_secret[32], dh[32];
	OmemoCrypto::ed25519_key_pair(peer_secret, peer_pub, peer_seed);
	OmemoCrypto::ed25519_to_x25519(peer_x, peer_pub);
	OmemoCrypto::ed25519_seed_to_x25519_secret(id_dh_secret, id_seed);
	OmemoCrypto::x25519_shared(dh, id_dh_secret, peer_x);
	if (memcmp(dh, dh_want, 32) != 0) {
		return "self_test: DH de la identity key";
	}

	// Vector de interop del payload OMEMO 2 (generado con openclaw-xmpp:
	// HKDF-SHA-256(zero salt, "OMEMO Payload") + AES-256-CBC + HMAC truncado).
	uint8_t key48[48], payload[44], plain_want[4];
	if (!_hex_to_bytes("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f6032e091967d4a46c46e5706855512ca", key48, 48) ||
			!_hex_to_bytes("0a106032e091967d4a46c46e5706855512ca1218080010001a0022102c450ccc8ab79c1862cc619b8b898342", payload, 44) ||
			!_hex_to_bytes("686f6c61", plain_want, 4)) {
		return "self_test: hex del payload inválido";
	}
	uint8_t *plain = nullptr;
	size_t plain_len = 0;
	if (!OmemoCrypto::payload_decrypt(&plain, &plain_len, payload, 44, key48, 48)) {
		return "self_test: payload_decrypt falló";
	}
	if (plain_len != 4 || memcmp(plain, plain_want, 4) != 0) {
		free(plain);
		return "self_test: payload descifrado incorrecto";
	}
	free(plain);

	// Round-trip con clave aleatoria.
	uint8_t key48_rt[48];
	uint8_t *payload_rt = nullptr;
	size_t payload_rt_len = 0;
	const uint8_t message[6] = { 'h', 'o', 'l', 'a', '!', '!' };
	if (!OmemoCrypto::payload_encrypt(key48_rt, &payload_rt, &payload_rt_len, message, sizeof(message))) {
		return "self_test: payload_encrypt falló";
	}
	uint8_t *plain_rt = nullptr;
	size_t plain_rt_len = 0;
	bool rt_ok = OmemoCrypto::payload_decrypt(&plain_rt, &plain_rt_len, payload_rt, payload_rt_len, key48_rt, 48);
	free(payload_rt);
	if (!rt_ok) {
		return "self_test: round-trip no descifró";
	}
	if (plain_rt_len != sizeof(message) || memcmp(plain_rt, message, sizeof(message)) != 0) {
		free(plain_rt);
		return "self_test: round-trip incorrecto";
	}
	free(plain_rt);

	return String();
}
