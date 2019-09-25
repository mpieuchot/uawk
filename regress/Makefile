
MAWK?=	../obj/awk
FILE=	/usr/share/misc/license.template

.MAIN: all

FILE_TARGETS=	00_head10 01_sum 02_begin 03_div_by_0 04_modulo 05_fields
PIPE_TARGETS=	40_line


${FILE_TARGETS}:
	${MAWK} -f ${.CURDIR}/${.TARGET}.awk ${FILE} 2>/dev/null | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

${PIPE_TARGETS}:
	cat ${FILE} | ${MAWK} -f ${.CURDIR}/${.TARGET}.awk - 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

REGRESS_TARGETS= ${FILE_TARGETS} ${PIPE_TARGETS}
.PHONY: ${REGRESS_TARGETS}

.include <bsd.regress.mk>
