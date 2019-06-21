CC= cc
PROG=	unveilro
SRCS=	unveilro.c

CFLAGS= -O2 -Wall -std=c11 -D_BSD_SOURCE -D_XOPEN_SOURCE=700

LDADD=	-lutil

.include <bsd.prog.mk>
