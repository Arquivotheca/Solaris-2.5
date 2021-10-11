/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1985 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
/* 	Portions Copyright(c) 1988, Sun Microsystems Inc.	*/
/*	All Rights Reserved					*/

#pragma ident	"@(#)usleep.c	1.2	95/03/02 SMI"	/* SVr4.0 1.1	*/

/*LINTLIBRARY*/
#include "synonyms.h"

#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

/*ARGSUSED*/
static void
sleepx(int signo)
{}

#define	USPS	1000000		/* number of microseconds in a second */

int
usleep(unsigned int n)
{
	struct itimerval itv, oitv;
	struct itimerval *itp = &itv;
	struct sigaction nact, oact;
	sigset_t amask, nset, oset;
	unsigned int alarm_time;

	if (n == 0)
		return (0);
#ifdef notdef
	/*
	 * Explicitly forbidden by XPG4.2 .. but 4.x allowed
	 * arbitrary sleep times.
	 */
	else if (n >= USPS) {
		errno = EINVAL;
		return (-1);
	}
#endif

	/*
	 * Disable the kernel timeout used by alarm(2)
	 *
	 * 4.x used the interval timer to implement sleep;
	 * XPG4.2 says the interaction is undefined.  However
	 * we want to avoid leaving processes hanging because
	 * they miss a SIGALRM from an alarm clock timeout.
	 *
	 * This approach is kludgey because we end up adding
	 * the usleep interval to the alarm interval.
	 *
	 * XXX	The only way we can really get 4.x semantics here
	 *	is for ualarm/usleep/sleep/alarm to *all* use
	 *	the same interval timers.  What a mess.
	 */
	alarm_time = alarm(0);

	/*
	 * Disable the interval timer
	 */
	timerclear(&itp->it_interval);
	timerclear(&itp->it_value);
	if (setitimer(ITIMER_REAL, itp, &oitv) < 0)
		return (-1);
	itp->it_value.tv_sec = n / USPS;
	itp->it_value.tv_usec = n % USPS;
	if (timerisset(&oitv.it_value)) {
		if (timercmp(&oitv.it_value, &itp->it_value, >)) {
			/*
			 * There's going to be time left over after the
			 * n microsecond sleep we're about to do.
			 */
			oitv.it_value.tv_sec -= itp->it_value.tv_sec;
			oitv.it_value.tv_usec -= itp->it_value.tv_usec;
			if (oitv.it_value.tv_usec < 0) {
				oitv.it_value.tv_usec += USPS;
				oitv.it_value.tv_sec--;
			}
		} else {
			/*
			 * We would've been woken early, so sleep that time
			 * instead.  Fake another SIGALRM when we're done.
			 */
			itp->it_value = oitv.it_value;
			oitv.it_value.tv_sec = 0;
			oitv.it_value.tv_usec = (2 * USPS) / CLK_TCK;
		}
	}

	/*
	 * Install a sigaction for SIGALRM to simply call the 'sleepx'
	 * handler.  No additional signals will be masked during signal
	 * delivery.
	 */
	nact.sa_handler = sleepx;
	nact.sa_flags = 0;
	(void) sigemptyset(&nact.sa_mask);
	(void) sigaction(SIGALRM, &nact, &oact);

	/* Block SIGALRM from being delivered to this lwp */

	(void) sigemptyset(&amask);
	(void) sigaddset(&amask, SIGALRM);
	(void) sigprocmask(SIG_BLOCK, &amask, &oset);
	nset = oset;
	(void) sigdelset(&nset, SIGALRM);

	/* start the interval timer .. */

	(void) setitimer(ITIMER_REAL, itp, (struct itimerval *)0);

	/* .. and suspend the lwp until we get a SIGALRM */

	(void) sigsuspend(&nset);

	/*
	 * restore the mask and sigaction associated with SIGALRM.
	 * (Though if the original signal mask was blocking SIGALRM, we
	 * mustn't unblock it here!)
	 */
	(void) sigaction(SIGALRM, &oact, (struct sigaction *)0);
	if (!sigismember(&oset, SIGALRM))
		(void) sigprocmask(SIG_UNBLOCK, &amask, (sigset_t *)0);

	/* restore the timer .. */
	(void) setitimer(ITIMER_REAL, &oitv, (struct itimerval *)0);

	if (alarm_time) {
		/* restore the pending alarm .. */
		(void) alarm(alarm_time);
	}

	return (0);
}
