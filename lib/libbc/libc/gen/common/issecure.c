#pragma ident	"@(#)issecure.c	1.2	92/07/20 SMI"  /* c2 secure */

#include <sys/file.h>

#define PWDADJ	"/etc/security/passwd.adjunct"

/*
 * Is this a secure system ?
 */
issecure()
{
	static int	securestate	= -1;

	if (securestate == -1)
		securestate = (access(PWDADJ, F_OK) == 0);
	return(securestate);
}
