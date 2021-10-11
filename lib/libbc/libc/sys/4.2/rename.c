#include "chkpath.h"
#include <syscall.h>

int rename(path1, path2)
char *path1, *path2;
{
	int ret;
	char buf1[256];
	char buf2[256];

	CHKNULL(path1);
	CHKNULL(path2);

	if ((ret = _syscall(SYS_rename, path1, path2)) == -1)
		return(-1);

	if (strcmp(path1, "/var/adm/utmp") == 0 ||
		strcmp(path1, "/var/adm/wtmp") == 0) {
		strcpy(buf1, path1);
		strcat(buf1, "x");
		strcpy(buf2, path2);
		strcat(buf2, "x");
		_syscall(SYS_rename, buf1, buf2);
	}
	return(ret);
}	
