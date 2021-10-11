/*
 *      Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#ifndef _TD_EXTDCL_H
#define	_TD_EXTDCL_H

#pragma ident	"@(#)td.extdcl.h	1.14	94/12/31 SMI"

/*  ../common/td.c */


/*  ../common/td_po.c */


/*  ../common/td_to.c */

#include "td_to.h"
extern	td_err_e
__td_dwrite_process(const td_thragent_t *ta_p,
		    char *symbol_name, void *buffer, int size);
extern td_err_e
__td_dread_process(const td_thragent_t *ta_p,
	char *symbol_name, void *buffer, int size);
extern td_terr_e
__td_read_thr_struct(const td_thragent_t *ta_p,
	paddr_t thr_addr, uthread_t * thr_struct_p);
extern td_terr_e
__td_thr_map_state(thstate_t ts_state, td_thr_state_e *to_state);
extern td_err_e
__td_read_thread_hash_tbl(const td_thragent_t *ta_p,
	thrtab_t * tab_p, int tab_size);
extern td_terr_e __td_ti_validate(const td_thrinfo_t *ti_p);
extern void	__td_tsd_dump(const td_thrinfo_t *ti_p, const thread_key_t key);
extern void	__td_ti_dump(const td_thrinfo_t *ti_p, int full);
extern int	__td_sigmask_are_equal(sigset_t * mask1_p, sigset_t * mask2_p);

/*  ../common/td_so.c */

#include "td_so.h"
extern	td_serr_e
__td_mutex2so(td_thragent_t *ta_p, mutex_t * lock_p,
	paddr_t lock_addr, td_syncinfo_t *si_p);
#ifdef TD_INTERNAL_TESTS
extern void	__td_si_dump(const td_syncinfo_t *si_p);
#endif

/*  ../common/td_error.c */

#include "td_error.h"
#ifdef TD_INTERNAL_TESTS
extern void	__td_report_db_err(ps_err_e error, char *s);
extern void	__td_report_po_err(td_err_e error, char *s);
extern void	__td_report_to_err(td_terr_e error, char *s);
extern void	__td_report_so_err(td_serr_e error, char *s);
extern void	__td_report_in_err(td_ierr_e error, char *s);
#else
#define	__td_report_db_err(x1, x2)
#define	__td_report_po_err(x1, x2)
#define	__td_report_to_err(x1, x2)
#define	__td_report_so_err(x1, x2)
#define	__td_report_in_err(x1, x2)
#endif

/*  ../common/td_event.c */


#endif /* _TD_EXTDCL_H */
