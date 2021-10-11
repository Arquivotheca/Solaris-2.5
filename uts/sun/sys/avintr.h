/*
 * Copyright (c) 1991-1993, by Sun Microsystems, Inc.
 */

#ifndef _SYS_AVINTR_H
#define	_SYS_AVINTR_H

#pragma ident	"@(#)avintr.h	1.11	94/09/22 SMI"	/* SVr4 */

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/dditypes.h>

/*
 * Period of autovector structures (add this in to get the next level).
 */
#define	MAXIPL	16

#define	INTLEVEL_SOFT		0x10
/*
 * These are only used by sun4m and later OBP versions
 */
#define	INTLEVEL_ONBOARD	0x20
#define	INTLEVEL_SBUS		0x30
#define	INTLEVEL_VME		0x40

#define	INT_LEVEL(lvl)	((lvl) & ~(MAXIPL-1))
#define	INT_IPL(lvl)	((lvl) & (MAXIPL-1))

/*
 * maximum number of autovectored interrupts at a given priority
 * XXX: This is temporary until we come up with dynamic additions..
 */

#define	NVECT	17	/* 16 shared per level, +1 to end the list */
#define	AV_INT_SPURIOUS	-1

#ifdef	__STDC__
typedef u_int (*avfunc)(caddr_t);
#else
typedef u_int (*avfunc)();
#endif	/* __STDC__ */


struct autovec {

	/*
	 * Interrupt handler and argument to pass to it.
	 */

	avfunc	av_vector;
	caddr_t	av_intarg;

	/*
	 * Device that requested the interrupt, used as an id in case
	 * we have to remove it later.
	 */
	dev_info_t *av_devi;

	/*
	 *
	 * If this flag is true, then this is a 'fast' interrupt reservation.
	 * Fast interrupts go directly out of the
	 * trap table for speed and do not go through the normal autovector
	 * interrupt setup code. There can be only one 'fast' interrupt
	 * per autovector level.
	 */
	u_int	av_fast;

	/*
	 * XXX: temporary
	 * av_mutex, if non-zero, points to &unsafe_driver.
	 */
	kmutex_t	*av_mutex;

};

#ifdef _KERNEL

extern const u_int maxautovec;
extern struct autovec * const vectorlist[];

extern int add_avintr(dev_info_t *, int, avfunc, caddr_t, kmutex_t *);
extern void rem_avintr(dev_info_t *, int, avfunc);
extern int settrap(dev_info_t *, int, avfunc);
extern int not_serviced(int *, int, char *);

extern void wait_till_seen(int);

extern u_int nullintr(caddr_t intrg);

extern kmutex_t av_lock;

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_AVINTR_H */
