/*
*
*ident  "@(#)td_event.c 1.45     95/01/03 SMI"
*
* Copyright 1993, 1994 by Sun Microsystems, Inc.
*
*/


/*
* Description:
* This module contains the functions for accessing events.
* The primary functions provide an address at which an event occurs
* and a message when the event occurs.
*
*/

#ifdef __STDC__
#ifdef PHASE2
#pragma weak td_ta_event_addr
#pragma weak td_thr_event_getmsg
#pragma weak td_thr_event_enable
#pragma weak td_thr_clear_event
#pragma weak td_thr_set_event
#endif /* END PHASE2 */
#endif				/* __STDC__ */

#include <thread_db.h>
#include "thread_db2.h"

#include "td.h"
#include "td_event.h"

static	td_terr_e
td_thr_struct_set_event(uthread_t * thr_struct_p, void *event2_p);
static td_terr_e
td_thr_struct_clear_event(uthread_t * thr_struct_p, void *event2_p);
static void
eventnotset(td_thr_events_t *event1_p, td_thr_events_t *event2_p);
static void
eventorset(td_thr_events_t *event1_p, td_thr_events_t *event2_p);
static void
eventandset(td_thr_events_t *event1_p, td_thr_events_t *event2_p);


#ifdef PHASE2


/*
* Description:
*   Given a process and an event number, return
* information about an address in the process or
* system call at which a breakpoint can be set to monitor
* the event.
*
* Input:
*   *ta_p - thread agent
*   event - Integer value corresponding to event of interest
*
* Output:
*   *notify_p - information on type of event notification
* 	and corresponding event information(e.g., address)
*   td_ta_event_addr - return value
*
* Side effects:
*   none.
*   Imported functions called: none
*/
td_err_e td_ta_event_addr(const td_thragent_t *ta_p, u_long event,
	td_notify_t *notify_p)
{

#ifdef TEST_PS_CALLS
	if (td_noop) {
		return (TD_OK);
	}
#endif

	/*
	 * Stops lint warning.
	 */
	{
		event = 0;
		notify_p = 0;
	}


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

	rw_unlock((rwlock_t *)&(ta_p->rwlock));

	return (TD_NOT_DONE);

}


/*
* Description:
*   This function returns the message associated with
* an event that occurred on a thread.  The user can either
* specify a thread via the thread handle parameter and
* get back the message for that thread or specify no thread
* with a NULL thread handle and get a message back that
* contains the thread handle for the thread on which the
* event occurred.
*   Given a thread handle, return the
* message corresponding to the event encountered by the thread.
*   If thread handle is NULL, return a message and set
* *th_p in msg. Subsequent calls will return different messages
* until all messages have been returned at which point an
* error(TD_NOMSG) will be returned. Subsequent calls with NULL
* thread handle will cycle through the messages again.
*   Only one message per thread is saved.
* Messages from earlier events are lost when later events
* occur.
*
* Input:
*   *th_p - thread handle
*
* Output:
*   *msg - event message for most recent event encountered
* by thread corresponding to *th_p
*   td_thr_event_getmsg - return value
*
* Side effects:
*   none
*   Import functions called:
*
* Note:
*   The NULL thread handle interface is made available to support
* debuggers that do not map breakpoints to events(i.e., do no keep
* information about breakpoints that map a breakpoint address to a
* particular type of event).  It would also be used in cases where
* a breakpoint causes a thread to be de-scheduled but does not cause
* the LWP to stop.  In this latter case, the debugger would be
* notified that a breakpoint had occurred but does not have an LWP
* stopped at the breakpoint.
*/
td_err_e
td_thr_event_getmsg(const td_thrhandle_t *th_p, td_event_msg_t *msg)
{

	/*
	 * This function may need a companion function to clear the messages.
	 */

	/*
	 * Stops lint warning.
	 */
	{
		msg = 0;
	}

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
		return (return_val);
	}

	if (th_p->th_ta_p->ph_p == NULL) {
		rw_unlock(&(th_p->th_ta_p->rwlock));
		return_val = TD_BADTA;
		return (return_val);
	}

	rw_unlock(&(th_p->th_ta_p->rwlock));

	return (TD_NOT_DONE);
}


