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
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "awk.h"

int yywrap(void) { return(1); }
void	yyerror(const char *, ...);

Node	*beginloc = 0;
Node	*endloc = 0;
%}

%union {
	Node	*p;
	Cell	*cp;
	int	i;
	char	*s;
}

%token	<p>	PROGRAM PASTAT XBEGIN XEND
%token	<i>	NL ',' '{' '(' '|' ';' '/' ')' '}' '[' ']'
%token	<i>	APPEND EQ GE GT LE LT NE
%token	<i>	EXIT IF
%token	<i>	ADD MINUS MULT DIVIDE MOD
%token	<i>	ASSIGN ASGNOP ADDEQ SUBEQ MULTEQ DIVEQ MODEQ
%token	<i>	PRINT PRINTF
%token	<p>	ELSE CONDEXPR
%token	<i>	POSTINCR PREINCR POSTDECR PREDECR
%token	<cp>	VAR IVAR NUMBER STRING

%type	<p>	pas pattern plist term
%type	<p>	pa_pat pa_stat pa_stats
%type	<p>	simple_stmt stmt stmtlist
%type	<p>	var
%type	<p>	if else
%type	<i>	st
%type	<i>	pst opt_pst lbrace rbrace rparen nl opt_nl
%type	<i>	print

%right	ASGNOP
%right	'?'
%right	':'
%nonassoc APPEND EQ GE GT LE LT NE '|'
%left	EXIT
%left	IF NUMBER
%left	PRINT PRINTF STRING
%left	VAR IVAR '('
%left	'+' '-'
%left	'*' '/' '%'
%left	UMINUS
%right	DECR INCR
%left	INDIRECT

%%

program:
	  pas	{ if (errorflag==0)
			rootnode = stat3(PROGRAM, beginloc, $1, endloc); }
	| error	{ yyclearin; bracecheck(); yyerror("bailing out"); }
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
	  pa_pat			{ $$ = stat2(PASTAT, $1, stat2(PRINT, rectonode(), NULL)); }
	| pa_pat lbrace stmtlist '}'	{ $$ = stat2(PASTAT, $1, $3); }
	| lbrace stmtlist '}'		{ $$ = stat2(PASTAT, NULL, $2); }
	| XBEGIN lbrace stmtlist '}'
		{ beginloc = linkum(beginloc, $3); $$ = 0; }
	| XEND lbrace stmtlist '}'
		{ endloc = linkum(endloc, $3); $$ = 0; }
	;

pa_stats:
	  pa_stat
	| pa_stats opt_pst pa_stat	{ $$ = linkum($1, $3); }
	;

pattern:
	  var ASGNOP pattern		{ $$ = op2($2, $1, $3); }
	| pattern '?' pattern ':' pattern %prec '?'
	 	{ $$ = op3(CONDEXPR, notnull($1), $3, $5); }
	| pattern EQ pattern		{ $$ = op2($2, $1, $3); }
	| pattern GE pattern		{ $$ = op2($2, $1, $3); }
	| pattern GT pattern		{ $$ = op2($2, $1, $3); }
	| pattern LE pattern		{ $$ = op2($2, $1, $3); }
	| pattern LT pattern		{ $$ = op2($2, $1, $3); }
	| pattern NE pattern		{ $$ = op2($2, $1, $3); }
	| term
	;

plist:
	  pattern ',' pattern		{ $$ = linkum($1, $3); }
	| plist ',' pattern		{ $$ = linkum($1, $3); }
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
	| print '(' pattern ')'		{ $$ = stat1($1, $3); }
	| print '(' plist ')'		{ $$ = stat1($1, $3); }
	| pattern			{ $$ = exptostat($1); }
	| error				{ yyclearin; }
	;

st:
	  nl
	| ';' opt_nl
	;

stmt:
	| EXIT pattern st	{ $$ = stat1(EXIT, $2); }
	| EXIT st		{ $$ = stat1(EXIT, NULL); }
	| if stmt else stmt	{ $$ = stat3(IF, $1, $2, $4); }
	| if stmt		{ $$ = stat2(IF, $1, $2); }
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
	| '-' term %prec UMINUS		{ $$ = op1(UMINUS, $2); }
	| '+' term %prec UMINUS		{ $$ = $2; }
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

