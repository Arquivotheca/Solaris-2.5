#pragma ident	"@(#)lockf.c	1.12	92/07/20 SMI" 

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>

/*
 * convert lockf() into fcntl() for SystemV compatibility
 */

/* New SVR4 values */
#define SV_GETLK	5
#define SV_SETLK	6
#define SV_SETLKW	7

lockf(fildes, function, size)
	int fildes;
	int function;
	long size;
{
	struct flock ld;
	register int cmd;

	cmd = SV_SETLK;		/* assume non-blocking operation */
	ld.l_type = F_WRLCK;	/* lockf() only deals with exclusive locks */
	ld.l_whence = 1;	/* lock always starts at current position */
	if (size < 0) {
		ld.l_start = size;
		ld.l_len = -size;
	} else {
		ld.l_start = 0L;
		ld.l_len = size;
	}

	switch (function) {
	case F_TEST:
		if (_syscall(SYS_fcntl, fildes, SV_GETLK, &ld) != -1) {
			if (ld.l_type == F_UNLCK) {
				ld.l_pid = ld.l_xxx;	
					/* l_pid is the last field in the 
					   SVr3 flock structure */
				return (0);
			} else
				errno = EACCES;		/* EAGAIN ?? */
		}
		return (-1);

	default:
		errno = EINVAL;
		return (-1);

			/* the rest fall thru to the fcntl() at the end */
	case F_ULOCK:
		ld.l_type = F_UNLCK;
		break;

	case F_LOCK:
		cmd = SV_SETLKW;	/* block, if not available */
		break;

	case F_TLOCK:
		break;
	}
	if (_syscall(SYS_fcntl, fildes, cmd, &ld) == -1) {
		switch (errno) {
		/* this hack is purported to be for /usr/group compatibility */
		case ENOLCK:
			errno = EDEADLK;
		}
		return(-1);
	} else {
		ld.l_pid = ld.l_xxx;	/* l_pid is the last field in the 
					   SVr3 flock structure */
		return(0);
	}
}
