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

#define DEBUG
#include <stdio.h>
#include <ctype.h>
#include <setjmp.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "awk.h"
#include "ytab.h"

#define tempfree(x)	if (istemp(x)) tfree(x); else

/*
#undef tempfree

void tempfree(Cell *p) {
	if (p->ctype == OCELL && (p->csub < CUNK || p->csub > CFREE)) {
		WARNING("bad csub %d in Cell %d %s",
			p->csub, p->ctype, p->sval);
	}
	if (istemp(p))
		tfree(p);
}
*/

/* do we really need these? */
/* #ifdef _NFILE */
/* #ifndef FOPEN_MAX */
/* #define FOPEN_MAX _NFILE */
/* #endif */
/* #endif */
/*  */
/* #ifndef	FOPEN_MAX */
/* #define	FOPEN_MAX	40 */	/* max number of open files */
/* #endif */
/*  */
/* #ifndef RAND_MAX */
/* #define RAND_MAX	32767 */	/* all that ansi guarantees */
/* #endif */

jmp_buf env;
extern	int	pairstack[];
extern	Awkfloat	srand_seed;

Node	*winner = NULL;	/* root of parse tree */
Cell	*tmps;		/* free temporary cells for execution */

static Cell	truecell	={ OBOOL, BTRUE, 0, 0, 1.0, NUM };
Cell	*True	= &truecell;
static Cell	falsecell	={ OBOOL, BFALSE, 0, 0, 0.0, NUM };
Cell	*False	= &falsecell;
static Cell	exitcell	={ OJUMP, JEXIT, 0, 0, 0.0, NUM };
Cell	*jexit	= &exitcell;
static Cell	tempcell	={ OCELL, CTEMP, 0, "", 0.0, NUM|STR|DONTFREE };

Node	*curnode = NULL;	/* the node being executed, for debugging */

void	stdinit(void);
void	flush_all(void);

/* buffer memory management */
int adjbuf(char **pbuf, int *psiz, int minlen, int quantum, char **pbptr,
	const char *whatrtn)
/* pbuf:    address of pointer to buffer being managed
 * psiz:    address of buffer size variable
 * minlen:  minimum length of buffer needed
 * quantum: buffer size quantum
 * pbptr:   address of movable pointer into buffer, or 0 if none
 * whatrtn: name of the calling routine if failure should cause fatal error
 *
 * return   0 for realloc failure, !=0 for success
 */
{
	if (minlen > *psiz) {
		char *tbuf;
		int rminlen = quantum ? minlen % quantum : 0;
		int boff = pbptr ? *pbptr - *pbuf : 0;
		/* round up to next multiple of quantum */
		if (rminlen)
			minlen += quantum - rminlen;
		tbuf = (char *) realloc(*pbuf, minlen);
		DPRINTF( ("adjbuf %s: %d %d (pbuf=%p, tbuf=%p)\n", whatrtn, *psiz, minlen, *pbuf, tbuf) );
		if (tbuf == NULL) {
			if (whatrtn)
				FATAL("out of memory in %s", whatrtn);
			return 0;
		}
		*pbuf = tbuf;
		*psiz = minlen;
		if (pbptr)
			*pbptr = tbuf + boff;
	}
	return 1;
}

void run(Node *a)	/* execution of parse tree starts here */
{
	stdinit();
	execute(a);
	closeall();
}

Cell *execute(Node *u)	/* execute a node of the parse tree */
{
	Cell *(*proc)(Node **, int);
	Cell *x;
	Node *a;

	if (u == NULL)
		return(True);
	for (a = u; ; a = a->nnext) {
		curnode = a;
		if (isvalue(a)) {
			x = (Cell *) (a->narg[0]);
			if (isfld(x) && !donefld)
				fldbld();
			else if (isrec(x) && !donerec)
				recbld();
			return(x);
		}
		if (notlegal(a->nobj))	/* probably a Cell* but too risky to print */
			FATAL("illegal statement");
		proc = proctab[a->nobj-FIRSTTOKEN];
		x = (*proc)(a->narg, a->nobj);
		if (isfld(x) && !donefld)
			fldbld();
		else if (isrec(x) && !donerec)
			recbld();
		if (isexpr(a))
			return(x);
		if (isjump(x))
			return(x);
		if (a->nnext == NULL)
			return(x);
		tempfree(x);
	}
}


