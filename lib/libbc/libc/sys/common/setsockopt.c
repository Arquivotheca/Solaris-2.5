#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* multicast setsockopts */
#define	SUNOS4X_IP_MULTICAST_IF		2
#define	SUNOS4X_IP_MULTICAST_TTL	3
#define	SUNOS4X_IP_MULTICAST_LOOP	4
#define	SUNOS4X_IP_ADD_MEMBERSHIP	5
#define	SUNOS4X_IP_DROP_MEMBERSHIP	6
#define	SUNOS5X_IP_MULTICAST_IF		0x10
#define	SUNOS5X_IP_MULTICAST_TTL	0x11
#define	SUNOS5X_IP_MULTICAST_LOOP	0x12
#define	SUNOS5X_IP_ADD_MEMBERSHIP	0x13
#define	SUNOS5X_IP_DROP_MEMBERSHIP	0x14

extern int	errno;

setsockopt(s, level, optname, optval, optlen)
register int	s;
register int	level;
register int	optname;
register char	*optval;
register int	optlen;
{
	int	a;

	if (level == SOL_SOCKET)
		switch (optname) {
		case SO_DONTLINGER: {
			struct linger ling;
			ling.l_onoff = 0;
			if ((a = _setsockopt(s, level, SO_LINGER, &ling,
			    sizeof (struct linger))) == -1)
				maperror(errno);
			return (a);
		}

		case SO_LINGER:
			if  (optlen == sizeof (int)) {
				struct linger ling;
				ling.l_onoff = 1;
				ling.l_linger = (int)*optval;
				if ((a = _setsockopt(s, level, SO_LINGER, &ling,
				    sizeof (struct linger))) == -1)
					maperror(errno);
				return (a);
			}
		case SO_DEBUG:
		case SO_KEEPALIVE:
		case SO_DONTROUTE:
		case SO_USELOOPBACK:
		case SO_REUSEADDR:
			if (!optval) {
				int val = 1;
				if ((a = _setsockopt(s, level, optname, &val,
				    sizeof (int))) == -1)
					maperror(errno);
				return (a);
			}
		}
	if (level == IPPROTO_IP)
		switch (optname) {
		case SUNOS4X_IP_MULTICAST_IF:
			optname = SUNOS5X_IP_MULTICAST_IF;
			break;

		case SUNOS4X_IP_MULTICAST_TTL:
			optname = SUNOS5X_IP_MULTICAST_TTL;
			break;

		case SUNOS4X_IP_MULTICAST_LOOP:
			optname = SUNOS5X_IP_MULTICAST_LOOP;
			break;

		case SUNOS4X_IP_ADD_MEMBERSHIP:
			optname = SUNOS5X_IP_ADD_MEMBERSHIP;
			break;

		case SUNOS4X_IP_DROP_MEMBERSHIP:
			optname = SUNOS5X_IP_DROP_MEMBERSHIP;
			break;
		}

	if ((a = _setsockopt(s, level, optname, optval, optlen)) == -1)
		maperror(errno);
	return(a);
}
