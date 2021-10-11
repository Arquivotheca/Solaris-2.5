/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)schedule.c	1.27	95/07/13 SMI"	/* SVr4.0 1.15.1.7	*/

#include "stdarg.h"
#include "lpsched.h"

extern int isStartingForms;

typedef struct later {
	struct later *		next;
	int			event,
				ticks;
	union arg {
		PSTATUS *		printer;
		RSTATUS *		request;
		SSTATUS *		system;
		FSTATUS *		form;
	}			arg;
}			LATER;

static LATER		LaterHead	= { 0 },
			TempHead;

static void		ev_interf(PSTATUS *);
static void		ev_system(SSTATUS *);
static void		ev_message(PSTATUS *);
static void		ev_form_message(FSTATUS *);
static void		ev_pollbsdsys(SSTATUS *);
static void		_ev_pollbsdsys(SSTATUS *, PSTATUS *);
static void		ev_status();
static void		status_failed(SSTATUS *, PSTATUS *, int, char *);

static int		ev_slowf(RSTATUS *);
static int		ev_notify(RSTATUS *);

static EXEC		*find_exec_slot(EXEC *, int);

/*
 * schedule() - SCHEDULE BY EVENT
 */

/*VARARGS1*/
void
schedule(int event, ...)
{
	ENTRY ("schedule")

	va_list			ap;

	LATER *			plprev;
	LATER *			pl;
	LATER *			plnext	= 0;

#if	defined(CHECK_CHILDREN)
	int			do_ev_checkchild;
#endif

	register PSTATUS *	pps;
	register RSTATUS *	prs;
	register SSTATUS *	pss;
	register FSTATUS *	pfs;

	/*
	 * If we're in the process of shutting down, don't
	 * schedule anything.
	 */
	if (Shutdown)
		return;

	va_start (ap, event);

	/*
	 * If we're still in the process of starting up, don't start
	 * anything! Schedule it for one tick later. While we're starting
	 * ticks aren't counted, so the events won't be started.
	 * HOWEVER, with a count of 1, a single EV_ALARM after we're
	 * finished starting will be enough to clear all things scheduled
	 * for later.
	 */
	if (Starting) {
		switch (event) {

		case EV_INTERF:
		case EV_ENABLE:
			pps = va_arg(ap, PSTATUS *);
			schedule (EV_LATER, 1, event, pps);
			goto Return;

		case EV_SLOWF:
		case EV_NOTIFY:
			prs = va_arg(ap, RSTATUS *);
			schedule (EV_LATER, 1, event, prs);
			goto Return;

		case EV_SYSTEM:
			pss = va_arg(ap, SSTATUS *);
			schedule (EV_LATER, 1, event, pss);
			goto Return;

		case EV_MESSAGE:
			pps = va_arg(ap, PSTATUS *);
			schedule (EV_LATER, 1, event, pps);
			goto Return;

		case EV_FORM_MESSAGE:
			pfs = va_arg(ap, FSTATUS *);
			schedule (EV_LATER, 1, event, pfs);
			goto Return;

		case EV_LATER:
			/*
			 * This is okay--in fact it may be us!
			 */
			break;

		case EV_ALARM:
			/*
			 * The alarm will go off again, hold off for now.
			 */
			goto Return;

		}
	}

	/*
	 * Schedule something:
	 */
	switch (event) {

	case EV_INTERF:
		if ((pps = va_arg(ap, PSTATUS *)))
			ev_interf (pps);

		else
			for (pps = walk_ptable(1); pps; pps = walk_ptable(0))
				ev_interf (pps);

		break;

	/*
	 * The EV_ENABLE event is used to get a printer going again
	 * after waiting for a fault to be cleared. We used to use
	 * just the EV_INTERF event, but this wasn't enough: For
	 * requests that can go on several different printers (e.g.
	 * queued for class, queued for ``any''), a printer is
	 * arbitrarily assigned. The EV_INTERF event just checks
	 * assignments, not possibilities, so a printer with no
	 * assigned requests but still eligible to handle one or
	 * more requests would never automatically start up again after
	 * a fault. The EV_ENABLE event calls "enable()" which eventually
	 * gets around to invoking the EV_INTERF event. However, it first
	 * calls "queue_attract()" to get an eligible request assigned
	 * so that things proceed. This also makes sense from the
	 * following standpoint: The documented method of getting a
	 * printer going, while it is waiting for auto-retry, is to
	 * manually issue the enable command!
	 *
	 * Note: "enable()" will destroy the current record of the fault,
	 * so if the fault is still with us any new alert will not include
	 * the history of each repeated fault. This is a plus and a minus,
	 * usually a minus: While a repeated fault may occasionally show
	 * a varied record, usually the same reason is given each time;
	 * before switching to EV_ENABLE we typically saw a boring, long
	 * list of identical reasons.
	 */
	case EV_ENABLE:
		if ((pps = va_arg(ap, PSTATUS *)))
			enable (pps);
		else
			for (pps = walk_ptable(1); pps; pps = walk_ptable(0))
				enable (pps);
		break;

	case EV_SLOWF:
		if ((prs = va_arg(ap, RSTATUS *)))
			(void) ev_slowf (prs);
		else
			for (prs = Request_List; prs && ev_slowf(prs) != -1;
				prs = prs->next);
		break;

	case EV_NOTIFY:
		if ((prs = va_arg(ap, RSTATUS *)))
			(void) ev_notify (prs);
		else
			for (prs = Request_List; prs && ev_notify(prs) != -1;
				prs = prs->next);
		break;

	case EV_SYSTEM:
		pss = va_arg(ap, SSTATUS *);
		ev_system (pss);
		break;

	case EV_MESSAGE:
		pps = va_arg(ap, PSTATUS *);
		ev_message(pps);
		break;

	case EV_FORM_MESSAGE:
		pfs = va_arg(ap, FSTATUS *);
		ev_form_message(pfs);
		break;

	case EV_LATER:
		pl = (LATER *)Malloc(sizeof (LATER));

		if (!LaterHead.next)
			alarm (CLOCK_TICK);

		pl->next = LaterHead.next;
		LaterHead.next = pl;

		pl->ticks = va_arg(ap, int);
		pl->event = va_arg(ap, int);
		switch (pl->event) {

		case EV_MESSAGE:
		case EV_INTERF:
		case EV_ENABLE:
			pl->arg.printer = va_arg(ap, PSTATUS *);
			if (pl->arg.printer)
				pl->arg.printer->status |= PS_LATER;
			break;

		case EV_FORM_MESSAGE:
			pl->arg.form = va_arg(ap, FSTATUS *);
			break;

		case EV_SLOWF:
		case EV_NOTIFY:
			pl->arg.request = va_arg(ap, RSTATUS *);
			break;

		case EV_SYSTEM:
		case EV_POLLBSDSYSTEMS:
			pl->arg.system = va_arg(ap, SSTATUS *);
			break;

#if	defined(CHECK_CHILDREN)
		case EV_CHECKCHILD:
			break;
#endif

		case EV_STATUS:
			break;

		}
		break;

	case EV_ALARM:
		Sig_Alrm = 0;

		/*
		 * The act of scheduling some of the ``laters'' may
		 * cause new ``laters'' to be added to the list.
		 * To ease the handling of the linked list, we first
		 * run through the list and move all events ready to
		 * be scheduled to another list. Then we schedule the
		 * events off the new list. This leaves the main ``later''
		 * list ready for new events.
		 */
		TempHead.next = 0;
		for (pl = (plprev = &LaterHead)->next; pl; pl = plnext) {
			plnext = pl->next;
			if (--pl->ticks)
				plprev = pl;
			else {
				plprev->next = plnext;

				pl->next = TempHead.next;
				TempHead.next = pl;
			}
		}

		for (pl = TempHead.next; pl; pl = plnext) {
			plnext = pl->next;
			switch (pl->event) {

			case EV_MESSAGE:
			case EV_INTERF:
			case EV_ENABLE:
				pl->arg.printer->status &= ~PS_LATER;
				schedule (pl->event, pl->arg.printer);
				break;

			case EV_FORM_MESSAGE:
				schedule (pl->event, pl->arg.form);
				break;

			case EV_SLOWF:
			case EV_NOTIFY:
				schedule (pl->event, pl->arg.request);
				break;

			case EV_SYSTEM:
			case EV_POLLBSDSYSTEMS:
				schedule (pl->event, pl->arg.system);
				break;

#if	defined(CHECK_CHILDREN)
			case EV_CHECKCHILD:
				/*
				 * Do this one only once.
				 */
				do_ev_checkchild = 1;
				break;
#endif

			case EV_STATUS:
				schedule (pl->event);
				break;

			}
			Free ((char *)pl);
		}

#if	defined(CHECK_CHILDREN)
		if (do_ev_checkchild)
			schedule (EV_CHECKCHILD);
#endif	/* CHECK_CHILDREN */

		if (LaterHead.next)
			alarm (CLOCK_TICK);
		break;

#if	defined(CHECK_CHILDREN)
	case EV_CHECKCHILD:
		ev_checkchild ();
		break;
#endif


	case EV_POLLBSDSYSTEMS:
		pss = va_arg(ap, SSTATUS *);
		ev_pollbsdsys (pss);
		break;


	case EV_STATUS:
		ev_status ();
		break;

	}

Return:	va_end (ap);

	return;
}

