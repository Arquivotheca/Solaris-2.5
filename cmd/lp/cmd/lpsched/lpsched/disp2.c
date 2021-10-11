/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)disp2.c	1.25	95/07/13 SMI"	/* SVr4.0 1.9.1.5	*/

#include	"dispatch.h"

extern long	time();
extern char	*LP_ALL_NEW;

char *showForms(PSTATUS *);

/*
 * untidbit_all() - CALL untidbit() FOR A LIST OF TYPES
 */

static void
untidbit_all (char **printer_types)
{
	ENTRY ("untidbit_all")

	char **			pl;

	for (pl = printer_types; *pl; pl++)
		untidbit (*pl);
	return;
}

/*
 * s_load_printer()
 */

void
s_load_printer(char *m, MESG *md)
{
	ENTRY ("s_load_printer")

	char			*printer;

	ushort			status;

	PRINTER			op;

	register PRINTER	*pp;

	register PSTATUS	*pps;

	char **paperDenied;


	(void) getmessage(m, S_LOAD_PRINTER, &printer);

	if (!*printer)
		status = MNODEST;

	/*
	 * Strange or missing printer?
	 */
	else if (!(pp = Getprinter(printer))) {
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
	 * Printer we know about already?
	 */
	} else if ((pps = search_ptable(printer))) {

		op = *(pps->printer);
		*(pps->printer) = *pp;

		/*
		 * Ensure that an old Terminfo type that's no longer
		 * needed gets freed, and that an existing type gets
		 * reloaded (in case it has been changed).
		 */
		untidbit_all (op.printer_types);
		untidbit_all (pp->printer_types);

		/*
		 * Does an alert get affected?
		 *	- Different command?
		 *	- Different wait interval?
		 */
		if (pps->alert->active)

#define	OALERT	op.fault_alert
#define	NALERT	pp->fault_alert
			if (!SAME(NALERT.shcmd, OALERT.shcmd) ||
			    NALERT.W != OALERT.W) {
				/*
				 * We can't use "cancel_alert()" here
				 * because it will remove the message.
				 * We'll do half of the cancel, then
				 * check if we need to run the new alert,
				 * and remove the message if not.
				 */
				pps->alert->active = 0;
				terminate (pps->alert->exec);
				if (NALERT.shcmd)
					alert(A_PRINTER, pps, (RSTATUS *)0,
						(char *)0);
				else
					Unlink (pps->alert->msgfile);
			}
#undef	OALERT
#undef	NALERT

		freeprinter (&op);

		unload_list (&pps->users_allowed);
		unload_list (&pps->users_denied);
		unload_list (&pps->forms_allowed);
		unload_list (&pps->forms_denied);
		load_userprinter_access(pp->name, &pps->users_allowed,
			&pps->users_denied);
		load_formprinter_access(pp->name, &pps->forms_allowed,
			&pps->forms_denied);

		unload_list (&pps->paper_allowed);
		load_paperprinter_access(pp->name, &pps->paper_allowed,
			&paperDenied);
		freelist(paperDenied);

		load_sdn (&pps->cpi, pp->cpi);
		load_sdn (&pps->lpi, pp->lpi);
		load_sdn (&pps->plen, pp->plen);
		load_sdn (&pps->pwid, pp->pwid);

		pps->last_dial_rc = 0;
		pps->nretry = 0;

		init_remote_printer (pps, pp);

		/*
		 * Evaluate all requests queued for this printer,
		 * to make sure they are still eligible. They will
		 * get moved to another printer, get (re)filtered,
		 * or get canceled.
		 */
		(void) queue_repel(pps, 0, (qchk_fnc_type)0);

		status = MOK;

	/*
	 * Room for new printer?
	 */
	} else if ((pps = search_ptable((char *)0))) {
		pps->status = PS_DISABLED | PS_REJECTED;
		pps->request = 0;
		pps->alert->active = 0;

		pps->forms = 0;
		pps->numForms = 0;
		pps->pwheel_name = 0;
		pps->pwheel = 0;

		load_str (&pps->dis_reason, CUZ_NEW_PRINTER);
		load_str (&pps->rej_reason, CUZ_NEW_DEST);
		load_str (&pps->fault_reason, CUZ_PRINTING_OK);
		time (&pps->dis_date);
		time (&pps->rej_date);

		*(pps->printer) = *pp;

		untidbit_all (pp->printer_types);

		unload_list (&pps->users_allowed);
		unload_list (&pps->users_denied);
		unload_list (&pps->forms_allowed);
		unload_list (&pps->forms_denied);
		load_userprinter_access(pp->name, &pps->users_allowed,
			&pps->users_denied);
		load_formprinter_access(pp->name, &pps->forms_allowed,
			&pps->forms_denied);

		unload_list (&pps->paper_allowed);
		load_paperprinter_access(pp->name, &pps->paper_allowed,
			&paperDenied);
		freelist(paperDenied);

		load_sdn (&pps->cpi, pp->cpi);
		load_sdn (&pps->lpi, pp->lpi);
		load_sdn (&pps->plen, pp->plen);
		load_sdn (&pps->pwid, pp->pwid);

		pps->last_dial_rc = 0;
		pps->nretry = 0;

		init_remote_printer (pps, pp);

		dump_pstatus ();

		status = MOK;

	} else {
		freeprinter (pp);
		status = MNOSPACE;
	}


	mputm (md, R_LOAD_PRINTER, status);
	return;
}

