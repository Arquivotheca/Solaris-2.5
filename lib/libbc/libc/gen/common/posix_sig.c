#ident	"@(#)posix_sig.c 1.1	93/05/04 SMI"	/* SVr4.0 1.28  */

/*
 * posix signal package
 */
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#define cantmask        (sigmask(SIGKILL)|sigmask(SIGSTOP))

extern errno;

/*
 * sigemptyset - all known signals
 */
sigemptyset(sigp)
    sigset_t *sigp;
{
    if (!sigp)
	return errno = EINVAL, -1;
    *sigp = 0;
    return 0;
}
    
/*
 * sigfillset - all known signals
 */
sigfillset(sigp)
    sigset_t *sigp;
{
    if (!sigp)
	return errno = EINVAL, -1;
    *sigp = sigmask(NSIG) - 1;
    return 0;
}

/*
 * add the signal to the set
 */
sigaddset(sigp,signo)	
    sigset_t* sigp;
{
    if (!sigp  ||  signo <= 0  ||  signo >= NSIG)
	return errno = EINVAL, -1;
    *sigp |= sigmask(signo);
    return 0;
}

/*
 * remove the signal from the set
 */
sigdelset(sigp,signo)
    sigset_t* sigp;
{
    if (!sigp  ||  signo <= 0  ||  signo >= NSIG)
	return errno = EINVAL, -1;
    *sigp &= ~sigmask(signo);
    return 0;
}

/*
 * return true if the signal is in the set (return is 0 or 1)
 */
sigismember(sigp,signo)
    sigset_t* sigp;
{
    if (!sigp  ||  signo <= 0  ||  signo >= NSIG)
	return errno = EINVAL, -1;
    return (*sigp & sigmask(signo)) != 0;
}
