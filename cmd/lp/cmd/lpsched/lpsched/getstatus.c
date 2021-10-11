/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)getstatus.c	1.24	95/07/13 SMI"	/* SVr4.0 1.9	*/

#include "stdlib.h"
#include "unistd.h"
#include "stdarg.h"

#include "lpsched.h"

int			Redispatch	= 0;

RSTATUS *		Status_List	= 0;

static SUSPENDED	*Suspend_List	= 0;

static void mkreq(SSTATUS *, short, va_list);

#define SHOULD_NOTIFY(PRS) \
	( \
		(PRS)->request->actions & (ACT_MAIL|ACT_WRITE|ACT_NOTIFY)\
	     || (PRS)->request->alert \
	)

/**
 ** mesgdup()
 **/

static char *
mesgdup(char *m)
{
	ENTRY ("mesgdup")

	char *			p;
	unsigned long		size	= msize(m);

	p = Malloc(size);
	memcpy (p, m, size);
	return (p);
}

/**
 ** mesgadd()
 **/

void
mesgadd(SSTATUS *pss, char *mesg)
{
	ENTRY ("mesgadd")

	size_t			len;

	if (pss->tmp_pmlist) {
		len = lenlist(pss->tmp_pmlist);
		pss->tmp_pmlist = (char **)Realloc(
			pss->tmp_pmlist,
			(len + 2) * sizeof(char *)
		);
		pss->tmp_pmlist[len] = mesgdup(mesg);
		pss->tmp_pmlist[len + 1] = 0;
	} else {
		pss->tmp_pmlist = (char **)Calloc(2, sizeof(char *));
		pss->tmp_pmlist[0] = mesgdup(mesg);
		pss->tmp_pmlist[1] = 0;
	}
}

/**
 ** askforstatus()
 **/

void
askforstatus(SSTATUS *pss, MESG *md, short msgType, ...)
{
	ENTRY ("askforstatus")

	WAITING		*w;

	va_list		args;

	
	va_start (args, msgType);

#if	defined(USE_TIMER)
	/*
	 * If wait is -1, the user has been through all of this once
	 * already and should not be kept waiting. This remedies the
	 * situation where the "md" is waiting for 2 or more systems
	 * and the response from one system comes more than
	 * USER_STATUS_EXPIRED seconds after another has reported back
	 * (i.e., while waiting for one system, the other expired again).
	 * Without this check, the <md> could deadlock always waiting
	 * for the status from one more system.
	 */
	if (md->wait == -1) {
		DEBUGLOG1("Already waited for status once, don't wait again\n");
		return;
	}

#endif

	w = (WAITING *)Malloc(sizeof(WAITING));
	w->md = md;
	w->next = pss->waiting;
	pss->waiting = w;
	md->wait++;
	mkreq(pss, msgType, args);
	va_end (ap);
	schedule (EV_SYSTEM, pss);
}

#ifdef DEBUG
char *conv_time(time_t *clock)
{
	struct tm *tm;
	static char buff[80];

	if (*clock == 0)
		sprintf(buff, "<no time>");
	else {
		tm = gmtime(clock);
		cftime(buff, "%T", clock);
	}
	return (buff);
}
#endif

/**
 ** waitforstatus()
 **/

int
waitforstatus(char *m, MESG *md)
{
	ENTRY ("waitforstatus")

	SUSPENDED *		s;

	if (md->wait <= 0) {
		md->wait = 0;
		DEBUGLOG1("No requests to wait for.\n");
		return (-1);
	}

	s = (SUSPENDED *)Malloc(sizeof(SUSPENDED));
	s->message = mesgdup(m);
	s->md = md;

	s->next = Suspend_List;
	Suspend_List = s;

	DEBUGLOG3("Suspend %lu for status, wait %d\n", md, md->wait);

	return (0);
}

/**
 ** load_bsd_stat
 **/

