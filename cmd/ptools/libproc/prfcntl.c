#ident	"@(#)prfcntl.c	1.1	94/11/10 SMI"

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include "pcontrol.h"

int	/* fcntl() system call -- executed by subject process */
prfcntl(Pr, fd, cmd, flockp)
process_t *Pr;
int fd;
int cmd;
struct flock *flockp;
{
	struct sysret rval;		/* return value from fcntl() */
	struct argdes argd[3];		/* arg descriptors for fcntl() */
	register struct argdes *adp;

	if (Pr == NULL)
		return fcntl(fd, cmd, flockp);

	adp = &argd[0];		/* file descriptor argument */
	adp->value = fd;
	adp->object = (char *)NULL;
	adp->type = AT_BYVAL;
	adp->inout = AI_INPUT;
	adp->len = 0;

	adp++;			/* cmd argument */
	adp->value = cmd;
	adp->object = (char *)NULL;
	adp->type = AT_BYVAL;
	adp->inout = AI_INPUT;
	adp->len = 0;

	adp++;			/* flockp argument */
	adp->value = 0;
	adp->object = (char *)flockp;
	if (flockp == NULL) {
		adp->type = AT_BYVAL;
		adp->inout = AI_INPUT;
		adp->len = 0;
	} else {
		adp->type = AT_BYREF;
		adp->inout = AI_INOUT;
		adp->len = sizeof(*flockp);
	}

	rval = Psyscall(Pr, SYS_fcntl, 3, &argd[0]);

	if (rval.errno < 0)
		rval.errno = ENOSYS;

	if (rval.errno == 0)
		return rval.r0;
	errno = rval.errno;
	return -1;
}
