/*	$OpenBSD: tran.c,v 1.17 2018/01/24 16:28:25 millert Exp $	*/
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

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "awk.h"

#define	FULLTAB	2		/* rehash when table gets this x full */
#define	GROWTAB 4		/* grow table by this factor */

struct array {			/* symbol table array */
	int	nelem;		/* elements in table right now */
	int	size;		/* size of tab */
	Cell	**tab;		/* hash table pointers */
};

#define	NSYMTAB	50		/* initial size of a symbol table */
struct array	*symtab;	/* main symbol table */

Cell		*nullloc;	/* empty cell, used for if(x)... tests */
Cell		*literal0;

int		 hash(const char *, int);
Cell		*lookup(const char *, struct array *);
void		 rehash(struct array *);
struct array	*symtab_alloc(int);

void
symtab_init(void)
{
	symtab = symtab_alloc(NSYMTAB);

	literal0 = symtab_set("0", "0", 0.0, NUM|STR|CON|DONTFREE);
	nullloc = symtab_set("$zero&null", "", 0.0, NUM|STR|CON|DONTFREE);
	nullnode = cell2node(nullloc, CCON);
}

struct array *
symtab_alloc(int n)
{
	struct array *ap;
	Cell **tp;

	ap = xmalloc(sizeof(*ap));
	tp = xcalloc(n, sizeof(Cell *));
	ap->nelem = 0;
	ap->size = n;
	ap->tab = tp;
	return ap;
}

Cell *
symtab_set(const char *n, const char *s, double f, unsigned t)
{
	struct array *tp = symtab;
	int h;
	Cell *p;

	if (n != NULL && (p = lookup(n, tp)) != NULL) {
		   DPRINTF("setsymtab found %p: n=%s s=\"%s\" f=%g t=%o\n",
			(void*)p, NN(p->nval), NN(p->sval), p->fval, p->tval);
		return p;
	}
	p = xmalloc(sizeof(Cell));
	p->nval = xstrdup(n);
	p->sval = s ? xstrdup(s) : xstrdup("");
	p->fval = f;
	p->tval = t;
	p->ctype = CUNK;
	tp->nelem++;
	if (tp->nelem > FULLTAB * tp->size)
		rehash(tp);
	h = hash(n, tp->size);
	p->cnext = tp->tab[h];
	tp->tab[h] = p;
	   DPRINTF("setsymtab set %p: n=%s s=\"%s\" f=%g t=%o\n",
		(void*)p, p->nval, p->sval, p->fval, p->tval);
	return p;
}

/*
 * form hash value for string s
 */
int
hash(const char *s, int n)
{
	unsigned hashval;

	for (hashval = 0; *s != '\0'; s++)
		hashval = (*s + 31 * hashval);
	return hashval % n;
}

/*
 * rehash items in small table into big one
 */
void
rehash(struct array *tp)
{
	int i, nh, nsz;
	Cell *cp, *op, **np;

	nsz = GROWTAB * tp->size;
	np = xcalloc(nsz, sizeof(Cell *));
	for (i = 0; i < tp->size; i++) {
		for (cp = tp->tab[i]; cp; cp = op) {
			op = cp->cnext;
			nh = hash(cp->nval, nsz);
			cp->cnext = np[nh];
			np[nh] = cp;
		}
	}
	free(tp->tab);
	tp->tab = np;
	tp->size = nsz;
}

/*
 * look for s in tp
 */
Cell *
lookup(const char *s, struct array *tp)
{
	Cell *p;
	int h;

	h = hash(s, tp->size);
	for (p = tp->tab[h]; p != NULL; p = p->cnext)
		if (strcmp(s, p->nval) == 0)
			return p;	/* found it */
	return NULL;			/* not found */
}
