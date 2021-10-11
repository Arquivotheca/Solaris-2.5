/*
 *      Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#ifndef _TD_PO_H
#define	_TD_PO_H

#pragma ident	"@(#)td_po.h	1.28	94/12/31 SMI"

/*
* MODULE_td_po.h____________________________________________________
*
*  Description:
*	Header for libthread_db thread agents.
____________________________________________________________________ */

struct td_thragent {
	struct ps_prochandle	*ph_p;
	paddr_t			hash_tab_addr;
	rwlock_t		rwlock;
};

/*
 * These are names of variables in libthread.  These names are passed.
 * to the symbol lookup function to find their addresses.
 */
#define	TD_TOTAL_THREADS_NAME "_totalthreads"
#define	TD_PO_CONCURRENCY_NAME "_nlwps"
#define	TD_PO_RUNNINGQ_NAME "_onprocq"
#define	TD_NUM_THREAD_NAME "_totalthreads"

/*
 * These are placeholder names of variables in libthread that will
 * be used in the future.
 */
#define	TD_PO_STATS_ENABLE_NAME "_po_stats_enable"
#define	TD_NTHREADS_NAME "_fake_nthreads"
#define	TD_R_CONCURRENCY_NAME "_fake_r_concurrrency"
#define	TD_NRUNNABLE_NAME "_fake_nrunnable"
#define	TD_A_CONCURRENCY_NAME "_fake_a_concurrency"
#define	TD_NLWPS_NAME "_fake_nlwps"
#define	TD_NIDLE_NAME "_fake_nidle"

#endif /* _TD_PO_H */