/*
 * maybe_schedule() - MAYBE SCHEDULE SOMETHING FOR A REQUEST
 */

void
maybe_schedule(RSTATUS *prs)
{
	ENTRY ("maybe_schedule")

	/*
	 * Use this routine if a request has been changed by some
	 * means so that it is ready for filtering or printing,
	 * but a previous filtering or printing process for this
	 * request MAY NOT have finished yet. If a process is still
	 * running, then the cleanup of that process will cause
	 * "schedule()" to be called. Calling "schedule()" regardless
	 * might make another request slip ahead of this request.
	 */

	/*
	 * "schedule()" will refuse if this request is filtering.
	 * It will also refuse if the request ``was'' filtering
	 * but the filter was terminated in "validate_request()",
	 * because we can not have heard from the filter process
	 * yet. Also, when called with a particular request,
	 * "schedule()" won't slip another request ahead.
	 */
	if (NEEDS_FILTERING(prs))
		schedule (EV_SLOWF, prs);

	else if (!(prs->request->outcome & RS_STOPPED))
		schedule (EV_INTERF, prs->printer);

	return;
}

static void
ev_message(PSTATUS *pps)
{
	ENTRY ("ev_message")

	register RSTATUS	*prs;
	char			*systemName;
	char			**sysList;
	SSTATUS			*pss;
	char			toSelf;

	toSelf = 0;
	sysList = NULL;
	BEGIN_WALK_BY_PRINTER_LOOP (prs, pps)
		note("prs (%d) pps (%d)\n", prs, pps);
		systemName = (prs->secure ? prs->secure->system : NULL);
		if ((!systemName) || STREQU(systemName, Local_System) ||
		    ((pss = search_stable(systemName)) == NULL)) {
			if (!toSelf) {
				toSelf = 1;
				DEBUGLOG2(
				"exec EX_FAULT_MESSAGE on system (%s)\n",
					(systemName ? systemName : "local"));
				exec(EX_FAULT_MESSAGE, pps, prs);
			} else
				DEBUGLOG2("dup exec on system (%s)\n",
					(systemName ? systemName : "local"));
		} else if (pss->system->protocol == S5_PROTO) {
			if (!searchlist (systemName, sysList)) {
				appendlist(&sysList, systemName);
				DEBUGLOG3(
				"going remote REX_FAULT_MESSAGE to (%d) (%s)\n",
					pss, systemName);
				prs->status |= RSS_SEND_FAULT_MESSAGE;
				rexec(pss, REX_FAULT_MESSAGE, pps, prs);
			} else
				DEBUGLOG3("dup remote system (%d) (%s)\n", pss,
					systemName);
		}
		else
			DEBUGLOG3(
			"not going remote REX_FAULT_MESSAGE to BSD (%d) (%s)\n",
				pss, systemName);
	END_WALK_LOOP
	freelist(sysList);
}

