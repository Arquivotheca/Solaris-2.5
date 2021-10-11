/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 *
 * This code is MKS code ported to Solaris with minimum modifications so that
 * upgrades from MKS will readily integrate. Avoid modification if possible!
 */
#ident	"@(#)reg.h 1.5	95/04/06 SMI"

/*
 * Internal header file for the regex library routines.
 *
 * Copyright 1993 by Mortice Kern Systems Inc.  All rights reserved.
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
 */

/*l
 *	Structure of a compiled regular expression
 *
 * This is essentially a nondeterministic finite-state machine
 * (aka syntax charts or "railroad normal form" in parsing technology).
 * Each node is an opcode, a "next" pointer, and a multi-type operand.
 * "Next" pointers of all nodes except BRANCH implement concatenation;
 * a BRANCH node hold two alternative in the next and u.np fields.
 * The NFA is a directed graph, starting at r->node and ending with RETURN 1.
 * (NB this is *not* a tree structure:  the tail of the branch connects
 * to the thing following the set of BRANCHes.)
 */

typedef struct _regnode node;
struct _regnode {
	int type;			/* node type */
	node *next;			/* successor node */
	union {
		int i;			/* integer */
		wuchar_t c[2];		/* char (0 terminated) */
		node *np;		/* branch node */
		wuchar_t *str;		/* string */
		char *clp;		/* character class */
		struct {
			unsigned char min,	/* {min,     */
				      max,	/*      max} */
				      n,	/* current repeat count */
				      unbound;	/* no max */
		} repeat;
	} u;				/* operands */
};

#define	U	u.repeat

/*l
 * The opcode definitions follow. There are some special cases:
 *
 * RETURN
 *	Return from the match function, signaling failure(0)
 *	or success(1). This is the only node without a next pointer.
 *
 * BRANCH
 *	Implement a two-way choice, first u.np, then next.
 *
 * SETSP, SETEP
 *	These set an subexpression start or end mark.
 *
 * PUSH, POP
 *	Save and restore subexpression start and end positions.
 *	These always occur as a pair, but only do the PUSH while
 *	matching, and only do the POP while backtracking.
 *
 * BREAK, BREAK_NL
 *	This replaces the ANY in ".*" when we know the next character.
 *	Instead of attempting a match at every string position,
 *	we only attempt to match at positions that contain the next character.
 *	This is an optimization that avoids unnecessary backtracking.
 *
 * SCAN, SCANTO
 *	These match the initial part of the string when the pattern
 *	is not anchored with "^". The ANY/BREAK optimization is done.
 *	This is equivalent to the BRANCH/ANY loop that ".*" generates,
 *	but it is faster and it match shortest-first instead of longest.
 */

/* definition	number	opnd?	meaning */
#define	RETURN	0	/* int	Return with failure(0) or success(1). */
#define	BOL	1	/* no	Match "" at beginning of line. */
#define	EOL	2	/* no	Match "" at end of line. */
#define	ANY	3	/* no	Match any one character. */
#define	BREAK	4	/* char	Match up to next occurence of char */
#define	ANYOF	5	/* clp	Match any character in this class. */
#define	ANYBUT	6	/* clp	Match any character not in this class. */
#define	BRANCH	7	/* node	Match np->u.np or np->next. */
#define	FAIL	8	/* no	Fail. */
#define	EMPTY	9	/* no	Match empty string. */
#define	CHAR	10	/* char	Match single char. */
#define	STRING	11	/* str	Match string. */
#define	ANYOF2	12	/* char[2] Match either character (for REG_ICASE) */
#define	WORDB	13	/* int	Match start(0) or end(1) of word */
#define	SETSP	14	/* int	Set sub[i].rm_sp */
#define	SETEP	15	/* int	Set sub[i].rm_ep */
#define	PUSH	16	/* int	Push sub[i]. */
#define	POP	17	/* int	Pop sub[i]. */
#define	EXPAND	18	/* int	Match sub[i]. */
#define	SCAN	19	/* no	Match initial part of unanchored RE. */
#define	SCANTO	20	/* char	Match initial part of unanchored RE. */
/* Following same as above and are used when REG_NEWLINE was set */
#define	BOL_NL	21
#define	EOL_NL	22
#define	ANY_NL	23
#define	BREAK_NL 24	/* must be ANY_NL+1 */
#define	ANYBUT_NL 25
#define	SCAN_NL	26	/* no   Find potential line after \n */
#define	REPEATSTART 27	/* no	Initialize the REPEATINFO current value */
#define	REPEATINFO  28	/* struct Push matching {} construct */
#define	REPEAT	    29	/* node	Pointer to end of REPEAT construct */
#define REPEATCHECK 30	/* check repeat is actually doing/going some{thing,where} */
#define REPEATPOP 31	/* backup the counter on {,} correctly */
#define REPEATPOP2 32	/* backup the CHECK ptr correctly */
#define MINMAXSIZE 33	/* for backtracking repeaters (*, ?, +, {}) */
#define SEPR 34		/* SubExPression Reset node; for nested subexp's */

#ifdef DEBUG
void regprint ANSI((register node *np));
#endif
