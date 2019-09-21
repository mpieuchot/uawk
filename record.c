/*	$OpenBSD: lib.c,v 1.25 2017/12/08 17:04:15 deraadt Exp $	*/
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
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include "awk.h"
#include "ytab.h"

#define	RECSIZE	(8 * 1024)	/* sets limit on records, fields, etc., etc. */

char	*file	= "";
char	*record;
int	recsize	= RECSIZE;
char	*fields;
int	fieldssize = RECSIZE;

Cell	**fldtab;	/* pointers to Cells */

#define	MAXFLD	2
int	nfields	= MAXFLD;	/* last allocated slot for $i */

int	donefld;	/* 1 = implies rec broken into fields */
int	donerec;	/* 1 = record is valid (no flds have changed) */

int	lastfld	= 0;	/* last used field */
int	argno	= 1;	/* current input argument number */
extern	Awkfloat *ARGC;

static Cell dollar0 = { OCELL, CFLD, NULL, "", 0.0, REC|STR|DONTFREE };
static Cell dollar1 = { OCELL, CFLD, NULL, "", 0.0, FLD|STR|DONTFREE };

void		 initgetrec(void);
char		*getargv(int);
void		 growfldtab(int n);
int		 readrec(char **buf, int *bufsize, FILE *inf);
void		 cleanfld(int, int);

void recinit(unsigned int n)
{
	if ( (record = (char *) malloc(n)) == NULL
	  || (fields = (char *) malloc(n+1)) == NULL
	  || (fldtab = (Cell **) calloc(nfields+1, sizeof(Cell *))) == NULL
	  || (fldtab[0] = (Cell *) malloc(sizeof(Cell))) == NULL )
		FATAL("out of space for $0 and fields");
	*record = '\0';
	*fldtab[0] = dollar0;
	fldtab[0]->sval = record;
	fldtab[0]->nval = xstrdup("0");
	makefields(1, nfields);
}

void makefields(int n1, int n2)		/* create $n1..$n2 inclusive */
{
	char temp[50];
	int i;

	for (i = n1; i <= n2; i++) {
		fldtab[i] = (Cell *) malloc(sizeof (struct Cell));
		if (fldtab[i] == NULL)
			FATAL("out of space in makefields %d", i);
		*fldtab[i] = dollar1;
		snprintf(temp, sizeof temp, "%d", i);
		fldtab[i]->nval = xstrdup(temp);
	}
}

int getrec(char **pbuf, int *pbufsize, int isrecord)	/* get next input record */
{			/* note: cares whether buf == record */
	int c;
	char *buf = *pbuf;
	uschar saveb0;
	int bufsize = *pbufsize, savebufsize = bufsize;
	extern FILE *infile;

	if (isrecord) {
		donefld = 0;
		donerec = 1;
	}
	saveb0 = buf[0];
	buf[0] = 0;
	setfval(fnrloc, 0.0);
	c = readrec(&buf, &bufsize, infile);
	if (c != 0 || buf[0] != '\0') {	/* normal record */
		if (isrecord) {
			if (freeable(fldtab[0]))
				xfree(fldtab[0]->sval);
			fldtab[0]->sval = buf;	/* buf == record */
			fldtab[0]->tval = REC | STR | DONTFREE;
			if (is_number(fldtab[0]->sval)) {
				fldtab[0]->fval = atof(fldtab[0]->sval);
				fldtab[0]->tval |= NUM;
			}
		}
		setfval(nrloc, nrloc->fval+1);
		setfval(fnrloc, fnrloc->fval+1);
		*pbuf = buf;
		*pbufsize = bufsize;
		return 1;
	}
	/* EOF arrived on this file; set up next */
	if (infile != stdin)
		fclose(infile);
	buf[0] = saveb0;
	*pbuf = buf;
	*pbufsize = savebufsize;
	return 0;	/* true end of file */
}

