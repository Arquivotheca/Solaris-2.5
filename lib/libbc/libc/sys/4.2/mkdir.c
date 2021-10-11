#pragma ident	"@(#)mkdir.c	1.3	92/07/20 SMI" 

# include "chkpath.h"

mkdir(p, m)
    char           *p;
{
    CHKNULL(p);
    return _syscall(SYS_mkdir, p, m);
}
