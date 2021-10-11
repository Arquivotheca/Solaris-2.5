/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 *
 * This code is MKS code ported to Solaris with minimum modifications so that
 * upgrades from MKS will readily integrate. Avoid modification if possible!
 */
#ident	"@(#)regexec.c 1.9	95/05/18 SMI"

/*
 * Regexec driver for multibyte enablement.
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
 * Written by Alex White.
 */
#ifdef M_RCSID
#ifndef lint
static char rcsID[] = "$Header: /u/rd/src/libc/regex/rcs/regexec.c 1.11 1995/02/07 18:41:42 jeffhe Exp $";
#endif
#endif

#include "mks.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <regex.h>
#include "reg.h"
#include <errno.h>

static int
_regwexec(const regex_t *, const wchar_t *, size_t, regwmatch_t *, int);

/* The maximum size of the back-track stack used by regexec */
#ifdef  M_SMALLSTACK
#define REG_BTSIZE      256     /* 1K in small, 2K in large model */
#else
#ifdef M_LDATA
#define REG_BTSIZE      4096    /* 32K on 32-bit machine */
#else
#define REG_BTSIZE      1024    /* 4K on 16-bit machine */
#endif
#endif

static	node return0 = {RETURN};	/* RETURN 0 node */

#define	istoken(c) (isalnum(c)  ||  (c) == '_')

#ifndef M_I18N_MB

/*
 * System will not support any form of multibyte.
 * Simply compile the normal regexec
 */
#define	UCHARTYPE	uchar
#define	CHARTYPE	char
#define	STRCHR(s, c)	(uchar *)strchr((char *)s, c)
#define	FETCH(c, p)	c = *(p)
#define	REGMATCH	regmatch_t
#define	MCCOLL		m_getmccoll
#define rcsID           rcsID1
#include "regx.x"
#undef	MCCOLL
#undef	UCHARTYPE
#undef	CHARTYPE
#undef	STRCHR
#undef	FETCH
#undef	REGMATCH

#else	/* ! M_I18N_MB */

#define	CHARTYPE	char	/* ZZZ */
/*f
 * Given # of *characters* into a multibyte string, return the *byte*
 * offset to that point.  Return also the shift state at that point.
 */
STATIC int
mboff(astring, off, state)
const char *astring;
int off;
mbstate_t *state;
{
	int c, n;
	const char *s;
	wchar_t wc;

	c = 0;	/* Character # */
	s = astring;
	*state = 0;
	while (c++ < off) {
		if ((n = mbrtowc(&wc, s, INT_MAX, state)) == 0)
			break;
		if (n == -1)
			n = 1;
		s += n;
	}
	return s-astring;
}

/*
 * System supports multibyte.
 * Compile regexec(), which will fork out to either regsbexec(), or regwexec().
 * User may decide to directly call regwexec().
 */
extern void * _loaded_coll_;
int
regexec(r, astring, nsub, sub, flags)
const regex_t *r;			/* compiled RE */
const char *astring;			/* subject string */
size_t nsub;				/* number of subexpressions */
regmatch_t *sub;			/* subexpression pointers */
int flags;
{
	if (_loaded_coll_ != NULL) {
		wchar_t *wcs;
		wchar_t	*reg_m_mbstowcsdup ANSI((const char *s));
		int i;

		/*
		 * This is all rather inefficient, but probably the cleanest
		 * way to do this.
		 */

		/* In mb locale, just convert to wide, and call wide routine */
		if ((wcs = reg_m_mbstowcsdup(astring)) == NULL)
			return errno == EILSEQ ? REG_ECHAR : REG_ESPACE;
		i = _regwexec(r, wcs, nsub, (regwmatch_t *)sub, flags);

		/* Now, adjust the pointers/counts/states in sub */
		if (i == REG_OK && (r->re_flags&REG_NOSUB) == 0) {
			int j, k;

			for (j = 0; j < nsub; j++) {
				regmatch_t *s = &sub[j];

				if ((k = s->rm_so) >= 0) {
#ifdef	M_I18N_LOCKING_SHIFT
					s->rm_so = mboff(astring, k, (mbstate_t *) &s->rm_ss);
#else
					mbstate_t st;
					s->rm_so = mboff(astring, k, &st);
#endif
					s->rm_sp = astring + s->rm_so;
				}
				if ((k = s->rm_eo) >= 0) {
#ifdef	M_I18N_LOCKING_SHIFT
					s->rm_eo = mboff(astring, k, (mbstate_t *) &s->rm_es);
#else
					mbstate_t st;
					s->rm_eo = mboff(astring, k, &st);
#endif
					s->rm_ep = astring + s->rm_eo;
				}
			}
		}
		free(wcs);
		return i;
	} else {
		/* Note: No external callers of regsbexec() */

		extern int regsbexec(const regex_t *, const char *, size_t,
			regmatch_t *, int);

		return regsbexec(r, (const char *)astring, nsub, 
			sub, flags);
	}
}

