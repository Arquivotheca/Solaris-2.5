#include <sys/syscall.h>
#include <stdio.h>

extern int errno;

kill(pid, sig)
int pid, sig;
{
	return(bc_kill(pid, sig));
}


bc_kill(pid, sig)
int pid, sig;
{
	return(_kill(pid, maptonewsig(sig)));
}


