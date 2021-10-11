/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)rexec.c	1.11	94/12/14 SMI"	/* SVr4.0 1.8	*/

#include "limits.h"
#include "stdarg.h"
#include "lpsched.h"

MESG *Net_md;

static void
rex_send_job(SSTATUS *pss, int job_type, char *job_file, char *msgbuf)
{
	ENTRY ("rex_send_job")

	if (!pss->laststat)
		pss->laststat = time((time_t *)0);
	DEBUGLOG4("rex_send_job: sending S_SEND_JOB (%d) to %s at %s\n",
		job_type, pss->system->name, conv_time(&(pss->laststat)));
	mputm(pss->exec->md,
		S_SEND_JOB,
		pss->system->name,
		job_type,
		job_file,
		msize(msgbuf),
		msgbuf
	);
	pss->exec->flags |= EXF_WAITJOB;
}

/**
 ** rexec() - FORWARD EXEC REQUEST TO REMOTE MACHINE
 **/

/*VARARGS2*/
int
rexec(SSTATUS *pss, int type, ...)
{
	ENTRY ("rexec")

	va_list			args;
	PSTATUS *		pps;
	RSTATUS *		prs;
	FSTATUS *		pfs;
	EXEC *			ep;
	char			msgbuf[MSGMAX];
	char *			full_user;
	char *			req_file;

	if (!pss) {
		errno = EINVAL;
		return (-1);
	}

	/*
	 * Extract useful values, sanity check request.
	 */
	va_start (args, type);
	switch (type) {

	case REX_INTERF:
		pps = va_arg(args, PSTATUS *);
		prs = va_arg(args, RSTATUS *);
		if (!(pps->status & PS_REMOTE)) {
			errno = EINVAL;
			return (-1);
		}
		break;

	case REX_FAULT_MESSAGE:
		pps = va_arg(args, PSTATUS *);
		prs = va_arg(args, RSTATUS *);
		break;

	case REX_FORM_MESSAGE:
		pfs = va_arg(args, FSTATUS *);
		prs = va_arg(args, RSTATUS *);
		break;

	case REX_CANCEL:
	case REX_NOTIFY:
	case REX_STATUS:
		prs = va_arg(args, RSTATUS *);
		break;

	default:
		errno = EINVAL;
		return (-1);

	}
	va_end (args);

	DEBUGLOG4("rexec, type %d: trying to send request %s to %s\n",
		type, prs->secure->req_id, pss->system->name);

	/*
	 * If the connection is ``busy'', we can't do anything yet.
	 */
	if (pss->exec->flags & (EXF_WAITJOB | EXF_WAITCHILD)) {
		DEBUGLOG2("rexec, type %d: connection is busy\n", type);
		return (0);	/* only a tiny lie */
	}

	/*
	 * If we don't yet have a connection to the network server
	 * child for this system, get one. The exec structure is
	 * marked to avoid us asking more than once.
	 *
	 *	md		EXF_WAITCHILD	action
	 *	--------	-------------	--------------------
	 *	NOT SET		NOT SET		send S_NEW_CHILD
	 *	NOT SET		SET		awaiting R_NEW_CHILD
	 *	SET		NOT SET		send S_SEND_JOB
	 *	SET		SET		uh oh!
	 */
	ep = pss->exec;
	if (!ep->md && !(ep->flags & EXF_WAITCHILD)) {
		DEBUGLOG2("Sending S_NEW_CHILD to lpNet, for system %s\n",
			pss->system->name);
		mputm (Net_md, S_NEW_CHILD, pss->system->name);
		ep->flags |= EXF_WAITCHILD;
		ep->type = (short)type; /* so as to pick up where we left off */
		return (0);	/* a minor lie indeed */
	}

	/*
	 * We already have a connection, and we know it is not busy,
	 * so we may proceed.
	 */

	/*
	 * Set flags that keep the scheduler informed of progress.
	 * Some of these flags may be little white lies, but they will
	 * keep the scheduler operating correctly.
	 */

	prs->request->outcome |= RS_SENDING;

	switch (type) {

	case REX_INTERF:
		break;

	case REX_CANCEL:
	case REX_NOTIFY:
		prs->status &= ~RSS_SENDREMOTE;
		break;

	case REX_STATUS:
		prs->status |= RSS_RECVSTATUS;
		break;
   
	case REX_FAULT_MESSAGE:
		prs->status &= ~RSS_SEND_FAULT_MESSAGE;
		break;

	case REX_FORM_MESSAGE:
		prs->status &= ~RSS_SEND_FORM_MESSAGE;
		break;
	}

	/*
	 * Attach the request that's going out (for whatever
	 * reason--printing, cancelling, etc.) to the system
	 * so we can easily match a returning R_SEND_JOB message
	 * with the request. Also, mark this request as no longer
	 * needing to be sent.
	 */
	pss->exec->ex.request = prs;
	prs->status &= ~RSS_SENDREMOTE;

	/*
	 * Do S_SEND_JOB.
	 */
	switch (type) {

	case REX_INTERF:
		putmessage (msgbuf, S_PRINT_REQUEST, prs->req_file);
		putjobfiles (prs);
		rex_send_job (pss, 1, prs->req_file, msgbuf);
		break;

	case REX_CANCEL:
		if (strchr(prs->secure->user, BANG_C))
			full_user = Strdup(prs->secure->user);
		else
			full_user = makestr(
				Local_System,
				BANG_S,
				prs->secure->user,
				(char *)0
			);
		putmessage (
			msgbuf,
			S_CANCEL,
			prs->printer->remote_name,
			full_user,
			prs->secure->req_id
		);
		rex_send_job (pss, 0, "", msgbuf);
		Free (full_user);
		break;

	case REX_NOTIFY:
		req_file = makereqerr(prs);
		putmessage (
			msgbuf,
			S_JOB_COMPLETED,
			prs->request->outcome,
			prs->secure->req_id,
			req_file
		);
		rex_send_job (pss, 0, req_file, msgbuf);
		break;

	case REX_STATUS:
		{
		char * nm;

		nm = (prs->printer ?  prs->printer->remote_name : "");
		switch (pss->system->protocol) {

		case S5_PROTO:

			switch(prs->msgType) {

			case S_INQUIRE_PRINTER_STATUS:
				DEBUGLOG2(
				"rexec: S_INQUIRE_PRINTER_STATUS for (%s)\n",
					nm);
				putmessage(msgbuf, S_INQUIRE_PRINTER_STATUS,
					nm);
				break;

			case S_INQUIRE_REMOTE_PRINTER:
				DEBUGLOG2(
				"rexec: S_INQUIRE_REMOTE_PRINTER for (%s)\n",
					nm);
				putmessage(msgbuf, S_INQUIRE_REMOTE_PRINTER,
					nm);
				break;

			case S_MOUNT_TRAY:
				{
				char *fNm;

				fNm = (prs->formName ?
					prs->formName: NAME_NONE);
				DEBUGLOG4("rexec: S_MOUNT_TRAY %s %s %d\n",
					nm, fNm, prs->trayNum);
				putmessage (msgbuf, S_MOUNT_TRAY, nm, fNm, "",
					prs->trayNum);
				break;
				}

			case S_UNMOUNT_TRAY:
				{
				char *fNm;

				fNm = (prs->formName ?
					prs->formName: NAME_NONE);
				DEBUGLOG4("rexec: S_UNMOUNT_TRAY %s (%s) %d\n",
					nm, fNm, prs->trayNum);
				putmessage(msgbuf, S_UNMOUNT_TRAY, nm, fNm,
					"", prs->trayNum);
				break;
				}

			case S_INQUIRE_REQUEST_RANK:
			default: /* for historical reasons */
				DEBUGLOG1("rexec: S_INQUIRE_REQUEST_RANK\n");
				putmessage(msgbuf, S_INQUIRE_REQUEST_RANK, 0,
					"", nm, "", "", "");
				break;
			}

			rex_send_job(pss, 0, "", msgbuf);
			break;

		case BSD_PROTO:
			if (nm && *nm) {
				DEBUGLOG4("rexec: %s for 1 %s %s\n",
					dispatchName(prs->msgType), nm,
					prs->printer->alert->msgfile);
				(void) putmessage(msgbuf, S_GET_STATUS, nm,
					prs->printer->alert->msgfile,
					prs->msgType, 0);
				rex_send_job(pss, 0, "", msgbuf);
			} else {
				PSTATUS *ppsLast;
				short isMore;
				ppsLast = NULL;

				for (pps = walk_ptable(1); pps;
				    pps = walk_ptable(0)) {
					if (pps->system != pss)
						continue;
					ppsLast = pps;
				}

				for (pps = walk_ptable(1); pps;
				    pps = walk_ptable(0)) {
					if (pps->system != pss)
						continue;
				 
					isMore = (pps != ppsLast);
					DEBUGLOG5("rexec: %s %s for %s %s\n",
						dispatchName(prs->msgType),
						(isMore ? "isMore" : "isLast"),
						pps->remote_name,
						pps->alert->msgfile);
					(void) putmessage(msgbuf, S_GET_STATUS,
						pps->remote_name,
						pps->alert->msgfile,
						prs->msgType, isMore);
					rex_send_job(pss, 0, "", msgbuf);
				}
			}

			break;
		}

		break;
		}

	case REX_FORM_MESSAGE:
		{
		int formMsgLen,allowLength;
		char *formMsg;

		DEBUGLOG2("sending mess S_SEND_FAULT (form) for (%s)\n",
			 pfs->form->name);
         
		formMsg = load_form_msg(pfs);
		formMsgLen = strlen(formMsg);
		allowLength = MSGMAX - 
			(MESG_LEN + strlen(prs->printer->printer->name) +
			5 + 8 + 5 + 20);
		/*
		 * the message is String, long, string a long take 8 bytes
		 * and a string takes its length + 5; The initial stuff takes
		 * MESG_LEN bytes; We add a fudge factor of 20 bytes
 		 */

		if (formMsgLen > allowLength)
			formMsg[allowLength] = 0;

		DEBUGLOG3("sending mess S_SEND_FAULT (form) for (%s) key %x\n",
			pfs->form->name, -(PS_USE_AS_KEY | PS_FORM_FAULT));
		putmessage(msgbuf, S_SEND_FAULT,pfs->form->name ,
			-(PS_USE_AS_KEY | PS_FORM_FAULT), formMsg);

		if (formMsg)
			Free(formMsg);

		rex_send_job(pss, 0, "", msgbuf);
		break;
		}

	case REX_FAULT_MESSAGE:
		DEBUGLOG4("sending mess S_SEND_FAULT for %s type %x (%s)\n",
			prs->printer->printer->name,
			-(pps->status | PS_USE_AS_KEY), pps->fault_reason);
         
		putmessage(msgbuf, S_SEND_FAULT, prs->printer->printer->name,
			-(pps->status | PS_USE_AS_KEY), pps->fault_reason);
		rex_send_job(pss, 0, "", msgbuf);
		break;
	}

	return (0);
}

/**
 ** resend_remote() - RESET SYSTEM AND REQUEST, FOR ANOTHER SEND TO REMOTE
 **/

void
resend_remote(SSTATUS *pss, int when)
{
	ENTRY ("resend_remote")

	RSTATUS *		prs = pss->exec->ex.request;

	pss->exec->flags &= ~(EXF_WAITJOB|EXF_WAITCHILD);
	if (prs && prs->printer && (prs->printer->status & PS_DISABLED)) {
		DEBUGLOG2("resend_remote(): %s disabled, not sending\n",
			prs->printer->remote_name);
		return;
	}
	if (prs && ! (prs->request->outcome & RS_SENT)) {
		prs->request->outcome &= ~RS_SENDING;
		prs->status |= RSS_SENDREMOTE;
	}

	switch (when) {

	case -1:
		break;

	case 0:
		schedule (EV_SYSTEM, pss);
		break;

	default:
		schedule (EV_LATER, when, EV_SYSTEM, pss);
		break;

	}
	return;
}
