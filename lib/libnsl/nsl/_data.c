/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)_data.c	1.11	94/01/27 SMI"	/* SVr4.0 1.3	*/

#include <thread.h>
#include <synch.h>
#include <sys/types.h>
#include <sys/timod.h>
#include <stdio.h>

/*
 * This must be here to preserve compatibility
 */
struct _oldti_user _old_ti = { 0, 0, NULL, 0, NULL, NULL, NULL, 0, 0, 0, 0, 0 };
