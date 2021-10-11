#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/syscall.h>


/* 
 * The following are the resource values for SVR4.
 * The resource values are mapped to SVR4 values
 * before invoking the system calls.
 */
int rlim_res[RLIM_NLIMITS] = {0, 1, 2, 3, 4, -1, 5};

int getrlimit(resource, rlp)
int resource;
struct rlimit *rlp;
{
	return(bc_getrlimit(resource, rlp));
}

int bc_getrlimit(resource, rlp)
int resource;
struct rlimit *rlp;
{
	return(_syscall(SYS_getrlimit, rlim_res[resource], rlp));
}

int setrlimit(resource, rlp)
int resource;
struct rlimit *rlp;
{
	return(bc_setrlimit(resource, rlp));
}

int bc_setrlimit(resource, rlp)
int resource;
struct rlimit *rlp;
{
	return(_syscall(SYS_setrlimit, rlim_res[resource], rlp));
}
