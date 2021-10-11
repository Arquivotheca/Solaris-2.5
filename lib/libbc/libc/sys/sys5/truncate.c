#include <syscall.h>
#include <unistd.h>
#include <errno.h>
#include <sys/fcntl.h>
#include <sys/param.h>

extern int errno;

int truncate(path, length)
char	*path;
off_t	length;
{
	int fd, ret=0; 

	if (strcmp(path, "/etc/mtab") == 0 || strcmp(path, "/etc/fstab") == 0) {
		errno = ENOENT;
		return(-1);
	}
	if ((fd = open(path, O_WRONLY)) == -1) {
		return(-1);
	}
	
	if (ftruncate(fd, length) == -1) {
		close(fd);
		return(-1);
	}
	close(fd);
	return(0);
}
