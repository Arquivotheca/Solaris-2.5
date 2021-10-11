/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)disp4.c	1.13	95/07/13 SMI"	/* SVr4.0 1.13.1.8	*/

#include "time.h"

#include "dispatch.h"

extern char *LP_KILL_NO_PAPER;
#define PRINTER_ON_SYSTEM(PPS,PSS) \
	(((PPS)->status & PS_REMOTE) && (PPS)->system == (PSS))

extern PSTATUS * search_ptable_remote(char * printer);

/**
 ** s_accept_dest()
 **/

void
s_accept_dest(char *m, MESG *md)
{
	ENTRY ("s_accept_dest")

	char			*destination;

	ushort			status;

	register PSTATUS	*pps;

	register CSTATUS	*pcs;


	getmessage (m, S_ACCEPT_DEST, &destination);

	/*
	 * Have we seen this destination as a printer?
	 */
	if ((pps = search_ptable(destination)))
		if ((pps->status & PS_REJECTED) == 0)
			status = MERRDEST;
		else {
			pps->status &= ~PS_REJECTED;
			(void) time (&pps->rej_date);
			dump_pstatus ();
			status = MOK;
		}

	/*
	 * Have we seen this destination as a class?
	 */
	else if ((pcs = search_ctable(destination)))
		if ((pcs->status & CS_REJECTED) == 0)
			status = MERRDEST;
		else {
			pcs->status &= ~CS_REJECTED;
			(void) time (&pcs->rej_date);
			dump_cstatus ();
			status = MOK;
		}

	else
		status = MNODEST;

	mputm (md, R_ACCEPT_DEST, status);
	return;
}

/**
 ** s_reject_dest()
 **/

void
s_reject_dest(char *m, MESG *md)
{
	ENTRY ("s_reject_dest")

	char			*destination,
				*reason;

	ushort			status;

	register PSTATUS	*pps;

	register CSTATUS	*pcs;


	getmessage (m, S_REJECT_DEST, &destination, &reason);

	/*
	 * Have we seen this destination as a printer?
	 */
	if ((pps = search_ptable(destination)))
		if (pps->status & PS_REJECTED)
			status = MERRDEST;
		else {
			pps->status |= PS_REJECTED;
			(void) time (&pps->rej_date);
			load_str (&pps->rej_reason, reason);
			dump_pstatus ();
			status = MOK;
		}

	/*
	 * Have we seen this destination as a class?
	 */
	else if ((pcs = search_ctable(destination)))
		if (pcs->status & CS_REJECTED)
			status = MERRDEST;
		else {
			pcs->status |= CS_REJECTED;
			(void) time (&pcs->rej_date);
			load_str (&pcs->rej_reason, reason);
			dump_cstatus ();
			status = MOK;
		}

	else
		status = MNODEST;

	mputm (md, R_REJECT_DEST, status);
	return;
}

/**
 ** s_enable_dest()
 **/

void
s_enable_dest(char *m, MESG *md)
{
	ENTRY ("s_enable_dest")

	char			*printer;

	ushort			status;

	register PSTATUS	*pps;


	getmessage (m, S_ENABLE_DEST, &printer);

	/*
	 * Have we seen this printer before?
	 */
	if ((pps = search_ptable(printer)))
		if (enable(pps) == -1)
			status = MERRDEST;
		else
			status = MOK;
	else
		status = MNODEST;

	mputm (md, R_ENABLE_DEST, status);
	return;
}

/**
 ** s_disable_dest()
 **/

void
s_disable_dest(char *m, MESG *md)
{
	ENTRY ("s_disable_dest")

	char			*destination,
				*reason,
				*req_id		= 0;

	ushort			when,
				status;

	register PSTATUS	*pps;


	getmessage (m, S_DISABLE_DEST, &destination, &reason, &when);

	/*
	 * Have we seen this printer before?
	 */
	if ((pps = search_ptable(destination))) {

		/*
		 * If we are to cancel a currently printing request,
		 * we will send back the request's ID.
		 * Save a copy of the ID before calling "disable()",
		 * in case the disabling loses it (e.g. the request
		 * might get attached to another printer). (Actually,
		 * the current implementation won't DETACH the request
		 * from this printer until the child process responds,
		 * but a future implementation might.)
		 */
		if (pps->request && when == 2)
			req_id = Strdup(pps->request->secure->req_id);

		if (disable(pps, reason, (int)when) == -1) {
			if (req_id) {
				Free (req_id);
				req_id = 0;
			}
			status = MERRDEST;
		} else
			status = MOK;

	} else
		status = MNODEST;

	mputm (md, R_DISABLE_DEST, status, NB(req_id));
	if (req_id)
		Free (req_id);

	return;
}

