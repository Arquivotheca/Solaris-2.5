#ident	"@(#)prsetitimer.c	1.1	94/11/10 SMI"

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "pcontrol.h"

int	/* setitimer() system call -- executed by subject process */
prsetitimer(Pr, which, itv, oitv)
process_t *Pr;
int which;
struct itimerval *itv;
struct itimerval *oitv;
{
	struct sysret rval;		/* return value from setitimer() */
	struct argdes argd[3];		/* arg descriptors for setitimer() */
	register struct argdes *adp;

	adp = &argd[0];		/* which argument */
	adp->value = which;
	adp->object = (char *)NULL;
	adp->type = AT_BYVAL;
	adp->inout = AI_INPUT;
	adp->len = 0;

	adp++;			/* itv argument */
	adp->value = (int)0;
	adp->object = (char *)itv;
	if (itv == NULL) {
		adp->type = AT_BYVAL;
		adp->inout = AI_INPUT;
		adp->len = 0;
	} else {
		adp->type = AT_BYREF;
		adp->inout = AI_INPUT;
		adp->len = sizeof(struct itimerval);
	}

	adp++;			/* oitv argument */
	adp->value = (int)0;
	adp->object = (char *)oitv;
	if (oitv == NULL) {
		adp->type = AT_BYVAL;
		adp->inout = AI_INPUT;
		adp->len = 0;
	} else {
		adp->type = AT_BYVAL;
		adp->inout = AI_OUTPUT;
		adp->len = sizeof(struct itimerval);
	}

	rval = Psyscall(Pr, SYS_setitimer, 3, &argd[0]);

	if (rval.errno < 0)
		rval.errno = ENOSYS;

	if (rval.errno == 0)
		return rval.r0;
	errno = rval.errno;
	return -1;
}
