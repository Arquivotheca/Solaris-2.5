/*
*
*ident  "@(#)td_so.c 1.41     94/12/31 SMI"
*
* Copyright 1993, 1994 by Sun Microsystems, Inc.
*
*/


/*
*
* Description:
* This module contains the functions that interact with the
* synchronization variables(locks, semaphores, r/w locks, and
* events).
*/


#ifdef __STDC__
#ifdef PHASE2
#pragma weak td_ta_map_addr2sync
#pragma weak td_sync_waiters
#pragma weak td_sync_ownerpc
#pragma weak td_sync_setstate
#pragma weak td_sync_reset_stats
#pragma weak td_sync_get_info
#endif /* END PHASE2 */
#endif				/* __STDC__ */

#include <thread_db.h>
#include "thread_db2.h"

#include "td.h"
#include "td_so.h"
#include "td_po.h"

#define	TDS_M1 "Cond. variable does not have value to reset - td_sync_setstate";

#ifdef PHASE2

/*
* Description:
*   Transfer information from mutex lock to sync. variable
*
* Input:
*   *ta_p - thread agent
*   *lock_p - mutex lock
*
* Output:
*   *si_p - sync. info. struct
*
* Side effects:
*   none
*
* Limitations:
* Statistics on mutex not available.
*/

td_serr_e __td_mutex2so(td_thragent_t *ta_p, mutex_t * lock_p,
			paddr_t lock_addr, td_syncinfo_t *si_p)
{
	td_serr_e	return_val = TD_SERR;
	td_err_e	po_return;
	thread_t	owner_id;
	td_thrhandle_t	owner_th;
	int	i;

	if (si_p == NULL) {
		return_val = TD_SBADSO;
		__td_report_so_err(return_val,
			"Null sync. info. struct pointer - td_mutext2so");
	} else {

		/*
		 * Load up the so.
		 */
		si_p->si_ta_p = ta_p;
		si_p->si_sv_addr = lock_addr;
		si_p->si_type = TD_SYNC_MUTEX;
		si_p->si_shared_type = lock_p->flags.type;

		for (i = 0; i < TD_SV_MAX_FLAGS; i++) {
			si_p->si_flags[i] =
				lock_p->flags.flag[i];
		}

		si_p->si_state.mutex_locked = lock_p->mutex_lockw;
		si_p->si_size = sizeof (*lock_p);
		si_p->si_has_waiters = lock_p->mutex_waiters;

		/*
		 * td_so_owner is a thread handle.
		 *
		 * FIXME - What if the lock is USYNC_PROCESS type and current
		 * owner is some other process?
		 */
		owner_id = lock_p->lock.owner64;
		po_return = __td_ta_map_id2thr(ta_p, owner_id, &owner_th);
		if (po_return == TD_OK) {
			si_p->si_owner = owner_th;
			si_p->si_data = lock_p->data;

			/*
			 * FIXME - statistics not available.
			 */
			memset(&si_p->si_stats, 0,
				sizeof (si_p->si_stats));

			return_val = TD_SOK;
		} else {
			return_val = TD_SBADMUTEX;
			__td_report_so_err(return_val,
				"Invalid lock owner - __td_mutex2so");
		}
	}

	return (return_val);
}



/*
* Description:
*   Given an address to a synchronization variable,
* convert it to a synchronization handle.
*
* Input:
*   *ta_p - thread agent
*   addr - address of synchronization primitive data structure
*
* Output:
*   *sh_p - synchronization handle corresponding to
* synchronization primitive
*   td_ta_map_addr2sync - return value
*
* Side effects:
*   none
*   Imported functions called: none
*
*/
td_err_e
td_ta_map_addr2sync(const td_thragent_t *ta_p, paddr_t addr,
	td_synchandle_t *sh_p)
{

	/*
	 * Sanity checks.
	 */
	if (ta_p == NULL) {
		return (TD_BADTA);
	}
	if (ta_p->ph_p == NULL) {
		return (TD_BADPH);
	}
	if (sh_p == NULL) {
		return (TD_BADSH);
	}
	if (addr == NULL) {
		return (TD_ERR);
	}

	/*
	 * Just fill in the appropriate fields of the sync. handle.
	 */

	sh_p->sh_ta_p = (td_thragent_t *) ta_p;
	sh_p->sh_unique = addr;

	return (TD_OK);

}



