/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 *
 * This code is MKS code ported to Solaris with minimum modifications so that
 * upgrades from MKS will readily integrate. Avoid modification if possible!
 */
#ident	"@(#)regcomp.c 1.15	95/10/05 SMI"

/*
 * regcomp, regwcomp, and regfree
 *
 * Copyright (c) 1986 by University of Toronto.
 *	Written by Henry Spencer.  Not derived from licensed software.
 *
 * Copyright 1985, 1992 by Mortice Kern Systems Inc.  All rights reserved.
 *
 * This Software is unpublished, valuable, confidential property of
 * Mortice Kern Systems Inc.  Use is authorized only in accordance
 * with the terms and conditions of the source licence agreement
 * protecting this Software.  Any unauthorized use or disclosure of
 * this Software is strictly prohibited and will result in the
 * termination of the licence agreement.
 *
 * If you have any questions, please consult your supervisor.
 * 
 *	Modifications by Eric Gisin and Alex White.
 *
 *	Permission is granted to anyone to use this software for any
 *	purpose on any computer system, and to redistribute it freely,
 *	subject to the following restrictions:
 *
 *	1. The author is not responsible for the consequences of use of
 *		this software, no matter how awful, even if they arise
 *		from defects in it.
 *
 *	2. The origin of this software must not be misrepresented, either
 *		by explicit claim or by omission.
 *
 *	3. Altered versions must be plainly marked as such, and must not
 *		be misrepresented as being the original software.
 *
 * Beware that some of this code is subtly aware of the way operator
 * precedence is structured in regular expressions.  Serious changes in
 * regular-expression syntax might require a total rethink.
 *
 **** This has been modified to be a subset of the P1003.2 regex library ****
 *
 * Summary of changes:
 *	- adopt the POSIX 1003.2 interface, with some extensions.
 *	- add function, regerror(regex_t*), to get error message string.
 *	- distinguish between basic and extended REs with REG_EXTENDED.
 *	- case insensitive match if REG_ICASE.
 *	  "grep -i" is half as fast with REG_ICASE.
 *	- concatenate STRING string in regbranch() instead of regatom().
 *	- rewrite the handling of \0, |, and ) terminators.
 *	- recognize ^, $, *, etc. only where appropriate (subject to change).
 *	- use struct regnode instead of linear byte stream to represent NFA.
 *	  there are three reasons for this:
 *		code is much more readable,
 *		it is a bit faster - (a|b) is 33% faster.
 *		the NFA is in a format suitable for conversion to DFA.
 *	- add implicit ".*" node to beginning of unanchored REs.
 *	- add BREAK node to optimize ".*".
 *	- add REG_ANCHOR (for grep -x).
 *	- add [:alpha:], etc. bracket expressions.
 *	- replace recursion in regmatch with a back-tracking stack.
 *		now (a|b) is 2-3 times faster.
 *	- remove regrepeat; the new regmatch is faster.
 *	- use bit vectors for character classes. 2-4 times faster.
 *	- regexec now trys all branches, for longest match
 *	- regex.c split regcomp/regfree into regcomp.c, regexec into regexec.c
 *	- multi-character collating element support
 *	- multibyte support
 *	- Make {} constructs work on non-atoms
 */
#ifdef M_RCSID
#ifndef lint
static char rcsID[] = "$Header: /u/rd/src/libc/regex/rcs/regcomp.c 1.39 1995/03/29 21:35:49 rodney Exp $";
#endif
#endif

#include "mks.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <regex.h>
#include "reg.h"
#include <stdarg.h>
#include <m_collate.h>
#include <errno.h>

/*
 * Tunable parameters.
 */

/* The maximum size of the string in a STRING node */
#define REG_MAXSTR      255

/*
 * Flags to be passed up and down.
 */
#define	WORST		0		/* Worst case. */
#define	HASWIDTH	0x1		/* Known never to match null string. */
#define	CLOSEPAR	0x2		/* Subexpression is closed */
#define NOWIDTH		0x4		/* Known never to match anything */

#define	NOTOKEN		(-1)		/* indicates no terminator in token */

/*l
 * We allocate nodes in blocks of 5 for efficiency.
 * It also allows us to iterate over the nodes in an NFA.
 */

#define	REGNODES	5		/* no. nodes for simple RE */

typedef struct _regblock block;
struct _regblock {
	block *next;
	node nodes[REGNODES];
};

#ifdef REG_CESC
static struct {
	char code;
	char character;
} cesc_map[] = {
	{'a', '\a'},
	{'b', '\b'},
	{'f', '\f'},
	{'n', '\n'},
	{'r', '\r'},
	{'t', '\t'},
	{'v', '\v'},
	{ 0 , '\0'},
};
#endif /* REG_CESC */

static	block *regblockp;		/* current allocation block */
static	int regnodei;			/* index of current regnode */

/*
 * Global work variables for regcomp().
 */
static	jmp_buf err;			/* jump back to error hander */
static	int regflags;			/* recomp flags */
static	wuchar_t *regbegin;		/* Input RE */
static	wuchar_t *regparse;		/* Input-scan pointer. */
static	int token;			/* branch terminator: \0, |, or ). */
static	uchar parflags[10];		/* flags of parenthesized sub-exprs */
static	node *optlist;			/* list of ANY nodes in ".*"s */
static  int pardepth;			/* count of open sub-exprs */

static node *reg(regex_t *r, int *flagp);
static node *regbranch(regex_t *r, int *flagp);
static node *regpiece(regex_t *r, int *flagp);
static node *regatom(register regex_t *r, int *flagp);
static node *regclass(regex_t *r);
static node *regnode(int op);
static m_collel_t regelement(void);
static node *regjoin(node *head, node *tail);
static block *regblock(void);
static node *regrepeat(register node *atom);
static void optimize(void);

/*
 * Porting mods
 */
