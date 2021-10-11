/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/* Copyright (c) 1991, 1992 Sun Microsystems, Inc. */

#ident	"@(#)getspent.c	1.15	92/10/01 SMI"	/* SVr4.0 1.4	*/

/*LINTLIBRARY*/
#ifdef __STDC__
	#pragma weak getspent	= _getspent
	#pragma weak getspnam	= _getspnam
	#pragma weak fgetspent	= _fgetspent
	/* putspent() has been moved to putspent.c */
#endif
#include "synonyms.h"
#include "shlib.h"
#include <shadow.h>
#include <nss_dbdefs.h>
#include <stdio.h>

#ifdef	NSS_INCLUDE_UNSAFE

/*
 * Ye olde non-reentrant interface (MT-unsafe, caveat utor)
 */

/*
 * Don't free this, even on an endspent(), because bitter experience shows
 *   that there's production code that does getXXXbyYYY(), then endXXXent(),
 *   and then continues to use the pointer it got back.
 */
static nss_XbyY_buf_t *buffer;
#define GETBUF()	\
	NSS_XbyY_ALLOC(&buffer, sizeof (struct spwd), NSS_BUFLEN_SHADOW)
	/* === ?? set ENOMEM on failure?  */

struct spwd *
getspnam(nam)
	const char	*nam;
{
	nss_XbyY_buf_t	*b = GETBUF();

	return (b == 0 ? 0 : getspnam_r(nam ,b->result, b->buffer, b->buflen));
}

struct spwd *
getspent()
{
	nss_XbyY_buf_t	*b = GETBUF();

	return (b == 0 ? 0 : getspent_r(b->result, b->buffer, b->buflen));
}

struct spwd *
fgetspent(f)
	FILE		*f;
{
	nss_XbyY_buf_t	*b = GETBUF();

	return (b == 0 ? 0 : fgetspent_r(f, b->result, b->buffer, b->buflen));
}

#endif	NSS_INCLUDE_UNSAFE