int	errorflag = 0;
int	lineno	= 1;
int	bracecnt = 0;
int	brackcnt  = 0;
int	parencnt = 0;

typedef struct Keyword {
	const char *word;
	int	sub;
	int	type;
} Keyword;

Keyword keywords[] ={	/* keep sorted: binary searched */
	{ "BEGIN",	XBEGIN,		XBEGIN },
	{ "END",	XEND,		XEND },
	{ "else",	ELSE,		ELSE },
	{ "exit",	EXIT,		EXIT },
	{ "if",		IF,		IF },
	{ "print",	PRINT,		PRINT },
	{ "printf",	PRINTF,		PRINTF },
};

#define	RET(x)	{ if(dbg)printf("lex %s\n", tokname(x)); return(x); }

int		 peek(void);
int		 gettok(char **, int *);
int		 binsearch(char *, Keyword *, int);
int		 word(char *);
int		 string(void);
void		 eprint(void);
void		 bclass(int);
void		 bcheck2(int, int, int);
void		 error(void);

int peek(void)
{
	int c = input();
	unput(c);
	return c;
}

int gettok(char **pbuf, int *psz)	/* get next input token */
{
	int c, retc;
	char *buf = *pbuf;
	int sz = *psz;
	char *bp = buf;

	c = input();
	if (c == 0)
		return 0;
	buf[0] = c;
	buf[1] = 0;
	if (!isalnum(c) && c != '.' && c != '_')
		return c;

	*bp++ = c;
	if (isalpha(c) || c == '_') {	/* it's a varname */
		for ( ; (c = input()) != 0; ) {
			if (bp-buf >= sz)
				if (!adjbuf(&buf, &sz, bp-buf+2, 100, &bp, "gettok"))
					FATAL( "out of space for name %.10s...", buf );
			if (isalnum(c) || c == '_')
				*bp++ = c;
			else {
				*bp = 0;
				unput(c);
				break;
			}
		}
		*bp = 0;
		retc = 'a';	/* alphanumeric */
	} else {	/* maybe it's a number, but could be . */
		char *rem;
		/* read input until can't be a number */
		for ( ; (c = input()) != 0; ) {
			if (bp-buf >= sz)
				if (!adjbuf(&buf, &sz, bp-buf+2, 100, &bp, "gettok"))
					FATAL( "out of space for number %.10s...", buf );
			if (isdigit(c) || c == 'e' || c == 'E' 
			  || c == '.' || c == '+' || c == '-')
				*bp++ = c;
			else {
				unput(c);
				break;
			}
		}
		*bp = 0;
		strtod(buf, &rem);	/* parse the number */
		if (rem == buf) {	/* it wasn't a valid number at all */
			buf[1] = 0;	/* return one character as token */
			retc = buf[0];	/* character is its own type */
			unputstr(rem+1); /* put rest back for later */
		} else {	/* some prefix was a number */
			unputstr(rem);	/* put rest back for later */
			rem[0] = 0;	/* truncate buf after number part */
			retc = '0';	/* type is number */
		}
	}
	*pbuf = buf;
	*psz = sz;
	return retc;
}

int	sc	= 0;	/* 1 => return a } right now */

