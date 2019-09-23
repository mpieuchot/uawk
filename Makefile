#	$OpenBSD: Makefile,v 1.16 2017/07/10 21:30:37 espie Exp $

PROG=	awk
SRCS=	ytab.c main.c node.c symtab.c record.c run.c xmalloc.c
LDADD=	-lm
DPADD=	${LIBM}
CLEANFILES+=ytab.c ytab.h
CFLAGS+=-I. -I${.CURDIR}

DEBUG=-g -DDEBUG -DYYDEBUG

ytab.c ytab.h: parser.y
	${YACC} -o ytab.c -d ${.CURDIR}/parser.y

BUILDFIRST = ytab.h

.include <bsd.prog.mk>
