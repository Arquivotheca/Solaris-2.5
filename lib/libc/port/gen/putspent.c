/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)putpwent.c	1.1	92/07/23 SMI"

/*	3.0 SID #	1.2	*/
/*LINTLIBRARY*/
/*
 * format a shadow file entry
 *
 * This code used to live in getspent.c
 */
#ifdef __STDC__
	#pragma weak putspent = _putspent
#endif
#include "synonyms.h"
#include <stdio.h>
#include <shadow.h>

int
putspent(p, f)
#ifdef __STDC__
register const struct spwd *p ;
#else
register struct spwd *p ;
#endif
register FILE *f ;
{
	(void) fprintf ( f, "%s:%s:", p->sp_namp,
		p->sp_pwdp ? p->sp_pwdp : "");
		/* pwdp could be null for +/- entries */
	if ( p->sp_lstchg >= 0 )
	   (void) fprintf ( f, "%d:", p->sp_lstchg ) ;
	else
	   (void) fprintf ( f, ":" ) ;
	if ( p->sp_min >= 0 )
	   (void) fprintf ( f, "%d:", p->sp_min ) ;
	else
	   (void) fprintf ( f, ":" ) ;
	if ( p->sp_max >= 0 )
	   (void) fprintf ( f, "%d:", p->sp_max ) ;
	else
	   (void) fprintf ( f, ":" ) ;
	if ( p->sp_warn > 0 )
	   (void) fprintf ( f, "%d:", p->sp_warn ) ;
	else
	   (void) fprintf ( f, ":" ) ;
	if ( p->sp_inact > 0 )
	   (void) fprintf ( f, "%d:", p->sp_inact ) ;
	else
	   (void) fprintf ( f, ":" ) ;
	if ( p->sp_expire > 0 )
	   (void) fprintf ( f, "%d:", p->sp_expire ) ;
	else
	   (void) fprintf ( f, ":" ) ;
	if ( p->sp_flag != 0 )
	   (void) fprintf ( f, "%d\n", p->sp_flag ) ;
	else
	   (void) fprintf ( f, "\n" ) ;

	fflush(f);
	return(ferror(f)) ;
}
