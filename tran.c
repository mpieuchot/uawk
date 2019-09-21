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

#define	FULLTAB	2	/* rehash when table gets this x full */
#define	GROWTAB 4	/* grow table by this factor */

Array	*symtab;	/* main symbol table */

Awkfloat *NF;		/* number of fields in current record */
Awkfloat *NR;		/* number of current record */
Awkfloat *FNR;		/* number of current record in current file */
char	**FILENAME;	/* current filename argument */
Awkfloat *ARGC;		/* number of arguments from command line */

Cell	*nrloc;		/* NR */
Cell	*nfloc;		/* NF */
Cell	*fnrloc;	/* FNR */
Array	*ARGVtab;	/* symbol table containing ARGV[...] */
Cell	*symtabloc;	/* SYMTAB */

Cell	*nullloc;	/* a guaranteed empty cell */
Node	*nullnode;	/* zero&null, converted into a node for comparisons */
Cell	*literal0;

extern Cell **fldtab;

int		 hash(const char *, int);
void		 rehash(Array *);

void syminit(void)	/* initialize symbol table with builtin vars */
{
	literal0 = setsymtab("0", "0", 0.0, NUM|STR|CON|DONTFREE, symtab);
	/* this is used for if(x)... tests: */
	nullloc = setsymtab("$zero&null", "", 0.0, NUM|STR|CON|DONTFREE, symtab);
	nullnode = celltonode(nullloc, CCON);

	FILENAME = &setsymtab("FILENAME", "", 0.0, STR|DONTFREE, symtab)->sval;
	nfloc = setsymtab("NF", "", 0.0, NUM, symtab);
	NF = &nfloc->fval;
	nrloc = setsymtab("NR", "", 0.0, NUM, symtab);
	NR = &nrloc->fval;
	fnrloc = setsymtab("FNR", "", 0.0, NUM, symtab);
	FNR = &fnrloc->fval;
	symtabloc = setsymtab("SYMTAB", "", 0.0, ARR, symtab);
	symtabloc->sval = (char *) symtab;
}

void arginit(int ac, char **av)	/* set up ARGV and ARGC */
{
	Cell *cp;
	int i;
	char temp[50];

	   DPRINTF( ("argc=%d, argv[0]=%s\n", ac, av[0]) );
	ARGC = &setsymtab("ARGC", "", (Awkfloat) ac, NUM, symtab)->fval;
	cp = setsymtab("ARGV", "", 0.0, ARR, symtab);
	ARGVtab = makesymtab(NSYMTAB);	/* could be (int) ARGC as well */
	cp->sval = (char *) ARGVtab;
	for (i = 0; i < ac; i++) {
		snprintf(temp, sizeof temp, "%d", i);
		if (is_number(*av))
			setsymtab(temp, *av, atof(*av), STR|NUM, ARGVtab);
		else
			setsymtab(temp, *av, 0.0, STR, ARGVtab);
		av++;
	}
}

Array *makesymtab(int n)	/* make a new symbol table */
{
	Array *ap;
	Cell **tp;

	ap = (Array *) malloc(sizeof(Array));
	tp = (Cell **) calloc(n, sizeof(Cell *));
	if (ap == NULL || tp == NULL)
		FATAL("out of space in makesymtab");
	ap->nelem = 0;
	ap->size = n;
	ap->tab = tp;
	return(ap);
}

void freesymtab(Cell *ap)	/* free a symbol table */
{
	Cell *cp, *temp;
	Array *tp;
	int i;

	if (!isarr(ap))
		return;
	tp = (Array *) ap->sval;
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
		WARNING("can't happen: inconsistent element count freeing %s", ap->nval);
	free(tp->tab);
	free(tp);
}

Cell *setsymtab(const char *n, const char *s, Awkfloat f, unsigned t, Array *tp)
{
	int h;
	Cell *p;

	if (n != NULL && (p = lookup(n, tp)) != NULL) {
		   DPRINTF( ("setsymtab found %p: n=%s s=\"%s\" f=%g t=%o\n",
			(void*)p, NN(p->nval), NN(p->sval), p->fval, p->tval) );
		return(p);
	}
	p = (Cell *) malloc(sizeof(Cell));
	if (p == NULL)
		FATAL("out of space for symbol table at %s", n);
	p->nval = xstrdup(n);
	p->sval = s ? xstrdup(s) : xstrdup("");
	p->fval = f;
	p->tval = t;
	p->csub = CUNK;
	p->ctype = OCELL;
	tp->nelem++;
	if (tp->nelem > FULLTAB * tp->size)
		rehash(tp);
	h = hash(n, tp->size);
	p->cnext = tp->tab[h];
	tp->tab[h] = p;
	   DPRINTF( ("setsymtab set %p: n=%s s=\"%s\" f=%g t=%o\n",
		(void*)p, p->nval, p->sval, p->fval, p->tval) );
	return(p);
}

int hash(const char *s, int n)	/* form hash value for string s */
{
	unsigned hashval;

	for (hashval = 0; *s != '\0'; s++)
		hashval = (*s + 31 * hashval);
	return hashval % n;
}