extern void * _loaded_coll_;
wchar_t * reg_m_mbstowcsdup(const char *s);	/* ZZZ */
/*
 * REG_WCHAROPT - this flag is passed in the re_flags field by regcomp to
 * regwcomp to indicate it was called by regcomp.  Note that this flag
 * must not conflict with the regcomp flags defined in regex.h.
 */
#define REG_WCHAROPT 0xF0000000
void _wchar_opt(wchar_t *temp_pattern, const wchar_t *pattern);

/*l
 * Character classes.
 * For each [] bracketted expression, we create a bit map for all the
 * characters in the character set that it can match.
 * Note that the maximum process code is *not* say 256 in single byte
 * code sets, but 256 + # of multi-character collating elements.
 * For wide chars, the assumption is that we only support 2 byte
 * wchar_t's, since 4 byte ones will take unusable amounts of storage.
 * For wide chars, the multi-character collating elements are assumed
 * to be allocated an unused portion of the wide array space.  e.g.
 * no encoding that I know of uses 0xFF as the first half of its process
 * code, so 0xFFyy could encode all the multi-character collating elements.
 * In practice, you will however allocate some unused part to keep the
 * maximum down; such that a full 8K need not be used.
 * However, even if it is, 8K is not an unreasonable size to use.
 */
static	char *cl;	/* Current class being built */

/*f
 * Allocate and clear to zero the bit array.
 */
static void
classbegin(void)
{
	m_collel_t max = m_maxcoll();

	if ((cl = (char *)calloc(1, (unsigned short) ((unsigned short)max+
	    CHAR_BIT-1)/ (unsigned short) CHAR_BIT)) == NULL)
		longjmp(err, REG_ESPACE);
}

/*f
 *  Set the cth bit in the character class.  If icase set, then we set
 *  both upper and lower case of the value.
 */
static void
classadd(m_collel_t c, int icase)
{
	wchar_t wc;

	if (c == -1 || c > 0xFFFF)
		longjmp(err, REG_ECOLLATE);
		
	/* Don't add illegal characters to list */
	if ((wc = _cetowc(_loaded_coll_, c)) < 0)
		return;

	if (icase && !m_ismccollel(c)) {
		if (iswlower(wc))
			classadd(_wctoce (_loaded_coll_, towupper(wc)), 0);
		else if (iswupper(wc))
			classadd(_wctoce (_loaded_coll_, towlower(wc)), 0);
	}
	cl[(unsigned)c/CHAR_BIT] |= 1<<(unsigned)c%CHAR_BIT;
}

/*f
 *  Save the class bits array.
 */
static char *
classclose(void)
{
	return cl;
}

/*f
 *  Free up memory from a class structure.
 */
static void
classfree(register char *clp)
{
	free(clp);
}

int 
classtest(char * CLP, m_collel_t C)
{
	int val;
	if (C == -1)
		return (0);
	val  = 
	((CLP)[(unsigned)(C)/CHAR_BIT] & (1<<(unsigned)(C)%CHAR_BIT));
	return (val);
}



#ifdef DEBUG
/*f
 * regprint - printable representation of opcode
 */
void
regprint(np)
register node *np;
{
	register char *p = NULL;

	printf("%p: ", (void *)np);
	switch (np->type) {
	case RETURN:
		printf("RETURN %d", np->u.i);
		break;
	case BOL:	p = "BOL";	break;
	case BOL_NL:	p = "BOL_NL";	break;
	case EOL:	p = "EOL";	break;
	case EOL_NL:	p = "EOL_NL";	break;
	case ANY:	p = "ANY";	break;
	case ANY_NL:	p = "ANY_NL";	break;
	case ANYOF:	p = "ANYOF";	break;
	case ANYBUT:	p = "ANYBUT";	break;
	case ANYBUT_NL:	p = "ANYBUT_NL"; break;
	case FAIL:	p = "FAIL";	break;
	case EMPTY:	p = "EMPTY";	break;
	case SCAN_NL:	p = "SCAN_NL";	break;
	case SCAN:	p = "SCAN";	break;
	case REPEATSTART:	p = "REPEATSTART";	break;
	case ANYOF2:
		printf(m_smalls("ANYOF2 '%C' or '%C'\n"),
			np->u.c[0], np->u.c[1]);
		break;
	case BRANCH:
		printf("BRANCH %p", (void *)np->u.np);
		break;
	case CHAR:
		printf(m_smalls("CHAR '%C'"), np->u.c[0]);
		break;
	case STRING:
		printf(m_smalls("STRING \"%S\""), np->u.str);
		break;
#ifdef REG_WORDS
	case WORDB:
		p = "WORDB";
		break;
#endif
	case SETSP:
		printf("SETSP %d", np->u.i);
		break;
	case SETEP:
		printf("SETEP %d", np->u.i);
		break;
	case EXPAND:
		printf("EXPAND %d", np->u.i);
		break;
	case BREAK:
		printf(m_smalls("BREAK '%C'"), np->u.c[0]);
		break;
	case BREAK_NL:
		printf(m_smalls("BREAK_NL '%C'"), np->u.c[0]);
		break;
	case SCANTO:
		printf(m_smalls("SCANTO '%C'"), np->u.c[0]);
		break;
	case SEPR:
		printf("SEPR %d", np->u.i);
		break;
	case PUSH:
		printf("PUSH %d", np->u.i);
		break;
	case POP:
		printf("POP %d", np->u.i);
		break;
	case REPEATINFO:
		printf("REPEATINFO {%d,%d:%d}:%d", np->U.min, np->U.max,
			np->U.unbound, np->U.n);
		break;
	case REPEAT:
		printf("REPEAT %p", (void *)np->u.np);
		break;
	case REPEATCHECK:
		printf(m_smalls("REPEATCHECK \"%s\""),
		    np->u.str == NULL ? "(NULL)": (char *)np->u.str);
		break;
	case REPEATPOP:
		printf("REPEATPOP");
		break;
	case REPEATPOP2:
		printf("REPEATPOP_2");
		break;
	case MINMAXSIZE:
		printf("MINMAXSIZE");
		break;
	default:
		printf("*GOK* %d", np->type);
		break;
	}
	if (p != NULL)
		printf("%s", p);
	printf(" -> %p\n", (void *) np->next);
}


