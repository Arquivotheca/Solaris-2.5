/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 *
 * This code is MKS code ported to Solaris with minimum modifications so that
 * upgrades from MKS will readily integrate. Avoid modification if possible!
 */
#ident	"@(#)m_collat.h 1.3	95/05/15 SMI"

/*
 * m_collate.h will declare all the following routines in their header
 * files -- if they conform to the mks extentions.
 * If they don't conform, then we have to define them ourselves.
 *
 * Copyright 1992 by Mortice Kern Systems Inc.  All rights reserved.
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
 * $Header: /u/rd/h/RCS/m_collat.h,v 1.10 1994/02/02 17:48:30 mark Exp $
 */
#ifndef	__M_M_COLLAT_H__
#define	__M_M_COLLAT_H__

#include <m_i18n.h>

/*
 * MKS i18n extentions for character collation support in POSIX.2
 */


/* m_collel_t: a type used to store collation elements
 * 		This must be an unsigned type.
 * M_MAX_COLLEL: maximum allowable value to be used for m_collel_t
 * M_COLL_ERR  : error value returned by collation functions
 */
/*
 * change this to "unsigned short" when we fix the rest of the code
 * to use M_COLL_ERR 
 */
typedef	unsigned m_collel_t;	/* Collating element */

#define __M_COLL_ERR	USHRT_MAX
#define __M_COLL_MAX	M_COLL_ERR-1

#undef	m_ismccollel
#define	m_ismccollel(c)	_m_ismccollel(c)
extern int		m_collequiv (m_collel_t, m_collel_t **);
extern int		m_collrange (m_collel_t,m_collel_t,m_collel_t **);
extern int		m_collorder (m_collel_t **);
extern	int		m_cclass (char *, m_collel_t **);
extern m_collel_t	m_strtocoll (char *);
extern char		*m_colltostr (m_collel_t);
extern m_collel_t	m_getmccoll (const char **);
/* XXX
extern m_collel_t	m_getwmccoll (const wchar_t **);
*/
extern m_collel_t	m_maxcoll (void);

#endif /*__M_M_COLLAT_H__*/
