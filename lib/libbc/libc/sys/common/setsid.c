#include <sys/errno.h>


static int setsid_called = 0;
static int real_setsid_called=0;
static int setsid_val, setsid_errno;

extern int errno;

/* setpgrp system call number, setsid command code */
#define SYS_pgrpsys     39
#define SYS_setsid	3

setsid()
{
	if (setsid_called != getpid()) {
		setsid_called = getpid();
		return(bc_setsid());
	} else {
		errno = EPERM;
		return(-1);
	}
}
	


int
bc_setsid()
{
	if (real_setsid_called != getpid()) {
		real_setsid_called = getpid();
		setsid_val = _syscall(SYS_pgrpsys, SYS_setsid);
		setsid_errno = errno;
	}
	errno = setsid_errno;
	return(setsid_val);
}



