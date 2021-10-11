/*
 *	Copyright (c) 1991, Sun Microsystems, Inc.
 */

#ifndef	_SYS_LWP_H
#define	_SYS_LWP_H

#pragma ident	"@(#)lwp.h	1.26	95/03/15 SMI"

#include <sys/synch.h>
#include <sys/ucontext.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * lwp create flags
 */
#define	LWP_DETACHED	0x00000040
#define	LWP_SUSPENDED	0x00000080

/*
 * The following flag is reserved. An application should never use it as a flag
 * to _lwp_create(2).
 */
#define	__LWP_ASLWP	0x00000100

/*
 * Definitions for user programs calling into the _lwp interface.
 */
struct lwpinfo {
	timestruc_t lwp_utime;
	timestruc_t lwp_stime;
	long	    lwpinfo_pad [64];
};

#ifndef _KERNEL

typedef unsigned int lwpid_t;

void		_lwp_makecontext(ucontext_t *, void ((*)(void *)),
		    void *, void *, caddr_t, size_t);
int		_lwp_create(ucontext_t *, unsigned long, lwpid_t *);
int		_lwp_kill(lwpid_t, int);
int		_lwp_info(struct lwpinfo *);
void		_lwp_exit(void);
int		_lwp_wait(lwpid_t, lwpid_t *);
lwpid_t		_lwp_self(void);
int		_lwp_suspend(lwpid_t);
int		_lwp_continue(lwpid_t);
void		_lwp_setprivate(void *);
void*		_lwp_getprivate(void);

#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_LWP_H */
