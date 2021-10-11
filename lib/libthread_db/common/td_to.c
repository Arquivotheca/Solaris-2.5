/*
*
*ident  "@(#)td_to.c 1.73     95/05/25 SMI"
*
* Copyright 1993, 1994 by Sun Microsystems, Inc.
*
*/

/*
* Description:
*	This module contains functions for interacting with
* the threads within the program.
*/


#ifdef __STDC__
#pragma weak td_thr_validate = __td_thr_validate  /* i386 work around */
#pragma weak td_thr_tsd = __td_thr_tsd  /* i386 work around */
#pragma weak td_thr_get_info = __td_thr_get_info
#pragma weak td_thr_sigsetmask = __td_thr_sigsetmask  /* i386 work around */
#pragma weak td_thr_setprio = __td_thr_setprio  /* i386 work around */
#pragma weak td_thr_setsigpending = __td_thr_setsigpending /* i386work around */
#ifdef PHASE2
#pragma weak td_thr_lockowner
#pragma weak td_thr_sleepinfo
#pragma weak td_thr_dbsuspend
#pragma weak td_thr_dbresume
#endif /* END PHASE2 */
#pragma weak td_ta_map_id2thr = __td_ta_map_id2thr

#pragma weak td_thr_getfpregs = __td_thr_getfpregs  /* i386 work around */
#pragma weak td_thr_setfpregs = __td_thr_setfpregs  /* i386 work around */

#define	V8PLUS_SUPPORT

#pragma weak td_thr_getxregsize = __td_thr_getxregsize  /* i386 work around */
#pragma weak td_thr_setxregs = __td_thr_setxregs  /* i386 work around */
#pragma weak td_thr_getxregs = __td_thr_getxregs  /* i386 work around */
#endif				/* __STDC__ */

#include <thread_db.h>
#include "thread_db2.h"

#include "td.h"
#include "td_to_impl.h"
#include "xtd_to.h"
#include "td.extdcl.h"
#include "xtd.extdcl.h"


/*
*   This structure is used to create a single parameter to pass to
* a function that writes signal information to a thread struct.
*/
struct td_thr_sigsetinfo_param {
	uchar_t	sigflag;
	td_thr_siginfo_t	*siginfo_p;
};
typedef struct td_thr_sigsetinfo_param td_thr_sigsetinfo_param_t;

struct td_mapper_param {
	thread_t	tid;
	int		found;
	td_thrhandle_t	th;
};
typedef struct td_mapper_param td_mapper_param_t;

static td_err_e
td_read_thread_tsd(const td_thrhandle_t *th_p, tsd_t *thr_tsd_p,
	struct tsd_common * tsd_common_p, void **tsd_array_ptr);
static td_terr_e
td_thr2to_const(td_thragent_t *ta_p, paddr_t ts_addr,
	uthread_t * thr_struct_p, td_thrinfo_t *ti_p);
static td_terr_e
td_thr2to_var(uthread_t * thr_struct_p, td_thrinfo_t *ti_p);
static td_terr_e
td_thr2to(td_thragent_t *ta_p, paddr_t ts_addr, uthread_t * thr_struct_p,
	td_thrinfo_t *ti_p);
static td_err_e
td_thr_get_const_info(const td_thrhandle_t *th_p, td_thrinfo_t *ti_p);
static td_err_e
td_thr_get_var_info(const td_thrhandle_t *th_p, td_thrinfo_t *ti_p);
static td_terr_e
__td_thr_struct_set(const td_thrhandle_t *th_p, td_set_func_t set_func,
	void *set_func_data);
static td_terr_e
td_thr_struct_sigsetmask(uthread_t * thr_struct_p, void *ti_sigmask_p);
static td_terr_e
td_thr_struct_setprio(uthread_t * thr_struct_p, void *prio_p);
static int
td_searcher(const td_thrhandle_t *th_p, void *data);
static int
td_mapper_id2thr(const td_thrhandle_t *th_p, td_mapper_param_t *data);
static void
td_ucontext_dump(const ucontext_t * uc_p);
static void
td_siginfo_dump(const siginfo_t * sig_p);

/* Macros used in calls that would otherwise be > 80 characters. */
#define	TDT_M1 "malloc() failed - td_read_thread_tsd"
#define	TDT_M2 "Writing FP information: td_thr_getfpregs"

static	td_err_e
/*
* Description:    Read thread specific data information.
*
* Input:
*   *th_p - thread handle
*
* Output:
*   *thr_tsd_p - tsd count and address of array of tsd keys
*   *tsd_common - common tsd information
*   **tsd_array_ptr - array of tsd values(pointers)
*   td_read_thread_tsd - return value
*
* Side effects:
*   Space is allocated for *tsd_array_ptr and should
* be released by caller.
*
* Notes:
*   Though "thr_tsd_p" seemingly contains "tsd_array_ptr"
* both are provided separately because "thr_tsd_p" only
* contains a pointer to an address in the target process.
* "tsd_array_ptr" contains the actual array.
*   In libthread, tsd_thread may be #define'ed to
* a reference to "t_tls" in the current threads thread struct.
*/
td_read_thread_tsd(const td_thrhandle_t *th_p, tsd_t *thr_tsd_p,
	struct tsd_common * tsd_common_p, void **tsd_array_ptr)
{
	td_err_e	return_val = TD_OK;
	ps_err_e	db_return;
	td_terr_e	td_return;
	paddr_t	key_array_addr;
	int		tsd_size;
	uthread_t	thr_struct;
	struct ps_prochandle	*ph_p;
	td_thragent_t	*ta_p;

	ta_p = th_p->th_ta_p;
	ph_p = ta_p->ph_p;

	/*
	 * Extract the thread struct address from the thread handle and read
	 * the thread struct.
	 */

#ifdef TLS

	/*
	 * I have not covered this option because I don't know that it really
	 * is an option.  But if it is, this will not compile.
	 */
	? ? ? ? ? ? ? ? ? ?
#endif

	td_return =
		__td_read_thr_struct(th_p->th_ta_p,
		th_p->th_unique, &thr_struct);

	if (td_return == TD_TOK) {

		/*
		 * tsd_thread holds the TSD count and a pointer array to TSD
		 * data for a thread. tsd_common holds information about
		 * number of keys used.
		 */

		*tsd_array_ptr = NULL;
		*tsd_common_p = NULL_TSD_COMMON;
		*thr_tsd_p = NULL_TSD_T;

		/*
		 * The pointer to thread tsd is in t_tls field of a thread
		 * struct. If t_tls is NIL, there is no thread specific data
		 * for this thread.
		 */
		if (thr_struct.t_tls) {
			db_return = ps_pdread(ph_p,
				(paddr_t) thr_struct.t_tls,
				(char *) thr_tsd_p, sizeof (*thr_tsd_p));

			if (db_return == PS_OK) {

				key_array_addr = (paddr_t)
					thr_tsd_p->array;

				/*
				 * If "key_array_addr" is NULL, then nothing
				 * has been allocated for this thread.
				 */
				if (key_array_addr) {

					/*
					 * Allocate storage to hold TSD array
					 * for this this thread only and read
					 * TSD array.
					 */
					tsd_size = sizeof (void *)
						* thr_tsd_p->count;
					*tsd_array_ptr =
						(void *) malloc(tsd_size);

					if (*tsd_array_ptr) {

						/*
						 * Calculate the address of
						 * the array of values for
						 * this thread.
						 */
						db_return =
							ps_pdread(ph_p,
							key_array_addr,
							*tsd_array_ptr,
							tsd_size);
						if (db_return == PS_OK) {

							/*
							 * Get tsd_common to
							 * check number of
							 * keys used.
							 */
							return_val =
							    __td_dread_process(
							    th_p->th_ta_p,
							    TD_TSD_COMMON_NAME,
							    tsd_common_p,
							    sizeof (tsd_common))
								;
						} else {
							free(*tsd_array_ptr);
							return_val = TD_ERR;
						}
					} else {
						return_val = TD_ERR;
						__td_report_to_err(TD_TERR,
							TDT_M1);
					}
				} else {
					return_val = TD_NOTSD;
				}
			} else {
				return_val = TD_DBERR;
			}
		} else {
			return_val = TD_NOTSD;
		}
	} else {
		return_val = TD_ERR;
	}

	return (return_val);

}


/*
* Description:
*   Check the struct thread address in *th_p again first
* value in "data".  If value in data is found, set second value
* in "data" to TRUE and return 1 to terminate iterations.
*   This function is used by td_thr_validate() to verify that
* a thread handle is valid.
*
* Input:
*   *th_p - thread handle
*   data[0] - struct thread address being sought
*	flag indicating struct thread address found
*
* Output:
*   td_searcher - returns 1 if thread struct address found
*   data[1] - flag indicating struct thread address found
*
* Side effects:
*/
static int
td_searcher(const td_thrhandle_t *th_p, void *data)
{
	int	return_val = 0;

	if (((int *) data)[0] == th_p->th_unique) {
		((int *) data)[1] = TRUE;
		return_val = 1;
	}
	return (return_val);
}


/*
* Description:
*   Validate the thread handle.  Check that
* a thread exists in the thread agent/process that
* corresponds to thread with handle *th_p.
*
* Input:
*   *th_p - thread handle
*
* Output:
*   td_thr_validate - return value
*	return value == TD_OK implies thread handle is valid
* 	return value == TD_NOTHR implies thread handle not valid
* 	return value == other implies error
*
* Side effects:
*   none
*   Imported functions called:
* ps_pdread, ps_pstop, ps_pcontinue.
*
*/
td_err_e
__td_thr_validate(const td_thrhandle_t *th_p)
{
	td_err_e	return_val;
	int		searcher_data[] = {0, FALSE};

#ifdef TEST_PS_CALLS
	if (td_noop) {
		(void) ps_pstop(0);

		(void) ps_pdread(0, 0, 0, 0);
		(void) ps_pcontinue(0);
		return (TD_OK);
	}
#endif

	/*
	 * check for valid thread handle pointer
	 */
	if (th_p == NULL) {
		return_val = TD_BADTH;
		return (return_val);
	}

	/*
	 * Check for valid thread handle - check for NULLs
	 */
	if ((th_p->th_unique == NULL) || (th_p->th_ta_p == NULL)) {
		return_val = TD_BADTH;
		return (return_val);
	}

	/*
	 * LOCKING EXCEPTION - Locking is not required
	 * here because no use of the thread agent is made (other
	 * than the sanity check) and checking of the thread
	 * agent will be
	 * done in __td_ta_thr_iter.  If __td_ta_thr_iter
	 * is not used or if some use of the thread agent
	 * is made other than the sanity checks, ADD
	 * locking.
	 */

	/*
	 * Use thread iterator.
	 */
	searcher_data[0] = th_p->th_unique;
	return_val = __td_ta_thr_iter(th_p->th_ta_p,
		td_searcher, &searcher_data,
		TD_THR_ANY_STATE, TD_THR_LOWEST_PRIORITY,
		TD_SIGNO_MASK, TD_THR_ANY_USER_FLAGS);

	if (return_val != TD_OK) {
		__td_report_po_err(return_val,
			"Iterator failed in td_thr_validate()");
	} else {
		if (searcher_data[1] == TRUE) {
			return_val = TD_OK;
		} else {
			return_val = TD_NOTHR;
		}
	}

	return (return_val);

}

