#pragma ident	"@(#)gtty.c	1.11	92/07/21 SMI"
	  /* from UCB 4.1 83/07/04 */

/*
 * Writearound to old gtty system call.
 */

#include <sgtty.h>

gtty(fd, ap)
	struct sgttyb *ap;
{

	return(ioctl(fd, TIOCGETP, ap));
}