void rehash(Array *tp)	/* rehash items in small table into big one */
{
	int i, nh, nsz;
	Cell *cp, *op, **np;

	nsz = GROWTAB * tp->size;
	np = (Cell **) calloc(nsz, sizeof(Cell *));
	if (np == NULL)		/* can't do it, but can keep running. */
		return;		/* someone else will run out later. */
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

Cell *lookup(const char *s, Array *tp)	/* look for s in tp */
{
	Cell *p;
	int h;

	h = hash(s, tp->size);
	for (p = tp->tab[h]; p != NULL; p = p->cnext)
		if (strcmp(s, p->nval) == 0)
			return(p);	/* found it */
	return(NULL);			/* not found */
}

Awkfloat setfval(Cell *vp, Awkfloat f)	/* set float val of a Cell */
{
	int fldno;

	if ((vp->tval & (NUM | STR)) == 0) 
		funnyvar(vp, "assign to");
	if (isfld(vp)) {
		donerec = 0;	/* mark $0 invalid */
		fldno = atoi(vp->nval);
		if (fldno > *NF)
			newfld(fldno);
		   DPRINTF( ("setting field %d to %g\n", fldno, f) );
	} else if (isrec(vp)) {
		donefld = 0;	/* mark $1... invalid */
		donerec = 1;
	}
	if (freeable(vp))
		xfree(vp->sval); /* free any previous string */
	vp->tval &= ~STR;	/* mark string invalid */
	vp->tval |= NUM;	/* mark number ok */
	   DPRINTF( ("setfval %p: %s = %g, t=%o\n", (void*)vp, NN(vp->nval), f, vp->tval) );
	return vp->fval = f;
}

void funnyvar(Cell *vp, const char *rw)
{
	if (isarr(vp))
		FATAL("can't %s %s; it's an array name.", rw, vp->nval);
	if (vp->tval & FCN)
		FATAL("can't %s %s; it's a function.", rw, vp->nval);
	WARNING("funny variable %p: n=%s s=\"%s\" f=%g t=%o",
		vp, vp->nval, vp->sval, vp->fval, vp->tval);
}

char *setsval(Cell *vp, const char *s)	/* set string val of a Cell */
{
	char *t;
	int fldno;

	   DPRINTF( ("starting setsval %p: %s = \"%s\", t=%o, r,f=%d,%d\n", 
		(void*)vp, NN(vp->nval), s, vp->tval, donerec, donefld) );
	if ((vp->tval & (NUM | STR)) == 0)
		funnyvar(vp, "assign to");
	if (isfld(vp)) {
		donerec = 0;	/* mark $0 invalid */
		fldno = atoi(vp->nval);
		if (fldno > *NF)
			newfld(fldno);
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
	   DPRINTF( ("setsval %p: %s = \"%s (%p) \", t=%o r,f=%d,%d\n", 
		(void*)vp, NN(vp->nval), t,t, vp->tval, donerec, donefld) );
	return(vp->sval = t);
}

Awkfloat getfval(Cell *vp)	/* get float val of a Cell */
{
	if ((vp->tval & (NUM | STR)) == 0)
		funnyvar(vp, "read value of");
	if (isfld(vp) && donefld == 0)
		fldbld();
	else if (isrec(vp) && donerec == 0)
		recbld();
	if (!isnum(vp)) {	/* not a number */
		vp->fval = atof(vp->sval);	/* best guess */
		if (is_number(vp->sval) && !(vp->tval&CON))
			vp->tval |= NUM;	/* make NUM only sparingly */
	}
	   DPRINTF( ("getfval %p: %s = %g, t=%o\n",
		(void*)vp, NN(vp->nval), vp->fval, vp->tval) );
	return(vp->fval);
}

static char *get_str_val(Cell *vp, char *fmt)        /* get string val of a Cell */
{
	int n;
	double dtemp;

	if ((vp->tval & (NUM | STR)) == 0)
		funnyvar(vp, "read value of");
	if (isfld(vp) && donefld == 0)
		fldbld();
	else if (isrec(vp) && donerec == 0)
		recbld();
	if (isstr(vp) == 0) {
		if (freeable(vp))
			xfree(vp->sval);
		if (modf(vp->fval, &dtemp) == 0)	/* it's integral */
			n = asprintf(&vp->sval, "%.30g", vp->fval);
		else
			n = asprintf(&vp->sval, fmt, vp->fval);
		if (n == -1)
			FATAL("out of space in get_str_val");
		vp->tval &= ~DONTFREE;
		vp->tval |= STR;
	}
	   DPRINTF( ("getsval %p: %s = \"%s (%p)\", t=%o\n",
		(void*)vp, NN(vp->nval), vp->sval, vp->sval, vp->tval) );
	return(vp->sval);
}

char *getsval(Cell *vp)       /* get string val of a Cell */
{
      return get_str_val(vp, CONVFMT);
}

char *getpssval(Cell *vp)     /* get string val of a Cell for print */
{
      return get_str_val(vp, OFMT);
}


char *xstrdup(const char *s)
{
	char *p;

	p = strdup(s);
	if (p == NULL)
		FATAL("%s: %s", __func__, strerror(errno));
	return p;
}
