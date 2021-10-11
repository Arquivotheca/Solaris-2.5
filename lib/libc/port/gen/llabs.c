/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ident	"@(#)llabs.c	1.1	91/08/19 SMI"

/*LINTLIBRARY*/

#ifdef __STDC__
#pragma weak llabs = _llabs
#endif

#include "synonyms.h"
#include <stdlib.h>

long long
llabs(arg)
register long long arg;
{
	return (arg >= 0 ? arg : -arg);
}
