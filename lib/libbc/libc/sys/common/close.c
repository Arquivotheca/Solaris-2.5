#include <syscall.h>

close(fd)
int fd;
{
	return(bc_close(fd));
}

bc_close(fd)
int fd;
{
	int fds;

	if ((fds = fd_get(fd)) != -1)
		_syscall(SYS_close, fds);
	fd_rem(fd);
	return(_syscall(SYS_close, fd));
}	
