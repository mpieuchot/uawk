/*	$OpenBSD: awk.h,v 1.14 2017/10/09 14:51:31 deraadt Exp $	*/
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

typedef double	Awkfloat;

/* unsigned char is more trouble than it's worth */

typedef	unsigned char uschar;

#define	xfree(a)	{ if ((a) != NULL) { free((void *) (a)); (a) = NULL; } }

#define	NN(p)	((p) ? (p) : "(null)")	/* guaranteed non-null for DPRINTF */
#ifdef	DEBUG
#	define	DPRINTF(x)	if (dbg) printf x
#else
#	define	DPRINTF(x)
#endif

extern int	compile_time;	/* 1 if compiling, 0 if running */

#define	RECSIZE	(8 * 1024)	/* sets limit on records, fields, etc., etc. */
extern int	recsize;	/* size of current record, orig RECSIZE */

#define	RS	"\n"		/* Input record separator */
#define	ORS	"\n"		/* Output record separator */
#define	OFS	" "		/* Output field separator */
#define OFMT	"%.6g"		/* Output format for numbers */
#define	CONVFMT "%.6g"		/* Conversion format when converting numbers */
extern Awkfloat *NR;
extern Awkfloat *FNR;
extern Awkfloat *NF;
extern char	**FILENAME;

extern char	*record;	/* points to $0 */
extern int	lineno;		/* line number in awk program */
extern int	errorflag;	/* 1 if error has occurred */
extern int	donefld;	/* 1 if record broken into fields */
extern int	donerec;	/* 1 if record is valid (no fld has changed */
extern char	inputFS[];	/* for field splitting */

extern int	dbg;

extern	char	*patbeg;	/* beginning of pattern matched */
extern	int	patlen;		/* length of pattern matched.  set in b.c */

/* Cell:  all information about a variable or constant */

typedef struct Cell {
	uschar	ctype;		/* OCELL, OBOOL, OJUMP, etc. */
	uschar	csub;		/* CCON, CTEMP, CFLD, etc. */
	char	*nval;		/* name, for variables only */
	char	*sval;		/* string value */
	Awkfloat fval;		/* value as number */
	int	 tval;		/* type info: STR|NUM|ARR|FCN|FLD|CON|DONTFREE */
	struct Cell *cnext;	/* ptr to next if chained */
} Cell;

typedef struct Array {		/* symbol table array */
	int	nelem;		/* elements in table right now */
	int	size;		/* size of tab */
	Cell	**tab;		/* hash table pointers */
} Array;

#define	NSYMTAB	50	/* initial size of a symbol table */
extern Array	*symtab;

extern Cell	*nrloc;		/* NR */
extern Cell	*fnrloc;	/* FNR */
extern Cell	*nfloc;		/* NF */

/* Cell.tval values: */
#define	NUM	01	/* number value is valid */
#define	STR	02	/* string value is valid */
#define DONTFREE 04	/* string space is not freeable */
#define	CON	010	/* this is a constant */
#define	ARR	020	/* this is an array */
#define	FCN	040	/* this is a function name */
#define FLD	0100	/* this is a field $1, $2, ... */
#define	REC	0200	/* this is $0 */


/* Node:  parse tree is made of nodes, with Cell's at bottom */

typedef struct Node {
	int	ntype;
	struct	Node *nnext;
	int	lineno;
	int	nobj;
	Cell *(*proc)(struct Node **, int);
	struct	Node *narg[1];	/* variable: actual size set by calling malloc */
} Node;

extern Node	*winner;
extern Node	*nullstat;
extern Node	*nullnode;

/* ctypes */
#define OCELL	1
#define OBOOL	2
#define OJUMP	3

/* Cell subtypes: csub */
#define	CFREE	7
#define CCOPY	6
#define CCON	5
#define CTEMP	4
#define CNAME	3 
#define CVAR	2
#define CFLD	1
#define	CUNK	0

/* bool subtypes */
#define BTRUE	11
#define BFALSE	12

/* jump subtypes */
#define JEXIT	21

/* node types */
#define NVALUE	1
#define NSTAT	2
#define NEXPR	3


extern	int	pairstack[], paircnt;

