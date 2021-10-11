#ident	"@(#)prmmap.c	1.1	94/11/10 SMI"

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include "pcontrol.h"

caddr_t	/* mmap() system call -- executed by subject process */
prmmap(Pr, addr, len, prot, flags, fd, off)
process_t *Pr;
caddr_t addr;
size_t len;
int prot;
int flags;
int fd;
off_t off;
{
	struct sysret rval;		/* return value from mmap() */
	struct argdes argd[6];		/* arg descriptors for mmap() */
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

	adp++;			/* prot argument */
	adp->value = (int)prot;
	adp->object = (char *)NULL;
	adp->type = AT_BYVAL;
	adp->inout = AI_INPUT;
	adp->len = 0;

	adp++;			/* flags argument */
	adp->value = (int)(_MAP_NEW|flags);
	adp->object = (char *)NULL;
	adp->type = AT_BYVAL;
	adp->inout = AI_INPUT;
	adp->len = 0;

	adp++;			/* fd argument */
	adp->value = (int)fd;
	adp->object = (char *)NULL;
	adp->type = AT_BYVAL;
	adp->inout = AI_INPUT;
	adp->len = 0;

	adp++;			/* off argument */
	adp->value = (int)off;
	adp->object = (char *)NULL;
	adp->type = AT_BYVAL;
	adp->inout = AI_INPUT;
	adp->len = 0;

	rval = Psyscall(Pr, SYS_mmap, 6, &argd[0]);

	if (rval.errno < 0)
		rval.errno = ENOSYS;

	if (rval.errno == 0)
		return (caddr_t) rval.r0;
	errno = rval.errno;
	return (caddr_t)(-1);
}