Cell *program(Node **a, int n)	/* execute an awk program */
{				/* a[0] = BEGIN, a[1] = body, a[2] = END */
	Cell *x;

	if (setjmp(env) != 0)
		goto ex;
	if (a[0]) {		/* BEGIN */
		x = execute(a[0]);
		if (isexit(x))
			return(True);
		if (isjump(x))
			FATAL("illegal break, continue, next or nextfile from BEGIN");
		tempfree(x);
	}
	if (a[1] || a[2])
		while (getrec(&record, &recsize, 1) > 0) {
			x = execute(a[1]);
			if (isexit(x))
				break;
			tempfree(x);
		}
  ex:
	if (setjmp(env) != 0)	/* handles exit within END */
		goto ex1;
	if (a[2]) {		/* END */
		x = execute(a[2]);
		tempfree(x);
	}
  ex1:
	return(True);
}

struct Frame {	/* stack frame for awk function calls */
	int nargs;	/* number of arguments in this call */
	Cell *fcncell;	/* pointer to Cell for function */
	Cell **args;	/* pointer to array of arguments after execute */
	Cell *retval;	/* return value */
};

#define	NARGS	50	/* max args in a call */

struct Frame *frame = NULL;	/* base of stack frames; dynamically allocated */
int	nframe = 0;		/* number of frames allocated */
struct Frame *fp = NULL;	/* frame pointer. bottom level unused */

Cell *call(Node **a, int n)	/* function call.  very kludgy and fragile */
{
	static Cell newcopycell = { OCELL, CCOPY, 0, "", 0.0, NUM|STR|DONTFREE };
	int i, ncall, ndef;
	int freed = 0; /* handles potential double freeing when fcn & param share a tempcell */
	Node *x;
	Cell *args[NARGS], *oargs[NARGS];	/* BUG: fixed size arrays */
	Cell *y, *z, *fcn;
	char *s;

	fcn = execute(a[0]);	/* the function itself */
	s = fcn->nval;
	if (!isfcn(fcn))
		FATAL("calling undefined function %s", s);
	if (frame == NULL) {
		fp = frame = (struct Frame *) calloc(nframe += 100, sizeof(struct Frame));
		if (frame == NULL)
			FATAL("out of space for stack frames calling %s", s);
	}
	for (ncall = 0, x = a[1]; x != NULL; x = x->nnext)	/* args in call */
		ncall++;
	ndef = (int) fcn->fval;			/* args in defn */
	   DPRINTF( ("calling %s, %d args (%d in defn), fp=%d\n", s, ncall, ndef, (int) (fp-frame)) );
	if (ncall > ndef)
		WARNING("function %s called with %d args, uses only %d",
			s, ncall, ndef);
	if (ncall + ndef > NARGS)
		FATAL("function %s has %d arguments, limit %d", s, ncall+ndef, NARGS);
	for (i = 0, x = a[1]; x != NULL; i++, x = x->nnext) {	/* get call args */
		   DPRINTF( ("evaluate args[%d], fp=%d:\n", i, (int) (fp-frame)) );
		y = execute(x);
		oargs[i] = y;
		   DPRINTF( ("args[%d]: %s %f <%s>, t=%o\n",
			   i, NN(y->nval), y->fval, isarr(y) ? "(array)" : NN(y->sval), y->tval) );
		if (isfcn(y))
			FATAL("can't use function %s as argument in %s", y->nval, s);
		if (isarr(y))
			args[i] = y;	/* arrays by ref */
		else
			args[i] = copycell(y);
		tempfree(y);
	}
	for ( ; i < ndef; i++) {	/* add null args for ones not provided */
		args[i] = gettemp();
		*args[i] = newcopycell;
	}
	fp++;	/* now ok to up frame */
	if (fp >= frame + nframe) {
		int dfp = fp - frame;	/* old index */
		frame = reallocarray(frame, (nframe += 100),
		    sizeof(struct Frame));
		if (frame == NULL)
			FATAL("out of space for stack frames in %s", s);
		fp = frame + dfp;
	}
	fp->fcncell = fcn;
	fp->args = args;
	fp->nargs = ndef;	/* number defined with (excess are locals) */
	fp->retval = gettemp();

	   DPRINTF( ("start exec of %s, fp=%d\n", s, (int) (fp-frame)) );
	y = execute((Node *)(fcn->sval));	/* execute body */
	   DPRINTF( ("finished exec of %s, fp=%d\n", s, (int) (fp-frame)) );

	for (i = 0; i < ndef; i++) {
		Cell *t = fp->args[i];
		if (isarr(t)) {
			if (t->csub == CCOPY) {
				if (i >= ncall) {
					freesymtab(t);
					t->csub = CTEMP;
					tempfree(t);
				} else {
					oargs[i]->tval = t->tval;
					oargs[i]->tval &= ~(STR|NUM|DONTFREE);
					oargs[i]->sval = t->sval;
					tempfree(t);
				}
			}
		} else if (t != y) {	/* kludge to prevent freeing twice */
			t->csub = CTEMP;
			tempfree(t);
		} else if (t == y && t->csub == CCOPY) {
			t->csub = CTEMP;
			tempfree(t);
			freed = 1;
		}
	}
	tempfree(fcn);
	if (isexit(y))
		return y;
	if (freed == 0) {
		tempfree(y);	/* don't free twice! */
	}
	z = fp->retval;			/* return value */
	   DPRINTF( ("%s returns %g |%s| %o\n", s, getfval(z), getsval(z), z->tval) );
	fp--;
	return(z);
}

