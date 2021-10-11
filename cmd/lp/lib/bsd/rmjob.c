/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)rmjob.c	1.10	94/03/09 SMI"	/* SVr4.0 1.3	*/

#include <string.h>
#include <locale.h>
#include <sys/types.h>
#include <unistd.h>
#include "lp.h"
#include "msgs.h"
#include "requests.h"
#include "lpd.h"
#include "oam_def.h"

static	char	* mkuser(char *, char *);
static	void	  cancel(char *, char *);
static	int	  isowner(char *, char *, char *);

int isclass(char *);

/*
 * Remove local print job(s)
 */
void
rmjob(void)
{
	int	  i;
	int	  all = 0;
	short	  status;
	char	 *owner;
	char	 *reqid;
	char	 *host;
	char	**rmjobs = NULL;

	if (!isprinter(Printer) && ! isclass(Printer)) {
		fatal(gettext("unknown printer/class"));
		/*NOTREACHED*/
	}
	if (Nusers < 0)
		if (getuid() == 0)
			all = 1;
		else {
			User[0] = Person;
			Nusers = 1;
		}
	if (STREQU(Person, ALL)) {	/* "-all" same as "root" */
		if (!Rhost) {		/* only allowed from remote */
			fatal(gettext("The login name \"%s\" is reserved"),
				ALL);
			/*NOTREACHED*/
		}
		all = 1;
	}
	if (all) {
		cancel(Rhost ? mkuser(Rhost, NAME_ALL) : "all!all", "");
		return;
	}
	if (!Nusers && !Nrequests) {	/* cancel current job */
		if (STREQU(Person, "root"))
			cancel(Rhost ? mkuser(Rhost, NAME_ALL) : "all!all", CURRENT_REQ);
		else
			cancel(mkuser(Rhost, Person), CURRENT_REQ);
		return;
	}
	snd_msg(S_INQUIRE_REQUEST, "", Printer, "", "", "");
	do {
		size_t	 size;
		time_t	 date;
		short	 outcome;
		char	*dest, *form, *pwheel, *file;
		int	 match = 0;

		rcv_msg(R_INQUIRE_REQUEST, &status, 
					   &reqid, 
					   &owner, 
					   &size, 
					   &date, 
					   &outcome, 
					   &dest, 
					   &form, 
					   &pwheel,
					   &file);
		switch(status) {
		case MOK:
		case MOKMORE:
			logit(LOG_DEBUG, 
			"R_INQUIRE_REQUEST(\"%s\", \"%s\", \"%s\", 0x%x)",
						reqid, owner, dest, outcome);
			if (outcome & RS_DONE)
				break;
			parseUser(owner, &host, &owner);
			for (i=0; !match && i<Nrequests; i++)
				if (STREQU(reqid, Request[i]) &&
				    isowner(reqid, host, owner))
					match++;
			for (i=0; !match && i<Nusers; i++)
				if (STREQU(owner, User[i]) &&
				    isowner(reqid, host, owner))
					match++;
			if (match) {
				char	buf[100];

				sprintf(buf, "%s %s", mkuser(host, owner), 
						      reqid);
				appendlist(&rmjobs, buf);
			}
			break;

		default:
			break;
		}
	} while (status == MOKMORE);
	if (rmjobs) {
		char	**job;

		for (job = rmjobs; *job; job++) {
			reqid = strchr(*job, ' ');
			*reqid++ = NULL;
			cancel(*job, reqid);		/* *job == owner */
		}
		freelist(rmjobs);
	}
	fflush(stdout);
}

static void
cancel(char *user, char *reqid)
{
	short	 status1; 
	long	 status2;

	logit(LOG_DEBUG, "S_CANCEL(\"%s\", \"%s\", \"%s\")", 
					Printer, user, reqid);
	snd_msg(S_CANCEL, Printer, user, reqid);
	do {
		rcv_msg(R_CANCEL, &status1, &status2, &reqid);
		logit(LOG_DEBUG, "R_CANCEL(%d, %d, \"%s\")", status1, status2, reqid);
		if (status1 == MNOINFO)		/* quiet if job doesn't exist */
			break;
		if (Rhost)
			printf("%s: ", Lhost);
		switch(status2) {

		case MOK:
			/*
		 	* It is not possible to know if remote really responded
		 	* to request for job removal; however, the job should
			* eventually be removed.
		 	*/
			printf(gettext("%s dequeued\n"), reqid);
			break;

		case MNOPERM:		/* This should be caught earlier */
					/* (except for CURRENT_REQ)	 */
			printf(gettext("%s: Permission denied\n"), reqid);
			break;

		case M2LATE:
			printf(gettext("cannot dequeue %s\n"), reqid);
			break;

		default:
			break;

		}
	} while (status1 == MOKMORE);
	fflush(stdout);
}

static
isowner(char *reqid, char *host, char *user)
{
	if (STREQU(Person, "root")) {	/* trapped later if impostor */
		if (!Rhost || STREQU(Rhost, host))
			return(1);
	} else if (STREQU(Person, user))
		if (Rhost ? STREQU(Rhost, host) : STREQU(Lhost, host))
			return(1);
	if (Rhost)
		printf("%s: ", Lhost);
	printf(gettext("%s %s: Permission denied\n"), reqid, host);
	return(0);
}

static char *
mkuser(char *host, char *user)
{
	static char	buf[50];

	if (host && Lhost && strcmp(host, Lhost) == 0)
		host = 0;
	sprintf(buf, "%s%s%s", host ? host : "",
			       host ? "!"  : "",
			       user ? user : "");
	return(buf);
}
