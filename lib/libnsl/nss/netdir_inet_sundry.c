/*
 * 	(c) 1991-1994  Sun Microsystems, Inc
 *	All rights reserved.
 *
 * lib/libnsl/nss/netdir_inet_sundry.c
 *
 * This file contains inet-specific implementations of netdir_options,
 * uaddr2taddr, and taddr2uaddr. These implementations
 * used to be in both tcpip.so and switch.so (identical copies).
 * Since we got rid of those, and also it's a good idea to build-in
 * inet-specific implementations in one place, we decided to put
 * them in this file with a not-so glorious name. These are INET-SPECIFIC
 * only, and will not be used for non-inet transports or by third-parties
 * that decide to provide their own nametoaddr libs for inet transports
 * (they are on their own for these as well => they get flexibility).
 *
 * Copied mostly from erstwhile lib/nametoaddr/tcpip/tcpip.c.
 */

#ident	"@(#)netdir_inet_sundry.c	1.5	94/09/21	SMI"

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <mtlib.h>
#include <thread.h>
#include <netconfig.h>
#include <netdir.h>
#include <nss_netdir.h>
#include <tiuser.h>
#include <sys/socket.h>
#include <net/if.h>
#include <sys/sockio.h>
#include <sys/fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <rpc/types.h>
#include <rpc/trace.h>
#include <sys/syslog.h>
#include <values.h>
#include <limits.h>
#ifdef DEBUG
#include <stdio.h>
#endif

#define	MAXIFS 32
#define	UDP "/dev/udp"

static char *inet_netdir_mergeaddr(struct netconfig *, char *, char *);

int
__inet_netdir_options(tp, opts, fd, par)
	struct netconfig *tp;
	int opts;
	int fd;
	char *par;
{
	struct nd_mergearg *ma;

	switch (opts) {
	case ND_SET_BROADCAST:
		/* Every one is allowed to broadcast without asking */
		return (ND_OK);
	case ND_SET_RESERVEDPORT:	/* bind to a resered port */
		return (bindresvport(fd, (struct netbuf *)par));
	case ND_CHECK_RESERVEDPORT:	/* check if reserved prot */
		return (checkresvport((struct netbuf *)par));
	case ND_MERGEADDR:	/* Merge two addresses */
		ma = (struct nd_mergearg *)(par);
		ma->m_uaddr = inet_netdir_mergeaddr(tp, ma->c_uaddr,
		    ma->s_uaddr);
		return (_nderror);
	default:
		return (ND_NOCTRL);
	}
}


/*
 * This routine will convert a TCP/IP internal format address
 * into a "universal" format address. In our case it prints out the
 * decimal dot equivalent. h1.h2.h3.h4.p1.p2 where h1-h4 are the host
 * address and p1-p2 are the port number.
 */
char *
__inet_taddr2uaddr(tp, addr)
	struct netconfig	*tp;	/* the transport provider */
	struct netbuf		*addr;	/* the netbuf struct */
{
	struct sockaddr_in	*sa;	/* our internal format */
	char			tmp[32];
	unsigned short		myport;

	if (!addr || !tp) {
		_nderror = ND_BADARG;
		return (NULL);
	}
	sa = (struct sockaddr_in *)(addr->buf);
	myport = ntohs(sa->sin_port);
	inet_ntoa_r(sa->sin_addr, tmp);
	sprintf(tmp + strlen(tmp), ".%d.%d", myport >> 8, myport & 255);
	return (strdup(tmp));	/* Doesn't return static data ! */
}

/*
 * This internal routine will convert one of those "universal" addresses
 * to the internal format used by the Sun TLI TCP/IP provider.
 */
struct netbuf *
__inet_uaddr2taddr(tp, addr)
	struct netconfig	*tp;	/* the transport provider */
	char			*addr;	/* the address */
{
	struct sockaddr_in	*sa;
	unsigned long		inaddr;
	unsigned short		inport;
	int			h1, h2, h3, h4, p1, p2;
	struct netbuf		*result;

	if (!addr || !tp) {
		_nderror = ND_BADARG;
		return ((struct netbuf *) 0);
	}
	result = (struct netbuf *) malloc(sizeof (struct netbuf));
	if (!result) {
		_nderror = ND_NOMEM;
		return ((struct netbuf *) 0);
	}

	sa = (struct sockaddr_in *)calloc(1, sizeof (*sa));
	if (!sa) {
		free((void *) result);
		_nderror = ND_NOMEM;
		return ((struct netbuf *) 0);
	}
	result->buf = (char *)(sa);
	result->maxlen = sizeof (struct sockaddr_in);
	result->len = sizeof (struct sockaddr_in);

	/* XXX there is probably a better way to do this. */
	if (sscanf(addr, "%d.%d.%d.%d.%d.%d", &h1, &h2, &h3, &h4,
	    &p1, &p2) != 6) {
		free((void *) result);
		_nderror = ND_NO_RECOVERY;
		return ((struct netbuf *) 0);
	}

	/* convert the host address first */
	inaddr = (h1 << 24) + (h2 << 16) + (h3 << 8) + h4;
	sa->sin_addr.s_addr = htonl(inaddr);

	/* convert the port */
	inport = (p1 << 8) + p2;
	sa->sin_port = htons(inport);

	sa->sin_family = AF_INET;

	return (result);
}

