/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	Portions Copyright (c) 1988, Sun Microsystems, Inc.	*/
/*	All Rights Reserved. 					*/

#ident	"@(#)touch.c	1.17	95/02/06 SMI"	/* SVr4.0 1.11	*/
/*	Copyright (c) 1987, 1988 Microsoft Corporation	*/
/*	  All Rights Reserved	*/

/*	This Module contains Proprietary Information of Microsoft  */
/*	Corporation and should be treated as Confidential.	   */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <libgen.h>
#include <fcntl.h>
#include <pwd.h>
#include <time.h>
#include <unistd.h>
#include <locale.h>
#include <utime.h>

#define	BADTIME	"bad time specification"

static char	*myname;

static int isnumber(char *);
static int atoi_for2(char *);
static void usage(const int);
static void touchabort(const char *);
static time_t parse_time(char *);
static time_t parse_datetime(char *);

main(argc, argv)
int argc;
char *argv[];
{
	register c;

	int		aflag	= 0;
	int		cflag	= 0;
	int		rflag	= 0;
	int		mflag	= 0;
	int		tflag	= 0;
	int		stflag	= 0;
	int		status	= 0;
	int		usecurrenttime = 1;
	int		optc;
	int		fd;
	struct stat	stbuf;
	struct stat	prstbuf;
	struct utimbuf	times;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	myname = basename(argv[0]);
	if (strcmp(myname, "settime") == NULL) {
		cflag++;
		stflag++;
		while ((optc = getopt(argc, argv, "f:")) != EOF)
			switch (optc) {
			case 'f':
				rflag++;
				usecurrenttime = 0;
				if (stat(optarg, &prstbuf) == -1) {
					(void) fprintf(stderr, "%s: ", myname);
					perror(optarg);
					return (2);
				}
				break;
			case '?':
				usage(stflag);
				break;
			};
	} else
		while ((optc = getopt(argc, argv, "acfmr:t:")) != EOF)
			switch (optc) {
			case 'a':
				aflag++;
				usecurrenttime = 0;
				break;
			case 'c':
				cflag++;
				break;
			case 'f':	/* silently ignore for UCB compat */
				break;
			case 'm':
				mflag++;
				usecurrenttime = 0;
				break;
			case 'r':	/* same as settime's -f option */
				rflag++;
				usecurrenttime = 0;
				if (stat(optarg, &prstbuf) == -1) {
					(void) fprintf(stderr, "%s: ", myname);
					perror(optarg);
					return (2);
				}
				break;
			case 't':
				tflag++;
				usecurrenttime = 0;
				prstbuf.st_mtime = prstbuf.st_atime =
				    parse_time(optarg);
				break;
			case '?':
				usage(stflag);
				break;
			}

	argc -= optind;
	argv += optind;

	if ((argc < 1) || (rflag + tflag > 1))
		usage(stflag);

	if ((aflag == 0) && (mflag == 0)) {
		aflag = 1;
		mflag = 1;
	}

	if ((rflag == 0) && (tflag == 0)) {
		if ((argc == 1) || (!isnumber(*argv))) {
			prstbuf.st_mtime = prstbuf.st_atime =
			    time((long *) 0);
		} else {
			prstbuf.st_mtime = prstbuf.st_atime =
			    parse_datetime(*argv++);
			usecurrenttime = 0;
			argc--;
		}
	}
	for (c = 0; c < argc; c++) {
		if (stat(argv[c], &stbuf)) {
			if (cflag) {
				continue;
			} else if ((fd = creat(argv[c],
			    (S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH)))
			    < 0) {
				(void) fprintf(stderr,
				    gettext("%s: %s cannot create\n"),
				    myname, argv[c]);
				status++;
				continue;
			} else {
				(void) close(fd);
				if (stat(argv[c], &stbuf)) {
					(void) fprintf(stderr,
					    gettext("%s: %s cannot stat\n"),
					    myname, argv[c]);
					status++;
					continue;
				}
			}
		}

		if (mflag == 0)
			times.modtime = stbuf.st_mtime;
		else
			times.modtime = prstbuf.st_mtime;

		if (aflag == 0)
			times.actime = stbuf.st_atime;
		else
			times.actime = prstbuf.st_atime;

		if (utime(argv[c], (usecurrenttime) ? NULL :
		    (struct utimbuf *)(&times))) {
			(void) fprintf(stderr,
			    gettext("%s: cannot change times on %s\n"),
			    myname, argv[c]);
			status++;
			continue;
		}
	}
	return (status);
}

