#pragma ident	"@(#)chmod.c	1.3	92/07/20 SMI" 

# include "chkpath.h"

chmod(s, m)
    char           *s;
{
    CHKNULL(s);
    return _syscall(SYS_chmod, s, m);
}
