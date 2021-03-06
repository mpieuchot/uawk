/*	$OpenBSD: run.c,v 1.44 2019/08/13 10:45:56 fcambus Exp $	*/
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

#include <assert.h>
#include <stdio.h>
#include <ctype.h>
#include <setjmp.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "awk.h"
#include "ytab.h"

#define istrue(n)	((n)->ctype == CTRUE)
#define istemp(n)	((n)->ctype == CTEMP)
#define isvalue(n)	((n)->ntype == NVALUE)
#define isexpr(n)	((n)->ntype == NEXPR)
#define isnum(n)	((n)->tval & NUM)

Cell		*tcell_get(void);
void		 tcell_put(Cell *);
int		 format(char **, int *, const char *, Node *);
int		 pclose(FILE *);
FILE		*popen(const char *, const char *);

jmp_buf env;

Cell	*tmps;		/* free temporary cells for execution */

static Cell	truecell	={ CTRUE, 0, 0, 1.0, NUM };
Cell	*True	= &truecell;
static Cell	falsecell	={ CFALSE, 0, 0, 0.0, NUM };
Cell	*False	= &falsecell;
static Cell	tempcell	={ CTEMP, 0, "", 0.0, NUM|STR|DONTFREE };

Node	*curnode = NULL;	/* the node being executed, for debugging */

/* buffer memory management */
/* pbuf:    address of pointer to buffer being managed
 * psiz:    address of buffer size variable
 * minlen:  minimum length of buffer needed
 * quantum: buffer size quantum
 * pbptr:   address of movable pointer into buffer, or 0 if none
 * whatrtn: name of the calling routine if failure should cause fatal error
 */
void
xadjbuf(char **pbuf, int *psiz, int minlen, int quantum, char **pbptr,
	const char *whatrtn)
{
	if (minlen > *psiz) {
		char *tbuf;
		int rminlen = quantum ? minlen % quantum : 0;
		int boff = pbptr ? *pbptr - *pbuf : 0;
		/* round up to next multiple of quantum */
		if (rminlen)
			minlen += quantum - rminlen;
		tbuf = xrealloc(*pbuf, minlen);
		DPRINTF("adjbuf %s: %d %d (pbuf=%p, tbuf=%p)\n", whatrtn, *psiz, minlen, *pbuf, tbuf);
		*pbuf = tbuf;
		*psiz = minlen;
		if (pbptr)
			*pbptr = tbuf + boff;
	}
}

/* execute a node of the parse tree */
Cell *
execute(Node *u)
{
	Cell *x;
	Node *a;

	if (u == NULL)
		return True;
	for (a = u; ; a = a->nnext) {
		curnode = a;
		if (isvalue(a)) {
			x = (Cell *) (a->narg[0]);
			record_cache(x);
			return x;
		}
		x = (*a->proc)(a->narg, a->nobj);
		record_cache(x);
		if (isexpr(a))
			return x;
		if (a->nnext == NULL)
			return x;
		tcell_put(x);
	}
}


/* execute an awk program */
/* a[0] = BEGIN, a[1] = body, a[2] = END */
Cell *
f_program(Node **a, int n)
{
	extern FILE *infile;
	Cell *x;

	if (setjmp(env) != 0)
		goto ex;
	if (a[0]) {		/* BEGIN */
		x = execute(a[0]);
		tcell_put(x);
	}
	if (a[1] || a[2]) {
		while (record_get(infile) > 0) {
			x = execute(a[1]);
			tcell_put(x);
		}
	}
  ex:
	if (setjmp(env) != 0)	/* handles exit within END */
		goto ex1;
	if (a[2]) {		/* END */
		x = execute(a[2]);
		tcell_put(x);
	}
  ex1:
	return True;
}

/* return */
Cell *
f_jump(Node **a, int n)
{
	Cell *y;

	switch (n) {
	case EXIT:
		if (a[0] != NULL) {
			y = execute(a[0]);
			errorflag = (int) fval_get(y);
			tcell_put(y);
		}
		longjmp(env, 1);
	default:	/* can't happen */
		FATAL("illegal jump type %d", n);
	}
	return 0;	/* not reached */
}

