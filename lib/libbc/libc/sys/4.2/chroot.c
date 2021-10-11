#pragma ident	"@(#)chroot.c	1.3	92/07/20 SMI" 

# include "chkpath.h"

chroot(d)
    char           *d;
{
    CHKNULL(d);
    return _syscall(SYS_chroot, d);
}
