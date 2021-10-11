/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ifndef	_SYS_PRIOINHERIT_H
#define	_SYS_PRIOINHERIT_H

#pragma ident	"@(#)prioinherit.h	1.9	93/05/07 SMI"

#include <sys/thread.h>
#include <sys/sleepq.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef	_KERNEL

#ifdef	__STDC__

extern void	pi_willto(kthread_t *);
extern void	pi_waive(turnstile_t *);
extern void	pi_amend(kthread_t *, turnstile_t *);

#else

extern void	pi_willto();
extern void	pi_waive();
extern void	pi_amend();

#endif	/* __STDC__ */

#define	PI_INHERITOR(t, ts)	(tstile_inheritor(ts) == t)

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PRIOINHERIT_H */
