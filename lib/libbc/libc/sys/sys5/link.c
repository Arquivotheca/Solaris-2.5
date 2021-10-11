#include <sys/syscall.h>

link(a, b)
    char           *a;
    char           *b;
{
    return _syscall(SYS_link, a, b);
}
