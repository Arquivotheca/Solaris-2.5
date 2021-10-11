/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#ifndef	_DLLIB_DOT_H
#define	_DLLIB_DOT_H

#pragma ident	"@(#)dllib.h	1.9	93/02/25 SMI"

#include "_rtld.h"

/*
 * Information for dlopen(), dlsym(), and dlclose() on libraries linked by rtld.
 * Each shared object referred to in a dlopen call has an associated dl_obj
 * structure.  For each such structure there is a list of the shared objects
 * on which the referenced shared object is dependent.
 */
#define	DL_MAGIC	0x580331
#define	DL_CIGAM	0x830504

typedef struct dl_obj {
	long		dl_magic;	/* DL_MAGIC */
	List		dl_lmps;	/* list of dependent libraries */
	long		dl_refcnt;	/* count of dlopen invocations */
	Permit *	dl_id;		/* id for this dlopen invocation */
	long		dl_cigam;	/* DL_CIGAM */
} Dl_obj;

#endif
