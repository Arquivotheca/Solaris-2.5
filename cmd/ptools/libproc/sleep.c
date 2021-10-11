#ident	"@(#)sleep.c	1.2	95/06/26 SMI"

#include <stropts.h>
#include <poll.h>

void
msleep(msec)
	unsigned int msec;	/* milliseconds to sleep */
{
	struct pollfd pollfd;

	pollfd.fd = -1;
	pollfd.events = 0;
	pollfd.revents = 0;

	if (msec)
		(void) poll(&pollfd, 0UL, msec);
}

/* This is for the !?!*%! call to sleep() in execvp() */
unsigned int
sleep(sec)
	unsigned int sec;	/* seconds to sleep */
{
	msleep(sec*1000);
	return 0;
}