/*
* Description:
*   Get a thread's  private binding to a given thread specific
* data(TSD) key(see thr_getspecific(3T).  If the thread doesn't
* have a binding for a particular key, then NULL is returned.
*
* Input:
*   *th_p - thread handle
*   key - TSD key
*
* Output:
*   data_pp - key value for given thread. This value
*	is typically a pointer.  It is NIL if
*	there is no TSD data for this thread.
*   td_thr_tsd - return value
*
* Side effects:
*   none
*   Imported functions called: ps_pglobal_lookup,
* ps_pdread, ps_pstop, ps_pcontinue.
*/
td_err_e
__td_thr_tsd(const td_thrhandle_t *th_p, const thread_key_t key,
	void **data_pp)
{
	tsd_t tsd_thread = NULL_TSD_T;
	struct tsd_common tsd_common = NULL_TSD_COMMON;
	td_err_e	return_val = TD_OK;

	void	*tsd_array_ptr = 0;

	/*
	 * I followed the code for libthread thr_getspecific().
	 */
#ifdef TEST_PS_CALLS
	if (td_noop) {
		(void) ps_pstop(0);
		(void) ps_pcontinue(0);
		(void) ps_pglobal_lookup(0, 0, 0, 0);
		(void) ps_pdread(0, 0, 0, 0);
		return (TD_OK);
	}
#endif

	/*
	 * Sanity checks.
	 */
	if (th_p == NULL) {
		return_val = TD_BADTH;
		return (return_val);
	}
	if ((th_p->th_ta_p == NULL) || (th_p->th_unique == NULL)) {
		return_val = TD_BADTA;
		return (return_val);
	}

	/*
	 * Get a reader lock.
	 */
	if (rw_rdlock(&(th_p->th_ta_p->rwlock))) {
		return_val = TD_ERR;
		return (return_val);
	}

	if (th_p->th_ta_p->ph_p == NULL) {
		rw_unlock(&(th_p->th_ta_p->rwlock));
		return_val = TD_BADTA;
		return (return_val);
	}

	/*
	 * tsd_thread holds the TSD count and an array of pointers to TSD
	 * data. tsd_common holds information about number of keys used.
	 */

	if (key == 0) {
		rw_unlock(&(th_p->th_ta_p->rwlock));
		return (TD_BADKEY);
	} else {

		/*
		 * More than 1 byte is geing read.  Stop the process.
		 */
		if (ps_pstop(th_p->th_ta_p->ph_p) == PS_OK) {

			/*
			 * Read struct containing TSD count and pointer to
			 * storage.
			 */
			return_val = td_read_thread_tsd(th_p,
				&tsd_thread, &tsd_common, &tsd_array_ptr);

			if (return_val == TD_OK) {

				/*
				 * If key is greater than TSD count but less
				 * than TSD nkeys, then request is valid but
				 * there is no data.  If key is greater than
				 * TSD nkeys, then the request is invalid.
				 */
				if (key > tsd_thread.count) {
					if (key > tsd_common.nkeys) {
						return_val = TD_BADKEY;
						__td_report_po_err(return_val,
						"TSD common - td_thr_tsd");
					} else {

						/*
						 * There is no TSD for this
						 * thread.
						 */
						*data_pp = 0;
					}
				} else {

					/*
					 * Extract the TSD for this thread
					 * from the array of TSD(.i.e.,
					 * data[key-1])
					 */
					*data_pp = (void *) ((paddr_t *)
						tsd_array_ptr)[key - 1];
				}
			} else if (return_val == TD_NOTSD) {
				*data_pp = 0;
			} else {
				*data_pp = 0;
				return_val = TD_ERR;
			}

			/*
			 * Continue process.
			 */

			if (ps_pcontinue(th_p->th_ta_p->ph_p) != PS_OK) {
				return_val = TD_DBERR;
			}
		}	/* ps_pstop succeeded */
		else {
			return_val = TD_DBERR;
		}

	}	/* key valid  */

	if (tsd_array_ptr) {
		(void) free(tsd_array_ptr);
	}

	rw_unlock(&(th_p->th_ta_p->rwlock));
	return (return_val);

}


/*
* Description:
*   Write the value of a variable.
*
* Input:
*   *ta_p - thread agent
*   *symbol_name - name of symbol to be read
*   buffer - location to holding contents of symbol
*   size - size of symbol
*
* Output:
*   __td_dwrite_process - return value
*
* Side effects:
*/
td_err_e
__td_dwrite_process(const td_thragent_t *ta_p, char *symbol_name, void *buffer,
	int size)
{
	char		error_msg[TD_MAX_BUFFER_SIZE];
	paddr_t	symbol_addr;
	td_err_e	return_val;
	ps_err_e	db_return;
	struct ps_prochandle *ph_p;

	/*
	 * Get address of symbol out of libthread.so
	 */

	ph_p = ta_p->ph_p;
	db_return = ps_pglobal_lookup(ph_p, TD_LIBTHREAD_NAME,
		symbol_name, (paddr_t *) & symbol_addr);

	if (db_return == PS_OK) {

		/*
		 * Write process
		 */
		db_return = ps_pdwrite(ph_p, symbol_addr,
			(char *) buffer, size);

		if (db_return != PS_OK) {
			strcpy(error_msg, "__td_dwrite_process - ");
			strcpy(&error_msg[strlen(error_msg)], symbol_name);
			__td_report_db_err(db_return, error_msg);
			return_val = TD_DBERR;
		} else {
			return_val = (TD_OK);
		}
	} else {
		return_val = TD_DBERR;
		__td_report_po_err(return_val,
			"Symbol lookup failed - __td_dwrite_process");
	}

	return (return_val);
}


/*
* Description:
*   Read the value of a variable.
*
* Input:
*   *ta_p - thread agent
*   *symbol_name - name of symbol to be read
*   buffer - location to hold contents of symbol
*   size - size of symbol
*
* Output:
*   buffer - value of symbol
*   __td_dread_process - return value
*
* Side effects:
*/
td_err_e
__td_dread_process(const td_thragent_t *ta_p, char *symbol_name, void *buffer,
	int size)
{
	char		error_msg[TD_MAX_BUFFER_SIZE];
	paddr_t	symbol_addr;
	td_err_e	return_val;
	ps_err_e	db_return;
	struct ps_prochandle *ph_p;

	ph_p = ta_p->ph_p;

	/*
	 * Get address of symbol out of libthread.so
	 */
	db_return = ps_pglobal_lookup(ph_p, TD_LIBTHREAD_NAME,
		symbol_name, (paddr_t *) & symbol_addr);

	if (db_return == PS_OK) {

		/*
		 * Read process
		 */
		db_return = ps_pdread(ph_p, symbol_addr, (char *) buffer, size);

		if (db_return != PS_OK) {
			strcpy(error_msg, "__td_dread_process - ");
			strcpy(&error_msg[strlen(error_msg)], symbol_name);
			__td_report_db_err(db_return, error_msg);
			return_val = TD_DBERR;
		} else {
			return_val = TD_OK;
		}
	} else {
		return_val = TD_DBERR;
		__td_report_po_err(return_val,
			"Symbol lookup failed - __td_dread_process");
	}

	return (return_val);
}



/*
* Description:
*   Write a thread structure.
*
* Input:
*   *ta_p - thread agent
*   thr_addr - address of thread structure in *td_thragent_t
*   *thr_struct_p - thread struct
*
* Output:
*   __td_write_thr_struct - return value
*
* Side effects:
*   Process is written
*/

td_terr_e
__td_write_thr_struct(td_thragent_t *ta_p,
	paddr_t thr_addr, uthread_t * thr_struct_p)
{
	td_terr_e	return_val = TD_TOK;
	struct ps_prochandle *ph_p;

	ph_p = ta_p->ph_p;

	if (return_val == TD_TOK) {
		if (ps_pdwrite(ph_p, thr_addr, (char *) thr_struct_p,
				sizeof (*thr_struct_p)) != PS_OK) {
			return_val = TD_TDBERR;
			__td_report_to_err(TD_TDBERR,
				"Writing thread struct: __td_write_thr_struct");
		}
	}

	return (return_val);

}


/*
* Description:
*   Read a thread structure.
*
* Input:
*   *ta_p - thread agent
*   thr_addr - address of thread structure in *ta_p
*
* Output:
*   *thr_struct_p - thread struct
*
* Side effects:
*   Process is read
*/
td_terr_e
__td_read_thr_struct(const td_thragent_t *ta_p, paddr_t thr_addr,
	uthread_t * thr_struct_p)
{
	td_terr_e	return_val = TD_TOK;
	struct ps_prochandle *ph_p;

	/*
	 * Extract the process handle and read the process.
	 */
	ph_p = ta_p->ph_p;

	if (ps_pdread(ph_p, thr_addr, (char *) thr_struct_p,
			sizeof (*thr_struct_p)) != PS_OK) {
		return_val = TD_TDBERR;
		__td_report_to_err(TD_TDBERR,
			"Reading thread struct: __td_read_thr_struct");
	}
	return (return_val);

}

/*
* Description:
*   Map state from threads struct to thread information states
*
* Input:
*   ts_state - thread struct state
*
* Output:
*   *to_state - thread information state
*
* Side effects:
*   none
*/
td_terr_e
__td_thr_map_state(thstate_t ts_state, td_thr_state_e *to_state)
{
	td_terr_e	return_val = TD_TOK;

	switch (ts_state) {
	case TS_SLEEP:
		*to_state = TD_THR_SLEEP;
		break;
	case TS_RUN:
		*to_state = TD_THR_RUN;
		break;
	case TS_DISP:
		*to_state = TD_THR_ACTIVE;
		break;
	case TS_ONPROC:
		*to_state = TD_THR_ACTIVE;
		break;
	case TS_STOPPED:
		*to_state = TD_THR_STOPPED;
		break;
	case TS_ZOMB:
		*to_state = TD_THR_ZOMBIE;
		break;
#ifdef NEW_LIBTHREAD_SUPPORT
	case TS_STOPPED_ASLEEP:
		*to_state = TD_THR_STOPPED_ASLEEP;
		break;
#endif
	default:
		__td_report_to_err(TD_TSTATE,
			"Unknown state from thread struct: td_thr_map_state");
		return_val = TD_TSTATE;
	}

	return (return_val);
}

