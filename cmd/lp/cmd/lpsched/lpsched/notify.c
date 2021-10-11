/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)notify.c	1.7	93/10/07 SMI"	/* SVr4.0 1.2.1.3	*/

#include "lpsched.h"

static char		*N_Msg[] = {
	"Subject: Status of lp request %s\n\nYour request %s destined for %s%s\n",
	"has completed successfully on printer %s.\n",
	"was canceled by the lpsched daemon%s\n", /* bugfix 1100252 */
	"encountered an error during filtering.\n",
	"encountered an error while printing on printer %s.\n",
	"Filtering stopped with an exit code of %d.\n",
	"Printing stopped with an exit code of %d.\n",
	"Filtering was interrupted with a signal %d.\n",
	"Printing was interrupted with a signal %d.\n",
	"\nReason for failure:\n\n%s\n",
	"\nReason for being canceled:\n\n%s\n",
};

static struct reason {
	short			reason;
	char			*msg;
}			N_Reason[] = {
    {
	MNODEST,
	"The requested print destination has been removed."
    }, {
	MERRDEST,
	"All candidate destinations are rejecting further requests."
    }, {
	MDENYDEST,
	"You are no longer allowed to use any printer suitable for\nthe request."
    }, {
	MDENYDEST,
	"No candidate printer can handle these characteristics:"
    }, {
	MNOMEDIA,
	"The form you requested no longer exists."
    }, {
	MDENYMEDIA,
	"You are no longer allowed to use the form you requested."
    }, {
	MDENYMEDIA,
	"The form you wanted now requires a different character set."
    }, {
	MNOFILTER,
	"There is no longer a filter that will convert your file for printing."
    }, {
	MNOMOUNT,
	"The form or print wheel you requested is not allowed on any\nprinter otherwise suitable for the request."
    }, {
	MNOSPACE,
	"Memory allocation problem."
    }, {
	-1,
	""
    }
};

/* start fix for bugid 1100252 */
/* this table is placed here to handle remote cancellation notices.
 * if at a future time you have a cryptic message, you can now give the
 * user a better idea of what went wrong, based on how the user triggered
 * the error, and the remote status message which comes back. The latter
 * should now be emailed back to the user.
 *
 * N.B. some of the error status's may be strange; this is why room is
 * being left for improvement. E.g. a mistyped content type sent to a
 * bsd printer gives you back an MNOMEM!! if you find other ways to
 * trigger this in the future, give the user a helpful hint here.
 */

static struct reason R_Reason[] = {
    {
	MNOMEM,
	"You may have specified an incorrect file content type."
    }, {
	MERRDEST,
	"Error talking to printer or print server, possibly down"
    }, {
	MUNKNOWN,
	"File possibly removed before filtered, printed or transfered"
    }, {
	-1,
	""
    }
};
/* end fix for bugid 1100252	*/	
	
	
#if	defined(__STDC__)
static void		print_reason ( FILE * , int );
#else
static void             print_reason();
#endif

/* start fix for bugid 1100252	*/	
#if	defined(__STDC__)
static void		print_reason_rmt ( FILE * , int );
#else
static void             print_reason_rmt();
#endif
/* end fix for bugid 1100252	*/	

/**
 ** notify() - NOTIFY USER OF FINISHED REQUEST
 **/
	
void
#if	defined(__STDC__)
notify (
	register RSTATUS *	prs,
	char *			errbuf,
	int			k,
	int			e,
	int			slow
)
#else
notify (prs, errbuf, k, e, slow)
	register RSTATUS        *prs;
	char                    *errbuf;
	int                     k,
				e,
				slow;
