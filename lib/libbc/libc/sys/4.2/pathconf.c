#pragma ident	"@(#)pathconf.c	1.7	92/07/20 SMI" 

#include "chkpath.h"

pathconf(p, what)
    char* p;
{
    CHKNULL(p);
    return _syscall(SYS_pathconf, p, what);
}
