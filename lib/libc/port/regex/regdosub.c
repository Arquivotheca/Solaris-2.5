/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 *
 * This code is MKS code ported to Solaris with minimum modifications so that
 * upgrades from MKS will readily integrate. Avoid modification if possible!
 */
#ident	"@(#)regdosub.c 1.2	95/03/08 SMI"

/*
 * Regdosub driver for multibyte enablement.
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
static char rcsID[] = "$Header: /u/rd/src/libc/regex/RCS/regdosub.c,v 1.28 1993/07/09 12:17:01 rog Exp $";
#endif
#endif

#include "mks.h"
#include <stdlib.h>
#include <string.h>
#include <regex.h>

#define	NSUB	10

#ifndef M_I18N_MB

/*
 * System will not support any form of multibyte.
 * Simply compile the normal regdosub/regdosuba
 */
#define	CHARTYPE	char
#define	REGMATCH	regmatch_t
#define	REGEXEC		regexec
#define	STRLEN		strlen
#include "regsub.x"
#undef	CHARTYPE
#undef	REGMATCH
#undef	REGEXEC

#else	/* ! M_I18N_MB */

#ifdef	EXPAND
#define	regname		regdosuba
#define	regwname	regwdosuba
#define	regsbname	regsbdosuba
#define	dest		dstp
#define	destarg		char **
#else
#define	regname		regdosub
#define	regwname	regwdosub
#define	regsbname	regsbdosub
#define	dest		dst
#define	destarg		char *
#endif

wchar_t *m_mbstowcsdup (const char *s);
extern char   *m_wcstombsdup (const wchar_t *w);
/*
 * System supports multibyte.
 * Compile regdosub(), which will fork out to either regsbdosub(),
 * or regwdosub().
 * User may decide to directly call regwdosub().
 */
int
regname(rp, rpl, src, dest, len, globp)
register regex_t *rp;	/* compiled RE */
const char *rpl;	/* replacement string */
const char *src;	/* source string */
destarg dest;		/* destination string */
int len;		/* destination length */
int *globp;             /* IN: occurence, 0 for all; OUT: substitutions */
{
	/* MB_BEGIN		XXX Force Wide character at all times */
		wchar_t *wsrc, *wrpl, *wdst;
		int i;

		wsrc = m_mbstowcsdup(src);
		wrpl = m_mbstowcsdup(rpl);

#ifdef	EXPAND
		i = (int) regwname(rp, wrpl, wsrc, &wdst, len, globp);
		*dstp = m_wcstombsdup(wdst);
		free(wdst);
#else
	{
		wchar_t wdst[1000];
		
/***TODO***/
		i = regwname(rp, wrpl, wsrc, wdst, len, globp);
		wcstombs(dest, wdst, len);
	}
#endif
		free(wsrc);
		free(wrpl);
		return i;
#ifdef XXX			/* XXX Force wide char version */
	MB_ELSE
		/* Note: No external callers of regsbexec() */
		extern int regsbname(regex_t *, const char *, const char *,
			destarg, int, int *);
		return regsbname(rp, rpl, src, dest, len, globp);
	MB_END
#endif
}

/*
 * Generate a thin version of regdosub
 */
#define	regdosub	regsbname
#define	regdosuba	regsbname
#define	CHARTYPE	char
#define	REGMATCH	regmatch_t
#define	REGEXEC		regexec
#define	STRLEN		strlen
#include "regsub.x"
#undef	CHARTYPE
#undef	REGMATCH
#undef	REGEXEC
#undef	STRLEN
#undef	regdosub
#undef	regdosuba

/*
 * Generate a wide version of regdosub
 */
#define	MB		1
#define	regdosub	regwname
#define	regdosuba	regwname
#define	CHARTYPE	wchar_t
#define	REGMATCH	regwmatch_t
#define	REGEXEC		regwexec
#define	STRLEN		wcslen
#include "regsub.x"
#undef	CHARTYPE
#undef	REGMATCH
#undef	REGEXEC
#undef	STRLEN
#undef	regdosub
#undef	regdosuba

#endif	/* ! M_I18N_MB */
