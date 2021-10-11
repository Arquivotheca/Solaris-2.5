/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)disp5.c	1.26	94/12/14 SMI"	/* SVr4.0 1.13	*/

#include "dispatch.h"

extern int		Net_fd;

extern MESG *		Net_md;

/**
 ** s_child_done()
 **/

void
s_child_done(char *m, MESG *md)
{
	ENTRY ("s_child_done")

	long			key;

	short			slot;
	short			status;
	short			err;


	getmessage (m, S_CHILD_DONE, &key, &slot, &status, &err);

	DEBUGLOG5("S_CHILD_DONE: key: %d, slot:%d, status:%d, err:%d\n",
			key, slot, status, err);

	if (
		0 <= slot
	     && slot < ET_Size
	     && Exec_Table[slot].key == key
	     && Exec_Table[slot].md == md
	) {

#if	defined(DEBUG)
		if (debug & (DB_EXEC|DB_DONE)) {
			EXEC *			ep = &Exec_Table[slot];

			execlog (
				"OKAY: slot %d pid %d status %d err %d\n",
				slot,
				ep->pid,
				status,
				err
			);
			execlog ("%e", ep);
		}
#endif
		/*
		 * Remove the message descriptor from the listen
		 * table, then forget about it; we don't want to
		 * accidently match this exec-slot to a future,
		 * unrelated child.
		 */
		DROP_MD (Exec_Table[slot].md);
		Exec_Table[slot].md = 0;

		Exec_Table[slot].pid = -99;
		Exec_Table[slot].status = status;
		Exec_Table[slot].errno = err;
		DoneChildren++;

	}

#if	defined(DEBUG)
	else if (debug & (DB_EXEC|DB_DONE)) {
		execlog (
			"FAKE! slot %d pid ??? status %d err %d\n",
			slot,
			status,
			err
		);
	}
#endif

	return;
}

/**
 ** r_new_child()
 **/