/*f
 * regdump - dump a regex onto stdout in vaguely comprehensible form
 */
static void
regdump(regex_t *r)
{
	register block *bp;
	register node *np;
	register int i;

	for (bp = r->re_block; bp != NULL; bp = bp->next)
		for (i = REGNODES, np = bp->nodes; i--; np++) {
			if (np == r->re_node)
				printf("*");
			regprint(np);
		}
	printf("flags %x, ", r->re_flags);
	if (r->re_regmust != NULL)
		printf("longest \"%S\" ", r->re_regmust);
	if (r->re_flags&REG_MUST)
		printf("(must have)");
	printf("\n");
}
#endif /* DEBUG */


#ifdef	M_I18N_MB
extern void * _loaded_coll_;
/*
 * Can't use the mks library function, because regex is in the system libs.
 */
wchar_t *
reg_m_mbstowcsdup(const char *s)
{
	int n;
	wchar_t *w;

	n = strlen(s) + 1;
	if ((w = (wchar_t *)malloc(n * sizeof(wchar_t))) == NULL)
		return(NULL);

	if (_loaded_coll_ != NULL) {
		if (mbstowcs(w, s, n) == -1)
			return(NULL);
	} else {
		int i;
		wchar_t *wp = w; 
		unsigned char *sp = (unsigned char *) s;
		for (i=0; i < n; i++)
			*wp++ = *sp++;
	}
		
	return w;
}
#endif
int
regcomp(register regex_t *r, const char *pattern, int uflags)
{
	wchar_t *wpattern;
	int i;
 	_reginit();		/* Solaris porting mod - load coll table */

	if ((wpattern = reg_m_mbstowcsdup(pattern)) == NULL)
		return errno == EILSEQ ? REG_ECHAR : REG_ESPACE;
	i = regwcomp(r, wpattern, uflags | REG_WCHAROPT);
	free(wpattern);
	return i;
}

/*f
 * regcomp - compile a regular expression into internal code
 *
 * Beware that the optimization-preparation code in here knows about some
 * of the structure of the compiled regex.
 */
int
regwcomp(register regex_t *r, const wchar_t *pattern, int uflags)
{
	register node *ret, *np;
	register size_t i;
	int flags, errnum;
	wchar_t temp_pattern[1024];

 	_reginit();		/* Solaris porting mod - load coll table */
	M_INVARIANTINIT();
	/* catch syntax and internal errors */
	if ((errnum = setjmp(err)) != 0) {
		/* fill up last block */
		while (regnodei != REGNODES)
			(void) regnode(RETURN);
		regfree(r);
		return errnum;
	}

	if (pattern == NULL)
		longjmp(err, REG_EFATAL);

	r->re_block = regblockp = regblock();
	r->re_nsub = 0;
	regflags = uflags;

	/*
	 * If the REG_WCHAROPT flag is set, then we've been called by
	 * regcomp, and any wide characters have already been optimized.
	 * Also, if the collation table is loaded, we don't optimize the
	 * the wide characters.  Otherwise, we call _wchar_opt to
	 * optimize the input characters.
	 */
	if (uflags & REG_WCHAROPT || _loaded_coll_ != NULL) {
		regbegin = regparse = (wuchar_t *)pattern;
	} else {
		_wchar_opt(temp_pattern, pattern);
		regbegin = regparse = (wuchar_t *)temp_pattern;
	}

	optlist = NULL;
	for (i = 0; i < 10; i++)
		parflags[i] = 0;
	pardepth = 0;
	if ((ret = reg(r, &flags)) == NULL)
		longjmp(err, REG_BADPAT);
	if (token != '\0')
		longjmp(err, (token==')') ? REG_EPAREN : REG_BADPAT);

	/*
	 * Find longest literal string that must appear.
	 * Resolve ties in favor of later strings, since
	 * the BREAK check works with the beginning of the r.e.
	 * and avoiding duplication strengthens checking.  Not a
	 * strong reason, but sufficient in the absence of others.
	 */
	i = 0;				/* length of longest string */
	r->re_regmust = NULL;
	for (np = ret; np != NULL; np = np->next) {
		if (np->type == CHAR && 1 >= i) {
			i = 1;
			r->re_regmust = (wchar_t*)np->u.c;
		}
		if (np->type == STRING && wcslen((wchar_t*)np->u.str) >= i) {
			i = wcslen((wchar_t*)np->u.str);
			r->re_regmust = (wchar_t*)np->u.str;
		}
	}

	/* prepend SETSP node to set sub[0].sp */
	np = regnode(SETSP);
	np->u.i = 0;
	np->next = ret;
	ret = np;
	/* append SETEP node to set sub[0].ep */
	np = regnode(SETEP);
	np->u.i = 0;
	regjoin(ret, np);

	if (regflags&REG_ANCHOR)
		regjoin(ret, regnode((regflags&REG_NEWLINE) ? EOL_NL : EOL));
	else if (ret->next->type != BOL && ret->next->type != BOL_NL) {
		np = regnode(SCAN);
		np->next = ret;
		np->u.np = optlist;		/* do SCANTO optimization */
		optlist = np;
		ret = np;
	} else if (ret->next->type == BOL_NL) {
		np = regnode(SCAN_NL);
		np->next = ret;
		ret = np;
	}

	/* append RETURN 1 node to signal success */
	np = regnode(RETURN);
	np->u.i = 1;
	np->next = NULL;
	regjoin(ret, np);

	/* optimize ".*"s to use BREAKs */
	optimize();

	/* unless RE starts with a string, do regmust optimization */
	if (r->re_regmust != NULL) {
#if 0					/* regmust always wins */
		if (ret->type != SCANTO)
#endif
			regflags |= REG_MUST;
	}

	/* fill up last block */
	while (regnodei != REGNODES)
		(void) regnode(RETURN);

	r->re_node = ret;
	r->re_flags = regflags;

#ifdef DEBUG
	if (regflags&REG_DEBUG)
		regdump(r);
#endif

	return 0;
}

