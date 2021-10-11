/*
 * Copyright (c) 1987 by Sun Microsystems, Inc.
 */

#ifndef _SYS_SYNC32_H
#define	_SYS_SYNC32_H

#pragma ident	"@(#)synch32.hM 1.2	93/02/03 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/* special defines for LWP mutexes */
#define	mutex_type	flags.type
#define	mutex_lockw	lock.lock64.pad[7]
#define	mutex_waiters	lock.lock64.pad[6]

/* special defines for LWP condition variables */
#define	cond_type	flags.type
#define	cond_waiters	flags.flag[3]

/* special defines for LWP semaphores */
#define	sema_count	count
#define	sema_type	type
#define	sema_waiters	flags[7]

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_SYNC32_H */
