#ident	"@(#)prfstat.c	1.1	94/11/10 SMI"

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "pcontrol.h"

int	/* fstat() system call -- executed by subject process */
prfstat(Pr, fd, buf)
	process_t *Pr;
	int fd;
	struct stat * buf;
{
	struct sysret rval;		/* return value from fstat() */
	struct argdes argd[3];		/* arg descriptors for fstat() */
	register struct argdes *adp = &argd[0];	/* first argument */
	register int syscall;		/* which syscall, fstat or fxstat */
	register int nargs;		/* number of actual arguments */

	if (Pr == (process_t *)NULL)	/* no subject process */
		return fstat(fd, buf);

/*
 * This is filthy, but /proc reveals everything about the
 * system call interfaces, despite what the architects of the
 * header files may desire.  We have to know here whether we
 * are calling the old or new fstat(2) syscall in the subject.
 */
#ifdef _STYPES		/* old version of fstat(2) */
	syscall = SYS_fstat;
	nargs = 2;
#elif defined(_STAT_VER) || defined(STAT_VER)
	/* new version of fstat(2) */
	syscall = SYS_fxstat;
	nargs = 3;
# ifdef _STAT_VER	/* k18.2 */
	adp->value = _STAT_VER;
# else
	adp->value = STAT_VER;
# endif
	adp->object = (char *)NULL;
	adp->type = AT_BYVAL;
	adp->inout = AI_INPUT;
	adp->len = 0;
	adp++;			/* move to fd argument */
#else			/* newest version of fstat(2) */
	syscall = SYS_fstat;
	nargs = 2;
#endif

	adp->value = fd;
	adp->object = (char *)NULL;
	adp->type = AT_BYVAL;
	adp->inout = AI_INPUT;
	adp->len = 0;
	adp++;			/* move to buffer argument */

	adp->value = 0;
	adp->object = (char *)buf;
	adp->type = AT_BYREF;
	adp->inout = AI_OUTPUT;
	adp->len = sizeof(struct stat);

	rval = Psyscall(Pr, syscall, nargs, &argd[0]);

	if (rval.errno < 0)
		rval.errno = ENOSYS;

	if (rval.errno == 0)
		return 0;
	errno = rval.errno;
	return -1;
}