/*f
 * reg - regular expression,  top level
 *
 * implements alteration.
 */
static node *
reg(regex_t *r, int *flagp)
{
	register node *ret = NULL;
	register node *br, *np;
	register node *end;
	int flags;

	*flagp = HASWIDTH;	/* Tentatively. */

	/* Pick up the branches, linking them together. */
	token = NOTOKEN;
	br = regbranch(r, &flags);
	*flagp = flags;
#ifndef	REG_EXTENDED
	return br;
#else
	if (token != '|')
		return br;
	*flagp &= ~(~flags&HASWIDTH);
	end = regnode(EMPTY);
	while (1) {
		/* create BRANCH node for this branch */
		regjoin(br, end);
		np = regnode(BRANCH);
		np->u.np = br;
		if (ret == NULL)
			ret = np;
		else
			regjoin(ret, np);

		if (token != '|')
			break;

		token = NOTOKEN;
		br = regbranch(r, &flags);
		*flagp &= ~(~flags&HASWIDTH);
	}
	/* fail after all branches attempted */
	br = regnode(FAIL);
	br->next = end;
	regjoin(ret, br);
	return ret;
#endif /* REG_EXTENDED */
}

/*f
 * regbranch - one alternative of an | operator
 *
 * Implements the concatenation operator.
 */
static node *
regbranch(regex_t *r, int *flagp)
{
	register node *ret = NULL;
	register node *last, *piece;
	wuchar_t *strend;
	int flags;

	*flagp = WORST;		/* Tentatively. */

	last = NULL;
	while ((piece = regpiece(r, &flags)) != NULL) {
		*flagp |= flags&HASWIDTH;
		if (last == NULL) {	/* First piece. */
			ret = last = piece;
			continue;
		}
		/* Join adjacent CHAR nodes into STRING */
		/* We only let a STRING grow REG_MAXSTR chars long */
		if (piece->type == CHAR && piece->next == NULL &&
		    last->next == NULL) {
			if (last->type == CHAR) {
				int c = last->u.c[0];
				last->type = STRING;
				last->u.str = strend = (wuchar_t *)
					malloc(sizeof(wchar_t)*(REG_MAXSTR+1));
				if (strend == NULL)
					longjmp(err, REG_ESPACE);
				*strend++ = c;
			}
			if (last->type == STRING &&
			    strend < last->u.str+REG_MAXSTR) {
				*strend++ = piece->u.c[0];
				*strend = '\0';
				regnodei--;	/* free the current CHAR node */
				continue;
			}
		}
		regjoin(last, piece);
		last = piece;
	}
	if (ret == NULL)
		ret = regnode(EMPTY);
	for (last = ret; last != NULL; last = last->next)
		if (last->type == STRING) {
			/* free the unused portion of STRING nodes */
			last->u.str = strend = (wuchar_t *)
				realloc((void *)last->u.str,
				sizeof(wchar_t)*(wcslen((wchar_t*)last->u.str)+1));
			if (strend == NULL)
				longjmp(err, REG_ESPACE);
		}

	return ret;
}

/*f
 * regpiece - something followed by possible [*+?{]
 * we allow \+ and \? in basic REs as an extension.
 */