static void
ev_form_message_body(FSTATUS *pfs, RSTATUS *prs, char *toSelf, char ***sysList)
{
	ENTRY ("ev_form_message_body")

	char *systemName;
	SSTATUS *pss;

	DEBUGLOG4("prs (%d) pfs (%d) isStart %d\n", prs, pfs, isStartingForms);
	systemName = (prs->secure ? prs->secure->system : NULL);
	if ((!systemName) || STREQU(systemName, Local_System) ||
	    ((pss = search_stable(systemName)) == NULL)) {
		if (!*toSelf) {
			*toSelf = 1;
			DEBUGLOG2("exec EX_FORM_MESSAGE on system (%s)\n",
				(systemName ? systemName : "local"));
			exec(EX_FORM_MESSAGE, pfs);
		} else
			DEBUGLOG2("dup exec on system (%s)\n",
				(systemName ? systemName : "local"));
	} else if (pss->system->protocol == S5_PROTO && (!isStartingForms)) {
		/* seems to hang when starting */
		if (!searchlist (systemName, *sysList)) {
			appendlist(sysList, systemName);
			DEBUGLOG3("send REX_FORM_MESSAGE to (%d) (%s)\n",
				pss, systemName);
			prs->status |= RSS_SEND_FORM_MESSAGE;
			rexec(pss, REX_FORM_MESSAGE, pfs, prs);
		} else
			DEBUGLOG3("dup remote system (%d) (%s)\n", pss,
				systemName);
	} else if (isStartingForms)
		DEBUGLOG2("starting forms (%s)\n", systemName);
	else
		DEBUGLOG3(
		"not going remote REX_FAULT_MESSAGE to BSD (%d) (%s)\n",
			pss, systemName);
}