/*
* Description:
*   Transfer constant information from thread struct to
* thread information struct.
*
* Input:
*   *ta_p - thread agent
*   ts_addr - address of thread struct
*   *thr_struct_p - libthread thread struct
*
* Output:
*   *ti_p - thread information struct
*
* Side effects:
*   none
*/
static td_terr_e
td_thr2to_const(td_thragent_t *ta_p, paddr_t ts_addr,
	uthread_t * thr_struct_p, td_thrinfo_t *ti_p)
{
	td_terr_e	return_val = TD_TOK;

	/*
	 * Set td_to_ph_p_(*ti_p)
	 */
	ti_p->ti_ta_p = ta_p;

	ti_p->ti_user_flags = thr_struct_p->t_usropts;
	ti_p->ti_tid = thr_struct_p->t_tid;
	ti_p->ti_tls = thr_struct_p->t_tls;
	ti_p->ti_startfunc = thr_struct_p->t_startpc;
	ti_p->ti_stkbase = (paddr_t) thr_struct_p->t_stk;
	ti_p->ti_stksize = thr_struct_p->t_stksize;
	ti_p->ti_flags = thr_struct_p->t_flag;
	ti_p->ti_ro_area = ts_addr;
	ti_p->ti_ro_size = sizeof (uthread_t);

	return (return_val);
}

/*
* Description:
*   Transfer variable information from thread struct to
* thread information struct.
*
* Input:
*   *thr_struct_p - libthread thread struct
*
* Output:
*   *ti_p - thread information struct variable part
*
* Side effects:
*   none
*
* Assumptions:
*   Fields not set:
*	to_db_suspended, to_events, to_traceme
*/
static td_terr_e
td_thr2to_var(uthread_t * thr_struct_p, td_thrinfo_t *ti_p)
{
	td_terr_e	return_val = TD_TOK;
	td_thr_state_e	state;

	return_val = __td_thr_map_state(thr_struct_p->t_state, &state);
	ti_p->ti_state = state;

	/*
	 * td_to_db_suspended_( *ti_p ) always set to NULL.
	 */
	ti_p->ti_db_suspended = 0;

	ti_p->ti_type = TD_CONVERT_TYPE(*thr_struct_p);
	ti_p->ti_pc = thr_struct_p->t_pc;
	ti_p->ti_sp = thr_struct_p->t_sp;
	ti_p->ti_pri = thr_struct_p->t_pri;

	/*
	 * Non-Null lwp id is not always provided.  See notes in
	 * td_thr_get_var_info() header.
	 */
	if (ISVALIDLWP(thr_struct_p)) {
		ti_p->ti_lid = thr_struct_p->t_lwpid;
	} else {
		ti_p->ti_lid = 0;
	}
	ti_p->ti_sigmask = thr_struct_p->t_hold;

	/*
	 * td_to_events_( *ti_p ) always set to NULL.
	 * td_to_traceme_( *ti_p ) always set to NULL
	 */
	ti_p->ti_traceme = 0;
	eventemptyset(&(ti_p->ti_events));

	ti_p->ti_preemptflag = thr_struct_p->t_preempt;
#ifdef PHASE2
	/*
	 * Set the priority inversion flag in the thread information struct
	 * from the thread struct.
	 */
#else
	memset(&(ti_p->ti_pirecflag), 0,
		sizeof (ti_p->ti_pirecflag));
#endif

	/*
	 * Set pending signal bits only if t_pending is set.
	 */
	if (thr_struct_p->t_pending) {
		ti_p->ti_pending = thr_struct_p->t_psig;
	} else {
		sigemptyset(&(ti_p->ti_pending));
	}

	return (return_val);
}

/*
* Description:
*   Transfer information from thread struct to thread information struct.
*
* Input:
*   *ta_p - thread agent
*   ts_addr - address of thread struct
*   *thr_struct_p - libthread thread struct
*
* Output:
*   *ti_p - thread information struct
*
* Side effects:
*   none
*/
static td_terr_e
td_thr2to(td_thragent_t *ta_p, paddr_t ts_addr,
	uthread_t * thr_struct_p, td_thrinfo_t *ti_p)
{
	td_terr_e	return_val = TD_TOK;

	return_val = td_thr2to_const(ta_p, ts_addr,
		thr_struct_p, ti_p);

	if (return_val == TD_TOK) {
		return_val = td_thr2to_var(thr_struct_p, ti_p);
	}
	return (return_val);
}

/*
* Description:
*   Read the threads hash table for the debug process.
*
* Input:
*   *ta_p - thread agent
*
* Output:
*   *tab_p[] - hash table
*
* Side effects:
*   Debug process is read.
*/
td_err_e
__td_read_thread_hash_tbl(const td_thragent_t *ta_p, thrtab_t * tab_p,
	int tab_size)
{
	td_err_e	return_val;
	ps_err_e	db_return;
	char		error_msg[TD_MAX_BUFFER_SIZE];
	paddr_t		symbol_addr;

	symbol_addr = ta_p->hash_tab_addr;

	db_return = ps_pdread(ta_p->ph_p, symbol_addr,
		(char *) tab_p, tab_size);
	if (db_return != PS_OK) {
		strcpy(error_msg, "__td_read_thread_hash_tbl - ");
		strcpy(&error_msg[strlen(error_msg)], TD_HASH_TAB_NAME);
		__td_report_db_err(db_return, error_msg);
		return_val = TD_DBERR;
	} else {
		return_val = TD_OK;
	}

	return (return_val);
}

/*
* Description:
*   Update the constant part of
* thread information struct. Constant fields in
* a thread information struct will be updated to be
* consistent with properties of its respective
* thread.
*
* Input:
*   *th_p - thread handle with valid ti_tid and ti_ta_p fields
*
* Output: *ti_p - updated thread information struct
*   td_thr_get_const_info - return value
*
* Side effects:
*   none
*/
static td_err_e
td_thr_get_const_info(const td_thrhandle_t *th_p,
	td_thrinfo_t *ti_p)
{
	uthread_t	thr_struct;
	td_err_e	return_val = TD_ERR;
	td_terr_e	td_return = TD_TERR;

	/*
	 * Extract the thread struct address from the thread handle and read
	 * the thread struct.  Transfer the thread struct to
	 * the thread information
	 * struct.  Check that the thread id is correct.
	 */

	td_return = __td_read_thr_struct(th_p->th_ta_p,
		th_p->th_unique, &thr_struct);


	if (td_return == TD_TOK) {
		td_return = td_thr2to_const(th_p->th_ta_p,
			th_p->th_unique, &thr_struct, ti_p);
		if (td_return == TD_TOK) {
			return_val = TD_OK;
		} else {
			return_val = TD_ERR;
		}
	}
	return (return_val);
}


/*
* Description:
*   Update the variable part of
* thread information struct. Variable fields in
* a thread information struct will be updated to be
* consistent with properties of its respective
* thread.
*
* Input:
*   *th_p - thread handle with valid ti_tid and ti_ta_p fields
*
* Output:
*   *ti_p - updated thread information struct
*   td_thr_get_var_info - return value
*
* Side effects:
*   none
*
* Notes:
*   The lwp id field of a thread information struct is update to
* an non-Null value only if the thread is active on an lwp,
* bound to an lwp, or parked on an lwp.
*/
static td_err_e
td_thr_get_var_info(const td_thrhandle_t *th_p, td_thrinfo_t *ti_p)
{
	uthread_t	thr_struct;
	td_err_e	return_val = TD_ERR;
	td_terr_e	td_return = TD_TERR;


	/*
	 * Extract the thread struct address from the thread handle and read
	 * the thread struct.  Transfer the thread struct to
	 * the thread information
	 * struct.  Check that the thread id is correct.
	 */

	td_return = __td_read_thr_struct(th_p->th_ta_p,
		th_p->th_unique, &thr_struct);

	if (td_return == TD_TOK) {
		td_return = td_thr2to_var(&thr_struct, ti_p);
		if (td_return == TD_TOK) {
			return_val = TD_OK;
		} else {
			return_val = TD_ERR;
		}
	}
	return (return_val);
}

/*
* Description:
*	   Update the thread information struct. All fields in a thread
*	information structure(td_thrinfo_t) will be updated to be
*	consistent with properties of its respective thread.
*
* Input:
*	   *th_p - thread handle
*
* Output:
*	   *ti_p - updated thread information structure
*	   td_thr_get_info - return value
*
* Side effects:
*	   none
*	   Imported functions called: ps_pdread, ps_pstop,
*	ps_pcontinue.
*
*/
td_err_e
__td_thr_get_info(const td_thrhandle_t *th_p, td_thrinfo_t *ti_p)
{
	uthread_t	thr_struct;
	td_err_e	return_val = TD_ERR;
	td_terr_e	td_return = TD_TERR;

#ifdef TEST_PS_CALLS
	if (td_noop) {
		(void) ps_pstop(0);
		(void) ps_pcontinue(0);
		(void) ps_pdread(0, 0, 0, 0);
		return (TD_OK);
	}
#endif

	/*
	 * Sanity checks.
	 */
	if (th_p == NULL) {
		return_val = TD_BADTH;
		return (return_val);
	}
	if ((th_p->th_ta_p == NULL) || (th_p->th_unique == NULL)) {
		return_val = TD_BADTA;
		return (return_val);
	}
	if (ti_p == NULL) {
		return_val = TD_ERR;
		return (return_val);
	}

	/*
	 * Get a reader lock.
	 */
	if (rw_rdlock(&(th_p->th_ta_p->rwlock))) {
		return_val = TD_ERR;
		return (return_val);
	}

	if (th_p->th_ta_p->ph_p == NULL) {
		rw_unlock(&(th_p->th_ta_p->rwlock));
		return_val = TD_BADTA;
		return (return_val);
	}


	/*
	 * Null out the thread information struct.
	 */
	memset(ti_p, NULL, sizeof (*ti_p));

	/*
	 * More than 1 byte is geing read.  Stop the process.
	 */
	if (ps_pstop(th_p->th_ta_p->ph_p) == PS_OK) {

		/*
		 * Extract the thread struct address from the
		 * thread handle and read the thread struct.
		 * Transfer the thread struct to
		 * the thread information struct.  Check that
		 * the thread id is correct.
		 */

		td_return = __td_read_thr_struct(th_p->th_ta_p,
			th_p->th_unique,
			&thr_struct);

		if (td_return == TD_TOK) {
			td_return = td_thr2to(th_p->th_ta_p,
				th_p->th_unique, &thr_struct, ti_p);
			if (td_return == TD_TOK) {
				return_val = TD_OK;
			} else {
				return_val = TD_ERR;
			}
		}

		/*
		 * Continue process.
		 */
		if (ps_pcontinue(th_p->th_ta_p->ph_p) != PS_OK) {
			return_val = TD_DBERR;
		}
	/* ps_pstop succeeded   */
	} else {
		return_val = TD_DBERR;
	}

	rw_unlock(&(th_p->th_ta_p->rwlock));
	return (return_val);
}