static node *
regpiece(regex_t *r, int *flagp)
{
	register node *np, *ret, *atom, *repy, *tp;
	register wuchar_t op, opp;
	register int i;
	unsigned int min = 0, max = 0;	/* {min,max} values */
	int unbound = 0;		/* {min,} unbound repeat */
	int flags;
	int backset;

	/* Pick off one atom to match */
	atom = regatom(r, &flags);
	if (atom == NULL)
		return (NULL);

	/* Is the atom followed by some form of repetition? */
	op = *regparse++;
	op = M_INVARIANT(op);
	backset = 2;
	if (!(regflags&REG_EXTENDED))
		/* BASIC: * is ok, but {, +, and ? must be preceeded by \ */
		if (op == '*') 
			;
		else if (op == '\\' &&
		    (M_INVARIANT(*regparse) == '{'
		     || *regparse == '+' || *regparse == '?')) {
			op = *regparse++, op = M_INVARIANT(op);
			backset = 3;
		}
		else
			op = 0;

	/*
	 * Wasn't something that looked like a repetition?
	 * Reverse the character we just picked up, and return the
	 * whole piece of this RE. Also, if it looked like one of the
	 * '*', '?', or '+' repeaters and it's following a BOL then
	 * treat it as an ordinary character; if it's '{' following a
	 * BOL don't treat it as an ordinary character (safer from the
	 * user standpoint. POSIX 2.8.4.1.2 says this behavior falls into
	 * the undefined zone, but this matches historical practice well.
	 * !Nack!, XPG4 counters this and says '*', '+' or '?' at BOL or
	 * after a '^' at BOL or after a '|' alternation is an error
	 * (REG_BADRPT or REG_BADPAT) in ERE's; same with BRE's except for
	 * '*' which is done historically and '\?' is to treated as a literal;
	 * mostly handled in regatom().
	 * Before the 'atom' is returned alone or wrapped in a repeat
	 * construct, put a PUSH, POP and SEPR before it. The PUSH/POP
	 * preserves the offsets of the subexpression if we need to do
	 * some backtracking and the SEPR clears the pointers as we go forward
	 * because nested subexpressions need to be reset for each iteration
	 * of the outer subexpression in case it does not participate in
	 * the match while the outer one does.
	 */

	/* Don't allow repetition of empty operand. */
	if ((flags & NOWIDTH) && (op == '+' || op == '{' ||
	    (op == '?' && (regflags&REG_EXTENDED)) ||
	    (op == '*' && (regflags&REG_EXTENDED)) )) {
		/*  +, ?, or { } has no atom/RE to repeat */
		longjmp(err, REG_BADRPT);
	}
	else if (!(op == '*' || op == '+' || op == '?' || op == '{')
	    || (M_INVARIANT(regparse[-backset]) == '^' &&
	    (regparse-backset) == regbegin && op != '{') ) {
		regparse--;
		*flagp = flags;
		if (atom->type == PUSH) {
			ret = regnode(PUSH);
			ret->u.i = atom->u.i;
			tp = ret->next = regnode(POP);
			tp->u.i = atom->u.i;
			tp->next = regnode(SEPR);
			tp = tp->next;
			tp->u.i = atom->u.i;
			tp->next = atom;
			atom = ret;
		}
		return (atom);
	}

	ret = NULL;	/* to be sure */

	/*
	 * Now look at repetition type, and do something with it.
	 * No default, because we know op is *+?{
	 * *, + and ? are now all converted to a {min,max} variation
	 * because of null matching when going forward after a backtrack.
	 */
	switch (op) {
	case '*':
		min = 0;
		max = 255;
		unbound = 1;
		break;
	case '+':
		min = 1;
		max = 255;
		unbound = 1;
		break;
	case '?':
		min = 0;
		max = 1;
		unbound = 0;
		break;
	case '{': /* } */
		/* {min[,[max]]}: Parse off minimum */
		if (!iswdigit(*regparse) && *regparse != '\0')
			longjmp(err, REG_BADBR);
		while ((op = *regparse++) != '\0' && iswdigit(op))
			min = min*10 + op-'0';
		/* Parse off maximum, or max same as min */
		if (op == ',') {
			max = 0;
			while ((op = *regparse++) != '\0' && iswdigit(op))
				max = max*10 + op-'0';
			if (max == 0 && regparse[-2] != '0') {
				max = min;
				unbound = 1;
			}
		} else
			max = min;
		/* Parse off trailing delimiter */
		if (op == '\0')
			longjmp(err, REG_EBRACE);
		if (M_INVARIANT(op) == '\\') /* \{ \} and \{ } are both valid */
			op = *regparse++;
		if (M_INVARIANT(op) != '}')
			longjmp(err, REG_BADBR);

		/* Check too large: RE_DUP_MAX is UCHAR_MAX */
		if (min > UCHAR_MAX || max > UCHAR_MAX || max < min)
			longjmp(err, REG_BADBR);
		
		break;
	}

	if (ret == NULL) {
		/*
		 * This is a code match from ipsu 1.55.1.6: If we haven't
		 * generated the repetition loop yet, then we need to
		 * generate a REPEATSTART, [REPEATCHECK,] REPEATINFO,
		 * REPEAT loop. For the weird construct we're building
		 * here, see the execution phase code.
		 */
		if (atom->type == PUSH) {
			ret = regnode(PUSH);
			ret->u.i = atom->u.i;
			tp = ret->next = regnode(POP);
			tp->u.i = atom->u.i;
			tp->next = regnode(SEPR);
			tp = tp->next;
			tp->u.i = atom->u.i;

			repy = np = tp->next = regnode(REPEATSTART);
			np = np->next = regnode(REPEATCHECK);
		}
		else {
			repy = ret = regnode(REPEATSTART);
			/* add a check for a possible empty match */
			np = ret->next = regnode(REPEATCHECK);
		}
		np->u.str = NULL;
		/*if (min > 1)
			min = 1;*/
		np = np->next = regnode(REPEATINFO);
		np->U.min = min;
		np->U.max = unbound ? UCHAR_MAX : max;
		np->U.n = 0;
		np->U.unbound = unbound;
		np->next = regnode(REPEAT);
		np->next->u.np = atom;
		regjoin(atom, repy->next);
	}

	*flagp = (min == 0) ? (WORST) : (WORST|HASWIDTH);
#if 0
	/* P1003.2 does not specify whether these should be literal or an error */
	if (ISMULT(*regparse))
		return NULL;		/*"nested *?+"*/
#endif
	return ret;
}

/*f
 * Generate a repetition node.
 */
static node *
regrepeat(register node *atom)
{
	register node *np;

	np = regnode(BRANCH);
	np->u.np = atom;		/* branch to atom then continue */
	regjoin(atom, np);		/* link atom to BRANCH */
	/* if ".*" add to post-optimization list */
	if ((atom->type == ANY || atom->type == ANY_NL) && atom->next == np) {
		atom->u.np = optlist;
		optlist = atom;
	}
	return np;
}

/*f
 * optimize - optimize ".*" when we know following char
 * In the ANY (ANY_NL) node, the arg is not normally used; we use u.np to
 * chain the ANY nodes together so we can find them all here.
 *
 * Replace the ANY (ANY_NL) with a faster BREAK (BREAK_NL),
 * or the SCAN with a SCANTO.
 * We depend on ANY+1 == BREAK, SCAN+1 == SCANTO, ANY_NL+1 == BREAK_NL
 */
static void
optimize(void)
{
	while (optlist != NULL) {
		register node *np;
		node *any = optlist;

		optlist = optlist->u.np;

		/* any->next is always BRANCH -- find next relevant node */
		np = any->next->next;		/* the EMPTY node */
		while (np->type==EMPTY || np->type==WORDB ||
		    np->type==SETSP || np->type==PUSH || np->type==POP)
			np = np->next;

		/*
		 * Change ANY->BREAK, ANY_NL->BREAK_NL, SCAN->SCANTO
		 * ANY/ANY_NL/SCAN don't normally care about the current
		 * character -- if the following node does, we do this
		 * optimization so we can use strchr at runtime.
		 */
		any->type++;			/* Assume we know next char */
		if (np->type == CHAR)
			any->u.c[0] = np->u.c[0];
		else if (np->type == STRING)
			any->u.c[0] = np->u.str[0];
		else if (np->type == EOL)
			any->u.c[0] = '\0';
		else
			any->type--;		/* mis-assumed */
	}
}

