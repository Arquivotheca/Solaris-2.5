#pragma ident	"@(#)symlink.c	1.2	92/07/20 SMI" 

#include <sys/syscall.h>

symlink(t, f)
    char           *t, *f;
{
    return _syscall(SYS_symlink, t, f);
}