Cell *copycell(Cell *x)	/* make a copy of a cell in a temp */
{
	Cell *y;

	y = gettemp();
	y->csub = CCOPY;	/* prevents freeing until call is over */
	y->nval = x->nval;	/* BUG? */
	if (isstr(x))
		y->sval = tostring(x->sval);
	y->fval = x->fval;
	y->tval = x->tval & ~(CON|FLD|REC|DONTFREE);	/* copy is not constant or field */
							/* is DONTFREE right? */
	return y;
}

Cell *arg(Node **a, int n)	/* nth argument of a function */
{

	n = ptoi(a[0]);	/* argument number, counting from 0 */
	   DPRINTF( ("arg(%d), fp->nargs=%d\n", n, fp->nargs) );
	if (n+1 > fp->nargs)
		FATAL("argument #%d of function %s was not supplied",
			n+1, fp->fcncell->nval);
	return fp->args[n];
}

Cell *jump(Node **a, int n)	/* return */
{
	Cell *y;

	switch (n) {
	case EXIT:
		if (a[0] != NULL) {
			y = execute(a[0]);
			errorflag = (int) getfval(y);
			tempfree(y);
		}
		longjmp(env, 1);
	default:	/* can't happen */
		FATAL("illegal jump type %d", n);
	}
	return 0;	/* not reached */
}

Cell *getnf(Node **a, int n)	/* get NF */
{
	if (donefld == 0)
		fldbld();
	return (Cell *) a[0];
}

Cell *boolop(Node **a, int n)	/* a[0] || a[1], a[0] && a[1], !a[0] */
{
	Cell *x, *y;
	int i;

	x = execute(a[0]);
	i = istrue(x);
	tempfree(x);
	switch (n) {
	case BOR:
		if (i) return(True);
		y = execute(a[1]);
		i = istrue(y);
		tempfree(y);
		if (i) return(True);
		else return(False);
	case AND:
		if ( !i ) return(False);
		y = execute(a[1]);
		i = istrue(y);
		tempfree(y);
		if (i) return(True);
		else return(False);
	case NOT:
		if (i) return(False);
		else return(True);
	default:	/* can't happen */
		FATAL("unknown boolean operator %d", n);
	}
	return 0;	/*NOTREACHED*/
}

