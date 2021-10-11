#include <sys/types.h>
#include <sys/socket.h>
#include <sys/errno.h>

#define  N_AGAIN	11

extern int	errno;

accept(s, addr, addrlen)
int	s;
struct sockaddr *addr;
int	*addrlen;
{
	int	a;
	if ((a = _accept(s, addr, addrlen)) == -1) {
		if (errno == N_AGAIN)
			errno = EWOULDBLOCK;
		else	
			maperror(errno);
	}
	return(a);
}


