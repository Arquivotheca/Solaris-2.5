/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)closewtmp.c	1.5	92/07/14 SMI"	/* SVr4.0 1.2	*/

/*	fudge an entry to wtmp for each user who is still logged on when
 *	acct is being run. This entry marks a DEAD_PROCESS, and the
 *	current time as time stamp. This should be done before connect
 *	time is processed. Called by runacct.
 */

#include <stdio.h>
#include <sys/types.h>
#include <utmp.h>

main(argc, argv)
int argc;
char **argv;
{
	struct utmp *getutent(), *utmp;
	FILE *fp;

	fp = fopen(WTMP_FILE, "a+");
	if (fp == NULL) {
		fprintf(stderr, "%s: could not open %s: ", argv[0], WTMP_FILE);
		perror(NULL);
		exit(1);
	}

	while ((utmp=getutent()) != NULL) {
		if (utmp->ut_type == USER_PROCESS) {
			utmp->ut_type = DEAD_PROCESS;
			time( &utmp->ut_time );
			fwrite( utmp, sizeof(*utmp), 1, fp);
		}
	}
	fclose(fp);
	exit(0);
}
