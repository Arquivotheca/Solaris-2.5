/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)getstatus.c	1.16	94/04/18 SMI"	/* SVr4.0 1.6	*/

#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "lp.h"
#include "msgs.h"
#include "lpd.h"

static	char	*r_get_status(int, char *, short, short);
static	int	parseJob(char *, char **, int *, char **, char **);
static	int	parseSize(long *, char **);

/*
 * Gather job status from remote lpd
 * (S_GET_STATUS processing)
 */
char *
s_get_status(char *msg)
{
	FILE	*fp;
	char	*filename;
	char	*user, *host, *jobid;
	int	 status;
	int	 rank = 0;
	int	 n;
	int	 doing_queue = 0;
	int	 more = 0;
	mode_t	 omask;
	short msgType;
	short isMore;
	int duplicateLines = 0;
	char tmpLine[BUFSIZ];

	(void)getmessage(msg, S_GET_STATUS, &Printer, &filename, &msgType,
		&isMore);
	Printer = strdup(Printer);
	logit(LOG_DEBUG, "S_GET_STATUS(\"%s\", \"%s\" %d)", Printer, filename,
			  msgType);
	omask = umask(077);
	fp = fopen(filename, "w");
	(void)umask(omask);
	if (!fp) {
		logit(LOG_WARNING,
		      "s_get_status: can't open %s: %s", filename, PERROR); 
		status = MNOINFO;
		goto out;
	}
	if (!snd_lpd_msg(DISPLAYQL, Printer, "", "")) {
		status = MNOINFO;
		goto out;
	}
	logit(LOG_DEBUG, "s_get_status(): sent query, awaiting response");

	tmpLine[0] = NULL;
	while (getNets(Buf, BUFSIZ)) {
		int	eof;
		long	size;

		if (strcmp(Buf, tmpLine)) {
			strcpy(tmpLine, Buf);
			duplicateLines = 0;
		} else if (duplicateLines++ >10) {
			logit(LOG_INFO, "s_get_status(): duplicate lines (%s)",
				tmpLine);
			break;
		}

		n = strlen(Buf);
		if (strspn(Buf, " \t\n\r") == n)   /* get rid of blank lines */
			continue;

		if (parseJob(Buf, &user, &rank, &jobid, &host)) {
			char *files = NULL;

			doing_queue = 1;
			(void)fprintf(fp, "%s:%d:%s:%s:",
				user, rank, jobid, host);
			eof = parseSize(&size, &files);
			(void)fprintf(fp, "%ld:%s\n", size, files);
			logit(LOG_DEBUG, "file list: %s", files);
			if (eof)
				break;

		} else if (doing_queue) {
			continue;

		} else {
			if (STREQU(NOENTRIES, Buf))
				continue;
			if (!strcmp("No entries.\n", &Buf[n - 12]))
				continue;	/* for PC-NFS */
			if (!more)
				fputs(PRINTER_STATUS_TAG, fp);
			(void)fprintf(fp, "%s", Buf);
			if (Buf[n-1] == '\n')
				more = 0;
			else
				more = 1;
		}
	}
	if (errno) {
		(void) fprintf(fp, "%ssystem not responding\n",
				PRINTER_STATUS_TAG);
	}
	logit(LOG_DEBUG, "s_get_status(): reply done, errno=%d",errno);
	status = MOK;
out:
	if (fp)
		fclose(fp);			/* close status file */
	msg = r_get_status(status, Printer, msgType, isMore);
	free(Printer);
	return(msg);
}

/*
 * Parse job status returned from remote lpd
 */
static
parseJob(char *p, char **puser, int *prank, char **pjobid, char **phost)
{
	register char	*t1, *t2;
	int		 rank;

	/*
	 * Attempt to parse lines of the form:
	 * user: rank                            [job jobidHostname]
	 * user: rank                            [job reqid Hostname]
	 * user  rank                            [job jobidHostname] (for pcnfs)
	 */
	if (isspace(*p))
		return(0);
	if ((t1 = strchr(p, ' ')) == NULL)
		return(0);
	if (*(t1 - 1) == ':')
		t1--;
	*puser = p;
	for (p = t1 + 1; isspace(*p); p++)
		;
	if (STRNEQU(p, "active", STRSIZE("active")))
		rank = 0;
	else {
		if (!isdigit(*p))
			return(0);
		rank = atoi(p);
	}
	if ((p = strchr(p, '[')) == NULL || 
	   !STRNEQU(p, "[job", STRSIZE("[job")))
		return(0);
	for (p += STRSIZE("[job"); isspace(*p); p++)
		;
	if ((t2 = strchr(p, ']')) == NULL)
		return(0);

	/* Now OK to alter input string */ 

	*t1 = *t2 = NULL;
	if (t1 = strchr(p, ' ')) {	/* check for S5 format */
		*t1 = NULL;
		*pjobid = p;
		*phost = t1+1;
	} else {		/* this is sick */
		for (t1 = t2 = p ; isdigit(*t1) ; t1++ )
			*(t1-1) = *t1;
		*(t1-1) = NULL;
		*pjobid = t2 - 1;
		*phost = t1;
	}
	if (rank < *prank)	/* in case status returned from > 1 machine */
		(*prank)++;
	else
		*prank = rank;
	return(1);
}

/*
 * Parse 2nd line of job status returned from remote lpd
 * Attempt to parse lines of the form (the 2nd & 3rd lines are literals):
 *  filename                            size bytes
 *  standard input                      size bytes
 *  <file name not available>           size bytes
 * A blank line will terminate the list of files.
 */

static int
parseSize(long *size, char **files)
{
	char	*p;
	char    *q;

	*files = NULL;
	*size = 0;
	while (getNets(Buf, BUFSIZ)) {
	 	int i;

		if ((i=strspn(Buf, " \t\n")) == strlen(Buf))
			/* blank line; done */
			return (0);
		q = &Buf[i];	
		p = Buf + strlen(Buf) - 1;
		for (; isspace(*p) && p >= Buf; p--);	/* trailing blanks */
		for (; ! isspace(*p) && p >= Buf; p--);	/* the word bytes */
		for (; isspace(*p) && p >= Buf; p--);	/* more spaces */
		*(p + 1) = NULL;
		for (; ! isspace(*p) && p >= Buf; p--);	/* the size */
		p++;					/* now pnting to size */
		*size += atol(p);
		while (isspace(*--p) && p>=q) *p = NULL;
		*files = (char *)strdup(q);
	}

	return (1);
}

static char *
r_get_status(int status, char * printer, short msgType, short isMore)
{
	logit(LOG_DEBUG, "R_GET_STATUS(%d, \"%s\" %d %d)", status, printer,
			  msgType, isMore);
	if (putmessage(Msg, R_GET_STATUS, status, printer, msgType, isMore) < 0)
		return(NULL);
	else
		return(Msg);
}
