#pragma ident	"@(#)chdir.c	1.4	92/07/20 SMI" 

#include <sys/syscall.h>

chdir(s)
    char           *s;
{
    return _syscall(SYS_chdir, s);
}
