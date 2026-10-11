#ifndef XAT_OMEMO_DR_H
#define XAT_OMEMO_DR_H

/*************************************************************************/
/*  omemo_dr.h                                                           */
/*************************************************************************/
/* Double Ratchet de OMEMO 2 (XEP-0384 §4.3) con el wire format de         */
/* `doubleratchet`/`twomemo` (el que usa OpenClaw).                        */
/*                                                                       */
/*  El estado es opaco y serializable a bytes para que GDScript lo guarde  */
/*  (SQLite) entre sesiones. Usa las primitivas de `omemo_crypto`.         */
/*************************************************************************/

#include <stddef.h>
#include <stdint.h>

namespace OmemoDr {

struct State;

// --- Ciclo de vida ---
State *state_new();
void state_free(State *p_state);
// Tamaño de la serialización; y serializa/deserializa el estado.
size_t state_size(const State *p_state);
void state_serialize(const State *p_state, uint8_t *p_out);
bool state_deserialize(State *p_state, const uint8_t *p_in, size_t p_len);

// --- Inicio de sesión (mensaje inicial) ---
// Activo: `own_ratchet_priv` null => se genera. Devuelve el mensaje inicial.
bool start_active(State *p_state, const uint8_t p_shared_secret[32], const uint8_t p_recipient_ratchet_pub[32],
		const uint8_t p_associated_data[64], const uint8_t *p_plain, size_t p_plain_len,
		uint8_t **p_ciphertext, size_t *p_ciphertext_len);
// Pasivo: consume el mensaje inicial y devuelve el plaintext.
bool start_passive(State *p_state, const uint8_t p_shared_secret[32], const uint8_t p_own_ratchet_priv[32],
		const uint8_t p_associated_data[64], const uint8_t *p_ciphertext, size_t p_ciphertext_len,
		uint8_t **p_plain, size_t *p_plain_len);

// --- Mensajes siguientes ---
bool encrypt(State *p_state, const uint8_t p_associated_data[64],
		const uint8_t *p_plain, size_t p_plain_len, uint8_t **p_ciphertext, size_t *p_ciphertext_len);
bool decrypt(State *p_state, const uint8_t p_associated_data[64],
		const uint8_t *p_ciphertext, size_t p_ciphertext_len, uint8_t **p_plain, size_t *p_plain_len);

// --- AEAD de mensaje (expuesto para tests) ---
// Devuelve un OMEMOAuthenticatedMessage serializado.
bool aead_encrypt(uint8_t **p_out, size_t *p_out_len, const uint8_t p_message_key[32],
		const uint8_t *p_plain, size_t p_plain_len, const uint8_t p_associated_data[64],
		uint32_t p_n, uint32_t p_pn, const uint8_t p_dh_pub[32]);
// Verifica el MAC y descifra. Devuelve el plaintext y el header del OMEMOMessage.
bool aead_decrypt(uint8_t **p_plain, size_t *p_plain_len, const uint8_t *p_in, size_t p_in_len,
		const uint8_t p_message_key[32], const uint8_t p_associated_data[64],
		uint32_t *p_n, uint32_t *p_pn, uint8_t p_dh_pub[32]);

} // namespace OmemoDr

#endif // XAT_OMEMO_DR_H
