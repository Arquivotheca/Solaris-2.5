#include <sys/types.h>
#include <sys/socket.h>

extern int	errno;

int	setpeername(s, name, namelen)
int	s;
struct sockaddr *name;
int	*namelen;
{
	int	a;
	if ((a = _setpeername(s, name, namelen)) == -1)
		maperror(errno);
	return(a);
}


