/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)acctwtmp.c	1.7	94/08/16 SMI"	/* SVr4.0 1.9	*/
/*
 *	acctwtmp reason >> /var/wtmp
 *	writes utmp.h record (with current time) to end of std. output
 *	acctwtmp `uname` >> /var/wtmp as part of startup
 *	acctwtmp pm >> /var/wtmp  (taken down for pm, for example)
 */
#include <stdio.h>
#include <sys/types.h>
#include "acctdef.h"
#include <utmp.h>

struct	utmp	wb;
char	*strncpy();

main(argc, argv)
char **argv;
{
	if(argc < 2) 
		fprintf(stderr, "Usage: %s reason [ >> %s ]\n",
			argv[0], WTMP_FILE), exit(1);

	strncpy(wb.ut_line, argv[1], sizeof(wb.ut_line));
	wb.ut_line[11] = NULL;
	wb.ut_type = ACCOUNTING;
	time(&wb.ut_time);
	fseek(stdout, 0L, 2);
	fwrite(&wb, sizeof(wb), 1, stdout);
	exit(0);
}
