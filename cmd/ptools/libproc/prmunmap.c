#ident	"@(#)prmunmap.c	1.1	94/11/10 SMI"

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include "pcontrol.h"

int	/* munmap() system call -- executed by subject process */
prmunmap(Pr, addr, len)
process_t *Pr;
caddr_t addr;
size_t len;
{
	struct sysret rval;		/* return value from munmap() */
	struct argdes argd[2];		/* arg descriptors for munmap() */
	register struct argdes *adp;

	adp = &argd[0];		/* addr argument */
	adp->value = (int)addr;
	adp->object = (char *)NULL;
	adp->type = AT_BYVAL;
	adp->inout = AI_INPUT;
	adp->len = 0;

	adp++;			/* len argument */
	adp->value = (int)len;
	adp->object = (char *)NULL;
	adp->type = AT_BYVAL;
	adp->inout = AI_INPUT;
	adp->len = 0;

	rval = Psyscall(Pr, SYS_munmap, 2, &argd[0]);

	if (rval.errno < 0)
		rval.errno = ENOSYS;

	if (rval.errno == 0)
		return rval.r0;
	errno = rval.errno;
	return -1;
}
