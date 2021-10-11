#include <sys/types.h>
#include <sys/socket.h>

extern int	errno;

getsockopt(s, level, optname, optval, optlen)
register int	s;
register int	level;
register int	optname;
register char	*optval;
register int	*optlen;
{
	int	a;
	if ((a = _getsockopt(s, level, optname, optval, optlen)) == -1)
		maperror(errno);
	return(a);
}


