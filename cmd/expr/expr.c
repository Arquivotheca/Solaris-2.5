/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)expr.c	1.20	95/08/17 SMI"	/* SVr4.0 1.21	*/

#include <stdlib.h>
#include <regexpr.h>
#include <locale.h>
#include <string.h>
#include <unistd.h>
#include <regex.h>
#include <limits.h>
#include <stdio.h>
#include <ctype.h>

#define	A_STRING 258
#define	NOARG 259
#define	OR 260
#define	AND 261
#define	EQ 262
#define	LT 263
#define	GT 264
#define	GEQ 265
#define	LEQ 266
#define	NEQ 267
#define	ADD 268
#define	SUBT 269
#define	MULT 270
#define	DIV 271
#define	REM 272
#define	MCH 273
#define	MATCH 274

/* size of subexpression array */
#define	MSIZE	LINE_MAX
#define	error(c)	errxx()
#define	EQL(x, y) (strcmp(x, y) == 0)

#define	ERROR(c)	errxx()
#define	MAX_MATCH 20
static int ematch(char *, char *);
static void yyerror(char *);
static void errxx();

long atol();
char *strcpy(), *strncpy();
void exit();

static char *ltoa();
static char	**Av;
static char *buf;
static int	Ac;
static int	Argi;
static int noarg;
static int paren;
/*
 *	Array used to store subexpressions in regular expressions
 *	Only one subexpression allowed per regular expression currently
 */
static char Mstring[1][MSIZE];


static char *operator[] = {
	"|", "&", "+", "-", "*", "/", "%", ":",
	"=", "==", "<", "<=", ">", ">=", "!=",
	"match", "\0" };
static int op[] = {
	OR, AND, ADD,  SUBT, MULT, DIV, REM, MCH,
	EQ, EQ, LT, LEQ, GT, GEQ, NEQ,
	MATCH };
static int pri[] = {
	1, 2, 3, 3, 3, 3, 3, 3, 4, 4, 5, 5, 5, 6, 7};


/*
 * clean_buf - XCU4 mod to remove leading zeros from negative signed
 *		numeric output, e.g., -00001 becomes -1
 */
static void
clean_buf(buf)
	char *buf;
{
	int i = 0;
	int is_a_num = 1;
	int len, num;

	if (buf[0] == '\0')
		return;
	len = strlen(buf);
	if (len <= 0)
		return;

	if (buf[0] == '-') {
		i++;		/* Skip the leading '-' see while loop */
		if (len <= 1)	/* Is it a '-' all by itself? */
			return; /* Yes, so return */

		while (i < len) {
			if (! isdigit(buf[i])) {
				is_a_num = 0;
				break;
			}
			i++;
		}
		if (is_a_num) {
			(void) sscanf(buf, "%d", &num);
			(void) sprintf(buf, "%d", num);
		}
	}
}

/*
 * End XCU4 mods.
 */

static int
yylex() {
	register char *p;
	register i;

	if (Argi >= Ac)
		return (NOARG);

	p = Av[Argi];

	if ((*p == '(' || *p == ')') && p[1] == '\0')
		return ((int)*p);
	for (i = 0; *operator[i]; ++i)
		if (EQL(operator[i], p))
			return (op[i]);


	return (A_STRING);
}

static char
*rel(oper, r1, r2) register char *r1, *r2;
{
	long i;

	if (ematch(r1, "-\\{0,1\\}[0-9]*$") && ematch(r2, "-\\{0,1\\}[0-9]*$"))
		i = atol(r1) - atol(r2);
	else
		i = strcoll(r1, r2);
	switch (oper) {
	case EQ:
		i = i == 0;
		break;
	case GT:
		i = i > 0;
		break;
	case GEQ:
		i = i >= 0;
		break;
	case LT:
		i = i < 0;
		break;
	case LEQ:
		i = i <= 0;
		break;
	case NEQ:
		i = i != 0;
		break;
	}
	return (i ? "1": "0");
}

static char
*arith(oper, r1, r2) char *r1, *r2;
{
	long i1, i2;
	register char *rv;

	if (!(ematch(r1, "-\\{0,1\\}[0-9]*$") &&
	    ematch(r2, "-\\{0,1\\}[0-9]*$")))
		yyerror("non-numeric argument");
	i1 = atol(r1);
	i2 = atol(r2);

	switch (oper) {
	case ADD:
		i1 = i1 + i2;
		break;
	case SUBT:
		i1 = i1 - i2;
		break;
	case MULT:
		i1 = i1 * i2;
		break;
	case DIV:
		if (i2 == 0)
			yyerror("division by zero");
		i1 = i1 / i2;
		break;
	case REM:
		if (i2 == 0)
			yyerror("division by zero");
		i1 = i1 % i2;
		break;
	}
	rv = malloc(16);
	(void) strcpy(rv, ltoa(i1));
	return (rv);
}

static char
*conj(oper, r1, r2)
	char *r1, *r2;
{
	register char *rv;

	switch (oper) {

	case OR:
		if (EQL(r1, "0") || EQL(r1, "")) {
			if (EQL(r2, "0") || EQL(r2, ""))
				rv = "0";
			else
				rv = r2;
		} else
			rv = r1;
		break;
	case AND:
		if (EQL(r1, "0") || EQL(r1, ""))
			rv = "0";
		else if (EQL(r2, "0") || EQL(r2, ""))
			rv = "0";
		else
			rv = r1;
		break;
	}
	return (rv);
}