int
regwexec(r, astring, nsub, sub, flags)
const regex_t *r;			/* compiled RE */
const wchar_t *astring;			/* subject string */
size_t nsub;				/* number of subexpressions */
regwmatch_t *sub;			/* subexpression pointers */
int flags;
{
	/*
	 * If we don't have a collation table loaded then we have
	 * to scan the string passed in for wide chars with values over
	 * 255, and convert them back into values in the range 0-255.
	 */

	if (_loaded_coll_ == NULL) {
		wchar_t *wcs;
		wchar_t	*wcharoptdup ANSI((const wchar_t *));
		int i;

		/*
		 * Duplicate the string and convert nasty wchar's
		 */
		if ((wcs = wcharoptdup(astring)) == NULL)
			return errno == EILSEQ ? REG_ECHAR : REG_ESPACE;
		i = _regwexec(r, wcs, nsub, (regwmatch_t *)sub, flags);

		/* Now, adjust the pointers/counts/states in sub */
		if (i == REG_OK && (r->re_flags&REG_NOSUB) == 0) {
			int j;

			for (j = 0; j < nsub; j++) {
				regwmatch_t *s = &sub[j];

				if (s->rm_so >= 0) {
					s->rm_sp = astring + s->rm_so;
				}
				if (s->rm_eo >= 0) {
					s->rm_ep = astring + s->rm_eo;
				}
			}
		}
		free(wcs);
		return i;
	} else {
		return(_regwexec(r, astring, nsub, sub, flags));
	}
}

static wchar_t *
wcharoptdup(const wchar_t * ws)
{
	int n;
	wchar_t *w;
	n = wslen(ws) + 1;
	if ((w = (wchar_t *)malloc(n * sizeof(wchar_t))) == NULL)
		return(NULL);
	_wchar_opt(w, ws);	
	return (w);
}


/*
 * Generate a thin version of regexec
 */
#define	regexec 	regsbexec
#define	UCHARTYPE	uchar
/*	#define	CHARTYPE	char	ZZZ */
#define	STRCHR(s, c)	(uchar *)strchr((char *)s, c)
#define	FETCH(c, p)	c = *(p)
#define	REGMATCH	regmatch_t
#define	MCCOLL		m_getmccoll
#define rcsID           rcsID1
#include "regx.x"
#undef  rcsID
#undef	MCCOLL
#undef	regexec
#undef	UCHARTYPE
#undef	CHARTYPE
#undef	STRCHR
#undef	FETCH
#undef	REGMATCH

/*
 * Generate a wide version of regexec
 */
#define	MB		1
#define	regexec		_regwexec
#define	CHARTYPE	wchar_t
#define	UCHARTYPE	wchar_t
#define	STRCHR(s, c)	(wchar_t *) wcschr(s, c)		/* ZZZ */
#define	FETCH(c, p)	c = *(p)
#define	REGMATCH	regwmatch_t
#define	MCCOLL		m_getwmccoll
#define rcsID           rcsID2
#include "regx.x"
#undef  rcsID
#undef	MCCOLL
#undef	regexec
#undef	CHARTYPE
#undef	STRCHR
#undef	FETCH
#undef	REGMATCH

#endif	/* ! M_I18N_MB */
