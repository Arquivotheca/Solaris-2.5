#pragma ident	"@(#)pathconf.c	1.2	92/07/20 SMI" 

#include <sys/syscall.h>

pathconf(p, what)
    char* p;
{
    return _syscall(SYS_pathconf, p, what);
}