Cell *relop(Node **a, int n)	/* a[0 < a[1], etc. */
{
	int i;
	Cell *x, *y;
	Awkfloat j;

	x = execute(a[0]);
	y = execute(a[1]);
	if (x->tval&NUM && y->tval&NUM) {
		j = x->fval - y->fval;
		i = j<0? -1: (j>0? 1: 0);
	} else {
		i = strcmp(getsval(x), getsval(y));
	}
	tempfree(x);
	tempfree(y);
	switch (n) {
	case LT:	if (i<0) return(True);
			else return(False);
	case LE:	if (i<=0) return(True);
			else return(False);
	case NE:	if (i!=0) return(True);
			else return(False);
	case EQ:	if (i == 0) return(True);
			else return(False);
	case GE:	if (i>=0) return(True);
			else return(False);
	case GT:	if (i>0) return(True);
			else return(False);
	default:	/* can't happen */
		FATAL("unknown relational operator %d", n);
	}
	return 0;	/*NOTREACHED*/
}

void tfree(Cell *a)	/* free a tempcell */
{
	if (freeable(a)) {
		   DPRINTF( ("freeing %s %s %o\n", NN(a->nval), NN(a->sval), a->tval) );
		xfree(a->sval);
	}
	if (a == tmps)
		FATAL("tempcell list is curdled");
	a->cnext = tmps;
	tmps = a;
}

Cell *gettemp(void)	/* get a tempcell */
{	int i;
	Cell *x;

	if (!tmps) {
		tmps = (Cell *) calloc(100, sizeof(Cell));
		if (!tmps)
			FATAL("out of space for temporaries");
		for(i = 1; i < 100; i++)
			tmps[i-1].cnext = &tmps[i];
		tmps[i-1].cnext = 0;
	}
	x = tmps;
	tmps = x->cnext;
	*x = tempcell;
	return(x);
}

Cell *indirect(Node **a, int n)	/* $( a[0] ) */
{
	Awkfloat val;
	Cell *x;
	int m;
	char *s;

	x = execute(a[0]);
	val = getfval(x);	/* freebsd: defend against super large field numbers */
	if ((Awkfloat)INT_MAX < val)
		FATAL("trying to access out of range field %s", x->nval);
	m = (int) val;
	if (m == 0 && !is_number(s = getsval(x)))	/* suspicion! */
		FATAL("illegal field $(%s), name \"%s\"", s, x->nval);
		/* BUG: can x->nval ever be null??? */
	tempfree(x);
	x = fieldadr(m);
	x->ctype = OCELL;	/* BUG?  why are these needed? */
	x->csub = CFLD;
	return(x);
}

#define	MAXNUMSIZE	50

int format(char **pbuf, int *pbufsize, const char *s, Node *a)	/* printf-like conversions */
{
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
	if ((fmt = (char *) malloc(fmtsz)) == NULL)
		FATAL("out of memory in format()");
	while (*s) {
		adjbuf(&buf, &bufsize, MAXNUMSIZE+1+p-buf, recsize, &p, "format1");
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
		adjbuf(&buf, &bufsize, fmtwd+1+p-buf, recsize, &p, "format2");
		for (t = fmt; (*t++ = *s) != '\0'; s++) {
			if (!adjbuf(&fmt, &fmtsz, MAXNUMSIZE+1+t-fmt, recsize, &t, "format3"))
				FATAL("format item %.30s... ran format() out of memory", os);
			if (isalpha((uschar)*s) && *s != 'l' && *s != 'h' && *s != 'L')
				break;	/* the ansi panoply */
			if (*s == '*') {
				if (a == NULL)
					FATAL("not enough args in printf(%s)", os);
				x = execute(a);
				a = a->nnext;
				snprintf(t-1, fmt + fmtsz - (t-1), "%d", fmtwd=(int) getfval(x));
				if (fmtwd < 0)
					fmtwd = -fmtwd;
				adjbuf(&buf, &bufsize, fmtwd+1+p-buf, recsize, &p, "format");
				t = fmt + strlen(fmt);
				tempfree(x);
			}
		}
		*t = '\0';
		if (fmtwd < 0)
			fmtwd = -fmtwd;
		adjbuf(&buf, &bufsize, fmtwd+1+p-buf, recsize, &p, "format4");

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
			WARNING("weird printf conversion %s", fmt);
			flag = '?';
			break;
		}
		if (a == NULL)
			FATAL("not enough args in printf(%s)", os);
		x = execute(a);
		a = a->nnext;
		n = MAXNUMSIZE;
		if (fmtwd > n)
			n = fmtwd;
		adjbuf(&buf, &bufsize, 1+n+p-buf, recsize, &p, "format5");
		switch (flag) {
		case '?':	/* unknown, so dump it too */
			snprintf(p, buf + bufsize - p, "%s", fmt);
			t = getsval(x);
			n = strlen(t);
			if (fmtwd > n)
				n = fmtwd;
			adjbuf(&buf, &bufsize, 1+strlen(p)+n+p-buf, recsize, &p, "format6");
			p += strlen(p);
			snprintf(p, buf + bufsize - p, "%s", t);
			break;
		case 'f':	snprintf(p, buf + bufsize - p, fmt, getfval(x)); break;
		case 'd':	snprintf(p, buf + bufsize - p, fmt, (long) getfval(x)); break;
		case 'u':	snprintf(p, buf + bufsize - p, fmt, (int) getfval(x)); break;
		case 's':
			t = getsval(x);
			n = strlen(t);
			if (fmtwd > n)
				n = fmtwd;
			if (!adjbuf(&buf, &bufsize, 1+n+p-buf, recsize, &p, "format7"))
				FATAL("huge string/format (%d chars) in printf %.30s... ran format() out of memory", n, t);
			snprintf(p, buf + bufsize - p, fmt, t);
			break;
		case 'c':
			if (isnum(x)) {
				if ((int)getfval(x))
					snprintf(p, buf + bufsize - p, fmt, (int) getfval(x));
				else {
					*p++ = '\0'; /* explicit null byte */
					*p = '\0';   /* next output will start here */
				}
			} else
				snprintf(p, buf + bufsize - p, fmt, getsval(x)[0]);
			break;
		default:
			FATAL("can't happen: bad conversion %c in format()", flag);
		}
		tempfree(x);
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
}

