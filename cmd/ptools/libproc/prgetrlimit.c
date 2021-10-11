#ident	"@(#)prgetrlimit.c	1.1	94/11/10 SMI"

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/resource.h>
#include "pcontrol.h"

int	/* getrlimit() system call -- executed by subject process */
prgetrlimit(Pr, resource, rlp)
process_t *Pr;
int resource;
struct rlimit *rlp;
{
	struct sysret rval;		/* return value from getrlimit() */
	struct argdes argd[2];		/* arg descriptors for getrlimit() */
	register struct argdes *adp;

	if (Pr == NULL)
		return getrlimit(resource, rlp);

	adp = &argd[0];		/* resource argument */
	adp->value = resource;
	adp->object = (char *)NULL;
	adp->type = AT_BYVAL;
	adp->inout = AI_INPUT;
	adp->len = 0;

	adp++;			/* rlp argument */
	adp->value = 0;
	adp->object = (char *)rlp;
	adp->type = AT_BYREF;
	adp->inout = AI_OUTPUT;
	adp->len = sizeof(*rlp);

	rval = Psyscall(Pr, SYS_getrlimit, 2, &argd[0]);

	if (rval.errno < 0)
		rval.errno = ENOSYS;

	if (rval.errno == 0)
		return rval.r0;
	errno = rval.errno;
	return -1;
}
