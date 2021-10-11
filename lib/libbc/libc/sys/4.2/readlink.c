#pragma ident	"@(#)readlink.c	1.3	92/07/20 SMI" 

# include "chkpath.h"

readlink(p, b, s)
    char           *p, *b;
{
    CHKNULL(p);
    return _syscall(SYS_readlink, p, b, s);
}
