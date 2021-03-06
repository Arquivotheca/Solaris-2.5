/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 *
 * This code is MKS code ported to Solaris with minimum modifications so that
 * upgrades from MKS will readily integrate. Avoid modification if possible!
 */
#ident	"@(#)m_i18n.h 1.1	94/10/12 SMI"

/*
 * m_i18n.h: Header file dealing with all i18n issues.  #included from mks.h,
 * no program should ever #include any i18n-specific header (i.e. this
 * will decide to include locale.h, nls.h and what not).
 *
 * Copyright 1992, 1993 by Mortice Kern Systems Inc.  All rights reserved.
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
 * $Header: /u/rd/h/RCS/m_i18n.h,v 1.15 1994/02/02 16:01:15 mark Exp $
 */

#ifndef __M_M_I18N_H_
#define __M_M_I18N_H_

#ifndef	M_I18N_M_
/*l
 * Libraries do not have leading m_ prefixes.
 * Thus, we must create #defines which will change all our code from
 * having m_ prefixes, to direct library calls.
 */
#define	m_collel_t	collel_t
#define	m_ismccollel	ismccollel
#define	m_collequiv	collequiv
#define	m_collrange	collrange
#define	m_collorder	collorder
#define	m_cclass	cclass
#define	m_strtocoll	strtocoll
#define	m_colltostr	colltostr

#define	m_localedtconv	localedtconv
#define	m_localeldconv	localeldconv
#define	m_dtconv	dtconv

#endif	/* !M_I18N_M_ */

#define	M_CSETSIZE	(UCHAR_MAX+1)

/*l
 *  Fetch all the data structures.
 *  Even if I18N is off, we need access to the data structures.
 *  Routines defined inside these headers may get changed via #define's
 *  below.
 */
#include <m_nls.h>	/* Our messaging scheme file */
#include <locale.h>	/* Local compiler's locale.h */
#include <m_collate.h>	/* Local compiler's collation: includes m_collel_t */

/*l
 * Define i18n portability routines -- built on top of what we define
 * as the mks extentions.
 */
extern int		m_isyes (char *);

#ifndef	I18N

/*l
 * I18N is not supported -- make most of it disappear
 *
 * If we don't want all the internationalization stuff, then we get rid
 * all the code, and all the data except the lconv structure (if used).
 * This is done if I18N is undefined at compile time.  In this case, #define's
 * are used to convert strcoll into strcmp; setlocale to simply return POSIX,
 * and localeconv to return a pointer to the static lconv structure.
 */

/* messaging */
#undef	m_textstr
#undef	m_msgdup
#undef	m_msgfree
#define	m_textdomain(str)
#define m_textmsg(id, str, cls)		(str)
#define m_textstr(id, str, cls)		str
#define m_strmsg(str)			(str)
#define m_msgdup(m)	(m)
#define m_msgfree(m)

/* locale */
#define	setlocale(class, locale)	((char *)"POSIX")
#define	strcoll				strcmp
#define	localeconv()			(&_m_lconv)
#undef	m_localedtconv
#define	m_localedtconv()		(&_m_dtconv)
#undef	m_localeldconv
#define	m_localeldconv()		(&_m_locdef)
#undef	m_colltostr
#define	m_colltostr(c)			(NULL)

extern struct lconv		_m_lconv;
extern struct m_dtconv		_m_dtconv;
extern struct _m_localedef	_m_locdef;

#endif /*I18N*/

#endif /*__M_M_I18N_H_*/