/*
* Description:
*   Get the floating point registers for the given thread.
* The floating registers are only available for a thread executing
* on and LWP.  If the thread is not running on an LWP, this
* function will return TD_NOFPREGS.
*
* Input:
*   *th_p - thread handle for which fp registers are
*		being requested
*
* Output:
*   *fpregset - floating point register values(see sys/procfs.h)
*   td_thr_getfpregs - return value
*
* Side effects:
*   none
*   Imported functions called:
*/
td_err_e
__td_thr_getfpregs(const td_thrhandle_t *th_p, prfpregset_t * fpregset)
{
	td_err_e	return_val = TD_OK;
	td_terr_e	td_return;
	uthread_t	thr_struct;

#ifdef TEST_PS_CALLS
	if (td_noop) {
		(void) ps_pstop(0);
		(void) ps_pcontinue(0);
		(void) ps_pdread(0, 0, 0, 0);
		ps_lgetfpregs(0, 0, 0);
		return (TD_OK);
	}
#endif

	/*
	 * Sanity checks.
	 */
	if (th_p == NULL) {
		return_val = TD_BADTH;
		return (return_val);
	}
	if ((th_p->th_ta_p == NULL) || (th_p->th_unique == NULL)) {
		return_val = TD_BADTA;
		return (return_val);
	}
	if (fpregset == NULL) {
		return_val = TD_DBERR;
		return (return_val);
	}

	/*
	 * Get a reader lock.
	 */
	if (rw_rdlock(&(th_p->th_ta_p->rwlock))) {
		return_val = TD_ERR;
	}

	if (th_p->th_ta_p->ph_p == NULL) {
		rw_unlock(&(th_p->th_ta_p->rwlock));
		return_val = TD_BADTA;
		return (return_val);
	}


	/*
	 * More than 1 byte is geing read.  Stop the process.
	 */

	if (ps_pstop(th_p->th_ta_p->ph_p) == PS_OK) {

		/*
		* Extract the thread struct address from
		* the thread handle and read
		* the thread struct.
		*/

		td_return = __td_read_thr_struct(th_p->th_ta_p,
			th_p->th_unique, &thr_struct);

		if (td_return == TD_TOK) {

			if (ISVALIDLWP(&thr_struct) &&
				HASVALIDFP(&thr_struct)) {
				/*
				 * Read the floating point registers
				 * using the imported interface.
				 */
				if (ps_lgetfpregs(th_p->th_ta_p->ph_p,
						thr_struct.t_lwpid, fpregset) ==
						PS_ERR) {
					return_val = TD_DBERR;
				}
			} else {

				/*
				* fp registers not available in thread struct.
				*/
				return_val = TD_NOFPREGS;
			}
		} else {
			return_val = TD_ERR;
		}

		/*
		 * Continue process.
		 */
		if (ps_pcontinue(th_p->th_ta_p->ph_p) != PS_OK) {
			return_val = TD_DBERR;
		}
	}
	/* ps_pstop succeeded   */
	else {
		return_val = TD_DBERR;
	}

	rw_unlock(&(th_p->th_ta_p->rwlock));
	return (return_val);
}

/*
* Description:
*   Get the size of the extra register set for the given thread.
* The extra registers are only available for a thread executing
* on and LWP.  If the thread is not running on an LWP, this
* function will return TD_NOXREGS.
*
* Input:
*   *th_p - thread handle for which the extra register set size
* is being requested.
*
* Output:
*   *xregsize - size of the extra register set.
*   td_thr_getxregsize - return value.
*
* Side Effect:
*   none
*   Imported functions called:
*/
td_err_e
__td_thr_getxregsize(const td_thrhandle_t *th_p, int *xregsize)
{

	td_err_e	return_val = TD_OK;
	td_terr_e	td_return;
	uthread_t	thr_struct;

#ifdef TEST_PS_CALLS
	if (td_noop) {
		(void) ps_pstop(0);
		(void) ps_pcontinue(0);
		(void) ps_pdread(0, 0, 0, 0);
		(void) ps_pdwrite(0, 0, 0, 0);
		(void) ps_lgetxregsize(0, 0, 0);
		return (TD_OK);
	}
#endif

	/*
	 * Sanity checks.
	 */
	if (th_p == NULL) {
		return_val = TD_BADTH;
		return (return_val);
	}
	if ((th_p->th_ta_p == NULL) || (th_p->th_unique == NULL)) {
		return_val = TD_BADTA;
		return (return_val);
	}
	if (xregsize == NULL) {
		return_val = TD_ERR;
		return (return_val);
	}

	/*
	 * Get a reader lock.
	 */
	if (rw_rdlock(&(th_p->th_ta_p->rwlock))) {
		return_val = TD_ERR;
	}

	if (th_p->th_ta_p->ph_p == NULL) {
		rw_unlock(&(th_p->th_ta_p->rwlock));
		return_val = TD_BADTA;
		return (return_val);
	}

	/*
	 * More than 1 byte is geing read.  Stop the process.
	 */

	if (ps_pstop(th_p->th_ta_p->ph_p) == PS_OK) {

		/*
		* Extract the thread struct address
		* from the thread handle and read
		* the thread struct.
		*/

		td_return = __td_read_thr_struct(th_p->th_ta_p,
			th_p->th_unique, &thr_struct);

		if (td_return == TD_TOK) {

			/*
			* Read the extra registers using the imported
			* interface.
			*/

			if (ISVALIDLWP(&thr_struct)) {
				if (ps_lgetxregsize(th_p->th_ta_p->ph_p,
					thr_struct.t_lwpid, xregsize) ==
					PS_ERR) {
					return_val = TD_DBERR;
				}
				if (!HASVALIDFP(&thr_struct)) {
					return_val = TD_NOFPREGS;
				}
			} else {
				return_val = TD_NOFPREGS;
			}
		} else {
			return_val = TD_ERR;
		}
		/*
		 * Continue process.
		 */
		if (ps_pcontinue(th_p->th_ta_p->ph_p) != PS_OK) {
			return_val = TD_DBERR;
		}
	}
	/* ps_pstop succeeded   */
	else {
		return_val = TD_DBERR;
	}

	rw_unlock(&(th_p->th_ta_p->rwlock));
	return (return_val);
}

/*
* Description:
*   Get the extra registers for the given thread.  Extra register can
* only be gotten for a thread executing on an LWP.  This
* operation will return TD_NOFPREGS
* for thread not on an LWP.
*
* Input:
*   *th_p - thread handle for thread on which extra registers
* are being requested.
*
* Output:
*   *xregset - extra register set, see sys/procfs.h.
*   td_thr_getxregs - return value
*
* Side Effect:
*   None
*   Imported function called: ps_pstop, ps_pcontinue, ps_pdread,
* ps_pdwrite, ps_lgetxregs.
*/
td_err_e
__td_thr_getxregs(const td_thrhandle_t *th_p, const caddr_t xregset)
{
	td_err_e	return_val = TD_OK;
	td_terr_e	td_return;
	uthread_t	thr_struct;

#ifdef TEST_PS_CALLS
	if (td_noop) {
		(void) ps_pstop(0);
		(void) ps_pcontinue(0);
		(void) ps_pdread(0, 0, 0, 0);
		(void) ps_pdwrite(0, 0, 0, 0);
		(void) ps_lgetxregs(0, 0, 0);
		return (TD_OK);
	}
#endif

	/*
	 * Sanity checks.
	 */
	if (th_p == NULL) {
		return_val = TD_BADTH;
		return (return_val);
	}
	if ((th_p->th_ta_p == NULL) || (th_p->th_unique == NULL)) {
		return_val = TD_BADTA;
		return (return_val);
	}
	if (xregset == NULL) {
		return (TD_ERR);
	}

	/*
	 * Get a reader lock.
	 */
	if (rw_rdlock(&(th_p->th_ta_p->rwlock))) {
		return_val = TD_ERR;
	}

	if (th_p->th_ta_p->ph_p == NULL) {
		rw_unlock(&(th_p->th_ta_p->rwlock));
		return_val = TD_BADTA;
		return (return_val);
	}

	/*
	 * More than 1 byte is geing read.  Stop the process.
	 */

	if (ps_pstop(th_p->th_ta_p->ph_p) == PS_OK) {
		/*
		* Extract the thread struct address
		* from the thread handle and read
		* the thread struct.
		*/

		td_return = __td_read_thr_struct(th_p->th_ta_p,
			th_p->th_unique, &thr_struct);

		if (td_return == TD_TOK) {

			/*
			* Read the x registers using the imported
			* interface.
			*/

			if (ISVALIDLWP(&thr_struct)) {
				if (ps_lgetxregs(th_p->th_ta_p->ph_p,
					thr_struct.t_lwpid, xregset)
					== PS_ERR) {
					return_val = TD_DBERR;
				}
				if (!HASVALIDFP(&thr_struct)) {
					/*
					 * Let user know floating pointer
					 * registers are not available.
					 */
					return_val = TD_NOFPREGS;
				}
			} else {
				return_val = TD_NOXREGS;
			}
		} else {
			return_val = TD_ERR;
		}
		/*
		 * Continue process.
		 */
		if (ps_pcontinue(th_p->th_ta_p->ph_p) != PS_OK) {
			return_val = TD_DBERR;
		}
	}
	/* ps_pstop succeeded   */
	else {
		return_val = TD_DBERR;
	}

	rw_unlock(&(th_p->th_ta_p->rwlock));
	return (return_val);
}

#ifdef PHASE2