/*
*
* Description:
*   For a given synchronization variable, iterate over the
* set of waiting threads. The call back function is passed
* two parameters, a pointer to a thread handle and a pointer
* to extra call back data that can be NULL. If the return
* value for cb is non-zero, the iterations terminate.
*
* Input:
*   *sh_p - synchronization handle
*   cb - call back function called on each thread waiting
* on synchronization variable.
*   cb_data_p - data pointer passed to cb
*
* Output:
*   td_sync_waiters - return value
*
* Side effects:
*   cb is called once for each thread waiting on the synchronization
* variable and is passed the thread handle for the thread and cb_data_p.
*   Imported functions called:
*
*/
td_err_e
td_sync_waiters(const td_synchandle_t *sh_p,
		td_thr_iter_f * cb, void *cb_data_p)
{
#ifdef TEST_PS_CALLS
	if (td_noop) {
		return (TD_OK);
	}
#endif

	/*
	 * Sanity checks.
	 */
	if (sh_p == NULL) {
		return (TD_BADSH);
	}
	if (sh_p->sh_ta_p == NULL) {
		return (TD_BADTA);
	}
	if (sh_p->sh_unique == NULL) {
		return (TD_BADSH);
	}

	/*
	 * Get a reader lock.
	 */
	if (rw_rdlock(&(sh_p->sh_ta_p->rwlock))) {
		return_val = TD_ERR;
		return (return_val);
	}

	if (sh_p->sh_ta_p->ph_p == NULL) {
		rw_unlock(&(sh_p->sh_ta_p->rwlock));
		return_val = TD_BADTA;
		return (return_val);
	}



	/*
	 * So lint won't complain.
	 */
	{
		td_thrhandle_t *th_p = 0;
		cb(th_p, cb_data_p);
	}


	rw_unlock(&(sh_p->sh_ta_p->rwlock));
	return (TD_NOT_DONE);
}


/*
* Description:
*   Get the pc where a synchronization variable was acquired.
* This only applies to synchronization variables of type,
* TD_SYNC_MUTEX, and TD_SYNC_RWLOCK which have the property
* of exclusive ownership.
*
* Input:
*   *sh_p - synchronization handle for mutex lock or r/w lock
*
* Output:
*   *pc_p - address where lock was acquired
*   td_sync_ownerpc - return value
*
* Side effects:
*   none
*   Imported functions called:
*/
td_err_e
td_sync_ownerpc(const td_synchandle_t *sh_p, paddr_t * pc_p)
{
#ifdef TEST_PS_CALLS
	if (td_noop) {
		return (TD_OK);
	}
#endif

	/*
	 * Sanity checks.
	 */
	if (sh_p == NULL) {
		return (TD_BADSH);
	}
	if (pc_p == NULL) {
		return (TD_ERR);
	}
	if (sh_p->sh_ta_p == NULL) {
		return (TD_BADTA);
	}
	if (sh_p->sh_unique == NULL) {
		return (TD_BADSH);
	}

	/*
	 * Get a reader lock.
	 */
	if (rw_rdlock(&(sh_p->sh_ta_p->rwlock))) {
		return_val = TD_ERR;
		return (return_val);
	}

	if (sh_p->sh_ta_p->ph_p == NULL) {
		rw_unlock(&(sh_p->sh_ta_p->rwlock));
		return_val = TD_BADTA;
		return (return_val);
	}

	/*
	 * What should be returned if lock is USYNC_PROCESS type and owner is
	 * some other process?
	 */


	rw_unlock(&(sh_p->sh_ta_p->rwlock));
	return (TD_NOT_DONE);
}



