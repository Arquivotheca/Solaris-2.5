#ident	"@(#)propen.c	1.1	94/11/10 SMI"

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "pcontrol.h"

int	/* open() system call -- executed by subject process */
propen(Pr, filename, flags, mode)
process_t *Pr;
char * filename;
int flags;
int mode;
{
	struct sysret rval;		/* return value from open() */
	struct argdes argd[3];		/* arg descriptors for open() */
	register struct argdes *adp;

	adp = &argd[0];		/* filename argument */
	adp->value = 0;
	adp->object = (char *)filename;
	adp->type = AT_BYREF;
	adp->inout = AI_INPUT;
	adp->len = strlen(filename)+1;

	adp++;			/* flags argument */
	adp->value = (int)flags;
	adp->object = (char *)NULL;
	adp->type = AT_BYVAL;
	adp->inout = AI_INPUT;
	adp->len = 0;

	adp++;			/* mode argument */
	adp->value = (int)mode;
	adp->object = (char *)NULL;
	adp->type = AT_BYVAL;
	adp->inout = AI_INPUT;
	adp->len = 0;

	rval = Psyscall(Pr, SYS_open, 3, &argd[0]);

	if (rval.errno < 0)
		rval.errno = ENOSYS;

	if (rval.errno == 0)
		return rval.r0;
	errno = rval.errno;
	return -1;
}
