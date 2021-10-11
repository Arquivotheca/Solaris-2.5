/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 *
 * This code is MKS code ported to Solaris with minimum modifications so that
 * upgrades from MKS will readily integrate. Avoid modification if possible!
 */
#ident	"@(#)m_nls.h 1.1	94/10/12 SMI"

/*
 * m_nls.h: mks NLS (National Language Support) header file
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
 * rcsid = $Header: /u/rd/h/sol2/RCS/m_nls.h,v 1.2 1993/12/03 22:12:11 mark Exp $
 */

#ifndef	__M_NLS_H__
#define	__M_NLS_H__

/* 
 * RA: Phase 1 - don't need message catalogues!
 *           and we probably don't want catopen()/catgets() scheme anyway.
 *           SUN probably wants gettext() at some point.
 *     For now, disable all the message catlogue code
 */
/* disable messaging */
#undef	m_textstr
#undef	m_msgdup
#undef	m_msgfree
#define	m_textdomain(str)
/*
#define m_textmsg(id, str, cls)		(str)
#define m_textstr(id, str, cls)		str
#define m_strmsg(str)			(str)
*/
#define m_msgdup(m)			(m)
#define m_msgfree(m)
/*
 * XXX Solaris porting hack
 */
#include "_libc_gettext.h"      /* Solaris Porting hack */
#define m_textmsg(id, str, cls)		_libc_gettext(str)
#define m_textstr(id, str, cls)		_libc_gettext(str)
#define m_strmsg(str)			_libc_gettext(str)


#endif/*__M_NLS_H__*/