#define isvalue(n)	((n)->ntype == NVALUE)
#define isexpr(n)	((n)->ntype == NEXPR)
#define isexit(n)	((n)->csub == JEXIT)
#define isrec(n)	((n)->tval & REC)
#define isfld(n)	((n)->tval & FLD)
#define isstr(n)	((n)->tval & STR)
#define isnum(n)	((n)->tval & NUM)
#define isarr(n)	((n)->tval & ARR)
#define isfcn(n)	((n)->tval & FCN)
#define istrue(n)	((n)->csub == BTRUE)
#define istemp(n)	((n)->csub == CTEMP)
#define	isargument(n)	((n)->nobj == ARG)
/* #define freeable(p)	(!((p)->tval & DONTFREE)) */
#define freeable(p)	( ((p)->tval & (STR|DONTFREE)) == STR )

/* structures used by regular expression matching machinery, mostly b.c: */

#define NCHARS	(256+3)		/* 256 handles 8-bit chars; 128 does 7-bit */
				/* watch out in match(), etc. */
#define NSTATES	32

typedef struct rrow {
	long	ltype;	/* long avoids pointer warnings on 64-bit */
	union {
		int i;
		Node *np;
		uschar *up;
	} lval;		/* because Al stores a pointer in it! */
	int	*lfollow;
} rrow;

typedef struct fa {
	uschar	gototab[NSTATES][NCHARS];
	uschar	out[NSTATES];
	uschar	*restr;
	int	*posns[NSTATES];
	int	anchor;
	int	use;
	int	initstat;
	int	curstat;
	int	accept;
	int	reset;
	struct	rrow re[1];	/* variable: actual size set by calling malloc */
} fa;

/* awkgram.y */
extern	Node	*notnull(Node *);
extern	int	yyparse(void);

/* lex.c */
extern	int	yylex(void);
extern	int	input(void);
extern	void	unput(int);
extern	void	unputstr(const char *);

/* main.c */
extern	int	pgetc(void);
extern	char	*cursource(void);

/* parse.c */
extern	Node	*exptostat(Node *);
extern	Node	*stat3(int, Node *, Node *, Node *);
extern	Node	*op2(int, Node *, Node *);
extern	Node	*op1(int, Node *);
extern	Node	*stat1(int, Node *);
extern	Node	*op3(int, Node *, Node *, Node *);
extern	Node	*stat2(int, Node *, Node *);
extern	Node	*celltonode(Cell *, int);
extern	Node	*rectonode(void);
extern	Node	*pa2stat(Node *, Node *, Node *);
extern	Node	*linkum(Node *, Node *);
extern	const char *tokname(int);
extern	Cell	*(*proctab[])(Node **, int);
extern	int	ptoi(void *);

/* tran.c */
extern	void	syminit(void);
extern	void	arginit(int, char **);
extern	Array	*makesymtab(int);
extern	void	freesymtab(Cell *);
extern	Cell	*setsymtab(const char *, const char *, double, unsigned int, Array *);
extern	Cell	*lookup(const char *, Array *);
extern	double	setfval(Cell *, double);
extern	void	funnyvar(Cell *, const char *);
extern	char	*setsval(Cell *, const char *);
extern	double	getfval(Cell *);
extern	char	*getsval(Cell *);
extern	char	*getpssval(Cell *);     /* for print */
extern	char	*xstrdup(const char *);

/* lib.c */
extern	void	recinit(unsigned int);
extern	void	makefields(int, int);
extern	int	getrec(char **, int *, int);
extern	void	fldbld(void);
extern	void	newfld(int);
extern	void	recbld(void);
extern	Cell	*fieldadr(int);
extern	void	yyerror(const char *);
extern	void	fpecatch(int);
extern	void	bracecheck(void);
extern	void	SYNTAX(const char *, ...);
extern	__dead void	FATAL(const char *, ...);
extern	void	WARNING(const char *, ...);
extern	int	is_number(const char *);

/* run.c */
extern	int	adjbuf(char **pb, int *sz, int min, int q, char **pbp, const char *what);
extern	void	run(Node *);
extern	Cell	*execute(Node *);
extern	Cell	*program(Node **, int);
extern	Cell	*jump(Node **, int);
extern	Cell	*relop(Node **, int);
extern	Cell	*indirect(Node **, int);
extern	Cell	*awkprintf(Node **, int);
extern	Cell	*arith(Node **, int);
extern	double	ipow(double, int);
extern	Cell	*incrdecr(Node **, int);
extern	Cell	*assign(Node **, int);
extern	Cell	*pastat(Node **, int);
extern	Cell	*dopa2(Node **, int);
extern	Cell	*condexpr(Node **, int);
extern	Cell	*ifstat(Node **, int);
extern	Cell	*printstat(Node **, int);
extern	Cell	*nullproc(Node **, int);
