/*
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

#ifndef _SYS_MACHLOCK_H
#define	_SYS_MACHLOCK_H

#pragma ident	"@(#)machlock.h	1.14	94/10/20 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef	_ASM

#include <sys/types.h>

#ifdef __STDC__
extern void	lock_set(lock_t *lp);
extern int	lock_try(lock_t *lp);
extern int	ulock_try(lock_t *lp);
extern void	lock_clear(lock_t *lp);
extern void	ulock_clear(lock_t *lp);

extern int	lock_set_spl(lock_t *lp, int pl);
extern void	lock_clear_splx(lock_t *lp, int s);
#else	/* __STDC__ */
extern void	lock_set();		/* get lock if possible */
extern int	lock_try();		/* get lock if possible */
extern int	ulock_try();		/* get a user space lock if possible */
extern void	lock_clear();		/* release lock */
extern void	ulock_clear();		/* user space release lock */

extern int	lock_set_spl();		/* disable interrupts then get lock */
extern void	lock_clear_splx();	/* release lock and enable interrupts */
#endif	/* __STDC__ */

#define	LOCK_HELD_VALUE		0xff
#define	LOCK_INIT_CLEAR(lp)	(*(lp) = 0)
#define	LOCK_INIT_HELD(lp)	(*(lp) = LOCK_HELD_VALUE)
#define	LOCK_HELD(lp)		(*(lp) != 0)

typedef	lock_t	disp_lock_t;		/* dispatcher lock type */

/*
 * SPIN_LOCK() macro indicates whether lock is implemented as a spin lock or
 * an adaptive mutex, depending on what interrupt levels use it.
 */
#define	SPIN_LOCK(pl)	((pl) > ipltospl(LOCK_LEVEL))

/*
 * Macro to control loops which spin on a lock and then check state
 * periodically.  Its passed an integer, and returns a boolean value
 * that if true indicates its a good time to get the scheduler lock and
 * check the state of the current owner of the lock.
 */
#define	LOCK_SAMPLE_INTERVAL(i)	(((i) & 0xff) == 0)

/*
 * Externs for CLOCK_LOCK and clock resolution
 */
extern	int	hres_lock;
extern	int	clock_res;

#endif	/* _ASM */

#define	LOCK_LEVEL	10		/* PIL level for lowest level locks */
#define	CLOCK_LEVEL	10		/* PIL level for clock */

/*
 * The mutex and semaphore code depends on being able to represent a lock
 * plus owner in a single 32-bit word.  Thus the owner must contain at most
 * 24 significant bits.  At present only threads, mutexes and semaphores
 * must be aware of this vile constraint.  Different ISAs may handle this
 * differently depending on their capabilities (e.g. compare-and-swap)
 * and limitations (e.g. constraints on alignment and/or KERNELBASE).
 */
#define	PTR24_LSB	5			/* lower bits all zero */
#define	PTR24_MSB	(PTR24_LSB + 24)	/* upper bits all one */
#define	PTR24_ALIGN	32		/* minimum alignment (1 << lsb) */
#define	PTR24_BASE	0xe0000000	/* minimum ptr value (-1 >> (32-msb)) */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MACHLOCK_H */