int yylex(void)
{
	int c;
	static char *buf = 0;
	static int bufsize = 5; /* BUG: setting this small causes core dump! */

	if (buf == 0 && (buf = (char *) malloc(bufsize)) == NULL)
		FATAL( "out of space in yylex" );
	if (sc) {
		sc = 0;
		RET('}');
	}
	for (;;) {
		c = gettok(&buf, &bufsize);
		if (c == 0)
			return 0;
		if (isalpha(c) || c == '_')
			return word(buf);
		if (isdigit(c)) {
			yylval.cp = setsymtab(buf, xstrdup(buf), atof(buf), CON|NUM, symtab);
			/* should this also have STR set? */
			RET(NUMBER);
		}

		yylval.i = c;
		switch (c) {
		case '\n':	/* {EOL} */
			RET(NL);
		case '\r':	/* assume \n is coming */
		case ' ':	/* {WS}+ */
		case '\t':
			break;
		case '#':	/* #.* strip comments */
			while ((c = input()) != '\n' && c != 0)
				;
			unput(c);
			break;
		case ';':
			RET(';');
		case '\\':
			if (peek() == '\n') {
				input();
			} else if (peek() == '\r') {
				input(); input();	/* \n */
				lineno++;
			} else {
				RET(c);
			}
			break;
		case '&':
			RET('&');
		case '|':
			RET('|');
		case '!':
			if (peek() == '=') {
				input(); yylval.i = NE; RET(NE);
			} else
				RET('!');
		case '<':
			if (peek() == '=') {
				input(); yylval.i = LE; RET(LE);
			} else {
				yylval.i = LT; RET(LT);
			}
		case '=':
			if (peek() == '=') {
				input(); yylval.i = EQ; RET(EQ);
			} else {
				yylval.i = ASSIGN; RET(ASGNOP);
			}
		case '>':
			if (peek() == '=') {
				input(); yylval.i = GE; RET(GE);
			} else if (peek() == '>') {
				input(); yylval.i = APPEND; RET(APPEND);
			} else {
				yylval.i = GT; RET(GT);
			}
		case '+':
			if (peek() == '+') {
				input(); yylval.i = INCR; RET(INCR);
			} else if (peek() == '=') {
				input(); yylval.i = ADDEQ; RET(ASGNOP);
			} else
				RET('+');
		case '-':
			if (peek() == '-') {
				input(); yylval.i = DECR; RET(DECR);
			} else if (peek() == '=') {
				input(); yylval.i = SUBEQ; RET(ASGNOP);
			} else
				RET('-');
		case '*':
			if (peek() == '=') {	/* *= */
				input(); yylval.i = MULTEQ; RET(ASGNOP);
			} else
				RET('*');
		case '/':
			RET('/');
		case '%':
			if (peek() == '=') {
				input(); yylval.i = MODEQ; RET(ASGNOP);
			} else
				RET('%');
		case '$':
			/* BUG: awkward, if not wrong */
			c = gettok(&buf, &bufsize);
			if (isalpha(c)) {
				c = peek();
				if (c == '(' || c == '[' ) {
					unputstr(buf);
					RET(INDIRECT);
				}
				yylval.cp = setsymtab(buf, "", 0.0, STR|NUM, symtab);
				RET(IVAR);
			} else if (c == 0) {	/*  */
				yyerror( "unexpected end of input after $" );
				RET(';');
			} else {
				unputstr(buf);
				RET(INDIRECT);
			}

		case '}':
			if (--bracecnt < 0)
				yyerror( "extra }" );
			sc = 1;
			RET(';');
		case ']':
			if (--brackcnt < 0)
				yyerror( "extra ]" );
			RET(']');
		case ')':
			if (--parencnt < 0)
				yyerror( "extra )" );
			RET(')');
		case '{':
			bracecnt++;
			RET('{');
		case '[':
			brackcnt++;
			RET('[');
		case '(':
			parencnt++;
			RET('(');
		case '"':
			return string();	/* BUG: should be like tran.c ? */
		default:
			RET(c);
		}
	}
}

