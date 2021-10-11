#include <sys/time.h>
#include <sys/resource.h>

/*
 * getdtablesize is implemented on top of getrlimit's
 * RLIMIT_NOFILE feature. The current (Soft) limit is
 * returned.
 */

getdtablesize()
{
        int     nds;
        int     error;
        struct  rlimit  rip;

        error = getrlimit(RLIMIT_NOFILE, &rip);
        if ( error < 0 )
                return (-1);
        else
                return (rip.rlim_cur);
}
