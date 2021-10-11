#ident	"@(#)prsigaction.c	1.1	94/11/10 SMI"

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "pcontrol.h"

int	/* sigaction() system call -- executed by subject process */
prsigaction(Pr, sig, act, oact)
process_t *Pr;
int sig;
struct sigaction *act;
struct sigaction *oact;
{
	struct sysret rval;		/* return value from sigaction() */
	struct argdes argd[3];		/* arg descriptors for sigaction() */
	register struct argdes *adp;

	adp = &argd[0];		/* sig argument */
	adp->value = sig;
	adp->object = (char *)NULL;
	adp->type = AT_BYVAL;
	adp->inout = AI_INPUT;
	adp->len = 0;

	adp++;			/* act argument */
	adp->value = (int)0;
	adp->object = (char *)act;
	if (act == NULL) {
		adp->type = AT_BYVAL;
		adp->inout = AI_INPUT;
		adp->len = 0;
	} else {
		adp->type = AT_BYREF;
		adp->inout = AI_INPUT;
		adp->len = sizeof(struct sigaction);
	}

	adp++;			/* oact argument */
	adp->value = (int)0;
	adp->object = (char *)oact;
	if (oact == NULL) {
		adp->type = AT_BYVAL;
		adp->inout = AI_INPUT;
		adp->len = 0;
	} else {
		adp->type = AT_BYVAL;
		adp->inout = AI_OUTPUT;
		adp->len = sizeof(struct sigaction);
	}

	rval = Psyscall(Pr, SYS_sigaction, 3, &argd[0]);

	if (rval.errno < 0)
		rval.errno = ENOSYS;

	if (rval.errno == 0)
		return rval.r0;
	errno = rval.errno;
	return -1;
}
