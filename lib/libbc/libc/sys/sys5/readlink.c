#pragma ident	"@(#)readlink.c	1.2	92/07/20 SMI" 

#include <sys/syscall.h>

readlink(p, b, s)
    char           *p, *b;
{
    return _syscall(SYS_readlink, p, b, s);
}