int string(void)
{
	int c, n;
	char *s, *bp;
	static char *buf = 0;
	static int bufsz = 500;

	if (buf == 0 && (buf = (char *) malloc(bufsz)) == NULL)
		FATAL("out of space for strings");
	for (bp = buf; (c = input()) != '"'; ) {
		if (!adjbuf(&buf, &bufsz, bp-buf+2, 500, &bp, "string"))
			FATAL("out of space for string %.10s...", buf);
		switch (c) {
		case '\n':
		case '\r':
		case 0:
			yyerror( "non-terminated string %.10s...", buf );
			lineno++;
			if (c == 0)	/* hopeless */
				FATAL( "giving up" );
			break;
		case '\\':
			c = input();
			switch (c) {
			case '"': *bp++ = '"'; break;
			case 'n': *bp++ = '\n'; break;	
			case 't': *bp++ = '\t'; break;
			case 'f': *bp++ = '\f'; break;
			case 'r': *bp++ = '\r'; break;
			case 'b': *bp++ = '\b'; break;
			case 'v': *bp++ = '\v'; break;
			case 'a': *bp++ = '\007'; break;
			case '\\': *bp++ = '\\'; break;

			case '0': case '1': case '2': /* octal: \d \dd \ddd */
			case '3': case '4': case '5': case '6': case '7':
				n = c - '0';
				if ((c = peek()) >= '0' && c < '8') {
					n = 8 * n + input() - '0';
					if ((c = peek()) >= '0' && c < '8')
						n = 8 * n + input() - '0';
				}
				*bp++ = n;
				break;

			case 'x':	/* hex  \x0-9a-fA-F + */
			    {	char xbuf[100], *px;
				for (px = xbuf; (c = input()) != 0 && px-xbuf < 100-2; ) {
					if (isdigit(c)
					 || (c >= 'a' && c <= 'f')
					 || (c >= 'A' && c <= 'F'))
						*px++ = c;
					else
						break;
				}
				*px = 0;
				unput(c);
	  			sscanf(xbuf, "%x", (unsigned int *) &n);
				*bp++ = n;
				break;
			    }

			default: 
				*bp++ = c;
				break;
			}
			break;
		default:
			*bp++ = c;
			break;
		}
	}
	*bp = 0; 
	s = xstrdup(buf);
	*bp++ = ' '; *bp++ = 0;
	yylval.cp = setsymtab(buf, s, 0.0, CON|STR|DONTFREE, symtab);
	RET(STRING);
}

int kcmp(const void *w, const void *kp)
{
	return strcmp(w, ((const Keyword *)kp)->word);
}

int word(char *w) 
{
	Keyword *kp;

	kp = bsearch(w, keywords, sizeof(keywords)/sizeof(keywords[0]),
	    sizeof(keywords[0]), kcmp);
	if (kp != NULL) {
		yylval.i = kp->sub;
		RET(kp->type);
	}
	yylval.cp = setsymtab(w, "", 0.0, STR|NUM|DONTFREE, symtab);
	RET(VAR);
}

/* low-level lexical stuff, sort of inherited from lex */

char	ebuf[300];
char	*ep = ebuf;
char	yysbuf[100];	/* pushback buffer */
char	*yysptr = yysbuf;
FILE	*yyin = 0;

int input(void)	/* get next lexical input character */
{
	int c;
	extern char *lexprog;

	if (yysptr > yysbuf)
		c = (uschar)*--yysptr;
	else if (lexprog != NULL) {	/* awk '...' */
		if ((c = (uschar)*lexprog) != 0)
			lexprog++;
	} else				/* awk -f ... */
		c = pgetc();
	if (c == '\n')
		lineno++;
	else if (c == EOF)
		c = 0;
	if (ep >= ebuf + sizeof ebuf)
		ep = ebuf;
	return *ep++ = c;
}

void unput(int c)	/* put lexical character back on input */
{
	if (c == '\n')
		lineno--;
	if (yysptr >= yysbuf + sizeof(yysbuf))
		FATAL("pushed back too much: %.20s...", yysbuf);
	*yysptr++ = c;
	if (--ep < ebuf)
		ep = ebuf + sizeof(ebuf) - 1;
}

void unputstr(const char *s)	/* put a string back on input */
{
	int i;

	for (i = strlen(s)-1; i >= 0; i--)
		unput(s[i]);
}

void yyerror(const char *fmt, ...)
{
	static int been_here = 0;
	va_list varg;

	if (been_here++ > 2)
		return;
	fprintf(stderr, "%s:%d: ", cursource() ? cursource() : getprogname(),
	    lineno);
	va_start(varg, fmt);
	vfprintf(stderr, fmt, varg);
	va_end(varg);
	fprintf(stderr, "\n");
	errorflag = 2;
	eprint();
}


