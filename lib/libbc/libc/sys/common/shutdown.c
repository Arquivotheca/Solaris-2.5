#include <sys/errno.h>
#include <sys/types.h>
#include <sys/socket.h>

extern int	errno;
#define N_ENOTCONN 134

int
shutdown(s, how)
register int	s;
int	how;
{
	int	a;
	if ((a = _shutdown(s, how)) == -1) {
		if (errno == N_ENOTCONN) {
			errno = 0;
			a = 0;
		} else
			maperror(errno);
	}
	return(a);
}


