/*	Copyright (c) 1993 by Sun Microsystems, Inc.		*/
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/


#ifndef _SYS_SYNC32_H
#define	_SYS_SYNC32_H

#pragma ident	"@(#)synch32.h	1.10	95/08/08 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/* special defines for LWP mutexes */
#define	mutex_type	flags.type
#define	mutex_lockw	lock.lock64.pad[7]
#define	mutex_waiters	lock.lock64.pad[6]

#ifdef i386 /* ideally, this should be "if platform has the swap instruction" */
/* use this to generate offsets for _mutex_unlock_asm() for i386 */
#define mutex_lockword  lock.lock64.pad[4]  /* address of word containing lk */
#endif

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
