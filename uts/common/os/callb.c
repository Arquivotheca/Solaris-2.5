/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)callb.c	1.6	94/03/31 SMI"

#include <sys/param.h>
#include <sys/t_lock.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/cpuvar.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/callb.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <sys/map.h>
#include <sys/swap.h>
#include <sys/vmsystm.h>
#include <sys/class.h>
#include <sys/debug.h>

#define	CB_MAXNAME	16

/*
 * The callb mechanism provides generic event scheduling/echoing.
 * A callb function is registered and called on behalf of the event.
 */
typedef struct callb {
	struct callb	*c_next; 	/* next in class or on freelist */
	char		c_flag;		/* info about the callb state */
	char		c_class;	/* this callb's class */
	void		(*c_func)();	/* function to call */
	void		*c_arg;		/* arg to c_func */
	kcondvar_t	c_done_cv;	/* signal callb completion */
	char		c_name[CB_MAXNAME]; /* debug:max name len for a callb */
} callb_t;

/*
 * callb c_flag bitmap definitions
 */
#define	CALLB_FREE		0x0
#define	CALLB_TAKEN		0x1
#define	CALLB_EXECUTING		0x2

/*
 * Basic structure for a callb table.
 * All callbs are organized into different class groups described
 * by ct_class array.
 * The callbs within a class are single-linked and normally run by a
 * serial execution.
 */
typedef struct callb_table {
	kmutex_t ct_lock;		/* protect all callb states */
	callb_t	*ct_freelist; 		/* free callb structures */
	int	ct_ncallb; 		/* num of callbs allocated */
	callb_t	*ct_first_cb[NCBCLASS];	/* ptr to 1st callb in a class */
} callb_table_t;

static callb_table_t callb_table;	/* system level callback table */
static callb_table_t *ct = &callb_table;

/*
 * Init all callb tables in the system.
 */
void
callb_init()
{
	mutex_init(&callb_table.ct_lock, "callb lock", MUTEX_DEFAULT, NULL);
}

/*
 * callout_add() is called to register func() be called later.
 */
callb_id_t
callb_add(void (*func)(void *arg, int code), void *arg, int class, char *name)
{
	callb_t *cp;

	ASSERT(class < NCBCLASS);

	mutex_enter(&ct->ct_lock);
	if ((cp = ct->ct_freelist) == NULL) {
		ct->ct_ncallb++;
		cp = (callb_t *)kmem_zalloc(sizeof (callb_t), KM_SLEEP);
	}
	ct->ct_freelist = cp->c_next;

	cp->c_func = func;
	cp->c_arg = arg;
	cp->c_class = class;
	cp->c_flag |= CALLB_TAKEN;
	ASSERT(strlen(name) <= CB_MAXNAME);
	strcpy(cp->c_name, name);

	/*
	 * Insert the new callb at the head of its class list.
	 */
	cp->c_next = ct->ct_first_cb[class];
	ct->ct_first_cb[class] = cp;

	mutex_exit(&ct->ct_lock);
	return ((callb_id_t)cp);
}

/*
 * callout_delete() is called to remove an entry identified by id
 * that was originally placed there by a call to callout_add().
 * return -1 if fail to delete a callb entry
 * otherwsie return 0.
 */
int
callb_delete(callb_id_t id)
{
	callb_t **pp;
	callb_t *me = (callb_t *)id;

	mutex_enter(&ct->ct_lock);

	for (;;) {
		pp = &ct->ct_first_cb[me->c_class];
		while (*pp != NULL && *pp != me)
			pp = &(*pp)->c_next;

#ifdef DEBUG
		if (*pp != me) {
			cmn_err(CE_WARN,
			    "callb delete bogus entry 0x%x", (int)me);
			mutex_exit(&ct->ct_lock);
			return (-1);
		}
#endif DEBUG

		/*
		 * It is not allowed to delete a callb in the middle of
		 * executing otherwise, the callb_execute() will be confused.
		 */
		if (!(me->c_flag & CALLB_EXECUTING))
			break;

		cv_wait(&me->c_done_cv, &ct->ct_lock);
	}
	/* relink the class list */
	*pp = me->c_next;

	/* clean up myself and return the free callb to the head of freelist */
	me->c_flag = CALLB_FREE;
	me->c_next = ct->ct_freelist;
	ct->ct_freelist = me;

	mutex_exit(&ct->ct_lock);
	return (0);
}