/**
 ** s_load_filter_table()
 **/

void
s_load_filter_table(char *m, MESG *md)
{
	ENTRY ("s_load_filter_table")

	ushort			status;

	trash_filters ();
	if (Loadfilters((char *)0) == -1)
		status = MNOOPEN;
	else {
		/*
		 * This is what makes changing filters expensive!
		 */
		queue_check (qchk_filter);

		status = MOK;
	}

	mputm (md, R_LOAD_FILTER_TABLE, status);
	return;
}

/**
 ** s_unload_filter_table()
 **/

void
s_unload_filter_table(char *m, MESG *md)
{
	ENTRY ("s_unload_filter_table")

	trash_filters ();

	/*
	 * This is what makes changing filters expensive!
	 */
	queue_check (qchk_filter);

	mputm (md, R_UNLOAD_FILTER_TABLE, MOK);
	return;
}

/**
 ** s_load_user_file()
 **/

void
s_load_user_file(char *m, MESG *md)
{
	ENTRY ("s_load_user_file")

	/*
	 * The first call to "getuser()" will load the whole file.
	 */
	trashusers ();

	mputm (md, R_LOAD_USER_FILE, MOK);
	return;
}

/**
 ** s_unload_user_file()
 **/

void
s_unload_user_file(char *m, MESG *md)
{
	ENTRY ("s_unload_user_file")

	trashusers ();	/* THIS WON'T DO TRUE UNLOAD, SORRY! */

	mputm (md, R_UNLOAD_USER_FILE, MOK);
	return;
}

/**
 ** s_load_system()
 **/

#define BUSY_SYSTEM(PSS) \
	((PSS)->exec->md || ((PSS)->exec->flags & EXF_WAITCHILD))

void
s_load_system(char *m, MESG *md)
{
	ENTRY ("s_load_system")

	char			*system;

	ushort			status;

	register SYSTEM		*ps;

	register SSTATUS	*pss;

	register PSTATUS	*pps;


	(void)getmessage (m, S_LOAD_SYSTEM, &system);

	if (!*system)
		status = MNODEST;

	/*
	 * Strange or missing system?
	 */
	else if (!(ps = Getsystem(system))) {
		switch (errno) {
		case EBADF:
			status = MERRDEST;
			break;
		case ENOENT:
		default:
			status = MNODEST;
			break;
		}

	/*
	 * Have we seen this system before?
	 */
	} else if ((pss = search_stable(system))) {

		if (pss->exec->flags & (EXF_WAITCHILD | EXF_WAITJOB))
			status = MBUSY;

		else {
			DROP_MD(pss->exec->md);
			pss->exec->md = 0;

			/*
			 * Other changes don't require doing anything
			 * more than just copying the new information.
			 */
			freesystem (pss->system);
			*(pss->system) = *ps;
			status = MOK;
		}

	/*
	 * Add new system.
	 */
	} else {
		pss = (SSTATUS *)Calloc(1, sizeof(SSTATUS));
		pss->system = (SYSTEM *)Calloc(1, sizeof(SYSTEM));
		pss->exec = (EXEC *)Calloc(1, sizeof(EXEC));

		*(pss->system) = *ps;

		addone ((void ***)&SStatus, pss, &ST_Size, &ST_Count);

		/*
		 * Try re-initializing the ``remoteness'' of orphan
		 * printers (those marked remote with no system).
		 * Properly behaving user-level commands (or better
		 * self-protection on our part) would make this moot
		 * But here we are....
		 */
		for (pps = walk_ptable(1); pps; pps = walk_ptable(0))
			if (PRINTER_ON_SYSTEM(pps, (SSTATUS *)0))
				init_remote_printer (pps, pps->printer);

		status = MOK;
	}

	mputm (md, R_LOAD_SYSTEM, status);
	return;
}

