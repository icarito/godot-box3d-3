#ifndef XMPP_CONNECTION_H
#define XMPP_CONNECTION_H

/*************************************************************************/
/*  xmpp_connection.h                                                    */
/*************************************************************************/
/* Módulo xmpp (xat): nodo nativo sobre libstrophe.                      */
/*                                                                       */
/*  - `xmpp_run_once` corre en un hilo propio (Thread de Godot).         */
/*  - Toda llamada a libstrophe ocurre en ese hilo.                      */
/*  - Los eventos (stanzas, conexión, logs) se marshalean al hilo        */
/*    principal con `call_deferred`, que es seguro entre hilos.          */
/*  - Nunca se toca SQLite ni el árbol de nodos desde el hilo de         */
/*    libstrophe.                                                        */
/*************************************************************************/

#include "core/list.h"
#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/safe_refcount.h"
#include "core/ustring.h"
#include "scene/main/node.h"

#include <strophe.h>

class XmppConnection : public Node {
	GDCLASS(XmppConnection, Node);

	// Toda la superficie de libstrophe pertenece al hilo de trabajo.
	xmpp_ctx_t *ctx = nullptr;
	xmpp_conn_t *conn = nullptr;
	Thread *thread = nullptr;

	// Contexto compartido entre reconexiones (requisito de XEP-0198 resume:
	// el sm_state sólo se puede reasignar a un conn del mismo ctx).
	xmpp_log_t log;
	xmpp_sm_state_t *sm_state = nullptr;
	String last_bare;

	// Cola de stanzas salientes (hilo principal -> hilo libstrophe).
	Mutex outgoing_mutex;
	List<String> outgoing;

	// Pedido de cierre desde el hilo principal.
	SafeFlag stop_requested;
	// Estado observable desde el hilo principal.
	SafeFlag worker_active;
	SafeFlag connected_flag;
	// Sólo se escribe/lee en el hilo de libstrophe.
	bool worker_disconnected = false;

	// Parámetros de conexión (se fijan antes de arrancar el hilo).
	String jid;
	String password;
	String host;
	String cafile;
	int port = 5222;
	int flags = 0;
	// Se incrementa en cada open() (hilo principal). El hilo de trabajo
	// captura su generación para no pisar el estado de una corrida nueva.
	int generation = 0;

	static void _thread_trampoline(void *p_user);
	void _thread_main();
	void _drain_outgoing();
	void _stop_worker();

	// Callbacks de libstrophe (hilo de trabajo).
	static void _conn_event_cb(xmpp_conn_t *p_conn,
			xmpp_conn_event_t p_event,
			int p_error,
			xmpp_stream_error_t *p_stream_error,
			void *p_userdata);
	static int _stanza_cb(xmpp_conn_t *p_conn, xmpp_stanza_t *p_stanza, void *p_userdata);
	static void _log_cb(void *p_userdata, xmpp_log_level_t p_level, const char *p_area, const char *p_msg);

	// Marshalling al hilo principal.
	void _emit_stanza(const String &p_xml);
	void _emit_connected(const String &p_bound_jid);
	void _emit_disconnected(int p_error);
	void _emit_log(int p_level, const String &p_msg);
	void _on_worker_finished(int p_generation);

protected:
	static void _bind_methods();

public:
	// Abre la conexión y arranca el hilo. Devuelve 0 (XMPP_EOK) si pudo
	// iniciarse; los errores de conexión llegan por la señal `disconnected`.
	// Se usa `open`/`close`/`send` porque `connect`, `disconnect` e
	// `is_connected` ya existen en Object (señales) y no se pueden redefinir.
	// Máx. 5 argumentos: límite de MethodBind en este motor.
	int open(const String &p_jid, const String &p_pass, const String &p_host = "", int p_port = 5222, const String &p_cafile = "");
	void close();
	int send(const String &p_xml);
	bool is_open() const;

	XmppConnection();
	~XmppConnection();
};

#endif // XMPP_CONNECTION_H
