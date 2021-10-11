#pragma ident	"@(#)mknod.c	1.2	92/07/20 SMI" 

/*
 * If we're asked to make a directory, do a "mkdir" instead, so we meet
 * the letter of the SVID (yuk!).
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

extern int _mknod();
extern int mkdir();

int
mknod(path, mode, dev)
	char *path;
	int mode;
	int dev;
{
	if ((mode & S_IFMT) == S_IFDIR)
		if (geteuid()) {
			errno = EPERM;
			return(-1);
		} else
			return (mkdir(path, mode & 07777));
	else
		return (_mknod(path, mode, dev));
}
