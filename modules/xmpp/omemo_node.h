#ifndef XAT_OMEMO_NODE_H
#define XAT_OMEMO_NODE_H

/*************************************************************************/
/*  omemo_node.h                                                         */
/*************************************************************************/
/* Nodo `XatOmemo`: expone a GDScript las primitivas de OMEMO del módulo  */
/* (curva25519, HKDF/AES/HMAC y la capa de payload de OMEMO 2).           */
/*                                                                       */
/*  Es un Reference puro (sin hilo propio): lo llama el hilo principal.   */
/*************************************************************************/

#include "core/dictionary.h"
#include "core/pool_vector.h"
#include "core/reference.h"
#include "core/ustring.h"

class XatOmemo : public Reference {
	GDCLASS(XatOmemo, Reference);

protected:
	static void _bind_methods();

public:
	// Corre los vectores de RFC 7748 (X25519), RFC 8032 (Ed25519) y el vector
	// de interop del payload OMEMO 2. Devuelve "" si todo está bien.
	String self_test();

	Dictionary payload_encrypt(const PoolByteArray &p_plain);
	PoolByteArray payload_encrypt_with_key(const PoolByteArray &p_key, const PoolByteArray &p_plain);
	PoolByteArray payload_decrypt(const PoolByteArray &p_payload, const PoolByteArray &p_key);

	PoolByteArray x25519_public_key(const PoolByteArray &p_secret);
	PoolByteArray x25519_shared(const PoolByteArray &p_secret, const PoolByteArray &p_peer);
	// X25519 nuevo: {secret(32), public(32)}.
	Dictionary x25519_keypair();
	PoolByteArray random_bytes(int p_count);
	PoolByteArray ed25519_public_key(const PoolByteArray &p_seed);
	// Ed25519 desde semilla: {secret(64), public(32)}.
	Dictionary ed25519_keypair(const PoolByteArray &p_seed);
	PoolByteArray ed25519_sign(const PoolByteArray &p_seed, const PoolByteArray &p_msg);
	bool ed25519_check(const PoolByteArray &p_pub, const PoolByteArray &p_msg, const PoolByteArray &p_sig);
	PoolByteArray ed25519_to_x25519(const PoolByteArray &p_ed);
	PoolByteArray ed25519_to_x25519_secret(const PoolByteArray &p_seed);

	// X3DH. `bundle` = {ik, spk, spk_sig, prekey}; `header` = {ik, ek}.
	// Devuelve {ss, ad, ik, ek, spk, prekey}.
	Dictionary x3dh_active(const PoolByteArray &p_own_seed, const Dictionary &p_bundle, const PoolByteArray &p_ephemeral);
	// Devuelve {ss, ad}.
	Dictionary x3dh_passive(const PoolByteArray &p_own_seed, const PoolByteArray &p_own_spk_priv, const PoolByteArray &p_own_prekey_priv, const Dictionary &p_header);

	// Double Ratchet. El estado viaja como PoolByteArray opaco.
	Dictionary dr_start_active(const PoolByteArray &p_ss, const PoolByteArray &p_recipient_ratchet_pub, const PoolByteArray &p_ad, const PoolByteArray &p_plain);
	Dictionary dr_start_passive(const PoolByteArray &p_ss, const PoolByteArray &p_own_ratchet_priv, const PoolByteArray &p_ad, const PoolByteArray &p_ciphertext);
	Dictionary dr_encrypt(const PoolByteArray &p_state, const PoolByteArray &p_ad, const PoolByteArray &p_plain);
	Dictionary dr_decrypt(const PoolByteArray &p_state, const PoolByteArray &p_ad, const PoolByteArray &p_ciphertext);
	// AEAD de mensaje aislado (para tests): header = {n, pn, dh_pub}.
	PoolByteArray dr_aead_encrypt(const PoolByteArray &p_mk, const PoolByteArray &p_plain, const PoolByteArray &p_ad, const Dictionary &p_header);
	Dictionary dr_aead_decrypt(const PoolByteArray &p_mk, const PoolByteArray &p_ad, const PoolByteArray &p_ciphertext);

	XatOmemo() {}
	~XatOmemo() {}
};

#endif // XAT_OMEMO_NODE_H
