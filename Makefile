.include <bsd.own.mk>

PREFIX?=	/usr/local
LIBDIR=		${PREFIX}/lib/pkg
SHLIB_DIR?=	${LIBDIR}/
SHLIB_NAME?=	${PLUGIN_NAME}.so

PLUGIN_NAME=	provides
SRCS=		provides.c progressbar.c mkpath.c configure.c

CFLAGS+= -I /usr/local/include
LDFLAGS+= -L /usr/lib -L /usr/local/lib -lc -lfetch -lpcre -lutil

.include <bsd.lib.mk>
