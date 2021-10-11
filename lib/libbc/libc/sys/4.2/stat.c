#include "chkpath.h"
#include <sys/stat.h>

extern int errno;

int stat(path, buf)
char *path;
struct stat *buf;
{
	return(bc_stat(path, buf));
}


int bc_stat(path, buf)
char *path;
struct stat *buf;
{
	if ((path == (char*)-1) || (path == (char*)0)) {
		errno = EFAULT;
		return (-1);
	}
	if ((buf == (struct stat*)0) || (buf == (struct stat*)-1)) {
		errno = EFAULT;
		return (-1);
	}
	return(stat_com(SYS_stat, path, buf));
}


int lstat(path, buf)
char *path;
struct stat *buf;
{
	return(bc_lstat(path, buf));
}

int bc_lstat(path, buf)
char *path;
struct stat *buf;
{
	CHKNULL(path);
	return(stat_com(SYS_lstat, path, buf));
}

