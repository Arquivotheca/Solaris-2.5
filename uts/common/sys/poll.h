/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_POLL_H
#define	_SYS_POLL_H

#pragma ident	"@(#)poll.h	1.19	94/08/31 SMI"	/* SVr4.0 11.9 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Structure of file descriptor/event pairs supplied in
 * the poll arrays.
 */
typedef struct pollfd {
	int fd;				/* file desc to poll */
	short events;			/* events of interest on fd */
	short revents;			/* events that occurred on fd */
} pollfd_t;

/*
 * Testable select events
 */
#define	POLLIN		0x0001		/* fd is readable */
#define	POLLPRI		0x0002		/* high priority info at fd */
#define	POLLOUT		0x0004		/* fd is writeable (won't block) */
#define	POLLRDNORM	0x0040		/* normal data is readable */
#define	POLLWRNORM	POLLOUT
#define	POLLRDBAND	0x0080		/* out-of-band data is readable */
#define	POLLWRBAND	0x0100		/* out-of-band data is writeable */

#define	POLLNORM	POLLRDNORM

/*
 * Non-testable poll events (may not be specified in events field,
 * but may be returned in revents field).
 */
#define	POLLERR		0x0008		/* fd has error condition */
#define	POLLHUP		0x0010		/* fd has been hung up on */
#define	POLLNVAL	0x0020		/* invalid pollfd entry */

#if defined(_KERNEL) || defined(_KMEMUSER)

#include <sys/t_lock.h>
#include <sys/thread.h>

/*
 * Poll list head structure.  A pointer to this is passed to
 * pollwakeup() from the caller indicating an event has occurred.
 * Only the ph_list field is used, but for DDI compliance, we can't
 * change the size of the structure.
 */
typedef struct pollhead {
	struct polldat	*ph_list;	/* list of pollers */
	struct polldat	*ph_dummy;	/* unused -- see above */
	short		ph_events;	/* unused -- see above */
} pollhead_t;

/*
 * Data necessary to notify process sleeping in poll(2)
 * when an event has occurred.
 */
typedef struct polldat {
	kthread_t	*pd_thread;	/* thread doing poll */
	int		pd_events;	/* events being polled */
	struct polldat	*pd_next;	/* next in poll list */
	struct polldat	*pd_prev;	/* previous in poll list */
	struct pollhead	*pd_headp;	/* backpointer to head of list */
	struct pollhead	*pd_sphp;	/* stored pollhead struct pointer */
} polldat_t;

/*
 * State information kept by each polling thread
 */
typedef struct pollstate {
	int		ps_nfds;
	int		ps_flag;
	pollfd_t	*ps_pollfd;
	polldat_t	*ps_polldat;
	kmutex_t	ps_lock;	/* mutex for the polling thread */
	kmutex_t	ps_no_exit;	/* protects ps_busy*, can't be nested */
	int		ps_busy;	/* can only exit when its 0 */
	kcondvar_t	ps_busy_cv;	/* cv to wait on if ps_busy != 0 */
	kcondvar_t	ps_cv;		/* cv to wait on if needed */
} pollstate_t;

/* ps_flag */
#define	T_POLLTIME	0x01	/* poll timeout pending */
#define	T_POLLWAKE	0x02	/* pollwakeup() occurred */

#if defined(_KERNEL)

/*
 * Routine called to notify a process of the occurrence
 * of an event.
 */
extern void pollwakeup(pollhead_t *, short);
/*
 * pollwakeup_safe will replace pollwakeup when the support
 * for unsafe drivers is removed.
 */
extern void pollwakeup_safe(pollhead_t *, short);

/*
 * Internal routines.
 */
extern void polltime(kthread_t *);
extern void pollrun(kthread_t *);
extern void polldel(pollstate_t *);

extern void pplock_init(void);
extern void pplock(pollhead_t *);
extern void ppunlock(pollhead_t *);
extern int  pplockowned(pollhead_t *);
extern void ppwaitemptylist(pollhead_t *);

#endif /* defined(_KERNEL) */

#endif /* defined(_KERNEL) || defined(_KMEMUSER) */

#if defined(__STDC__) && !defined(_KERNEL)
int poll(struct pollfd *, unsigned long, int);
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_POLL_H */
