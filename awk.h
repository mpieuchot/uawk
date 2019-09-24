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

#define	xfree(a)	{ if ((a) != NULL) { free((void *) (a)); (a) = NULL; } }

#define	NN(p)	((p) ? (p) : "(null)")	/* guaranteed non-null for DPRINTF */
#ifdef	DEBUG
#	define	DPRINTF(x)	if (debug) printf x
#else
#	define	DPRINTF(x)
#endif

extern int	compile_time;	/* 1 if compiling, 0 if running */

extern int	recsize;	/* size of current record, orig RECSIZE */

extern double *NR;
extern double *NF;

extern char	*record;	/* points to $0 */
extern int	lineno;		/* line number in awk program */
extern int	errorflag;	/* 1 if error has occurred */
extern int	donefld;	/* 1 if record broken into fields */
extern int	donerec;	/* 1 if record is valid (no fld has changed */

extern int	debug;

extern	char	*patbeg;	/* beginning of pattern matched */
extern	int	patlen;		/* length of pattern matched.  set in b.c */

enum ctype {
	CUNK = 0,
	CFLD,
	CVAR,
	CTEMP,
	CCON,
	CFREE,
	CCOPY,
	CTRUE,
	CFALSE,
};


/*
 * Cell:  all information about a variable or constant
 */
typedef struct Cell {
	enum ctype	 ctype;	/* Cell type */
	char		*nval;	/* name, for variables only */
	char		*sval;	/* string value */
	double	 	 fval;	/* value as number */
	unsigned int 	 tval;	/* type info */
#define	NUM		0001	/* number value is valid */
#define	STR		0002	/* string value is valid */
#define DONTFREE	0004	/* string space is not freeable */
#define	CON		0010	/* this is a constant */
#define	ARR		0020	/* this is an array */
#define	FCN		0040	/* this is a function name */
#define FLD		0100	/* this is a field $1, $2, ... */
#define	REC		0200	/* this is $0 */
	struct Cell	*cnext;	/* ptr to next if chained */
} Cell;

extern Cell	*nrloc;		/* NR */
extern Cell	*nfloc;		/* NF */



/* Node:  parse tree is made of nodes, with Cell's at bottom */

typedef struct Node {
	int	ntype;
	struct	Node *nnext;
	int	lineno;
	int	nobj;
	Cell *(*proc)(struct Node **, int);
	struct	Node *narg[1];	/* variable: actual size set by calling malloc */
} Node;

extern Node	*rootnode;
extern Node	*nullnode;

/* node types */
#define NVALUE	1
#define NSTAT	2
#define NEXPR	3

#define isvalue(n)	((n)->ntype == NVALUE)
#define isexpr(n)	((n)->ntype == NEXPR)
#define isrec(n)	((n)->tval & REC)
#define isfld(n)	((n)->tval & FLD)
#define isstr(n)	((n)->tval & STR)
#define isnum(n)	((n)->tval & NUM)
#define isarr(n)	((n)->tval & ARR)
#define isfcn(n)	((n)->tval & FCN)
#define istrue(n)	((n)->ctype == CTRUE)
#define	isargument(n)	((n)->nobj == ARG)
/* #define freeable(p)	(!((p)->tval & DONTFREE)) */
#define freeable(p)	( ((p)->tval & (STR|DONTFREE)) == STR )

/* parser.y */
extern	Node	*notnull(Node *);
extern	int	yyparse(void);
extern	int	yylex(void);
extern	int	input(void);
extern	void	unput(int);
extern	void	unputstr(const char *);
extern	void	bracecheck(void);
extern	__dead void	FATAL(const char *, ...);

/* main.c */
extern	int	pgetc(void);
extern	char	*cursource(void);

/* node.c */
Node		*op1(int, Node *);
Node		*op2(int, Node *, Node *);
Node		*op3(int, Node *, Node *, Node *);
Node		*stat1(int, Node *);
Node		*stat2(int, Node *, Node *);
Node		*stat3(int, Node *, Node *, Node *);
Node		*exp2stat(Node *);
Node		*cell2node(Cell *, int);
Node		*record2node(void);
Node		*node_link(Node *, Node *);
const char	 *tokname(int);

/* symtab.c */
void		 symtab_init(void);
Cell		*symtab_set(const char *, const char *, double, unsigned int);
double		 fval_get(Cell *);
double		 fval_set(Cell *, double);
char		*sval_get(Cell *);
char		*sval_set(Cell *, const char *);
void		 funnyvar(Cell *, const char *);

/* record.c */
void		 record_init(void);
int		 record_get(void);
void		 record_parse(void);
void		 field_from_record(void);
void		 field_add(int);
Cell		*field_get(int);
int		 is_number(const char *);

/* run.c */
void		 xadjbuf(char **, int *, int, int, char **, const char *);
extern	Cell	*execute(Node *);
extern	Cell	*f_program(Node **, int);
extern	Cell	*f_jump(Node **, int);
extern	Cell	*f_relop(Node **, int);
extern	Cell	*f_indirect(Node **, int);
extern	Cell	*f_printf(Node **, int);
extern	Cell	*f_arith(Node **, int);
extern	Cell	*f_incrdecr(Node **, int);
extern	Cell	*f_assign(Node **, int);
extern	Cell	*f_pastat(Node **, int);
extern	Cell	*f_condexpr(Node **, int);
extern	Cell	*f_if(Node **, int);
extern	Cell	*f_print(Node **, int);
extern	Cell	*f_null(Node **, int);

/* xmalloc.c */
void	*xmalloc(size_t);
void	*xcalloc(size_t, size_t);
void	*xrealloc(void *, size_t);
void	*xreallocarray(void *, size_t, size_t);
char	*xstrdup(const char *);
int	 xasprintf(char **, const char *, ...)
                __attribute__((__format__ (printf, 2, 3)))
                __attribute__((__nonnull__ (2)));