static void
ev_form_message(FSTATUS *pfs)
{
	ENTRY ("ev_form_message")

	register RSTATUS	*prs;
	char **sysList;
	char toSelf;

	toSelf = 0;
	sysList = NULL;
	BEGIN_WALK_BY_FORM_LOOP (prs, pfs)
	ev_form_message_body(pfs, prs, &toSelf, &sysList);
	END_WALK_LOOP
	if (NewRequest && (NewRequest->form == pfs))
		ev_form_message_body(pfs, NewRequest, &toSelf, &sysList);

	freelist(sysList);
}

/*
 * ev_interf() - CHECK AND EXEC INTERFACE PROGRAM
 */

/*
 * Macro to check if the request needs a print wheel or character set (S)
 * and the printer (P) has it mounted or can select it. Since the request
 * has already been approved for the printer, we don't have to check the
 * character set, just the mount. If the printer has selectable character
 * sets, there's nothing to check so the request is ready to print.
 */
#define	MATCH(PRS, PPS) (\
		!(PPS)->printer->daisy || \
		!(PRS)->pwheel_name || \
		!((PRS)->status & RSS_PWMAND) || \
		STREQU((PRS)->pwheel_name, NAME_ANY) || \
		((PPS)->pwheel_name && \
		STREQU((PPS)->pwheel_name, (PRS)->pwheel_name)))


