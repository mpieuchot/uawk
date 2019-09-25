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

Node	*rootnode = NULL;	/* root of parse tree */
Node	*nullnode;	/* zero&null, converted into a node for comparisons */

void		 nodeinit(int, Node *);
Node		*nodealloc(int);
Node		*node1(int, Node *);
Node		*node2(int, Node *, Node *);
Node		*node3(int, Node *, Node *, Node *);
Node		*node4(int, Node *, Node *, Node *, Node *);

struct
{	int value;
	Cell *(*func)(Node **, int);
} functions[] = {
	{ PROGRAM,	f_program },
	{ NE,		f_relop },
	{ EQ,		f_relop },
	{ LE,		f_relop },
	{ LT,		f_relop },
	{ GE,		f_relop },
	{ GT,		f_relop },
	{ INDIRECT,	f_indirect },
	{ ADD,		f_arith },
	{ MINUS,	f_arith },
	{ MULT,		f_arith },
	{ DIVIDE,	f_arith },
	{ MOD,		f_arith },
	{ UMINUS,	f_arith },
	{ PREINCR,	f_incrdecr },
	{ POSTINCR,	f_incrdecr },
	{ PREDECR,	f_incrdecr },
	{ POSTDECR,	f_incrdecr },
	{ PASTAT,	f_pastat },
	{ PRINTF,	f_printf },
	{ PRINT,	f_print },
	{ ASSIGN,	f_assign },
	{ ADDEQ,	f_assign },
	{ SUBEQ,	f_assign },
	{ MULTEQ,	f_assign },
	{ DIVEQ,	f_assign },
	{ MODEQ,	f_assign },
	{ CONDEXPR,	f_condexpr },
	{ IF,		f_if },
	{ EXIT,		f_jump },
};

void
nodeinit(int a, Node *x)
{
	int i;

	/* NVALUE */
	if (a == 0)
		return;

	for (i = 0; i < nitems(functions); i++) {
		if (a == functions[i].value)
			break;
	}

	x->nobj = functions[i].value;
	x->proc = functions[i].func;
}

Node *
nodealloc(int n)
{
	Node *x;

	x = xmalloc(sizeof(Node) + (n-1)*sizeof(Node *));
	x->nnext = NULL;
	x->lineno = lineno;
	return x;
}

Node *
exp2stat(Node *a)
{
	a->ntype = NSTAT;
	return a;
}

Node *
node1(int a, Node *b)
{
	Node *x;

	x = nodealloc(1);
	nodeinit(a, x);
	x->narg[0]=b;
	return x;
}

Node *
node2(int a, Node *b, Node *c)
{
	Node *x;

	x = nodealloc(2);
	nodeinit(a, x);
	x->narg[0] = b;
	x->narg[1] = c;
	return x;
}

Node *
node3(int a, Node *b, Node *c, Node *d)
{
	Node *x;

	x = nodealloc(3);
	nodeinit(a, x);
	x->narg[0] = b;
	x->narg[1] = c;
	x->narg[2] = d;
	return x;
}

Node *
node4(int a, Node *b, Node *c, Node *d, Node *e)
{
	Node *x;

	x = nodealloc(4);
	nodeinit(a, x);
	x->narg[0] = b;
	x->narg[1] = c;
	x->narg[2] = d;
	x->narg[3] = e;
	return x;
}

Node *
stat1(int a, Node *b)
{
	Node *x;

	x = node1(a,b);
	x->ntype = NSTAT;
	return x;
}

Node *
stat2(int a, Node *b, Node *c)
{
	Node *x;

	x = node2(a,b,c);
	x->ntype = NSTAT;
	return x;
}

Node *
stat3(int a, Node *b, Node *c, Node *d)
{
	Node *x;

	x = node3(a,b,c,d);
	x->ntype = NSTAT;
	return x;
}

Node *
op1(int a, Node *b)
{
	Node *x;

	x = node1(a,b);
	x->ntype = NEXPR;
	return x;
}

Node *
op2(int a, Node *b, Node *c)
{
	Node *x;

	x = node2(a,b,c);
	x->ntype = NEXPR;
	return x;
}

Node *
op3(int a, Node *b, Node *c, Node *d)
{
	Node *x;

	x = node3(a,b,c,d);
	x->ntype = NEXPR;
	return x;
}

Node *
cell2node(Cell *a, int b)
{
	Node *x;

	a->ctype = b;
	x = node1(0, (Node *) a);
	x->ntype = NVALUE;
	return x;
}

/*
 * make $0 into a Node
 */
Node *
record2node(void)
{
	extern Cell *literal0;
	return op1(INDIRECT, cell2node(literal0, CUNK));
}

Node *
node_link(Node *a, Node *b)
{
	Node *c;

	if (errorflag)	/* don't link things that are wrong */
		return a;
	if (a == NULL)
		return b;
	else if (b == NULL)
		return a;
	for (c = a; c->nnext != NULL; c = c->nnext)
		;
	c->nnext = b;
	return a;
}
