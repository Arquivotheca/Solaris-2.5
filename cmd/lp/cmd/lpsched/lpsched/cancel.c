/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)cancel.c	1.12	94/01/24 SMI"	/* SVr4.0 1.2.1.4	*/

#include "lpsched.h"

static char cerrbuf[160] = "";	/* fix for bugid 1100252	*/


/**
 ** cancel() - CANCEL A REQUEST
 **/

int
#if	defined(__STDC__)
cancel (
	register RSTATUS *	prs,
	int			spool
)
#else
cancel (prs, spool)
	register RSTATUS	*prs;
	int			spool;
#endif
{
	ENTRY ("cancel")

	if (prs->request->outcome & RS_DONE)
		return (0);

	prs->request->outcome |= RS_CANCELLED;

	DEBUGLOG5("start cancel spool %d %d status %x act %x\n",
		spool, prs->printer, (prs->printer ? prs->printer->status : 0),
		prs->request->actions);

	if ((spool || (prs->request->actions & ACT_NOTIFY)) &&
	    (prs->request->version != VERSION_BSD))
		prs->request->outcome |= RS_NOTIFY;

	/*
	 * If the printer for this request is on a remote system,
	 * send a cancellation note across. HOWEVER, this isn't
	 * necessary if the request hasn't yet been sent!
	 */
	if (prs->printer && prs->printer->status & PS_REMOTE &&
	    prs->request->outcome & (RS_SENT | RS_SENDING)) {
		/*
		 * Mark this request as needing sending, then
		 * schedule the send in case the connection to
		 * the remote system is idle.
		 */
		prs->status |= RSS_SENDREMOTE;
		DEBUGLOG1("remote cancel\n");
		schedule(EV_SYSTEM, prs->printer->system);

	} else if (prs->request->outcome & RS_PRINTING) {
		DEBUGLOG1("local cancel\n");
		terminate(prs->printer->exec);
	}
	else if (prs->request->outcome & RS_FILTERING) {
		terminate (prs->exec);
	}
	else if (prs->request->outcome | RS_NOTIFY) {
		/* start fix for bugid 1100252	*/
		DEBUGLOG1("send cancel mail\n");
		if (prs->printer->status & PS_REMOTE) {
			sprintf(cerrbuf,
				"Remote status=%d, canceled by remote system\n",
				prs->reason);
		}
		notify (prs, cerrbuf, 0, 0, 0);
		cerrbuf[0] = (char) NULL;
		/* end fix for bugid 1100252	*/
	}
	check_request (prs);

	return (1);
}