/* try to print context around error */
void eprint(void)
{
	static int been_here = 0;
	char *p, *recstart, *tokstart, *tokend;
	int c;

	if (compile_time != 1)
		return;

	if (been_here++ > 0 || ebuf == ep)
		return;

	tokend = ep - 1;

	/* Rewind to beginning of record. */
	p = tokend;
	if (p > ebuf && *p == '\n')
		p--;
	while (p > ebuf && *p != '\n' && *p != '\0')
		p--;
	while (*p == '\n')
		p++;
	recstart = p;

	/* Rewind to the space before the syntax error. */
	for (p = tokend; p >= recstart && !isspace(*p); p--)
		;
	tokstart = p + 1;

	if (tokstart == tokend)
		return;

	/* Print read characters. */
	for (p = recstart; p < ep; p++) {
		if (*p)
			putc(*p, stderr);
	}

	/* Print rest of the line. */
	while ((c = input()) != '\n' && c != '\0' && c != EOF) {
		putc(c, stderr);
		bclass(c);
	}
	putc('\n', stderr);

	for (p = recstart; p < tokstart; p++)
		putc(' ', stderr);

	/* Underline the incorrect token. */
	putc('^', stderr);
	for (; p < tokend; p++)
		putc('~', stderr);
	putc('\n', stderr);

	/* Prevent printing this message twice. */
	ep = ebuf;
}

__dead void FATAL(const char *fmt, ...)
{
	va_list varg;

	fflush(stdout);
	fprintf(stderr, "%s: ", getprogname());
	va_start(varg, fmt);
	vfprintf(stderr, fmt, varg);
	va_end(varg);
	error();
	if (dbg > 1)		/* core dump if serious debugging on */
		abort();
	exit(2);
}

void WARNING(const char *fmt, ...)
{
	va_list varg;

	fflush(stdout);
	fprintf(stderr, "%s: ", getprogname());
	va_start(varg, fmt);
	vfprintf(stderr, fmt, varg);
	va_end(varg);
	error();
}

void error()
{
	extern Node *curnode;

	fprintf(stderr, "\n");
	if (compile_time != 2 && NR && *NR > 0) {
		fprintf(stderr, " input record number %d", (int) (*FNR));
		if (strcmp(*FILENAME, "-") != 0)
			fprintf(stderr, ", file %s", *FILENAME);
		fprintf(stderr, "\n");
	}
	if (compile_time != 2 && curnode)
		fprintf(stderr, " source line number %d", curnode->lineno);
	else if (compile_time != 2 && lineno)
		fprintf(stderr, " source line number %d", lineno);
	if (compile_time == 1 && cursource() != NULL)
		fprintf(stderr, " source file %s", cursource());
	fprintf(stderr, "\n");
	eprint();
}

void bracecheck(void)
{
	int c;
	static int beenhere = 0;

	if (beenhere++)
		return;
	while ((c = input()) != EOF && c != '\0')
		bclass(c);
	bcheck2(bracecnt, '{', '}');
	bcheck2(brackcnt, '[', ']');
	bcheck2(parencnt, '(', ')');
}

void bcheck2(int n, int c1, int c2)
{
	if (n == 1)
		fprintf(stderr, "\tmissing %c\n", c2);
	else if (n > 1)
		fprintf(stderr, "\t%d missing %c's\n", n, c2);
	else if (n == -1)
		fprintf(stderr, "\textra %c\n", c2);
	else if (n < -1)
		fprintf(stderr, "\t%d extra %c's\n", -n, c2);
}

void bclass(int c)
{
	switch (c) {
	case '{': bracecnt++; break;
	case '}': bracecnt--; break;
	case '[': brackcnt++; break;
	case ']': brackcnt--; break;
	case '(': parencnt++; break;
	case ')': parencnt--; break;
	}
}

Node *notnull(Node *n)
{
	switch (n->nobj) {
	case LE: case LT: case EQ: case NE: case GT: case GE:
		return n;
	default:
		return op2(NE, n, nullnode);
	}
}
