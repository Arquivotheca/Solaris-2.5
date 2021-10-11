/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ifndef _SYS_TURNSTILE_H
#define	_SYS_TURNSTILE_H

#pragma ident	"@(#)turnstile.h	1.27	94/10/27 SMI"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/pirec.h>
#include <sys/sleepq.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	NTSTILE_SQ	2	/* # of sleep queues per turnstile */

/*
 * Definitions for which queue number to use
 * depending on the synchronization object type.
 */

typedef enum {
	QOBJ_UND	= -1,	/* sleepq queue is undefined (illegal) */
	QOBJ_DEF	= 0,	/* default queue to use */
	QOBJ_READER	= 0,	/* sleepq queue to use for readers */
	QOBJ_WRITER	= 1,	/* sleepq queue to use for writers */
	QOBJ_CV		= 0,	/* sleepq queue to use for cond. variables */
	QOBJ_MUTEX	= 0,	/* sleepq queue to use for mutexes */
	QOBJ_SEMA	= 0	/* sleepq queue to use for semaphores */
} qobj_t;


#define	TSTILE_FREE	0	/* on free list */
#define	TSTILE_ACTIVE	1	/* attached to synch object */

typedef struct turnstile	turnstile_t;

typedef ushort_t		turnstile_id_t;

struct turnstile {
	union tstile_un {
		/*
		 * ts_forw is a link for chaining turnstile_t's
		 * into a pool.
		 */
		turnstile_t	*ts_forw;

		/*
		 * ts_prioinv contains the priority inversion
		 * information about the synchronization object
		 * associated with this turnstile.
		 */
		pirec_t		ts_prioinv;
	} tsun;

	/*
	 * Maintain separate queues for readers &
	 * writer locks.
	 */
	sleepq_t	ts_sleepq[NTSTILE_SQ];
	/*
	 * ts_id is the unique 16 bit id for this turnstile.
	 * It consists of an encoded [row][column] pair used to
	 * locate a turnstile by indexing into the the chunk of
	 * allocated turnstiles.
	 */
	turnstile_id_t	ts_id;
	uchar_t		ts_flags;
	disp_lock_t	ts_wlock;	/* lock for use by sync obj */

	/*
	 * ts_sobj_priv_data is used only by certain synchronization objects
	 * which may need to re-verify that the turnstile is theirs.
	 * This field is cleared for unallocated turnstiles.
	 */
	void		*ts_sobj_priv_data;	/* private data for sync obj */
};

#ifdef	_KERNEL

/*
 * The following controls the allocation of turnstiles and
 * the encoding of the turnstile-id #.
 */
#define	TS_ROWSZ		512	/* # of turnstile chunk rows */
#define	TS_COLSZ		128	/* # of turnstiles/chunk */
#define	TS_ROWBITS		9	/* # bits in id for row # */
#define	TS_COLBITS		7	/* # bits in id for column # */
#define	TS_ROWMASK		((1 << TS_ROWBITS) - 1)

/*
 * Encode a row,column pair and as a turnstile-id.
 */
#define	TS_ROWCOL(row, col)	(col << TS_ROWBITS | row)

/*
 * Extract a column # from a turnstile-id.
 */
#define	TS_COL(ts_id)		((ts_id) >> TS_ROWBITS)

/*
 * Extract a row # from a turnstile-id.
 */
#define	TS_ROW(ts_id)		((ts_id) & TS_ROWMASK)

/*
 * Allocate "n" turnstiles. "cond" tells whether sleeping is okay.
 */
#define	TS_NEW(n, cond)		\
	(turnstile_t *)kmem_zalloc(n * sizeof (turnstile_t), cond)

/*
 * Determine whether the indicated row and column represent valid
 * (allocated) turnstiles.
 */
#define	TS_VALID(r, c)		((r < tstile_mod.tsm_rowcnt) && \
				(c < tstile_mod.tsm_colsz))


#define	TSTILE_PRIO_INVERTED(ts)	\
		((ts)->tsun.ts_prioinv.pi_benef != NULL)
#define	TSTILE_INSERT(ts, qnum, c)	\
		sleepq_insert(&(ts)->ts_sleepq[qnum], c)
#define	TSTILE_WAKEONE(ts, qnum)	\
		sleepq_wakeone(&(ts)->ts_sleepq[qnum])
#define	TSTILE_WAKEALL(ts, qnum)	\
		sleepq_wakeall(&(ts)->ts_sleepq[qnum])
#define	TSTILE_SLEEPQ(ts, sobj_ops)	\
		((ts)->ts_sleepq[SOBJ_QNUM(sobj_ops)])
#define	TSTILE_DEQ(ts, qnum, t)	\
		sleepq_dequeue(&(ts)->ts_sleepq[qnum], t)
#define	TSTILE_UNSLEEP(ts, qnum, t)	\
		sleepq_unsleep(&(ts)->ts_sleepq[qnum], t)
#define	TSTILE_EMPTY(ts, qnum)	\
		((ts)->ts_sleepq[qnum].sq_first == NULL)


#ifdef	__STDC__

extern void		tstile_init(void);
extern int		tstile_more(int, int);
extern turnstile_t	*tstile_pointer(turnstile_id_t);
extern turnstile_t	*tstile_pointer_verify(turnstile_id_t);
extern turnstile_t	*tstile_alloc(void);
extern void		tstile_free(turnstile_t *, turnstile_id_t *);
extern struct _kthread *tstile_unsleep(turnstile_t *, qobj_t,
					struct _kthread *);
extern void		tstile_insert(turnstile_t *, qobj_t, struct _kthread *);
extern void 		tstile_wakeone(turnstile_t *, qobj_t);
extern void		tstile_wakeall(turnstile_t *, qobj_t);
extern int		tstile_deq(turnstile_t *, qobj_t, struct _kthread *t);
extern pri_t		tstile_maxpri(turnstile_t *);
extern int		tstile_prio_inverted(turnstile_t *);
extern int		tstile_empty(turnstile_t *, qobj_t);
extern struct _kthread	*tstile_inheritor(turnstile_t *);

#else

extern void		tstile_init();
extern int		tstile_more();
extern turnstile_t	*tstile_pointer();
extern turnstile_t	*tstile_pointer_verify();
extern turnstile_t	*tstile_alloc();
extern void		tstile_free();
extern struct _kthread	*tstile_unsleep();
extern void		tstile_insert();
extern void		tstile_wakeone();
extern void		tstile_wakeall();
extern int		tstile_deq();
extern pri_t		tstile_maxpri();
extern int		tstile_prio_inverted();
extern int		tstile_empty();
extern struct _kthread	*tstile_inheritor();

#endif	/* __STDC__ */

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_TURNSTILE_H */