/**
 ** s_unload_system()
 **/

static void
_unload_system(SSTATUS *pss)
{
	ENTRY ("_unload_system")

	if (pss->pmlist)
		freelist (pss->pmlist);
	if (pss->tmp_pmlist)
		freelist (pss->tmp_pmlist);
	if (pss->waiting)
		Free (pss->waiting);
	Free (pss->exec);
	freesystem (pss->system);
	Free (pss->system);
	delone ((void ***)&SStatus, pss, &ST_Size, &ST_Count);
	Free (pss);

	return;
}

void
s_unload_system(char *m, MESG *md)
{
	ENTRY ("s_unload_system")

	char *			system;

	ushort			status;

	register SSTATUS	*pss;

	register PSTATUS	*pps;

	register int		i;


	(void)getmessage (m, S_UNLOAD_SYSTEM, &system);

	/*
	 * Unload ALL systems?
	 */
	if (!*system || STREQU(system, NAME_ALL)) {

		/*
		 * Satisfy the request only if NO system has a printer.
		 */
		status = MOK;
		for (i = 0; (pss = SStatus[i]); i++) {
			for (pps = walk_ptable(1); pps; pps = walk_ptable(0))
				if (PRINTER_ON_SYSTEM(pps, pss)) {
					status = MBUSY;
					break;
				}
			if (pss->exec->flags & EXF_WAITCHILD) {
				status = MBUSY;
				break;
			}
		}
		if (status == MOK)
			/*
			 * DELONE DEPENDENT:
			 * This requires knowing how "delone()" works,
			 * sorry.
			 */
			while ((pss = SStatus[0])) {
				DROP_MD(pss->exec->md);
				_unload_system (pss);
			}

	/*
	 * Have we seen this system before?
	 */
	} else if (!(pss = search_stable(system)))
		status = MNODEST;

	/*
	 * Any printers on this system?
	 */
	else {
		status = MOK;
		for (pps = walk_ptable(1); pps; pps = walk_ptable(0))
			if (PRINTER_ON_SYSTEM(pps, pss)) {
				status = MBUSY;
				break;
			}
		if (pss->exec->flags & EXF_WAITCHILD)
			status = MBUSY;

		if (status == MOK) {
			DEBUGLOG2("drop %0x\n", pss->exec->md);
			DROP_MD(pss->exec->md);
			_unload_system (pss);
		}
	}

	mputm (md, R_UNLOAD_SYSTEM, status);
	return;
}

/**
 ** s_shutdown()
 **/

void
s_shutdown(char *m, MESG *md)
{
	ENTRY ("s_shutdown")

	ushort			immediate;

	int			i;

	SSTATUS *		pss;


	(void)getmessage (m, S_SHUTDOWN, &immediate);

	switch (md->type) {

	case MD_UNKNOWN:	/* Huh? */
	case MD_BOUND:		/* MORE: Not sure about this one */
	case MD_MASTER:		/* This is us. */
		DEBUGLOG2("Received S_SHUTDOWN on a type %d connection\n",
			md->type);
		break;

	case MD_STREAM:
	case MD_SYS_FIFO:
	case MD_USR_FIFO:
		mputm (md, R_SHUTDOWN, MOK);
		lpshut (immediate);
		/*NOTREACHED*/

	case MD_CHILD:
		/*
		 * A S_SHUTDOWN from a network child means that IT has
		 * shut down, not that WE are to shut down.
		 *
		 * We have to clear the message descriptor
		 * so we don't accidently try using it in the future.
		 * Unfortunately, this requires looking through the
		 * system table to see which network child died.
		 */		
		DROP_MD (md);
		for (i = 0; (pss = SStatus[i]); i++)
			if (pss->exec->md == md)
				break;
		if (pss) {
			RSTATUS *prs = pss->exec->ex.request;

			DEBUGLOG3(
				"Trying the %s connection again (request %s)\n",
				pss->system->name,
				(pss->exec->ex.request ?
				pss->exec->ex.request->secure->req_id :
				    "<none>"));
			pss->exec->md = 0;
			if (prs && (prs->request->outcome & RS_DONE)) {	
				DEBUGLOG3(
				"request done: reason(0x%x), outcome(0x%x)\n",
					prs->reason, prs->request->outcome);
				prs->reason = MERRDEST; ;
				prs->request->outcome = RS_PRINTED;
				check_request(prs);
			} else 
				resend_remote (pss, 0);
		}
		break;

	}

	return;
}