/*
 * s_unload_printer()
 */

static void
_unload_printer(PSTATUS *pps)
{
	ENTRY ("_unload_printer")

	register CSTATUS	*pcs;

	if (pps->alert->active)
		cancel_alert (A_PRINTER, pps);

	/*
	 * Remove this printer from the classes it may be in.
	 * This is likely to be redundant, i.e. upon deleting
	 * a printer the caller is SUPPOSED TO check all the
	 * classes; any that contain the printer will be changed
	 * and we should receive a S_LOAD_CLASS message for each
	 * to reload the class.
	 *
	 * HOWEVER, this leaves a (small) window where someone
	 * can sneak a request in destined for the CLASS. If
	 * we have deleted the printer but still have it in the
	 * class, we may have trouble!
	 */
	for (pcs = walk_ctable(1); pcs; pcs = walk_ctable(0))
		(void) dellist(&(pcs->class->members), pps->printer->name);

	untidbit_all (pps->printer->printer_types);
	freeprinter (pps->printer);
	pps->printer->name = 0;		/* freeprinter() doesn't */
	if (pps->forms) {
		Free(pps->forms);
		}
	pps->forms = NULL;
	pps->numForms = 0;

	return;
}

void
s_unload_printer(char *m, MESG *md)
{
	ENTRY ("s_unload_printer")

	char			*printer;

	ushort			status;

	register PSTATUS	*pps;


	(void) getmessage(m, S_UNLOAD_PRINTER, &printer);

	/*
	 * Unload ALL printers?
	 */
	if (!*printer || STREQU(printer, NAME_ALL))

		/*
		 * If we have ANY requests queued, we can't do it.
		 */
		if (!Request_List)
			status = MBUSY;

		else {
			for (pps = walk_ptable(1); pps; pps = walk_ptable(0))
				_unload_printer (pps);
			status = MOK;
		}

	/*
	 * Have we seen this printer before?
	 */
	else if (!(pps = search_ptable(printer)))
		status = MNODEST;

	/*
	 * Can we can move all requests queued for this printer
	 * to another printer?
	 */
	else {
		/*
		 * Note: This routine WILL MOVE requests to another
		 * printer. It will not stop until it has gone through
		 * the entire list of requests, so all requests that
		 * can be moved will be moved. If any couldn't move,
		 * however, we don't unload the printer.
		 */
		if (queue_repel(pps, 1, (qchk_fnc_type)0))
			status = MOK;
		else
			status = MBUSY;

		if (status == MOK)
			_unload_printer (pps);

	}

	if (status == MOK)
		dump_pstatus ();

	mputm (md, R_UNLOAD_PRINTER, status);
	return;
}

