#pragma ident	"@(#)seekdir.c	1.4	93/12/26 SMI" 

#include <sys/param.h>
#include <dirent.h>

/*
 * seek to an entry in a directory.
 * Only values returned by "telldir" should be passed to seekdir.
 */
void
seekdir(dirp, tell)
	register DIR *dirp;
	register long tell;
{
	extern long lseek();
	long curloc;

	curloc = telldir(dirp);
	if (curloc == tell)
		return;
        dirp->dd_loc = 0;
        (void) lseek(dirp->dd_fd, tell, 0);
        dirp->dd_size = 0;
        dirp->dd_off = tell;
}

#undef	rewinddir

void
rewinddir(dirp)
	DIR *dirp;
{
	seekdir(dirp, 0);
}
