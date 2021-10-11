/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 *
 * This code is MKS code ported to Solaris with minimum modifications so that
 * upgrades from MKS will readily integrate. Avoid modification if possible!
 */
#ident	"@(#)fnmatch.c 1.8	95/10/05 SMI"

/*
 * fnmatch: P1003.2 shell-style filename pattern matching
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
 */
#ifdef M_RCSID
#ifndef lint
static char rcsID[] = "$Header: /u/rd/src/libc/gen/rcs/fnmatch.c 1.43 1994/05/09 15:21:27 miked Exp $";
#endif
#endif

/*l
 * Extention: Handles case-insensitive comparisions for filesystems which
 * don't support dual-case thru DUALCASE environment variable (turned on via
 * M_FNMATCH_DUALCASE), and thru FNM_IGNORECASE flag.
 *
 * Outstanding issues: i) Does backslash remove meaning inside []?
 * (full description of the issue below)
 * ii) Are multi-character collating-elements supposed to match?
 * Both these will turn into ballots next chance to POSIX.
 *
 * Quick fnmatch description:
 * If you want unanchored, prepend and append a '*'.
 * If flag is FNM_NOESCAPE, then disable \ quoting.
 * If flag is FNM_PATHNAME, then '/' in string will not be matched by these.
 * If flag is FNM_PERIOD, then leading '.'in string not matched by these.
 * If flag is FNM_IGNORECASE, then ignore case in comparision.
 *
 * Returns : 0           to indicate    match found
 * 	     FNM_NOMATCH to indicate no errors occurred, no match found
 * 	     FNM_ERROR   to indicate an error  occurred
 *
 * Special characters:
 * "?"		matches any character
 * "*"		matches zero or more of any characters
 * "[a-z]"	matches a set of chars
 * "[!A-Z]"	matches the complement of a set
 * "[[:alpha:]]" matches the set of all characters in the alpha class
 * "[[.c.]]"	matches the collating-element c
 * "[[=a=]]"	matches the equivalence class a
 *
 * MB NOTES
 *
 * When compiled in a MB environment, the functions fnwwmatch() and
 * fnwnmatch() are both available.  These take a wide pattern string
 * and, respectively, a wide or narrow file name.  The most efficient
 * of these to use, if possible, is fnwnmatch(), since it doesn't need
 * to widen or narrow any of its arguments to call the underlying
 * driver function, domatch().
 */

#include <mks.h>
#include <stdlib.h>
#include <fnmatch.h>
#include <ctype.h>
#include <m_collate.h>
#include <limits.h>
#include <string.h>

extern void *_loaded_coll_;

/* these defines make the code tidy */
#define NOMATCH	((wchar_t *)FNM_NOMATCH)
#define ERROR	((wchar_t *)FNM_ERROR)

STATREF const wchar_t *fn_cclass ANSI((const wchar_t *pp, wint_t c, int flag));
STATREF const wchar_t *element ANSI((wint_t matchc, const wchar_t *p,
			int *match, m_collel_t *last));
STATREF int domatch ANSI((wchar_t *, const char *, int, mbstate_t *));
extern	wchar_t *m_mbstowcsdup ANSI((const char *s));

/*f
 * If FNM_IGNORECASE set then return the argument in lowercase.
 */
static wint_t
handlecase(wint_t c, int flag)
{
	if (flag&FNM_IGNORECASE)
		return (towlower(c));
	else
		return (c);
}

/*f
 * do pattern matching
 */
LDEFN int
fnmatch(pattern, string, flag)
const char *pattern, *string;
int flag;
{
#ifdef M_I18N_MB
	wchar_t	*wpat;
	mbstate_t sstate = { 0 };
	int status;
	_reginit();

	if ((wpat = m_mbstowcsdup(pattern)) == (wchar_t *)0)
		return (FNM_ERROR);

	status = domatch(wpat, string, flag, &sstate);
	free(wpat);
	return(status);
}

/*f
 * Pattern matching: wide pattern, narrow string
 */
LDEFN int
fnwnmatch(pattern, string, flag)
wchar_t *pattern;
const char *string;
int flag;
{
	mbstate_t sstate = { 0 };

	_reginit();
	return(domatch(pattern, string, flag, &sstate));
}

LDEFN int
fnwwmatch(pattern, string, flag)
wchar_t *pattern;
wchar_t *string;
int flag;
{
	char	*nstr;
	mbstate_t sstate = { 0 };
	int status;

	if ((nstr = (char *) m_wcstombsdup(string)) == NULL)
		return(FNM_ERROR);

	status = domatch(pattern, nstr, flag, &sstate);
	free(nstr);

	return(status);
}

