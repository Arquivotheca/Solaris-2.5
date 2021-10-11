/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _LOCALE_H
#define	_LOCALE_H

#pragma ident	"@(#)locale.h	1.13	93/07/09 SMI"	/* SVr4.0 1.8	*/

/*
 * NOTE: Inclusion of <libintl.h> here is for backward compatibility with the
 * the previous release that defined dgettext() et al in <locale.h>.  The
 * users of dgettext() et al should include <libintl.h> directly.  Inclusion
 * of <libintl.h> by <locale.h> may not be supported in a future relase.
 *
 * Note: one reads "__STDC__ - 0 == 0" as "not ANSI-C conformant".
 */

#if defined(__EXTENSIONS__) || __STDC__ - 0 == 0
#include <libintl.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

struct	lconv 	{
	char *decimal_point;
	char *thousands_sep;
	char *grouping;
	char *int_curr_symbol;
	char *currency_symbol;
	char *mon_decimal_point;
	char *mon_thousands_sep;
	char *mon_grouping;
	char *positive_sign;
	char *negative_sign;
	char int_frac_digits;
	char frac_digits;
	char p_cs_precedes;
	char p_sep_by_space;
	char n_cs_precedes;
	char n_sep_by_space;
	char p_sign_posn;
	char n_sign_posn;
};

#define	LC_CTYPE	0
#define	LC_NUMERIC	1
#define	LC_TIME		2
#define	LC_COLLATE	3
#define	LC_MONETARY	4
#define	LC_MESSAGES	5
#define	LC_ALL		6

#ifndef NULL
#define	NULL	0
#endif

#if defined(__STDC__)
extern char *setlocale(int, const char *);
extern struct lconv *localeconv(void);
#else
extern char *setlocale();
extern struct lconv *localeconv();
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _LOCALE_H */
