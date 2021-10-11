#include <syscall.h>
#include <sys/errno.h>

int unlink(path)
char *path;
{
	int ret;
	char buf[256];
	extern int errno;
	char dot[2] = {'.','\n'};

	if (path == (char *) 0) {
		errno = EFAULT;
		return(-1);
	} else if (strcmp(path, "") == 0)
		path = dot;

	if ((ret = _syscall(SYS_unlink, path)) == -1)
		return(-1);

	if (strcmp(path, "/var/adm/utmp") == 0 ||
		strcmp(path, "/var/adm/wtmp") == 0) {
		strcpy(buf, path);
		strcat(buf, "x");
		_syscall(SYS_unlink, buf);
	}
	return(ret);
}	