static void
ev_interf(PSTATUS *pps)
{
	ENTRY ("ev_interf")

	register RSTATUS	*prs;


	/*
	 * If the printer isn't tied up doing something
	 * else, and isn't disabled, see if there is a request
	 * waiting to print on it. Note: We don't include
	 * PS_FAULTED here, because simply having a printer
	 * fault (without also being disabled) isn't sufficient
	 * to keep us from trying again. (In fact, we HAVE TO
	 * try again, to see if the fault has gone away.)
	 *
	 * NOTE: If the printer is faulted but the filter controlling
	 * the printer is waiting for the fault to clear, a
	 * request will still be attached to the printer, as
	 * evidenced by "pps->request", so we won't try to
	 * schedule another request!
	 */
	if (pps->request || pps->status & (PS_DISABLED|PS_LATER|PS_BUSY))
		return;

	BEGIN_WALK_BY_PRINTER_LOOP (prs, pps)

	DEBUGLOG4("check job %s on %s %s\n", prs->secure->req_id,
		(pps->status & PS_REMOTE ? "remote" : "local"),
		pps->printer->name);
	/*
	 * Just because the printer isn't busy and the
	 * request is assigned to this printer, don't get the
	 * idea that the request can't be printing (RS_ACTIVE),
	 * because another printer may still have the request
	 * attached but we've not yet heard from the child
	 * process controlling that printer.
	 */
	if (qchk_waiting(prs))

		if (pps->status & PS_REMOTE) {
			/*
			 * We have a request waiting to go to a remote
			 * Spooler. Let "rexec()" worry about getting
			 * a connection, etc.
			 */
			if (pps->system)
				rexec (pps->system, REX_INTERF, pps, prs);

		} else if (isFormUsableOnPrinter(pps, prs->form) &&
			    MATCH(prs, pps)) {
			/*
			 * We have the waiting request, we have
			 * the ready (local) printer. If the exec fails
			 * because the fork failed, schedule a
			 * try later and claim we succeeded. The
			 * later attempt will sort things out,
			 * e.g. will re-schedule if the fork fails
			 * again.
			 */
			pps->request = prs;
			if (exec(EX_INTERF, pps) == 0) {
				pps->status |= PS_BUSY;
				return;
			}
			pps->request = 0;
			if (errno == EAGAIN) {
				load_str (&pps->dis_reason, CUZ_NOFORK);
				schedule (EV_LATER, WHEN_FORK, EV_ENABLE, pps);
				return;
			}
		}
	END_WALK_LOOP

	return;
}

/*
 * ev_slowf() - CHECK AND EXEC SLOW FILTER
 */

static int
ev_slowf(RSTATUS *prs)
{
	ENTRY ("ev_slowf")

	register EXEC		*ep;

	/*
	 * Return -1 if no more can be executed (no more exec slots)
	 * or if it's unwise to execute any more (fork failed).
	 */

	if (!(ep = find_exec_slot(Exec_Slow, ET_SlowSize)))
		return (-1);

	if (!(prs->request->outcome & (RS_DONE|RS_HELD|RS_ACTIVE)) &&
	    NEEDS_FILTERING(prs)) {
		(prs->exec = ep)->ex.request = prs;
		if (exec(EX_SLOWF, prs) != 0) {
			ep->ex.request = 0;
			prs->exec = 0;
			if (errno == EAGAIN) {
				schedule (EV_LATER, WHEN_FORK, EV_SLOWF, prs);
				return (-1);
			}
		}
	}
	return (0);
}

/*
 * ev_notify() - CHECK AND EXEC NOTIFICATION
 */

