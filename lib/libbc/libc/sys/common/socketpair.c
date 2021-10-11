#include <sys/types.h>
#include <sys/socket.h>

#include <stdio.h>

extern int	errno;

int
socketpair(family, type, protocol, sv)
register int	family;
register int	type;
register int	protocol;
register int	sv[2];
{
	int	ret;
	static int map[] = {0, 2, 1, 4, 5, 6};
	if ((ret = _socketpair(family, map[type], protocol, sv)) == -1) {
		maperror(errno);
	}
	/* _socketpair returns sv[1] on success. 4.x expects a 0 */
	if (ret > 0)
		ret = 0;
	return (ret);
}
