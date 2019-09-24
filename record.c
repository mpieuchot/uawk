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

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "awk.h"
#include "ytab.h"

#define	RECSIZE	(8 * 1024)	/* sets limit on records, fields, etc., etc. */

char	*file	= "";
char	*record;		/* points to $0 */
int	recsize	= RECSIZE;
char	*fields;
int	fieldssize = RECSIZE;

Cell	**fldtab;	/* pointers to Cells */

#define	MAXFLD	2
int	nfields	= MAXFLD;	/* last allocated slot for $i */

int	donefld;	/* 1 = implies rec broken into fields */
int	donerec;	/* 1 = record is valid (no flds have changed) */

int	lastfld	= 0;	/* last used field */

static Cell dollar0 = { CFLD, NULL, "", 0.0, REC|STR|DONTFREE };
static Cell dollar1 = { CFLD, NULL, "", 0.0, FLD|STR|DONTFREE };

void		 field_alloc(int, int);
void		 field_realloc(int n);
int		 record_read(char **buf, int *bufsize, FILE *inf);
void		 field_purge(int, int);

void
record_init(void)
{
	recsize = RECSIZE;
	record = xmalloc(recsize);
	*record = '\0';

	fieldssize = RECSIZE;
	fields = xmalloc(fieldssize+1);

	fldtab = xcalloc(nfields+1, sizeof(Cell *));
	fldtab[0] = xmalloc(sizeof(Cell));
	*fldtab[0] = dollar0;
	fldtab[0]->sval = record;
	fldtab[0]->nval = xstrdup("0");
	field_alloc(1, nfields);
}

/*
 * get next input record
 */
int
record_get(FILE *infile)
{			/* note: cares whether buf == record */
	char *buf = record;
	int c, bufsize = recsize, savebufsize = recsize;
	unsigned char saveb0;

	donefld = 0;
	donerec = 1;
	saveb0 = buf[0];
	buf[0] = 0;
	c = record_read(&buf, &bufsize, infile);
	if (c != 0 || buf[0] != '\0') {	/* normal record */
		if (freeable(fldtab[0]))
			xfree(fldtab[0]->sval);
		fldtab[0]->sval = buf;	/* buf == record */
		fldtab[0]->tval = REC | STR | DONTFREE;
		if (is_number(fldtab[0]->sval)) {
			fldtab[0]->fval = atof(fldtab[0]->sval);
			fldtab[0]->tval |= NUM;
		}
		fval_set(nrloc, nrloc->fval+1);
		record = buf;
		recsize = bufsize;
		return 1;
	}
	buf[0] = saveb0;
	record = buf;
	recsize = savebufsize;
	return 0;	/* true end of file */
}

/*
 * read one record into buf
 */
int
record_read(char **pbuf, int *pbufsize, FILE *inf)
{
	int c, sep = '\n';
	char *rr, *buf = *pbuf;
	int bufsize = *pbufsize;

	rr = buf;
	while ((c=getc(inf)) != sep && c != EOF) {
		if (rr-buf+1 > bufsize) {
			xadjbuf(&buf, &bufsize, 1+rr-buf, recsize, &rr,
			    "record_read 1");
		}
		*rr++ = c;
	}
	xadjbuf(&buf, &bufsize, 1+rr-buf, recsize, &rr, "record_read 3");
	*rr = 0;
	   DPRINTF( ("record_read saw <%s>, returns %d\n", buf, c == EOF && rr == buf ? 0 : 1) );
	*pbuf = buf;
	*pbufsize = bufsize;
	return c == EOF && rr == buf ? 0 : 1;
}

/*
 * create fields from current record
 *
 * this relies on having fields[] the same length as $0
 * the fields are all stored in this one array with \0's
 * possibly with a final trailing \0 not associated with any field
 */
