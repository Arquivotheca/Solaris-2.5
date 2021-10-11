/*
 *	Copyright (c) 1991,1993 Sun Microsystems, Inc.
 */

#ifndef _SYS_MUTEX_H
#define	_SYS_MUTEX_H

#pragma ident	"@(#)mutex.h	1.14	94/07/29 SMI"

#ifndef	_ASM
#include <sys/types.h>
#include <sys/dki_lkinfo.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef	_ASM
/*
 * Mutex types.
 *
 * The basic mutex type is MUTEX_ADAPTIVE, and this is expected to be used
 * in almost all of the kernel.  MUTEX_SPIN provides interrupt blocking
 * and must be used in interrupt handlers above LOCK_LEVEL.
 *
 * The _STAT variants of these types keep statistics on how many times
 * the mutex was held and waited for, and for how long.
 *
 * MUTEX_DEFAULT is the type usually specified (except in drivers) to
 * mutex_init().  This lets the system choose whether the mutex will
 * have statistics kept on it or not, based on compile time options
 * and the runtime flag 'lock_stats'.
 *
 * MUTEX_DRIVER is always used by drivers and automatically selects spin
 * or adaptive types, depending on the arg, and also selects whether to
 * keep statistics as in MUTEX_DEFAULT.
 *
 * The DRIVER mutex types 4 and 5 must be implemented as 2-word mutexes
 * for Sun Sparc DDI binary compatibility.  If the size of the mutex
 * must be increased beyond 2 words, a those type names may be reassigned
 * but the old types must still support 2-word mutexes, perhaps by pointing
 * to a longer dynamicly-allocated mutex.
 */
typedef enum {
	MUTEX_ADAPTIVE = 0,	/* spin if owner is running */
	MUTEX_SPIN,		/* spin and block interrupts */
	MUTEX_ADAPTIVE_STAT,	/* adaptive with statistics */
	MUTEX_SPIN_STAT,	/* spin with statistics */
	MUTEX_DRIVER_NOSTAT = 4, /* driver mutex */
	MUTEX_DRIVER_STAT = 5,	/* driver mutex with statistics */
	MUTEX_ADAPTIVE_DEF	/* adaptive (with statistics if enabled) */
} kmutex_type_t;

#if defined(_LOCKTEST) || defined(_MPSTATS)
#define	MUTEX_DEFAULT		MUTEX_ADAPTIVE_STAT
#define	MUTEX_DRIVER		MUTEX_DRIVER_STAT
#define	MUTEX_SPIN_DEFAULT	MUTEX_SPIN_STAT
#else
#define	MUTEX_DEFAULT		MUTEX_ADAPTIVE_DEF
#define	MUTEX_DRIVER		MUTEX_DRIVER_NOSTAT
#define	MUTEX_SPIN_DEFAULT	MUTEX_SPIN
#endif

/*
 * Default argument for MUTEX_ADAPTIVE:  This should not be used for
 * type MUTEX_DEFAULT.  That should be passed NULL.
 * For type MUTEX_SPIN and MUTEX_SPIN_STAT, the argument is the %psr PIL
 * level that should be blocked while the mutex is held.
 */
#define	DEFAULT_WT	NULL


typedef struct mutex {
	void	*_opaque[2];
} kmutex_t;


#if defined(_KERNEL) && defined(__STDC__)

#define	MUTEX_HELD(x)		(mutex_owned(x))
#define	MUTEX_NOT_HELD(x)	(!mutex_owned(x) || panicstr)

extern	void	lock_mutex_flush(void);

/*
 * mutex function prototypes
 */

extern	void	mutex_init(kmutex_t *, char *, kmutex_type_t, void *);
extern	void	mutex_destroy(kmutex_t *);
extern	void	mutex_enter(kmutex_t *);
extern	int	mutex_tryenter(kmutex_t *);
extern	void	mutex_exit(kmutex_t *);
extern	lkstat_t *mutex_stats(kmutex_t *);
extern	int	mutex_owned(kmutex_t *);
extern	struct _kthread *mutex_owner(kmutex_t *);


#endif	/* defined(_KERNEL) && defined(__STDC__) */

#endif	/* _ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MUTEX_H */
