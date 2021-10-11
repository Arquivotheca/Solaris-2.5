/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)uid.c	1.5	92/07/14 SMI"       /* SVr4.0 1.5 */

#include	<sys/types.h>
#include	<stdio.h>
#include	<userdefs.h>

#include	<sys/param.h>
#ifndef	MAXUID
#include	<limits.h>
#ifdef UID_MAX
#define	MAXUID	UID_MAX
#else 
#define	MAXUID	60000
#endif
#endif

extern pid_t getpid();

static char cmdbuf[ 128 ];

uid_t
findnextuid()
{
	FILE *fptr;
	uid_t last, next;

	/*
		Sort the used UIDs in decreasing order to return
		MAXUSED + 1
	*/

	if ( (fptr=popen(
			 "exec sh -c \"getent passwd|cut -f3 -d:|sort -nr|uniq\" 2>/dev/null",
			 "r")) == NULL)
		return( -1 );

	if( fscanf( fptr, "%ld\n", &next ) == EOF ) {
		pclose( fptr);
		return( DEFRID + 1 );
	}

	/* Still some UIDs left between next and MAXUID */
	if( next < MAXUID ) {
		pclose(fptr);
		return( next <= DEFRID? DEFRID + 1: next + 1 );
	}

	/* Look for the next unused one */
	for( last = next; fscanf( fptr, "%ld\n", &next ) != EOF; last = next ) {
		if( next <= DEFRID ) {
			pclose(fptr);
			if( last != DEFRID + 1 )
				return( DEFRID + 1 );
			else
				return( -1 );
		}
		if( (last != (next + 1)) && ((next + 1) < MAXUID) ) {
			pclose(fptr);
			return( next + 1 );
		}
	}

	pclose(fptr);
	/* None left */
	return( -1 );
}
