/*	$OpenBSD: awkgram.y,v 1.9 2011/09/28 19:27:18 millert Exp $	*/
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

%{
#include <stdio.h>
#include <string.h>
#include "awk.h"

int yywrap(void) { return(1); }

Node	*beginloc = 0;
Node	*endloc = 0;
char	*curfname = 0;	/* current function name */
%}

%union {
	Node	*p;
	Cell	*cp;
	int	i;
	char	*s;
}

%token	<i>	FIRSTTOKEN	/* must be first */
%token	<p>	PROGRAM PASTAT PASTAT2 XBEGIN XEND
%token	<i>	NL ',' '{' '(' '|' ';' '/' ')' '}' '[' ']'
%token	<i>	FINAL DOT ALL CCL NCCL CHAR OR STAR QUEST PLUS EMPTYRE
%token	<i>	AND BOR APPEND EQ GE GT LE LT NE
%token	<i>	EXIT
%token	<i>	IF
%token	<i>	ADD MINUS MULT DIVIDE MOD
%token	<i>	ASSIGN ASGNOP ADDEQ SUBEQ MULTEQ DIVEQ MODEQ POWEQ
%token	<i>	PRINT PRINTF
%token	<p>	ELSE CONDEXPR
%token	<i>	POSTINCR PREINCR POSTDECR PREDECR
%token	<cp>	VAR IVAR NUMBER STRING

%type	<p>	pas pattern ppattern plist pplist patlist prarg term
%type	<p>	pa_pat pa_stat pa_stats
%type	<p>	simple_stmt stmt stmtlist
%type	<p>	var
%type	<p>	if else
%type	<i>	st
%type	<i>	pst opt_pst lbrace rbrace rparen comma nl opt_nl and bor
%type	<i>	print

%right	ASGNOP
%right	'?'
%right	':'
%left	BOR
%left	AND
%nonassoc APPEND EQ GE GT LE LT NE '|'
%left	EXIT
%left	IF NUMBER
%left	PRINT PRINTF STRING
%left	VAR IVAR '('
%left	'+' '-'
%left	'*' '/' '%'
%left	NOT UMINUS
%right	POWER
%right	DECR INCR
%left	INDIRECT
%token	LASTTOKEN	/* must be last */

%%

program:
	  pas	{ if (errorflag==0)
			winner = (Node *)stat3(PROGRAM, beginloc, $1, endloc); }
	| error	{ yyclearin; bracecheck(); SYNTAX("bailing out"); }
	;

and:
	  AND | and NL
	;

bor:
	  BOR | bor NL
	;

comma:
	  ',' | comma NL
	;

else:
	  ELSE | else NL
	;

if:
	  IF '(' pattern rparen		{ $$ = notnull($3); }
	;

lbrace:
	  '{' | lbrace NL
	;

nl:
	  NL | nl NL
	;

opt_nl:
	  /* empty */	{ $$ = 0; }
	| nl
	;

opt_pst:
	  /* empty */	{ $$ = 0; }
	| pst
	;


pas:
	  opt_pst			{ $$ = 0; }
	| opt_pst pa_stats opt_pst	{ $$ = $2; }
	;

pa_pat:
	  pattern	{ $$ = notnull($1); }
	;

pa_stat:
	  pa_pat			{ $$ = stat2(PASTAT, $1, stat2(PRINT, rectonode(), NIL)); }
	| pa_pat lbrace stmtlist '}'	{ $$ = stat2(PASTAT, $1, $3); }
	| pa_pat ',' opt_nl pa_pat		{ $$ = pa2stat($1, $4, stat2(PRINT, rectonode(), NIL)); }
	| pa_pat ',' opt_nl pa_pat lbrace stmtlist '}'	{ $$ = pa2stat($1, $4, $6); }
	| lbrace stmtlist '}'		{ $$ = stat2(PASTAT, NIL, $2); }
	| XBEGIN lbrace stmtlist '}'
		{ beginloc = linkum(beginloc, $3); $$ = 0; }
	| XEND lbrace stmtlist '}'
		{ endloc = linkum(endloc, $3); $$ = 0; }
	;

pa_stats:
	  pa_stat
	| pa_stats opt_pst pa_stat	{ $$ = linkum($1, $3); }
	;

patlist:
	  pattern
	| patlist comma pattern		{ $$ = linkum($1, $3); }
	;

ppattern:
	  var ASGNOP ppattern		{ $$ = op2($2, $1, $3); }
	| ppattern '?' ppattern ':' ppattern %prec '?'
	 	{ $$ = op3(CONDEXPR, notnull($1), $3, $5); }
	| ppattern bor ppattern %prec BOR
		{ $$ = op2(BOR, notnull($1), notnull($3)); }
	| ppattern and ppattern %prec AND
		{ $$ = op2(AND, notnull($1), notnull($3)); }
	| term
	;

