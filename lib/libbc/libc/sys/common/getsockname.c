#include <sys/types.h>
#include <sys/socket.h>

extern int	errno;

getsockname(s, name, namelen)
int	s;
struct sockaddr *name;
int	*namelen;
{
	int	a;
	if ((a = _getsockname(s, name, namelen)) == -1)
		maperror(errno);
	return(a);
}


