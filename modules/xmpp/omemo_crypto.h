#ifndef XAT_OMEMO_CRYPTO_H
#define XAT_OMEMO_CRYPTO_H

/*************************************************************************/
/*  omemo_crypto.h                                                       */
/*************************************************************************/
/* Primitivas criptográficas para OMEMO (XEP-0384) en el módulo xmpp.     */
/*                                                                       */
/*  - Curva25519 (X25519 + Ed25519/EdDSA) con Monocypher.                 */
/*  - HKDF-SHA-256, AES-256-CBC y HMAC-SHA-256 con el mbedTLS del motor.   */
/*  - Capa de payload de OMEMO 2 (XEP-0384 §4.3) y su protobuf.           */
/*                                                                       */
/*  Lenguaje C puro (sin Godot) para poder testearse aislado; el nodo      */
/*  nativo `XatOmemo` envuelve estas funciones para GDScript.             */
/*************************************************************************/

#include <stddef.h>
#include <stdint.h>

namespace OmemoCrypto {

// --- Aleatoriedad (mbedTLS CTR-DRBG) ---
// Devuelve false si el DRBG no pudo sembrarse (sin fuente de entropía).
bool random_bytes(uint8_t *out, size_t len);

// --- X25519 (intercambio de claves Diffie-Hellman) ---
// Deriva la clave pública de un secreto de 32 bytes.
void x25519_public_key(uint8_t public_key[32], const uint8_t secret_key[32]);
// Acuerdo: shared = X25519(secret, peer_public). No es uniforme; hashear antes.
void x25519_shared(uint8_t shared[32], const uint8_t secret_key[32], const uint8_t peer_public[32]);

// --- Ed25519 (RFC 8032, firma) ---
void ed25519_key_pair(uint8_t secret_key[64], uint8_t public_key[32], const uint8_t seed[32]);
void ed25519_sign(uint8_t signature[64], const uint8_t secret_key[64], const uint8_t *msg, size_t msg_len);
// Devuelve true si la firma es válida.
bool ed25519_check(const uint8_t signature[64], const uint8_t public_key[32], const uint8_t *msg, size_t msg_len);
// Convierte una clave pública Ed25519 (Edwards) a X25519 (Montgomery), como
// hace Signal para la identity key de X3DH.
void ed25519_to_x25519(uint8_t x25519[32], const uint8_t ed25519[32]);
// Clave secreta X25519 derivada de una semilla Ed25519 (SHA-512(seed)[:32]
// recortada), para el DH de la identity key.
void ed25519_seed_to_x25519_secret(uint8_t secret[32], const uint8_t seed[32]);
// SHA-512 (expuesto para X3DH).
void sha512(uint8_t out[64], const uint8_t *msg, size_t len);

// --- HKDF-SHA-256 ---
void hkdf_sha256(uint8_t *out, size_t out_len, const uint8_t *ikm, size_t ikm_len,
		const uint8_t *salt, size_t salt_len, const uint8_t *info, size_t info_len);

// --- AES-256-CBC con relleno PKCS#7 ---
// `out` debe tener al menos `in_len + 16` bytes. Devuelve los bytes escritos
// en `*out_len`, o false si `in_len` no cabe.
bool aes256cbc_encrypt(uint8_t *out, size_t *out_len, const uint8_t key[32], const uint8_t iv[16], const uint8_t *in, size_t in_len);
// `out` debe tener al menos `in_len` bytes. `in_len` debe ser múltiplo de 16
// y el relleno PKCS#7 válido.
bool aes256cbc_decrypt(uint8_t *out, size_t *out_len, const uint8_t key[32], const uint8_t iv[16], const uint8_t *in, size_t in_len);

// --- HMAC-SHA-256 ---
void hmac_sha256(uint8_t out[32], const uint8_t *key, size_t key_len, const uint8_t *msg, size_t msg_len);

// --- Payload de OMEMO 2 ---
// Cifra `plaintext` con una clave aleatoria de 32 bytes (HKDF -> enc/auth/iv),
// devuelve `key48 = key(32) || mac(16)` y el payload protobuf
// `OMEMOAuthenticatedMessage`. `*payload`/`*key48` los libera quien llama con
// free().
bool payload_encrypt(uint8_t *key48, uint8_t **payload, size_t *payload_len,
		const uint8_t *plaintext, size_t plaintext_len);
// Variante determinista para tests de interop: usa `key32` en vez de aleatoria.
bool payload_encrypt_with_key(const uint8_t key32[32], uint8_t *key48, uint8_t **payload, size_t *payload_len,
		const uint8_t *plaintext, size_t plaintext_len);
// Descifra un payload. `key` puede ser 32 (sólo clave) o 48 (clave + mac del
// transporte, como define XEP-0384). `*plaintext` lo libera quien llama.
bool payload_decrypt(uint8_t **plaintext, size_t *plaintext_len,
		const uint8_t *payload, size_t payload_len,
		const uint8_t *key, size_t key_len);

// --- X3DH (XEP-0384 ap. 4.2, identidad Ed25519) ---
// Acuerdo activo: verifica la firma del signed prekey, arma el header y deriva
// `ss` (32) y `ad` = ik_activo_ed || ik_pasivo_ed (64). `header_ek` es la
// pública X25519 de la efímera. `p_ephemeral` puede ser null (aleatoria).
bool x3dh_active(uint8_t ss[32], uint8_t ad[64], uint8_t header_ek[32], uint8_t ephemeral_priv[32],
		const uint8_t own_seed[32],
		const uint8_t other_ik_ed[32], const uint8_t other_spk[32],
		const uint8_t other_spk_sig[64], const uint8_t other_prekey[32],
		const uint8_t *p_ephemeral);
// Acuerdo pasivo: dado el header del activo y las privadas propias del signed
// prekey y del prekey usados, deriva `ss` y el mismo `ad`.
bool x3dh_passive(uint8_t ss[32], uint8_t ad[64],
		const uint8_t own_seed[32], const uint8_t own_spk_priv[32], const uint8_t own_prekey_priv[32],
		const uint8_t header_ik[32], const uint8_t header_ek[32]);

// --- Protobuf de los mensajes internos ---
// OMEMOMessage{n=1, pn=2, dh_pub=3, ciphertext=4};
// OMEMOAuthenticatedMessage{mac=1, message=2}. Coincide con
// `openclaw-xmpp/src/omemo/v2-crypto.ts`.
void pb_encode_authenticated(uint8_t **out, size_t *out_len,
		const uint8_t *ciphertext, size_t ct_len, const uint8_t mac[16],
		uint32_t n, uint32_t pn, const uint8_t *dh_pub, size_t dh_len);
// Los punteros devueltos apuntan dentro de `buf` (no se liberan).
bool pb_decode_authenticated(const uint8_t *buf, size_t len,
		const uint8_t **mac, size_t *mac_len,
		const uint8_t **ciphertext, size_t *ct_len,
		uint32_t *n, uint32_t *pn,
		const uint8_t **dh_pub, size_t *dh_len);

} // namespace OmemoCrypto

#endif // XAT_OMEMO_CRYPTO_H
