/*
 *      Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#ifndef _TD_PUBDCL_H
#define	_TD_PUBDCL_H

#pragma ident	"@(#)td.pubdcl.h	1.10	94/12/31 SMI"

/*  ../common/td.c */

extern void
td_log(const int on_off);
extern	td_err_e
td_ta_new(const struct ps_prochandle * ph_p, td_thragent_t **ta_pp);
extern td_err_e
td_ta_delete(td_thragent_t *ta_p);
extern td_err_e
td_init();

/*  ../common/td_po.c */

extern td_err_e
td_ta_setconcurrency(const td_thragent_t *ta_p, int level);
extern td_err_e
td_ta_get_ph(const td_thragent_t *ta_p, struct ps_prochandle ** ph_pp);
extern td_err_e
td_ta_get_nthreads(const td_thragent_t *ta_p, int *nthread_p);
extern td_err_e
td_ta_enable_stats(const td_thragent_t *ta_p, int onoff);
extern td_err_e
td_ta_reset_stats(const td_thragent_t *ta_p);
extern td_err_e
td_ta_get_stats(const td_thragent_t *ta_p, td_ta_stats_t *tstats);
extern td_err_e
td_ta_tsd_iter(const td_thragent_t *ta_p, td_key_iter_f * cb, void *cbdata_p);
extern td_err_e
__td_ta_thr_iter(const td_thragent_t *ta_p, td_thr_iter_f * cb,
	void *cbdata_p, td_thr_state_e state, int ti_pri,
	sigset_t * ti_sigmask_p, unsigned ti_user_flags);
extern td_err_e
td_ta_sync_iter(const td_thragent_t *ta_p, td_sync_iter_f * cb, void *cbdata_p);
extern td_err_e
td_ta_map_addr2thr(const td_thragent_t *ta_p, paddr_t thread,
	td_thrhandle_t *th_p);

/*  ../common/td_to.c */

extern td_err_e
td_thr_validate(const td_thrhandle_t *th_p);
extern td_err_e
td_thr_tsd(const td_thrhandle_t *th_p, const thread_key_t key, void **data_pp);
extern td_err_e
__td_thr_get_info(const td_thrhandle_t *th_p, td_thrinfo_t *ti_p);
extern td_err_e
td_thr_getfpregs(const td_thrhandle_t *th_p, prfpregset_t * fpregset);
extern td_err_e
td_thr_getxregsize(const td_thrhandle_t *th_p, int *xregsize);
extern td_err_e
td_thr_getxregs(const td_thrhandle_t *th_p, const caddr_t xregset);
extern td_err_e
td_thr_sleepinfo(const td_thrhandle_t *th_p, td_synchandle_t *sh_p);
extern td_err_e
td_thr_lockowner(const td_thrhandle_t *th_p, td_sync_iter_f * cb,
	void *cb_data_p);
extern td_err_e
td_thr_sigsetmask(const td_thrhandle_t *th_p, const sigset_t ti_sigmask);
extern td_err_e
td_thr_setprio(const td_thrhandle_t *th_p, const int ti_pri);
extern td_err_e
td_thr_setsigpending(const td_thrhandle_t *th_p, const uchar_t ti_pending_flag,
	const sigset_t ti_pending);
extern td_err_e
td_thr_setfpregs(const td_thrhandle_t *th_p, const prfpregset_t * fpregset);
extern td_err_e
td_thr_setxregs(const td_thrhandle_t *th_p, const caddr_t xregset);
extern td_err_e
td_thr_dbsuspend(const td_thrhandle_t *th_p);
extern td_err_e
td_thr_dbresume(const td_thrhandle_t *th_p);
extern td_err_e
td_ta_map_id2thr(const td_thragent_t *ta_p, thread_t tid, td_thrhandle_t *th_p);

/*  ../common/td_so.c */

extern td_err_e
td_ta_map_addr2sync(const td_thragent_t *ta_p, paddr_t addr,
	td_synchandle_t *sh_p);
extern td_err_e
td_sync_waiters(const td_synchandle_t *sh_p, td_thr_iter_f * cb,
	void *cb_data_p);
extern td_err_e
td_sync_ownerpc(const td_synchandle_t *sh_p, paddr_t * pc_p);
extern td_err_e
td_sync_setstate(const td_synchandle_t *sh_p, int value);
extern td_err_e
td_sync_reset_stats(const td_synchandle_t *sh_p);
extern td_err_e
td_sync_get_info(const td_synchandle_t *sh_p, td_syncinfo_t *si_p);

/*  ../common/td_error.c */


/*  ../common/td_event.c */

extern td_err_e
td_ta_event_addr(const td_thragent_t *ta_p, u_long event,
	td_notify_t *notify_p);
extern td_err_e
td_thr_event_getmsg(const td_thrhandle_t *th_p, td_event_msg_t *msg);
extern td_err_e
td_thr_event_enable(td_thrhandle_t *th_p, int onoff);
extern td_err_e
td_thr_clear_event(td_thrhandle_t *th_p, td_thr_events_t event);
extern td_err_e
td_thr_set_event(const td_thrhandle_t *th_p, td_thr_events_t event);


#endif /* _TD_PUBDCL_H */
