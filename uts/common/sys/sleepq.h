/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ifndef _SYS_SLEEPQ_H
#define	_SYS_SLEEPQ_H

#pragma ident	"@(#)sleepq.h	1.17	94/07/29 SMI"

#include <sys/machlock.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Common definition for a sleep queue,
 * be it an old-style sleep queue, or
 * a constituent of a turnstile.
 */

typedef struct sleepq {
	struct _kthread * sq_first;
} sleepq_t;

/*
 * Definition of the head of a sleep queue hash bucket.
 */
typedef struct _sleepq_head {
	sleepq_t	sq_queue;
	disp_lock_t	sq_lock;
} sleepq_head_t;

#ifdef	_KERNEL

#ifdef	__STDC__

extern void		sleepq_insert(sleepq_t *, struct _kthread *);
extern void		sleepq_wakeone(sleepq_t *);
extern void		sleepq_wakeall(sleepq_t *);
extern void		sleepq_wakeone_chan(sleepq_t *, caddr_t);
extern void		sleepq_wakeall_chan(sleepq_t *, caddr_t);
extern struct _kthread	*sleepq_unsleep(sleepq_t *, struct _kthread *);
extern struct _kthread	*sleepq_dequeue(sleepq_t *, struct _kthread *);

extern sleepq_head_t	*sqhash(caddr_t wchan);

#else

extern void		sleepq_insert();
extern void		sleepq_wakeone();
extern void		sleepq_wakeall();
extern void		sleepq_wakeone_chan();
extern void		sleepq_wakeall_chan();
extern struct _kthread	*sleepq_unsleep();
extern struct _kthread	*sleepq_dequeue();

extern sleepq_head_t	*sqhash();

#endif	/* __STDC__ */

/*
 * The following stuff is in here for compatibility with
 * the old sleep/wakeup interface. It is to be hoped that
 * when all the drivers have been modified to use the new
 * facilities (chiefly condition variables), we we have
 * no further use for this stuff. When that happens, it can
 * go away.
 */

/*
 * sleep-wakeup hashing:  Each entry in sleepq[] points
 * to the front and back of a linked list of sleeping processes.
 * Processes going to sleep go to the back of the appropriate
 * sleep queue and wakeprocs wakes them from front to back (so the
 * first process to go to sleep on a given channel will be the first
 * to run after a wakeup on that channel).
 * NSLEEPQ must be a power of 2.  The function sqhash(x) is used to
 * index into sleepq[] based on the sleep channel.
 */

#define	NSLEEPQ		512
#define	sqhashindex(X)	(((u_int)X >> 2) + ((u_int)(X) >> 9) & (NSLEEPQ - 1))

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif


#endif	/* _SYS_SLEEPQ_H */
