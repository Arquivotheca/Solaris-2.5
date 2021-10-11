#pragma ident	"@(#)open.c	1.4	92/07/29 SMI" 
#include <sys/errno.h>

extern int errno;

open(path, flags, mode)
    char           *path;
    int            flags;
    int		   mode;
{
    return (bc_open(path, flags, mode));
}



bc_open(path, flags, mode)
    char           *path;
    int            flags;
    int		   mode;
{
    if ((path == (char*)0) || (path == (char*) -1)) {
	errno = EFAULT;
	return (-1);
    }
    return (open_com(path, flags, mode));
}