#endif
{
	ENTRY ("notify")

	register char		*cp;

	char			*file;

	FILE                    *fp;


	/*
	 * Screen out cases where no notification is needed.
	 */
	if (!(prs->request->outcome & RS_NOTIFY))
		return;
	if (
		!(prs->request->actions & (ACT_MAIL|ACT_WRITE|ACT_NOTIFY))
	     && !prs->request->alert
	     && !(prs->request->outcome & RS_CANCELLED)
	     && !e && !k && !errbuf       /* exited normally */
	)
		return;

	/*
	 * Create the notification message to the user.
	 */
	file = makereqerr(prs);
	if ((fp = open_lpfile(file, "w", MODE_NOREAD))) {
		fprintf (
			fp,
			N_Msg[0],
			prs->secure->req_id,
			prs->secure->req_id,
			prs->request->destination,
			STREQU(prs->request->destination, NAME_ANY)?
				  " printer"
				: ""
		);

		if (prs->request) {
			char file[BUFSIZ];
			
			GetRequestFiles(prs->request, file, sizeof(file));
			fprintf(fp, "\nThe job title was:\t%s\n", file);
			fprintf(fp, "   submitted from:\t%s\n",
				prs->secure->system);
			fprintf(fp, "               at:\t%s\n",
				ctime(&prs->secure->date));
		}
	
		if (prs->request->outcome & RS_PRINTED)
			fprintf (
				fp,
				N_Msg[1],
				prs->printer->printer->name
			);

		if (prs->request->outcome & RS_CANCELLED)
			fprintf (
				fp,
				N_Msg[2],
				(prs->request->outcome & RS_FAILED)?
					  ", and"
					: "."
			);
		
	
		if (prs->request->outcome & RS_FAILED) {
			if (slow)
				fputs (N_Msg[3], fp);
			else
				fprintf (
					fp,
					N_Msg[4],
					prs->printer->printer->name
				);
	
			if (e > 0)
				fprintf (fp, N_Msg[slow? 5 : 6], e);
			else if (k)
				fprintf (fp, N_Msg[slow? 7 : 8], k);
		}
	
		if (errbuf) {
			for (cp = errbuf; *cp && *cp == '\n'; cp++)
				;
			fprintf (fp, N_Msg[9], cp);
			if (prs->request->outcome & RS_CANCELLED)
				fputs ("\n", fp);
		}

		/* start fix for bugid 1100252	*/
		if (prs->request->outcome & RS_CANCELLED) {
			if (prs->printer->status & PS_REMOTE) {
				print_reason_rmt(fp, prs->reason);
			} else {
				print_reason (fp, prs->reason);
			}
		}

		close_lpfile (fp);
		schedule (EV_NOTIFY, prs);

	}
	if (file)
		Free (file);

	return;
}

/**
 ** print_reason() - PRINT REASON FOR AUTOMATIC CANCEL
 **/

static void
#if	defined(__STDC__)
print_reason (
	FILE *			fp,
	int			reason
)
#else
print_reason (fp, reason)
	FILE			*fp;
	register int		reason;
#endif
{
	ENTRY ("print_reason")

	register int		i;


#define P(BIT,MSG)	if (chkprinter_result & BIT) fputs (MSG, fp)

	for (i = 0; N_Reason[i].reason != -1; i++)
		if (N_Reason[i].reason == reason) {
			if (reason == MDENYDEST && chkprinter_result)
				i++;
			if (reason == MDENYMEDIA && chkprinter_result)
				i++;
			fprintf (fp, N_Msg[10], N_Reason[i].msg);
			if (reason == MDENYDEST && chkprinter_result) {
				P (PCK_TYPE,	"\tprinter type\n");
				P (PCK_CHARSET,	"\tcharacter set\n");
				P (PCK_CPI,	"\tcharacter pitch\n");
				P (PCK_LPI,	"\tline pitch\n");
				P (PCK_WIDTH,	"\tpage width\n");
				P (PCK_LENGTH,	"\tpage length\n");
				P (PCK_BANNER,	"\tno banner\n");
			}
			break;
		}

	return;
}


/**
 ** print_reason_rmt() - PRINT REASON FOR REMOTE CANCEL
 **/

static void
#if	defined(__STDC__)
print_reason_rmt (
	FILE *			fp,
	int			reason
)
#else
print_reason_rmt (fp, reason)
	FILE			*fp;
	register int		reason;
#endif
{
	ENTRY ("print_reason_rmt")

	register int		i;

	for (i = 0; R_Reason[i].reason != -1; i++) {
		if (R_Reason[i].reason == reason) {
			fprintf (fp, N_Msg[10], R_Reason[i].msg);
			break;
		}
	}

	return;
}
