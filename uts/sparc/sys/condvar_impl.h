/*
 *	Copyright (c) 1991,1993 Sun Microsystems, Inc.
 */

/*
 * condvar_impl.h:
 *
 * platform-specific definitions for thread synchronization
 * primitives: condition variables
 */

#ifndef _SYS_CONDVAR_IMPL_H
#define	_SYS_CONDVAR_IMPL_H

#pragma ident	"@(#)condvar_impl.h	1.1	93/10/13 SMI"

#ifndef	_ASM
#include <sys/types.h>
#include <sys/thread.h>
#endif	/* _ASM */

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef	_ASM

/*
 * Condtion variables.
 */

typedef struct _condvar_impl {
	ushort_t	cv_waiters;
} condvar_impl_t;

#endif	/* _ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CONDVAR_IMPL_H */