Cell *awkprintf(Node **a, int n)		/* printf */
{	/* a[0] is list of args, starting with format string */
	/* a[1] is redirection operator, a[2] is redirection file */
	FILE *fp;
	Cell *x;
	Node *y;
	char *buf;
	int len;
	int bufsz=3*recsize;

	if ((buf = (char *) malloc(bufsz)) == NULL)
		FATAL("out of memory in awkprintf");
	y = a[0]->nnext;
	x = execute(a[0]);
	if ((len = format(&buf, &bufsz, getsval(x), y)) == -1)
		FATAL("printf string %.30s... too long.  can't happen.", buf);
	tempfree(x);
	if (a[1] == NULL) {
		/* fputs(buf, stdout); */
		fwrite(buf, len, 1, stdout);
		if (ferror(stdout))
			FATAL("write error on stdout");
	} else {
		fp = redirect(ptoi(a[1]), a[2]);
		/* fputs(buf, fp); */
		fwrite(buf, len, 1, fp);
		fflush(fp);
		if (ferror(fp))
			FATAL("write error on %s", filename(fp));
	}
	free(buf);
	return(True);
}

Cell *arith(Node **a, int n)	/* a[0] + a[1], etc.  also -a[0] */
{
	Awkfloat i, j = 0;
	double v;
	Cell *x, *y, *z;

	x = execute(a[0]);
	i = getfval(x);
	tempfree(x);
	if (n != UMINUS) {
		y = execute(a[1]);
		j = getfval(y);
		tempfree(y);
	}
	z = gettemp();
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
	case POWER:
		if (j >= 0 && modf(j, &v) == 0.0)	/* pos integer exponent */
			i = ipow(i, (int) j);
		else
			i = errcheck(pow(i, j), "pow");
		break;
	default:	/* can't happen */
		FATAL("illegal arithmetic operator %d", n);
	}
	setfval(z, i);
	return(z);
}

double ipow(double x, int n)	/* x**n.  ought to be done by pow, but isn't always */
{
	double v;

	if (n <= 0)
		return 1;
	v = ipow(x, n/2);
	if (n % 2 == 0)
		return v * v;
	else
		return x * v * v;
}

Cell *incrdecr(Node **a, int n)		/* a[0]++, etc. */
{
	Cell *x, *z;
	int k;
	Awkfloat xf;

	x = execute(a[0]);
	xf = getfval(x);
	k = (n == PREINCR || n == POSTINCR) ? 1 : -1;
	if (n == PREINCR || n == PREDECR) {
		setfval(x, xf + k);
		return(x);
	}
	z = gettemp();
	setfval(z, xf);
	setfval(x, xf + k);
	tempfree(x);
	return(z);
}