pattern:
	  var ASGNOP pattern		{ $$ = op2($2, $1, $3); }
	| pattern '?' pattern ':' pattern %prec '?'
	 	{ $$ = op3(CONDEXPR, notnull($1), $3, $5); }
	| pattern bor pattern %prec BOR
		{ $$ = op2(BOR, notnull($1), notnull($3)); }
	| pattern and pattern %prec AND
		{ $$ = op2(AND, notnull($1), notnull($3)); }
	| pattern EQ pattern		{ $$ = op2($2, $1, $3); }
	| pattern GE pattern		{ $$ = op2($2, $1, $3); }
	| pattern GT pattern		{ $$ = op2($2, $1, $3); }
	| pattern LE pattern		{ $$ = op2($2, $1, $3); }
	| pattern LT pattern		{ $$ = op2($2, $1, $3); }
	| pattern NE pattern		{ $$ = op2($2, $1, $3); }
	| term
	;

plist:
	  pattern comma pattern		{ $$ = linkum($1, $3); }
	| plist comma pattern		{ $$ = linkum($1, $3); }
	;

pplist:
	  ppattern
	| pplist comma ppattern		{ $$ = linkum($1, $3); }
	;

prarg:
	  /* empty */			{ $$ = rectonode(); }
	| pplist
	| '(' plist ')'			{ $$ = $2; }
	;

print:
	  PRINT | PRINTF
	;

pst:
	  NL | ';' | pst NL | pst ';'
	;

rbrace:
	  '}' | rbrace NL
	;

rparen:
	  ')' | rparen NL
	;

simple_stmt:
	  print prarg '|' term		{ 
			$$ = stat3($1, $2, itonp($3), $4); }
	| print prarg APPEND term	{
			$$ = stat3($1, $2, itonp($3), $4); }
	| print prarg GT term		{
			$$ = stat3($1, $2, itonp($3), $4); }
	| print prarg			{ $$ = stat3($1, $2, NIL, NIL); }
	| pattern			{ $$ = exptostat($1); }
	| error				{ yyclearin; SYNTAX("illegal statement"); }
	;

st:
	  nl
	| ';' opt_nl
	;

stmt:
	| EXIT pattern st	{ $$ = stat1(EXIT, $2); }
	| EXIT st		{ $$ = stat1(EXIT, NIL); }
	| if stmt else stmt	{ $$ = stat3(IF, $1, $2, $4); }
	| if stmt		{ $$ = stat3(IF, $1, $2, NIL); }
	| lbrace stmtlist rbrace { $$ = $2; }
	| simple_stmt st
	| ';' opt_nl		{ $$ = 0; }
	;

stmtlist:
	  stmt
	| stmtlist stmt		{ $$ = linkum($1, $2); }
	;

term:
 	  term '/' ASGNOP term		{ $$ = op2(DIVEQ, $1, $4); }
 	| term '+' term			{ $$ = op2(ADD, $1, $3); }
	| term '-' term			{ $$ = op2(MINUS, $1, $3); }
	| term '*' term			{ $$ = op2(MULT, $1, $3); }
	| term '/' term			{ $$ = op2(DIVIDE, $1, $3); }
	| term '%' term			{ $$ = op2(MOD, $1, $3); }
	| term POWER term		{ $$ = op2(POWER, $1, $3); }
	| '-' term %prec UMINUS		{ $$ = op1(UMINUS, $2); }
	| '+' term %prec UMINUS		{ $$ = $2; }
	| NOT term %prec UMINUS		{ $$ = op1(NOT, notnull($2)); }
	| DECR var			{ $$ = op1(PREDECR, $2); }
	| INCR var			{ $$ = op1(PREINCR, $2); }
	| var DECR			{ $$ = op1(POSTDECR, $1); }
	| var INCR			{ $$ = op1(POSTINCR, $1); }
	| '(' pattern ')'		{ $$ = $2; }
	| NUMBER			{ $$ = celltonode($1, CCON); }
	| STRING	 		{ $$ = celltonode($1, CCON); }
	| var
	;

var:
	  VAR				{ $$ = celltonode($1, CVAR); }
	| IVAR				{ $$ = op1(INDIRECT, celltonode($1, CVAR)); }
	| INDIRECT term	 		{ $$ = op1(INDIRECT, $2); }
	;	

%%

Node *notnull(Node *n)
{
	switch (n->nobj) {
	case LE: case LT: case EQ: case NE: case GT: case GE:
	case BOR: case AND: case NOT:
		return n;
	default:
		return op2(NE, n, nullnode);
	}
}