/*
 * Interface caching routines.  The cache is refreshed every
 * IF_CACHE_REFRESH_TIME seconds.  A read-write lock is used to
 * protect the cache.
 */
#define	IF_CACHE_REFRESH_TIME 10

static int if_cache_refresh_time = IF_CACHE_REFRESH_TIME;
static rwlock_t iflock = DEFAULTRWLOCK;
static time_t last_updated = 0;		/* protected by iflock */

typedef struct if_info_s {
	struct in_addr if_netmask;	/* netmask in network order */
	struct in_addr if_address;	/* address in network order */
	short if_flags;			/* interface flags */
} if_info_t;

static if_info_t *if_info = NULL;	/* if cache, protected by iflock */
static int n_ifs = 0;			/* number of cached interfaces */
static int numifs_last = 0;		/* number of interfaces last seen */

/*
 * Builds the interface cache.  Write lock on iflock is needed
 * for calling this routine.  It sets _nderror for error returns.
 * Returns TRUE if successful, FALSE otherwise.
 */
static bool_t
get_if_info()
{
	struct ifreq *buf;
	int numifs;
	int fd, i;
	struct ifconf ifc;
	struct ifreq *ifr;

	if ((fd = open(UDP, O_RDONLY)) < 0) {
		_nderror = ND_OPEN;
		return (FALSE);
	}
#ifdef SIOCGIFNUM
	if (ioctl(fd, SIOCGIFNUM, (char *)&numifs) < 0)
		numifs = MAXIFS;
#else
	numifs = MAXIFS;
#endif

	buf = (struct ifreq *) malloc(numifs * sizeof (struct ifreq));
	if (buf == NULL) {
		close(fd);
		_nderror = ND_NOMEM;
		return (FALSE);
	}

	if (if_info == NULL || numifs > numifs_last) {
		if (if_info != NULL)
			free((char *)if_info);
		if_info = (if_info_t *) malloc(numifs * sizeof (if_info_t));
		if (if_info == NULL) {
			close(fd);
			free((char *)buf);
			_nderror = ND_NOMEM;
			return (FALSE);
		}
		numifs_last = numifs;
	}

	ifc.ifc_len = numifs * sizeof (struct ifreq);
	ifc.ifc_buf = (char *)buf;
	if (ioctl(fd, SIOCGIFCONF, (char *)&ifc) < 0) {
		close(fd);
		free((char *)buf);
		free((char *)if_info);
		if_info = NULL;
		_nderror = ND_SYSTEM;
		return (FALSE);
	}
	numifs = ifc.ifc_len/sizeof (struct ifreq);

	n_ifs = 0;
	for (ifr = buf; ifr < (buf + numifs); ifr++) {
		if (ifr->ifr_addr.sa_family != AF_INET)
			continue;

		if_info[n_ifs].if_address =
			((struct sockaddr_in *)&ifr->ifr_addr)->sin_addr;

		if (ioctl(fd, SIOCGIFFLAGS, (char *)ifr) < 0)
			continue;

		if ((ifr->ifr_flags & IFF_UP) == 0)
			continue;
		if_info[n_ifs].if_flags = ifr->ifr_flags;

		if (ioctl(fd, SIOCGIFNETMASK, (char *)ifr) < 0)
			continue;

		if_info[n_ifs].if_netmask =
			((struct sockaddr_in *)&ifr->ifr_addr)->sin_addr;
		n_ifs++;
	}
	free((char *)buf);
	close(fd);
	return (TRUE);
}

/*
 * Given an IP address, find the interface address that has the best
 * prefix match.  Return the address in network order.
 */
