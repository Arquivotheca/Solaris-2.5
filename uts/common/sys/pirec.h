/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ifndef _SYS_PIREC_H
#define	_SYS_PIREC_H

#pragma ident	"@(#)pirec.h	1.11	93/12/20 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The pirec_t structure is allocated when a lock becomes priority
 * inverted, i.e., a higher priority thread blocks requesting a lock
 * held by a lower priority thread.
 *
 * The thread that owns the priority inverted lock maintains a circular
 * linked list of pirec_t's for each priority inverted lock that it
 * holds. The priority inverted lock also contains a pointer to its
 * pirec_t in the thread's linked list.
 *
 * The pirec_t structure contains the following data:
 *	o Pointers to implement the circular linked list of
 *	  these structures, one for each priority-inverted lock
 * 	o The dispatch priority (effective priority) of the highest
 *	  priority thread blocked on the lock that contains a pointer
 *	  to this pirec_t.
 */

typedef struct pirec
{
	struct pirec	*pi_forw;	/* Used to implement C-linked-list */
	struct pirec	*pi_back;	/*  "    "     "         "	   */
	struct _kthread *pi_benef;	/* beneficiary of the PI */
	uint_t		pi_epri_hi;	/* max priority of blocked threads */
} pirec_t;

#ifdef	_KERNEL

/*
 * Check to see whether we need to raise the dispatch priority
 * of a synchronization object that has already been marked as
 * priority inverted.
 */
#define	PIREC_RAISE(p, dpri)	\
	if ((p)->pi_epri_hi < (dpri))	\
		(p)->pi_epri_hi = (dpri)


#ifdef	__STDC__
extern void	pirec_init(pirec_t *, struct _kthread *, pri_t);
extern void	pirec_clear(pirec_t *);
extern void	pirec_insque(pirec_t *, pirec_t *);
extern void	pirec_remque(pirec_t *);
extern pri_t	pirec_calcpri(pirec_t *, pri_t);
#else
extern void	pirec_init();
extern void	pirec_clear();
extern void	pirec_insque();
extern void	pirec_remque();
extern pri_t	pirec_calcpri();
#endif	/* __STDC__ */

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PIREC_H */
