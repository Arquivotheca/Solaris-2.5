/*
 *      Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#ifndef _TD_TO_H
#define	_TD_TO_H

#pragma ident	"@(#)td_to.h	1.46	95/05/25 SMI"

/*
* MODULE_td_to.h____________________________________________________
*  Description:
____________________________________________________________________ */

#define	TD_PARTIAL 0
#define	TD_FULL 1
#define	TD_EVENT_ON 1
#define	TD_EVENT_OFF 0

/*
*   These are names of variables within libthread that are used
* to find thread information.
*/

#define	TD_HASH_TAB_NAME "_allthreads"
#define	TD_TSD_THREAD_NAME "tsd_thread"
#define	TD_LIBTHREAD_NAME "libthread.so"
#define	TD_TSD_COMMON_NAME "tsd_common"
#define	TD_TSD_TOTALTHREADS_NAME "_totalthreads"
#define	TD_HASH_TAB_SIZE ALLTHR_TBLSIZ
#define	TD_HASH_TID(x) HASH_TID(x)

/*
*  Thread struct access macros.  Macros containing $$$$$$$$ access
* are place holders for information not yet available from libthread.
*/

#define	TD_CONVERT_TYPE(uthread_t) 			\
	((uthread_t).t_flag&T_INTERNAL ? TD_THR_SYSTEM : TD_THR_USER)

/*
*   Threads hash table
*/

#define	td_hash_first_(x) ((x).first)

#define	DB_NOT_SUSPENDED 0
#define	DB_SUSPENDED  1

/*
*   Create a tsd.h file in libthread!!!
*/

#ifdef TD_ALIEN_CODE
typedef struct tsd_thread {
	unsigned int	count;	/* number of allocated storage cells */
	void		**array;	/* pointer to allocated storage cells */
} tsd_t;
#endif

/*
*   Thread information access macros.
* The td_toc_* and td_tov_* are relics of the earlier definition of
* the constant and variable parts of the thread information.
*/

/*
*   Macros for testing state of thread.
*/
#define	ISONPROC(x)	((*(x)).t_state == TS_ONPROC)
#define	ISZOMBIE(x)	((*(x)).t_state == TS_ZOMB)
#define	TO_ISONPROC(x) ((x).ti_state == TD_THR_ACTIVE)
#define	TO_ISBOUND(x)  ((x).ti_user_flags & THR_BOUND)
#define	TO_ISPARKED(x) ((x).ti_user_flags & T_PARKED)

/*
 * Is this thread currently associated with an lwp?
 * ISONPROC says that the thread is running on an lwp and we
 * can therefore set/get the registers from the lwp. ISBOUND
 * says that the thread is bound to an lwp.  Even if it is
 * not running, it still has an lwp so set/get of registers
 * to lwp are allowed. ISPARKED says the lwp is parked on a
 * semaphore waiting for a runnable thread.
 */
#define	ISVALIDLWP(t)	(ISONPROC(t) || ISPARKED(t) || \
	(!ISZOMBIE(t) && (ISBOUND(t) || ISTEMPBOUND(t))))

/*
 * FIXME - when thread struct has a bit to show when FP registers
 * in an LWP are valid, use it here.
 */
#define	HASVALIDFP(x)	(1)

typedef	td_terr_e
td_set_func_t(uthread_t *, void *);

/*
*   Only good for 2 word of events.  Check event masks macros.
*/
#if TD_EVENTSIZE != 2
#endif

#endif /* _TD_TO_H */
