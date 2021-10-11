#pragma ident	"@(#)telldir.c	1.2	92/07/20 SMI" 

#include <sys/param.h>
#include <dirent.h>

/*
 * return a pointer into a directory
 */
long
telldir(dirp)
	register DIR *dirp;
{
        return(dirp->dd_off);
}