/*
* Description:
*   If a thread is in the SLEEP state, its ti_p->ti_state ==
* TD_THR_SLEEP, then get the synchronization handle of the
* synchronization variable that this thread is asleep on.
*
* Input:
*   *th_p - thread handle
*
* Output:
*   *sh_p - synchronization handle on which thread is asleep
* if thread in SLEEP state, NULL otherwise.
*   td_thr_sleepinfo - return value
*
* Side effects:
*   none
*   Imported functions called: ps_pdread, ps_pstop,
* ps_pcontinue, ps_pglobal_lookup, ps_pdwrite.
*
*
*/
td_err_e
td_thr_sleepinfo(const td_thrhandle_t *th_p, td_synchandle_t *sh_p)
{
	td_err_e	return_val = TD_NOT_DONE;

	/*
	 * not currently supported by libthread
	 */

#ifdef TEST_PS_CALLS
	if (td_noop) {
		(void) ps_pstop(0);
		(void) ps_pcontinue(0);
		if (return_val != TD_NOT_DONE) {
			(void) ps_pglobal_lookup(0, 0, 0, 0);
			(void) ps_pdread(0, 0, 0, 0);
			(void) ps_pdwrite(0, 0, 0, 0);
		}
		return (TD_OK);
	}
#endif
	if (__td_pub_debug)
		diag_print(
		    "libthread_db: td_thr_sleepinfo() not fully implemented\n");

	/*
	 * Get a reader lock.
	 */
	if (rw_rdlock(&(th_p->th_ta_p->rwlock))) {
		return_val = TD_ERR;
	}

	if (th_p->th_ta_p->ph_p == NULL) {
		rw_unlock(&(th_p->th_ta_p->rwlock));
		return_val = TD_BADTA;
		return (return_val);
	}

	/*
	 * More than 1 byte is geing read.  Stop the process.
	 */
	if (ps_pstop(th_p->th_ta_p->ph_p) == PS_OK) {

		/*
		 * Set to avoid lint warning.
		 */
		sh_p->sh_ta_p = 0;

		/*
		 * Continue process.
		 */

		if (ps_pcontinue(th_p->th_ta_p->ph_p) != PS_OK) {
			return_val = TD_DBERR;
		}
	}
	/* ps_pstop succeeded   */
	else {
		return_val = TD_DBERR;
	}
	rw_unlock(&(th_p->th_ta_p->rwlock));
	return (TD_NOT_DONE);

}


/*
* Description:
*   Iterate over the set of locks owned by a specified thread.
* If cb returns a non-zero value, terminate iterations.
*
* Input:
*   *th_p - thread handle
*   *cb - function to be called on each lock owned by thread.
*   cb_data_p - pointer passed to td_sync_iter_f()
*
* Output:
*   td_thr_lockowner - return value
*
* Side effects:
*   *cb is called on each lock owned by thread.
*   Imported functions called: ps_pdread, ps_pstop,
* ps_pcontinue, ps_pglobal_lookup, ps_pdwrite.
*/
td_err_e
td_thr_lockowner(const td_thrhandle_t *th_p,
	td_sync_iter_f * cb, void *cb_data_p)
{
	td_err_e	return_val = TD_NOT_DONE;

	/*
	 * not currently supported by libthread
	 */

#ifdef TEST_PS_CALLS
	if (td_noop) {
		(void) ps_pstop(0);
		(void) ps_pcontinue(0);
		if (return_val != TD_NOT_DONE) {
			(void) ps_pglobal_lookup(0, 0, 0, 0);
			(void) ps_pdread(0, 0, 0, 0);
		}
		return (TD_OK);
	}
#endif

	/*
	 * Sanity checks.
	 */
	if (th_p == NULL) {
		return_val = TD_BADTH;
		return (TD_NOT_DONE);
	}
	if ((th_p->th_ta_p == NULL) || (th_p->th_unique == NULL)) {
		return_val = TD_BADTA;
		return (TD_NOT_DONE);
	}
	if (__td_pub_debug) diag_print(
		"libthread_db: td_thr_lockowner() not fully implemented\n");

	/*
	 * Get a reader lock.
	 */
	if (rw_rdlock(&(th_p->th_ta_p->rwlock))) {
		return_val = TD_ERR;
	}

	if (th_p->th_ta_p->ph_p == NULL) {
		rw_unlock(&(th_p->th_ta_p->rwlock));
		return_val = TD_BADTA;
		return (return_val);
	}

	/*
	 * More than 1 byte is geing read.  Stop the process.
	 */
	if (ps_pstop(th_p->th_ta_p->ph_p) == PS_OK) {

		/*
		 * This is just to fix lint warning.
		 */
		{
			const	td_synchandle_t *sh_p = 0;
			if (((int) sh_p) != 0) {
				cb(sh_p, cb_data_p);
			}
		}

		/*
		 * Continue process.
		 */

		if (ps_pcontinue(th_p->th_ta_p->ph_p) != PS_OK) {
			return_val = TD_DBERR;
		}
	}
	/* ps_pstop succeeded   */
	else {
		return_val = TD_DBERR;
	}

	rw_unlock(&(th_p->th_ta_p->rwlock));
	return (TD_NOT_DONE);

}

#endif	/* END PHASE2 */

/*
*
* Description:
*   Read a thead structure, change a field in the
* thread structure by calling a provided function, and
* write back the thread structure.
*
* Input:
*   *th_p - thread handle
*   set_func - function to call to set field in thread struct
*   set_func_data - data for set_func
*
* Output:
*   __td_thr_struct_set - return value
*
* Side effects:
*   Thread structure corresponding to *th_p is changed
*/
static td_terr_e
__td_thr_struct_set(const td_thrhandle_t *th_p,
	td_set_func_t set_func, void *set_func_data)
{
	paddr_t	ts_addr;
	uthread_t	thread_struct;
	struct ps_prochandle	*ph_p;
	td_terr_e	return_val = TD_TOK;


	/*
	 * Read the thread struct, reset the signal mask, and write back the
	 * thread struct.
	 */

	ts_addr = th_p->th_unique;
	ph_p = th_p->th_ta_p->ph_p;

	if (ps_pdread(ph_p, ts_addr, (char *) &thread_struct,
			sizeof (thread_struct)) != PS_OK) {
		return_val = TD_TDBERR;
		__td_report_to_err(return_val,
			"Thread struct read - td_thr_sigsetmask");
	} else {

		return_val = (set_func) (&thread_struct, set_func_data);

		if (return_val == TD_TOK) {

			if (ps_pdwrite(ph_p, ts_addr, (char *) &thread_struct,
					sizeof (thread_struct)) != PS_OK) {
				return_val = TD_TDBERR;
				__td_report_to_err(return_val,
				    "Thread struct write - td_thr_sigsetmask");
			}
		}
	}

	return (return_val);
}

/*
* Description:
*   Set the signal mask in the thread struct.
*
* Input:
*   *thr_struct_p - thread struct
*   sigmask - signal mask
*
* Output:
*   td_thr_struct_sigsetmask - return value
*
* Side effects:
*   none
*/
static td_terr_e
td_thr_struct_sigsetmask(uthread_t * thr_struct_p, void *ti_sigmask_p)
{
	thr_struct_p->t_hold = *((sigset_t *) ti_sigmask_p);

	return (TD_TOK);
}

/*
* Description:
*   Change a thread's signal mask to the value specified by
* ti_sigmask.
*
* Input:
*   *th_p - thread handle
*   ti_sigmask - new value of signal mask
*
* Output:
*   td_thr_sigsetmask - return value
*
* Side effects:
*   Thread corresponding to *th_p is assigned new signal mask.
*   Imported functions called: ps_pstop,
* ps_pcontinue, ps_pdwrite.
*/
td_err_e
__td_thr_sigsetmask(const td_thrhandle_t *th_p, const sigset_t ti_sigmask)
{
	td_err_e	return_val = TD_OK;
	uthread_t	*ts_p;
	paddr_t		sigmask_addr;

#ifdef TEST_PS_CALLS
	if (td_noop) {
		(void) ps_pstop(0);
		(void) ps_pcontinue(0);
		(void) ps_pdwrite(0, 0, 0, 0);
		return (return_val);
	}
#endif

	/*
	 * Sanity checks.
	 */
	if (th_p == NULL) {
		return_val = TD_BADTH;
		return (return_val);
	}
	if ((th_p->th_ta_p == NULL) || (th_p->th_unique == NULL)) {
		return_val = TD_BADTA;
		return (return_val);
	}

	/*
	 * Get a reader lock.
	 */
	if (rw_rdlock(&(th_p->th_ta_p->rwlock))) {
		return_val = TD_ERR;
	}

	if (th_p->th_ta_p->ph_p == NULL) {
		rw_unlock(&(th_p->th_ta_p->rwlock));
		return_val = TD_BADTA;
		return (return_val);
	}

	/*
	 * More than 1 byte is being written.  Stop the process.
	 */
	if (ps_pstop(th_p->th_ta_p->ph_p) == PS_OK) {

		/*
		 * Write the sigmask into the thread struct.
		 */
		ts_p = (uthread_t *) th_p->th_unique;
		sigmask_addr = (paddr_t) & (ts_p->t_hold);
		if (ps_pdwrite(th_p->th_ta_p->ph_p, sigmask_addr,
			(char *) &ti_sigmask, sizeof (ti_sigmask)) != PS_OK) {

			return_val = TD_DBERR;
		}

		/*
		 * Continue process.
		 */

		if (ps_pcontinue(th_p->th_ta_p->ph_p) != PS_OK) {
			return_val = TD_DBERR;
		}
	}
	/* ps_pstop succeeded   */
	else {
		return_val = TD_DBERR;
	}

	rw_unlock(&(th_p->th_ta_p->rwlock));
	return (return_val);
}

/*
* Description:
*   Set the thread priority in the thread struct.
*
* Input:
*   *thr_struct_p - thread struct
*   *prio_p - new thread priority
*
* Output:
*   td_thr_struct_setprio - return value
*
* Side effects:
*   none
*/
static td_terr_e
td_thr_struct_setprio(uthread_t * thr_struct_p, void *prio_p)
{

	thr_struct_p->t_pri = *((int *) prio_p);

	return (TD_TOK);
}

/*
* Description:
*   Change a thread's priority to the value specified by ti_pri.
*
* Input:
*   *th_p -  thread handle
*   ti_pri - new value of thread priority >= 0(see thr_setprio(3T))
*
* Output:
*   td_thr_setprio - return value
*
* Side effects:
*   Thread corresponding to *th_p is assigned new priority.
*   Imported functions called: ps_pdwrite.
*
*/
td_err_e
__td_thr_setprio(const td_thrhandle_t *th_p, const int ti_pri)
{
	uthread_t	*ts_p;
	paddr_t		pri_addr;
	td_err_e	return_val = TD_OK;

#ifdef TEST_PS_CALLS
	if (td_noop) {
		(void) ps_pdwrite(0, 0, 0, 0);
		return (return_val);
	}
#endif

	/*
	 * Sanity checks.
	 */
	if (th_p == NULL) {
		return_val = TD_BADTH;
		return (return_val);
	}
	if ((th_p->th_ta_p == NULL) || (th_p->th_unique == NULL)) {
		return_val = TD_BADTA;
		return (return_val);
	}

	/*
	 * Get a reader lock.
	 */
	if (rw_rdlock(&(th_p->th_ta_p->rwlock))) {
		return_val = TD_ERR;
	}
	if (th_p->th_ta_p->ph_p == NULL) {
		rw_unlock(&(th_p->th_ta_p->rwlock));
		return_val = TD_BADTA;
		return (return_val);
	}

	/*
	 * Set the priority in the thread struct.
	 */

	/*
	 * Only setting 1 byte.  Don't stop process.
	 */

	if ((ti_pri >= THREAD_MIN_PRIORITY) &&
			(ti_pri <= THREAD_MAX_PRIORITY)) {
		ts_p = (uthread_t *) th_p->th_unique;
		pri_addr = (paddr_t) & ts_p->t_pri;
		if (ps_pdwrite(th_p->th_ta_p->ph_p, pri_addr,
				(char *) &ti_pri, sizeof (ti_pri)) != PS_OK) {
			return_val = TD_DBERR;
		}
	} else {
		return_val = TD_ERR;
	}

	rw_unlock(&(th_p->th_ta_p->rwlock));
	return (return_val);
}

