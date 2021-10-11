#include <sys/types.h>
#include <sys/socket.h>

extern int	errno;

int
socket(family, type, protocol)
register int	family;
register int	type;
register int	protocol;
{
	int	a;
	static int map[]={0,2,1,4,5,6};
	if ((a = _socket(family, map[type], protocol)) == -1)
		maperror(errno);
	return(a);
}