void
load_bsd_stat(SSTATUS *pss, PSTATUS *pps, short msgType, short isMore,
	PSTATUS *alias)
{
	ENTRY ("load_bsd_stat")

	FILE *			fp;

	char			buf[BUFSIZ];
	char			mbuf[MSGMAX];
	char 			req[BUFSIZ];

	char *			file;
	char *			rmesg	= 0;
	char *			dmesg	= 0;
	char *			cp;
	char *			job_id;
	char *			files;
	char *			userName;
	char *			hostName;
	char *			size;

	RSTATUS *		prs;

	time_t			now;

	short			status	= 0;


	short			rank;
	char			isRequestRank = 0;

	req[0] = 0;
	file = pps->alert->msgfile;
	if ((fp = open_lpfile(file, "r", MODE_NOREAD)) == NULL)
		return;
	Unlink (file);

	now = time((time_t *)0);

	switch (msgType) {
		case S_INQUIRE_PRINTER_STATUS:
		case S_INQUIRE_REMOTE_PRINTER:
			isRequestRank = 0;
			break;

		case S_INQUIRE_REQUEST_RANK:
			isRequestRank = 1;
			break;
	}
	
	while (fgets(buf, BUFSIZ, fp)) {
		buf[strlen(buf) - 1] = '\0';
	
		DEBUGLOG2(">>>%s\n", buf);

		switch(*buf) {
		case '%':
			/*
			 * MORE: add code to fetch old status and restore
			 * it
			 */
			break;
		    
		case '-':
			if (strstr(buf + 2, "printing")) {
				DEBUGLOG1("Added to good reason\n");
				status |= PS_BUSY;
			} else if (strstr(buf + 2, "enabled")) {
				DEBUGLOG1("Added to reject reason\n");
				status |= PS_REJECTED;
				addstring (&rmesg, buf + 2);
				addstring (&rmesg, "\n");
			} else {
				DEBUGLOG1("Added to fault reason\n");
				status |= PS_FAULTED;
				load_str (&pps->fault_reason, buf+2);
				addstring (&dmesg, buf + 2);
				addstring (&dmesg, "\n");
				dump_fault_status (pps);
			}
			break;
		    
		default:
			/*
			 * Message format:
			 *
			 *	user:rank:jobid:host:size
			 */
			userName = strtok (buf, ":");
			if (!(cp = strtok((char *)0, ":")))
				break;
			rank = atoi(cp);
			if (!(job_id = strtok((char *)0, ":")))
				break;

			hostName = strtok((char *) 0, ":");

			if (!STREQU(Local_System, hostName) || !(prs =
			    request_by_jobid(pps->printer->name, job_id))) {
				DEBUGLOG3("Missing request %s %s\n",
					pps->printer->name, job_id);
				if (isRequestRank) {
					char *rq, *un;

					size = strtok((char *) 0, ":");
					files = strtok((char *)0, "\n");
					rq = Malloc(strlen(job_id) +
						strlen(pps->printer->name) + 2);
					un = Malloc(strlen(userName) +
						strlen(hostName) + 2);
					sprintf(rq, "%s-%s", pps->printer->name,
						job_id);
					sprintf(un, "%s!%s", hostName,
						 userName);
					DEBUGLOG5(
			"send R_INQUIRE_REQUEST_RANK MOKMORE for %s %s %s %s\n",
						job_id, userName, hostName,
						pps->printer->name);
					putmessage(mbuf,
						R_INQUIRE_REQUEST_RANK,
						/*
						 * MOK or MOKMORE is ok in
						 * this context
						 */
						MOKMORE,
						rq,
						un,
						atol(size),
						now,
						/* rank == 0  it is printing */
						(rank ? 0 : RS_PRINTING),
						pps->printer->name,
						"",
						"",
						rank,
						(files ? files : "" )
					);
					mesgadd(pss, mbuf);
					if (rq)
						Free(rq);
					if (un)
						Free(un);
					
				} else if (status & PS_BUSY && rank == 0) {
					sprintf(req, "%s-%s",
						pps->printer->name, job_id);
					DEBUGLOG2("Active job %s\n", req);
				} else if (status & PS_BUSY && req[0] == 0 &&
					rank == 1) {
					sprintf(req, "%s-%s",
						pps->printer->name, job_id);
					DEBUGLOG2("1st job %s\n", req);
				}
				
				continue;
			}
			DEBUGLOG2("Saving a rank of %d\n", rank);
			prs->status |= (RSS_MARK|RSS_RANK);
			if ((prs->rank = rank) == 0) {
				status |= PS_BUSY;
				strcpy(req, prs->secure->req_id);
			}
			if (isRequestRank) {
				char files[BUFSIZ];

				GetRequestFiles(prs->request, files,
						sizeof(files));
				DEBUGLOG3(
			"send R_INQUIRE_REQUEST_RANK MOKMORE for %s %s\n",
					prs->secure->req_id,
					pps->printer->name);
				putmessage(mbuf,
					R_INQUIRE_REQUEST_RANK,
					/*
					 * MOK or MOKMORE is ok in
					 * this context
					 */
					MOKMORE,
					prs->secure->req_id,
					prs->secure->user,
					prs->secure->size,
					prs->secure->date,
					prs->request->outcome |
						(rank ? 0 : RS_PRINTING),
					pps->printer->name,
					(prs->form? prs->form->form->name : ""),
					NB(prs->pwheel_name),
					prs->rank,
					files
				);
				mesgadd(pss, mbuf);
			}
		}
	}

	/*
	 * Make sure these msg strings aren't so long that they overflow
	 * mbuf in the putmessage below.  This can happen on some systems
	 * when lpNet can't parse the status so all of the status is passed
	 * as a printer fault.  Bugid 1144048.
	 */
	if (dmesg && strlen(dmesg) > (size_t) 800) {
		*(dmesg + 799) = '\n';
		*(dmesg + 800) = '\0';
	}

	if (rmesg && strlen(rmesg) > (size_t) 800) {
		*(rmesg + 799) = '\n';
		*(rmesg + 800) = '\0';
	}

	DEBUGLOG1("Cleaning up old requests\n");
	BEGIN_WALK_BY_PRINTER_LOOP (prs, pps)
		if (!(prs->request->outcome & RS_SENT))
			continue;
		if (prs->status & RSS_MARK) {
			prs->status &= ~RSS_MARK;
			continue;
		}
		DEBUGLOG2("Completed %s\n", prs->secure->req_id);
		prs->request->outcome &= ~RS_ACTIVE;
		prs->request->outcome |= RS_PRINTED;
		if (SHOULD_NOTIFY(prs))
			prs->request->outcome |= RS_NOTIFY;
		notify (prs, (char *)0, 0, 0, 0);
		check_request(prs);
	END_WALK_LOOP

	if ( !isRequestRank ) {
		if (alias == NULL) {
			/*
			 * not a request for a specific printer, so show aliases
			 */
			PSTATUS *tpps;

			for (tpps = walk_ptable(1); tpps;
			    tpps = walk_ptable(0)) {
				if (tpps->system != pps->system)
					continue;
				/* skip the printer we got the response for */
				if (STREQU(tpps->printer->name,
				    pps->printer->name))
					continue;
				/* make sure this is an alias for the remote */
				if (! STREQU(tpps->remote_name,
				    pps->remote_name))
					continue;

				DEBUGLOG3("alias %s: %s\n",
					tpps->system->system->name,
					tpps->printer->name);
				DEBUGLOG3(
				    "send R_INQUIRE_REMOTE_PRINTER %s for %s\n",
					"MOKMORE", tpps->printer->name);
				putmessage(mbuf, R_INQUIRE_REMOTE_PRINTER,
					MOKMORE, tpps->printer->name, "", "",
					dmesg, rmesg, status, req, (long)now,
					(long)now);
				mesgadd (pss, mbuf);
			}
		}

		DEBUGLOG3("send R_INQUIRE_REMOTE_PRINTER %s for %s\n",
			(isMore ? "MOKMORE" : "MOK"), pps->printer->name);
		putmessage(mbuf, R_INQUIRE_REMOTE_PRINTER,
			(isMore ? MOKMORE : MOK), pps->printer->name, "", "",
			dmesg, rmesg, status, req, (long)now, (long)now);
		mesgadd (pss, mbuf);
	}

	if (dmesg)
		Free(dmesg);
	if (rmesg)
		Free(rmesg);

	close_lpfile(fp);
}