/*
 * combineReasons()
 */

static char *
combineReasons(PSTATUS *pps, char *freeReason)
{
	ENTRY ("combineReasons")

	char	*reason = NULL;

	if (pps->status & PS_FAULTED) {
		if ((pps->status & (PS_DISABLED | PS_LATER)) &&
		    (!STREQU(pps->dis_reason, CUZ_STOPPED)) &&
		    (addstring(&reason, "Fault reason: ") == 0) &&
		    (addstring(&reason, pps->fault_reason) == 0) &&
		    (addstring(&reason, "\n\tDisable reason: ") == 0) &&
		    (addstring(&reason, pps->dis_reason) == 0))
			*freeReason = 1;

		else {
			if (reason)
				/* memory allocation failed part way through */
				Free(reason);

			reason = pps->fault_reason;
			*freeReason = 0;
		}
	} else {
		reason = pps->dis_reason;
		*freeReason = 0;
	}
	DEBUGLOG4("reason free %d %x (%s)\n", *freeReason, pps->status,
		(reason ? reason : "NONE"));
	return (reason);
}

static void
local_printer_status(MESG *md, PSTATUS *pps, short status)
{
	ENTRY ("local_printer_status")

	char	*reason = NULL;
	char	freeReason = 0;
	char	*formList = NULL;

	reason = combineReasons(pps, &freeReason);
	formList = showForms(pps);

	DEBUGLOG4("send R_INQUIRE_PRINTER_STATUS %s %d (%s)\n",
		statusName(status), pps->status, pps->printer->name);

	send(md, R_INQUIRE_PRINTER_STATUS, status, pps->printer->name,
		(formList ? formList : ""),
		(pps->pwheel_name ? pps->pwheel_name : ""),
		reason, pps->rej_reason, pps->status,
		(pps->request ? pps->request->secure->req_id : ""),
		pps->dis_date, pps->rej_date);

	if (formList)
		Free(formList);

	if (freeReason)
		Free(reason);
}

/*
 * s_inquire_printer_status()
 */

void
s_inquire_printer_status(char *m, MESG *md)
{
	ENTRY ("s_inquire_printer_status")

	char			*printer;
	register PSTATUS	*pps, *ppsnext;

	(void) getmessage(m, S_INQUIRE_PRINTER_STATUS, &printer);
	DEBUGLOG2("s_inquire_printer (%s)\n", printer);

	/* Inquire about ALL printers? */
	if (!*printer || STREQU(printer, NAME_ALL)) {
		if ((pps = walk_ptable(1)))
			for (; (ppsnext = walk_ptable(0)); pps = ppsnext)
				local_printer_status(md, pps, MOKMORE);

	/* Inquire about a specific printer? */
	} else
		pps = search_ptable(printer);

	if (pps)
		local_printer_status(md, pps, MOK);
	else {
		DEBUGLOG1("send R_INQUIRE_PRINTER_STATUS MNODEST\n");
		mputm(md, R_INQUIRE_PRINTER_STATUS, MNODEST, "", "", "", "",
			"", 0, "", 0L, 0L);
	}
}