/* a[0 < a[1], etc. */
Cell *
f_relop(Node **a, int n)
{
	int i;
	Cell *x, *y;
	double j;

	x = execute(a[0]);
	y = execute(a[1]);
	if (x->tval&NUM && y->tval&NUM) {
		j = x->fval - y->fval;
		i = j<0? -1: (j>0? 1: 0);
	} else {
		i = strcmp(sval_get(x), sval_get(y));
	}
	tcell_put(x);
	tcell_put(y);
	switch (n) {
	case LT:	if (i<0) return True;
			else return False;
	case LE:	if (i<=0) return True;
			else return False;
	case NE:	if (i!=0) return True;
			else return False;
	case EQ:	if (i == 0) return True;
			else return False;
	case GE:	if (i>=0) return True;
			else return False;
	case GT:	if (i>0) return True;
			else return False;
	default:	/* can't happen */
		FATAL("unknown relational operator %d", n);
	}
	return 0;	/*NOTREACHED*/
}

/* free a tempcell */
void
tcell_put(Cell *a)
{
	if (!istemp(a))
		return;
	cell_free(a);
	if (a == tmps)
		FATAL("tempcell list is curdled");
	a->cnext = tmps;
	tmps = a;
}

/* get a tempcell */
Cell *
tcell_get(void)
{	int i;
	Cell *x;

	if (!tmps) {
		tmps = xcalloc(100, sizeof(Cell));
		for(i = 1; i < 100; i++)
			tmps[i-1].cnext = &tmps[i];
		tmps[i-1].cnext = 0;
	}
	x = tmps;
	tmps = x->cnext;
	*x = tempcell;
	return x;
}

void
cell_free(Cell *a)
{
	if ((a->tval & (STR|DONTFREE)) == STR) {
		DPRINTF("freeing %s %s %o\n", NN(a->nval), NN(a->sval), a->tval);
		xfree(a->sval);
	}
}

/* $( a[0] ) */
Cell *
f_indirect(Node **a, int n)
{
	double val;
	Cell *x;
	int m;
	char *s;

	x = execute(a[0]);
	val = fval_get(x);	/* freebsd: defend against super large field numbers */
	if ((double)INT_MAX < val)
		FATAL("trying to access out of range field %s", x->nval);
	m = (int) val;
	if (m == 0 && !is_number(s = sval_get(x)))	/* suspicion! */
		FATAL("illegal field $(%s), name \"%s\"", s, x->nval);
		/* BUG: can x->nval ever be null??? */
	tcell_put(x);
	x = field_get(m);
	return x;
}


	/* printf-like conversions */
