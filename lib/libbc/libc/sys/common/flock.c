#include <sys/file.h>
#include <sys/fcntl.h>

int flock(fd, operation)
int fd, operation;
{
	struct flock fl;
	int cmd = F_SETLKW;

	fl.l_whence = 0;
	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_type = 0;
	if (operation & LOCK_UN)
		fl.l_type |= F_UNLCK;
	if (operation & LOCK_SH)
		fl.l_type |= F_RDLCK;
	if (operation & LOCK_EX)
		fl.l_type |= F_WRLCK;
	if (operation & LOCK_NB) 
		cmd = F_SETLK;
	return(bc_fcntl(fd, cmd, &fl));
}