Cell *assign(Node **a, int n)	/* a[0] = a[1], a[0] += a[1], etc. */
{		/* this is subtle; don't muck with it. */
	Cell *x, *y;
	Awkfloat xf, yf;
	double v;

	y = execute(a[1]);
	x = execute(a[0]);
	if (n == ASSIGN) {	/* ordinary assignment */
		if (x == y && !(x->tval & (FLD|REC)))	/* self-assignment: */
			;		/* leave alone unless it's a field */
		else if ((y->tval & (STR|NUM)) == (STR|NUM)) {
			setsval(x, getsval(y));
			x->fval = getfval(y);
			x->tval |= NUM;
		}
		else if (isstr(y))
			setsval(x, getsval(y));
		else if (isnum(y))
			setfval(x, getfval(y));
		else
			funnyvar(y, "read value of");
		tempfree(y);
		return(x);
	}
	xf = getfval(x);
	yf = getfval(y);
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
	case POWEQ:
		if (yf >= 0 && modf(yf, &v) == 0.0)	/* pos integer exponent */
			xf = ipow(xf, (int) yf);
		else
			xf = errcheck(pow(xf, yf), "pow");
		break;
	default:
		FATAL("illegal assignment operator %d", n);
		break;
	}
	tempfree(y);
	setfval(x, xf);
	return(x);
}

Cell *cat(Node **a, int q)	/* a[0] cat a[1] */
{
	Cell *x, *y, *z;
	int n1, n2;
	char *s;
	size_t len;

	x = execute(a[0]);
	y = execute(a[1]);
	getsval(x);
	getsval(y);
	n1 = strlen(x->sval);
	n2 = strlen(y->sval);
	len = n1 + n2 + 1;
	s = (char *) malloc(len);
	if (s == NULL)
		FATAL("out of space concatenating %.15s... and %.15s...",
			x->sval, y->sval);
	strlcpy(s, x->sval, len);
	strlcpy(s+n1, y->sval, len - n1);
	tempfree(x);
	tempfree(y);
	z = gettemp();
	z->sval = s;
	z->tval = STR;
	return(z);
}

Cell *pastat(Node **a, int n)	/* a[0] { a[1] } */
{
	Cell *x;

	if (a[0] == 0)
		x = execute(a[1]);
	else {
		x = execute(a[0]);
		if (istrue(x)) {
			tempfree(x);
			x = execute(a[1]);
		}
	}
	return x;
}

Cell *dopa2(Node **a, int n)	/* a[0], a[1] { a[2] } */
{
	Cell *x;
	int pair;

	pair = ptoi(a[3]);
	if (pairstack[pair] == 0) {
		x = execute(a[0]);
		if (istrue(x))
			pairstack[pair] = 1;
		tempfree(x);
	}
	if (pairstack[pair] == 1) {
		x = execute(a[1]);
		if (istrue(x))
			pairstack[pair] = 0;
		tempfree(x);
		x = execute(a[2]);
		return(x);
	}
	return(False);
}

Cell *condexpr(Node **a, int n)	/* a[0] ? a[1] : a[2] */
{
	Cell *x;

	x = execute(a[0]);
	if (istrue(x)) {
		tempfree(x);
		x = execute(a[1]);
	} else {
		tempfree(x);
		x = execute(a[2]);
	}
	return(x);
}

Cell *ifstat(Node **a, int n)	/* if (a[0]) a[1]; else a[2] */
{
	Cell *x;

	x = execute(a[0]);
	if (istrue(x)) {
		tempfree(x);
		x = execute(a[1]);
	} else if (a[2] != 0) {
		tempfree(x);
		x = execute(a[2]);
	}
	return(x);
}

