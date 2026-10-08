# Módulo nativo xmpp para el fork godot3-box3d.
#
# Provee:
#   - XmppConnection: nodo nativo -> libstrophe (stream XML, SASL, TLS, SM).
#   - SQLiteBinding: binding mínimo de SQLite (amalgama vendorizada).
#   - Backend TLS de libstrophe sobre el mbedTLS del motor (tls_mbedtls.c).
#
# Requiere que modules/mbedtls esté habilitado: de ahí salen los símbolos
# mbedtls_ssl_* y el bundle de verificación. Si se deshabilita, el link falla
# (a propósito: no queremos TLS a medias).


def can_build(env, platform):
    return platform in ("frt", "x11", "server", "windows", "osx", "android", "iphone")


def is_enabled():
    # General-purpose fork builds omit XMPP. Xat opts in with
    # module_xmpp_enabled=yes on its platform-template jobs.
    return False


def configure(env):
    pass
