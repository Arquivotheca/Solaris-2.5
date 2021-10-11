/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _SYS_FLOCK_H
#define	_SYS_FLOCK_H

#pragma ident	"@(#)flock.h	1.26	94/12/15 SMI"	/* SVr4.0 11.11	*/

#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/vnode.h>
#include <sys/t_lock.h>		/* for <sys/callb.h> */
#include <sys/callb.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Private declarations and instrumentation for local locking.
 */

/*
 * The command passed to reclock() is made by ORing together one or more of
 * the following values.
 */

#define	INOFLCK		1	/* Vnode is locked when reclock() is called. */
#define	SETFLCK		2	/* Set a file lock. */
#define	SLPFLCK		4	/* Wait if blocked. */
#define	RCMDLCK		8	/* RGETLK/RSETLK/RSETLKW specified */

/*
 * Special pid value that can be passed to cleanlocks().  It means that
 * cleanlocks() should flush all locks for the given sysid, not just the
 * locks owned by a specific process.
 */

#define	IGN_PID		(-1)

/* file locking structure (connected to vnode) */

#define	l_end		l_len

/*
 * The lock manager is allowed to use unsigned offsets and lengths, though
 * regular Unix processes are still required to use signed offsets and
 * lengths.
 */
typedef ulong_t u_off_t;
#define	MAX_U_OFF_T	((u_off_t) ~0)

/*
 * define MAXEND in terms of MAXOFF_T so it will be tied to future changes
 */
#define	MAXEND		((u_off_t) MAXOFF_T)

/*
 * Definitions for accessing the l_pad area of struct flock.  The
 * descriminant of the pad_info_t union is the fcntl command used in
 * conjunction with the flock struct.
 */

typedef struct {
	callb_cpr_t	*(*cb_callback)(void *); /* callback function */
	void		*cb_cbp;	/* ptr to callback data */
} flk_callback_t;

typedef union {
	long	pi_pad[4];		/* (original pad area) */
	int	pi_has_rmt;		/* F_HASREMOTELOCKS */
	flk_callback_t pi_cback;	/* F_RSETLKW */
} pad_info_t;

#define	l_has_rmt(flockp)	(((pad_info_t *)((flockp)->l_pad))->pi_has_rmt)
#define	l_callback(flockp)	\
		(((pad_info_t *)((flockp)->l_pad))->pi_cback.cb_callback)
#define	l_cbp(flockp)	(((pad_info_t *)((flockp)->l_pad))->pi_cback.cb_cbp)