/**
 ** s_quiet_alert()
 **/

void
s_quiet_alert(char *m, MESG *md)
{
	ENTRY ("s_quiet_alert")

	char			*name;

	ushort			type,
				status;

	register FSTATUS	*pfs;

	register PSTATUS	*pps;

	register PWSTATUS	*ppws;


	/*
	 * We quiet an alert by cancelling it with "cancel_alert()"
	 * and then resetting the active flag. This effectively just
	 * terminates the process running the alert but tricks the
	 * rest of the Spooler into thinking it is still active.
	 * The alert will be reactivated only AFTER "cancel_alert()"
	 * has been called (to clear the active flag) and then "alert()"
	 * is called again. Thus:
	 *
	 * For printer faults the alert will be reactivated when:
	 *	- a fault is found after the current fault has been
	 *	  cleared (i.e. after successful print or after manually
	 *	  enabled).
	 *
	 * For forms/print-wheels the alert will be reactivated when:
	 *	- the form/print-wheel becomes mounted and then unmounted
	 *	  again, with too many requests still pending;
	 *	- the number of requests falls below the threshold and
	 *	  then rises above it again.
	 */

	(void)getmessage (m, S_QUIET_ALERT, &name, &type);

	if (!*name)
		status = MNODEST;

	else switch (type) {
	case QA_FORM:
		if (!(pfs = search_ftable(name)))
			status = MNODEST;

		else if (!pfs->alert->active)
			status = MERRDEST;

		else {
			cancel_alert (A_FORM, pfs);
			pfs->alert->active = 1;
			status = MOK;
		}
		break;
		
	case QA_PRINTER:
		if (!(pps = search_ptable(name)))
			status = MNODEST;

		else if (!pps->alert->active)
			status = MERRDEST;

		else {
			cancel_alert (A_PRINTER, pps);
			pps->alert->active = 1;
			status = MOK;
		}
		break;
		
	case QA_PRINTWHEEL:
		if (!(ppws = search_pwtable(name)))
			status = MNODEST;

		else if (!ppws->alert->active)
			status = MERRDEST;

		else {
			cancel_alert (A_PWHEEL, ppws);
			ppws->alert->active = 1;
			status = MOK;
		}
		break;
	}
	
	mputm (md, R_QUIET_ALERT, status);
	return;
}

/**
 ** s_send_fault()
 **/

void
s_send_fault(char *m, MESG *md)
{
	ENTRY ("s_send_fault")

	long			key;
	char			*printerOrForm, *alert_text;
	ushort			status;
	register PSTATUS	*pps;
	register FSTATUS	*pfs;

	getmessage (m, S_SEND_FAULT, &printerOrForm, &key, &alert_text);

	DEBUGLOG4("S_SEND_FAULT from (%s) key %x txt (%s)\n", printerOrForm,
		key, alert_text);

	if (key < 0 ) {
		/*
		 * came from remote system, normally keys come from lrand
		 * which returns a nonnegative 32 bit integer
		 */

		DEBUGLOG2("S_SEND_FAULT from remote %s\n", printerOrForm);
		key = -key;
		if (key & PS_FORM_FAULT ) {
			if ((pfs = search_ftable(printerOrForm))) {
				DEBUGLOG2("key %x\n", key);
				dump_form_msg(pfs, alert_text); 
				DEBUGLOG3(
				"exec EX_FORM_MESSAGE key %d from %s\n",
					key, printerOrForm);
				exec(EX_FORM_MESSAGE,pfs);
				status = MOK;
			} else
				status = MNOMEDIA;
		} else {
			if ((pps = search_ptable_remote(printerOrForm))) {
				DEBUGLOG3("key %x & pr stat before %x -> ",
					key, pps->status);
				pps->status &=
					~(PS_FAULTED | PS_BUSY | PS_SHOW_FAULT);
				pps->status |= key &
					(PS_FAULTED | PS_BUSY | PS_SHOW_FAULT);
				DEBUGLOG2("after %x\n", pps->status);

				load_str(&pps->fault_reason, alert_text);
				dump_fault_status(pps);
				DEBUGLOG3(
				"exec EX_FAULT_MESSAGE key %d from %s\n",
					key, printerOrForm);
				exec(EX_FAULT_MESSAGE, pps,NULL);
				status = MOK;
			} else
				status = MERRDEST;
		}
	}  else if (!(pps = search_ptable(printerOrForm)) || (!pps->exec) ||
		pps->exec->key != key || !pps->request) {

		DEBUGLOG5("S_SEND_FAULT error %s %d %d %d\n",
			printerOrForm, pps, key,
			(pps && pps->exec ? pps->exec->key : 0));
		status = MERRDEST;

	} else {
		DEBUGLOG2("S_SEND_FAULT to printer_fault %s\n", printerOrForm);
		printer_fault(pps, pps->request, alert_text, 0);
		status = MOK;
	}

	mputm (md, R_SEND_FAULT, status);
}

