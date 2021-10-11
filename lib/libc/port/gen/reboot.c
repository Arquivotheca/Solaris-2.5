/*
 * Copyright (c) 1995 (c), by Sun Microsystems, Inc.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/* 	Portions Copyright(c) 1988, Sun Microsystems Inc.	*/
/*	All Rights Reserved					*/

#pragma	ident	"@(#)reboot.c	1.2	95/03/02 SMI"	/* SVr4.0 1.2	*/

/*LINTLIBRARY*/
#include "synonyms.h"

#include <sys/uadmin.h>
#include <sys/reboot.h>
#include <errno.h>

/*
 * Not all BSD's semantics are supported
 * including RB_SINGLE, RB_RB_DUMP, RB_STRING
 */

/*ARGSUSED*/
int
reboot(int howto, char *bootargs)
{
	int fcn;

	if (howto & RB_HALT)
		fcn = AD_HALT;
	else if (howto & RB_ASKNAME)
		fcn = AD_IBOOT;
	else			/* assume RB_AUTOBOOT */
		fcn = AD_BOOT;

	return (uadmin(A_SHUTDOWN, fcn, (int)bootargs));
}       
