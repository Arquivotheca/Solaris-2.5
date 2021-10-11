#pragma ident	"@(#)statfs.c	1.4	92/07/20 SMI" 

#include "chkpath.h"
#include <sys/types.h>
#include <sys/vfs.h>

statfs(s, b)
    char            *s;
    struct statfs *b;
{
	CHKNULL(s);

	return(statfs_com(s, b));
}