/*f
 * regatom - the lowest level - pick off one character, backreference,
 * subexpression, bracketed RE, start-of-line, ...
 */
static node *
regatom(register regex_t *r, int *flagp)
{
	register node *ret;
	register int op;
	int quoted = 0;
	int i;

	*flagp = WORST;		/* Tentatively. */

	/* Look for backreference, subexpression, and newline */
	op = *regparse++;
	op = M_INVARIANT(op);
	if (op == '\\') {
		op = *regparse++;
		if (op == '\0') {
			longjmp(err, REG_EESCAPE);
		} else if (op == '(' || op == ')') {
			if (regflags&REG_EXTENDED)
				quoted++;
		} else if (op == '{') {
			if (regflags&REG_EXTENDED)
				goto Default;
			else
				longjmp(err, REG_BADRPT);
		} else if (op == '*') {
			goto Default;
		} else if (op == '+') {
			if (regflags&REG_EXTENDED)
				goto Default;
			else
				longjmp(err, REG_BADRPT);
		} else if (op == '?') {
			if (regflags&REG_EXTENDED)
				goto Default;
		else if (regbegin == &regparse[-2])
			goto Default;
		else
			longjmp(err, REG_BADRPT);
		} else if ('1' <= op && op <= '9') {
			int	n = op - '0';

			if (!(parflags[n]&CLOSEPAR))
				longjmp(err, REG_ESUBREG);
			ret = regnode(EXPAND);
			ret->u.i = n;
			*flagp = parflags[n];
			return ret;
#ifdef REG_CESC
		} else if (op == 'x') {
			/* hex escape sequence */
			long n;

			errno = 0;
			n = strtol(regparse, &regparse, 16);
			if ((n == LONG_MAX || n == LONG_MIN)
			    && errno == ERANGE) {
				longjmp(err, REG_BADPAT);
			}
			ret = regnode(CHAR);
			ret->u.c[0] = n & 0xFF;
			ret->u.c[1] = '\0';
			return ret;
		} else if (op == '0') {
			/* octal escape sequence */
			int n = 0;

			for (i = 0; '0' <= *regparse && *regparse <= '7'
			     && i < 3; i += 1) {
				n = (n << 3) + (*regparse++ - '0');
			}
			ret = regnode(CHAR);
			ret->u.c[0] = n;
			ret->u.c[1] = '\0';
			return ret;
#endif /* REG_CESC */
		} else
			quoted++;
	} else {
		if (op == '(' || op == '|' || op == '{') {
			if (!(regflags&REG_EXTENDED))
				quoted++;
		} else if (op == ')') {
			/* Unmatched )'s are ordinary characters in ERE's */
			if (!(regflags&REG_EXTENDED) || pardepth == 0 ) {
				quoted++;
			}
		}
	}

	if (quoted) {
#ifdef REG_WORDS
		switch (op) {
		case '<':
			ret = regnode(WORDB);
			ret->u.i = 0;
			*flagp = NOWIDTH;
			return ret;
		case '>':
			ret = regnode(WORDB);
			ret->u.i = 1;
			*flagp = NOWIDTH;
			return ret;
		}
#endif
#ifdef REG_CESC
		for (i = 0; cesc_map[i].code != '\0'; i += 1) {
			if (cesc_map[i].code == op) {
				ret = regnode(CHAR);
				ret->u.c[0] = cesc_map[i].character;
				ret->u.c[1] = '\0';
				return ret;
			}
		}
#endif
		goto Default;
	}
	switch (op) {
	case '^':
		if (regparse-1 == regbegin || regflags&REG_EXTENDED) {
			ret = regnode((regflags&REG_NEWLINE) ? BOL_NL : BOL);
			*flagp = NOWIDTH;
		}
		else
			goto Default;
		break;
	case '$':
		if (*regparse == '\0' || regflags&REG_EXTENDED) {
			ret = regnode((regflags&REG_NEWLINE) ? EOL_NL : EOL);
			*flagp = NOWIDTH;
		}
		else
			goto Default;
		break;
	case '.':
		ret = regnode((regflags&REG_NEWLINE) ? ANY_NL : ANY);
		*flagp = HASWIDTH;
		break;
	case '[':
		ret = regclass(r);
		op = *regparse++;
		if (M_INVARIANT(op) != ']')
			longjmp(err, REG_EBRACK);
		*flagp = HASWIDTH;
		break;
	case '(':
		op = ++(r->re_nsub);
		pardepth++;
		ret = reg(r, flagp);
		pardepth--;
		if (op < 10)
			parflags[op] = *flagp|CLOSEPAR;
		if (token != ')')
			longjmp(err, REG_EPAREN);

		{
			node *np, *re;

			/* set subexpression start and end positions */
			re = regnode(SETSP);
			re->next = ret;
			re->u.i = op;
			ret = re;
			re = regnode(SETEP);
			re->u.i = op;
			regjoin(ret, re);

			/* generate PUSH and POP to save and restore */
			re = regnode(PUSH);
			re->next = np = regnode(POP);
			re->u.i = np->u.i = op;
			np->next = ret;
			ret = re;
		}
		break;
	case '\0':
	case '|':
	case ')':
		token = op;		/* handled higher up */
		ret = NULL;
		break;
	case '?':
	case '+':
	case '{':
		if (!(regflags & REG_EXTENDED))
			goto Default;
		longjmp(err, REG_BADRPT); /* wasn't quoted out as a literal */
		break;
	case '*':
		if (!(regflags & REG_EXTENDED)) {
			if  (regparse-1 == regbegin ||
			    (regparse-2 == regbegin
			    && M_INVARIANT(regparse[-2]) == '^') ||
			    (regparse-3 >= regbegin
			    && M_INVARIANT(regparse[-2]) == '('
			    && M_INVARIANT(regparse[-3]) == '\\'))
				/* fall through */ ;
			else
				longjmp(err, REG_BADRPT);
				/* not a literal because of RE context */
		}
		else
			longjmp(err, REG_BADRPT);
			/* wasn't quoted out as a literal */
	default:
	Default:
		regparse--;
		op = *regparse++;
		*flagp = HASWIDTH;
		if (regflags&REG_ICASE && iswalpha(op)) {
			ret = regnode(ANYOF2);
			ret->u.c[0] = op;
			ret->u.c[1] = iswlower(op) ? towupper(op) :
				iswupper(op) ? towlower(op) : op;
		} else {
			ret = regnode(CHAR);
			ret->u.c[0] = op;
			ret->u.c[1] = '\0';
		}
		break;
	}

	return ret;
}

