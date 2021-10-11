/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#ident	"@(#)regex.c 1.9 88/02/08 SMI"

#ident	"@(#)regex.c 1.3 91/12/20"

#include <stdio.h>
#include <locale.h>
#include <sys/types.h>
#include <ctype.h>
#include <stdlib.h>
/*
 * routines to do [multiple] regular expression matching
 *
 * Entry points:
 *
 *	mreg_comp(s)
 *		char *s;
 *	... returns 0 if the string s was compiled successfully,
 *		a pointer to an error message otherwise.
 *	    If passed 0 or a null string returns without changing
 *		the currently compiled re (see note 11 below).
 *
 *	mreg_exec(s)
 *		char *s;
 *	 ... returns 1 if the string s matches the last compiled regular
 *		expression,
 *		    0 if the string s failed to match the last compiled
 *			regular expression, and
 *		    -1 if the compiled regular expression was invalid
 *			indicating an internal error).
 *
 * The strings passed to both mreg_comp and mreg_exec may have trailing or
 * embedded newline characters; they are terminated by nulls.
 *
 * The identity of the author of these routines is lost in antiquity;
 * this is essentially the same as the re code in the original V6 ed.
 *
 * The regular expressions recognized are described below. This description
 * is essentially the same as that for ed.
 *
 *	A regular expression specifies a set of strings of characters.
 *	A member of this set of strings is said to be matched by
 *	the regular expression.  In the following specification for
 *	regular expressions the word `character' means any character but NUL.
 *
 *	1.  Any character except a special character matches itself.
 *	    Special characters are the regular expression delimiter plus
 *	    \ [ . and sometimes ^ * $.
 *	2.  A . matches any character.
 *	3.  A \ followed by any character except a digit or ( )
 *	    matches that character.
 *	4.  A nonempty string s bracketed [s] (or [^s]) matches any
 *	    character in (or not in) s. In s, \ has no special meaning,
 *	    and ] may only appear as the first letter. A substring
 *	    a-b, with a and b in ascending ASCII order, stands for
 *	    the inclusive range of ASCII characters.
 *	5.  A regular expression of form 1-4 followed by * matches a
 *	    sequence of 0 or more matches of the regular expression.
 *	6.  A regular expression, x, of form 1-8, bracketed \(x\)
 *	    matches what x matches.
 *	7.  A \ followed by a digit n matches a copy of the string that the
 *	    bracketed regular expression beginning with the nth \( matched.
 *	8.  A regular expression of form 1-8, x, followed by a regular
 *	    expression of form 1-7, y matches a match for x followed by
 *	    a match for y, with the x match being as long as possible
 *	    while still permitting a y match.
 *	9.  A regular expression of form 1-8 preceded by ^ (or followed
 *	    by $), is constrained to matches that begin at the left
 *	    (or end at the right) end of a line.
 *	10. A regular expression of form 1-9 picks out the longest among
 *	    the leftmost matches in a line.
 *	11. An empty regular expression stands for a copy of the last
 *	    regular expression encountered.
 *	12. More than one `branch' (multiple regular expressions of
 *	    form 1-9) may be specified separated by |.  Evaluation of
 *	    branches occurs left-to-right and ends when any branch
 *	    matches the argument string.
 */

/*
 * constants for re's
 */
#define	CBRA	1
#define	CCHR	2
#define	CDOT	4
#define	CCL	6
#define	NCCL	8
#define	CDOL	10
#define	CEOF	11
#define	CKET	12
#define	CBACK	18

#define	CSTAR	01

#define	ESIZE	512
#define	NBRA	9

static struct re_globals {
	char	_expbuf[ESIZE];
	char	*_braslist[NBRA], *_braelist[NBRA];
	char	_circf;
	struct re_globals *next;
} *re_globals;
#define	expbuf (_re->_expbuf)
#define	braslist (_re->_braslist)
#define	braelist (_re->_braelist)
#define	circf (_re->_circf)

