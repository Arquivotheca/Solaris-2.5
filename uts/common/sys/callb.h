/*
 * Coypright (c) 1992-1993 by Sun Microsystems, Inc.
 * All Rights Reserved.
 */

#ifndef	_SYS_CALLB_H
#define	_SYS_CALLB_H

#pragma ident	"@(#)callb.h	1.7	95/02/24 SMI"

#include <sys/t_lock.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * definitions of callback classes (c_class)
 */
#define	CB_CL_CPR_DAEMON	0
#define	CB_CL_CPR_VM		1
#define	CB_CL_CPR_CALLOUT	2
#define	CB_CL_CPR_MPSTART	3
#define	CB_CL_CPR_RES1		4
#define	CB_CL_CPR_RES2		5
#define	NCBCLASS		6 /* CHANGE ME if classes are added/removed */

/*
 * CB_CL_CPR_DAEMON class specific definitions are given below:
 */

/*
 * code for CPR callb_execute_class
 */
#define	CB_CODE_CPR_CHKPT	0
#define	CB_CODE_CPR_RESUME	1

typedef	void *		callb_id_t;
/*
 * Per kernel thread structure for CPR daemon callbacks.
 * Must be protected by either a existing lock in the daemon or
 * a new lock created for such a purpose.
 */
typedef struct callb_cpr {
	kmutex_t	*cc_lockp;	/* lock to protect this struct */
	char		cc_events;	/* various events for CPR */
	callb_id_t	cc_id;		/* callb id address */
	kcondvar_t	cc_callb_cv;	/* cv for callback waiting */
	kcondvar_t	cc_stop_cv;	/* cv to checkpoint block */
} callb_cpr_t;

/*
 * cc_events definitions
 */
#define	CALLB_CPR_START		1	/* a checkpoint request's started */
#define	CALLB_CPR_SAFE		2	/* thread is safe for CPR */
#define	CALLB_CPR_LASTRUN	4	/* an extra run needed before stop */

#ifdef  _KERNEL
/*
 * lockp is the lock to protect the callb_cpr_t (cp) structure later on.
 * no lock held is needed for this initialization.
 */
#define	CALLB_CPR_INIT(cp, lockp, func, name)	{			\
		bzero((caddr_t)(cp), sizeof (callb_cpr_t));		\
		(cp)->cc_lockp = lockp;					\
		(cp)->cc_id = callb_add(func, (void *)(cp),		\
			CB_CL_CPR_DAEMON, name);			\
	}
/*
 * The lock to protect cp's content must be held before
 * calling the following two macros.
 *
 * Any code region between CALLB_CPR_SAFE_BEGIN and CALLB_CPR_SAFE_END
 * is safe for checkpoint/resume.
 */
#define	CALLB_CPR_SAFE_BEGIN(cp) { 			\
		ASSERT(MUTEX_HELD((cp)->cc_lockp));	\
		(cp)->cc_events |= CALLB_CPR_SAFE;	\
		if ((cp)->cc_events & CALLB_CPR_START)	\
			cv_signal(&(cp)->cc_callb_cv);	\
	}
#define	CALLB_CPR_SAFE_END(cp, lockp) {				\
		ASSERT(MUTEX_HELD((cp)->cc_lockp));		\
		while ((cp)->cc_events & CALLB_CPR_START &&	\
			!((cp)->cc_events & CALLB_CPR_LASTRUN))	\
			cv_wait(&(cp)->cc_stop_cv, lockp);	\
		(cp)->cc_events &= ~CALLB_CPR_SAFE;		\
		if ((cp)->cc_events & CALLB_CPR_LASTRUN)	\
			(cp)->cc_events &= ~CALLB_CPR_LASTRUN;	\
	}
/*
 * cv_destory is nop right now but may be needed in the future.
 */
#define	CALLB_CPR_EXIT(cp) {				\
		ASSERT(MUTEX_HELD((cp)->cc_lockp));	\
		callb_delete((cp)->cc_id);		\
		cv_destroy(&(cp)->cc_callb_cv);		\
		cv_destroy(&(cp)->cc_stop_cv);		\
	}

extern void	callb_init(void);
extern callb_id_t callb_add(void (*func)(void *arg, int code),
		void *arg, int class, char *name);
extern int	callb_delete(callb_id_t);
extern void	callb_execute(callb_id_t, int code);
extern void	callb_execute_class(int class, int code);
extern void	callb_generic_cpr(void *arg, int code);
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CALLB_H */