int
format(char **pbuf, int *pbufsize, const char *s, Node *a)
{
#define	MAXNUMSIZE	50
	char *fmt;
	char *p, *t;
	const char *os;
	Cell *x;
	int flag = 0, n;
	int fmtwd; /* format width */
	int fmtsz = recsize;
	char *buf = *pbuf;
	int bufsize = *pbufsize;

	os = s;
	p = buf;
	fmt = xmalloc(fmtsz);
	while (*s) {
		xadjbuf(&buf, &bufsize, MAXNUMSIZE+1+p-buf, recsize, &p, "format1");
		if (*s != '%') {
			*p++ = *s++;
			continue;
		}
		if (*(s+1) == '%') {
			*p++ = '%';
			s += 2;
			continue;
		}
		/* have to be real careful in case this is a huge number, eg, %100000d */
		fmtwd = atoi(s+1);
		if (fmtwd < 0)
			fmtwd = -fmtwd;
		xadjbuf(&buf, &bufsize, fmtwd+1+p-buf, recsize, &p, "format2");
		for (t = fmt; (*t++ = *s) != '\0'; s++) {
			xadjbuf(&fmt, &fmtsz, MAXNUMSIZE+1+t-fmt, recsize, &t,
			    "format3");
			if (isalpha((unsigned char)*s) && *s != 'l' && *s != 'h' && *s != 'L')
				break;	/* the ansi panoply */
			if (*s == '*') {
				if (a == NULL)
					FATAL("not enough args in printf(%s)", os);
				x = execute(a);
				a = a->nnext;
				snprintf(t-1, fmt + fmtsz - (t-1), "%d", fmtwd=(int) fval_get(x));
				if (fmtwd < 0)
					fmtwd = -fmtwd;
				xadjbuf(&buf, &bufsize, fmtwd+1+p-buf, recsize,
				    &p, "format");
				t = fmt + strlen(fmt);
				tcell_put(x);
			}
		}
		*t = '\0';
		if (fmtwd < 0)
			fmtwd = -fmtwd;
		xadjbuf(&buf, &bufsize, fmtwd+1+p-buf, recsize, &p, "format4");

		switch (*s) {
		case 'f': case 'e': case 'g': case 'E': case 'G':
			flag = 'f';
			break;
		case 'd': case 'i':
			flag = 'd';
			if(*(s-1) == 'l') break;
			*(t-1) = 'l';
			*t = 'd';
			*++t = '\0';
			break;
		case 'o': case 'x': case 'X': case 'u':
			flag = *(s-1) == 'l' ? 'd' : 'u';
			break;
		case 's':
			flag = 's';
			break;
		case 'c':
			flag = 'c';
			break;
		default:
			FATAL("unknown printf conversion %s", fmt);
			break;
		}
		if (a == NULL)
			FATAL("not enough args in printf(%s)", os);
		x = execute(a);
		a = a->nnext;
		n = MAXNUMSIZE;
		if (fmtwd > n)
			n = fmtwd;
		xadjbuf(&buf, &bufsize, 1+n+p-buf, recsize, &p, "format5");
		switch (flag) {
		case '?':	/* unknown, so dump it too */
			snprintf(p, buf + bufsize - p, "%s", fmt);
			t = sval_get(x);
			n = strlen(t);
			if (fmtwd > n)
				n = fmtwd;
			xadjbuf(&buf, &bufsize, 1+strlen(p)+n+p-buf, recsize,
			    &p, "format6");
			p += strlen(p);
			snprintf(p, buf + bufsize - p, "%s", t);
			break;
		case 'f':	snprintf(p, buf + bufsize - p, fmt, fval_get(x)); break;
		case 'd':	snprintf(p, buf + bufsize - p, fmt, (long) fval_get(x)); break;
		case 'u':	snprintf(p, buf + bufsize - p, fmt, (int) fval_get(x)); break;
		case 's':
			t = sval_get(x);
			n = strlen(t);
			if (fmtwd > n)
				n = fmtwd;
			xadjbuf(&buf, &bufsize, 1+n+p-buf, recsize, &p,
			    "format7");
			snprintf(p, buf + bufsize - p, fmt, t);
			break;
		case 'c':
			if (isnum(x)) {
				if ((int)fval_get(x))
					snprintf(p, buf + bufsize - p, fmt, (int) fval_get(x));
				else {
					*p++ = '\0'; /* explicit null byte */
					*p = '\0';   /* next output will start here */
				}
			} else
				snprintf(p, buf + bufsize - p, fmt, sval_get(x)[0]);
			break;
		default:
			FATAL("can't happen: bad conversion %c in format()", flag);
		}
		tcell_put(x);
		p += strlen(p);
		s++;
	}
	*p = '\0';
	free(fmt);
	for ( ; a; a = a->nnext)		/* evaluate any remaining args */
		execute(a);
	*pbuf = buf;
	*pbufsize = bufsize;
	return p - buf;
#undef MAXNUMSIZE
}

