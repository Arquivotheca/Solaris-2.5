/*
 * POSIX.1 compatible getgroups() routine
 * This is needed while gid_t is not the same size as int (or whatever the
 * syscall is using at the time).
 */

#pragma ident	"@(#)getgroups.c	1.4	92/07/20 SMI" 

#include <sys/types.h>
#include <sys/param.h>
#include <sys/syscall.h>

getgroups(gidsetsize, grouplist)
int	gidsetsize;
gid_t	grouplist[];
{
	int	glist[NGROUPS];	/* getgroups() syscall returns ints */
	register int	i;	/* loop control */
	register int	rc;	/* return code hold area */

	rc = _syscall(SYS_getgroups, gidsetsize, glist);
	if (rc > 0 && gidsetsize != 0)
		for (i = 0; i < rc; i++)
			grouplist[i] = (gid_t)glist[i];
	return (rc);
}