/*
* Description:
*    Change the state of a synchronization variable.
* 	1) mutex lock state set to value
* 	2) semaphore's count set to value
* 	3) writer's lock set to value
* 	4) reader's lock number of readers set to value
*
* Input:
*   *sh_p - synchronization handle
*   value - new value of state of synchronization
* variable
*
* Output:
*   td_sync_setstate - return value
*
* Side effects:
*   State of synchronization variable is changed.
*   Imported functions called: ps_pdread, ps_pdwrite
*/
td_err_e
td_sync_setstate(const td_synchandle_t *sh_p, int value)
{
	td_err_e	return_val;
	ps_err_e	db_return;
	td_so_un_t	generic_so;

	/*
	 * So lint won't complain.
	 */
	if (sh_p == NULL) {
		return (TD_BADSH);
	}
	if (sh_p->sh_ta_p == NULL) {
		return (TD_BADTA);
	}
	if (sh_p->sh_unique == NULL) {
		return (TD_BADSH);
	}

	/*
	 * Read the synch. variable information.
	 */

	db_return = ps_pdread(sh_p->sh_ta_p.ph_p, sh_p->sh_unique,
		(char *) &generic_so, sizeof (generic_so));

	/*
	 * Set the new value in the sync. variable, read the synch. variable
	 * information. from the process, reset its value and write it back.
	 */

	switch (si.si_type) {
	case TD_SYNC_COND:
		return_val = TD_ERR;
		__td_report_po_err(return_val, TDS_M1);
		break;
	case TD_SYNC_MUTEX:
		db_return = ps_pdread(sh_p->sh_ta_p,
			sh_p->sh_unique, &lock,
		sizeof (lock));
		if (db_return == PS_OK) {
			lock.mutex_lockw = value;
			db_return = ps_pdwrite(sh_p->sh_ta_p,
				sh_p->sh_unique, &lock,
				sizeof (lock));
		}
		break;
	case TD_SYNC_SEMA:
		db_return = ps_pdread(sh_p->sh_ta_p,
			sh_p->sh_unique, &semaphore,
			sizeof (semaphore));
		if (db_return == PS_OK) {
			semaphore.count = value;
			db_return = ps_pdwrite(sh_p->sh_ta_p,
				sh_p->sh_unique, &semaphore,
				sizeof (semaphore));
		}
		break;
	case TD_SYNC_RWLOCK:
		db_return = ps_pdread(sh_p->sh_ta_p,
			sh_p->sh_unique, &rwlock,
			sizeof (rwlock));
		if (db_return == PS_OK) {
			rwlock.readers = value;
			db_return = ps_pdwrite(sh_p->sh_ta_p,
				sh_p->sh_unique, &rwlock,
				sizeof (rwlock));
		}
		break;
	default:
		return_val = TD_BADSH;
		__td_report_po_err(return_val,
			"Unknown Sync. variable type - td_sync_setstate");
	}

	/*
	 * Check returns of all ps_pdwrite()'s here.
	 */
	if (db_return != PS_OK) {
		return_val = TD_ERR;
	}
	return (TD_NOT_DONE);

}

/*
* Description:
*   Reset the statistics for a synchronization variable.
*
* Input:
*   *sh_p - synchronization handle
*
* Output:
*   td_sync_reset_stats - return value
*
* Side effects:
*   Statistics for *sh_p are reset to zero
*   Imported functions called:
*/
td_err_e
td_sync_reset_stats(const td_synchandle_t *sh_p)
{
#ifdef TEST_PS_CALLS
	if (td_noop) {
		return (TD_OK);
	}
#endif

	/*
	 * Sanity checks.
	 */
	if (sh_p == NULL) {
		return (TD_BADSH);
	}
	if (sh_p->sh_ta_p == NULL) {
		return (TD_BADTA);
	}
	if (sh_p->sh_unique == NULL) {
		return (TD_BADSH);
	}

	/*
	 * Get a reader lock.
	 */
	if (rw_rdlock(&(sh_p->sh_ta_p->rwlock))) {
		return_val = TD_ERR;
		return (return_val);
	}

	if (sh_p->sh_ta_p->ph_p == NULL) {
		rw_unlock(&(sh_p->sh_ta_p->rwlock));
		return_val = TD_BADTA;
		return (return_val);
	}


	rw_unlock(&(sh_p->sh_ta_p->rwlock));

	return (TD_NOT_DONE);

}