/* printf */
Cell *
f_printf(Node **a, int n)
{	/* a[0] is list of args, starting with format string */
	FILE *fp = stdout;
	Cell *x;
	Node *y;
	char *buf;
	int len;
	int bufsz=3*recsize;

	buf = xmalloc(bufsz);
	y = a[0]->nnext;
	x = execute(a[0]);
	if ((len = format(&buf, &bufsz, sval_get(x), y)) == -1)
		FATAL("printf string %.30s... too long.  can't happen.", buf);
	tcell_put(x);
	fwrite(buf, len, 1, fp);
	if (ferror(fp))
		FATAL("write error");
	free(buf);
	return True;
}

/* a[0] + a[1], etc.  also -a[0] */
Cell *
f_arith(Node **a, int n)
{
	double i, j = 0;
	double v;
	Cell *x, *y, *z;

	x = execute(a[0]);
	i = fval_get(x);
	tcell_put(x);
	if (n != UMINUS) {
		y = execute(a[1]);
		j = fval_get(y);
		tcell_put(y);
	}
	z = tcell_get();
	switch (n) {
	case ADD:
		i += j;
		break;
	case MINUS:
		i -= j;
		break;
	case MULT:
		i *= j;
		break;
	case DIVIDE:
		if (j == 0)
			FATAL("division by zero");
		i /= j;
		break;
	case MOD:
		if (j == 0)
			FATAL("division by zero in mod");
		modf(i/j, &v);
		i = i - j * v;
		break;
	case UMINUS:
		i = -i;
		break;
	default:	/* can't happen */
		FATAL("illegal arithmetic operator %d", n);
	}
	fval_set(z, i);
	return z;
}

/* a[0]++, etc. */
Cell *
f_incrdecr(Node **a, int n)
{
	Cell *x, *z;
	int k;
	double xf;

	x = execute(a[0]);
	xf = fval_get(x);
	k = (n == PREINCR || n == POSTINCR) ? 1 : -1;
	if (n == PREINCR || n == PREDECR) {
		fval_set(x, xf + k);
		return x;
	}
	z = tcell_get();
	fval_set(z, xf);
	fval_set(x, xf + k);
	tcell_put(x);
	return z;
}

/* a[0] = a[1], a[0] += a[1], etc. */
Cell *
f_assign(Node **a, int n)
{		/* this is subtle; don't muck with it. */
	Cell *x, *y;
	double xf, yf;
	double v;

	y = execute(a[1]);
	x = execute(a[0]);
	if (n == ASSIGN) {	/* ordinary assignment */
		if (x == y && !(isfld(x) || isrec(x))) /* self-assignment: */
			;		/* leave alone unless it's a field */
		else if ((y->tval & (STR|NUM)) == (STR|NUM)) {
			sval_set(x, sval_get(y));
			x->fval = fval_get(y);
			x->tval |= NUM;
		}
		else if (isstr(y))
			sval_set(x, sval_get(y));
		else if (isnum(y))
			fval_set(x, fval_get(y));
		else {
			FATAL("incorrect assign %p: n=%s s=\"%s\" f=%g t=%o",
			    y, y->nval, y->sval, y->fval, y->tval);
		}
		tcell_put(y);
		return x;
	}
	xf = fval_get(x);
	yf = fval_get(y);
	switch (n) {
	case ADDEQ:
		xf += yf;
		break;
	case SUBEQ:
		xf -= yf;
		break;
	case MULTEQ:
		xf *= yf;
		break;
	case DIVEQ:
		if (yf == 0)
			FATAL("division by zero in /=");
		xf /= yf;
		break;
	case MODEQ:
		if (yf == 0)
			FATAL("division by zero in %%=");
		modf(xf/yf, &v);
		xf = xf - yf * v;
		break;
	default:
		FATAL("illegal assignment operator %d", n);
		break;
	}
	tcell_put(y);
	fval_set(x, xf);
	return x;
}

/* a[0] { a[1] } */
Cell *
f_pastat(Node **a, int n)
{
	Cell *x;

	if (a[0] == 0)
		x = execute(a[1]);
	else {
		x = execute(a[0]);
		if (istrue(x)) {
			tcell_put(x);
			x = execute(a[1]);
		}
	}
	return x;
}

