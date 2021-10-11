#include <sys/types.h>
#include <sys/socket.h>

extern int	errno;

int	send(s, msg, len, flags)
int	s;
char	*msg;
int	len, flags;
{
	int	a;
	if ((a = _send(s, msg, len, flags)) == -1)
		maperror(errno);
	return(a);
}


int	sendto(s, msg, len, flags, to, tolen)
int	s;
char	*msg;
int	len, flags;
struct sockaddr *to;
int	tolen;
{
	int	a;
	if ((a = _sendto(s, msg, len, flags, to, tolen)) == -1)
		maperror(errno);
	return(a);
}


int	sendmsg(s, msg, flags)
int	s;
struct msghdr *msg;
int	flags;
{
	int	a;
	if ((a = _sendmsg(s, msg, flags)) == -1)
		maperror(errno);
	return(a);
}


