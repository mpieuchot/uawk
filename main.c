/*	$OpenBSD: main.c,v 1.21 2017/10/09 14:51:31 deraadt Exp $	*/
/****************************************************************
Copyright (C) Lucent Technologies 1997
All Rights Reserved

Permission to use, copy, modify, and distribute this software and
its documentation for any purpose and without fee is hereby
granted, provided that the above copyright notice appear in all
copies and that both that the copyright notice and this
permission notice and warranty disclaimer appear in supporting
documentation, and that the name Lucent Technologies or any of
its entities not be used in advertising or publicity pertaining
to distribution of the software without specific, written prior
permission.

LUCENT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
IN NO EVENT SHALL LUCENT OR ANY OF ITS ENTITIES BE LIABLE FOR ANY
SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
THIS SOFTWARE.
****************************************************************/

#include <stdio.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "awk.h"

extern	char	*__progname;

int	dbg	= 0;
char	*cmdname;	/* gets argv[0] for error messages */
extern	FILE	*yyin;	/* lex input file */
char	*lexprog;	/* points to program argument if it exists */
extern	int errorflag;	/* non-zero if any syntax errors; set by yyerror */
int	compile_time = 2;	/* for error printing: */
				/* 2 = cmdline, 1 = compile, 0 = running */

#define	MAX_PFILE	20	/* max number of program files */
char	*pfile[MAX_PFILE];	/* program filenames */
int	npfile = 0;		/* number of filenames */
int	curpfile = 0;		/* current filename */

__dead void usage(void)
{
	fprintf(stderr, "usage: %s [-d] [prog | -f progfile]\tfile ...\n",
	    cmdname);
	exit(1);
}

int main(int argc, char *argv[])
{
	int ch;

	setlocale(LC_ALL, "");
	setlocale(LC_NUMERIC, "C"); /* for parsing cmdline & prog */

	cmdname = __progname;
	if (pledge("stdio rpath wpath cpath proc exec", NULL) == -1) {
		fprintf(stderr, "%s: pledge: incorrect arguments\n", cmdname);
		exit(1);
	}

	while ((ch = getopt(argc, argv, "f:d")) != -1) {
		switch (ch) {
		case 'f':
			if (npfile >= MAX_PFILE - 1)
				FATAL("too many -f options");
			pfile[npfile++] = optarg;
			break;
		case 'd':
			dbg++;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (npfile == 0) {	/* no -f; first argument is program */
		if (argc <= 1)
			usage();
		   DPRINTF( ("program = |%s|\n", argv[0]) );
		lexprog = argv[0];
		argc--;
		argv++;
	}

	signal(SIGFPE, fpecatch);

	yyin = NULL;
	symtab = makesymtab(NSYMTAB);

	recinit(recsize);
	syminit();
	compile_time = 1;

	/* put prog name at front of arglist */
	argc++;
	argv--;
	argv[0] = cmdname;
	arginit(argc, argv);
	yyparse();
	setlocale(LC_NUMERIC, ""); /* back to whatever it is locally */
	   DPRINTF( ("errorflag=%d\n", errorflag) );
	if (errorflag == 0) {
		compile_time = 0;
		execute(rootnode);
	} else
		bracecheck();
	return(errorflag);
}

int pgetc(void)		/* get 1 character from awk program */
{
	int c;

	for (;;) {
		if (yyin == NULL) {
			if (curpfile >= npfile)
				return EOF;
			if (strcmp(pfile[curpfile], "-") == 0)
				yyin = stdin;
			else if ((yyin = fopen(pfile[curpfile], "r")) == NULL)
				FATAL("can't open file %s", pfile[curpfile]);
			lineno = 1;
		}
		if ((c = getc(yyin)) != EOF)
			return c;
		if (yyin != stdin)
			fclose(yyin);
		yyin = NULL;
		curpfile++;
	}
}

char *cursource(void)	/* current source file name */
{
	if (npfile > 0)
		return pfile[curpfile];
	else
		return NULL;
}
