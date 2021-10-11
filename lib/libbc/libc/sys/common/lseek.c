#include "compat.h"
#include <errno.h>
#include <syscall.h>
#include <sys/types.h>
#include <unistd.h>

extern int errno;

off_t lseek(fd, offset, whence)
int fd;
off_t offset;
int whence;
{
	int off, ret, fds;

	if (whence <0 || whence > 2) {
		errno = EINVAL;
		return(-1);
	}
	if ((fds = fd_get(fd)) != -1) {
		off = getmodsize(offset, sizeof(struct utmp), 
						sizeof(struct compat_utmp));
		if ((ret = _syscall(SYS_lseek, fd, off, whence)) == -1)
			return(-1);
		off = getmodsize(offset, sizeof(struct utmp), 
						sizeof(struct utmpx));
		if (_syscall(SYS_lseek, fds, off, whence) == -1) {
			/* undo the previous lseek */
			_syscall(SYS_lseek, fd, ret, SEEK_SET);
			return(-1);
		}
		ret = getmodsize(ret, sizeof(struct compat_utmp),
					sizeof(struct utmp));
		return(ret);
	}
	else 
		return(_syscall(SYS_lseek, fd, offset, whence));
}
