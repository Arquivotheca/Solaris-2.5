/*
 * Copyright (c) 1991-1993, Sun Microsystems, Inc.
 */

#ifndef	_SYS_MEMLIST_H
#define	_SYS_MEMLIST_H

#pragma ident	"@(#)memlist.h	1.1	94/06/10 SMI" /* SunOS-4.0 1.7 */

/*
 * Common memlist format, exported by boot.
 */

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Boot programs of version 4 and greater pass a linked list
 * of struct memlist to the kernel.
 */
struct memlist {
	u_longlong_t	address;	/* starting address of memory segment */
	u_longlong_t	size;		/* size of same */
	struct memlist	*next;		/* link to next list element */
	struct memlist	*prev;		/* link to previous list element */
};

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_MEMLIST_H */
