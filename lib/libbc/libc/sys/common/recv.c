#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/errno.h>

extern int	errno;

#define N_AGAIN 11

int	recv(s, buf, len, flags)
int	s;
char	*buf;
int	len, flags;
{
	int	a;
	if ((a = _recv(s, buf, len, flags)) == -1) {
		if (errno == N_AGAIN)
			errno = EWOULDBLOCK;
		else
			maperror(errno);
	}
	return(a);
}


int	recvfrom(s, buf, len, flags, from, fromlen)
int	s;
char	*buf;
int	len, flags;
struct sockaddr *from;
int	*fromlen;
{
	int	a;
	if ((a = _recvfrom(s, buf, len, flags, from, fromlen)) == -1) {
		if (errno == N_AGAIN)
			errno = EWOULDBLOCK;
		else
			maperror(errno);
	}
	return(a);
}


int	recvmsg(s, msg, flags)
int	s;
struct msghdr *msg;
int	flags;
{
	int	a;
	if ((a = _recvmsg(s, msg, flags)) == -1) {
		if (errno == N_AGAIN)
			errno = EWOULDBLOCK;
		else
			maperror(errno);
	}
	return(a);
}


