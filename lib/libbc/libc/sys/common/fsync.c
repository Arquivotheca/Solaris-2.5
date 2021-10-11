#include <syscall.h>


int fsync(fd)
int fd;
{
	int ret, fds;

	if ((ret = _syscall(SYS_fsync, fd)) == -1)
		return(-1);

	if ((fds = fd_get(fd)) != -1) 
		_syscall(SYS_fsync, fds);

	return(ret);
}