static u_long
get_best_match(addr)
	struct in_addr addr;
{
	register if_info_t *bestmatch, *ifn;
	register int bestcount, count, limit;
	register u_long mask, netmask, clnt_addr, if_addr;
	register bool_t found, subnet_match;
	register int subnet_count;

	bestmatch = NULL;				/* no match yet */
	bestcount = BITSPERBYTE * sizeof (u_long);	/* worst match */
	clnt_addr = ntohl(addr.s_addr);			/* host order */

	subnet_match = FALSE;		/* subnet match not found yet */
	subnet_count = bestcount;	/* worst subnet match */

	for (ifn = if_info; ifn < (if_info + n_ifs); ifn++) {
		netmask = ntohl(ifn->if_netmask.s_addr);  /* host order */
		if_addr = ntohl(ifn->if_address.s_addr);  /* host order */

		/*
		 * set initial count to first bit set in netmask, with
		 * zero being the number of the least significant bit.
		 */
		for (count = 0, mask = netmask; mask && ((mask & 1) == 0);
						count++, mask >>= 1);

		/*
		 * Set limit so that we don't try to match prefixes shorter
		 * than the inherent netmask for the class (A, B, C, etc).
		 */
		if (IN_CLASSC(if_addr))
			limit = IN_CLASSC_NSHIFT;
		else if (IN_CLASSB(if_addr))
			limit = IN_CLASSB_NSHIFT;
		else if (IN_CLASSA(if_addr))
			limit = IN_CLASSA_NSHIFT;
		else
			limit = 0;

		/*
		 * We assume that the netmask consists of a contiguous
		 * sequence of 1-bits starting with the most significant bit.
		 * Prefix comparison starts at the subnet mask level.
		 * The prefix mask used for comparison is progressively
		 * reduced until it equals the inherent mask for the
		 * interface address class.  The algorithm finds an
		 * interface in the following order of preference:
		 *
		 * (1) the longest subnet match
		 * (2) the best partial subnet match
		 * (3) the first non-loopback interface
		 */
		found = FALSE;
		while (netmask && count < subnet_count) {
			if ((netmask & clnt_addr) == (netmask & if_addr)) {
				bestcount = count;
				bestmatch = ifn;
				found = TRUE;
				break;
			}
			netmask <<= 1;
			count++;
			if (count >= bestcount || count > limit || subnet_match)
				break;
		}
		/*
		 * If a subnet level match occurred, note this for
		 * comparison with future subnet matches.
		 */
		if (found && (netmask == ntohl(ifn->if_netmask.s_addr))) {
			subnet_match = TRUE;
			subnet_count = count;
		}
	}

	/*
	 * If we don't have a match, select the first interface that
	 * is not a loopback interface as the best match.
	 */
	if (bestmatch == NULL) {
		for (ifn = if_info; ifn < (if_info + n_ifs); ifn++) {
			if ((ifn->if_flags & IFF_LOOPBACK) == 0) {
				bestmatch = ifn;
				break;
			}
		}
	}

	if (bestmatch != NULL)
		return (bestmatch->if_address.s_addr);
	else
		return (0);
}


/*
 * This internal routine will merge one of those "universal" addresses
 * to the one which will make sense to the remote caller.
 */
static char *
inet_netdir_mergeaddr(tp, ruaddr, uaddr)
	struct netconfig	*tp;	/* the transport provider */
	char			*ruaddr; /* remote uaddr of the caller */
	char			*uaddr;	/* the address */
{
	char	tmp[SYS_NMLN], *cp;
	int	j;
	struct	in_addr clientaddr, bestmatch;
	time_t	curtime;

	if (!uaddr || !ruaddr || !tp) {
		_nderror = ND_BADARG;
		return ((char *)NULL);
	}
	if (strncmp(ruaddr, "0.0.0.0.", strlen("0.0.0.0.")) == 0)
		/* thats me: return the way it is */
		return (strdup(uaddr));

	/*
	 * Convert remote uaddr into an in_addr so that we can compare
	 * to it.  Shave off last two dotted-decimal values.
	 */
	for (cp = ruaddr, j = 0; j < 4; j++, cp++)
		cp = strchr(cp, '.');

	if (cp != NULL)
		*--cp = '\0';	/* null out the dot after the IP addr */

	clientaddr.s_addr = inet_addr(ruaddr);

#ifdef DEBUG
	printf("client's address is %s and %s\n",
		ruaddr, inet_ntoa(clientaddr));
#endif

	*cp = '.';	/* Put the dot back in the IP addr */

	(void) time(&curtime);
	if ((curtime - last_updated) >= if_cache_refresh_time) {
		/*
		 * Cache needs to be refreshed.
		 */
		(void) rw_wrlock(&iflock);
		/*
		 * Check again; some other thread may have beaten this
		 * one to it.
		 */
		(void) time(&curtime);
		if ((curtime - last_updated) >= if_cache_refresh_time) {
			if (!get_if_info()) {
				(void) rw_unlock(&iflock);
				return ((char *)NULL);
			}
			(void) time(&last_updated);
		}
		(void) rw_unlock(&iflock);
	}

	/*
	 * Find the best match now.
	 */
	(void) rw_rdlock(&iflock);
	bestmatch.s_addr = get_best_match(clientaddr);
	(void) rw_unlock(&iflock);

	if (bestmatch.s_addr)
		_nderror = ND_OK;
	else {
		_nderror = ND_NOHOST;
		return ((char *)NULL);
	}

	/* prepare the reply */
	memset(tmp, '\0', sizeof (tmp));

	/* reply consists of the IP addr of the closest interface */
	strcpy(tmp, inet_ntoa(bestmatch));

	/*
	 * ... and the port number part (last two dotted-decimal values)
	 * of uaddr
	 */
	for (cp = uaddr, j = 0; j < 4; j++, cp++)
		cp = strchr(cp, '.');
	strcat(tmp, --cp);

	return (strdup(tmp));
}