static int
ev_notify(RSTATUS *prs)
{
	ENTRY ("ev_notify")

	register EXEC		*ep;

	register SSTATUS	*pss;

	/*
	 * Return -1 if no more can be executed (no more exec slots)
	 * or if it's unwise to execute any more (fork failed, already
	 * sent one to remote side).
	 */

	/*
	 * If the job came from a remote machine, we forward the
	 * outcome of the request to the network manager for sending
	 * to the remote side.
	 */
	DEBUGLOG3("ev_notify start %s %x\n", prs->secure->req_id, prs->status);
	if (prs->request->actions & ACT_NOTIFY) {
		if (prs->request->outcome & RS_NOTIFY) {

			pss = search_stable(prs->secure->system);
			if (!pss) {
				DEBUGLOG2(
					"Tried to notify unknown system (%s)\n",
					prs->secure->system);
				prs->request->actions &= ~ACT_NOTIFY;
				return (0);  /* but try another request */
			}
			prs->status |= RSS_SENDREMOTE;
			;
			rexec (pss, REX_NOTIFY, prs);
			DEBUGLOG3("ev_notify end rexec %s %x\n",
				prs->secure->req_id, prs->status);
			return (-1); /* this one went--no more for now */
		}

	/*
	 * If the job didn't come from a remote system,
	 * we'll try to start a process to send the notification
	 * to the user. But we only allow so many notifications
	 * to run at the same time, so we may not be able to
	 * do it.
	 */
	} else if (!(ep = find_exec_slot(Exec_Notify, ET_NotifySize)))
		return (-1);

	else if (prs->request->outcome & RS_NOTIFY &&
		!(prs->request->outcome & RS_NOTIFYING)) {

		(prs->exec = ep)->ex.request = prs;
		if (exec(EX_NOTIFY, prs) != 0) {
			ep->ex.request = 0;
			prs->exec = 0;
			if (errno == EAGAIN) {
				schedule (EV_LATER, WHEN_FORK, EV_NOTIFY, prs);
				return (-1);
			}
		}
	}
	return (0);
}

/*
 * ev_system() - CHECK AND ``EXEC'' SYSTEM
 */