/*
* Description:
*   Enable or disable tracing for a given thread.  Tracing
* is filtered based on the event mask of each thread.  Tracing
* can be turned on/off for the thread without changing thread
* event mask.
*
* Input:
*   *th_p - thread handle
*   onoff - = 0 disables events in thread
* non_zero enables events in thread
*
* Output:
*   td_thr_event_enable - return value
*
* Side effect:
*   Thread data structures are updated to enable or disable
* events.
* Imported functions called: ps_pdwrite, ps_pglobal_lookup.
*/
td_err_e
td_thr_event_enable(td_thrhandle_t *th_p, int onoff)
{
	td_err_e	return_val = TD_NOT_DONE;

#ifdef TEST_PS_CALLS
	if (td_noop) {
		if (return_val != TD_NOT_DONE) {
			ps_pdwrite(0, 0, 0, 0);
			ps_pglobal_lookup(0, 0, 0, 0);
		}
		return (TD_OK);
	}
#endif

	/*
	 * Stops lint warning.
	 */
	{
		onoff = 0;
	}


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

	rw_unlock(&(th_p->th_ta_p->rwlock));
	return (TD_NOT_DONE);
}


/*
* Description:
*   Set event mask to disable event. event is cleared from
* the event mask of the thread.  Events that occur for a thread
* with the event masked off will not cause notification to be
* sent to the debugger(see td_thr_set_event for fuller
* description).
*
* Input:
*   *th_p - thread handle
*   event - event number(see thread_db.h)
*
* Output:
*   td_thr_clear_event - return value
*
* Side effects:
*   Event mask in thread structure is updated or global
* event maks is updated.
*   Imported functions called: ps_pstop, ps_pcontinue,
* ps_pdread, ps_pdwrite.
*/
td_err_e
td_thr_clear_event(td_thrhandle_t *th_p, td_thr_events_t event)
{
	td_err_e	return_val = TD_NOT_DONE;
	td_terr_e	td_return = TD_TERR;
	td_thrinfo_t	ti;
	td_thr_events_t	thread_events, not_events;

#ifdef TEST_PS_CALLS
	if (td_noop) {
		ps_pstop(0);
		ps_pcontinue(0);
		ps_pdread(0, 0, 0, 0);
		if (return_val != TD_NOT_DONE) {
			ps_pdwrite(0, 0, 0, 0);
		}
		return (return_val);
	}
#endif

	/*
	 * Check for clearing of global event mask.
	 */

	if (th_p == NULL) {

		/*
		 * Clear global event mask.
		 */
		eventnotset(&not_events, &event);
		eventandset(&thread_events, &not_events);
	} else {

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
		 * A read and a write is required.  Stop the process.
		 */
		if (ps_pstop(th_p->th_ta_p.ph_p) == PS_OK) {

			/*
			 * Clear event mask for thread.
			 */
			return_val = __td_thr_get_info(th_p, &ti);
			if (return_val == TD_OK) {
				thread_events = ti.ti_events;
				eventnotset(&not_events, &event);
				eventandset(&thread_events, &not_events);

				/*
				 * OR in global event mask.
				 */
				if (!mutex_lock(&__gd_lock)) {
					eventorset(&thread_events,
						&td_global_events);
					mutex_unlock(&__gd_lock);
				} else {
					return_val = TD_ERR;
				}

				if (return_val == TD_OK) {

					td_return = __td_thr_struct_set(th_p,
						td_thr_struct_clear_event,
						(void *) &thread_events);

					if (td_return != TD_TOK) {
						return_val = TD_DBERR;
					}
				}
			}

			/*
			 * Continue process.
			 */

			if (ps_pcontinue(th_p->th_ta_p.ph_p) != PS_OK) {
				return_val = TD_DBERR;
			}
		}		/* ps_pstop succeeded   */
		rw_unlock(&(th_p->th_ta_p->rwlock));
	}


	return (TD_NOT_DONE);
}

/*
* Description:
* Set the event mask in the thread struct.
*
* Input:
*   *thr_struct_p - thread struct
*   *event_p - event mask
*
* Output:
*   td_thr_struct_set_event - return value
*
* Side effects:
*   none
*/

static td_terr_e
td_thr_struct_set_event(uthread_t * thr_struct_p,
			void *event2_p)
{

	/*
	 * Don't know how to set events in thread struct yet so better not
	 * execute this.
	 */

	/*
	 * Stops lint warning.
	 */
	{
		thr_struct_p = 0;
		event2_p = 0;
	}

	return (TD_NOT_DONE);
}

/*
* Description:
*   Clear the event mask in the thread struct.
*
* Input:
*   *thr_struct_p - thread struct
*   *event_p - event mask
*
* Output:
*   td_thr_struct_clear_event - return value
*
* Side effects:
*   none
*/
static td_terr_e
td_thr_struct_clear_event(uthread_t * thr_struct_p,
	void *event2_p)
{

	/*
	 * Don't know how to set events in thread struct yet so better not
	 * execute this.
	 */

	/*
	 * Stops lint warning.
	 */
	{
		thr_struct_p = 0;
		event2_p = 0;
	}

	return (TD_NOT_DONE);
}

/* Make eventnotset, eventorset, eventandset macros? */

