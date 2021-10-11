#pragma ident	"@(#)chown.c	1.4	93/03/16 SMI" 

# include "chkpath.h"

chown(s, u, g)
    char           *s;
{
    CHKNULL(s);
    return _syscall(SYS_lchown, s, u, g);
}