Cell *printstat(Node **a, int n)	/* print a[0] */
{
	Node *x;
	Cell *y;
	FILE *fp;

	if (a[1] == 0)	/* a[1] is redirection operator, a[2] is file */
		fp = stdout;
	else
		fp = redirect(ptoi(a[1]), a[2]);
	for (x = a[0]; x != NULL; x = x->nnext) {
		y = execute(x);
		fputs(getpssval(y), fp);
		tempfree(y);
		if (x->nnext == NULL)
			fputs(*ORS, fp);
		else
			fputs(*OFS, fp);
	}
	if (a[1] != 0)
		fflush(fp);
	if (ferror(fp))
		FATAL("write error on %s", filename(fp));
	return(True);
}

Cell *nullproc(Node **a, int n)
{
	n = n;
	a = a;
	return 0;
}


FILE *redirect(int a, Node *b)	/* set up all i/o redirections */
{
	FILE *fp;
	Cell *x;
	char *fname;

	x = execute(b);
	fname = getsval(x);
	fp = openfile(a, fname);
	if (fp == NULL)
		FATAL("can't open file %s", fname);
	tempfree(x);
	return fp;
}

struct files {
	FILE	*fp;
	const char	*fname;
	int	mode;	/* '|', 'a', 'w' => LE/LT, GT */
} *files;

int nfiles;

void stdinit(void)	/* in case stdin, etc., are not constants */
{
	nfiles = FOPEN_MAX;
	files = calloc(nfiles, sizeof(*files));
	if (files == NULL)
		FATAL("can't allocate file memory for %u files", nfiles);
        files[0].fp = stdin;
	files[0].fname = "/dev/stdin";
	files[0].mode = LT;
        files[1].fp = stdout;
	files[1].fname = "/dev/stdout";
	files[1].mode = GT;
        files[2].fp = stderr;
	files[2].fname = "/dev/stderr";
	files[2].mode = GT;
}

FILE *openfile(int a, const char *us)
{
	const char *s = us;
	int i, m;
	FILE *fp = 0;

	if (*s == '\0')
		FATAL("null file name in print or getline");
	for (i=0; i < nfiles; i++)
		if (files[i].fname && strcmp(s, files[i].fname) == 0) {
			if (a == files[i].mode || (a==APPEND && files[i].mode==GT))
				return files[i].fp;
		}

	for (i=0; i < nfiles; i++)
		if (files[i].fp == 0)
			break;
	if (i >= nfiles) {
		struct files *nf;
		int nnf = nfiles + FOPEN_MAX;
		nf = reallocarray(files, nnf, sizeof(*nf));
		if (nf == NULL)
			FATAL("cannot grow files for %s and %d files", s, nnf);
		memset(&nf[nfiles], 0, FOPEN_MAX * sizeof(*nf));
		nfiles = nnf;
		files = nf;
	}
	fflush(stdout);	/* force a semblance of order */
	m = a;
	if (a == GT) {
		fp = fopen(s, "w");
	} else if (a == APPEND) {
		fp = fopen(s, "a");
		m = GT;	/* so can mix > and >> */
	} else if (a == '|') {	/* output pipe */
		fp = popen(s, "w");
	} else if (a == LE) {	/* input pipe */
		fp = popen(s, "r");
	} else if (a == LT) {	/* getline <file */
		fp = strcmp(s, "-") == 0 ? stdin : fopen(s, "r");	/* "-" is stdin */
	} else	/* can't happen */
		FATAL("illegal redirection %d", a);
	if (fp != NULL) {
		files[i].fname = tostring(s);
		files[i].fp = fp;
		files[i].mode = m;
	}
	return fp;
}

const char *filename(FILE *fp)
{
	int i;

	for (i = 0; i < nfiles; i++)
		if (fp == files[i].fp)
			return files[i].fname;
	return "???";
}

void closeall(void)
{
	int i, stat;

	for (i = 0; i < FOPEN_MAX; i++) {
		if (files[i].fp) {
			if (ferror(files[i].fp))
				WARNING( "i/o error occurred on %s", files[i].fname );
			if (files[i].mode == '|' || files[i].mode == LE)
				stat = pclose(files[i].fp);
			else
				stat = fclose(files[i].fp);
			if (stat == EOF)
				WARNING( "i/o error occurred while closing %s", files[i].fname );
		}
	}
}

void flush_all(void)
{
	int i;

	for (i = 0; i < nfiles; i++)
		if (files[i].fp)
			fflush(files[i].fp);
}
