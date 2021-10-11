#include <syscall.h>
#include <sys/types.h>

/* The following are from SVR4 sys/fcntl.h */

#define F_FREESP 11	/* Free file space */
#define F_WRLCK	 02	/* Write Lock */

/* lock structure from SVR4. */
struct fl {
	short l_type;
	short l_whence;
	off_t l_start;
	off_t l_len;
	long  l_sysid;
	pid_t l_pid;
	long  pad[4];
};

int ftruncate(fd, length)
int	fd;
off_t	length; 
{
	int fds;

	struct fl lck;

	lck.l_whence = 0;	/* offset l_start from beginning of file */
	lck.l_start = length;	
	lck.l_type = F_WRLCK;	/* setting a write lock */
	lck.l_len = 0L;

	if ((fds = fd_get(fd)) != -1)
		_syscall(SYS_fcntl, fds, F_FREESP, (int)&lck);		
	if (_syscall(SYS_fcntl, fd, F_FREESP, (int)&lck) == -1)
		return(-1);
	else 
		return(0);
}	
