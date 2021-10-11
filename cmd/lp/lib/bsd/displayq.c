/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)displayq.c	1.21	94/03/31 SMI"	/* SVr4.0 1.4	*/

#include <string.h>
#include <locale.h>
#include "lp.h"
#include "msgs.h"
#include "requests.h"
#include "printers.h"
#include "systems.h"
#include "lpd.h"
#include "oam_def.h"

#if CLASS
#undef CLASS
#endif
#include "class.h"

static int	 Col;		/* column on screen */

static	char	* printerstatus(int *);
static	int	  inlist(char *, char *, char *);
static	void	  show(int, size_t, char *);
static	void 	  blankfill(int);
static	void 	  header(void);
static	void 	  prank(int, int);
static	int	put_flt(char *, char *);

int remote_cmd = 0;	/* flag for remote status - enabled by lpq */

/*
 * Display lpq-like status to stdout
 */
int
displayq(int format)
{
	short	 status, outcome, state = 0, rank = 0;
	char	*host, *owner, *reqid, *shortReqid, *dest, *form, *pwheel,
		*printer, *character_set, *prst, *file;
	size_t	 size;
	time_t	 date;
	int      need_header = format == 0;
	int      printer_faulted;
	char     **plist = NULL;
	CLASS	*clp = NULL;

	prst = printerstatus(&printer_faulted);		/* have to get status ahead of time */

	if (isclass(Printer) && (clp = getclass(Printer)))
		plist = clp->members;

	do {
	 if (plist) 
		Printer = *(plist++);
	if (remote_cmd)
		snd_msg(S_INQUIRE_REQUEST_RANK, 2, "", Printer, "", "", "");
	else
		snd_msg(S_INQUIRE_REQUEST, "", Printer, "", "", "");
	do {
		if (remote_cmd)
			rcv_msg(R_INQUIRE_REQUEST_RANK, &status, &reqid,
				&owner, &size, &date, &state, &printer, &form,
				&character_set, &rank, &file);
		else
			rcv_msg(R_INQUIRE_REQUEST, &status, &reqid,
				&owner, &size, &date, &outcome, &dest, &form,
				&pwheel, &file);

		switch(status) {
		case MOK:
		case MOKMORE:
			if (prst) {
				(void)printf("%s", prst);
				fflush(stdout);
				prst = NULL;
			}
			if (need_header) {	/* short format */
				header();
				need_header = 0;
			}
			break;

		case MNOINFO:
			if (clp != NULL) {
			        if (prst != NULL)
				        printf(gettext(prst));
			        prst = NULL;
			        break;
			}
			if (prst && *prst && printer_faulted)
				(void)printf(gettext(prst));
			else
				(void)printf(gettext(NOENTRIES));
			fflush(stdout);
			return(0);

		default:
			return(0);
		}
		if (status == MNOINFO)
		  break;
		parseUser(owner, &host, &owner);
		if (!(inlist(host, owner, reqid)))
			continue;
		shortReqid = strrchr(reqid, '-') + 1;
		if (format == 0) {		/* short format */
			Col = 0;
			prank(rank++, state);
			blankfill(OWNCOL);
			Col += printf("%s", owner);
			blankfill(REQCOL);
			Col += printf("%s", shortReqid);
			blankfill(FILCOL);
		} else {			/* long format */
			Col = printf("\n%s: ", owner) - 1;
			prank(rank++, state);
			blankfill(JOBCOL);
			(void)printf(gettext("[job %s %s]\n"),
				shortReqid, host); 
			Col = 0;
		}
		show(format, size, file);
	} while (status == MOKMORE);
        } while (plist && (*plist != NULL));
	fflush(stdout);
	return(1);
}

/*
 * Return pointer to printer status string
 */
