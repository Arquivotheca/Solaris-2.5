#pragma ident	"@(#)symlink.c	1.3	92/07/20 SMI" 

# include "chkpath.h"

symlink(t, f)
    char           *t, *f;
{
    CHKNULL(t);
    CHKNULL(f);
    return _syscall(SYS_symlink, t, f);
}
