#pragma ident	"@(#)link.c	1.3	92/07/20 SMI" 

# include "chkpath.h"

link(a, b)
    char           *a;
    char           *b;
{
    CHKNULL(a);
    CHKNULL(b);
    return _syscall(SYS_link, a, b);
}