/**
 ** update_req()
 **/

void
update_req(char *req_id, long rank)
{
	ENTRY ("update_req")

	RSTATUS		*prs;

	if (!(prs = request_by_id(req_id)))
		return;
	
	prs->status |= RSS_RANK;
	prs->rank = rank;

	return;
}

/**
 ** md_wakeup()
 **/

void
md_wakeup(SSTATUS *pss)
{
	ENTRY ("md_wakeup")

	WAITING *		w;

	int			wakeup	= 0;
	int			curWait = 0;

	SUSPENDED *		susp;
	SUSPENDED *		newlist	= 0;


	while (pss->waiting) {
		w = pss->waiting;
		pss->waiting = w->next;
		if (--(w->md->wait) <= 0)
			wakeup = 1;
		curWait = w->md->wait;
		Free (w);
	}

	if (wakeup) {
		DEBUGLOG2("md_wakeup: waking up for %s\n", pss->system->name);
		while (Suspend_List) {
			susp = Suspend_List;
			Suspend_List = susp->next;
			if (susp->md->wait <= 0) {
				susp->md->wait = -1;
				Redispatch = 1;
				dispatch(mtype(susp->message), susp->message,
					susp->md);
				Redispatch = 0;
				Free (susp->message);
				Free (susp);
			} else {
				susp->next = newlist;
				newlist = susp;
			}
		}
		Suspend_List = newlist;
	}
	else
		DEBUGLOG2("md_wakeup: still waiting for %d\n", curWait);
}
/**
 ** mkreqInd()
 **/

