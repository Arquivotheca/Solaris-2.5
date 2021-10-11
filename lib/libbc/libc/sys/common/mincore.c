/* mincore.c  SMI 12/14/90  */
#include <errno.h>
#include <syscall.h>
#include <sys/types.h>
#include <unistd.h>

#define INCORE 1;	/* return only the incore status bit */

extern int errno;

int mincore(addr, len, vec)
caddr_t	addr;
int	len;
char	*vec;
{
	int i;

	if (len <0) {
		errno = EINVAL;
		return(-1);
	}
		
	if(_syscall(SYS_mincore, addr, len, vec) == 0) {
		for (i=0; i< len/getpagesize(); i++) {
			vec[i] &= INCORE;
		}
	}
}