/*f
 * Widen the MB character pointed to by *str, and store
 * it into *ch.  Increment the string pointer past the MB
 * sequence, and return the size of the MB sequence, or the
 * error code returned.
 */
STATIC int
bump_string(str, ch, ps)
char **str;
wchar_t *ch;
mbstate_t *ps;
{
	int incr;

	if ((incr = mbrtowc(ch, *str, INT_MAX, ps)) > 0)
		*str += incr;

	return (incr);
}

/*f
 * int domatch(wchar_t *pattern, char *string, int flag, mbstate_t *sstate)
 *
 * This is the function which actually performs the file matching
 * operation.  fnmatch() is just a front end which invokes the
 * driver properly.  We need to do it this way in order to ensure
 * that the correct state information is passed to recursive invocations
 * of the function.
 */
STATIC int
domatch(pattern, string, flag, sstate)
wchar_t *pattern;
const char *string;
int flag;
mbstate_t *sstate;
{

#else	/* ! M_I18N_MB -- continue fnmatch function */

#define	domatch(p, s, f, st)	fnmatch(p, s, f)
#endif /* M_I18N_MB */

	const char *cp = string;
	register const wchar_t *pp = pattern;
	wchar_t c;
	register const char *lastcp;
	wchar_t schar, lastchar;
	int status;
	const wchar_t *xp;
#ifdef	M_I18N_MB
	mbstate_t laststate;
#endif

	M_INVARIANTINIT();
#if M_FNMATCH_DUALCASE
	if (getenv("DUALCASE") == NULL)
		flag |= FNM_IGNORECASE;
#endif
	/*
	 * Loop matching each pattern component with each character in the
	 * string.
	 */
	schar = 0;
	while (1) {
		lastcp = cp;
		lastchar = schar;

#ifdef	M_I18N_MB
		laststate = *sstate;

		/*
		 * Fetch next character from the string
		 * Note that the string is passed narrow, while pattern
		 * is passed wide.
		 * Not invariantized, since we don't compare against internal
		 * constants
		 */
		if ((status = bump_string(&cp, &schar, sstate)) == -1)
			return FNM_ERROR;
		else if (status == -2) {
			/*l
			 * redundant shifts all the way to the end of
			 * the string
			 */
			return (*pp == 0 ? 0 : FNM_NOMATCH);
		}
#else
		schar = *(uchar *)cp++;
#endif

		/*
		 * Fetch next character from the wide pattern.
		 * Since we compare it to [ and \, invariant it.
		 */
		c = *(wuchar_t *)pp++;
		c = M_INVARIANT(c);

		/*l
		 * If we don't match initial '.'s in pathnames, and we're
		 * at a '.', and this is the beginning of the string,
		 * or the character right after a '/', and we're trying to
		 * match a pattern, then fail.
		 */
		if ((flag & FNM_PERIOD) && (schar == '.')
		    && (lastcp==string
			|| ((flag&FNM_PATHNAME && lastchar=='/')))
		    && (c == '?' || c == '[' || c == '*'))
			return (FNM_NOMATCH);

		switch (c) {
		case '\0': /* End of pattern -- success if end of string */
			return (schar == 0 ? 0 : FNM_NOMATCH);

		case '?': /* Match single character -- skip character */
			if (schar==0 || flag&FNM_PATHNAME && schar=='/')
				return (FNM_NOMATCH);
			continue;

		case '[': /* Match character class */
			if (schar==0 || flag&FNM_PATHNAME && schar=='/')
				return (FNM_NOMATCH);
			/*
			 * Debate: Should ``[a-z'', where there is no closing
			 * ], fall back on matching [, a, -, z?
			 * D11.2: 2.8.3.1.2, lines 3633, 3634 appear to
			 * say the behaviour is undefined, so we don't
			 * have to do it.
			 */
			xp = fn_cclass(pp, schar, flag);
			if (xp == NOMATCH)
				return (FNM_NOMATCH);
			if (xp == ERROR)
				return (FNM_ERROR);
			pp = xp;
			continue;

		case '*':
			/*
			 * Match any number of characters. Because fnmatch
			 * has to match the entire string, we don't need
			 * to match the longest one -- the first match which
			 * allows us to accept the string is OK.  It might be
			 * less efficient that searching backwards, but it's
			 * better than widening the entire string for
			 * the multibyte case.
			 */
			while ((status = domatch((wchar_t *)pp,
			    lastcp, flag, &laststate)) == FNM_NOMATCH) {
				if (schar == 0
				|| (schar == '/' && (flag&FNM_PATHNAME)))
					break;

				lastcp = cp;
				lastchar = schar;
#ifdef	M_I18N_MB
				laststate = *sstate;
				if ((status = bump_string(&cp, &schar, sstate)) == -1)
					return FNM_ERROR;
				else if (status == -2) {
					/*l
					 * More redundant shifts
					 */
					return (*pp == 0 ? 0 : FNM_NOMATCH);
				}
#else
				schar = *(uchar *)cp++;
#endif
			}
			return (status);

		case '\\':
			if (!(flag&FNM_NOESCAPE) && *pp++ == '\0')
				return (FNM_NOMATCH);
			/*FALLTHROUGH*/

		default:
			/* Do not invariant the characters in the compare! */
			if (schar == 0 ||
			    handlecase(((wuchar_t *)pp)[-1], flag)
			    != handlecase((wuchar_t)schar, flag))
				return (FNM_NOMATCH);
		}
	}
	/* NOTREACHED */
}