/*
 * s_clear_fault()
 */
void
s_clear_fault(char *m, MESG *md)
{
	ENTRY ("s_clear_fault")

	long	key;
	char	*printerOrForm, *alert_text;
	ushort	status;
	register PSTATUS	*pps;

	getmessage(m, S_CLEAR_FAULT, &printerOrForm, &key, &alert_text);

	DEBUGLOG4("S_CLEAR_FAULT from (%s) key %x txt (%s)\n", printerOrForm,
		key, alert_text);
	if (! (pps = search_ptable(printerOrForm)) || ((key > 0) &&
	    ((!pps->exec) || pps->exec->key != key || !pps->request ))) {
		DEBUGLOG4("S_CLEAR_FAULT error %s %d %d\n", printerOrForm, pps,
			key);
		status = MERRDEST;

	} else {
		DEBUGLOG2("S_CLEAR_FAULT to printer_fault %s\n", printerOrForm);
		clear_printer_fault(pps, alert_text);
		status = MOK;
	}

	mputm (md, R_CLEAR_FAULT, status);
}


/*
 * s_paper_changed()
 */
void
s_paper_changed(char *m, MESG *md)
{
	ENTRY ("s_paper_changed")

	short			trayNum, mode, pagesPrinted;
	char			*printer, *paper;
	ushort			status;
	short			chgd = 0;
	register PSTATUS	*pps;
	register FSTATUS	*pfs,*pfsWas;

	getmessage(m, S_PAPER_CHANGED, &printer, &trayNum, &paper, &mode,
		&pagesPrinted);

	DEBUGLOG5("S_PAPER_CHANGED from (%s) tray %d paper (%s) pp %d\n",
		printer, trayNum, paper, pagesPrinted);

	if ((!(pps = search_ptable(printer))) || (pps->status & PS_REMOTE))
		status = MNODEST;
	else if ((trayNum <=0) || (trayNum > pps->numForms))
		status = MNOTRAY;
	else {
		status = MOK;
		if (*paper && (pfsWas = pps->forms[trayNum-1].form) && 
		    (!STREQU(pfsWas->form->paper,paper))) {
			pfs = search_fptable(paper);
			if (pfs) {
				remount_form(pps, pfs, trayNum);
				chgd = 1;
			} else
				status = MNOMEDIA;
		}
		if ( status == MOK ) {
			pps->forms[trayNum].isAvailable = mode;
			if ((chgd || !mode) && (!pagesPrinted) &&
			    LP_KILL_NO_PAPER && pps->exec) {
				DEBUGLOG1("kill and hold request\n");
				if (pps->request)
					pps->request->request->outcome |=
						RS_STOPPED;
				terminate(pps->exec);
				schedule(EV_LATER, 1, EV_INTERF, pps);
			}
		}
	}
	DEBUGLOG2("R_PAPER_CHANGED %d\n", status);
	mputm(md, R_PAPER_CHANGED, status);
}

