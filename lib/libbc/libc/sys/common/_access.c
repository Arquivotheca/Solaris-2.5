#include <syscall.h>
#include <unistd.h>
#include <sys/param.h>

int access_com(path, mode)
char	*path;
int	mode;
{
	int ret=0; 

	if (strcmp(path, "/etc/mtab") == 0 || strcmp(path, "/etc/fstab") == 0) {
		if (mode == W_OK || mode == X_OK)
			return(-1);
		else return(0);
	} else if (strcmp(path, "/var/adm/utmp") == 0 || 
	    strcmp(path, "/var/adm/wtmp") == 0) {
		if ((ret = _syscall(SYS_access, path, mode)) == -1) 
			return(-1);
		else {
			char buf[MAXPATHLEN+100];
			strcpy(buf, path);
			strcat(buf, "x");
			if (_syscall(SYS_access, buf, mode) == -1)
				return(-1);
		}
		return(ret);
	} else
		return(_syscall(SYS_access, path, mode));
}