static char *
printerstatus(int *printer_faulted)
{
	short	status;
	char	*prname = NULL,
		*form = NULL,
		*pwheel = NULL,
		*dis_reason = NULL,
		*rej_reason = NULL;
	short	 prstatus;
	char	*reqid = NULL;
	time_t	*dis_date = NULL,
		*rej_date = NULL;
	int	 type;
	int	 n = 0;
	PRINTER	*pr;
	SYSTEM	*sys = NULL;
	static char	 buf[1024];
 
/* if (!isprinter(Printer) && ! isclass(Printer)) { */

	if (isclass(Printer)) 
		return(gettext("printer class: status unknown\n"));
		

	if (!(pr = getprinter(Printer))) {
		fatal(gettext("unknown printer"));
		/*NOTREACHED*/
	}
	*printer_faulted=1;
	if (pr->remote)  {
		if (prname = strchr(pr->remote, '!'))
			*prname = NULL;
		if (sys = getsystem(pr->remote))
			type = S_INQUIRE_REMOTE_PRINTER;
		else {
			logit(LOG_WARNING, "No system entry for %s",
				pr->remote);
			type = S_INQUIRE_PRINTER_STATUS;
		}
	} else
		type = S_INQUIRE_PRINTER_STATUS;

	buf[0] = NULL;
	snd_msg(type, Printer);
	rcv_msg(R_INQUIRE_PRINTER_STATUS, &status, &prname, &form, &pwheel, 
					  &dis_reason, &rej_reason, &prstatus, 
					  &reqid, &dis_date, &rej_date);
	switch(status) {
	case MNODEST:
		fatal(gettext("unknown printer"));
		/*NOTREACHED*/
	case MOK:
		/* 
		 * It is not possible to know if remote really responded
		 * to request for status.  Lpsched may return old status;
		 * however, if we can determine that status is indeed old,
		 * then we might consider printing:
		 *	if (Rhost)
		 *		printf("%s: ", Lhost);
		 *	printf("connection to %s is down\n", pr->remote);
		 */
		break;
	case MNOINFO:
		return(buf);
		/*NOTREACHED*/
	default:
		lp_fatal(E_LP_BADSTATUS, status);
		/*NOTREACHED*/
	}

	if (type == S_INQUIRE_REMOTE_PRINTER && sys->protocol == BSD_PROTO) {
		int origN;

		origN = n;
		if (*dis_reason) {
			if (strncmp("Warning: ", dis_reason, 9) == 0) {
				int tmp;

				if (! (tmp = put_flt(buf + n, dis_reason)))
					n += sprintf(buf+n, "%s",
						gettext(dis_reason));
				else
					n += tmp;
			}
			else
				n += sprintf(buf+n, "%s", gettext(dis_reason));
		}
		if (*rej_reason) n += sprintf(buf+n, "%s", rej_reason);
		if (origN == n  ) {
			*printer_faulted = 0;
			n += sprintf(buf+n,
				gettext("%s is ready and printing\n"), 
				prname);
		}
	} else {
		if (prstatus & (PS_DISABLED | PS_FAULTED))
			n += sprintf(buf+n,
				gettext("Warning: %s is down: %s\n"),
				prname, gettext(dis_reason));
		if (prstatus & PS_REJECTED)
			n += sprintf(buf+n,
			gettext("Warning: %s queue is turned off: %s\n"),
				prname, gettext(rej_reason));

		/* this message is not produced by bsd lpq */
		else if (!(prstatus & (PS_DISABLED | PS_FAULTED))) {
			*printer_faulted = 0;
			n += sprintf(buf+n,
				gettext("%s is ready and printing\n"), 
				prname);
		}
	}

	/*
	 * There are other types of status that lpd reports, e.g.:
	 *	sending to remote
	 *	waiting for remote to come up
	 *	waiting for queue to be enabled on remote
	 *	no space on remote; waiting for queue to drain
	 *	waiting for printer to become ready (offline?)
	 * but there is no way to determine this information from the
	 * lpsched printer status
	 */ 
	freesystem(sys);
	freeprinter(pr);
	if (buf[n-1] != '\n')
		 strcat(buf, "\n");
	return(buf);
}

/*
 * if the msg is in the format:
 *  Warning: <printer> is down: <reason>\n
 * break it up and reprint it out so we can do it in the users language.
 */
static
int put_flt(char *buf, char *flt)
{
	int n;
	char *dup, *p, *printer;

	if ((dup = strdup(flt)) == NULL)
		return (0);

	/* start after "Warning: " */
	if ((printer = strtok(dup + 9, " ")) == NULL) {
		free(dup);
		return (0);
	}

	p = strtok(NULL, " ");
	if (p == NULL || strcmp(p, "is") != 0) {
		free(dup);
		return (0);
	}

	p = strtok(NULL, " ");
	if (p == NULL || strcmp(p, "down:") != 0) {
		free(dup);
		return (0);
	}

	p += 6;
	if (*p == NULL) {
		free(dup);
		return (0);
	}

	if (*(p + (strlen(p) - 1)) == '\n')
		*(p + (strlen(p) - 1)) = 0;
	putchar('\t');
	n = sprintf(buf, gettext("Warning: %s is down: %s\n"),
		printer, gettext(p));

	free(dup);
	return (n);
}

/*
 * Return 1 if user or request-id has been selected, 0 otherwise
 */
static
inlist(char *host, char *name, char *reqid)
{
	register char 	**p;

	if (!Nusers && !Nrequests)
		return(1);
	/*
	 * Check to see if it's in the user list
	 */
	for (p = User; p < &User[Nusers]; p++)
		if (STREQU(*p, name))
			return(1);
	/*
	 * Check the request list
	 */
	if (Rhost ? !STREQU(host, Rhost) : !STREQU(host, Lhost))
		return(0);
	for (p = Request; p < &Request[Nrequests]; p++)
		if (STREQU(*p, reqid))
			return(1);
	return(0);
}

/*
 * Print lpq header
 */
static void
header(void)
{
	Col = printf(gettext(HEAD0));
	blankfill(SIZCOL);
	(void)printf(gettext(HEAD1));
	fflush(stdout);
	Col = 0;
}

static void
blankfill(register int n)
{
	do				/* always output at least one blank */
		putchar(' ');
	while (++Col < n);
}

static void
prank(int n, int state)
{
	static char 	*r[] = {
		"th", "st", "nd", "rd", "th", "th", "th", "th", "th", "th"
	};
	char		 buf[20];

	if (state & RS_HELD) {
		Col += printf(gettext("held"));
		return;
	}
	if (n == 0) {
		Col += printf(gettext("active"));
		return;
	}
	if ((n/10) == 1)
		(void)sprintf(buf, "%dth", n);
	else
		(void)sprintf(buf, "%d%s", n, r[n%10]);
	Col += printf("%s", buf);
}

static void
show(int f, size_t filesize, char *file)
{
	if ( !file || STREQU(file, "") )
		file = gettext(NO_FILENAME);
	if (STREQU(file, "stdin"))
		file = gettext("standard input");
	if (f == 0) {		/* short format */
		Col += printf("%s", gettext(file));
		blankfill(SIZCOL);
	} else {		/* long format */
		Col += printf("        %s", gettext(file));
		blankfill(JOBCOL);
	}
	(void)printf(gettext("%lu bytes\n"), filesize );
	Col = 0;
}