#ifdef __STDC__
static int advance(char	*, char *, struct re_globals *);
static int backref(int, char *, struct re_globals *);
static int cclass(char *, int, int);
#else
static int advance();
static int backref();
static int cclass();
#endif

/*
 * compile the regular expression argument into a dfa
 */
char *
mreg_comp(sp)
	register char	*sp;
{
	struct re_globals *_re;
	char	c, *ep;
	int	cclcnt, numbra = 0;
	char	*lastep = 0;
	char	bracket[NBRA];
	char	*bracketp = &bracket[0];
	char	*retoolong = gettext("Regular expression too long");

	if (re_globals == 0) {
		_re = (struct re_globals *) calloc(1, sizeof (*_re));
		if (_re == 0)
			return (gettext("Out of memory"));
		re_globals = _re;
	} else {
		register struct re_globals *next;
#ifdef lint
		next = _re;
#endif
		for (_re = re_globals->next; _re; _re = next) {
			next = _re->next;
			free((char *)_re);
		}
		re_globals->next = 0;
		_re = re_globals;
	}
	ep = expbuf;

#define	comerr(msg) { expbuf[0] = 0; numbra = 0; return (msg); }

	if (sp == 0 || *sp == '\0') {
		if (*ep == 0)
			return (gettext("No previous regular expression"));
		return (0);
	}
	if (*sp == '^') {
		circf = 1;
		sp++;
	}
	else
		circf = 0;
	for (;;) {
		if (ep >= &expbuf[ESIZE])
			comerr(retoolong);
		if ((c = *sp++) == '\0') {
			if (bracketp != bracket)
				comerr(gettext("unmatched \\("));
			*ep++ = CEOF;
			*ep++ = 0;
			return (0);
		}
		if (c != '*')
			lastep = ep;
		switch (c) {

		case '.':
			*ep++ = CDOT;
			continue;

		case '*':
			if (lastep == 0 || *lastep == CBRA || *lastep == CKET)
				goto defchar;
			*lastep |= CSTAR;
			continue;

		case '$':
			if (*sp != '\0')
				goto defchar;
			*ep++ = CDOL;
			continue;

		case '[':
			*ep++ = CCL;
			*ep++ = 0;
			cclcnt = 1;
			if ((c = *sp++) == '^') {
				c = *sp++;
				ep[-2] = NCCL;
			}
			do {
				if (c == '\0')
					comerr(gettext("missing ]"));
				if (c == '-' && ep [-1] != 0) {
					if ((c = *sp++) == ']') {
						*ep++ = '-';
						cclcnt++;
						break;
					}
					while (ep[-1] < c) {
						*ep = ep[-1] + 1;
						ep++;
						cclcnt++;
						if (ep >= &expbuf[ESIZE])
							comerr(retoolong);
					}
				}
				*ep++ = c;
				cclcnt++;
				if (ep >= &expbuf[ESIZE])
					comerr(retoolong);
			} while ((c = *sp++) != ']');
			lastep[1] = cclcnt;
			continue;

		case '\\':
			if ((c = *sp++) == '(') {
				if (numbra >= NBRA)
					comerr(gettext(
						"too many \\(\\) pairs"));
				*bracketp++ = numbra;
				*ep++ = CBRA;
				*ep++ = numbra++;
				continue;
			}
			if (c == ')') {
				if (bracketp <= bracket)
					comerr(gettext("unmatched \\)"));
				*ep++ = CKET;
				*ep++ = *--bracketp;
				continue;
			}
			if (isdigit((u_char)c)) {
				int	n;
				(void) sscanf(&c, "%1d", &n);	/* XXX */
				if (n >= 1 && n < NBRA + 1) {
					*ep++ = CBACK;
					(void) sprintf(ep++, "%1.1d", n - 1);
					continue;
				}
			}
			*ep++ = CCHR;
			*ep++ = c;
			continue;

		case '|':
			if (bracketp != bracket)
				comerr(gettext("unmatched \\("));
			*ep++ = CEOF;
			*ep++ = 0;
			_re->next =
			    (struct re_globals *) calloc(1, sizeof (*_re));
			_re = _re->next;
			if (_re == 0)
				return (gettext("Out of memory"));
			bracketp = &bracket[0];
			ep = expbuf;
			numbra = 0;
			lastep = 0;
			continue;

		defchar:
		default:
			*ep++ = CCHR;
			*ep++ = c;
		}
	}
}

