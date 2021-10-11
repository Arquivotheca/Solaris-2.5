#pragma ident	"@(#)closedir.c	1.2	92/07/20 SMI" 

#include <sys/param.h>
#include <dirent.h>

/*
 * close a directory.
 */
int
closedir(dirp)
	register DIR *dirp;
{
	int fd;
	extern void free();
	extern int close();

	fd = dirp->dd_fd;
	dirp->dd_fd = -1;
	dirp->dd_loc = 0;
	free(dirp->dd_buf);
	free((char *)dirp);
	return (close(fd));
}