/*
* Description:
*   Change the pending signal flag and pending signals
*
* Input:
*   *th_p - thread handle
*   ti_pending_flag - flag that indicates that there are
*	pending signals in the thread.  If this value is not
*	set, the pending signal mask is ignored.
*   ti_pending - new pending signal information.
*
* Output:
*   td_thr_setsigpending - return value
*
* Side effects:
*   Thread corresponding to *th_p has pending signal
* information changed.
*   Imported functions called: ps_pstop,
* ps_pcontinue, ps_pdwrite.
*/
td_err_e
__td_thr_setsigpending(const td_thrhandle_t *th_p,
	const uchar_t ti_pending_flag, const sigset_t ti_pending)
{

	/*
	 * Only the pending signal flag and pending signal mask are set in
	 * the thread struct.
	 */

	td_err_e	return_val = TD_OK;
	td_terr_e	td_return = TD_TOK;

#ifdef TEST_PS_CALLS
	if (td_noop) {
		(void) ps_pstop(0);
		(void) ps_pcontinue(0);
		(void) ps_pdwrite(0, 0, 0, 0);
		return (TD_OK);
	}
#endif

	/*
	 * Sanity checks.
	 */
	if (th_p == NULL) {
		return_val = TD_BADTH;
		return (return_val);
	}
	if ((th_p->th_ta_p == NULL) || (th_p->th_unique == NULL)) {
		return_val = TD_BADTA;
		return (return_val);
	}

	/*
	 * Get a reader lock.
	 */
	if (rw_rdlock(&(th_p->th_ta_p->rwlock))) {
		return_val = TD_ERR;
	}

	if (th_p->th_ta_p->ph_p == NULL) {
		rw_unlock(&(th_p->th_ta_p->rwlock));
		return_val = TD_BADTA;
		return (return_val);
	}

	/*
	 * More than 1 byte is being written.  Stop the process.
	 */

	if (ps_pstop(th_p->th_ta_p->ph_p) == PS_OK) {
		uthread_t *thr_struct_p;
		/*
		 * Point a thread struct pointer at the address
		 * of the thread struct in the debuggee.  The
		 * address of the pending signal information will
		 * be offset from this thread struct address.
		 */
		thr_struct_p = (uthread_t *)th_p->th_unique;

		/*
		 * Write t_psig.
		 */
		if (ps_pdwrite(th_p->th_ta_p->ph_p, (paddr_t)
			&(thr_struct_p->t_psig), (char *)&ti_pending,
			sizeof (thr_struct_p->t_psig)) != PS_OK) {
			return_val = TD_DBERR;
		}

		/*
		 * Write t_pending.
		 */
		if (ps_pdwrite(th_p->th_ta_p->ph_p, (paddr_t)
			&(thr_struct_p->t_pending), (char *)&ti_pending_flag,
			sizeof (thr_struct_p->t_pending)) != PS_OK) {
			return_val = TD_DBERR;
		}

		/*
		 * Continue process.
		 */

		if (ps_pcontinue(th_p->th_ta_p->ph_p) != PS_OK) {
			return_val = TD_DBERR;
		}
	}
	/* ps_pstop succeeded   */
	else {
		return_val = TD_DBERR;
	}

	rw_unlock(&(th_p->th_ta_p->rwlock));
	return (return_val);
}

/*
* Description:
*   Set the floating pointing registers for a given thread.
* The floating registers are only available for a thread executing
* on and LWP.  If the thread is not running on an LWP, this
* function will return TD_NOFPREGS.
*
* Input:
*   *th_p - thread handle for thread on which fp registers are
* being set.
*   *fpregset - floating point register values(see sys/procfs.h)
*
* Output:
*   none
*
* Side effects:
*   Floating point registers in thread corresponding to *th_p
* are set.
*   Imported functions called: ps_pdread,
* ps_pstop, ps_pcontinue, ps_lsetfpregs.
*/
td_err_e
__td_thr_setfpregs(const td_thrhandle_t *th_p, const prfpregset_t * fpregset)
{
	td_err_e	return_val = TD_OK;
	td_terr_e	td_return;
	uthread_t	thr_struct;
	fpregset_t	ts_fpregset;

#ifdef TEST_PS_CALLS
	if (td_noop) {
		(void) ps_pstop(0);
		(void) ps_pcontinue(0);
		(void) ps_pdread(0, 0, 0, 0);

		(void) ps_lsetfpregs(0, 0, 0);
		return (TD_OK);
	}
#endif

	/*
	 * Sanity checks.
	 */
	if (th_p == NULL) {
		return_val = TD_BADTH;
		return (return_val);
	}
	if ((th_p->th_ta_p == NULL) || (th_p->th_unique == NULL)) {
		return_val = TD_BADTA;
		return (return_val);
	}
	if (fpregset == NULL) {
		return (TD_ERR);
	}

	/*
	 * Get a reader lock.
	 */
	if (rw_rdlock(&(th_p->th_ta_p->rwlock))) {
		return_val = TD_ERR;
	}

	if (th_p->th_ta_p->ph_p == NULL) {
		rw_unlock(&(th_p->th_ta_p->rwlock));
		return_val = TD_BADTA;
		return (return_val);
	}

	/*
	 * More than 1 byte is geing read.  Stop the process.
	 */

	if (ps_pstop(th_p->th_ta_p->ph_p) == PS_OK) {
		/*
		* Extract the thread struct address from the
		* thread handle and read
		* the thread struct.
		*/

		td_return = __td_read_thr_struct(th_p->th_ta_p,
			th_p->th_unique, &thr_struct);

		if (td_return == TD_TOK) {

			/*
			* Read the floating point registers using the imported
			* interface.
			*/

			if (ISVALIDLWP(&thr_struct) &&
					HASVALIDFP(&thr_struct)) {
				if (ps_lsetfpregs(th_p->th_ta_p->ph_p,
					thr_struct.t_lwpid,
					fpregset) == PS_ERR) {
					return_val = TD_DBERR;
				}
			} else {
				return_val = TD_NOFPREGS;
			}
		} else {
			return_val = TD_ERR;
		}
		/*
		 * Continue process.
		 */
		if (ps_pcontinue(th_p->th_ta_p->ph_p) != PS_OK) {
			return_val = TD_DBERR;
		}
	}
	/* ps_pstop succeeded   */
	else {
		return_val = TD_DBERR;
	}

	rw_unlock(&(th_p->th_ta_p->rwlock));
	return (return_val);
}


/*
* Description:
*   Set the extra registers for the given thread.  Extra register can
* only be set on a thread in on an LWP.  This operation will return
* TD_NOFPREGS for thread not on and LWP.
*
* Input:
*   *th_p - thread handle for thread on which extra registers
* are being set.
*   *xregset - extra register set values, see sys / procfs.h.
*
* Output:
*   td_thr_setxregs - return value.  TD_NOCAPAB will be
* returned if x registers are not available from the
* thread.
*
* Side Effect:
*   Extra registers in the thread corresponding to
* *th_p are set.
*   Imported functions called: ps_pdread, ps_pdwrite, ps_lsetxregs.
* ps_pstop, ps_pcontinue.
*/
td_err_e
__td_thr_setxregs(const td_thrhandle_t *th_p, const caddr_t xregset)
{
	td_err_e	return_val = TD_OK;
	td_terr_e	td_return;
	uthread_t	thr_struct;

#ifdef TEST_PS_CALLS
	if (td_noop) {
		(void) ps_pstop(0);
		(void) ps_pcontinue(0);
		(void) ps_pdread(0, 0, 0, 0);
		(void) ps_pdwrite(0, 0, 0, 0);
		(void) ps_lsetxregs(0, 0, 0);
		return (TD_OK);
	}
#endif

	/*
	 * Sanity checks.
	 */
	if (th_p == NULL) {
		return_val = TD_BADTH;
		return (return_val);
	}
	if ((th_p->th_ta_p == NULL) || (th_p->th_unique == NULL)) {
		return_val = TD_BADTA;
		return (return_val);
	}
	if (xregset == NULL) {
		return (TD_ERR);
	}

	/*
	 * Get a reader lock.
	 */
	if (rw_rdlock(&(th_p->th_ta_p->rwlock))) {
		return_val = TD_ERR;
	}

	if (th_p->th_ta_p->ph_p == NULL) {
		rw_unlock(&(th_p->th_ta_p->rwlock));
		return_val = TD_BADTA;
		return (return_val);
	}

	/*
	 * More than 1 byte is geing read.  Stop the process.
	 */

	if (ps_pstop(th_p->th_ta_p->ph_p) == PS_OK) {

		/*
		* Extract the thread struct address from
		* the thread handle and read
		* the thread struct.
		*/

		td_return =
			__td_read_thr_struct(th_p->th_ta_p,
			th_p->th_unique,
			&thr_struct);

		if (td_return == TD_TOK) {

			if (ISVALIDLWP(&thr_struct)) {

				/*
				* Write the x registers
				* using the imported interface.
				*/
				if (ps_lsetxregs(th_p->th_ta_p->ph_p,
					thr_struct.t_lwpid,
					xregset) == PS_ERR) {
					return_val = TD_DBERR;
				}
				if (!HASVALIDFP(&thr_struct)) {
					/*
					 * Let user now that floating
					 * point registeres are not
					 * available.
					 */
					return_val = TD_NOFPREGS;
				}
			} else {
				return_val = TD_NOXREGS;
			}
		} else {
			return_val = TD_ERR;
		}
		/*
		 * Continue process.
		 */
		if (ps_pcontinue(th_p->th_ta_p->ph_p) != PS_OK) {
			return_val = TD_DBERR;
		}
	}
	/* ps_pstop succeeded   */
	else {
		return_val = TD_DBERR;
	}

	rw_unlock(&(th_p->th_ta_p->rwlock));
	return (return_val);
}


#if 0
/*   This function does the same thing as td_thr_setsiginfo().  */

