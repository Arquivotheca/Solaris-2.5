#pragma ident	"@(#)mkdir.c	1.2	92/07/20 SMI" 

#include <sys/syscall.h>

mkdir(p, m)
    char           *p;
{
    return _syscall(SYS_mkdir, p, m);
}
