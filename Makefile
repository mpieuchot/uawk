#	$OpenBSD: Makefile,v 1.16 2017/07/10 21:30:37 espie Exp $

PROG=	awk
SRCS=	ytab.c main.c parse.c proctab.c tran.c lib.c run.c
LDADD=	-lm
DPADD=	${LIBM}
CLEANFILES+=proctab.c maketab ytab.c ytab.h
CFLAGS+=-I. -I${.CURDIR} -DDEBUG
HOSTCFLAGS+=-I. -I${.CURDIR} -DDEBUG

ytab.c ytab.h: parser.y
	${YACC} -o ytab.c -d ${.CURDIR}/parser.y

BUILDFIRST = ytab.h

proctab.c: maketab
	./maketab >proctab.c

maketab: ytab.h maketab.c
	${HOSTCC} ${HOSTCFLAGS} ${.CURDIR}/maketab.c -o $@

.include <bsd.prog.mk>