int readrec(char **pbuf, int *pbufsize, FILE *inf)	/* read one record into buf */
{
	int sep, c;
	char *rr, *buf = *pbuf;
	int bufsize = *pbufsize;

	/*fflush(stdout); avoids some buffering problem but makes it 25% slower*/
	if ((sep = *RS) == 0) {
		sep = '\n';
		while ((c=getc(inf)) == '\n' && c != EOF)	/* skip leading \n's */
			;
		if (c != EOF)
			ungetc(c, inf);
	}
	for (rr = buf; ; ) {
		for (; (c=getc(inf)) != sep && c != EOF; ) {
			if (rr-buf+1 > bufsize)
				if (!adjbuf(&buf, &bufsize, 1+rr-buf, recsize, &rr, "readrec 1"))
					FATAL("input record `%.30s...' too long", buf);
			*rr++ = c;
		}
		if (*RS == sep || c == EOF)
			break;
		if ((c = getc(inf)) == '\n' || c == EOF) /* 2 in a row */
			break;
		if (!adjbuf(&buf, &bufsize, 2+rr-buf, recsize, &rr, "readrec 2"))
			FATAL("input record `%.30s...' too long", buf);
		*rr++ = '\n';
		*rr++ = c;
	}
	if (!adjbuf(&buf, &bufsize, 1+rr-buf, recsize, &rr, "readrec 3"))
		FATAL("input record `%.30s...' too long", buf);
	*rr = 0;
	   DPRINTF( ("readrec saw <%s>, returns %d\n", buf, c == EOF && rr == buf ? 0 : 1) );
	*pbuf = buf;
	*pbufsize = bufsize;
	return c == EOF && rr == buf ? 0 : 1;
}

char *getargv(int n)	/* get ARGV[n] */
{
	Cell *x;
	char *s, temp[50];
	extern Array *ARGVtab;

	snprintf(temp, sizeof temp, "%d", n);
	if (lookup(temp, ARGVtab) == NULL)
		return NULL;
	x = setsymtab(temp, "", 0.0, STR, ARGVtab);
	s = getsval(x);
	   DPRINTF( ("getargv(%d) returns |%s|\n", n, s) );
	return s;
}

void fldbld(void)	/* create fields from current record */
{
	/* this relies on having fields[] the same length as $0 */
	/* the fields are all stored in this one array with \0's */
	/* possibly with a final trailing \0 not associated with any field */
	char *r, *fr, sep;
	Cell *p;
	int i, j, n;

	if (donefld)
		return;
	if (!isstr(fldtab[0]))
		getsval(fldtab[0]);
	r = fldtab[0]->sval;
	n = strlen(r);
	if (n > fieldssize) {
		xfree(fields);
		if ((fields = (char *) malloc(n+2)) == NULL) /* possibly 2 final \0s */
			FATAL("out of space for fields in fldbld %d", n);
		fieldssize = n;
	}
	fr = fields;
	i = 0;	/* number of fields accumulated here */
	for (i = 0; ; ) {
		while (*r == ' ' || *r == '\t' || *r == '\n')
			r++;
		if (*r == 0)
			break;
		i++;
		if (i > nfields)
			growfldtab(i);
		if (freeable(fldtab[i]))
			xfree(fldtab[i]->sval);
		fldtab[i]->sval = fr;
		fldtab[i]->tval = FLD | STR | DONTFREE;
		do
			*fr++ = *r++;
		while (*r != ' ' && *r != '\t' && *r != '\n' && *r != '\0');
		*fr++ = 0;
	}
	*fr = 0;
	if (i > nfields)
		FATAL("record `%.30s...' has too many fields; can't happen", r);
	cleanfld(i+1, lastfld);	/* clean out junk from previous record */
	lastfld = i;
	donefld = 1;
	for (j = 1; j <= lastfld; j++) {
		p = fldtab[j];
		if(is_number(p->sval)) {
			p->fval = atof(p->sval);
			p->tval |= NUM;
		}
	}
	setfval(nfloc, (Awkfloat) lastfld);
	if (dbg) {
		for (j = 0; j <= lastfld; j++) {
			p = fldtab[j];
			printf("field %d (%s): |%s|\n", j, p->nval, p->sval);
		}
	}
}

void cleanfld(int n1, int n2)	/* clean out fields n1 .. n2 inclusive */
{				/* nvals remain intact */
	Cell *p;
	int i;

	for (i = n1; i <= n2; i++) {
		p = fldtab[i];
		if (freeable(p))
			xfree(p->sval);
		p->sval = "";
		p->tval = FLD | STR | DONTFREE;
	}
}

