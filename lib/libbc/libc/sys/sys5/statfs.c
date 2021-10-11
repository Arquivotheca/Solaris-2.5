#pragma ident	"@(#)statfs.c	1.2	92/07/20 SMI" 

#include <sys/types.h>
#include <sys/vfs.h>

statfs(s, b)
    char            *s;
    struct statfs *b;
{
	return(statfs_com(s, b));		
}