/*
* Description:
*   Send a signal to a thread corresponding to thread handle
* *th_p.
*
* Input:
*   *th_p - thread handle
*   sig - signal to be sent to thread. (see signal(5) for
* valid sig values.)
*
* Output:
*   td_thr_kill - return value
*
* Side effects:
*   Threads corresponding to *th_p is sent signal sig
*   Imported functions called: ps_pdread, ps_pstop,
* ps_pcontinue, ps_pdwrite.
*/
td_err_e
td_thr_kill(const td_thrhandle_t *th_p, int sig)
{
	int	kill_return = 0;
	td_err_e	return_val = TD_OK;
	td_terr_e	td_return = TD_TERR;
	uthread_t	thr_struct;


#ifdef TEST_PS_CALLS
	if (td_noop) {
		(void) ps_pstop(0);
		(void) ps_pcontinue(0);
		(void) ps_pdread(0, 0, 0, 0);
		(void) ps_pdwrite(0, 0, 0, 0);
		return (TD_OK);
	}
#endif

	/*
	 * Sanity checks.
	 */
	if (th_p == NULL) {
		return_val = TD_BADTH;
		return (return_val);
	}
	if ((th_p->th_ta_p == NULL) || (th_p->th_unique == NULL)) {
		return_val = TD_BADTA;
		return (return_val);
	}

	/*
	 * Check range of signal value.
	 */
	if (sig == NULL) {
		return_val = TD_ERR;
		return (return_val);
	}
	if (sig > SIGRTMAX) {
		return_val = TD_ERR;
		return (return_val);
	}

	/*
	 * Get a reader lock.
	 */
	if (rw_rdlock(&(th_p->th_ta_p->rwlock))) {
		return_val = TD_ERR;
	}

	if (th_p->th_ta_p->ph_p == NULL) {
		rw_unlock(&(th_p->th_ta_p->rwlock));
		return_val = TD_BADTA;
		return (return_val);
	}

	/*
	 * Have to read pending signals and add signal.  Stop to prevent
	 * change of pending signals before write back.
	 */
	if (ps_pstop(th_p->th_ta_p->ph_p) == PS_OK) {

		/*
		 * Extract the thread struct address from the thread handle
		 * and read the thread struct.
		 */

		td_return = __td_read_thr_struct(th_p->th_ta_p,
			th_p->th_unique,
			&thr_struct);

		if (td_return == TD_TOK) {

			/*
			 * Set the signal as a pending signal in the target
			 * thread. Set the bit for the signal in the thread
			 * struct pending signal mask. Note that if there is
			 * no pending signal, the bits for pending signals
			 * are cleared.
			 */

			if (!(thr_struct.t_pending)) {
				sigemptyset(&(thr_struct.t_psig));
				thr_struct.t_pending = TRUE;
			}
			sigaddset(&(thr_struct.t_psig), sig);

			/*
			 * Write thread struct back.
			 */

			td_return = __td_write_thr_struct(th_p->th_ta_p,
				th_p->th_unique,
				&thr_struct);

			if (td_return != TD_TOK) {
				return_val = TD_ERR;
			}
		}
		/* read of thread struct succeeded   */
		else {
			return_val = TD_ERR;
		}

		/*
		 * Continue process.
		 */

		if (ps_pcontinue(th_p->th_ta_p->ph_p) != PS_OK) {
			return_val = TD_DBERR;
		}
	}
	/* ps_pstop succeeded   */
	else {
		return_val = TD_DBERR;
	}

	rw_unlock(&(th_p->th_ta_p->rwlock));
	return (return_val);
}
#endif

#ifdef TD_INTERNAL_TESTS

/*
* Description:
*   Validate the thread information in the
* thread information struct by comparison with original
* threads information.
*
* Input:
*   *ti_p - thread information struct
*
* Output:
*   __td_ti_validate - return value
*
* Side effects:
*   none
*
*/
td_terr_e
__td_ti_validate(const td_thrinfo_t *ti_p)
{

	/*
	 * Compare the contents of the thread information struct to that of the
	 * threads in this process.
	 */
	td_terr_e	return_val = TD_TOK;
	uthread_t	*ts_p;
	td_thr_state_e	mapped_state;

	ts_p = (uthread_t *) ti_p->ti_ro_area;

	if (ts_p->t_usropts != ti_p->ti_user_flags) {
		if (__td_debug)
			diag_print("Compare failed: user flags %x %x\n",
			ts_p->t_usropts, ti_p->ti_user_flags);
		return_val = TD_TBADTH;
	}
	if (ts_p->t_tid != ti_p->ti_tid) {
		if (__td_debug)
			diag_print("Compare failed: tid %x %x\n",
				ts_p->t_tid, ti_p->ti_tid);
		return_val = TD_TBADTH;
	}
	if (ts_p->t_startpc != ti_p->ti_startfunc) {
		if (__td_debug)
			diag_print("Compare failed: start func %x %x\n",
			ts_p->t_startpc, ti_p->ti_startfunc);
		return_val = TD_TBADTH;
	}
	if (ts_p->t_stk != ti_p->ti_stkbase) {
		if (__td_debug)
			diag_print("Compare failed: stkbase %x %x\n",
				ts_p->t_stk, ti_p->ti_stkbase);
		return_val = TD_TBADTH;
	}
	if (ts_p->t_stksize != ti_p->ti_stksize) {
		if (__td_debug)
			diag_print("Compare failed: stksize %x %x\n",
				ts_p->t_stksize, ti_p->ti_stksize);
		return_val = TD_TBADTH;
	}
	if (ts_p->t_flag != ti_p->ti_flags) {
		if (__td_debug)
			diag_print("Compare failed: flags %x %x\n",
				ts_p->t_flag, ti_p->ti_flags);
		return_val = TD_TBADTH;
	}
	if (__td_thr_map_state(ts_p->t_state, &mapped_state) != TD_TOK) {
		if (__td_debug)
			diag_print("State mapping failed\n");
	}
	if (mapped_state != ti_p.ti_state) {
		if (__td_debug)
			diag_print("Compare failed: state %x %x\n",
				mapped_state, ti_p->ti_state);
		return_val = TD_TBADTH;
	}
	if (TD_CONVERT_TYPE(*ts_p) != ti_p->ti_type) {
		if (__td_debug)
			diag_print("Compare failed: type %x %x\n",
				TD_CONVERT_TYPE(*ts_p), ti_p->ti_type);
		return_val = TD_TBADTH;
	}
	if (ts_p->t_pc != ti_p->ti_pc) {
		if (__td_debug)
			diag_print("Compare failed: pc %x %x\n",
				ts_p.t_pc, ti_p->ti_pc);
		return_val = TD_TBADTH;
	}
	if (ts_p->t_sp != ti_p->ti_sp) {
		if (__td_debug)
			diag_print("Compare failed: sp %x %x\n",
				ts_p->t_sp, ti_p->ti_sp);
		return_val = TD_TBADTH;
	}
	if (ts_p->t_pri != ti_p->ti_pri) {
		if (__td_debug)
			diag_print("Compare failed: priority %x %x\n",
				ts_p->t_pri, ti_p->ti_pri);
		return_val = TD_TBADTH;
	}

	/*
	 * lwp id is only correct if thread is on an lwp.
	 */
	if (ISVALIDLWP(ts_p)) {
		if (ts_p->t_lwpid != ti_p->ti_lid) {
			if (__td_debug)
				diag_print("Compare failed: lid %x %x\n",
					ts_p->t_lwpid, ti_p->ti_lid);
			return_val = TD_TBADTH;
		}
	}
	if (ts_p->__sigbits[0] !=
			ti_p->ti_sigmask.__sigbits[0]) {
		if (__td_debug)
			diag_print("Compare failed: sigmask %x %x\n",
				ts_p->t_hold, ti_p->ti_sigmask);
		return_val = TD_TBADTH;
	}

	if (ts_p->t_preempt != ti_p->ti_preemptflag) {
		if (__td_debug)
			diag_print("Compare failed: preemptflag %x %x\n",
				ts_p->t_preempt,
				ti_p->ti_preemptflag);
		return_val = TD_TBADTH;
	}

	if (ts_p->__sigbits[0] !=
			ti_p->ti_pending.__sigbits[0]) {
		if (__td_debug)
			diag_print("Compare failed: pending %x %x\n",
				ts_p->t_psig, ti_p->ti_pending);
		return_val = TD_TBADTH;
	}
#ifdef PHASE2
	/*
	 * Check priority inversion flag when it is present in thread
	 * struct.
	 */
#endif

#if TD_DEBUG

	if (!return_val) {
		if (__td_debug)
			diag_print("Compare passed\n");
	}
#endif

	return (return_val);
}
#endif /* TD_INTERNAL_TESTS */
#ifdef PHASE2


/*
* Description:
*   Suspend the execution of a specified thread.  It remains
* suspended until it is resumed by td_thr_dbresume(). A thread
* that is suspended via td_thr_dbsuspend() is completely different
* from a thread being suspended *	via thr_suspend(). The
* application cannot cause a suspended thread to become runnable.
*
* Input:
*   *th_p - thread handle
*
* Output:
*   td_thr_dbsuspend - return value
*
* Side effects:
*   Thread is suspended.
*   Imported functions called:
*/
td_err_e
td_thr_dbsuspend(const td_thrhandle_t *th_p)
{
	td_err_e	return_val;

#ifdef TEST_PS_CALLS
	if (td_noop) {
		return (TD_OK);
	}
#endif

	/*
	 * Sanity checks.
	 */
	if (th_p == NULL) {
		return_val = TD_BADTH;
		return (return_val);
	}
	if ((th_p->th_ta_p == NULL) || (th_p->th_unique == NULL)) {
		return_val = TD_BADTA;
		return (return_val);
	}

	/*
	 * Get a reader lock.
	 */
	if (rw_rdlock(&(th_p->th_ta_p->rwlock))) {
		return_val = TD_ERR;
	}

	if (th_p->th_ta_p->ph_p == NULL) {
		rw_unlock(&(th_p->th_ta_p->rwlock));
		return_val = TD_BADTA;
		return (return_val);
	}

	/*
	 * This will be implemented with libthread help.  Will
	 * only have to set a bit.
	 */

	if (__td_pub_debug) diag_print(
		"libthread_db: td_thr_dbsuspend() not fully implemented\n");

	rw_unlock(&(th_p->th_ta_p->rwlock));
	return (TD_NOT_DONE);

}


/*
* Description:
*   Resume the execution of a thread suspended via
* td_thr_dbsuspend().
*
* Input:
*   *th_p - thread handle
*
* Output:
*   td_thr_dbresume - return value
*
* Side effects:
*   Thread is continued.
*   Imported functions called:
*/
td_err_e
td_thr_dbresume(const td_thrhandle_t *th_p)
{
	td_err_e	return_val;

#ifdef TEST_PS_CALLS
	if (td_noop) {
		return (TD_OK);
	}
#endif

	/*
	 * Sanity checks.
	 */
	if (th_p == NULL) {
		return_val = TD_BADTH;
		return (return_val);
	}
	if ((th_p->th_ta_p == NULL) || (th_p->th_unique == NULL)) {
		return_val = TD_BADTA;
		return (return_val);
	}

	/*
	 * Get a reader lock.
	 */
	if (rw_rdlock(&(th_p->th_ta_p->rwlock))) {
		return_val = TD_ERR;
	}

	if (th_p->th_ta_p->ph_p == NULL) {
		rw_unlock(&(th_p->th_ta_p->rwlock));
		return_val = TD_BADTA;
		return (return_val);
	}
	if (__td_pub_debug) diag_print(
		"libthread_db: td_thr_dbresume() not fully implemented\n");

	rw_unlock(&(th_p->th_ta_p->rwlock));
	return (TD_NOT_DONE);

}