void
mkreqInd(SSTATUS *pss, short msgType, ...)
{
	ENTRY ("mkreqInd")

	va_list        args;
	va_start(args, msgType);
	mkreq(pss, msgType, args);
	va_end(args);
}

/**
 ** mkreq()
 **/

static void
mkreq(SSTATUS *pss, short msgType, va_list args)
{
	ENTRY ("mkreq")

	char			idno[STRSIZE(BIGGEST_REQID_S) + 1];

	RSTATUS *		prs;
	RSTATUS *		r;
	static unsigned long	fake_req_id = 0;

	/*
	 * Create a fake request with enough information to
	 * fool the various request handling routines.
	 */
	prs = allocr();
	sprintf (idno, "%ld", ++fake_req_id);
	prs->secure->req_id = makestr("(fake)", "-", idno, (char *)0);
	prs->system = pss;
	prs->msgType = msgType;
	switch (msgType) {
	default:
		prs->printer =  va_arg(args, PSTATUS *);
		break;

	case S_INQUIRE_REQUEST_RANK:
	case S_INQUIRE_PRINTER_STATUS: 
	case S_INQUIRE_REMOTE_PRINTER:
		prs->printer =  va_arg(args, PSTATUS *);

		DEBUGLOG3("mkreq: %s for %s\n", dispatchName(prs->msgType),
			(pss->system) ? (pss->system->name) ? pss->system->name
			    : "" : "");

		if (pss->laststat)
			if (time((time_t *)0) - pss->laststat >=
			    WHEN_STATUS * CLOCK_TICK) {
				DEBUGLOG1("Request already pending\n");
				freerstatus(prs);
				md_wakeup(pss);
				return;
			}
		break;

	case S_MOUNT_TRAY: 
	case S_UNMOUNT_TRAY:
		prs->printer =  va_arg(args, PSTATUS *);
		prs->formName  = va_arg(args, char *);
		prs->trayNum= va_arg(args, int);
		DEBUGLOG5("mkreq: %s %d %d %d\n", dispatchName(msgType),
			prs->printer, prs->form,prs->trayNum);
		break;
	}

	if (Status_List) {
		for (r = Status_List; r->next; r = r->next)
			DEBUGLOG3("mkreq: %s pending for %s\n",
				dispatchName(r->msgType),
				((r->system) && (r->system->system) &&
				 (r->system->system->name)) ?
				  r->system->system->name : "");
		DEBUGLOG3("mkreq: %s pending for %s\n",
			dispatchName(r->msgType), ((r->system) &&
			     (r->system->system) && (r->system->system->name)) ?
			      r->system->system->name : "");

		r->next = prs;
		prs->prev = r;
		DEBUGLOG3("mkreq: add %s for %s to list\n",
			dispatchName(prs->msgType),
			((pss->system) && (pss->system->name)) ?
			 pss->system->name : "");
	} else {
		Status_List = prs;
		DEBUGLOG3("mkreq: first on list %s for %s\n",
			dispatchName(prs->msgType),
			(pss->system) ? (pss->system->name) ? pss->system->name
			    : "" : "");
	}
}

/**
 ** rmreq()
 **/

void
rmreq(RSTATUS *prs)
{
	ENTRY ("rmreq")

	/*
	 * This is "Ok", since freerstatus() calls remover() to remove the
	 * request from the chain, and remover() now checks for the head of
	 * for the Request_List and Status_List (which we are interested in
	 * here)
	 */
	DEBUGLOG2("rmreq: %d\n", prs);
	freerstatus (prs);
}