/*
* Description:
*   Given an synchronization handle, fill in the
* information for the synchronization variable into *si_p.
*
* Input:
*   *sh_p - synchronization handle
*
* Output:
*   *si_p - synchronization information structure corresponding to
* synchronization handle
*   td_sync_get_info - return value
*
* Side effects:
*   none
*   Imported functions called:
*/
td_err_e
td_sync_get_info(const td_synchandle_t *sh_p,
	td_syncinfo_t *si_p)
{
#ifdef TEST_PS_CALLS
	if (td_noop) {
		return (TD_OK);
	}
#endif

	/*
	 * Sanity checks.
	 */
	if (sh_p == NULL) {
		return (TD_BADSH);
	}
	if (si_p == NULL) {
		return (TD_ERR);
	}
	if (sh_p->sh_ta_p == NULL) {
		return (TD_BADTA);
	}
	if (sh_p->sh_unique == NULL) {
		return (TD_BADSH);
	}

	/*
	 * Get a reader lock.
	 */
	if (rw_rdlock(&(sh_p->sh_ta_p->rwlock))) {
		return_val = TD_ERR;
		return (return_val);
	}

	if (sh_p->sh_ta_p->ph_p == NULL) {
		rw_unlock(&(sh_p->sh_ta_p->rwlock));
		return_val = TD_BADTA;
		return (return_val);
	}

	/*
	 * Do a consistency check on synchronization
	 * information struct and if it is
	 * inconsistent, pass back TD_SYNC_UNKNOWN.  Check at least the type
	 * of the synchronization variable.
	 */

	rw_unlock(&(sh_p->sh_ta_p->rwlock));

	return (TD_NOT_DONE);

}

#endif /* END PHASE2 */

#ifdef TD_INTERNAL_TESTS


/*
* Description:
*   Dump the contents of the synchronization information struct
*
* Input:
*   *si_p - synchronization information struct
*
* Output:
*   none
*
* Side effects:
*/
void
__td_si_dump(const td_syncinfo_t *si_p)
{

	diag_print("Synchronization variable:\n");
	diag_print("  Address:			%x\n", si_p->si_sv_addr);
	diag_print("  Type:				%s\n",
		td_sync_type_names[si_p->si_type]);
	diag_print("  Flags:				%x\n",
		si_p->si_flags);
	switch (si_p->si_type) {
	case TD_SYNC_MUTEX:
		diag_print("  Mutex locked:			%d\n",
			si_p->si_state.mutex_locked);
		break;
	case TD_SYNC_SEMA:
		diag_print("  Semaphore count:		%d\n",
			si_p->sema_count);
		break;
	case TD_SYNC_RWLOCK:
		diag_print("  Reader count:		%d\n",
			si_p->si_state.nreaders);
		break;
	default:
		diag_print("  BAD SO TYPE - BAD SO TYPE - BAD SO TYPE\n");
	}
	diag_print("  Size:				%x\n",
		si_p->si_size);
	diag_print("  Has waiters:			%d\n",
		si_p->si_has_waiters);
	diag_print("  Thread owner struct addr:	%d\n",
		si_p->si_owner.th_unique);
	diag_print("  Optional data ptr:		%x\n",
		si_p->si_data);

	diag_print("  Statistics:\n");
	diag_print("    Waiters:			%d\n",
		si_p->si_stats.waiters);
	diag_print("    Contention:			%d\n",
		si_p->si_stats.contention);
	diag_print("    Acquires:			%d\n",
		si_p->si_stats.acquires);
	diag_print("    Wait time:			%d\n",
		si_p->si_stats.waittime);

}
#endif
