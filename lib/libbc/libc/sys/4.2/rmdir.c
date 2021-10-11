#pragma ident	"@(#)rmdir.c	1.3	92/07/20 SMI" 

# include "chkpath.h"

rmdir(d)
    char           *d;
{
	int ret;
	extern errno;

	CHKNULL(d);
	ret = _syscall(SYS_rmdir, d);
	if (errno == EEXIST)
		errno = ENOTEMPTY;
	return ret;
}
