/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_CALLO_H
#define	_SYS_CALLO_H

#pragma ident	"@(#)callo.h	1.17	95/02/09 SMI"	/* SVr4.0 11.3	*/

#include <sys/t_lock.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The callout mechanism provides general-purpose event scheduling:
 * an arbitrary function is called in a specified amount of time.
 */
typedef struct callout {
	struct callout	*c_next;	/* next in bucket, or on freelist */
	int		c_xid;		/* extended callout ID; see below */
	long		c_runtime;	/* absolute run time */
	void		(*c_func)();	/* function to call */
	caddr_t		c_arg;		/* argument to function */
	kthread_id_t	c_executor;	/* thread executing callout */
	kcondvar_t	c_done;		/* signal callout completion */
} callout_t;

/*
 * The extended callout ID consists of the callout ID (as returned by
 * timeout()), a bit indicating whether the callout is executing,
 * and a bit indicating whether unsafe_driver was held when the callout
 * was created.  (This will go away when unsafe driver support goes away.)
 *
 * The callout ID uniquely identifies a callout.  It contains a table ID,
 * which identifies the appropriate callout table (normal or realtime), a
 * bit indicating whether this is a short-term or long-term callout, and
 * the bucket ID, all described below.
 *
 * We use two distinct bits to distinguish normal vs. realtime callouts,
 * instead of just one, because this guarantees a non-zero callout ID, thus
 * eliminating the need for an explicit wrap-around test during ID generation.
 *
 * The long-term bit exists to address the problem of callout ID collision.
 * This is an issue because the system typically generates a large number of
 * timeout() requests, which means that callout IDs eventually get recycled.
 * Most timeouts are very short-lived, so that ID recycling isn't a problem;
 * but there are a handful of timeouts which are sufficiently long-lived to
 * see their own IDs reused.  We use the long-term bit to partition the
 * ID namespace into pieces; the short-term space gets all the heavy traffic
 * and can wrap frequently (i.e., on the order of a day) with no ill effects;
 * the long-term space gets very little traffic and thus never wraps.
 *
 * The bucket ID is the rest of the callout ID; it is so named because the
 * low-order bits are the bucket number (i.e., which bucket the callout is
 * stored in).  The high-order bits are just a free-running counter.
 */
#define	CALLOUT_EXECUTING	0x80000000
#define	CALLOUT_UNSAFE_DRIVER	0x40000000
#define	CALLOUT_REALTIME	0x20000000
#define	CALLOUT_NORMAL		0x10000000
#define	CALLOUT_LONGTERM	0x08000000
#define	CALLOUT_TABLE_ID_MASK	(CALLOUT_REALTIME | CALLOUT_NORMAL)
#define	CALLOUT_AUX_ID_MASK	(CALLOUT_TABLE_ID_MASK | CALLOUT_LONGTERM)
#define	CALLOUT_BUCKET_ID_MASK	(CALLOUT_LONGTERM - 1)
#define	CALLOUT_ID_MASK		(CALLOUT_AUX_ID_MASK | CALLOUT_BUCKET_ID_MASK)

/*
 * CALLOUT_LONGTERM_TICKS and CALLOUT_BUCKETS are inversely related,
 * because the more bits you dedicate to bucket number, the sooner the bucket
 * ID wraps.  When you set these constants, you are making the following
 * assertion about system behavior: during any period of CALLOUT_LONGTERM_TICKS
 * ticks, no more than (CALLOUT_LONGTERM / CALLOUT_BUCKETS) callouts will be
 * generated *which hash to the same bucket*.
 */
#define	CALLOUT_LONGTERM_TICKS	0x4000
#define	CALLOUT_BUCKETS		256		/* MUST be a power of 2 */
#define	CALLOUT_BUCKET_MASK	(CALLOUT_BUCKETS - 1)

#define	CALLOUT_THREADS		2		/* keep it simple for now */

/*
 * NOTE: The first field of the callout_bucket structure matches the first
 * field of the callout structure, so that callout_t and callout_bucket_t
 * pointers may be used interchangeably for linked list management.
 */
typedef struct callout_bucket {
	struct callout	*b_first;	/* first in bucket */
	int		b_short_id;	/* last short-term ID in this bucket */
	int		b_long_id;	/* last long-term ID in this bucket */
} callout_bucket_t;

/*
 * All of the state information associated with a callout table.
 * The fields are ordered with cache performance in mind.
 */
typedef struct callout_state {
	kmutex_t	cs_lock;	/* protects all callout state */
	callout_t	*cs_freelist;	/* free callout structures */
	long		cs_curtime;	/* current time; tracks lbolt */
	long		cs_runtime;	/* the callouts we're running now */
	kcondvar_t	cs_threadpool;	/* schedules callout threads */
	int		cs_ncallout;	/* number of callouts allocated */
	long		cs_busyticks;	/* number of ticks with 1+ callouts */
	callout_bucket_t cs_bucket[CALLOUT_BUCKETS];	/* bucket heads */
} callout_state_t;

#ifdef	_KERNEL
extern	callout_state_t	callout_state, rt_callout_state;
extern	void		callout_init(void);
extern	void		callout_schedule(callout_state_t *);
extern	kmutex_t	unsafe_driver;
extern	pri_t		maxclsyspri;
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CALLO_H */