static void
ev_system(SSTATUS *pss)
{
	ENTRY ("ev_system")

	RSTATUS *		prs;


	/*
	 * If the connection to the system isn't busy and
	 * we have something to send there, send it!
	 */
	DEBUGLOG2("ev_system: check passed system %s\n",
		(pss ? pss->system->name : "NULL"));
	if (pss->exec->flags & (EXF_WAITJOB | EXF_WAITCHILD)) {
		int flg;
		flg = pss->exec->flags;
		DEBUGLOG5("ev_system: system %s is busy %d %s %s\n",
			pss->system->name, flg,
			(flg & EXF_WAITJOB ? "EXF_WAITJOB" : ""),
			(flg & EXF_WAITCHILD ? "EXF_WAITCHILD" : ""));
		return;
	}

	/*
	 * Note: If the remote send is successful, leave because we can
	 * send only one at a time. If the remote send is not successful,
	 * leave because another try won't help.
	 */

	/*
	 * Look for the next cancellation that has to go
	 * out to this system:
	 */
	DEBUGLOG1("ev_system: check cancelations\n" );
	for (prs = Request_List; prs; prs = prs->next) {
		DEBUGLOG4("request: %s, dest: %s, status 0x%x\n",
			(prs->req_file ? prs->req_file : "NULL"),
			(prs->request ? prs->request->destination : "NULL"),
			prs->status);
		if (PRINTING_AT(prs, pss) &&
		    prs->request->outcome & RS_CANCELLED &&
		    prs->status & RSS_SENDREMOTE) {
			DEBUGLOG2(
			"ev_system: process request for REX_CANCEL to %s\n",
				pss->system->name);
			rexec (pss, REX_CANCEL, prs);
			return;
		}
	}

	/*
	 * Look for the next status check that has to go
	 * out to this system:
	 */
	DEBUGLOG1("ev_system: check status\n" );
	for (prs = Status_List; prs; prs = prs->next) {
		DEBUGLOG2("system: checking status request for %s\n",
			((prs->system == NULL || prs->system->system == NULL ||
			 prs->system->system->name == NULL) ? "EMPTY" : 
			prs->system->system->name));
		if (prs->system == NULL) {	/* should never happen */
			DEBUGLOG1(
			   "ev_system: fixing empty request on status chain\n");
			remover(prs);
		}
		if (prs->system == pss) {
			DEBUGLOG2(
			"ev_system: process request for REX_STATUS to %s\n",
				pss->system->name);
			rexec (pss, REX_STATUS, prs);
			return;
		}
	}

	/*
	 * Look for the next print request that has to go
	 * out to this system:
	 */
	DEBUGLOG1("ev_system: check requests\n" );
	for (prs = Request_List; prs; prs = prs->next) {
		DEBUGLOG4("request: %s, dest: %s, status 0x%x\n",
			(prs->req_file ? prs->req_file : "NULL"),
			(prs->request ? prs->request->destination : "NULL"),
			prs->status);
		if (PRINTING_AT(prs, pss) && qchk_waiting(prs)) {
			if ((prs->printer) &&
			    (prs->printer->status & PS_DISABLED))
				continue;
			DEBUGLOG2(
			"ev_system: process request for REX_INTERF to %s\n",
				pss->system->name);
			rexec (pss, REX_INTERF, prs->printer, prs);
			return;
		}
	}

	/* Look for fault messages to send */

	DEBUGLOG1("ev_system: check faults\n" );
	for (prs = Request_List; prs; prs = prs->next) {
		DEBUGLOG4("request: %s, dest: %s, status 0x%x\n",
			(prs->req_file ? prs->req_file : "NULL"),
			(prs->request ? prs->request->destination : "NULL"),
			prs->status);
		if (prs->system == pss) {
			if (prs->status & RSS_SEND_FAULT_MESSAGE) {
				DEBUGLOG2(
		"ev_system: process request for REX_FAULT_MESSAGE to %s\n",
					pss->system->name);
				rexec(pss, REX_FAULT_MESSAGE, prs->printer,
					prs);
				return;
			} else if (prs->status & RSS_SEND_FORM_MESSAGE) {
				DEBUGLOG2(
		"ev_system: process requests for REX_FORM_MESSAGE to %s\n",
					pss->system->name);
				rexec(pss, REX_FORM_MESSAGE, prs->form, prs);
				return;
			}
		}
	}
	/*
	 * Look for the next notification that has to go
	 * out to this system:
	 */
	DEBUGLOG1("ev_system: check notifications\n" );
	for (prs = Request_List; prs; prs = prs->next) {
		DEBUGLOG4("request: %s, dest: %s, status 0x%x\n",
			(prs->req_file ? prs->req_file : "NULL"),
			(prs->request ? prs->request->destination : "NULL"),
			prs->status);
		if (ORIGINATING_AT(prs, pss) &&
		    prs->request->outcome & RS_NOTIFY &&
		    prs->status & RSS_SENDREMOTE) {
			DEBUGLOG2(
			"ev_system: process request for REX_NOTIFY to %s\n",
				pss->system->name);
			rexec(pss, REX_NOTIFY, prs);
			return;
		}
	}
	DEBUGLOG2("ev_system: nothing to process for %s\n", pss->system->name);
	return;
}

/*
 * ev_pollbsdsys() - send out a poll to bsd systems
 * if pss is null, check for all BSD systems and poll any with pending job.
 * else pss is a specific system, poll it if pending jobs > threshold.
 */

