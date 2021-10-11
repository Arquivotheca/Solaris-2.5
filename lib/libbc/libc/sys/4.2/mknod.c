# include "chkpath.h"

mknod(p, m, d)
    char           *p;
    int m, d;
{
    CHKNULL(p);
    return _syscall(SYS_mknod, p, m, d);
}
