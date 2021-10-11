#include <syscall.h>


int dup(fd)
int fd;
{
	int ret, fds;

	if ((ret = _syscall(SYS_dup, fd)) == -1)
		return(-1);

	if ((fds = fd_get(fd)) != -1) 
		fd_add(ret, fds);

	return(ret);
}