/*
 * mep:		run callb pointed by mep;
 * code:	optional argument for the callb functions.
 */
void
callb_execute(callb_id_t id, int code)
{
	callb_t *cp;
	callb_t *me = (callb_t *)id;

	mutex_enter(&ct->ct_lock);

#ifdef DEBUG /* validate the id */
	cp = ct->ct_first_cb[me->c_class];
	while (cp != NULL && cp != me)
		cp = cp->c_next;

	if (cp == NULL) {
		cmn_err(CE_WARN, "callb executing bogus entry");
		mutex_exit(&ct->ct_lock);
		return;
	}
#endif DEBUG

	while (me->c_flag & CALLB_EXECUTING) {
		cv_wait(&me->c_done_cv, &ct->ct_lock);
		/*
		 * return if the callb is deleted while we're sleeping
		 */
		if (me->c_flag == CALLB_FREE) {
			mutex_exit(&ct->ct_lock);
			return;
		}
	}
	me->c_flag |= CALLB_EXECUTING;

	mutex_exit(&ct->ct_lock);
	(*me->c_func)(me->c_arg, code);
	mutex_enter(&ct->ct_lock);

	me->c_flag &= ~CALLB_EXECUTING;
	cv_broadcast(&me->c_done_cv);

	mutex_exit(&ct->ct_lock);
}

/*
 * class:	indicates to execute all callbs in the same class;
 * code:	optional argument for the callb functions.
 */
void
callb_execute_class(int class, int code)
{
	callb_t *cp;

	ASSERT(class < NCBCLASS);

	mutex_enter(&ct->ct_lock);

	for (cp = ct->ct_first_cb[class]; cp != NULL; cp = cp->c_next) {
		while (cp->c_flag & CALLB_EXECUTING)
			cv_wait(&cp->c_done_cv, &ct->ct_lock);
		/*
		 * cont if the callb is deleted while we're sleeping
		 */
		if (cp->c_flag == CALLB_FREE)
			continue;
		cp->c_flag |= CALLB_EXECUTING;

#ifdef CALLB_DEBUG
		printf("callb_execute: name=%s func=%x arg=%x\n",
			cp->c_name, cp->c_func, cp->c_arg);
#endif CALLB_DEBUG

		mutex_exit(&ct->ct_lock);
		(*cp->c_func)(cp->c_arg, code);
		mutex_enter(&ct->ct_lock);

		cp->c_flag &= ~CALLB_EXECUTING;
		cv_broadcast(&cp->c_done_cv);
	}
	mutex_exit(&ct->ct_lock);
}

/*
 * callers make sure no recursive entries to this func.
 * dp->cc_lockp is registered by callb_add to protect callb_cpr_t structure.
 *
 * Note that this is a generic callback handler for daemon CPR and
 * should NOT be changed to accommondate any specific requirement in a daemon.
 * Individual daemons that require changes to the handler shall write
 * callback routines in their own daemon modules.
 */
void
callb_generic_cpr(void *arg, int code)
{
	callb_cpr_t *cp = (callb_cpr_t *)arg;

	mutex_enter(cp->cc_lockp);

	switch (code) {
	case CB_CODE_CPR_CHKPT:
		cp->cc_events |= CALLB_CPR_START;
		while (!(cp->cc_events & CALLB_CPR_SAFE))
			cv_wait(&cp->cc_callb_cv, cp->cc_lockp);
		break;

	case CB_CODE_CPR_RESUME:
		cp->cc_events &= ~CALLB_CPR_START;
		cv_signal(&cp->cc_stop_cv);

		break;
	}
	mutex_exit(cp->cc_lockp);
}