static void
ev_pollbsdsys(SSTATUS *pss)
{
	ENTRY ("ev_pollbsdsys")
	RSTATUS *prs;

	if (!pss) {
		int x;

		DEBUGLOG1("ev_pollbsdsys: pss is null\n");
		for (x = 0; (pss = SStatus[x]) != NULL; x++)
			if (pss->system->protocol == BSD_PROTO)
				for (prs = Request_List; prs; prs = prs->next)
					if (PRINTING_AT(prs, pss) &&
					    prs->request->outcome & RS_SENT) {
						_ev_pollbsdsys(pss, NULL);
						break;
					}
	} else {
		int n = 0, limit;

		/*
		 * count the number of jobs for the printer which just took
		 * this job.
		 */
		for (prs = Request_List; prs; prs = prs->next)
			if (PRINTING_AT(prs, pss) &&
			    prs->printer == pss->exec->ex.request->printer &&
			    prs->request->outcome & RS_SENT)
				n++;

		limit = (pss->system->threshold < 0) ? MAX_BSD_JOBS :
			pss->system->threshold;

		DEBUGLOG4("ev_pollbsdsys: system %s count %d %d\n",
			pss->system->name, n, limit);

		if (n <= limit)
			return;

		/*
		 * check for a status query already queued up for this printer
		 */
		for (prs = Status_List; prs; prs = prs->next)
			if (prs->printer == pss->exec->ex.request->printer) {
				DEBUGLOG2("found status request for %s\n",
					prs->system->system->name);
				return;
			}

		_ev_pollbsdsys(pss, pss->exec->ex.request->printer);
	}

}

static void
_ev_pollbsdsys(SSTATUS *pss, PSTATUS *p)
{
	ENTRY ("_ev_pollbsdsys")

	DEBUGLOG3("_ev_pollbsdsys: status for printer: %s on: %s\n",
		(p ? p->remote_name : "all"), pss->system->name);

	mkreqInd(pss, S_INQUIRE_REQUEST_RANK, p);
	schedule(EV_SYSTEM, pss);
}

/*
 * find_exec_slot() - FIND AVAILABLE EXEC SLOT
 */

static EXEC *
find_exec_slot(EXEC *exec_table, int size)
{
	ENTRY ("find_exec_slot")

	register EXEC *		ep;
	register EXEC *		last_ep	= exec_table + size - 1;

	for (ep = exec_table; ep <= last_ep; ep++)
		if (ep->pid == 0)
			return (ep);

	return (0);
}

/*
 * ev_status() - see if any status queries haven't returned yet.
 */

static void
ev_status()
{
	ENTRY ("ev_status")

	SSTATUS	*pss;
	int	i;
	time_t	now;

	now = time((time_t *)0);
	DEBUGLOG2("Now %s\n", conv_time(&now));

	/*
	 * A single event will wakeup WHEN_STATUS seconds after it is
	 * scheduled.  However, if another event is scheduled earlier, the
	 * scheduler will wakeup based on that event and not our event.
	 * Thus, our event could wakeup as much as CLOCK_TICK seconds early.
	 * To fix, check delta from now, minus 1 CLOCK_TICK.
	 */
	for (i = 0; (pss = SStatus[i]) != NULL; i++) {
		DEBUGLOG3("status timeout: %s %s\n", pss->system->name,
			conv_time(&(pss->laststat)));
		if (pss->laststat && (now - pss->laststat) >=
		    (WHEN_STATUS * CLOCK_TICK) - CLOCK_TICK)
			remote_status_failed(pss);
	}
}

void
remote_status_failed(SSTATUS *pss)
{
	ENTRY ("remote_status_failed")

	RSTATUS *prs;

	if (!(pss->exec->flags & EXF_WAITJOB)) {
		DEBUGLOG1("error-not waiting!\n");
		return;
	}
	if (!(pss->exec->ex.request)) {
		DEBUGLOG1("error-fallen on deaf ears!\n");
		pss->exec->flags &= (~EXF_WAITJOB);
		pss->laststat = 0;
		schedule(EV_SYSTEM, pss);
		return;
	}
	if (!(pss->exec->ex.request->request->outcome & RS_SENDING)) {
		DEBUGLOG1("error-no request sent!\n");
		return;
	}

	/*
	 * Remove any pending status queries for this system, since they
	 * will be handled by this timeout.  Don't remove the current
	 * status request, since other parts of the code reference the
	 * request through the pss pointer.
	 */

        for (prs = Status_List; prs; prs = prs->next)
		if (prs->system == pss && prs != pss->exec->ex.request) {
			rmreq(prs);
			prs = Status_List;
			if (! prs)
				break;
		}
 
	md_wakeup (pss);
}