static mutex_t	port_lock = DEFAULTMUTEX;
static short	port;

static
bindresvport(fd, addr)
	int fd;
	struct netbuf *addr;
{
	int res;
	struct sockaddr_in myaddr;
	struct sockaddr_in *sin;
	int i;
	struct t_bind tbindstr, *tres;
	struct t_info tinfo;

#define	STARTPORT 600
#define	ENDPORT (IPPORT_RESERVED - 1)
#define	NPORTS	(ENDPORT - STARTPORT + 1)

	_nderror = ND_SYSTEM;
	if (geteuid()) {
		errno = EACCES;
		return (-1);
	}
	if ((i = t_getstate(fd)) != T_UNBND) {
		if (t_errno == TBADF)
			errno = EBADF;
		if (i != -1)
			errno = EISCONN;
		return (-1);
	}
	if (addr == NULL) {
		sin = &myaddr;
		(void) memset((char *)sin, 0, sizeof (*sin));
		sin->sin_family = AF_INET;
	} else {
		sin = (struct sockaddr_in *)addr->buf;
		if (sin->sin_family != AF_INET) {
			errno = EPFNOSUPPORT;
			return (-1);
		}
	}

	/* Transform sockaddr_in to netbuf */
	if (t_getinfo(fd, &tinfo) == -1)
		return (-1);
	tres = (struct t_bind *)t_alloc(fd, T_BIND, T_ADDR);
	if (tres == NULL) {
		_nderror = ND_NOMEM;
		return (-1);
	}

	tbindstr.qlen = 0; /* Always 0; user should change if he wants to */
	tbindstr.addr.buf = (char *)sin;
	tbindstr.addr.len = tbindstr.addr.maxlen = __rpc_get_a_size(tinfo.addr);
	sin = (struct sockaddr_in *)tbindstr.addr.buf;

	res = -1;
	mutex_lock(&port_lock);
	if (port == 0)
		port = (getpid() % NPORTS) + STARTPORT;
	for (i = 0; i < NPORTS; i++) {
		sin->sin_port = htons(port++);
		if (port > ENDPORT)
			port = STARTPORT;
		res = t_bind(fd, &tbindstr, tres);
		if (res == 0) {
			if ((tbindstr.addr.len == tres->addr.len) &&
			    (memcmp(tbindstr.addr.buf, tres->addr.buf,
			    (int)tres->addr.len) == 0))
				break;
			(void) t_unbind(fd);
		} else if (t_errno != TSYSERR || errno != EADDRINUSE)
			break;
	}
	mutex_unlock(&port_lock);

	(void) t_free((char *)tres, T_BIND);
	if (i != NPORTS) {
		_nderror = ND_OK;
	} else {
		_nderror = ND_FAILCTRL;
		res = 1;
	}
	return (res);
}

static
checkresvport(addr)
	struct netbuf *addr;
{
	struct sockaddr_in *sin;
	unsigned short port;

	if (addr == NULL) {
		_nderror = ND_FAILCTRL;
		return (-1);
	}
	sin = (struct sockaddr_in *)(addr->buf);
	port = ntohs(sin->sin_port);
	if (port < IPPORT_RESERVED)
		return (0);
	return (1);
}
