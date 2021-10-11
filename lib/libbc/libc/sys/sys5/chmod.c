#pragma ident	"@(#)chmod.c	1.4	92/07/20 SMI" 

#include <sys/syscall.h>

chmod(s, m)
    char           *s;
{
    return _syscall(SYS_chmod, s, m);
}