void newfld(int n)	/* add field n after end of existing lastfld */
{
	if (n > nfields)
		growfldtab(n);
	cleanfld(lastfld+1, n);
	lastfld = n;
	setfval(nfloc, (Awkfloat) n);
}

Cell *fieldadr(int n)	/* get nth field */
{
	if (n < 0)
		FATAL("trying to access out of range field %d", n);
	if (n > nfields)	/* fields after NF are empty */
		growfldtab(n);	/* but does not increase NF */
	return(fldtab[n]);
}

void growfldtab(int n)	/* make new fields up to at least $n */
{
	int nf = 2 * nfields;
	size_t s;

	if (n > nf)
		nf = n;
	s = (nf+1) * (sizeof (struct Cell *));  /* freebsd: how much do we need? */
	if (s / sizeof(struct Cell *) - 1 == nf) /* didn't overflow */
		fldtab = (Cell **) realloc(fldtab, s);
	else					/* overflow sizeof int */
		xfree(fldtab);	/* make it null */
	if (fldtab == NULL)
		FATAL("out of space creating %d fields", nf);
	makefields(nfields+1, nf);
	nfields = nf;
}

void recbld(void)	/* create $0 from $1..$NF if necessary */
{
	int i;
	char *r, *p;

	if (donerec == 1)
		return;
	r = record;
	for (i = 1; i <= *NF; i++) {
		p = getsval(fldtab[i]);
		if (!adjbuf(&record, &recsize, 1+strlen(p)+r-record, recsize, &r, "recbld 1"))
			FATAL("created $0 `%.30s...' too long", record);
		while ((*r = *p++) != 0)
			r++;
		if (i < *NF) {
			if (!adjbuf(&record, &recsize, 2+strlen(OFS)+r-record, recsize, &r, "recbld 2"))
				FATAL("created $0 `%.30s...' too long", record);
			for (p = OFS; (*r = *p++) != 0; )
				r++;
		}
	}
	if (!adjbuf(&record, &recsize, 2+r-record, recsize, &r, "recbld 3"))
		FATAL("built giant record `%.30s...'", record);
	*r = '\0';
	   DPRINTF( ("in recbld fldtab[0]=%p\n", (void*)fldtab[0]) );

	if (freeable(fldtab[0]))
		xfree(fldtab[0]->sval);
	fldtab[0]->tval = REC | STR | DONTFREE;
	fldtab[0]->sval = record;

	   DPRINTF( ("in recbld fldtab[0]=%p\n", (void*)fldtab[0]) );
	   DPRINTF( ("recbld = |%s|\n", record) );
	donerec = 1;
}

void fpecatch(int sig)
{
	extern Node *curnode;

	dprintf(STDERR_FILENO, "floating point exception\n");

	if (compile_time != 2 && NR && *NR > 0) {
		dprintf(STDERR_FILENO, " input record number %d", (int) (*FNR));
		dprintf(STDERR_FILENO, "\n");
	}
	if (compile_time != 2 && curnode) {
		dprintf(STDERR_FILENO, " source line number %d", curnode->lineno);
	} else if (compile_time != 2 && lineno) {
		dprintf(STDERR_FILENO, " source line number %d", lineno);
	}
	if (compile_time == 1 && cursource() != NULL) {
		dprintf(STDERR_FILENO, " source file %s", cursource());
	}
	dprintf(STDERR_FILENO, "\n");
	if (dbg > 1)		/* core dump if serious debugging on */
		abort();
	_exit(1);
}

/* strtod is supposed to be a proper test of what's a valid number */
/* appears to be broken in gcc on linux: thinks 0x123 is a valid FP number */
/* wrong: violates 4.10.1.4 of ansi C standard */

#include <math.h>
int is_number(const char *s)
{
	double r;
	char *ep;
	errno = 0;
	r = strtod(s, &ep);
	if (ep == s || r == HUGE_VAL || errno == ERANGE)
		return 0;
	while (*ep == ' ' || *ep == '\t' || *ep == '\n')
		ep++;
	if (*ep == '\0')
		return 1;
	else
		return 0;
}
