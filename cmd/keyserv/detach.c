/*
 *	detach.c
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)detach.c	1.8	93/03/05 SMI"

#include <sys/termios.h>
#include <fcntl.h>

/*
 * detach from tty
 */
detachfromtty()
{
	int tt;

	close(0);
	close(1);
	close(2);
	switch (fork()) {
	case -1:
		perror("fork");
		break;
	case 0:
		break;
	default:
		exit(0);
	}

	/* become session leader, and disassociate from controlling tty */
	(void) setsid();

	(void) open("/dev/null", O_RDWR, 0);
	dup(0);
	dup(0);
}