/*f
 * Handle [] construct.  pp is pointer at first character of the inside of
 * a [] construct; c is the character we want to match against; we return
 * either NOMATCH to indicate that the character didn't match, ERROR
 * to indicate that an error occurred, or a pointer to the first character
 * after the closing ].
 *
 * We don't know if the intent of posix 3.13.1 which points at 2.8.3.2
 * to allow or not allow multi-character collating-elements.
 * The current implementation is to ignore any multi-character ce's.
 *
 * NOTE: it is permissable to use NOMATCH (defined as 1) and ERROR
 *       (defined to be 2) as a return here because, even if a string
 *       started at 0, a valid construct is greater than 3 characters
 *       in length.
 */
STATIC const wchar_t *
fn_cclass(pp, c, flag)
register const wchar_t *pp;
register wint_t c;
int flag;
{
	int match, compl, n;
	m_collel_t start, end;
	m_collel_t *rp;
	wchar_t c1;

	c = handlecase(c, flag);
	compl = match = 0;

	/* Fetch first character of the class: Invariant cuz of !, ] */
	c1 = *(wuchar_t *)pp;
	c1 = M_INVARIANT(c1);

	/* [!abc] is complement of abc */
	if (c1 == '!') {	/* '^' also? */
		compl = 1;
		c1 = *(wuchar_t *)++pp;
		c1 = M_INVARIANT(c1);
	}

	/*
	 * []abc] and [-abc] are the only ways to match ] and -
	 * Use *pp, not c1, since we don't want to invariant characters
	 * except for our parsing purposes.
	 */
	if (c1 == ']' || c1 == '-') {
		start = _wctoce(_loaded_coll_, (wuchar_t)*pp);
		match = (*(wuchar_t *)pp++ == c);	/* "[-...]" */
	}

	while (1) switch (c1 = *(wuchar_t *)pp++, c1 = M_INVARIANT(c1)) {
	case 0:	/* error -- no closing ] */
		return (ERROR);

	case ']':	/* Finished parsing [] -- Did it match? */
		return ((match != compl) ? pp : NOMATCH);

	case '-':	/* Range expression */
		if (*pp == 0)
			return (ERROR);	/* ERROR: "[...-" */

		c1 = *(wuchar_t *)pp;
		c1 = M_INVARIANT(c1);

		/* Trailing - matches a - */
		if (c1 == ']') { /* "[...-]" */
			if (c == '-')
				match = 1;
			break;
		}

		/* Was the last char invalid as endpoint of a range? */
		if (start == -1)
			return (ERROR);	/* ERROR: [[=a=]-...] invalid */

		/*
		 * Check endpoint of range for [==][..][::]
		 * Error if syntax problem, or if [==] or [::]
		 */
		if (c1 == '['
		&& (pp[1] == ':' || pp[1] == '=' || pp[1] == '.')) {
			pp = element(c, pp+1, &match, &end);
			if (pp == ERROR || end == -1)
				return (ERROR);
		} else
				/* Normal character as endpoint */
			end = _wctoce(_loaded_coll_, *(wuchar_t *)pp++);
		end = M_INVARIANT(end);

		n = m_collrange(start, end, &rp);
		if (n <= 0)
			return (ERROR);
		while (n-- > 0) {
			m_collel_t c1;
			wchar_t	wch;

			c1 = *rp++;

			/*
			 * multi-character collating elements
			 * don't match range expressions
			 */
			if (m_ismccollel(c1))
				continue;
			if ((wch = _cetowc(_loaded_coll_, c1)) < 0)
				continue;

			if (handlecase(wch, flag) == c) {
				match = 1;
				break;
			}
		}
		break;

	case '[':
		/* Is it [: class :], [= equiv class =], or [. coll-el .] ? */
		if (*pp == ':' || *pp == '=' || *pp == '.') {
			pp = element(c, pp, &match, &start);
			if (pp == ERROR)
				return (ERROR);
			break;
		}
		goto def; /* treat [ as itself */

	default: def:
		if (handlecase((wuchar_t)pp[-1], flag) == c)
			match = 1;
		start = _wctoce(_loaded_coll_, ((wuchar_t *)pp)[-1]);
		break;
	}
	/* NOTREACHED */
}