static void
status_failed(MESG *md, SSTATUS *pss, int more, PSTATUS *specific_pps)
{
	ENTRY ("status_failed")

	int cnt = 0;
	PSTATUS *pps;

	if (! more)
		for (pps = walk_ptable(1); pps; pps = walk_ptable(0)) {
			if (pps->system != pss)
				continue;
			if (specific_pps && pps != specific_pps)
				continue;
			cnt++;
		}

	for (pps = walk_ptable(1); pps; pps = walk_ptable(0)) {
		DEBUGLOG5("status fail %x %x %s %s\n", pps->system, pss,
			pss->system->name,
			(pps->system ?
			    (pps->system->system ?
				pps->system->system->name : "") : ""));
		if (pps->system != pss)
			continue;
		if (specific_pps && pps != specific_pps)
			continue;

		cnt--;
		DEBUGLOG5(
		"send R_INQUIRE_PRINTER_STATUS %s (%d) %s %s\n",
			(more ? "MOKMORE" : (cnt ? "MOKMORE" : "MOK")), cnt,
			pss->system->name, pps->printer->name);
		mputm(md, R_INQUIRE_PRINTER_STATUS,
			(more ? MOKMORE : (cnt ? MOKMORE : MOK)),
			pps->printer->name, "", "",
			gettext("system not responding"),
			"", PS_FAULTED, "", (pps ? pps->dis_date : 0L), 0L);
	}
}

/*
 * Handle undefined remote printers.  For each printer locally
 * defined for the remote system, check if the remote system does
 * not define it.  If so, fault it.
 * Return 1 if we have done all of the response processing in this function
 * and return_remote_status should just return.  Return 0 if
 * return_remote_status should continue and process the normal responses.
 */

static int
undefined_remote(MESG *md, SSTATUS *pss, int isMore, PSTATUS *pps)
{
	ENTRY ("undefined_remote")

	char	**ppm;
	char	*s1, *s2, *s3, *s4, *s5, *s6;
	short	h1, h2;
	long	l1, l2;
	int	undef_cnt, found;
	PSTATUS	*tpps;

	if (pps)
		/*
		 * Checking explicit printer.  return_remote_status will
		 * handle this case.  If the printer is undefined on the
		 * remote, the remote system will tell us that.
		 */
		return (0);

	/*
	 * Checking the status of all printers.
	 * Start by counting the # of undefined printers (for MOK handling)
	 */

	undef_cnt = 0;
	for (tpps = walk_ptable(1); tpps; tpps = walk_ptable(0)) {

		if (tpps->system != pss)
			continue;

		found = 0;
		for (ppm = pss->pmlist; *ppm; ppm++) {
			if (mtype(*ppm) != R_INQUIRE_PRINTER_STATUS)
				continue;
			(void) getmessage(*ppm, R_INQUIRE_PRINTER_STATUS, &h1,
				&s1, &s2, &s3, &s4, &s5, &h2, &s6, &l1, &l2);
			if (strcmp(tpps->printer->name, s1) == 0) {
				found = 1;
				break;
			}
		}
		if (! found)
			undef_cnt++;
	}

	/*
	 * If there are no undefined printers, we're done.
	 */
	if (! undef_cnt)
		return (0);

	/*
	 * Now add the # of real printer responses to undef_cnt so
	 * we do MOK processing correctly. Incr. cntr for each real printer.
	 */
	for (ppm = pss->pmlist; *ppm; ppm++) {
		if (mtype(*ppm) != R_INQUIRE_PRINTER_STATUS)
			continue;
		(void) getmessage(*ppm, R_INQUIRE_PRINTER_STATUS, &h1,
			&s1, &s2, &s3, &s4, &s5, &h2, &s6, &l1, &l2);
		if (*s1)
			undef_cnt++;
	}

	/*
	 * Now send a msg for each undefined remote printer.
	 */
	for (tpps = walk_ptable(1); tpps; tpps = walk_ptable(0)) {
		int found;

		if (tpps->system != pss)
			continue;

		found = 0;
		/*
		 * see if the local printer is in a resp msg
		 */
		for (ppm = pss->pmlist; *ppm; ppm++) {
			if (mtype(*ppm) != R_INQUIRE_PRINTER_STATUS)
				continue;

			(void) getmessage(*ppm, R_INQUIRE_PRINTER_STATUS, &h1,
				&s1, &s2, &s3, &s4, &s5, &h2, &s6, &l1, &l2);

			if (strcmp(tpps->printer->name, s1) == 0) {
				found = 1;
				break;
			}
		}
		if (! found) {
			char	*s4 = "unknown printer";
			short	h2 = PS_FAULTED;

			undef_cnt--;
			DEBUGLOG3("R_INQUIRE_PRINTER_STATUS %s %s undefined\n",
				((isMore || undef_cnt) ? "MOKMORE" : "MOK"),
				tpps->printer->name);

			/*
			 * if locally disabled, override the remote status.
			 * show the local disable reason
			 */
			if (tpps->status & PS_DISABLED) {
				h2 = tpps->status;
				s4 = tpps->dis_reason;
			}

			mputm(md, R_INQUIRE_PRINTER_STATUS,
				((isMore || undef_cnt) ? MOKMORE : MOK),
				tpps->printer->name, "", "", s4, "", h2, "",
				tpps->dis_date, tpps->rej_date);
		}
	}

	return (0);
}

