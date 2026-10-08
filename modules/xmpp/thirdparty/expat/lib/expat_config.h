/* Config de expat para Linux/glibc (vendorizado en xat).
 * No usamos el config generado por autotools/CMake: definimos lo mínimo que
 * xmlparse.c/xmltok.c/xmlrole.c necesitan en un entorno Linux con glibc.
 * XMPP no usa DTD ni namespaces XML de expat (libstrophe crea el parser sin
 * namespace processing), así que XML_DTD/XML_NS quedan apagados. */
#ifndef EXPAT_CONFIG_H
#define EXPAT_CONFIG_H 1

#define HAVE_DLFCN_H 1
#define HAVE_FCNTL_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_MEMORY_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define HAVE_GETPAGESIZE 1
#define HAVE_MMAP 1
#define STDC_HEADERS 1

/* Buffer de contexto para XML_GetBuffer; 1024 es el default de expat. */
#define XML_CONTEXT_BYTES 1024
/* Entidades generales habilitadas (necesarias para &amp; etc. internamente). */
#define XML_GE 1
/* Usa getrandom()/dev/urandom para el hash de entidades. */
#ifndef _WIN32
#define XML_DEV_URANDOM 1
#endif

#endif /* EXPAT_CONFIG_H */
