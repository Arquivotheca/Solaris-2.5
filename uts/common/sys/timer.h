/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#ifndef	_SYS_TIMER_H
#define	_SYS_TIMER_H

#pragma ident	"@(#)timer.h	1.12	94/12/06 SMI"

#include <sys/types.h>
#include <sys/proc.h>
#include <sys/thread.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	_KERNEL

#define	_TIMER_MAX 32

#define	timerspecisset(tvp)		((tvp)->tv_sec || (tvp)->tv_nsec)
#define	timerspeccmp(tvp, uvp)		(((tvp)->tv_sec - (uvp)->tv_sec) ? \
	((tvp)->tv_sec - (uvp)->tv_sec):((tvp)->tv_nsec - (uvp)->tv_nsec))
#define	timerspecclear(tvp)		((tvp)->tv_sec = (tvp)->tv_nsec = 0)

/*
 * Timer structure
 */

typedef struct timerstr {
	int		trs_flags;		/* in use, signal, pending? */
	clockid_t	trs_clock_id;		/* clock id		*/
	itimerspec_t	trs_itimer;		/* itimer value		*/
	struct sigqueue	*trs_sigqp;		/* sigqueue ptr		*/
	int		trs_callout_id;		/* callout id		*/
	int		trs_overrun1;		/* current overrun count   */
	int		trs_overrun2;		/* previous overrun count  */
	union {
		klwp_t	*trs_unlwp;		/* ptr to the LWP struc	*/
		struct proc	*trs_unproc;	/* ptr to the proc struc */
	}	trs_un;
} timerstr_t;

#define	TRS_INUSE	0x1
#define	TRS_PERLWP	0x2
#define	TRS_SIGNAL	0x4
#define	TRS_PENDING	0x8

#define	trs_lwp		trs_un.trs_unlwp
#define	trs_proc	trs_un.trs_unproc

#ifdef	__STDC__
extern	void	timer_func(sigqueue_t *);
extern	void	timer_exit(void);
extern	void	timer_lwpexit(void);
extern	int	hzto(struct timeval *);
extern	int	timespectohz(timespec_t *, timespec_t);
extern	int	itimerspecfix(timespec_t *);
extern	void	timespecadd(timespec_t *, timespec_t *);
extern	void	timespecsub(timespec_t *, timespec_t *);
extern	void	timespecfix(timespec_t *);
#else
extern	void	timer_func();
extern	void	timer_exit();
extern	void	timer_lwpexit();
extern	int	hzto();
extern	int	timespectohz();
extern	int	itimerspecfix();
extern	void	timespecadd();
extern	void	timespecsub();
extern	void	timespecfix();
#endif	/* __STDC__ */

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_TIMER_H */