/*
 * return_remote_status()
 */

static void
return_remote_status(MESG *md, SSTATUS *pss, int isMore, PSTATUS *pps)
{
	ENTRY ("return_remote_status")

	char	**ppm;
	char	*s1, *s2, *s3, *s4, *s5, *s6;
	short	h1, h2;
	long	l1, l2;
	int	ii, cnt = 0;
	PSTATUS *tpps;

	/*
	 * If
	 *	a) we timed out, or
	 *	b) there is no msg from remote system, but laststat is 0,
	 *	   then there were two simlutaneous (within 10 secs) status
	 *	   queries to the remote system; the first timed out and
	 *	   cleared laststat.  The 2nd must now be processed.
	 */
	if (pss->laststat || ! pss->pmlist) {
		DEBUGLOG3("timeout %s %s\n", pss->system->name,
			conv_time(&(pss->laststat)));
		status_failed(md, pss, isMore, pps);
		return;
	}

	if (undefined_remote(md, pss, isMore, pps))
		return;

	/*
	 * Now process the responses for the remote printers.
	 * This is the normal path (remote printers exist for the local ones)
	 */

	/*
	 * start by counting the number of real responses to send back for MOK.
	 */
	for (ppm = pss->pmlist; *ppm; ppm++) {
		if (mtype(*ppm) != R_INQUIRE_PRINTER_STATUS)
			continue;
		(void) getmessage(*ppm, R_INQUIRE_PRINTER_STATUS, &h1,
			&s1, &s2, &s3, &s4, &s5, &h2, &s6, &l1, &l2);
		if (*s1)
			cnt++;
	}

	for (ppm = pss->pmlist; *ppm; ppm++) {
		ii = mtype(*ppm);
		if (ii != R_INQUIRE_PRINTER_STATUS) {
			DEBUGLOG4("bad message type %s (%d) for %s\n",
				dispatchName(ii), ii, pss->system->name);
			status_failed(md, pss, isMore, pps);
			continue;
		}

		(void) getmessage(*ppm, R_INQUIRE_PRINTER_STATUS, &h1,
			&s1, &s2, &s3, &s4, &s5, &h2, &s6, &l1, &l2);

		/*
		 * treat a null printer name as a special case.  See
		 * R_INQUIRE_PRINTER_STATUS in r_send_job.
		 */
		if (*s1 == 0)
			continue;

		cnt--;

		tpps = pps;
		if (!tpps)
			for (tpps = walk_ptable(1); tpps;
			    tpps = walk_ptable(0)) {
				if (tpps->system != pss)
					continue;
				if (strcmp(tpps->printer->name, s1) == 0)
					break;
			}

		DEBUGLOG5(
		"forward R_INQUIRE_PRINTER_STATUS %s (%s) status %d from %s\n",
			((isMore || cnt) ? "MOKMORE" : "MOK"), s1, h2,
			pss->system->name);

		/*
		 * if locally disabled, override the remote status.
		 * show the local disable reason
		 */
		if (tpps->status & PS_DISABLED) {
			h2 = tpps->status;
			s4 = tpps->dis_reason;
		}
		l1 = tpps->dis_date;
		l2 = tpps->rej_date;

		mputm(md, R_INQUIRE_PRINTER_STATUS,
			((isMore || cnt) ? MOKMORE : MOK),
			s1, s2, s3, s4, s5, h2, s6, l1, l2);
	}
}

