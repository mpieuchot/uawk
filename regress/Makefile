
UAWK?=	../obj/uawk
FILE=	/usr/share/misc/license.template

.MAIN: all

FILE_TARGETS=	00_head10 01_sum 02_begin 03_div_by_0 04_modulo 05_fields \
		06_indirect
PIPE_TARGETS=	40_line


${FILE_TARGETS}:
	${UAWK} -f ${.CURDIR}/${.TARGET}.awk ${FILE} 2>/dev/null | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

${PIPE_TARGETS}:
	cat ${FILE} | ${UAWK} -f ${.CURDIR}/${.TARGET}.awk - 2>&1 | \
		diff -u ${.CURDIR}/${.TARGET}.ok /dev/stdin

REGRESS_TARGETS= ${FILE_TARGETS} ${PIPE_TARGETS}
.PHONY: ${REGRESS_TARGETS}

.include <bsd.regress.mk>
