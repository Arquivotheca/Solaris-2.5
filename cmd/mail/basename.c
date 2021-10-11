/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)basename.c	1.3	92/07/16 SMI" 	/* SVr4.0 1.3	*/
/*
    NAME
	basename - return base from pathname

    SYNOPSIS
	char *basename(char *path)

    DESCRIPTION
	basename() returns a pointer to the base
	component of a pathname.
*/
#include "mail.h"

char *
basename(path)
	char *path;
{
	char *cp;

	cp = strrchr(path, '/');
	return cp==NULL ? path : cp+1;
}