/*f
 * regclass - parse character class (bracket expression)
 */
static node *
regclass(regex_t *r)
{
	register node *np;
	register m_collel_t cbeg, cend;

	if (M_INVARIANT(*regparse) == '^') {	/* Complement of range. */
		np = regnode((regflags&REG_NEWLINE) ? ANYBUT_NL : ANYBUT);
		regparse++;
	} else
		np = regnode(ANYOF);
	np->u.clp = NULL;
	classbegin();

	/*
	 * Process any leading - or ] as a special case.
	 */
	if (M_INVARIANT(*regparse) == ']' || *regparse == '-') {
		if ((cbeg = regelement()) == -1)
			longjmp(err, REG_ECOLLATE);
		cend = 0;
		while (*regparse == '-' &&
		    M_INVARIANT(regparse[1]) != ']' && regparse[1] != '\0') {
			m_collel_t n;
			m_collel_t *rp;

			regparse++;
			if ((cend = regelement()) == -1)
				longjmp(err, REG_ECOLLATE);
			/* Range beginning/ending in [==] or [::] not valid */
			if (cbeg == 0 || cend == 0
			|| (n = m_collrange(cbeg, cend, &rp)) <= 0 ||
			    n == (m_collel_t) -1)
				longjmp(err, REG_ERANGE);
			/* Add each character in the range */
			while (n--)
				classadd(*rp++, regflags&REG_ICASE);
			cbeg = cend;
		}
		if (cend == 0 && cbeg > 0)	/* not range & not added */
			classadd(cbeg, regflags&REG_ICASE);
	}

	/*
	 * Process until end of bracketed expression, looking for ranges.
	 */
	while (*regparse != '\0' && M_INVARIANT(*regparse) != ']') {
		if ((cbeg = regelement()) == -1)
			longjmp(err, REG_ECOLLATE);
		cend = 0;
		while (*regparse == '-'
		    && M_INVARIANT(regparse[1]) != ']'
		    && M_INVARIANT(regparse[1]) != '\0') {
			int n;
			m_collel_t *rp;

			regparse++;
			if ((cend = regelement()) == -1)
				longjmp(err, REG_ECOLLATE);
			/* Range beginning/ending in [==] or [::] not valid */
			if (cbeg == 0 || cend == 0
			|| (n = m_collrange(cbeg, cend, &rp)) <= 0)
				longjmp(err, REG_ERANGE);
			/* Add each character in the range */
			while (n--)
				classadd(*rp++, regflags&REG_ICASE);
			cbeg = cend;
		}
		if (cend == 0 && cbeg > 0)	/* not range & not added */
			classadd(cbeg, regflags&REG_ICASE);
	}
	np->u.clp = classclose();
	return np;
}

/*f
 * regelement - parse element of char class, either alone or in range.
 * returns 0 if all the characters are already added to the class by
 * calling classadd; otherwise a character or collating element to add.
 * 0 implies that the element is not valid as an end of a range because
 * it has multiple values
 * e.g. [[=a=]-z], [[:alpha:]-z] are not valid, returns 0;
 * but [[.ch.]-z] is valid.
 */
