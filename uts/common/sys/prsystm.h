/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_PRSYSTM_H
#define	_SYS_PRSYSTM_H

#pragma ident	"@(#)prsystm.h	1.21	95/02/16 SMI"	/* SVr4.0 1.4	*/

#include <sys/procfs.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _KERNEL

extern kmutex_t pr_pidlock;
extern kcondvar_t *pr_pid_cv;

/*
 * These are functions in the procfs module that are
 * called from the kernel proper and from other modules.
 */
extern void prinvalidate(struct user *);
extern void prgetpsinfo(proc_t *, struct prpsinfo *, kthread_t *);
extern void prgetstatus(kthread_t *, prstatus_t *);
extern void prgetprfpregs(klwp_t *, prfpregset_t *);
extern void prgetprxregs(klwp_t *, caddr_t);
extern int  prgetprxregsize(void);
extern int  prnsegs(struct as *);
extern void prfree(proc_t *);
extern void prexit(proc_t *);
extern void prlwpexit(kthread_t *);
extern void prexecstart(void);
extern void prexecend(void);
extern void prbarrier(proc_t *);
extern void prstop(klwp_t *, int why, int what);
extern void prnotify(struct vnode *);
extern void prstep(klwp_t *);
extern void prnostep(klwp_t *);
extern void prdostep(void);
extern int  prundostep(void);
extern int  prhasfp(void);
extern int  prhasx(void);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PRSYSTM_H */