/*
 * The private data structures used by the local locking code are a bit
 * hairy.  The following pictures and notes are to help explain them.
 *
 *                                            queue of locks blocked on lock B:
 *    vnode in                                attachers serve to link together
 *  active_vplist                             filocks, which lack "next
 *  +-----------+   locks held by vnode,      blocked", "prev blocked" ptrs.
 *  |  ......   |   ordered by offset
 *  +-----------+   into vp
 *  | v_filocks |
 *  +-----+-----+   struct filock                   attacher
 *        |        +----------------+              +-----------+
 *        +---> +->|    cv          |  ((2))--> +->| my_filock +---> ((1))
 *              |  +----------------+           |  +-----------+
 *              |  |    set         |           |  | next      +-+
 *  lock A:     |  | (lock info:    |           |  +-----------+ |
 *  no locks    |  | range, etc)    |           |  | prev      | |
 *  blocked on  |  +--+-------------+           |  +-----------+ |
 *  lock        |  |  | granted_flag|           |                |
 *              |  |  |         = 1 |           |       +--------+
 *              |  |  +-------------+           |       |
 *              |  |  |   blk (not  |           |       V
 *              |  |s |  used here) |           |
 *              |  |t +-------------+           |       .
 *              |  |a |blocking_list|           |       . other locks
 *              |  |t |      = NULL |           |       . blocked on
 *              |  |  +-------------+           |       . lock B
 *              |  |  |  my_attacher|           |       .
 *              |  |  | (not used   |           |
 *              |  |  |  here)      |           |
 *              |  +--+-------------+           |
 *              |  | next           +--+        |
 *              |  +----------------+  |        |
 *              |  | prev = NULL    |  |        |
 *              |  +----------------+  |        |
 *              |                      |        |
 *              |                      |        |
 *              |                      |        |
 *             /   +----------------+  |        |
 *  ((B))--------->|    cv          |<-+        |
 *             \   +----------------+           |
 *              |  |    set         |	        |
 *              |  | (lock info:    |	        |
 *              |  | range, etc)    |	        |
 *              |  +--+-------------+           |
 *              |  |  | granted_flag|           |
 *  lock B:     |  |  |         = 1 |           |
 *  has lock    |  |  +-------------+           |
 *  blocked     |  |  |   blk (not  |           |
 *  on it       |  |s |  used here) |           |
 *              |  |t +-------------+           |
 *              |  |a |blocking_list+-----------+
 *              |  |t |             |
 *              |  |  +-------------+
 *              |  |  |  my_attacher|
 *              |  |  | (not used   |
 *              |  |  |  here)      |
 *              |  +--+-------------+
 *              |  | next = NULL    |
 *              |  +----------------+
 *              +--+ prev           |
 *                 +----------------+
 *
 *
 *                actual lock blocked on
 *                lock B; pointed to by
 *                attacher.
 *
 *                  struct filock
 *                 +----------------+
 *  ((1))--------->|    cv          |
 *                 +----------------+
 *                 |    set         |
 *                 | (lock info:    |
 *                 | range, etc)    |
 *                 +--+-------------+
 *                 |  | granted_flag|
 *                 |  |         = 0 |
 *                 |  +-------------+
 *                 |  | blk (lock   +---->((B))
 *                 |s | blocked on) |
 *                 |t +-------------+
 *                 |a |blocking_list|
 *                 |t |(unused here)|
 *                 |  +-------------+
 *                 |  |my_attacher  +---->((2))
 *                 |  |(points back |
 *                 |  |to attacher) |
 *                 +--+-------------+
 *                 | next           +----> to next entry
 *                 |                |      in "sleeplcks"
 *                 |                |      list of blocked
 *                 |                |      locks.
 *                 +----------------+
 *                 | prev           +----> prev entry in
 *                 +----------------+      sleeplcks
 *
 *
 * Notes:
 *
 * 1. vnodes which have locks active on them are entered on the system-global
 *    "active_vplist".  This list is composed of "struct vplist" elements,
 *    which contain pointers to vnodes holding locks, along with
 *    "sleep_start" and "sleep_end" pointers.  These pointers point into the
 *    "sleeplcks" list and identify those locks blocked (sleeping) on the
 *    vnode.
 *
 * 2. There is a system-global list of sleeping (blocked) locks called
 *    "sleeplcks".  This list is ordered by vnode; within the region for a
 *    particular vnode (identified by the "sleep_start" and "sleep_end"
 *    pointers in members of the "active_vplist" list), they are sorted by
 *    lock starting position.
 *
 * 3. Locks which are blocked on an active lock are linked in a queue which
 *    is pointed to by the "blocking_list" field in the "filock" struct.  The
 *    "blocking_list" field is only valid for active locks, as locks may only
 *    block on locks that have been granted.  In other words, blocked locks
 *    queue on the active locks they are blocked on, not on other blocked
 *    locks.
 *
 *    The blocked locks are threaded two ways.  They are threaded on the
 *    blocking_list field using "attachers" structures (see note 4).
 *    They are also threaded on the sleeplcks list using filock's
 *    next/prev pointers (see note 2).
 *
 * 4. "attachers" are data structures which are used to link together blocked
 *    locks.  They are necessary because "struct filock" does not include
 *    enough pointers to allow the locks to be linked to all the other locks
 *    blocked on a lock and also thread them into the "sleeplcks" list.
 *
 * 5. A sleeping lock points to its "blocker" (the lock it is blocked on)
 *    using the "blk" pointer; this pointer is not valid for active (granted)
 *    locks.
 *
 * 6. The "granted_flag" field of "struct filock" is used to indicate whether
 *    or not the requested lock has been granted.  If it is 0, this means
 *    that either the lock is not granted or, if the "filock" is in the list
 *    pointed by the vnode field "v_filocks", that the lock is being
 *    repositioned due to a new lock request (e.g. a preceeding adjacent
 *    lock) and is currently a placeholder.  "granted_flag" may also be set
 *    to FLP_DELAYED_FREE to indicate to the flock code that the "filock" may
 *    be freed after the thread sleeping on it wakes up.
 */