static int
isnumber(char *s)
{
	register c;

	while ((c = *s++) != '\0')
		if (!isdigit(c))
			return (0);
	return (1);
}


static time_t
parse_time(char *t)
{
	int		century = 0;
	int		seconds = 0;
	char		*p;
	time_t		when;
	struct tm	tm;

	/*
	 * time in the following format (defined by the touch(1) spec):
	 *	[[CC]YY]MMDDhhmm[.SS]
	 */
	if ((p = strchr(t, '.')) != NULL) {
		if (strchr(p+1, '.') != NULL)
			touchabort(BADTIME);
		seconds = atoi_for2(p+1);
		*p = '\0';
	}

	(void) memset(&tm, 0, sizeof (struct tm));
	when = time(0);
	tm.tm_year = localtime(&when)->tm_year;

	switch (strlen(t)) {
		case 12:	/* CCYYMMDDhhmm */
			century = atoi_for2(t);
			t += 2;
			/* FALLTHROUGH */
		case 10:	/* YYMMDDhhmm */
			tm.tm_year = atoi_for2(t);
			t += 2;
			if (century == 0) {
				if (tm.tm_year < 69)
					tm.tm_year += 100;
			} else
				tm.tm_year += (century - 19) * 100;
			/* FALLTHROUGH */
		case 8:		/* MMDDhhmm */
			tm.tm_mon = atoi_for2(t) - 1;
			t += 2;
			tm.tm_mday = atoi_for2(t);
			t += 2;
			tm.tm_hour = atoi_for2(t);
			t += 2;
			tm.tm_min = atoi_for2(t);
			tm.tm_sec = seconds;
			break;
		default:
			touchabort(BADTIME);
	}

	if ((when = mktime(&tm)) == -1)
		touchabort(BADTIME);
	if (tm.tm_isdst)
		when -= (timezone-altzone);
	return (when);
}

static time_t
parse_datetime(char *t)
{
	time_t		when;
	struct tm	tm;

	/*
	 * time in the following format (defined by the touch(1) spec):
	 *	MMDDhhmm[yy]
	 */

	(void) memset(&tm, 0, sizeof (struct tm));
	when = time(0);
	tm.tm_year = localtime(&when)->tm_year;

	switch (strlen(t)) {
		case 10:	/* MMDDhhmmyy */
			tm.tm_year = atoi_for2(t+8);
			if (tm.tm_year < 69)
				tm.tm_year += 100;
			/* FALLTHROUGH */
		case 8:		/* MMDDhhmm */
			tm.tm_mon = atoi_for2(t) - 1;
			t += 2;
			tm.tm_mday = atoi_for2(t);
			t += 2;
			tm.tm_hour = atoi_for2(t);
			t += 2;
			tm.tm_min = atoi_for2(t);
			break;
		default:
			touchabort(BADTIME);
	}

	if ((when = mktime(&tm)) == -1)
		touchabort(BADTIME);
	if (tm.tm_isdst)
		when -= (timezone-altzone);
	return (when);
}

static int
atoi_for2(char *p) {
	int value;

	value = (*p - '0') * 10 + *(p+1) - '0';
	if ((value < 0) || (value > 99))
		touchabort(BADTIME);
	return (value);
}

static void
touchabort(const char *message)
{
	(void) fprintf(stderr, "%s: %s\n", myname, gettext(message));
	exit(1);
}

static void
usage(const int settime)
{
	if (settime)
		(void) fprintf(stderr, gettext(
		    "usage: %s [-f file] [mmddhhmm[yy]] file...\n"),
		    myname);
	else
		(void) fprintf(stderr, gettext(
		    "usage: %s [-acm] [-r ref_file] file...\n"
		    "       %s [-acm] [MMDDhhmm[yy]] file...\n"
		    "       %s [-acm] [-t [[CC]YY]MMDDhhmm[.SS]] file...\n"),
		    myname, myname, myname);
	exit(2);
}