void
r_new_child(char *m, MESG *md)
{
	ENTRY ("r_new_child")

	/* arbitration mod for bugid 1094851 */
	static unsigned int	remwincount = 0;

	char *			name;
	char *			originator_name;

	short			status;

	int			were_waiting	= 0;

	struct strrecvfd	recvfd;

	SSTATUS *		pss;

	PSTATUS *		pps;

	MESG *			new_md;
	MESG *			drop_md;
	MESG *			hold_md;


	getmessage(m, R_NEW_CHILD, &name, &originator_name, &status);
	DEBUGLOG3("Received R_NEW_CHILD for system %s requested by %s\n",
		name, originator_name);

	if (!(pss = search_stable(name))) {
		note("%s is an unknown system\n", name);
		recvfd.fd = -1;	/* So as not to clobber someone else */
		ioctl (Net_fd, I_RECVFD, &recvfd);
		close (recvfd.fd);
		return;
	}


	switch (status) {

	case MOK:
		break;

	case MUNKNOWN:
		/*
		 * The network manager doesn't know about this system.
		 * While strictly speaking this ought not occur, it can
		 * because we can't prevent someone from mucking with
		 * the system table. So if this happens we disable the
		 * printer(s) that go to this system.
		 */
		for (pps = walk_ptable(1); pps; pps = walk_ptable(0))
			if (pps->system == pss)
				disable (pps, CUZ_NOREMOTE, DISABLE_STOP);
		return;

	case MNOSTART:
		/*
		 * We get this only in response to our request for a
		 * connection. However, between the time we had asked
		 * and the receipt of this response we may have received
		 * another R_NEW_CHILD originated remotely. So, we try
		 * again later only if we still need to.
		 */
		if (!pss->exec->md) {
			note("Failed contact with %s, retry in %d min.\n",
				name, WHEN_NOSTART / MINUTE);
			resend_remote(pss, WHEN_NOSTART);
		}
		return;

	default:
		note("Strange status (%d) in R_NEW_CHILD for %s.\n",
			status, name);
		return;
	}
				    

	if (ioctl(Net_fd, I_RECVFD, &recvfd) == -1) {
		switch (errno) {
		case EBADMSG:
			note("No file descriptor passed.\n");
			break;

		case ENXIO:
			note("System server terminated early.\n");
			break;

		case EMFILE:
			note("Too many open files!\n");
			break;
		}
		return;
	}

	new_md = mconnect(NULL, recvfd.fd, recvfd.fd);
	if (!new_md)
		mallocfail ();

	new_md->uid = recvfd.uid;
	new_md->gid = recvfd.gid;
	new_md->admin = (new_md->uid == 0 || new_md->uid == Lp_Uid);
	/*
	 * Save this flag, because in the hustle and bustle below
	 * we may lose the original information.
	 */
	were_waiting = (pss->exec->flags & EXF_WAITCHILD);


	/*
	 * Check for a collision with another system trying to contact us:
	 *
	 *	* We had asked for a connection to this system (i.e. had
	 *	  sent a S_NEW_CHILD message), but this isn't the response
	 *	  to that message.
	 *
	 *	* We already have a connection.
	 *
	 * These cases are handled separately below, but the same
	 * arbitration is used: The system with the name that comes
	 * ``first'' in collating order gets to keep the connection
	 * it originated.
	 */
	if (were_waiting) {

		if (STREQU(Local_System, originator_name)) {
			/*
			 * This is the usual case.
			 */
			DEBUGLOG2("Making new connection to %s\n", name);
			hold_md = new_md;
			drop_md = 0;

		} else {
			/*
			 * We have a pending collision, since we
			 * are still waiting for a response to our
			 * connection request (this isn't it). Resolve
			 * the collision now, by either accepting
			 * this response (we'll have to refuse our
			 * real response later) or by refusing this
			 * response.
			 */
			DEBUGLOG3("Potential collision between %s and %s\n",
				Local_System, name);
			if (strcmp(Local_System, name) < 0) {
				DEBUGLOG1("Take no connection.\n");
				hold_md = 0;
				drop_md = new_md;
			} else {
				DEBUGLOG1("Drop this connection.\n");
				hold_md = new_md;
				drop_md = 0;
			}
		}

	} else 
	if (pss->exec->md) {
		MESG *			my_md;
		MESG *			his_md;

		DEBUGLOG3("Collision between %s and %s!\n", Local_System,
			name);

		/*
		 * The message descriptor we got last time
		 * MAY NOT be for the connection we originated.
		 * We have to check the "originator_name" to be sure.
		 */
		if (STREQU(Local_System, originator_name)) {
			my_md = new_md;
			his_md = pss->exec->md;
		} else {
			my_md = pss->exec->md;
			his_md = new_md;
		}

		/* Modified arbitration scheme for bugid 1094851/1103374.
		 * Here we've removed the strcmp() arbitration scheme,
		 * as it will set up a deadlock situation under certain
		 * circumstances, which isn't easily cured. This seems
		 * to happen most frequently when a BSD system is trying 
		 * to connect, along with an SVR4 system.
		 *
		 * Instead, we use the remwincount counter. If the last
		 * winner was local, we then let the remote connection
		 * win. If the last winner was remote, we now let the
		 * local connection win.
		 */
		if ((remwincount++ % 2) == 1) {
			DEBUGLOG1("I won!\n");		/* local winner	  */
			drop_md = his_md;
			hold_md = my_md;
		} else {				/* remote winner */
			DEBUGLOG1("He wins.\n");
			drop_md = my_md;
			hold_md = his_md;
		}
	} else
	{
		DEBUGLOG1("Accepting unsolicited connection.\n");
		hold_md = new_md;
		drop_md = 0;
	}

	if (drop_md) {
		if (drop_md == pss->exec->md) {
			DEBUGLOG2("Dropping fd %d from listen table\n",
				drop_md->readfd);
			DROP_MD (drop_md);

			/*
			 * We are probably waiting on a response
			 * to an S_SEND_JOB from the network child
			 * on the other end of the connection we
			 * just dropped. If so, we have to resend the
			 * job through the new channel...yes, we know
			 * we have a new channel, as the only way to
			 * get here is if we're dropping the exising
			 * channel, and we do that only if we have to
			 * pick between it and the new channel.
			 */
			if (pss->exec->flags & EXF_WAITJOB) {
				resend_remote (pss, -1);
				were_waiting = 1;
			}
		} else {
			DEBUGLOG2("Sending S_CHILD_SYNC M2LATE on %x\n",
				drop_md);
			drop_md->type = MD_CHILD;
			mputm (drop_md, S_CHILD_SYNC, M2LATE);
			mdisconnect (drop_md);
		}
	}
	if (hold_md) {
		if (hold_md != pss->exec->md) {
			DEBUGLOG2("Sending S_CHILD_SYNC MOK on %x\n", hold_md);
			hold_md->type = MD_CHILD;
			mputm (hold_md, S_CHILD_SYNC, MOK);
			pss->exec->md = hold_md;
			if (mlistenadd(pss->exec->md, POLLIN) == -1)
				mallocfail ();
		}
		pss->exec->flags &= ~EXF_WAITCHILD;
	}

	/*
	 * If we still have a connection to the remote system,
	 * and we had been waiting for the connection, (re)send
	 * the job.
	 */
	if (pss->exec->md && were_waiting)
		schedule (EV_SYSTEM, pss);

	return;
}