typedef struct attacher {
	struct filock *my_filock;
	struct attacher *next;
	struct attacher *prev;
} attacher_t;

typedef struct filock {
	kcondvar_t cv;
	struct	flock set;	/* contains type, start, and end */
	struct	{
		int granted_flag;	/* granted flag */
		struct filock *blk;	/* for sleeping locks only */
		struct attacher *blocking_list;
		struct attacher *my_attacher;
	}	stat;
	struct	filock *prev;
	struct	filock *next;
} filock_t;

#define	FLP_DELAYED_FREE	-1	/* special value for granted_flag */

/* file and record locking configuration structure */
/* record use total may overflow */
struct flckinfo {
	long reccnt;	/* number of records currently in use */
	long rectot;	/* number of records used since system boot */
};

/* structure that contains list of locks to be granted */

#define	MAX_GRANT_LOCKS		52

typedef struct grant_lock {
	struct filock *grant_lock_list[MAX_GRANT_LOCKS];
	struct grant_lock *next;
} grant_lock_t;


/*
 * The following structure is used to hold a list of locks returned
 * by the F_ACTIVELIST or F_SLEEPINGLIST commands to fs_frlock.
 *
 * N.B. The lists returned by these commands are dynamically
 * allocated and must be freed by the caller.  The vnodes returned
 * in the lists are held and must be released when the caller is done.
 */

typedef struct locklist {
	struct vnode *ll_vp;
	struct flock ll_flock;
	struct locklist *ll_next;
} locklist_t;

/*
 * Provide a way to cleanly enable and disable lock manager locking
 * requests (i.e., requests from remote clients).  FLK_WAKEUP_SLEEPERS
 * forces all blocked lock manager requests to bail out and return ENOLCK.
 * FLK_LOCKMGR_DOWN clears all granted lock manager locks.  Both status
 * codes cause new lock manager requests to fail immediately with ENOLCK.
 */

typedef enum {FLK_LOCKMGR_UP, FLK_WAKEUP_SLEEPERS, FLK_LOCKMGR_DOWN}
    flk_lockmgr_status_t;


#if defined(_KERNEL)
extern struct flckinfo	flckinfo;

int	reclock(struct vnode *, struct flock *, int, int, off_t);
void	kill_proc_locks(struct vnode *, struct flock *);
int	chklock(struct vnode *, int, off_t, int, int);
int	convoff(struct vnode *, struct flock *, int, off_t);
int	cleanlocks(struct vnode *, pid_t, sysid_t);
locklist_t *flk_get_sleeping_locks(long sysid, pid_t pid);
locklist_t *flk_get_active_locks(long sysid, pid_t pid);
int	flk_convert_lock_data(struct vnode *, struct flock *,
		unsigned long *, unsigned long *, long);
int	flk_check_lock_data(u_long, u_long);
int	flk_has_remote_locks(struct vnode *vp);
int	flk_vfs_has_locks(struct vfs *vfsp);
void	flk_set_lockmgr_status(flk_lockmgr_status_t status);
int	flk_sysid_has_locks(long sysid);
#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FLOCK_H */