#endif /* END PHASE2 */

/*
* Description:
*   Check the value in data against the thread id.  If
* it matches, return 1 to terminate interations.
*   This function is used by td_ta_map_id2thr() to map id to
* a thread handle.
*
* Input:
*   *th_p - thread handle
*   *data - thread id being sought
*
* Output:
*   td_mapper_id2thr - returns 1 if thread id is found.
*
* Side effects:
*/
static int
td_mapper_id2thr(const td_thrhandle_t *th_p, td_mapper_param_t *data)
{
	int	return_val = 0;
	td_thrinfo_t ti;
	td_err_e td_return;

	td_return = __td_thr_get_info(th_p, &ti);

	if (td_return == TD_OK) {
		if (data->tid == ti.ti_tid) {
			data->found = TRUE;
			data->th = *th_p;
			return_val = 1;
		}
	}
	return (return_val);
}

/*
* Description:
*   Given a thread identifier, return the corresponding thread
* handle.
*
* Input:
*   *ta_p - thread agent
*   tid - Value of thread identifier(e.g., as
* returned by call to thr_self()).
*
* Output:
*   *th_p - Thread handle
*   td_ta_map_id2thr - return value
*
* Side effects:
*   none
*   Imported functions called: ps_pdread, ps_pstop,
* ps_pcontinue.
*/
td_err_e
__td_ta_map_id2thr(const td_thragent_t *ta_p, thread_t tid,
	td_thrhandle_t *th_p)
{

	td_err_e	return_val;
	td_mapper_param_t	data;

#ifdef TEST_PS_CALLS
	if (td_noop) {
		(void) ps_pstop(0);
		(void) ps_pcontinue(0);
		(void) ps_pdread(0, 0, 0, 0);

		return (TD_OK);
	}
#endif

	/*
	 * Check for bad thread agent pointer.
	 */
	if (ta_p == NULL) {
		return_val = TD_BADTA;
		return (return_val);
	}

	/*
	 * LOCKING EXCEPTION - Locking is not required
	 * here because the locking and checking will be
	 * done in __td_ta_thr_iter.  If __td_ta_thr_iter
	 * is not used or if some use of the thread agent
	 * is made other than the sanity checks, ADD
	 * locking.
	 */

	/*
	 * Check for bad thread handle pointer.
	 */
	if (th_p == NULL) {
		return_val = TD_BADTH;
		return (return_val);
	}

	if (tid != 0) {
		data.tid = tid;
		data.found = NULL;
		return_val = __td_ta_thr_iter(ta_p,
			(td_thr_iter_f *) td_mapper_id2thr, (void *)&data,
			TD_THR_ANY_STATE, TD_THR_LOWEST_PRIORITY,
			TD_SIGNO_MASK, TD_THR_ANY_USER_FLAGS);
		if (return_val == TD_OK) {
			if (data.found != NULL) {
				*th_p = (data.th);
			} else {
				return_val = TD_NOTHR;
				__td_report_po_err(return_val,
					"Thread not found - td_ta_map_id2thr");
			}
		}
	} else {
		return_val = TD_NOTHR;
		__td_report_po_err(return_val,
			"Invalid thread id - td_ta_map_id2thr");
	}

	return (return_val);

}

#ifdef TD_INTERNAL_TESTS

/*
* Description:
*   Dump out selected fields of a thread information struct
*
* Input:
*   *ti_p - thread information struct
*
* Output:
*   none
*
* Side effects:
*   none
*/
void
__td_tsd_dump(const td_thrinfo_t *ti_p, const thread_key_t key)
{
	void		*data;
	td_thrhandle_t	th;

	th.th_unique = ti_p->ti_ro_area;
	th.th_ta_p = ti_p->ti_ta_p;
	td_thr_tsd(&th, key, &data);

	diag_print("Thread Object:\n");
	diag_print("      Id:			%d\n", ti_p->ti_tid);
	diag_print("      TSD key:			%i\n", key);
	diag_print("      Key binding:		%x\n", data);

}

/*
* Description:
*   Dump out a thread information struct
*
* Input:
*   *ti_p - thread information struct
*   full - flag for full dump
*
* Output:
*   none
*
* Side effects:
*   none
*/
void
__td_ti_dump(const td_thrinfo_t *ti_p, int full)
{
	int	i;

	diag_print("Thread Object:\n");
	if (full) {
		diag_print("  Thread agent pointer:	%x\n",
			ti_p->ti_ta_p);
		diag_print("    Thread creation information:\n");
		diag_print("      User flags:		%x\n",
			ti_p->ti_user_flags);
	}
	diag_print("      Id:			%d\n", ti_p->ti_tid);
	diag_print("      Start function address:	%x\n",
		ti_p->ti_startfunc);
	diag_print("      Stack base:		%x\n",
		ti_p->ti_stkbase);
	diag_print("      Stack size:		%d\n", ti_p->stksize);
	if (full) {
		diag_print("      Thread flags:		%x\n",
			ti_p->ti_flags);
		diag_print("      Thread struct address:	%x\n",
			ti_p_ti_ro_area);
		diag_print("      Thread struct size:	%d\n",
			ti_p->ti_ro_size);
	}
	diag_print("   Thread variable information:\n");
	diag_print("      PC:			%x\n", ti_p->ti_pc);
	diag_print("      SP:			%x\n", ti_p->ti_sp);
	diag_print("      State:			%s\n",
		td_thr_state_names[ti_p->ti_state];
	if (full) {
		diag_print("      Debugger suspended:	%d\n",
			ti_p->ti_db_suspended);
		diag_print("      Priority:			%d\n",
			ti_p->ti_pri);
		diag_print("      Associated LWP Id:		%d\n",
			ti_p->ti_lid);
		diag_print("      Signal mask:		%x\n",
			ti_p->ti_sigmask);
		diag_print("      Enabled events:		%x\n",
			ti_p->ti_events.event_bits[1]);
		diag_print("      Trace enabled:		%d\n",
			ti_p->ti_traceme);
		diag_print("      Has been preempted:	%d\n",
			ti_p->ti_preemptflag);
		diag_print("      Priority inheritance done	%d\n",
			ti_p->ti_pirecflag);

		diag_print("      Pending signal information:\n");

		diag_print("        Pending signals:		");
		for (i = 1; i < 4; i++) {
			diag_print("%x ",
				ti_p->ti_pending.__sigbits[i]);
		}
		diag_print("\n");
	}
}


/*
* Description:
*   Dump ucontext struct
*
* Input:
*   *uc_p - ucontext struct
*
* Output:
*   none
*
* Side effects:
*   ucontext_t is dumped
*/
static void
td_ucontext_dump(const ucontext_t * uc_p)
{

	diag_print("        Signal ucontext:\n");
	diag_print("          Flags:			%x\n",
		uc_p->uc_flags);
	diag_print("          Link:			%x\n",
		uc_p->uc_link);
	diag_print("          Signal mask		%x\n",
		uc_p->uc_sigmask);
	diag_print("          Stack:			%x\n",
		uc_p->uc_stack);
	diag_print("          mcontext:		(Not shown)\n");

}

/*
* Description:
*   Dump siginfo_t struct
*
* Input:
*   *sig - siginfo_t struct
*
* Output:
*   none
*
* Side effects:
*   none
*/
static void
td_siginfo_dump(const siginfo_t * sig_p)
{
	siginfo_t	sig;

	sig = *sig_p;

	diag_print("si_signo - signal from signal.h:	%d\n", sig.si_signo);
	diag_print("si_code - code from above:	%d\n", sig.si_code);
	diag_print("si_errno - error from errno.h:	%d\n", sig.si_errno);
	diag_print("union _data:\n");
	diag_print("  struct _proc:\n");
	diag_print("    _pid:			%d\n", sig._data._proc._pid);
	diag_print("    union _data:\n");
	diag_print("      struct _kill:\n");
	diag_print("        _uid:			%d\n",
		sig._data._proc._pid);
	diag_print("      struct _cld:\n");
	diag_print("        _utime:			%d\n",
		sig._data._proc._pdata._cld._utime);
	diag_print("        _status:			%d\n",
		sig._data._proc._pdata._cld._status);
	diag_print("        _stime:			%d\n",
		sig._data._proc._pdata._cld._stime);
	diag_print("  struct _fault - SIGSEGV, SIGBUS, SIGILL and SIGFPE :\n");
	diag_print("    _addr - faulting address:	%x\n",
		sig._data._fault._addr);
	diag_print("    _trapno - Illegal trap number:%d\n",
		sig._data._fault._trapno);
	diag_print("  struct _file - SIGPOLL, SIGXFSZ\n");
	diag_print("    _fd - file descriptor:		%d\n",
		sig._data._file._fd);
	diag_print("    _band:			%d\n", sig._data._file._band);
	diag_print("  struct _prof - SIGPROF\n");
	diag_print("     _faddr - last fault address	%x\n",
		sig._data._prof._faddr);
	diag_print("     _tstamp - real time stamp:	%d\n",
		sig._data._prof._tstamp);
	diag_print("     _syscall - current syscall:	%d\n",
		sig._data._prof._syscall);
	diag_print("     _nsysarg - no. of arguments:%d\n",
		sig._data._prof._nsysarg);
	diag_print("     _fault - last fault type:	%d\n",
		sig._data._prof._fault);
	diag_print("     _sysarg - syscall arguments:(not shown)\n");
	diag_print("     _mstate 			(not shown)\n");

}
#endif

/*
* Description:
*   Compare mask1 and mask2.  If equal, return 0,
* Otherwise, return non-zero.
*
* Input:
*   mask1_p - signal mask
*   mask2_p - signal mask
*
* Output:
*   td_sigmak_are_equal - return value
*
* Side effects:
*   none
*/
int
__td_sigmask_are_equal(sigset_t * mask1_p, sigset_t * mask2_p)
{
	int	return_val = 1;
	int	i, mask_word_cnt;

	if ((mask1_p != NULL) && (mask2_p != NULL)) {
		mask_word_cnt = sizeof (sigset_t) /
			sizeof ((*mask1_p).__sigbits[0]);

		for (i = 0; i < mask_word_cnt; i++) {
			if ((*mask1_p).__sigbits[i] !=
					(*mask2_p).__sigbits[i]) {
				return_val = 0;
				break;
			}
		}
	} else {
		return_val = 0;
	}

	return (return_val);
}