/* a[0] ? a[1] : a[2] */
Cell *
f_condexpr(Node **a, int n)
{
	Cell *x;

	x = execute(a[0]);
	if (istrue(x)) {
		tcell_put(x);
		x = execute(a[1]);
	} else {
		tcell_put(x);
		x = execute(a[2]);
	}
	return x;
}

/* if (a[0]) a[1]; else a[2] */
Cell *
f_if(Node **a, int n)
{
	Cell *x;

	x = execute(a[0]);
	if (istrue(x)) {
		tcell_put(x);
		x = execute(a[1]);
	} else if (a[2] != 0) {
		tcell_put(x);
		x = execute(a[2]);
	}
	return x;
}

/* print a[0] */
Cell *
f_print(Node **a, int n)
{
	FILE *fp = stdout;
	Node *x;
	Cell *y;

	for (x = a[0]; x != NULL; x = x->nnext) {
		y = execute(x);
		fputs(sval_get(y), fp);
		tcell_put(y);
		if (x->nnext == NULL)
			fputs("\n", fp);
		else
			fputs(" ", fp);
	}
	if (ferror(fp))
		FATAL("write error");
	return True;
}

Cell *
f_null(Node **a, int n)
{
	n = n;
	a = a;
	return 0;
}

/*
 * get float val of a Cell
 */
double
fval_get(Cell *vp)
{
	assert(vp->tval & (NUM | STR));

	record_cache(vp);
	if (!isnum(vp)) {	/* not a number */
		vp->fval = atof(vp->sval);	/* best guess */
		if (is_number(vp->sval) && !(vp->tval&CON))
			vp->tval |= NUM;	/* make NUM only sparingly */
	}
	   DPRINTF("getfval %p: %s = %g, t=%o\n",
		(void*)vp, NN(vp->nval), vp->fval, vp->tval);
	return(vp->fval);
}

/*
 * set float val of a Cell
 */
double
fval_set(Cell *vp, double f)
{
	int fldno;

	assert(vp->tval & (NUM | STR));

	if (isfld(vp)) {
		fldno = atoi(vp->nval);
		if (fldno > *NF)
			field_add(fldno);
		   DPRINTF("setting field %d to %g\n", fldno, f);
	}
	record_invalidate(vp);
	cell_free(vp);
	vp->tval &= ~STR;	/* mark string invalid */
	vp->tval |= NUM;	/* mark number ok */
	   DPRINTF("setfval %p: %s = %g, t=%o\n", (void*)vp, NN(vp->nval), f, vp->tval);
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

	assert(vp->tval & (NUM | STR));

	record_cache(vp);
	if (isstr(vp) == 0) {
		cell_free(vp);
		if (modf(vp->fval, &dtemp) == 0)	/* it's integral */
			n = xasprintf(&vp->sval, "%.30g", vp->fval);
		else
			n = xasprintf(&vp->sval, "%.6g", vp->fval);
		vp->tval &= ~DONTFREE;
		vp->tval |= STR;
	}
	   DPRINTF("getsval %p: %s = \"%s (%p)\", t=%o\n",
		(void*)vp, NN(vp->nval), vp->sval, vp->sval, vp->tval);
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

	   DPRINTF("starting sval_set %p: %s = \"%s\", t=%o\n",
		(void*)vp, NN(vp->nval), s, vp->tval);
	assert(vp->tval & (NUM | STR));

	if (isfld(vp)) {
		fldno = atoi(vp->nval);
		if (fldno > *NF)
			field_add(fldno);
		   DPRINTF("setting field %d to %s (%p)\n", fldno, s, s);
	}
	record_invalidate(vp);
	t = xstrdup(s);	/* in case it's self-assign */
	cell_free(vp);
	vp->tval &= ~NUM;
	vp->tval |= STR;
	vp->tval &= ~DONTFREE;
	   DPRINTF("setsval %p: %s = \"%s (%p) \", t=%o\n",
		(void*)vp, NN(vp->nval), t,t, vp->tval);
	return(vp->sval = t);
}
