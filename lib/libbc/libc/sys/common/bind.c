#include <sys/errno.h>
#include <sys/types.h>
#include <sys/socket.h>

extern int	errno;

#define SV_EEXIST   17

bind(s, name, namelen)
int	s;
struct sockaddr *name;
int	namelen;
{
	int	a;
	if ((a = _bind(s, name, namelen)) !=0) {
		if (errno == SV_EEXIST)
			errno = EADDRINUSE;
		else
			maperror(errno);
	}
	return(a);
}


