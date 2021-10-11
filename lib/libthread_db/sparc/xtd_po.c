/*
*
*ident  "@(#)xtd_po.c 1.16     95/02/12 SMI"
*
* Copyright 1993, 1994 by Sun Microsystems, Inc.
*
*/


/*
*  Description:
* The functions in this module contain functions that
* interact in a sparc specific way with the process or processes
* that execute the program.
*/

#ifdef __STDC__
#pragma weak td_ta_map_lwp2thr = __td_ta_map_lwp2thr /* i386 work around */
#endif				/* __STDC__ */

#include <thread_db.h>
#include "thread_db2.h"

#include "td.h"
#include "td_po.h"
#include "td_to.h"


/*
* Description:
*   Convert an LWP to a thread handle.
*
* Input:
*   *ta_p - thread agent
*   lwpid - lwpid for LWP on which thread
* 	is being requested.
*
* Output:
*   *th_p - thread handle for thread executing
* on LWP with lwpid.
*
* Side effects:
*   none.
*   Imported functions called: ps_lgetregs, ps_pdread.
*/

td_err_e
__td_ta_map_lwp2thr(const td_thragent_t *ta_p, lwpid_t lwpid,
	td_thrhandle_t *th_p)
{
	td_err_e	return_val = TD_ERR;
	prgregset_t	gregset;
	ps_err_e	db_return;
	uthread_t	ts;

#ifdef TEST_PS_CALLS
	if (td_noop) {
		(void) ps_lgetregs(0, 0, 0);
		(void) ps_pdread(0, 0, 0, 0);
		return (TD_OK);
	}
#endif

	/*
	 * Sanity checks.
	 */
	if (ta_p == NULL) {
		return (TD_BADTA);
	}
	/*
	 * Get a reader lock.
	 */
	if (rw_rdlock((rwlock_t *)&(ta_p->rwlock))) {
		return_val = TD_ERR;
		return (return_val);
	}
	if (ta_p->ph_p == NULL) {
		rw_unlock((rwlock_t *)&(ta_p->rwlock));
		return (TD_BADPH);
	}

	/*
	 * Extract %g7 from register set.  This is done so that early use of
	 * interface can be made before all the thread initialization has
	 * been done.  Is %g7 set earlier that the list of all threads?
	 */

	db_return = ps_lgetregs(ta_p->ph_p, lwpid, gregset);

	if (db_return != PS_OK) {
		return_val = TD_DBERR;
	} else {
		th_p->th_unique = (paddr_t) gregset[R_G7];

		/*
		 * That thread struct pointer is not NIL
		 */
		if (th_p->th_unique == NULL) {
			return_val = TD_NOTHR;
		} else {
			/*
			 * Check that lwpid matches.
			 */
			if (__td_read_thr_struct(ta_p, th_p->th_unique,
					&ts) == TD_TOK) {
				if (ts.t_lwpid == lwpid) {
					th_p->th_ta_p =
						(td_thragent_t *) ta_p;
					return_val = TD_OK;
				} else {
					return_val = TD_NOTHR;
				}
			} else {
				return_val = TD_NOTHR;
			}
		}
	}

	rw_unlock((rwlock_t *)&(ta_p->rwlock));
	return (return_val);

}