static void
inquire_all_printers(char *m, MESG *md)
{
	ENTRY ("inquire_all_printers")

	int	i, cnt;
	SSTATUS	*pss;
	PSTATUS *pps;

	/*
	 * First pass in this function.  Send requests to remote systems
	 * and return status right away for local printers.
	 */
	if (Redispatch == 0) {
		char	**sstlist = NULL;
		PSTATUS	*pps, *ppsnext;

		for (pps = walk_ptable(1); pps; pps = ppsnext) {
			ppsnext = walk_ptable(0);
			if ((pps->status & PS_REMOTE) && pps->system)
				if (pps->status &  PS_DISABLED) 
					local_printer_status(md, pps,
					 (ppsnext || sstlist ? MOKMORE : MOK));
				else
					(void) addlist(&sstlist,
						pps->system->system->name);
			else
				local_printer_status(md, pps,
					(ppsnext || sstlist ? MOKMORE : MOK));
		}

		if (lenlist(sstlist) > 0) {
			int index;

			for (index = 0; sstlist[index]; index++)
				askforstatus(search_stable(sstlist[index]), md,
					(LP_ALL_NEW ?
					    S_INQUIRE_REMOTE_PRINTER :
					    S_INQUIRE_PRINTER_STATUS),
					NULL);

			freelist(sstlist);

			/* schedule a wakeup in case we get no response */
			schedule(EV_LATER, WHEN_STATUS, EV_STATUS);

			if (waitforstatus(m, md) == 0)
				return;
		} else if (walk_ptable(1))
			/* catch the case of no printers defined */
			return;

		/*
		 * return if we are waiting for responses from remote
		 * systems or if we have sent back status for all (local)
		 * printers.
		 */
	}

	/*
	 * Respond back to caller (might be second pass in this function).
	 * Get here when Redispatch is set or when there are no printers.
	 */

	/*
	 * Determine how many real systems we have so we can set MOK/MOKMORE
	 */
	cnt = 0;
	for (i = 0; (pss = SStatus[i]) != NULL; i++) {
		if (strcmp(pss->system->name, "+") == 0)
			continue;

		/* Does this system have at least one printer? */
		for (pps = walk_ptable(1); pps; pps = walk_ptable(0))
			if (pps->system == pss) {
				cnt++;
				break;
			}
	}

	if (cnt) {
		for (i = 0; (pss = SStatus[i]) != NULL; i++) {
			if (strcmp(pss->system->name, "+") == 0)
				continue;

			/* Does this system have at least one printer? */
			for (pps = walk_ptable(1); pps; pps = walk_ptable(0))
				if (pps->system == pss)
					break;

			if (pps) {
				DEBUGLOG4("system %s mesg %s cnt %d\n",
					pss->system->name,
					(pss->pmlist ?
					dispatchName(mtype(*(pss->pmlist))) :
					    "Null"), cnt);

				cnt--;
				return_remote_status(md, pss, cnt, NULL);
			}
		}
	} else {
		DEBUGLOG1(
		"send R_INQUIRE_PRINTER_STATUS MNOINFO (nothing in SStatus)\n");
		mputm(md, R_INQUIRE_PRINTER_STATUS, MNOINFO, "", "", "", "",
			"", 0, "", 0L, 0L);
	}
}

