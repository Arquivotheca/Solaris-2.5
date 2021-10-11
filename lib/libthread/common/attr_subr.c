/*	Copyright (c) 1993, by Sun Microsystems, Inc.	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma " @(#)attr_subr.c 1.4 94/09/14 "

#include <sys/types.h>
#include "libpthr.h"

/*
 * To allocate space for attribute objects, currently we use
 * malloc only. In future we may implement a better solution.
 */
caddr_t
_alloc_attr(int size)
{
	return ((caddr_t) malloc(size));
}

/*
 * To free the attribute object space. Currently we use free()
 * but in future we may have better solution.
 */
int
_free_attr(caddr_t attr)
{
	return (free(attr));
}
