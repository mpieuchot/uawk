/*	$OpenBSD: parse.c,v 1.7 2017/10/09 14:51:31 deraadt Exp $	*/
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
#include <stdlib.h>
#include "awk.h"
#include "ytab.h"

void		 nodeinit(int, Node *);
Node		*nodealloc(int);
Node		*node1(int, Node *);
Node		*node2(int, Node *, Node *);
Node		*node3(int, Node *, Node *, Node *);
Node		*node4(int, Node *, Node *, Node *, Node *);

struct
{	int value;
	Cell *(*func)(Node **, int);
	const char *name;
} tokens[] = {
	{ XBEGIN, nullproc, "XBEGIN" },
	{ XEND, nullproc, "XEND" },
	{ NL, nullproc, "NL" },
	{ ASGNOP, nullproc, "ASGNOP" },
	{ VAR, nullproc, "VAR" },
	{ IVAR, nullproc, "IVAR" },
	{ NUMBER, nullproc, "NUMBER" },
	{ STRING, nullproc, "STRING" },
	{ NOT, nullproc, "NOT" },
	{ PROGRAM, program,  "PROGRAM" },
	{ BOR, boolop,  "BOR" },
	{ AND, boolop,  "AND" },
	{ NOT, boolop,  "NOT" },
	{ NE, relop,  "NE" },
	{ EQ, relop,  "EQ" },
	{ LE, relop,  "LE" },
	{ LT, relop,  "LT" },
	{ GE, relop,  "GE" },
	{ GT, relop,  "GT" },
	{ INDIRECT, indirect,  "INDIRECT" },
	{ ADD, arith,  "ADD" },
	{ MINUS, arith,  "MINUS" },
	{ MULT, arith,  "MULT" },
	{ DIVIDE, arith,  "DIVIDE" },
	{ MOD, arith,  "MOD" },
	{ UMINUS, arith,  "UMINUS" },
	{ POWER, arith,  "POWER" },
	{ PREINCR, incrdecr,  "PREINCR" },
	{ POSTINCR, incrdecr,  "POSTINCR" },
	{ PREDECR, incrdecr,  "PREDECR" },
	{ POSTDECR, incrdecr,  "POSTDECR" },
	{ PASTAT, pastat,  "PASTAT" },
	{ PASTAT2, dopa2,  "PASTAT2" },
	{ PRINTF, awkprintf,  "PRINTF" },
	{ PRINT, printstat,  "PRINT" },
	{ ASSIGN, assign,  "ASSIGN" },
	{ ADDEQ, assign,  "ADDEQ" },
	{ SUBEQ, assign,  "SUBEQ" },
	{ MULTEQ, assign,  "MULTEQ" },
	{ DIVEQ, assign,  "DIVEQ" },
	{ MODEQ, assign,  "MODEQ" },
	{ POWEQ, assign,  "POWEQ" },
	{ CONDEXPR, condexpr,  "CONDEXPR" },
	{ IF, ifstat,  "IFSTAT" },
	{ EXIT, jump,  "exit" },
};

const char *
tokname(int a)
{
	static char buf[100];
	int i;

	for (i = 0; i < (sizeof(tokens) / sizeof(tokens[0])); i++) {
		if (a == tokens[i].value)
			return tokens[i].name;
	}

	snprintf(buf, sizeof(buf), "token %d", a);
	return buf;
}

void
nodeinit(int a, Node *x)
{
	Cell *(*xproc)(Node **, int) = nullproc;
	int i;

	for (i = 0; i < (sizeof(tokens) / sizeof(tokens[0])); i++) {
		if (a == tokens[i].value) {
			xproc = tokens[i].func;
			break;
		}
	}

	x->nobj = a;
	x->proc = xproc;
}

Node *nodealloc(int n)
{
	Node *x;

	x = (Node *) malloc(sizeof(Node) + (n-1)*sizeof(Node *));
	if (x == NULL)
		FATAL("out of space in nodealloc");
	x->nnext = NULL;
	x->lineno = lineno;
	return(x);
}

Node *exptostat(Node *a)
{
	a->ntype = NSTAT;
	return(a);
}

Node *node1(int a, Node *b)
{
	Node *x;

	x = nodealloc(1);
	nodeinit(a, x);
	x->narg[0]=b;
	return(x);
}

Node *node2(int a, Node *b, Node *c)
{
	Node *x;

	x = nodealloc(2);
	nodeinit(a, x);
	x->narg[0] = b;
	x->narg[1] = c;
	return(x);
}

Node *node3(int a, Node *b, Node *c, Node *d)
{
	Node *x;

	x = nodealloc(3);
	nodeinit(a, x);
	x->narg[0] = b;
	x->narg[1] = c;
	x->narg[2] = d;
	return(x);
}

Node *node4(int a, Node *b, Node *c, Node *d, Node *e)
{
	Node *x;

	x = nodealloc(4);
	nodeinit(a, x);
	x->narg[0] = b;
	x->narg[1] = c;
	x->narg[2] = d;
	x->narg[3] = e;
	return(x);
}

Node *stat1(int a, Node *b)
{
	Node *x;

	x = node1(a,b);
	x->ntype = NSTAT;
	return(x);
}

Node *stat2(int a, Node *b, Node *c)
{
	Node *x;

	x = node2(a,b,c);
	x->ntype = NSTAT;
	return(x);
}

Node *stat3(int a, Node *b, Node *c, Node *d)
{
	Node *x;

	x = node3(a,b,c,d);
	x->ntype = NSTAT;
	return(x);
}

Node *op1(int a, Node *b)
{
	Node *x;

	x = node1(a,b);
	x->ntype = NEXPR;
	return(x);
}

Node *op2(int a, Node *b, Node *c)
{
	Node *x;

	x = node2(a,b,c);
	x->ntype = NEXPR;
	return(x);
}

Node *op3(int a, Node *b, Node *c, Node *d)
{
	Node *x;

	x = node3(a,b,c,d);
	x->ntype = NEXPR;
	return(x);
}

Node *celltonode(Cell *a, int b)
{
	Node *x;

	a->ctype = OCELL;
	a->csub = b;
	x = node1(0, (Node *) a);
	x->ntype = NVALUE;
	return(x);
}

Node *rectonode(void)	/* make $0 into a Node */
{
	extern Cell *literal0;
	return op1(INDIRECT, celltonode(literal0, CUNK));
}

#define PA2NUM	50	/* max number of pat,pat patterns allowed */
int	paircnt;		/* number of them in use */
int	pairstack[PA2NUM];	/* state of each pat,pat */

Node *pa2stat(Node *a, Node *b, Node *c)	/* pat, pat {...} */
{
	Node *x;

	x = node4(PASTAT2, a, b, c, itonp(paircnt));
	if (paircnt++ >= PA2NUM)
		SYNTAX( "limited to %d pat,pat statements", PA2NUM );
	x->ntype = NSTAT;
	return(x);
}

Node *linkum(Node *a, Node *b)
{
	Node *c;

	if (errorflag)	/* don't link things that are wrong */
		return a;
	if (a == NULL)
		return(b);
	else if (b == NULL)
		return(a);
	for (c = a; c->nnext != NULL; c = c->nnext)
		;
	c->nnext = b;
	return(a);
}

int ptoi(void *p)	/* convert pointer to integer */
{
	return (int) (long) p;	/* swearing that p fits, of course */
}

Node *itonp(int i)	/* and vice versa */
{
	return (Node *) (long) i;
}