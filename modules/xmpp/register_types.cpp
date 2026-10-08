/*************************************************************************/
/*  register_types.cpp                                                   */
/*************************************************************************/
/* Módulo xmpp (xat).                                                     */
/*************************************************************************/

#include "register_types.h"

#include "sqlite_binding.h"
#include "xmpp_connection.h"

#include "core/class_db.h"

#include <strophe.h>

void register_xmpp_types() {
	// libstrophe requiere init global (sockets, resolver, TLS).
	xmpp_initialize();

	ClassDB::register_class<XmppConnection>();
	ClassDB::register_class<SQLiteBinding>();
	ClassDB::register_class<SQLiteQuery>();
}

void unregister_xmpp_types() {
	xmpp_shutdown();
}
