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

double	*NF;		/* number of fields in current record */
double	*NR;		/* number of current record */

Cell		*nrloc;		/* NR */
Cell		*nfloc;		/* NF */
Cell		*symtabloc;	/* SYMTAB */

Cell		*nullloc;	/* empty cell, used for if(x)... tests */
Cell		*literal0;

int		 hash(const char *, int);
Cell		*lookup(const char *, struct array *);
void		 rehash(struct array *);
struct array	*symtab_alloc(int);
void		 symtab_free(Cell *);

void
symtab_init(void)
{
	symtab = symtab_alloc(NSYMTAB);

	literal0 = symtab_set("0", "0", 0.0, NUM|STR|CON|DONTFREE);
	nullloc = symtab_set("$zero&null", "", 0.0, NUM|STR|CON|DONTFREE);
	nullnode = cell2node(nullloc, CCON);

	nfloc = symtab_set("NF", "", 0.0, NUM);
	NF = &nfloc->fval;
	nrloc = symtab_set("NR", "", 0.0, NUM);
	NR = &nrloc->fval;
	symtabloc = symtab_set("SYMTAB", "", 0.0, ARR);
	symtabloc->sval = (char *) symtab;
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

void
symtab_free(Cell *ap)
{
	Cell *cp, *temp;
	struct array *tp;
	int i;

	if (!isarr(ap))
		return;
	tp = (struct array *) ap->sval;
	if (tp == NULL)
		return;
	for (i = 0; i < tp->size; i++) {
		for (cp = tp->tab[i]; cp != NULL; cp = temp) {
			xfree(cp->nval);
			if (freeable(cp))
				xfree(cp->sval);
			temp = cp->cnext;	/* avoids freeing then using */
			free(cp); 
			tp->nelem--;
		}
		tp->tab[i] = 0;
	}
	if (tp->nelem != 0)
		FATAL("can't happen: inconsistent element count freeing %s", ap->nval);
	free(tp->tab);
	free(tp);
}

Cell *
symtab_set(const char *n, const char *s, double f, unsigned t)
{
	struct array *tp = symtab;
	int h;
	Cell *p;

	if (n != NULL && (p = lookup(n, tp)) != NULL) {
		   DPRINTF( ("symtab_set found %p: n=%s s=\"%s\" f=%g t=%o\n",
			(void*)p, NN(p->nval), NN(p->sval), p->fval, p->tval) );
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
	   DPRINTF( ("symtab_set set %p: n=%s s=\"%s\" f=%g t=%o\n",
		(void*)p, p->nval, p->sval, p->fval, p->tval) );
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

/*
 * get float val of a Cell
 */
double
fval_get(Cell *vp)
{
	if ((vp->tval & (NUM | STR)) == 0)
		funnyvar(vp, "read value of");
	if (isfld(vp) && donefld == 0)
		field_from_record();
	else if (isrec(vp) && donerec == 0)
		record_parse();
	if (!isnum(vp)) {	/* not a number */
		vp->fval = atof(vp->sval);	/* best guess */
		if (is_number(vp->sval) && !(vp->tval&CON))
			vp->tval |= NUM;	/* make NUM only sparingly */
	}
	   DPRINTF( ("fval_get %p: %s = %g, t=%o\n",
		(void*)vp, NN(vp->nval), vp->fval, vp->tval) );
	return(vp->fval);
}

/*
 * set float val of a Cell
 */
double
fval_set(Cell *vp, double f)
{
	int fldno;

	if ((vp->tval & (NUM | STR)) == 0) 
		funnyvar(vp, "assign to");
	if (isfld(vp)) {
		donerec = 0;	/* mark $0 invalid */
		fldno = atoi(vp->nval);
		if (fldno > *NF)
			field_add(fldno);
		   DPRINTF( ("setting field %d to %g\n", fldno, f) );
	} else if (isrec(vp)) {
		donefld = 0;	/* mark $1... invalid */
		donerec = 1;
	}
	if (freeable(vp))
		xfree(vp->sval); /* free any previous string */
	vp->tval &= ~STR;	/* mark string invalid */
	vp->tval |= NUM;	/* mark number ok */
	   DPRINTF( ("fval_set %p: %s = %g, t=%o\n", (void*)vp, NN(vp->nval), f, vp->tval) );
	return vp->fval = f;
}

/*
 * get string val of a Cell
 */
char *
sval_get(Cell *vp)
{
	int n;
	double dtemp;

	if ((vp->tval & (NUM | STR)) == 0)
		funnyvar(vp, "read value of");
	if (isfld(vp) && donefld == 0)
		field_from_record();
	else if (isrec(vp) && donerec == 0)
		record_parse();
	if (isstr(vp) == 0) {
		if (freeable(vp))
			xfree(vp->sval);
		if (modf(vp->fval, &dtemp) == 0)	/* it's integral */
			n = xasprintf(&vp->sval, "%.30g", vp->fval);
		else
			n = xasprintf(&vp->sval, "%.6g", vp->fval);
		vp->tval &= ~DONTFREE;
		vp->tval |= STR;
	}
	   DPRINTF( ("sval_get %p: %s = \"%s (%p)\", t=%o\n",
		(void*)vp, NN(vp->nval), vp->sval, vp->sval, vp->tval) );
	return(vp->sval);
}

/*
 * set string val of a Cell
 */
char *
sval_set(Cell *vp, const char *s)
{
	char *t;
	int fldno;

	   DPRINTF( ("starting sval_set %p: %s = \"%s\", t=%o, r,f=%d,%d\n", 
		(void*)vp, NN(vp->nval), s, vp->tval, donerec, donefld) );
	if ((vp->tval & (NUM | STR)) == 0)
		funnyvar(vp, "assign to");
	if (isfld(vp)) {
		donerec = 0;	/* mark $0 invalid */
		fldno = atoi(vp->nval);
		if (fldno > *NF)
			field_add(fldno);
		   DPRINTF( ("setting field %d to %s (%p)\n", fldno, s, s) );
	} else if (isrec(vp)) {
		donefld = 0;	/* mark $1... invalid */
		donerec = 1;
	}
	t = xstrdup(s);	/* in case it's self-assign */
	if (freeable(vp))
		xfree(vp->sval);
	vp->tval &= ~NUM;
	vp->tval |= STR;
	vp->tval &= ~DONTFREE;
	   DPRINTF( ("sval_set %p: %s = \"%s (%p) \", t=%o r,f=%d,%d\n", 
		(void*)vp, NN(vp->nval), t,t, vp->tval, donerec, donefld) );
	return(vp->sval = t);
}

void
funnyvar(Cell *vp, const char *rw)
{
	if (isarr(vp))
		FATAL("can't %s %s; it's an array name.", rw, vp->nval);
	if (vp->tval & FCN)
		FATAL("can't %s %s; it's a function.", rw, vp->nval);
	FATAL("funny variable %p: n=%s s=\"%s\" f=%g t=%o",
		vp, vp->nval, vp->sval, vp->fval, vp->tval);
}