static char *
match(s, p)
char *s, *p;
{
	register char *rv;
	long val;			/* XCU4 */

	(void) strcpy(rv = malloc(8), ltoa(val = (long)ematch(s, p)));
	if (nbra /* && val != 0 */) {
		rv = malloc((unsigned) strlen(Mstring[0]) + 1);
		(void) strcpy(rv, Mstring[0]);
	}
	return (rv);
}


/*
 * ematch 	- XCU4 mods involve calling compile/advance which simulate
 *		  the obsolete compile/advance functions using regcomp/regexec
 */
static int
ematch(s, p)
char *s;
register char *p;
{
	static char *expbuf;
	char *nexpbuf;
	register num;

	nexpbuf = compile(p, (char *)0, (char *)0);	/* XCU4 regex mod */
	if (0 /* XXX nbra > 1*/)
		yyerror("Too many '\\('s");
	if (regerrno) {
		if (regerrno != 41 || expbuf == NULL)
			errxx();
	} else {
		if (expbuf)
			free(expbuf);
		expbuf = nexpbuf;
	}
	if (advance(s, expbuf)) {
		if (nbra > 0) {
			p = braslist[0];
			num = braelist[0] - p;
			if ((num > MSIZE - 1) || (num < 0))
				yyerror("string too long");
			(void) strncpy(Mstring[0], p, num);
			Mstring[0][num] = '\0';
		}
		return (loc2-s);
	}
	return (0);
}

static void
errxx()
{
	yyerror("RE error");
}

static void
yyerror(s)
char *s;
{
	(void) write(2, "expr: ", 6);
	(void) write(2, gettext(s), (unsigned) strlen(gettext(s)));
	(void) write(2, "\n", 1);
	exit(2);
	/* NOTREACHED */
}

static char
*ltoa(l)
long l;
{
	static str[20];
	register char *sp = (char *) &str[18];	/* u370 */
	register i;
	register neg = 0;

	if (l == 0x80000000L)
		return ("-2147483648");
	if (l < 0)
		++neg, l = -l;
	str[19] = '\0';
	do {
		i = l % 10;
		*sp-- = '0' + i;
		l /= 10;
	}
	while (l);
	if (neg)
		*sp-- = '-';
	return (++sp);
}

static char
*expres(prior, par)
	int prior, par;
{
	int ylex, temp, op1;
	char *r1, *ra, *rb;
	ylex = yylex();
	if (ylex >= NOARG && ylex < MATCH) {
		yyerror("syntax error");
	}
	if (ylex == A_STRING) {
		r1 = Av[Argi++];
		temp = Argi;
	} else {
		if (ylex == '(') {
			paren++;
			Argi++;
			r1 = expres(0, Argi);
			Argi--;
		}
	}
lop:
	ylex = yylex();
	if (ylex > NOARG && ylex < MATCH) {
		op1 = ylex;
		Argi++;
		if (pri[op1-OR] <= prior)
			return (r1);
		else {
			switch (op1) {
			case OR:
			case AND:
				r1 = conj(op1, r1, expres(pri[op1-OR], 0));
				break;
			case EQ:
			case LT:
			case GT:
			case LEQ:
			case GEQ:
			case NEQ:
				r1 = rel(op1, r1, expres(pri[op1-OR], 0));
				break;
			case ADD:
			case SUBT:
			case MULT:
			case DIV:
			case REM:
				r1 = arith(op1, r1, expres(pri[op1-OR], 0));
				break;
			case MCH:
				r1 = match(r1, expres(pri[op1-OR], 0));
				break;
			}
			if (noarg == 1) {
				return (r1);
			}
			Argi--;
			goto lop;
		}
	}
	ylex = yylex();
	if (ylex == ')') {
		if (par == Argi) {
			yyerror("syntax error");
		}
		if (par != 0) {
			paren--;
			Argi++;
		}
		Argi++;
		return (r1);
	}
	ylex = yylex();
	if (ylex > MCH && ylex <= MATCH) {
		if (Argi == temp) {
			return (r1);
		}
		op1 = ylex;
		Argi++;
		switch (op1) {
		case MATCH:
			rb = expres(pri[op1-OR], 0);
			ra = expres(pri[op1-OR], 0);
		}
		switch (op1) {
		case MATCH:
			r1 = match(rb, ra);
			break;
		}
		if (noarg == 1) {
			return (r1);
		}
		Argi--;
		goto lop;
	}
	ylex = yylex();
	if (ylex == NOARG) {
		noarg = 1;
	}
	return (r1);
}

void
main(argc, argv)
	char **argv;
{

	/*
	 * XCU4 allow "--" as argument
	 */
	if (argc > 1 && strcmp(argv[1], "--") == 0)
		argv++, argc--;
	/*
	 * XCU4 - print usage message when invoked without args
	 */
	if (argc < 2) {
		fprintf(stderr, gettext("Usage: expr expression\n"));
		exit(3);
	}
	Ac = argc;
	Argi = 1;
	noarg = 0;
	paren = 0;
	Av = argv;
	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);
	buf = expres(0, 1);
	if (Ac != Argi || paren != 0) {
		yyerror("syntax error");
	}
	/*
	 * XCU4 - strip leading zeros from numeric output
	 */
	clean_buf(buf);
	(void) write(1, buf, (unsigned) strlen(buf));
	(void) write(1, "\n", 1);
	exit((strcmp(buf, "0") == 0 || buf[0] == 0) ? 1 : 0);
	/* NOTREACHED */
}
