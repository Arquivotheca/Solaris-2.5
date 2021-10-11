#include <sys/types.h>
#include <sys/socket.h>

extern int	errno;

getpeername(s, name, namelen)
int	s;
struct sockaddr *name;
int	*namelen;
{
	int	a;
	if ((a = _getpeername(s, name, namelen)) == -1)
		maperror(errno);
	return(a);
}