/*
 * match the argument string against one of the compiled re's
 */
int
mreg_exec(str)
	char	*str;
{
	register struct re_globals *_re = re_globals;
	register char	*p1, *p2;
	register int	c;
	int	rv;

	if (re_globals == 0)
		return (0);
	for (_re = re_globals; _re != 0; _re = _re->next) {
		p1 = str;
		p2 = expbuf;
		for (c = 0; c < NBRA; c++) {
			braslist[c] = 0;
			braelist[c] = 0;
		}
		if (circf) {
			if (advance(p1, p2, _re))
				return (1);
			continue;
		}
		/*
		 * fast check for first character
		 */
		if (*p2 == CCHR) {
			c = p2[1];
			do {
				if (*p1 != c)
					continue;
				if (rv = advance(p1, p2, _re))
					return (rv);
			} while (*p1++);
			continue;
		}
		/*
		 * regular algorithm
		 */
		do
			if (rv = advance(p1, p2, _re))
				return (rv);
		while (*p1++);
	}
	return (0);
}

/*
 * try to match the next thing in the dfa
 */
static	int
advance(lp, ep, _re)
	register char	*lp, *ep;
	register struct re_globals *_re;
{
	register char	*curlp;
	int	ct, i;
	int	rv;

	for (;;)
		switch (*ep++) {

		case CCHR:
			if (*ep++ == *lp++)
				continue;
			return (0);

		case CDOT:
			if (*lp++)
				continue;
			return (0);

		case CDOL:
			if (*lp == '\0')
				continue;
			return (0);

		case CEOF:
			return (1);

		case CCL:
			if (cclass(ep, (u_char)*lp++, 1)) {
				ep += *ep;
				continue;
			}
			return (0);

		case NCCL:
			if (cclass(ep, (u_char)*lp++, 0)) {
				ep += *ep;
				continue;
			}
			return (0);

		case CBRA:
			braslist[*ep++] = lp;
			continue;

		case CKET:
			braelist[*ep++] = lp;
			continue;

		case CBACK:
			if (braelist[i = *ep++] == 0)
				return (-1);
			if (backref(i, lp, _re)) {
				lp += braelist[i] - braslist[i];
				continue;
			}
			return (0);

		case CBACK|CSTAR:
			if (braelist[i = *ep++] == 0)
				return (-1);
			curlp = lp;
			ct = braelist[i] - braslist[i];
			while (backref(i, lp, _re))
				lp += ct;
			while (lp >= curlp) {
				if (rv = advance(lp, ep, _re))
					return (rv);
				lp -= ct;
			}
			continue;

		case CDOT|CSTAR:
			curlp = lp;
			while (*lp++)
				;
			goto star;

		case CCHR|CSTAR:
			curlp = lp;
			while (*lp++ == *ep)
				;
			ep++;
			goto star;

		case CCL|CSTAR:
		case NCCL|CSTAR:
			curlp = lp;
			while (cclass(ep, (u_char)*lp++, ep[-1] == (CCL|CSTAR)))
				;
			ep += *ep;
			goto star;

		star:
			do {
				lp--;
				if (rv = advance(lp, ep, _re))
					return (rv);
			} while (lp > curlp);
			return (0);

		default:
			return (-1);
		}
}

static int
backref(i, lp, _re)
	register int	i;
	register char	*lp;
	register struct re_globals *_re;
{
	register char	*bp;

	bp = braslist[i];
	while (*bp++ == *lp++)
		if (bp >= braelist[i])
			return (1);
	return (0);
}

static int
cclass(set, c, af)
	register char	*set;
	int	c, af;
{
	register int	n;

	if (c == 0)
		return (0);
	n = *set++;
	while (--n)
		if (*set++ == c)
			return (af);
	return (!af);
}
