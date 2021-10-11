#include <syscall.h>
#include <unistd.h>
#include <errno.h>
#include <sys/param.h>

extern int errno;

int creat_com(path, mode)
char	*path;
int	mode;
{
	int ret=0; 

	if (strcmp(path, "/etc/mtab") == 0 || strcmp(path, "/etc/fstab") == 0) {
		errno = ENOENT;
		return(-1);
	}
	if (strcmp(path, "/var/adm/utmp") == 0 || 
	    strcmp(path, "/var/adm/wtmp") == 0) {
		if ((ret = _syscall(SYS_creat, path, mode)) == -1) 
			return(-1);
		else {
			char buf[MAXPATHLEN+100];

                        strcpy(buf, path);
			strcat(buf, "x");
			if (_syscall(SYS_creat, buf, mode) == -1)
				return(-1);
		}
		return(ret);
	}
	else
		return(_syscall(SYS_creat, path, mode));
}