static m_collel_t
regelement(void)
{
	register m_collel_t c;
	register wuchar_t *cp;
	char *brack;
	wuchar_t *p;
	int m, n;

	/*
	 * Simple case: If it isn't [., [=, or [:, just return the single
	 * character.
	 */
	if (M_INVARIANT(*regparse) != '[' ||
	    ((c=regparse[1]) != '.' && c != '=' && c != ':'))
		return (_wctoce(_loaded_coll_, *regparse++));

	regparse += 2;

	/*
	 * Look forward for bracket of matching type.
	 * We can't nest the same type brackets: [.[.ch.]h.] or similar
	 * is never valid: the only reason we do this at all is for [=[.ch.]=]
	 * Any nesting is meaningless inside [..] or [::]; and only [..]
	 * means anything inside [==].
	 */
	for (cp = regparse; *cp != '\0'; cp++)
		if (*cp == c && M_INVARIANT(cp[1]) == ']')
			break;
	if (*cp == '\0')
		longjmp(err, REG_EBRACK);

	/*
	 * Copy the contents of the [ ... ] out.
	 * It would be so much easier to just stuff a \0 in place,
	 * but regparse is the argument to regcomp, which the standard defines
	 * to be a const *, so it might really be un-stuffable.
	 * Need to explicitly check whether there is an expression in the
	 * bracket.  That is, [..], and [==] are invalid r.e.'s.
	 */
	if ((n = cp - regparse) <= 0)
		longjmp(err, REG_ECOLLATE);

#ifdef	M_I18N_MB
	/*
	 * Since the mks i18n routines work on narrow chars,
	 * convert here.
	 */
	if ((brack = malloc(m = n * (MB_LEN_MAX + 1))) == NULL)
		longjmp(err, REG_ESPACE);
	if ((p = (wuchar_t *)malloc(sizeof(wchar_t)*(n+1))) == NULL) {
		free(brack);
		longjmp(err, REG_ESPACE);
	}
	for (n = 0; regparse[n] != '\0'; n++) {
		if (regparse[n] == c && M_INVARIANT(regparse[n+1]) == ']')
			break;
		p[n] = regparse[n];
	}
	if (!n) {
		free(brack);
		free(p);
		longjmp(err, REG_ECHAR);
	}
	p[n] = 0;
	if ((n = wcstombs(brack, p, m)) == -1) {
		free(brack);
		free(p);
		longjmp(err, REG_ECHAR);
	}
	free(p);
#else
	if ((brack = malloc(n+1)) == NULL)
		longjmp(err, REG_ESPACE);
	memcpy(brack, regparse, n);
#endif
	brack[n] = '\0';
	switch (c) {
	case ':': {	/* [: character-class-name :] */
		m_collel_t n;
		m_collel_t *ccl;

		n = m_cclass(brack, &ccl);
		free(brack);
		if (n == (m_collel_t) -1)
			longjmp(err, REG_ECTYPE);
		while (n-- > 0)
			if ((c = *ccl++) != '\0')
				classadd(c, regflags&REG_ICASE);
		c = 0;
		break;
	}
	case '.':	/* [. collating-symbol .] */
		c = m_strtocoll(brack);
		free(brack);
		if (c == (m_collel_t) -1)
			longjmp(err, REG_ECOLLATE);
		break;

	/*l
	 * Equivalence class is normally a single character; which may
	 * expand to multiple characters and some multi-character
	 * collating elements.  There doesn't appear to be any reason that
	 * it can't be a collating-element: [=[.ch.]=] should produce all
	 * chars and collating elements equivalent to the collating element ch.
	 * Note that as of D11.2 of posix.2, the regular expression grammar
	 * in 2.8.5 doesn't make any sense.
	 */
	case '=': {	/* [= equivalence-class =] */
		int n;
		m_collel_t *rp;

		/*
		 * We accept either [=ch=], or [=[.ch.]=].
		 * It isn't obvious which the standard requires/allows.
		 * If it starts with [, then recurse to do this.
		 */
		if (M_INVARIANT(*brack) == '[') {
			free(brack);	/* In case longjmp out of regelement */
			/* Try for [=[.ch.]=] style */
			c = regelement();
			/*
			 * regelement will return 0 if it was [==] or [::],
			 * otherwise return the appropriate multi-character
			 * collating element.
			 * regparse when done must point at our ending point
			 * (the =]) or we have something like [=[.ch.]x=].
			 * Why isn't there a special error message like
			 * REG_EEQUIV?
			 */
			if (c == 0 || regparse != cp)
				longjmp(err, REG_ECOLLATE);
		} else {
			/* Try for [=ch=] */
			c = m_strtocoll(brack);
			free(brack);
			if (c == (m_collel_t) -1)
				longjmp(err, REG_ECOLLATE);
		}

		n = m_collequiv(c, &rp);
		if (n!= (m_collel_t) -1)
			while (n-- > 0)
				classadd(*rp++, regflags & REG_ICASE);
		c = 0;
		break;
	}
	}
	regparse = cp+2;
	return (c);
}

/*f
 * regnode - allocate and initialize a node
 */
static node *
regnode(int op)
{
	register node *np;

	if (regnodei == REGNODES)
		regblockp = regblockp->next = regblock();
	np = &regblockp->nodes[regnodei++];
	np->type = op;
	np->next = NULL;
	return np;
}

/*f
 * regblock - allocate a block of nodes.
 */
static block *
regblock(void)
{
	register block *bp;

	bp = (block*) malloc(sizeof(block));
	if (bp == NULL)
		longjmp(err, REG_ESPACE);
	bp->next = NULL;
	regnodei = 0;
	return bp;
}

/*f
 * regjoin - concatenate two node lists.
 */
static node *
regjoin(node *head, node *tail)
{
	register node *np, *nplast = NULL;

	if (head == NULL)
		return tail;
	for (np = head; np != NULL; np = np->next)
		nplast = np;
	nplast->next = tail;
	return head;
}

/*f
 *  Free a regular expression.  Cycle thru all the nodes freeing all the
 *  memory associated with them.
 */
void
regfree(regex_t *r)
{
	register block *bp;
	register node *np;
	register int i;
	block *nextbp;

	if (r->re_block == NULL)
		return;
	for (bp = r->re_block; bp != NULL; bp = nextbp) {
		for (i = REGNODES, np = bp->nodes; i--; np++)
			switch (np->type) {
			  case STRING:
				if (np->u.str != NULL)
					free((void *)np->u.str);
				break;
			  case ANYOF: case ANYBUT: case ANYBUT_NL:
				if (np->u.clp != NULL)
					classfree(np->u.clp);
				break;
			}
		nextbp = bp->next;
		free((void *) bp);
	}
	r->re_block = NULL;
}

/*
 * wchar_opt	- Scan the string for 8 bit chars converted to wide chars
 *
 * This routine is to be invoked only in "C" locale.  It scans the string
 * to see if there are wide characters over 255 - that means that it
 * was a character in the range of 128-255 that got converted via
 * wctomb into a wide char.  In that case convert back to the 8 bit value
 * and assign it directly to the wide character string.
 */
void
_wchar_opt(wchar_t *temp_pattern, const wchar_t *pattern)
{
	unsigned char temp[16];
	while (*pattern) {
		if (*pattern > 255) {
			if (wctomb((char *)temp, *pattern) != 1)
				longjmp(err, REG_ECOLLATE);
			else
				*temp_pattern = (wchar_t) temp[0];
		} else
			*temp_pattern = *pattern;
		temp_pattern++;
		pattern++;
	}
	*temp_pattern = (wchar_t) '\0';
}
