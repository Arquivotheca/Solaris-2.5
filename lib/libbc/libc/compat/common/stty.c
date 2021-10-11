#pragma ident	"@(#)stty.c	1.11	92/07/21 SMI" 
	 /* from UCB 4.2 83/07/04 */

/*
 * Writearound to old stty system call.
 */

#include <sgtty.h>

stty(fd, ap)
	struct sgttyb *ap;
{

	return(ioctl(fd, TIOCSETP, ap));
}