/*
* Description:
*   Take compliment of the events in *event1_p and return in
* *event1_p.
*
* Input:
*   *event2_p - events in set
*
* Output:
*   *event1_p - OR'ed events
*
* Side effects:
*   none
*/
static void
eventnotset(td_thr_events_t *event1_p, td_thr_events_t *event2_p)
{
	int	i;

	for (i = 0; i < TD_EVENTSIZE; i++) {
		event1_p->event_bits[i] =
			~event2_p->event_bits[i];
	}

}

/*
* Description:
*   OR the events in *event1_p and *event2_p and return in *event1_p.
*
* Input:
*   *event1_p - events in set 1
*   *event2_p - events in set 2
*
* Output:
*   *event1_p - OR'ed events
*
* Side effects:
*   none
*/
static void
eventorset(td_thr_events_t *event1_p, td_thr_events_t *event2_p)
{
	int	i;

	for (i = 0; i < TD_EVENTSIZE; i++) {
		event1_p->event_bits[i] =
			event1_p->event_bits[i] |
			event2_p->event_bits[i];
	}

}

/*
* Description:
*   AND the events in *event1_p and *event2_p and return in *event1_p.
*
* Input:
*   *event1_p - events in set 1
*   *event2_p - events in set 2
*
* Output:
*   *event1_p - AND'ed events
*
* Side effects:
*   none
*/

static void
eventandset(td_thr_events_t *event1_p, td_thr_events_t *event2_p)
{
	int	i;

	for (i = 0; i < TD_EVENTSIZE; i++) {
		event1_p->event_bits[i] =
			event1_p->event_bits[i] &
			event2_p->event_bits[i];
	}
	return;

}

/*
* Description:
*   Set event mask to enable event. event is turned on in
* event mask for thread.  If a thread encounters an event
* for which its event mask is on, notification will be sent
* to the debugger.
*   Addresses for each event are provided to the
* debugger.  It is assumed that a breakpoint of some type will
* be placed at that address.  If the event mask for the thread
* is on, the instruction at the address will be executed.
* Otherwise, the instruction will be skipped.
*   If thread handle is NULL, event is set in global
* event mask.  The global event mask is applied to all threads.
*
* Input:
*   *th_p - thread handle
*   event - event number(see thread_db.h)
*
* Output:
*   td_thr_set_event - return value
*
* Side effects:
*   Event mask in thread structure is updated
* Imported functions called: ps_pstop, ps_pcontinue,
* ps_pdwrite, ps_pdread.
*/
td_err_e
td_thr_set_event(const td_thrhandle_t *th_p, td_thr_events_t event)
{

	td_err_e	return_val = TD_NOT_DONE;
	td_terr_e	td_return = TD_TERR;
	td_thrinfo_t	ti;
	td_thr_events_t	thread_events;


#ifdef TEST_PS_CALLS
	if (td_noop) {
		ps_pstop(0);
		ps_pcontinue(0);
		ps_pdread(0, 0, 0, 0);
		if (return_val != TD_NOT_DONE) {
			ps_pdwrite(0, 0, 0, 0);
		}
		return (return_val);
	}
#endif

	/*
	 * Check for setting of global event mask.
	 */

	if (th_p == NULL) {

		/*
		 * Clear global event mask.
		 */
		if (!mutex_lock(&__gd_lock)) {
			eventorset(&td_global_events, &event);
			mutex_unlock(&__gd_lock);
		} else {
			return_val = TD_ERR;
		}
	} else {
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
		 * A read and a write is required.  Stop the process.
		 */
		if (ps_pstop(th_p->th_ta_p.ph_p) == PS_OK) {

			/*
			 * Get the event mask from the thread information
			 * struct,
			 * OR in the global event mask, write event mask to
			 * thread struct.
			 */
			return_val = __td_thr_get_info(th_p, &ti);
			if (return_val == TD_OK) {
				thread_events = ti.ti_events;
				eventorset(&thread_events, &event);

				/*
				 * OR in global event mask.
				 */
				if (!mutex_lock(&__gd_lock)) {
					eventorset(&thread_events,
						&td_global_events);
					mutex_unlock(&__gd_lock);
				} else {
					return_val = TD_ERR;
				}

				td_return = __td_thr_struct_set(th_p,
					td_thr_struct_set_event,
					(void *) &thread_events);
				if (td_return != TD_TOK) {
					return_val = TD_DBERR;
				}
			}

			/*
			* Continue process.
			*/

			if (ps_pcontinue(th_p->th_ta_p.ph_p) != PS_OK) {
				return_val = TD_DBERR;
			}
		}			/* ps_pstop succeeded   */
		rw_unlock(&(th_p->th_ta_p->rwlock));
	}

	return (TD_NOT_DONE);
}

#endif /* END PHASE2 */