static void
inquire_specific_printer(char *m, MESG *md, PSTATUS *pps)
{
	ENTRY ("inquire_specific_printer")

	if ((pps->status & PS_REMOTE) && pps->system) {
		if (pps->status &  PS_DISABLED) {
			local_printer_status(md, pps, MOK);
			return;
		} else if (Redispatch  == 0) {
			askforstatus(pps->system, md,
				(LP_ALL_NEW ?
				    S_INQUIRE_REMOTE_PRINTER :
				    S_INQUIRE_PRINTER_STATUS), pps);

			/* schedule a wakeup in case we get no response */
			schedule(EV_LATER, WHEN_STATUS, EV_STATUS);

			if (waitforstatus(m, md) == 0)
				return;
		}

		/* get here in second pass when processing response */
		return_remote_status(md, pps->system, 0, pps);
	} else
		local_printer_status(md, pps, MOK);
}

/*
 * s_inquire_remote_printer()
 */

void
s_inquire_remote_printer(char *m, MESG *md)
{
	ENTRY ("s_inquire_remote_printer")

	char			*printer;

	(void) getmessage(m, S_INQUIRE_REMOTE_PRINTER, &printer);
	DEBUGLOG2("s_inquire_remote_printer (%s)\n", printer);

	/*
	 * Inquire about ALL printers?
	 */
	if (! *printer || STREQU(printer, NAME_ALL))
		inquire_all_printers(m, md);

	else {
		/* Inquire about a specific printer? */

		PSTATUS	*pps;

		pps = search_ptable(printer);

		if (pps)
			inquire_specific_printer(m, md, pps);
		else {
			DEBUGLOG1("send R_INQUIRE_PRINTER_STATUS MNODEST\n");
			mputm(md, R_INQUIRE_PRINTER_STATUS, MNODEST, "",
				"", "", "", "", 0, "", 0L, 0L);
		}
	}
}


/*
 * s_load_class()
 */

void
s_load_class(char *m, MESG *md)
{
	ENTRY ("s_load_class")

	char			*class;

	ushort			status;

	register CLASS		*pc;

	register CSTATUS	*pcs;


	(void) getmessage(m, S_LOAD_CLASS, &class);

	if (!*class)
		status = MNODEST;

	/*
	 * Strange or missing class?
	 */
	else if (!(pc = Getclass(class))) {
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
	 * Class we already know about?
	 */
	} else if ((pcs = search_ctable(class))) {
		register RSTATUS	*prs;

		freeclass (pcs->class);
		*(pcs->class) = *pc;

		/*
		 * Here we go through the list of requests
		 * to see who gets affected.
		 */
		BEGIN_WALK_BY_DEST_LOOP (prs, class)
			/*
			 * If not still eligible for this class...
			 */
			switch (validate_request(prs, (char **)0, 1)) {
			case MOK:
			case MERRDEST:	/* rejecting (shouldn't happen) */
				break;
			case MDENYDEST:
			case MNOMOUNT:
			case MNOMEDIA:
			case MNOFILTER:
			default:
				/*
				 * ...then too bad!
				 */
				cancel (prs, 1);
				break;
			}
		END_WALK_LOOP
		status = MOK;

	/*
	 * Room for new class?
	 */
	} else if ((pcs = search_ctable((char *)0))) {
		pcs->status = CS_REJECTED;

		load_str (&pcs->rej_reason, CUZ_NEW_DEST);
		time (&pcs->rej_date);

		*(pcs->class) = *pc;

		dump_cstatus ();

		status = MOK;

	} else {
		freeclass (pc);
		status = MNOSPACE;
	}


	mputm (md, R_LOAD_CLASS, status);
	return;
}

/*
 * s_unload_class()
 */

static void
_unload_class(CSTATUS *pcs)
{
	ENTRY ("_unload_class")

	freeclass (pcs->class);
	pcs->class->name = 0;	/* freeclass() doesn't */

	return;
}

