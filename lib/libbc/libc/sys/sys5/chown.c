#pragma ident	"@(#)chown.c	1.5	93/03/16 SMI" 

#include <sys/syscall.h>

chown(s, u, g)
    char           *s;
{
    return _syscall(SYS_lchown, s, u, g);
}
