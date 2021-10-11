/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)sleep.c	1.11	94/11/27 SMI"	/* SVr4.0 1.3	*/
/*
**	sleep -- suspend execution for an interval
**
**		sleep time
*/

#include	<stdio.h>
#include	<signal.h>
#include	<locale.h>
#include	<unistd.h>
#include        <limits.h>
void
main(argc, argv)
char **argv;
{
	unsigned long n;
	unsigned long leftover;
	int	c;
	char	*s;
	static void    catch_sig();

	n = 0;
	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);
	while ((c = getopt(argc, argv, "")) != -1)
		switch (c) {
		case '?':
		(void) fprintf(stderr, gettext("usage: sleep time\n"));
		(void) exit(2);
		}
	argc -= optind-1;
	argv += optind-1;
	if (argc < 2) {
		(void) fprintf(stderr, gettext("usage: sleep time\n"));
		(void) exit(2);
	}

	/*
	* XCU4: utility must terminate with zero exit status upon receiving
	* SIGALRM signal
	*/

	signal(SIGALRM, catch_sig);
	s = argv[1];
	while (c = *s++) {
		if (c < '0' || c > '9') {
			(void) fprintf(stderr,
				gettext("sleep: bad character in argument\n"));
			(void) exit(2);
		}
		n = n*10 + c - '0';
	}

	/*
	* to fix - sleep fails silently when on "long sleep" BUG: 1164064.
	* logic is to repeatedly sleep for unslept remaining time after sleep
	* of USHRT_MAX seconds, via reset and repeat call to sleep() 
	* library routine until there is none remaining time to sleep
	*/

	leftover = 0;
	if (n >= USHRT_MAX) {
		leftover = n - USHRT_MAX;
		sleep(USHRT_MAX);
        	while (leftover != 0) {
                	if (leftover >= USHRT_MAX) {
				leftover = sleep(USHRT_MAX);
				leftover -= USHRT_MAX;
			}
			else {
				leftover = sleep(leftover);
                		if (leftover)
                        		leftover -= leftover;
			}
		}
	} else
		sleep(n);
	(void) exit(0);
}


static void
catch_sig(sig)
int sig;
{
}
