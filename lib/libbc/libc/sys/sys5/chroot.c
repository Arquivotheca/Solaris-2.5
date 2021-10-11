#pragma ident	"@(#)chroot.c	1.4	92/07/20 SMI" 

#include <sys/syscall.h>

chroot(d)
    char           *d;
{
    return _syscall(SYS_chroot, d);
}