/**
 ** r_send_job()
 **/

void
r_send_job(char *m, MESG *md)
{
	ENTRY ("r_send_job")

	char			buf[MSGMAX];

	char *			name;
	char *			sent_msg;
	char *			req_id;
	char *			remote_name;
	char *			s1;
	char *			s2;
	char *			s3;
	char *			s4;
	char *			s5;
	char *			s6=""; /* so we don't dump core talking to older						spoolers via s5 protocol */

	short			status;
	short			sent_size;
	short			rank;
	short			h1;
	short			msgType;
	short			isMore;

	long			lstatus;
	long			l1;
	long			l2;

	SSTATUS *		pss;

	RSTATUS *		prs;

	PSTATUS *		pps;
	int 			did_disable = 0;

	getmessage(m, R_SEND_JOB, &name, &status, &sent_size, &sent_msg);
	DEBUGLOG4("rcv R_SEND_JOB (%s) from %s status %s.\n",
		dispatchName(mtype(sent_msg)), name, statusName(status));

	if (!(pss = search_stable(name))) {
		note("%s is an unknown system\n", name);
		return;
	}

	if (! pss->exec || ! pss->exec->ex.request) {
		note("missing request!\n");
		return;
	}

	prs = pss->exec->ex.request;

	if (!(prs->request->outcome & RS_SENDING)) {
		note("Unexpected R_SEND_JOB--no request sent!\n");
		return;
	}
	if (!(pss->exec->flags & EXF_WAITJOB)) {
		note("Unexpected R_SEND_JOB--not waiting!\n");
		return;
	}

	DEBUGLOG2("received at %s\n", conv_time(&(pss->laststat)));

	switch (status) {

	case MOK:
	case MOKMORE:
	case MERRDEST:
		break;

	case MTRANSMITERR:
		did_disable = 0;
		if (prs && prs->printer && prs->printer->printer) {
			if (prs->printer->printer->fault_rec &&
			    strcmp(prs->printer->printer->fault_rec,
				NAME_WAIT) == 0) {
					did_disable++;
					disable(prs->printer,
			"communication failure, must \"enable\" to restart",
						DISABLE_STOP);
			}
		} else {
			if (prs && prs->request) 
				prs->request->outcome |= RS_DONE;
			for (pps = walk_ptable(1); pps; pps = walk_ptable(0))
				if (pps->system == pss && pps->printer &&
				    pps->printer->fault_rec &&
				    strcmp(pps->printer->fault_rec,
					NAME_WAIT) == 0) {
					did_disable++;
					disable(pps,
			"communication failure, must \"enable\" to restart",
						DISABLE_STOP);
				}
		}
		if (did_disable == 0) {
			note("%s: retrying.\n", name);
			resend_remote (pss, 0);
		}
		return;
	default:
		note("Odd status in R_SEND_JOB, %d!\n", status);
		return;

	}

	switch (msgType = mtype(sent_msg)) {

	case R_PRINT_REQUEST:
		/*
		 * Clear the waiting-for-R_SEND_JOB flag now that
		 * we know this message isn't bogus.
		 */
		pss->exec->flags &= ~EXF_WAITJOB;

		getmessage (
			sent_msg,
			R_PRINT_REQUEST,
			&status,
			&req_id,
			&chkprinter_result
		);

		prs->request->outcome &= ~RS_SENDING;
		prs->printer->status &= ~PS_BUSY;

		if (status == MOK) {
			DEBUGLOG1("S_SEND_JOB had succeeded\n");
			prs->request->outcome |= RS_SENT;
			/*
			 * Record the fact that we've sent this job,
			 * to avoid sending it again if we restart.
			 */
			putrequest (prs->req_file, prs->request);
			/*
			 * see if we need to clean the local queue.
			 */
			if (pss->system->protocol == BSD_PROTO)
				schedule(EV_POLLBSDSYSTEMS, pss);
		} else {
			DEBUGLOG2("S_SEND_JOB had failed, status was %d!\n",
				status);
			/*
			 * This is very much like what happens if the
			 * print service configuration changes and causes
			 * a local job to be no longer printable.
			 */
			prs->reason = status;
			cancel (prs, 1);
		}

		break;

	case R_GET_STATUS:
		/*
		 * Were we expecting this?
		 */
		if (!(prs->status & RSS_RECVSTATUS)) {
			DEBUGLOG2("Unexpected GET_STATUS from system: %s\n",
				pss->system->name);
			break;
		}

		/*
		 * Is the protocol correct?
		 */
		if (pss->system->protocol == S5_PROTO) {
			DEBUGLOG2(
			"Protocol mismatch: system %s, got BSD, expected S5\n",
				pss->system->name);
			if (status != MOKMORE)
				rmreq(prs);
			break;
		}

		getmessage(sent_msg, R_GET_STATUS, &status, &remote_name,
			&msgType, &isMore);
		DEBUGLOG5("R_GET_STATUS, st %d from %s msgType %s isMore %d\n",
			status, remote_name, dispatchName(msgType), isMore);

		for (pps = walk_ptable(1); pps; pps = walk_ptable(0)) {
			if (pps->system != pss)
				continue;
			if (prs->printer && prs->printer->printer &&
			    ! STREQU(pps->printer->name,
					prs->printer->printer->name))
				continue;
			if (STREQU(pps->remote_name, remote_name))
				break;
		}
		if (!pps) {
			DEBUGLOG2("Received GET_STATUS on unknown printer %s\n",
				remote_name);
			break;
		}

		load_bsd_stat(pss, pps, msgType, isMore, prs->printer);

		if (!isMore)
			rmreq(prs);

		/*
		 * Processed the last printer for pss,
		 * so clear the waiting-for-R_SEND_JOB flag.
		 */
		if (!isMore) {
			pss->exec->flags &= ~EXF_WAITJOB;
			pss->laststat = 0;

			if (pss->pmlist)
				freelist (pss->pmlist);
			pss->pmlist = pss->tmp_pmlist;
			pss->tmp_pmlist = NULL;

			md_wakeup (pss);
		}

		break;

	case R_INQUIRE_PRINTER_STATUS:
		/* Expecting this? */
		if (!(prs->status & RSS_RECVSTATUS)) {
			DEBUGLOG2(
			"Unexpected R_INQUIRE_PRINTER_STATUS from system %s\n",
				pss->system->name);
			break;
		}

		/* Protocol ok? */
		if (pss->system->protocol == BSD_PROTO) {
			DEBUGLOG2("Protocol mismatch: system %s BSD, got S5\n",
				pss->system->name);
			if (status != MOKMORE)
				rmreq(prs);
			break;
		}

		getmessage(sent_msg, R_INQUIRE_PRINTER_STATUS, &status,
			&remote_name, &s1, &s2, &s3, &s4, &h1, &s5, &l1, &l2);

		DEBUGLOG3("get R_INQUIRE_PRINTER_STATUS status %s on (%s)\n",
			statusName(status), remote_name);

		for (pps = walk_ptable(1); pps; pps = walk_ptable(0)) {
			if (pps->system != pss)
				continue;
			if (prs->printer && prs->printer->printer &&
			    ! STREQU(pps->printer->name,
					prs->printer->printer->name))
				continue;
			DEBUGLOG2("checking %s\n", pps->remote_name);
			if (STREQU(pps->remote_name, remote_name))
				break;
		}

		if (!pps) {
			DEBUGLOG2("can't find printer (%s)\n", remote_name);
			/*
			 * We might be in a situation where we get status for
			 * remote printers, but don't have any of those locally,
			 * but we do have local printers that are undefined
			 * on the remote system.  We need some msg to make sure
			 * we process an "unknown printer" response.  Use
			 * a empty printer name field for this and handle it
			 * as a special case in return_remote_status.
			 */
			putmessage(buf, R_INQUIRE_PRINTER_STATUS, MOK,
				((status == MNODEST) ?
				    ((prs->printer) ?
					prs->printer->remote_name : "") : ""),
				"", "",
				gettext("unknown printer"),
				"", PS_FAULTED, "", 0L, 0L);
			mesgadd(pss, buf);
	        } else {
			if (prs->printer == NULL) {
				PSTATUS *tpps;

				for (tpps = walk_ptable(1); tpps;
				     tpps = walk_ptable(0)) {
					if (tpps->system != pps->system)
						continue;
					if (STREQU(tpps->printer->name,
					    pps->printer->name))
						continue;
					if (! STREQU(tpps->remote_name,
					    pps->remote_name))
						continue;

					DEBUGLOG3("alias %s: %s\n",
						tpps->system->system->name,
						tpps->printer->name);
					putmessage(buf,
						R_INQUIRE_PRINTER_STATUS,
						MOKMORE, tpps->printer->name,
						s1, s2, s3, s4, h1, s5, l1, l2);
					mesgadd(pss, buf);
       				}
			}

			putmessage(buf, R_INQUIRE_PRINTER_STATUS, status,
				pps->printer->name,
				s1, s2, s3, s4, h1, s5, l1, l2);
			mesgadd (pss, buf);
		}

		if (status != MOKMORE) {
			rmreq(prs);

			/*
			 * Last of status received from the system, so
			 * clear the waiting-for-R_SEND_JOB flag.
			 */
			if (pss->pmlist)
				freelist (pss->pmlist);
			pss->pmlist = pss->tmp_pmlist;
			pss->tmp_pmlist = 0;
			pss->exec->flags &= ~EXF_WAITJOB;
			pss->laststat = 0;
			md_wakeup (pss);
		}

		break;

	case R_INQUIRE_REQUEST_RANK:
		/*
		 * Expecting this?
		 */
		if (!(prs->status & RSS_RECVSTATUS)) {
			DEBUGLOG2(
			"Unexpected INQUIRE_REQUEST_RANK from system %s\n",
				pss->system->name);
			break;
		}
		if (status != MOKMORE)
			rmreq (prs);

		/*
		 * Protocol ok?
		 */
		if (pss->system->protocol == BSD_PROTO) {
			DEBUGLOG2("Protocol mismatch: system %s BSD got S5\n",
				pss->system->name);
			break;
		}
		getmessage(sent_msg, R_INQUIRE_REQUEST_RANK, &status, &req_id,
			&s1, &l1, &l2, &h1, &s2, &s3, &s4, &rank, &s6);

		/*
		 * Filter out responses from remote machines that are queued
		 * on yet a third system.  Do this by checking that a job
		 * queued on a system is for a printer attached to that
		 * system.  If not, just throw away the response, since we'll
		 * this job reported again from the system that actually has
		 * the printer attached (as long as we know about that system,
		 * if not then we don't care anyway).
		 */

		if (status != MOKMORE && status != MOK) {
			putmessage(buf, R_INQUIRE_REQUEST_RANK, status, req_id,
				s1, l1, l2, h1, s2, s3, s4, rank, s6);
			mesgadd(pss, buf);
			DEBUGLOG3(
				"R_INQUIRE_REQUEST_RANK status %d reqId (%s)\n",
				status, req_id);
		}
		else {
			/* status is either MOK or MOKMORE */

			for (pps = walk_ptable(1); pps; pps = walk_ptable(0)) {
				if (pps->system != pss)
					continue;
				/*
				 * check if the printer name matches the printer
				 * name returned by the status call for this job
				 */
				if (STREQU(pps->remote_name, s2))
					break;
			}

			if (!pps) {
				/*
				 * This is queued for a remote printer
				 * skip it
				 */
				DEBUGLOG4("%s reporting remote job %s on %s\n",
					pss->system->name, req_id, s2);
			} else {
				/*
				 * This is queued for a local printer, so
				 * lets report it
				 */
				DEBUGLOG4("%s reporting local job (%s) on %s\n",
					pss->system->name, req_id, s2);

				putmessage(buf, R_INQUIRE_REQUEST_RANK,
					(status == MOK ? MOKMORE : status),
					req_id, s1, l1, l2, h1, s2, s3, s4,
					rank, s6);
				mesgadd(pss, buf);

				DEBUGLOG3(
				"R_INQUIRE_REQUEST_RANK status %s reqId (%s)\n",
					statusName(status), req_id);
			}
			update_req (req_id, rank);
		}

		if (status != MOKMORE) {
			/*
			 * If a list of new messages has been created
			 * free the old list (if any) and point to
			 * the new one.
			 */
			if (pss->pmlist)
				freelist (pss->pmlist);
			pss->pmlist = pss->tmp_pmlist;
			pss->tmp_pmlist = 0;
			/*
			 * Last of status received from the system, so
			 * clear the waiting-for-R_SEND_JOB flag.
			 */
			pss->exec->flags &= ~EXF_WAITJOB;
			md_wakeup (pss);
		}

		break;

	case R_UNMOUNT_TRAY:
	case R_MOUNT_TRAY:
		/*
		 * Expecting this?
		 */
		if (!(prs->status & RSS_RECVSTATUS)) {
			DEBUGLOG2(
			"Unexpected INQUIRE_REQUEST_RANK from system %s\n",
				pss->system->name);
			break;
		}
		rmreq(prs);

		/*
		 * Protocol ok?
		 */
		if (pss->system->protocol == BSD_PROTO) {
			DEBUGLOG2("Protocol mismatch: system %s BSD got S5\n",
				pss->system->name);
			break;
		}

		getmessage(sent_msg, msgType, &status);
		putmessage(buf, msgType, status);
		mesgadd(pss, buf);
		DEBUGLOG3("%s status %d\n", dispatchName(msgType), status);

		/*
		 * If a list of new messages has been created
		 * free the old list (if any) and point to
		 * the new one.
		 */
		if (pss->pmlist)
			freelist(pss->pmlist);
		pss->pmlist = pss->tmp_pmlist;
		pss->tmp_pmlist = 0;
		/*
		 * Last of status received from the system, so
		 * clear the waiting-for-R_SEND_JOB flag.
		 */
		pss->exec->flags &= ~EXF_WAITJOB;
		md_wakeup(pss);

		break;

	case R_CANCEL:
		if (!(prs->request->outcome & RS_CANCELLED)) {
			note("Unexpected R_CANCEL (not canceled)!\n");
			break;
		}

		getmessage(sent_msg, R_CANCEL, &status, &lstatus, &req_id);
		DEBUGLOG4("R_CANCEL: status: %s lstatus: %s reqid: %s\n",
			statusName(status), statusName((int) lstatus), req_id);

		/*
		 * lstatus can be unknown if the job is already done. 1111420
		 * lstatus can be NOINFO, NOPERM or 2LATE. 1141761
		 */
		if (lstatus != MNOINFO && lstatus != MUNKNOWN &&
		    lstatus != MNOPERM && lstatus != M2LATE &&
		    !STREQU(prs->secure->req_id, req_id)) {
			note("Out of sync on R_CANCEL: wanted %s got %s\n",
				prs->secure->req_id, req_id);
			break;
		}

		if (status != MOKMORE) {
			/*
			 * Clear the waiting-for-R_SEND_JOB flag now that
			 * we know this message isn't bogus.
			 */
			pss->exec->flags &= ~EXF_WAITJOB;
			prs->request->outcome &= ~RS_SENDING;
		}
/*
 * The S_CANCEL that we sent will cause notification of the job
 * completion to be sent back to us. s_job_completed() will called,
 * and it will do the following:
 *		dowait_remote (EX_NOTIFY, prs, 0, (char *)0);
 */
		break;

	case R_JOB_COMPLETED:
		if (!(prs->request->outcome & RS_DONE)) {
			DEBUGLOG1("Unexpected R_JOB_COMPLETED (not done)!\n");
			return;
		}

		DEBUGLOG2("Received R_JOB_COMPLETED, request %s\n",
			prs->secure->req_id);

		/*
		 * Clear the waiting-for-R_SEND_JOB flag now that
		 * we know this message isn't bogus.
		 */
		pss->exec->flags &= ~EXF_WAITJOB;

		getmessage (sent_msg, R_JOB_COMPLETED, &status);
		if (status == MUNKNOWN)
			note (
		"%s refused job completion notice for request %s.\n",
				name,
				prs->secure->req_id
			);

		prs->request->outcome &= ~RS_SENDING;
		dowait_remote (EX_NOTIFY, prs, 0, (char *)0);

		break;

	case R_SEND_FAULT:

		DEBUGLOG1("Received R_SEND_FAULT\n");

		/*
		 * Clear the waiting-for-R_SEND_JOB flag now that
		 * we know this message isn't bogus.
		 */
		pss->exec->flags &= ~EXF_WAITJOB;
		prs->request->outcome &= ~RS_SENDING;

		getmessage (sent_msg, R_SEND_FAULT, &status);
		if (status == MUNKNOWN)
			note (
		"%s refused job completion notice for request %s.\n",
				name,
				prs->secure->req_id
			);


		break;

	}

	if ((status != MOKMORE) && !(pss->exec->flags & EXF_WAITJOB) &&
	    (pss->laststat != 0))
		pss->laststat = 0;

	/*
	 * There may be another request waiting to go out over
	 * the network.
	 */
	schedule (EV_SYSTEM, pss);

	return;
}

/**
 ** s_job_completed()
 **/

void
s_job_completed(char *m, MESG *md)
{
	ENTRY ("s_job_completed")

	char *			req_id;
	char *			errfile;

	short			outcome;

	RSTATUS *		prs;


	getmessage(m, S_JOB_COMPLETED, &outcome, &req_id, &errfile);

	if (!(prs = request_by_id(req_id))) {
		DEBUGLOG2("Got S_JOB_COMPLETED for unknown request %s\n",
			req_id);
		mputm(md, R_JOB_COMPLETED, MUNKNOWN);
		return;
	}

	mputm(md, R_JOB_COMPLETED, MOK);
	dowait_remote(EX_INTERF, prs, outcome, errfile);
}
