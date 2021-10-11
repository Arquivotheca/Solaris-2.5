#pragma ident	"@(#)chdir.c	1.3	92/07/20 SMI" 

#include "chkpath.h"

chdir(s)
    char           *s;
{
    CHKNULL(s);
    return _syscall(SYS_chdir, s);
}