/*f
 * Check for character match against a [..], [==], or [::]
 * Return end of expression, or ERROR if an error occurred.
 * Set *match if it matched; set *last to possible
 * value for a range.
 *
 * NOTE: it is permissable to use ERROR (defined to be 2) as a return
 *       here because, even if a string started at 0, a valid construct is
 *       greater than 3 characters in length.
 */
STATIC const wchar_t *
element(matchc, p, match, last)
wint_t matchc;
const wchar_t *p;
int *match;
m_collel_t *last;
{
	const wchar_t *cp;
	m_collel_t c1;
	wint_t c;
	char *brack;
	int n;
	wchar_t temp[1024];
	int i;

	c = *p++;	/* Opening bracket type: ., =, or : */
	*last = -1;	/* Default: Invalid as range endpoint */

	/*
	 * Look forward for bracket of matching type.
	 * We can't nest the same type brackets: [.[.ch.]h.] or
	 * similar is never valid: the only reason we do this
	 * at all is for [=[.ch.]=]
	 * Any nesting is meaningless inside [..] or [::]; and
	 * only [..] means anything inside [==].
	 */
	for (cp = p; *cp != '\0'; cp++)
		if (*cp == c && cp[1] == M_UNVARIANT(']'))
			break;
	if (*cp == '\0')
		return (ERROR);		/* ERROR: No matching c] */

	/*
	 * We have now p pointing at start of bracket expression, and cp
	 * pointing at the end.
	 * Copy out the bracket expression, for passing to the various
	 * char oriented routines.
	 */
	n = cp - p;
	for (i=0; i < n; i++)
		temp[i] = p[i];
	temp[n] = '\0';
#ifdef	M_I18N_MB
	if ((brack = malloc(n * MB_LEN_MAX + 1)) == NULL)
		return (ERROR);
	if ((n = wcstombs(brack, temp, n * MB_LEN_MAX)) == -1)
		return (ERROR);
#else
	if ((brack = malloc(n+1)) == NULL)
		return (ERROR);
	memcpy(brack, p, n);
#endif
	brack[n] = '\0';	/* terminate string */
	switch(c) {
	case ':': {	/* [: character-class-name :] */
		wctype_t iswhat;

		iswhat = wctype(brack);
		free(brack);

		if (iswhat == 0)
			return (ERROR);	/* ERROR: [:foo:] unknown class */

		/* Check matchc now for match against whole class */
		*match = (iswctype(matchc, iswhat) != 0);
		break;
	}
	case '.':	/* [. collating-symbol .] */
		c1 = m_strtocoll(brack);
		free(brack);
		if (c1 == -1)
			return (ERROR);	/* ERROR: [.ch.] unknown coll-el */

		/* Can't match mc collel's */
		if (! m_ismccollel(c1) &&
		    (c1 == _wctoce(_loaded_coll_, matchc)))
			*match = 1;
		*last = c1;		/* Range endpoint */
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
		 * Accept only [=ch=].  regex accepts [=[.ch.]=]
		 * Not obvious if other should be accepted.
		 */
		c1 = m_strtocoll(brack);
		free(brack);
		if (c1 == -1)
			return (ERROR);

		/*
		 * Figured out the character or collating element, now
		 * check for match against that whole equiv class.
		 */
		n = m_collequiv(c1, &rp);
		while (n-- > 0) {
			c1 = *rp++;
			if (m_ismccollel(c1))
				continue;
			if (c1 == _wctoce(_loaded_coll_, matchc)) {
				*match = 1;
				break;
			}
		}
	}	break;
	}
	return cp+2;	/* Point after closing ] */
}
