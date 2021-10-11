/*
*ident  "@(#)td.c 1.38     95/02/13 SMI"
*
* Copyright 1993, 1994 by Sun Microsystems, Inc.
*/


/*
*  Description:
*	Main module for td.  Globals are defined in this file.
*/


/*
 * Define TD_INITIALIZER.  It is used in the td.h file to determine
 * how global variables are declared.  By defining TD_INITIALIZER
 * here, the definition of globals variables is done in this
 * file and other files will declare them as extern.
 */
#define		TD_INITIALIZER

#ifdef __STDC__
#pragma weak td_log = __td_log  /* i386 work around */
#pragma weak td_ta_new = __td_ta_new /* i386 work around */
#pragma weak td_ta_delete = __td_ta_delete /* i386 work around */
#pragma weak td_init = __td_init  /* i386 work around */
#endif				/* __STDC__ */

#include <thread_db.h>
#include "thread_db2.h"
#include "td_impl.h"

/*
 * The #define TEST_PS_CALLS and the global td_noop (also only defined
 * when TEST_PS_CALLS is defined, can be used to test proc_service
 * calls made by a function.  The proc_service functions used for
 * testing will mark its position in an array.  Calling the function
 * with and without td_noop can be used to check the proc_service
 * functions that are called (the proc_service functions called
 * under the guard of the td_noop variable should equal those
 * called by the function (directly and indirectly).  This can
 * be used to check the documentation on which functions proc_service
 * are called.
 */

/*
 * td_log_() turns logging on and off.  A function in the
 * imported library is provided by the user for logging events
 * in libthread_db.  If logging is turned on, this imported
 * function is called.  The nominal name of the function
 * is "ps_plog".
 *
 * Input:
 *    on_off - 0 turns logging off
 *		non-zero turns logging on
 *
 * Output:
 *    none
 *
 * Side effects:
 * Global variable for logging is modified.  The variable
 * for logging is global to the entire library - as opposed
 * to global for a thread agent.  Logging is turned on
 * for any operation in libthread_db.  No imported
 * functions are called.
 */
void
__td_log(const int on_off)
{

#ifdef TEST_PS_CALLS
	if (td_noop) {
		return;
	}
#endif
	__td_logging = on_off;


}


/*
* Allocate a thread agent and return a pointer to it.
*
* Input:
*    *ph_p - process handle defined by debugger
*
* Output:
*    *ta_pp - thread agent for input process handle.
*    td_ta_new - return value
*
* Side effects:
*    Storage for thread agent is allocated.
*    Imported functions called: ps_pglobal_lookup().
*/
td_err_e
__td_ta_new(const struct ps_prochandle * ph_p,
    td_thragent_t **ta_pp)
{
	td_err_e	return_val = TD_OK;
	paddr_t 	symbol_addr;
	ps_err_e	db_return;

#ifdef TEST_PS_CALLS
	if (td_noop) {
		(void) ps_pglobal_lookup(0, 0, 0, 0);
		return (TD_OK);
	}
#endif

	/*
	 * Check that the pointer ta_pp is okay.
	 */
	if (ph_p == NULL) {
		return_val = TD_BADPH;
		__td_report_po_err(TD_BADPH,
		    "Input parameter error - td_ta_new");
		return (return_val);
	}

	if (ta_pp == NULL) {
		return_val = TD_ERR;
		__td_report_po_err(return_val,
		    "Input parameter error - td_ta_new");
		return (return_val);
	}

	*ta_pp = (td_thragent_t *)malloc(sizeof (**ta_pp));
	if (*ta_pp == NULL) {
		return_val = TD_MALLOC;
		__td_report_po_err(return_val,
		    "Malloc failed - td_ta_new");
	} else {
		/*
		 * Set the proc handle field.
		 */

		(*ta_pp)->ph_p = (struct ps_prochandle *) ph_p;

		/*
		 * Initialize the rwlock for the thread agent.
		 */

		rwlock_init(&((*ta_pp)->rwlock), USYNC_THREAD, NULL);

		/*
		 * Check for libthread.
		 */
		db_return = ps_pglobal_lookup(ph_p, TD_LIBTHREAD_NAME,
		    TD_HASH_TAB_NAME, (paddr_t *) & symbol_addr);
		if (db_return != PS_OK) {
			if (db_return == PS_NOSYM) {

				/*
				 * Not absolutely true, but assume
				 * that libthread not loaded.
				 */
				return_val = TD_NOLIBTHREAD;
			} else {
				return_val = TD_ERR;
			}
			free(*ta_pp);
			*ta_pp = (td_thragent_t *) 0;
		} else {
			/*
			 *  On success, preload some of symbol information into
			 * thread agent(e.g., address of _allthreads).
			 */
			(*ta_pp)->hash_tab_addr = symbol_addr;
		}
	}

	return (return_val);

}


/*
* Description:
*    Deallocate the thread agent.
*
* Input:
*    ta_p - thread agent pointer
*
* Output:
*    ta_delete - return value
*
* Side effects:
*    Storage for thread agent is not deallocated.  The prochandle
* in the thread agent is set to NULL so that future uses of
* the thread agent can be detected and an error value returned.
* All functions in the external user interface that make
* use of the thread agent are expected
* to check for a NULL prochandle in the therad agent.
* All such functions are also expect to use obtain a
* reader lock on the thread agent while it is using it.
*    Imported functions called: none
*/
td_err_e
__td_ta_delete(td_thragent_t *ta_p)
{
	td_err_e	return_val = TD_OK;

#ifdef TEST_PS_CALLS
	if (td_noop) {
		return (TD_OK);
	}
#endif

	if (ta_p != NULL) {
		/*
		 * Grab a writer lock while thread agent is
		 * being deleted.
		 */
		if (rw_wrlock((rwlock_t *)&ta_p->rwlock)) {
			return_val = TD_ERR;
		} else {
			/*
			 * Free only the hash table storage and NULL
			 * the prochandle.
			 */
			free((void *) ta_p->hash_tab_addr);
			ta_p->hash_tab_addr = NULL;
			ta_p->ph_p = NULL;

			rw_unlock(&ta_p->rwlock);
		}
	} else {
		return_val = TD_BADTA;
		__td_report_po_err(return_val,
		    "NULL thread agent ptr - td_ta_delete");
	}

	return (return_val);

}


/*
* Description:
*    Perform initialization for libthread_db
* interface to debugger.
*
* Input:
*    none
*
* Output:
*    td_init - return value
*
* Side effects:
*    none.
*    Imported functions called: none
*    Lock for protecting global data is initialized.
*/
td_err_e
__td_init()
{
	td_err_e	return_val = TD_OK;

	/*
	 * Initialize global data lock(s).  Initialize only once
	 * based on td_initialized flag.
	 */
	if (!td_initialized) {
		if (mutex_init(&__gd_lock, USYNC_THREAD, 0) != 0) {
			return_val = TD_ERR;
		}
	}

	td_initialized = 1;
	return (return_val);
}
