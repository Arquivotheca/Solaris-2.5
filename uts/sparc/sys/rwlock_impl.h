/*
 *	Copyright (c) 1991,1993 Sun Microsystems, Inc.
 */

/*
 * rwlock_impl.h:
 *
 * Implementation specific definitions for thread synchronization
 * primitives: readers/writer locks.
 */

#ifndef _SYS_RWLOCK_IMPL_H
#define	_SYS_RWLOCK_IMPL_H

#pragma ident	"@(#)rwlock_impl.h	1.1	93/10/13 SMI"

#ifndef	_ASM
#include <sys/machlock.h>
#include <sys/dki_lkinfo.h>
#include <sys/sleepq.h>
#include <sys/turnstile.h>
#endif	/* _ASM */

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef	_ASM

typedef struct _rwlock_impl {
	unsigned char 	type;
	disp_lock_t	rw_wlock;
	turnstile_id_t	waiters;	/* cookie for finding the */
					/* turnstile for this rwlock */
	union {
		struct rwlock_stats *sp;
		struct {
			short	writewanted;	/* num of blocked write reqs */
			short	holdcnt;	/* rlock > 0, wlock == -1 */
		} rw;
	} un;
	/*
	 * The rwlock owner is needed to implement
	 * the limited priority inheritance that we
	 * provide for readers-writers locks.
	 */
	struct _kthread *owner;
} rwlock_impl_t;

/*
 * Statistics package for rwlocks.
 */
struct rwlock_stats {
	rwlock_impl_t	real;			/* real r/w lock */
	lkinfo_t	lkinfo;
	lkstat_t	*lkstat;		/* statistics */
	char		name[LOCK_NAME_LEN+1];	/* name and ending NULL */
	char		flag;			/* flag */
};


/*
 * flags in rwlock_stats.flag
 */
#define	RWSTAT_STARTUP_ALLOC	1	/* allocated from startup pool */


#endif	/* _ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_RWLOCK_IMPL_H */
