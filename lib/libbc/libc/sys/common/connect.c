#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>

/* SVR4 stream operation macros */
#define	STR		('S'<<8)
#define	I_SWROPT	(STR|023)
#define	SNDPIPE		0x002

static int set_sndpipe(int, int);

extern int	errno;

connect(s, name, namelen)
int	s;
struct sockaddr *name;
int	namelen;
{
	int	a;

	if ((a = set_sndpipe(s, 0)) == -1)
		maperror();
	else {
		if ((a = _connect(s, name, namelen)) == -1)
			maperror();
		(void) set_sndpipe(s, 1);
	}

	return (a);
}


static int
set_sndpipe(int s, int set)
{
	int ret;
	int save_errno = errno;

	do {
		ret = _ioctl(s, I_SWROPT, set ? SNDPIPE : 0);
	} while ((ret < 0) && (errno == EINTR));
	/* EINTR = 4 on 4.x and Solaris 2 */

	if ((ret < 0) && (errno != 9)) {	/* Solaris 2 EBADF */
		errno = save_errno;
		return (-1);
	}

	errno = save_errno;
	return (0);
}
