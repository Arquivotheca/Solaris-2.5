#pragma ident	"@(#)rmdir.c	1.2	92/07/20 SMI" 

#include <sys/syscall.h>

rmdir(d)
    char           *d;
{
	int ret;
	extern errno;

	return(_syscall(SYS_rmdir, d));
}
