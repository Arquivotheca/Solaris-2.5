/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)gethostid.c	1.3	95/03/02 SMI"	/* SVr4.0 1.3	*/

/*******************************************************************

		PROPRIETARY NOTICE (Combined)

This source code is unpublished proprietary information
constituting, or derived under license from AT&T's UNIX(r) System V.
In addition, portions of such source code were derived from Berkeley
4.3 BSD under license from the Regents of the University of
California.



		Copyright Notice 

Notice of copyright on this source code product does not indicate 
publication.

	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
	          All rights reserved.
********************************************************************/ 

/*LINTLIBRARY*/
#include "synonyms.h"

#include <sys/types.h>
#include <sys/systeminfo.h>
#include <stdlib.h>
#include <unistd.h>

#define	HOSTIDLEN	40

long
gethostid(void)
{
	char	name[HOSTIDLEN+1], *end;
	unsigned long	hostid;

	if (sysinfo(SI_HW_SERIAL, name, HOSTIDLEN) == -1)
		return (-1);
	hostid = strtoul(name, &end, 10);
	if (end == name)
		return (-1);
	return ((long)hostid);
}