void
s_unload_class(char *m, MESG *md)
{
	ENTRY ("s_unload_class")

	char			*class;

	ushort			status;

	RSTATUS			*prs;

	register CSTATUS	*pcs;


	(void) getmessage(m, S_UNLOAD_CLASS, &class);

	/*
	 * Unload ALL classes?
	 */
	if (!*class || STREQU(class, NAME_ALL)) {

		/*
		 * If we have a request queued for a member of ANY
		 * class, we can't do it.
		 */
		status = MOK;
		for (pcs = walk_ctable(1); pcs && status == MOK;
		    pcs = walk_ctable(0))
			BEGIN_WALK_BY_DEST_LOOP (prs, pcs->class->name)
				status = MBUSY;
				break;
			END_WALK_LOOP

		if (status == MOK)
			for (pcs = walk_ctable(1); pcs; pcs = walk_ctable(0))
				_unload_class (pcs);

	/*
	 * Have we seen this class before?
	 */
	} else if (!(pcs = search_ctable(class)))
		status = MNODEST;

	/*
	 * Is there even one request queued for this class?
	 * If not, we can safely remove it.
	 */
	else {
		status = MOK;
		BEGIN_WALK_BY_DEST_LOOP (prs, class)
			status = MBUSY;
			break;
		END_WALK_LOOP
		if (status == MOK)
			_unload_class (pcs);
	}

	if (status == MOK)
		dump_cstatus ();

	mputm (md, R_UNLOAD_CLASS, status);
	return;
}

/*
 * s_inquire_class()
 */

void
s_inquire_class(char *m, MESG *md)
{
	ENTRY ("s_inquire_class")

	char			*class;

	register CSTATUS	*pcs,
				*pcsnext;


	(void) getmessage(m, S_INQUIRE_CLASS, &class);

	/*
	 * Inquire about ALL classes?
	 */
	if (!*class || STREQU(class, NAME_ALL)) {
		if ((pcs = walk_ctable(1)))
			for (; (pcsnext = walk_ctable(0)); pcs = pcsnext)
				send(md, R_INQUIRE_CLASS, MOKMORE,
					pcs->class->name, pcs->status,
					pcs->rej_reason, pcs->rej_date);

	/*
	 * Inquire about a single class?
	 */
	} else
		pcs = search_ctable(class);

	if (pcs)
		send(md, R_INQUIRE_CLASS, MOK, pcs->class->name, pcs->status,
			pcs->rej_reason, pcs->rej_date);

	else
		mputm (md, R_INQUIRE_CLASS, MNODEST, "", 0, "", 0L);

	return;
}

/*
 * s_paper_allowed()
 */

void
s_paper_allowed(char *m, MESG *md)
{
	ENTRY ("s_paper_allowed")

	char			*printer;
	char			*paperList = NULL;
	register PSTATUS	*pps, *ppsnext;

	(void) getmessage(m, S_PAPER_ALLOWED, &printer);
	DEBUGLOG2("S_PAPER_ALLOWED (%s)\n", printer);

	/*
	 * Inquire about ALL printers?
	 */
	if (!*printer || STREQU(printer, NAME_ALL)) {
		if ((pps = walk_ptable(1))) {
			for (; (ppsnext = walk_ptable(0)); pps = ppsnext) {
				paperList = sprintlist(pps->paper_allowed);
				DEBUGLOG4(
				"send R_PAPER_ALLOWED MOKMORE %d (%s) (%s)\n",
					pps->status, pps->printer->name,
					(paperList ? paperList : ""));
				send(md, R_PAPER_ALLOWED, MOKMORE,
					pps->printer->name,
					(paperList ? paperList : ""));

				if (paperList)
					Free(paperList);
			}
		}

	/*
	 * Inquire about a specific printer?
	 */
	} else
		pps = search_ptable(printer);

	if (pps) {
		paperList = sprintlist(pps->paper_allowed);
		DEBUGLOG4("send R_PAPER_ALLOWED MOK %d (%s) (%s)\n",
			pps->status, pps->printer->name,
			(paperList ? paperList : ""));
		send(md, R_PAPER_ALLOWED, MOK, pps->printer->name,
			(paperList ? paperList : ""));

		if (paperList)
			Free(paperList);

	} else {
		DEBUGLOG1("send R_PAPER_ALLOWED MNODEST\n");
		mputm(md, R_PAPER_ALLOWED, MNODEST, "", "");
	}
}