void
field_from_record(void)
{
	char *r, *fr, sep;
	Cell *p;
	int i, j, n;

	if (donefld)
		return;
	if (!isstr(fldtab[0]))
		sval_get(fldtab[0]);
	r = fldtab[0]->sval;
	n = strlen(r);
	if (n > fieldssize) {
		xfree(fields);
		fields = xmalloc(n+2); /* possibly 2 final \0s */
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
			field_realloc(i);
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
	field_purge(i+1, lastfld);	/* clean out junk from previous record */
	lastfld = i;
	donefld = 1;
	for (j = 1; j <= lastfld; j++) {
		p = fldtab[j];
		if(is_number(p->sval)) {
			p->fval = atof(p->sval);
			p->tval |= NUM;
		}
	}
	fval_set(nfloc, (double) lastfld);
	if (debug) {
		for (j = 0; j <= lastfld; j++) {
			p = fldtab[j];
			printf("field %d (%s): |%s|\n", j, p->nval, p->sval);
		}
	}
}

void
record_validate(Cell *x)
{
	if (isfld(x) && !donefld)
		field_from_record();
	if (isrec(x) && !donerec)
		record_parse();
}

/*
 * clean out fields n1 .. n2 inclusive
 *
 * nvals remain intact
 */
void
field_purge(int n1, int n2)
{
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

/*
 * add field n after end of existing lastfld
 */
void
field_add(int n)
{
	if (n > nfields)
		field_realloc(n);
	field_purge(lastfld+1, n);
	lastfld = n;
	fval_set(nfloc, (double) n);
}

/*
 * get nth field
 */
Cell *
field_get(int n)
{
	if (n < 0)
		FATAL("trying to access out of range field %d", n);
	/* fields after NF are empty */
	if (n > nfields) {
		/* but does not increase NF */
		field_realloc(n);
	}
	return(fldtab[n]);
}

/*
 * create $n1..$n2 inclusive
 */
void
field_alloc(int n1, int n2)
{
	char temp[50];
	int i;

	for (i = n1; i <= n2; i++) {
		fldtab[i] = xmalloc(sizeof (struct Cell));
		*fldtab[i] = dollar1;
		snprintf(temp, sizeof temp, "%d", i);
		fldtab[i]->nval = xstrdup(temp);
	}
}

/*
 * make new fields up to at least $n
 */
void
field_realloc(int n)
{
	int nf = 2 * nfields;
	size_t s;

	if (n > nf)
		nf = n;
	s = (nf+1) * (sizeof (struct Cell *));  /* freebsd: how much do we need? */
	if (s / sizeof(struct Cell *) - 1 == nf) /* didn't overflow */
		fldtab = xrealloc(fldtab, s);
	else					/* overflow sizeof int */
		xfree(fldtab);	/* make it null */
	field_alloc(nfields+1, nf);
	nfields = nf;
}

/*
 * create $0 from $1..$NF if necessary
 */
void
record_parse(void)
{
	int i;
	char *r, *p;

	if (donerec == 1)
		return;
	r = record;
	for (i = 1; i <= *NF; i++) {
		p = sval_get(fldtab[i]);
		xadjbuf(&record, &recsize, 1+strlen(p)+r-record, recsize, &r,
		    "record_parse 1");
		while ((*r = *p++) != 0)
			r++;
		if (i < *NF) {
			xadjbuf(&record, &recsize, 2+strlen(" ")+r-record,
			    recsize, &r, "record_parse 2");
			for (p = " "; (*r = *p++) != 0; )
				r++;
		}
	}
	xadjbuf(&record, &recsize, 2+r-record, recsize, &r, "record_parse 3");
		FATAL("built giant record `%.30s...'", record);
	*r = '\0';
	   DPRINTF( ("in record_parse fldtab[0]=%p\n", (void*)fldtab[0]) );

	if (freeable(fldtab[0]))
		xfree(fldtab[0]->sval);
	fldtab[0]->tval = REC | STR | DONTFREE;
	fldtab[0]->sval = record;

	   DPRINTF( ("in record_parse fldtab[0]=%p\n", (void*)fldtab[0]) );
	   DPRINTF( ("record_parse = |%s|\n", record) );
	donerec = 1;
}

/* strtod is supposed to be a proper test of what's a valid number */
/* appears to be broken in gcc on linux: thinks 0x123 is a valid FP number */
/* wrong: violates 4.10.1.4 of ansi C standard */
int
is_number(const char *s)
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
