/*
 * 	(c) 1991-1994  Sun Microsystems, Inc
 *	All rights reserved.
 *
 * lib/libnsl/nss/netdir_inet.c
 *
 * This is where we have chosen to combine every useful bit of code for
 * all the Solaris frontends to lookup hosts, services, and netdir information
 * for inet family (udp, tcp) transports. gethostbyYY(), getservbyYY(), and
 * netdir_getbyYY() are all implemented on top of this code. Similarly,
 * netdir_options, taddr2uaddr, and uaddr2taddr for inet transports also
 * find a home here.
 *
 * If the netconfig structure supplied has NO nametoaddr libs (i.e. a "-"
 * in /etc/netconfig), this code calls the name service switch, and
 * therefore, /etc/nsswitch.conf is effectively the only place that
 * dictates hosts/serv lookup policy.
 * If an administrator chooses to bypass the name service switch by
 * specifying third party supplied nametoaddr libs in /etc/netconfig, this
 * implementation does NOT call the name service switch, it merely loops
 * through the nametoaddr libs. In this case, if this code was called
 * from gethost/servbyYY() we marshal the inet specific struct into
 * transport independent netbuf or hostserv, and unmarshal the resulting
 * nd_addrlist or hostservlist back into hostent and servent, as the case
 * may be.
 *
 * Goes without saying that most of the future bugs in gethost/servbyYY
 * and netdir_getbyYY are lurking somewhere here.
 */

#ident	"@(#)netdir_inet.c	1.14	95/08/21	SMI"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/byteorder.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <errno.h>
#include <fcntl.h>
#include <thread.h>
#include <synch.h>
#include <sys/utsname.h>
#include <netdb.h>
#include <netconfig.h>
#include <netdir.h>
#include <tiuser.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <nss_dbdefs.h>
#include <nss_netdir.h>
#include <rpc/trace.h>
#include <sys/syslog.h>

#define	GETHOSTBUF(host_buf)					\
	NSS_XbyY_ALLOC(&host_buf, sizeof (struct hostent), NSS_BUFLEN_HOSTS)
#define	GETSERVBUF(serv_buf)					\
	NSS_XbyY_ALLOC(&serv_buf, sizeof (struct servent), NSS_BUFLEN_SERVICES)

#ifdef PIC
#define	DOOR_GETHOSTBYNAME_R	_door_gethostbyname_r
#define	DOOR_GETHOSTBYADDR_R	_door_gethostbyaddr_r
#else
#define	DOOR_GETHOSTBYNAME_R	_switch_gethostbyname_r
#define	DOOR_GETHOSTBYADDR_R	_switch_gethostbyaddr_r
#endif PIC

#define	MAXBCAST	10

/*
 * constant values of addresses for HOST_SELF_BIND, HOST_SELF_CONNECT
 * and localhost.
 *
 * The following variables are static to the extent that they should
 * not be visible outside of this file. Watch out for sa_con which
 * is initialized programmatically to a const at only ONE place (we
 * can't initialize a union's non-first member); it should not
 * be modified elsewhere so as to keep things re-entrant.
 */
static char *localaddr[] = {"\000\000\000\000", NULL};
static char *connectaddr[] = {"\177\000\000\001", NULL};
static struct sockaddr_in sa_con;
static struct netbuf nd_conbuf = {sizeof (sa_con),\
    sizeof (sa_con), (char *)&sa_con};
static struct nd_addrlist nd_conaddrlist = {1, &nd_conbuf};
#define	LOCALHOST "localhost"

int str2hostent(const char *, int, void *, char *, int);
int str2servent(const char *, int, void *, char *, int);

static struct ifinfo *get_local_info(void);
struct hostent *_switch_gethostbyname_r(const char *,
			struct hostent *, char *, int, int *);
struct hostent *_switch_gethostbyaddr_r(const char *, int, int,
			struct hostent *, char *, int, int *);
struct hostent *_door_gethostbyname_r(const char *,
			struct hostent *, char *, int, int *);
struct hostent *_door_gethostbyaddr_r(const char *, int, int,
			struct hostent *, char *, int, int *);
struct servent *_switch_getservbyname_r(const char *,
			const char *, struct servent *, char *, int);
struct servent *_switch_getservbyport_r(int,
			const char *, struct servent *, char *, int);


int ndaddr2hent(const char *nam, struct nd_addrlist *nlist,
    struct hostent *result, char *buffer, int buflen);
int hsents2ndhostservs(struct hostent *he, struct servent *se,
	u_short port, struct nd_hostservlist **hslist);
int hent2ndaddr(char **haddrlist, int *servp,
    struct nd_addrlist **nd_alist);
int order_haddrlist(char **haddrlist, struct sockaddr_in **res_salist);
int ndaddr2srent(const char *name, const char *proto, u_short port,
    struct servent *result, char *buffer, int buflen);
int ndhostserv2hent(struct netbuf *nbuf, struct nd_hostservlist *addrs,
    struct hostent *result, char *buffer, int buflen);
int ndhostserv2srent(int port, const char *proto, struct nd_hostservlist *addrs,
    struct servent *result, char *buffer, int buflen);
static int nd2herrno(int nerr);

/*
 * Begin: PART I
 * Top Level Interfaces that gethost/serv/netdir funnel through.
 *
 */

/*
 * gethost/servbyname always call this function; if they call
 * with nametoaddr libs in nconf, we call netdir_getbyname
 * implementation: __classic_netdir_getbyname, otherwise nsswitch.
 *
 * netdir_getbyname calls this only if nametoaddr libs are NOT
 * specified for inet transports; i.e. it's supposed to follow
 * the name service switch.
 */
int
_get_hostserv_inetnetdir_byname(nconf, args, res)
	struct	netconfig *nconf;
	struct	nss_netdirbyname_in *args;
	union	nss_netdirbyname_out *res;
{
	int	server_port;
	int *servp = &server_port;
	char	**haddrlist;
	u_long	dotnameaddr;
	char	*dotnamelist[2];
	struct in_addr	inaddrs[MAXBCAST];
	char	*baddrlist[MAXBCAST + 1];

	if (nconf == 0)
		return (ND_BADARG);

	/*
	 * 1. gethostbyname()/netdir_getbyname() special cases:
	 */
	switch (args->op_t) {

		case NSS_HOST:
		/*
		 * Worth the performance gain -- assuming a lot of inet apps
		 * actively use "localhost".
		 */
		if (strcmp(args->arg.nss.host.name, LOCALHOST) == 0) {
			int	ret;

			IN_SET_LOOPBACK_ADDR(&sa_con);
			ret = ndaddr2hent(args->arg.nss.host.name,
			    &nd_conaddrlist, res->nss.host.hent,
			    args->arg.nss.host.buf,
			    args->arg.nss.host.buflen);
			if (ret != ND_OK)
				*(res->nss.host.herrno_p) = nd2herrno(ret);
			return (ret);
		}
		/*
		 * If the caller passed in a dot separated IP notation to
		 * gethostbyname, return that back as the address.
		 */
		if ((int)(dotnameaddr = inet_addr(args->arg.nss.host.name)) !=
		    -1) {
			int	ret;

			sa_con.sin_family = AF_INET;
			sa_con.sin_addr.s_addr = dotnameaddr;
			ret = ndaddr2hent(args->arg.nss.host.name,
			    &nd_conaddrlist, res->nss.host.hent,
			    args->arg.nss.host.buf,
			    args->arg.nss.host.buflen);
			if (ret != ND_OK)
				*(res->nss.host.herrno_p) = nd2herrno(ret);
			return (ret);
		}
		break;

		case NETDIR_BY:
		if (args->arg.nd_hs == 0)
			return (ND_BADARG);
		/*
		 * If servname is NULL, return 0 as the port number.
		 * If servname is rpcbind, return 111 as the port number.
		 * If servname is a number, return it back as the port number.
		 */
		if (args->arg.nd_hs->h_serv == 0) {
			*servp = htons(0);
		} else if (strcmp(args->arg.nd_hs->h_serv, "rpcbind") == 0) {
			*servp = htons(111);
		} else if (strspn(args->arg.nd_hs->h_serv, "0123456789")
			    == strlen(args->arg.nd_hs->h_serv)) {
			*servp = htons(atoi(args->arg.nd_hs->h_serv));
		} else {
			/* i.e. need to call a name service on this */
			servp = NULL;
		}

		/*
		 * If the hostname is HOST_SELF_BIND, we return 0.0.0.0 so the
		 * binding can be contacted through all interfaces.
		 * If the hostname is HOST_SELF_CONNECT, we return 127.0.0.1 so
		 * the address can be connected to locally.
		 * If the hostname is HOST_ANY, we return no addresses because
		 * IP doesn't know how to specify a service without a host.
		 * And finally if we specify HOST_BROADCAST then we ask a tli
		 * fd to tell us what the broadcast addresses are for any udp
		 * interfaces on this machine.
		 */
		if (args->arg.nd_hs->h_host == 0) {
			return (ND_NOHOST);
		} else if ((strcmp(args->arg.nd_hs->h_host,
			    HOST_SELF_BIND) == 0)) {
			haddrlist = localaddr;
		} else if ((strcmp(args->arg.nd_hs->h_host,
			    HOST_SELF_CONNECT) == 0)) {
			haddrlist = connectaddr;
		} else if ((strcmp(args->arg.nd_hs->h_host,
			    LOCALHOST) == 0)) {
			haddrlist = connectaddr;
		} else if ((int)(dotnameaddr =
		    inet_addr(args->arg.nd_hs->h_host)) != -1) {
			/*
			 * If the caller passed in a dot separated IP notation
			 * to netdir_getbyname, convert that back into address.
			 */

			dotnamelist[0] = (char *)&dotnameaddr;
			dotnamelist[1] = NULL;
			haddrlist = dotnamelist;
		} else if ((strcmp(args->arg.nd_hs->h_host,
				HOST_BROADCAST) == 0)) {
			int i, bnets;

			memset((char *)inaddrs, 0,
			    sizeof (struct in_addr) * MAXBCAST);
			bnets = getbroadcastnets(nconf, inaddrs);
			if (bnets == 0)
				return (ND_NOHOST);
			for (i = 0; i < bnets; i++)
				baddrlist[i] = (char *)&inaddrs[i];
			baddrlist[i] = NULL;
			haddrlist = baddrlist;
		} else {
			/* i.e. need to call a name service on this */
			haddrlist = 0;
		}

		if (haddrlist && servp)
			/*
			 * Convert h_addr_list into ordered nd_addrlist.
			 * malloc's will be done, freed using netdir_free.
			 */
			return (hent2ndaddr(haddrlist, servp, res->nd_alist));
		break;
	}

	/*
	 * 2. Most common scenario. This is the way we ship /etc/netconfig.
	 *    Emphasis on improving performance in the "if" part.
	 */
	if (nconf->nc_nlookups == 0) {
		struct hostent	*he;
		struct servent	*se;
		int	ret;
		nss_XbyY_buf_t	*ndbuf4switch = 0;

	switch (args->op_t) {

		case NSS_HOST:
		he = DOOR_GETHOSTBYNAME_R(args->arg.nss.host.name,
		    res->nss.host.hent, args->arg.nss.host.buf,
		    args->arg.nss.host.buflen,
		    res->nss.host.herrno_p);
		if (he == 0) {
			return (ND_NOHOST);
		} else {
			/*
			 * Order host addresses, in place, if need be.
			 */
			char	**t;
			int	num;

			haddrlist = res->nss.host.hent->h_addr_list;
			for (num = 0, t = haddrlist; *t; t++, num++);
			if (num == 1)
				return (ND_OK);

			ret = order_haddrlist(haddrlist, NULL);
			return (ret);
		}

		case NSS_SERV:

		se = _switch_getservbyname_r(args->arg.nss.serv.name,
		    args->arg.nss.serv.proto,
		    res->nss.serv, args->arg.nss.serv.buf,
		    args->arg.nss.serv.buflen);

		if (se == 0)
			return (ND_NOSERV);
		return (ND_OK);

		case NETDIR_BY:

		if (servp == 0) {
			char	*proto =
	    (strcmp(nconf->nc_proto, NC_TCP) == 0) ? NC_TCP : NC_UDP;

			/*
			 * We go through all this for just one port number,
			 * which is most often constant. How about linking in
			 * an indexed database of well-known ports in the name
			 * of performance ?
			 */
			GETSERVBUF(ndbuf4switch);
			if (ndbuf4switch == 0)
				return (ND_NOMEM);
			se = _switch_getservbyname_r(args->arg.nd_hs->h_serv,
			    proto, ndbuf4switch->result,
			    ndbuf4switch->buffer, ndbuf4switch->buflen);
			if (!se) {
				NSS_XbyY_FREE(&ndbuf4switch);
				return (ND_NOSERV);
			}
			server_port = se->s_port;
			NSS_XbyY_FREE(&ndbuf4switch);
		}

		if (haddrlist == 0) {
			int	dummy;

			GETHOSTBUF(ndbuf4switch);
			if (ndbuf4switch == 0)
				return (ND_NOMEM);
			he = DOOR_GETHOSTBYNAME_R(args->arg.nd_hs->h_host,
			    ndbuf4switch->result, ndbuf4switch->buffer,
			    ndbuf4switch->buflen, &dummy);
			if (!he) {
				NSS_XbyY_FREE(&ndbuf4switch);
				return (ND_NOHOST);
				/* XXX: Or, more specific err from dummy ??? */
			}
			/*
			 * Convert h_addr_list into ordered nd_addrlist.
			 * malloc's will be done, freed using netdir_free.
			 */
			ret = hent2ndaddr(
		    ((struct hostent *)(ndbuf4switch->result))->h_addr_list,
		    &server_port, res->nd_alist);

			NSS_XbyY_FREE(&ndbuf4switch);
			return (ret);
		} else {
			/*
			 * Convert h_addr_list into ordered nd_addrlist.
			 * malloc's will be done, freed using netdir_free.
			 */
			return (hent2ndaddr(haddrlist,
			    &server_port, res->nd_alist));
		}

		default: return (ND_BADARG); /* should never happen */
	}

	}

	/*
	 * 3. We come this far only if nametoaddr libs are specified for
	 *    inet transports and we are called by gethost/servbyname only.
	 */
	switch (args->op_t) {
		struct	nd_hostserv service;
		struct	nd_addrlist *addrs;
		int	ret;

		case NSS_HOST:

		service.h_host = (char *)args->arg.nss.host.name;
		service.h_serv = NULL;
		if ((ret = __classic_netdir_getbyname(nconf,
			    &service, &addrs)) != ND_OK) {
			*(res->nss.host.herrno_p) = nd2herrno(ret);
			return (ret);
		}
		/*
		 * convert addresses back into sockaddr for gethostbyname.
		 */
		ret = ndaddr2hent(service.h_host, addrs, res->nss.host.hent,
		    args->arg.nss.host.buf, args->arg.nss.host.buflen);
		if (ret != ND_OK)
			*(res->nss.host.herrno_p) = nd2herrno(ret);
		netdir_free((char *)addrs, ND_ADDRLIST);
		return (ret);

		case NSS_SERV:

		if (args->arg.nss.serv.proto == NULL) {
			/*
			 * A similar HACK showed up in Solaris 2.3.
			 * The caller wild-carded proto -- i.e. will
			 * accept a match using tcp or udp for the port
			 * number. Since we have no hope of getting
			 * directly to a name service switch backend
			 * from here that understands this semantics,
			 * we try calling the netdir interfaces first
			 * with "tcp" and then "udp".
			 */
			int	i;
			args->arg.nss.serv.proto = "tcp";
			i = _get_hostserv_inetnetdir_byname(nconf, args, res);
			if (i != ND_OK) {
				args->arg.nss.serv.proto = "udp";
				i = _get_hostserv_inetnetdir_byname(nconf,
				    args, res);
			}
			return (i);
		}

		/*
		 * Third-parties should optimize their nametoaddr
		 * libraries for the HOST_SELF case.
		 */
		service.h_host = HOST_SELF;
		service.h_serv = (char *)args->arg.nss.serv.name;
		if ((ret = __classic_netdir_getbyname(nconf,
			    &service, &addrs)) != ND_OK) {
			return (ret);
		}
		/*
		 * convert addresses back into servent for getservbyname.
		 */
		ret = ndaddr2srent(service.h_serv,
		    args->arg.nss.serv.proto,
		    ((struct sockaddr_in *)addrs->n_addrs->buf)->sin_port,
		    res->nss.serv,
		    args->arg.nss.serv.buf, args->arg.nss.serv.buflen);
		netdir_free((char *)addrs, ND_ADDRLIST);
		return (ret);

		default: return (ND_BADARG); /* should never happen */
	}
}

/*
 * gethostbyaddr/servbyport always call this function; if they call
 * with nametoaddr libs in nconf, we call netdir_getbyaddr
 * implementation __classic_netdir_getbyaddr, otherwise nsswitch.
 *
 * netdir_getbyaddr calls this only if nametoaddr libs are NOT
 * specified for inet transports; i.e. it's supposed to follow
 * the name service switch.
 */
int
_get_hostserv_inetnetdir_byaddr(nconf, args, res)
	struct	netconfig *nconf;
	struct	nss_netdirbyaddr_in *args;
	union	nss_netdirbyaddr_out *res;
{
	int	server_port;
	int *servp = &server_port;
	char	**haddrlist;
	struct in_addr	inaddrs[MAXBCAST];
	char	*baddrlist[MAXBCAST + 1];

	if (nconf == 0)
		return (ND_BADARG);

	/*
	 * 1. gethostbyaddr()/netdir_getbyaddr() special cases:
	 */
	switch (args->op_t) {

		case NSS_HOST:
		/*
		 * Worth the performance gain: assuming a lot of inet apps
		 * actively use "127.0.0.1".
		 */
		if (*(u_long *)(args->arg.nss.host.addr) == INADDR_LOOPBACK) {
			int	ret;

			IN_SET_LOOPBACK_ADDR(&sa_con);
			ret = ndaddr2hent(LOCALHOST,
			    &nd_conaddrlist, res->nss.host.hent,
			    args->arg.nss.host.buf,
			    args->arg.nss.host.buflen);
			if (ret != ND_OK)
				*(res->nss.host.herrno_p) = nd2herrno(ret);
			return (ret);
		}
		break;

		case NETDIR_BY:
		if (args->arg.nd_nbuf == 0)
			return (ND_BADARG);
		break;
	}

	/*
	 * 2. Most common scenario. This is the way we ship /etc/netconfig.
	 *    Emphasis on improving performance in the "if" part.
	 */
	if (nconf->nc_nlookups == 0) {
		struct hostent	*he;
		struct servent	*se;
		nss_XbyY_buf_t	*ndbuf4host = 0;
		nss_XbyY_buf_t	*ndbuf4serv = 0;
		char	*proto =
		    (strcmp(nconf->nc_proto, NC_TCP) == 0) ? NC_TCP : NC_UDP;
		struct	sockaddr_in *sa;
		int	dummy;

	switch (args->op_t) {

		case NSS_HOST:

		he = DOOR_GETHOSTBYADDR_R(args->arg.nss.host.addr,
		    args->arg.nss.host.len, args->arg.nss.host.type,
		    res->nss.host.hent, args->arg.nss.host.buf,
		    args->arg.nss.host.buflen,
		    res->nss.host.herrno_p);
		if (he == 0)
			return (ND_NOHOST);
		return (ND_OK);

		case NSS_SERV:

		se = _switch_getservbyport_r(args->arg.nss.serv.port,
		    args->arg.nss.serv.proto,
		    res->nss.serv, args->arg.nss.serv.buf,
		    args->arg.nss.serv.buflen);

		if (se == 0)
			return (ND_NOSERV);
		return (ND_OK);

		case NETDIR_BY:

		GETSERVBUF(ndbuf4serv);
		if (ndbuf4serv == 0)
			return (ND_NOMEM);
		sa = (struct sockaddr_in *)(args->arg.nd_nbuf->buf);
		se = _switch_getservbyport_r(sa->sin_port, proto,
		    ndbuf4serv->result, ndbuf4serv->buffer, ndbuf4serv->buflen);
		if (!se) {
			NSS_XbyY_FREE(&ndbuf4serv);
			/*
			 * We can live with this - i.e. the address does not
			 * belong to a well known service. The caller
			 * traditionally accepts a stringified port number
			 * as the service name. The state of se is used ahead
			 * to indicate the same.
			 * However, we do not tolerate this nonsense when we
			 * cannot get a host name. See below.
			 */
		}

		GETHOSTBUF(ndbuf4host);
		if (ndbuf4host == 0)
			return (ND_NOMEM);
		he = DOOR_GETHOSTBYADDR_R((char *)&(sa->sin_addr.s_addr),
		    4, sa->sin_family, ndbuf4host->result, ndbuf4host->buffer,
		    ndbuf4host->buflen, &dummy);
		if (!he) {
			NSS_XbyY_FREE(&ndbuf4host);
			if (ndbuf4serv)
			    NSS_XbyY_FREE(&ndbuf4serv);
			return (ND_NOHOST);
			/* XXX: Or, more specific err from dummy ??? */
		}
		/*
		 * Convert host names and service names into hostserv
		 * pairs. malloc's will be done, freed using netdir_free.
		 */
		dummy = hsents2ndhostservs(he, se,
		    sa->sin_port, res->nd_hslist);

		NSS_XbyY_FREE(&ndbuf4host);
		if (ndbuf4serv)
		    NSS_XbyY_FREE(&ndbuf4serv);
		return (dummy);

		default: return (ND_BADARG); /* should never happen */
	}

	}
	/*
	 * 3. We come this far only if nametoaddr libs are specified for
	 *    inet transports and we are called by gethost/servbyname only.
	 */
	switch (args->op_t) {
		struct	netbuf nbuf;
		struct	nd_hostservlist *addrs;
		struct	sockaddr_in sa;
		int	ret;

		case NSS_HOST:

		sa.sin_addr.s_addr = *(u_long *)args->arg.nss.host.addr;
		sa.sin_family = AF_INET;
		/* Hopefully, third-parties get this optimization */
		sa.sin_port = 0;
		nbuf.buf = (char *)&sa;
		nbuf.len = nbuf.maxlen = sizeof (sa);
		if ((ret = __classic_netdir_getbyaddr(nconf,
			    &addrs, &nbuf)) != 0) {
			*(res->nss.host.herrno_p) = nd2herrno(ret);
			return (ret);
		}
		/*
		 * convert the host-serv pairs into h_aliases and hent.
		 */
		ret = ndhostserv2hent(&nbuf, addrs, res->nss.host.hent,
		    args->arg.nss.host.buf, args->arg.nss.host.buflen);
		if (ret != ND_OK)
			*(res->nss.host.herrno_p) = nd2herrno(ret);
		netdir_free((char *)addrs, ND_HOSTSERVLIST);
		return (ret);

		case NSS_SERV:

		if (args->arg.nss.serv.proto == NULL) {
			/*
			 * A similar HACK showed up in Solaris 2.3.
			 * The caller wild-carded proto -- i.e. will
			 * accept a match on tcp or udp for the port
			 * number. Since we have no hope of getting
			 * directly to a name service switch backend
			 * from here that understands this semantics,
			 * we try calling the netdir interfaces first
			 * with "tcp" and then "udp".
			 */
			int	i;
			args->arg.nss.serv.proto = "tcp";
			i = _get_hostserv_inetnetdir_byaddr(nconf, args, res);
			if (i != ND_OK) {
				args->arg.nss.serv.proto = "udp";
				i = _get_hostserv_inetnetdir_byaddr(nconf,
				    args, res);
			}
			return (i);
		}

		/*
		 * Third-party nametoaddr_libs should be optimized for
		 * this case. It also gives a special semantics twist to
		 * netdir_getbyaddr. Only for the INADDR_ANY case, it gives
		 * higher priority to service lookups (over host lookups).
		 * If service lookup fails, the backend returns ND_NOSERV to
		 * facilitate lookup in the "next" naming service.
		 * BugId: 1075403.
		 */
		sa.sin_addr.s_addr = INADDR_ANY;
		sa.sin_family = AF_INET;
		sa.sin_port = (u_short)args->arg.nss.serv.port;
		sa.sin_zero[0] = '\0';
		nbuf.buf = (char *)&sa;
		nbuf.len = nbuf.maxlen = sizeof (sa);
		if ((ret = __classic_netdir_getbyaddr(nconf,
			    &addrs, &nbuf)) != ND_OK) {
			return (ret);
		}
		/*
		 * convert the host-serv pairs into s_aliases and servent.
		 */
		ret = ndhostserv2srent(args->arg.nss.serv.port,
		    args->arg.nss.serv.proto, addrs, res->nss.serv,
		    args->arg.nss.serv.buf, args->arg.nss.serv.buflen);
		netdir_free((char *)addrs, ND_HOSTSERVLIST);
		return (ret);

		default: return (ND_BADARG); /* should never happen */
	}
}

/*
 * Part II: Name Service Switch interfacing routines.
 */

static DEFINE_NSS_DB_ROOT(db_root_hosts);
static DEFINE_NSS_DB_ROOT(db_root_services);


/*
 * There is a copy of __nss2herrno() in nsswitch/files/gethostent.c.
 * It is there because /etc/lib/nss_files.so.1 cannot call
 * routines in libnsl.  Care should be taken to keep the two copies
 * in sync.
 */
int
__nss2herrno(nsstat)
	nss_status_t nsstat;
{
	switch (nsstat) {
	case NSS_SUCCESS:
		/* no macro-defined success code for h_errno */
		return (0);
	case NSS_NOTFOUND:
		return (HOST_NOT_FOUND);
	case NSS_TRYAGAIN:
		return (TRY_AGAIN);
	case NSS_UNAVAIL:
		return (NO_RECOVERY);
	}
}

nss_status_t
_herrno2nss(int h_errno)
{
	switch (h_errno) {
	case 0:
		return (NSS_SUCCESS);
	case TRY_AGAIN:
		return (NSS_TRYAGAIN);
	case NO_RECOVERY:
		return (NSS_UNAVAIL);
	case HOST_NOT_FOUND:
	case NO_DATA:
	default:
		return (NSS_NOTFOUND);
	}
}

static void
_nss_initf_hosts(p)
	nss_db_params_t	*p;
{
	trace1(TR__nss_initf_hosts, 0);
	p->name	= NSS_DBNAM_HOSTS;
	p->default_config = NSS_DEFCONF_HOSTS;
	trace1(TR__nss_initf_hosts, 1);
}


/*
 * The _switch_getXXbyYY_r() routines should be static.  They used to
 * be exported in SunOS 5.3, and in fact publicised as work-around
 * interfaces for getting CNAME/aliases, and therefore, we preserve
 * their signatures here. Just in case.
 */

struct hostent *
_switch_gethostbyname_r(name, result, buffer, buflen, h_errnop)
	const char	*name;
	struct hostent	*result;
	char		*buffer;
	int		buflen;
	int		*h_errnop;
{
	nss_XbyY_args_t arg;
	nss_status_t	res;

	trace2(TR__switch_gethostbyname_r, 0, buflen);
	NSS_XbyY_INIT(&arg, result, buffer, buflen, str2hostent);
	arg.key.name	= name;
	arg.stayopen	= 0;
	res = nss_search(&db_root_hosts, _nss_initf_hosts,
	    NSS_DBOP_HOSTS_BYNAME, &arg);
	arg.status = res;
	*h_errnop = arg.h_errno;
	trace2(TR__switch_gethostbyname_r, 1, buflen);
	return (struct hostent *) NSS_XbyY_FINI(&arg);
}

struct hostent *
_switch_gethostbyaddr_r(addr, len, type, result, buffer, buflen, h_errnop)
	const char	*addr;
	int		len;
	int		type;
	struct hostent	*result;
	char		*buffer;
	int		buflen;
	int		*h_errnop;
{
	nss_XbyY_args_t arg;
	nss_status_t	res;

	trace3(TR__switch_gethostbyaddr_r, 0, len, buflen);
	NSS_XbyY_INIT(&arg, result, buffer, buflen, str2hostent);
	arg.key.hostaddr.addr	= addr;
	arg.key.hostaddr.len	= len;
	arg.key.hostaddr.type	= type;
	arg.stayopen		= 0;
	res = nss_search(&db_root_hosts, _nss_initf_hosts,
	    NSS_DBOP_HOSTS_BYADDR, &arg);
	arg.status = res;
	*h_errnop = arg.h_errno;
	trace3(TR__switch_gethostbyaddr_r, 1, len, buflen);
	return (struct hostent *) NSS_XbyY_FINI(&arg);
}

static void
_nss_initf_services(p)
	nss_db_params_t	*p;
{
	/* === need tracepoints */
	p->name	= NSS_DBNAM_SERVICES;
	p->default_config = NSS_DEFCONF_SERVICES;
}

struct servent *
_switch_getservbyname_r(name, proto, result, buffer, buflen)
	const char	*name;
	const char	*proto;
	struct servent	*result;
	char		*buffer;
	int		buflen;
{
	nss_XbyY_args_t arg;
	nss_status_t	res;

	NSS_XbyY_INIT(&arg, result, buffer, buflen, str2servent);
	arg.key.serv.serv.name	= name;
	arg.key.serv.proto	= proto;
	arg.stayopen		= 0;
	res = nss_search(&db_root_services, _nss_initf_services,
	    NSS_DBOP_SERVICES_BYNAME, &arg);
	arg.status = res;
	return ((struct servent *) NSS_XbyY_FINI(&arg));
}

struct servent *
_switch_getservbyport_r(port, proto, result, buffer, buflen)
	int		port;
	const char	*proto;
	struct servent	*result;
	char		*buffer;
	int		buflen;
{
	nss_XbyY_args_t arg;
	nss_status_t	res;

	NSS_XbyY_INIT(&arg, result, buffer, buflen, str2servent);
	arg.key.serv.serv.port	= port;
	arg.key.serv.proto	= proto;
	arg.stayopen		= 0;
	res = nss_search(&db_root_services, _nss_initf_services,
	    NSS_DBOP_SERVICES_BYPORT, &arg);
	arg.status = res;
	return ((struct servent *) NSS_XbyY_FINI(&arg));
}


/*
 * Return values: 0 = success, 1 = parse error, 2 = erange ...
 * The structure pointer passed in is a structure in the caller's space
 * wherein the field pointers would be set to areas in the buffer if
 * need be. instring and buffer should be separate areas.
 *
 * Defined here because we need it and we (libnsl) cannot have a dependency
 * on libsocket (however, libsocket always depends on libnsl).
 */
int
str2servent(instr, lenstr, ent, buffer, buflen)
	const char	*instr;
	int		lenstr;
	void	*ent; /* really (struct servnet *) */
	char	*buffer;
	int	buflen;
{
	struct servent	*serv	= (struct servent *)ent;
	const char	*p, *fieldstart, *limit, *namestart;
	int		fieldlen, namelen = 0;
	char		numbuf[12];
	char		*numend;

	if ((instr >= buffer && (buffer + buflen) > instr) ||
	    (buffer >= instr && (instr + lenstr) > buffer)) {
		return (NSS_STR_PARSE_PARSE);
	}

	p = instr;
	limit = p + lenstr;

	while (p < limit && isspace(*p)) {
		p++;
	}
	namestart = p;
	while (p < limit && !isspace(*p)) {
		p++;		/* Skip over the canonical name */
	}
	namelen = p - namestart;

	if (buflen <= namelen) { /* not enough buffer */
		return (NSS_STR_PARSE_ERANGE);
	}
	(void) memcpy(buffer, namestart, namelen);
	buffer[namelen] = '\0';
	serv->s_name = buffer;

	while (p < limit && isspace(*p)) {
		p++;
	}

	fieldstart = p;
	do {
		if (p > limit || isspace(*p)) {
			/* Syntax error -- no port/proto */
			return (NSS_STR_PARSE_PARSE);
		}
	}
	while (*p++ != '/');
	fieldlen = p - fieldstart - 1;
	if (fieldlen == 0 || fieldlen >= sizeof (numbuf)) {
		/* Syntax error -- supposed number is empty or too long */
		return (NSS_STR_PARSE_PARSE);
	}
	(void) memcpy(numbuf, fieldstart, fieldlen);
	numbuf[fieldlen] = '\0';
	serv->s_port = htons(strtol(numbuf, &numend, 10));
	if (*numend != '\0') {
		/* Syntax error -- port number isn't a number */
		return (NSS_STR_PARSE_PARSE);
	}

	fieldstart = p;
	while (p < limit && !isspace(*p)) {
		p++;		/* Scan the protocol name */
	}
	fieldlen = p - fieldstart + 1;		/* Include '\0' this time */
	if (fieldlen > buflen - namelen - 1) {
		return (NSS_STR_PARSE_ERANGE);
	}
	serv->s_proto = buffer + namelen + 1;
	(void) memcpy(serv->s_proto, fieldstart, fieldlen - 1);
	serv->s_proto[fieldlen - 1] = '\0';

	while (p < limit && isspace(*p)) {
		p++;
	}
	/*
	 * Although nss_files_XY_all calls us with # stripped,
	 * we should be able to deal with it here in order to
	 * be more useful.
	 */
	if (p >= limit || *p == '#') { /* no aliases, no problem */
		char **ptr;

		ptr = (char **) ROUND_UP(buffer + namelen + 1 + fieldlen,
		    sizeof (char *));
		if ((char *)ptr >= buffer + buflen) {
			/* hope they don't try to peek in */
			serv->s_aliases = 0;
			return (NSS_STR_PARSE_ERANGE);
		} else {
			*ptr = 0;
			serv->s_aliases = ptr;
			return (NSS_STR_PARSE_SUCCESS);
		}
	}
	serv->s_aliases = _nss_netdb_aliases(p, lenstr - (p - instr),
	    buffer + namelen + 1 + fieldlen,
	    buflen - namelen - 1 - fieldlen);
	return (NSS_STR_PARSE_SUCCESS);
}

/*
 * Part III: All `n sundry routines that are useful only in this
 * module. In the interest of keeping this source file shorter,
 * we would create them a new module only if the linker allowed
 * "library-static" functions.
 *
 * Routines to order addresses based on local interfaces and netmasks,
 * to get and check reserved ports, and to get broadcast nets.
 */

struct ifinfo {
	struct in_addr addr, netmask;
};

/*
 * Move any local addresses toward the beginning of haddrlist,
 * preserving the ordering of the list otherwise.  In other words, do
 * a stable sort on "locality".
 *
 * Order haddrlist in place if res_salist is NULL, otherwise malloc it
 * under res_salist.  Caller is responsible for freeing this storage.
 */
int
order_haddrlist(haddrlist, res_salist)
	char	**haddrlist;
	struct	sockaddr_in	**res_salist;
{
	struct	ifinfo *localinfo;
	int	localif, num;
	char	**t;
	int	inplace = (res_salist == 0);

	for (num = 0, t = haddrlist; *t; t++, num++);

	if (inplace) {
		char	**addrs;	/* sorted haddrlist */
		int	i, j;

		if (num == 1)
			return (ND_OK);
		addrs = malloc(num * sizeof (addrs[0]));
		if (addrs == NULL)
			return (ND_NOMEM);
		localinfo = get_local_info();

		j = 0;	/* the next free slot in "addrs" */

		/* Copy locals. */
		for (i = 0; i < num; i++) {
			if (islocal(localinfo, haddrlist[i])) {
				addrs[j++] = haddrlist[i];
				haddrlist[i] = NULL;
			}
		}
		/* Copy nonlocals. */
		for (i = 0; i < num; i++) {
			if (haddrlist[i] != NULL) {
				addrs[j++] = haddrlist[i];
			}
		}
		memcpy(haddrlist, addrs, num * sizeof (addrs[0]));
		free(addrs);
	} else {
		struct	sockaddr_in *sa;

		sa = (struct sockaddr_in *) calloc(num,
		    sizeof (struct sockaddr_in));
		if (sa == 0)
			return (ND_NOMEM);

		*res_salist = sa;
		if (num == 1) {
			memcpy((char *)&(sa->sin_addr), *haddrlist,
			    sizeof (sa->sin_addr));
			return (ND_OK);
		}
		localinfo = get_local_info();

		/*
		 * copy over haddrs into sockaddr_in's - locals first.
		 */
		for (localif = 1; localif >= 0; localif--) {
			struct in_addr	ina;

			for (t = haddrlist; *t; t++) {
				memcpy(&ina, *t, sizeof (ina));
				/*
				 * first pass gets only local addresses,
				 * second gets only nonlocal addresses
				 */
				if ((localif && !islocal(localinfo, ina)) ||
				    (!localif && islocal(localinfo, ina)))
					continue;
				memcpy((char *)&(sa->sin_addr), &ina,
				    sizeof (sa->sin_addr));
				sa++;
			}
		}
	}
	if (localinfo)
	    free(localinfo);

	return (ND_OK);
}

/*
 * Given an haddrlist and a port number, mallocs and populates
 * a new nd_addrlist with ordered addresses.
 */
int
hent2ndaddr(haddrlist, servp, nd_alist)
	char	**haddrlist;
	int	*servp;
	struct	nd_addrlist **nd_alist;
{
	struct	nd_addrlist *result;
	int		num, ret, i;
	char	**t;
	struct	netbuf *na;
	struct	sockaddr_in *sa;

	result = (struct nd_addrlist *) malloc(sizeof (struct nd_addrlist));
	if (result == 0)
		return (ND_NOMEM);

	/* Address count */
	for (num = 0, t = haddrlist; *t; t++, num++);

	result->n_cnt = num;
	result->n_addrs = (struct netbuf *) calloc(num, sizeof (struct netbuf));
	if (result->n_addrs == 0) {
		free(result);
		return (ND_NOMEM);
	}
	na = result->n_addrs;

	ret = order_haddrlist(haddrlist, &sa);
	if (ret != 0) {
		free(result->n_addrs);
		free(result);
		return (ret);
	}

	for (i = 0; i < num; i++, na++, sa++) {
		na->len = na->maxlen = sizeof (struct sockaddr_in);
		na->buf = (char *)sa;
		sa->sin_family = AF_INET;
		sa->sin_port = *servp;
	}

	*(nd_alist) = result;
	return (ND_OK);
}

/*
 * Given a hostent and a servent, mallocs and populates
 * a new nd_hostservlist with host and service names.
 *
 * We could be passed in a NULL servent, in which case stringify port.
 */
int
hsents2ndhostservs(struct hostent *he, struct servent *se,
    u_short port, struct nd_hostservlist **hslist)
{
	struct	nd_hostservlist *result;
	struct	nd_hostserv *hs;
	int	hosts, servs, i, j;
	char	**hn, **sn;

	if ((result = (struct nd_hostservlist *)
		    malloc(sizeof (struct nd_hostservlist))) == 0)
		return (ND_NOMEM);

	/*
	 * We initialize the counters to 1 rather than zero because
	 * we have to count the "official" name as well as the aliases.
	 */
	for (hn = he->h_aliases, hosts = 1; hn && *hn; hn++, hosts++);
	if (se)
		for (sn = se->s_aliases, servs = 1; sn && *sn; sn++, servs++);
	else
		servs = 1;

	if ((hs = (struct nd_hostserv *)calloc(hosts * servs,
			sizeof (struct nd_hostserv))) == 0)
		return (ND_NOMEM);

	result->h_cnt	= servs * hosts;
	result->h_hostservs = hs;

	for (i = 0, hn = he->h_aliases; i < hosts; i++) {
		sn = se ? se->s_aliases : NULL;

		for (j = 0; j < servs; j++) {
			if (i == 0)
				hs->h_host = strdup(he->h_name);
			else
				hs->h_host = strdup(*hn);
			if (j == 0) {
				if (se)
					hs->h_serv = strdup(se->s_name);
				else {
					/* Convert to a number string */
					char stmp[16];

					sprintf(stmp, "%d", port);
					hs->h_serv = strdup(stmp);
				}
			} else
				hs->h_serv = strdup(*sn++);

			if ((hs->h_host == 0) || (hs->h_serv == 0)) {
				free((void *)result->h_hostservs);
				free((void *)result);
				return (ND_NOMEM);
			}
			hs++;
		}
		if (i)
			hn++;
	}
	*(hslist) = result;
	return (ND_OK);
}

/*
 * Process results from nd_addrlist ( returned by netdir_getbyname)
 * into a hostent using buf.
 * *** ASSUMES that nd_addrlist->n_addrs->buf contains IP addresses in
 * sockaddr_in's ***
 */
int
ndaddr2hent(nam, addrs, result, buffer, buflen)
	const	char *nam;
	struct	nd_addrlist *addrs;
	struct	hostent *result;
	char	*buffer;
	int	buflen;
{
	int	i, count;
	struct	in_addr *addrp;
	char	**addrvec;
	struct	sockaddr_in *sa;
	struct	netbuf *na;

	result->h_name		= buffer;
	result->h_addrtype	= AF_INET;
	result->h_length	= sizeof (*addrp);

	/*
	 * Build addrlist at start of buffer (after name);  store the
	 * addresses themselves at the end of the buffer.
	 */
	i = strlen(nam) + 1;
	addrvec = (char **)ROUND_UP(buffer + i, sizeof (*addrvec));
	result->h_addr_list 	= addrvec;

	addrp	= (struct in_addr *)ROUND_DOWN(buffer + buflen,
	    sizeof (*addrp));

	count = addrs->n_cnt;
	if ((char *)(&addrvec[count + 1]) > (char *)(&addrp[-count]))
		return (ND_NOMEM);

	memcpy(buffer, nam, i);

	for (na = addrs->n_addrs, i = 0;  i < count;  na++, i++) {
		--addrp;
		memcpy(addrp,
		    &((struct sockaddr_in *)na->buf)->sin_addr,
		    sizeof (*addrp));
		*addrvec++ = (char *) addrp;
	}
	*addrvec = 0;
	result->h_aliases	= addrvec;

	return (ND_OK);
}

/*
 * Process results from nd_addrlist ( returned by netdir_getbyname)
 * into a servent using buf.
 */
int
ndaddr2srent(const char *name, const char *proto, u_short port,
    struct servent *result, char *buffer, int buflen)
{
	int	i;
	char	*bufend = (buffer + buflen);

	result->s_port = (int) port;

	result->s_aliases =
	    (char **) ROUND_UP(buffer, sizeof (char *));
	result->s_aliases[0] = NULL;
	buffer = (char *) &result->s_aliases[1];
	result->s_name = buffer;
	i = strlen(name) + 1;
	if ((buffer + i) > bufend)
		return (ND_NOMEM);
	(void) memcpy(buffer, name, i);
	buffer += i;

	result->s_proto	= buffer;
	i = strlen(proto) + 1;
	if ((buffer + i) > bufend)
		return (ND_NOMEM);
	(void) memcpy(buffer, proto, i);
	buffer += i;

	return (ND_OK);
}

/*
 * Process results from nd_hostservlist ( returned by netdir_getbyaddr)
 * into a hostent using buf.
 * *** ASSUMES that nd_buf->buf is a sockaddr_in ***
 */
int
ndhostserv2hent(nbuf, addrs, result, buffer, buflen)
	struct	netbuf *nbuf;
	struct	nd_hostservlist *addrs;
	struct	hostent *result;
	char	*buffer;
	int	buflen;
{
	int	i, count;
	char	*aliasp;
	char	**aliasvec;
	struct	sockaddr_in *sa;
	struct	nd_hostserv *hs;
	const	char *la;

	/* First, give the lonely address a specious home in h_addr_list. */
	aliasp   = (char  *)ROUND_UP(buffer, sizeof (sa->sin_addr));
	sa = (struct sockaddr_in *)nbuf->buf;
	memcpy(aliasp, (char *)&(sa->sin_addr), sizeof (sa->sin_addr));
	aliasvec = (char **)ROUND_UP(aliasp + sizeof (sa->sin_addr),
		sizeof (*aliasvec));
	result->h_addr_list = aliasvec;
	*aliasvec++ = aliasp;
	*aliasvec++ = 0;

	/*
	 * Build h_aliases at start of buffer (after addr and h_addr_list);
	 * store the aliase strings at the end of the buffer (before h_name).
	 */

	aliasp = buffer + buflen;

	result->h_aliases	= aliasvec;

	hs = addrs->h_hostservs;
	if (! hs)
		return (ND_NOHOST);

	i = strlen(hs->h_host) + 1;
	aliasp -= i;
	if ((char *)(&aliasvec[1]) > aliasp)
		return (ND_NOMEM);
	memcpy(aliasp, hs->h_host, i);

	result->h_name		= aliasp;
	result->h_addrtype	= AF_INET;
	result->h_length	= sizeof (sa->sin_addr);

	/*
	 * Assumption: the netdir nametoaddr_libs
	 * sort the vector of (host, serv) pairs in such a way that
	 * all pairs with the same host name are contiguous.
	 */
	la = hs->h_host;
	count = addrs->h_cnt;
	for (i = 0;  i < count;  i++, hs++)
		if (strcmp(la, hs->h_host) != 0) {
			int len = strlen(hs->h_host) + 1;

			aliasp -= len;
			if ((char *)(&aliasvec[2]) > aliasp)
				return (ND_NOMEM);
			memcpy(aliasp, hs->h_host, len);
			*aliasvec++ = aliasp;
			la = hs->h_host;
		}
	*aliasvec = 0;

	return (ND_OK);
}

/*
 * Process results from nd_hostservlist ( returned by netdir_getbyaddr)
 * into a servent using buf.
 */
int
ndhostserv2srent(port, proto, addrs, result, buffer, buflen)
	int	port;
	const	char *proto;
	struct	nd_hostservlist *addrs;
	struct	servent *result;
	char	*buffer;
	int	buflen;
{
	int	i, j, count;
	char	*aliasp;
	char	**aliasvec;
	struct	nd_hostserv *hs;
	const	char *host_cname;

	result->s_port = port;
	/*
	 * Build s_aliases at start of buffer;
	 * store proto and aliases at the end of the buffer (before h_name).
	 */

	aliasp = buffer + buflen;
	aliasvec = (char **) ROUND_UP(buffer, sizeof (char *));

	result->s_aliases	= aliasvec;

	hs = addrs->h_hostservs;
	if (! hs)
		return (ND_NOHOST);
	host_cname = hs->h_host;

	i = strlen(proto) + 1;
	j = strlen(hs->h_serv) + 1;
	if ((char *)(&aliasvec[2]) > (aliasp - i -j))
		return (ND_NOMEM);

	aliasp -= i;
	(void) memcpy(aliasp, proto, i);
	result->s_proto = aliasp;

	aliasp -= j;
	(void) memcpy(aliasp, hs->h_serv, j);
	result->s_name = aliasp;

	/*
	 * Assumption: the netdir nametoaddr_libs
	 * do a host aliases first and serv aliases next
	 * enumeration for creating the list of hostserv
	 * structures.
	 */
	count = addrs->h_cnt;
	for (i = 0;
	    i < count && hs->h_serv && strcmp(hs->h_host, host_cname) == 0;
	    i++, hs++) {
		int len = strlen(hs->h_serv) + 1;

		aliasp -= len;
		if ((char *)(&aliasvec[2]) > aliasp)
			return (ND_NOMEM);
		(void) memcpy(aliasp, hs->h_serv, len);
		*aliasvec++ = aliasp;
	}
	*aliasvec = NULL;

	return (ND_OK);
}


static int
nd2herrno(nerr)
int nerr;
{
	trace1(TR_nd2herrno, 0);
	switch (nerr) {
	case ND_OK:
		trace1(TR_nd2herrno, 1);
		return (0);
	case ND_TRY_AGAIN:
		trace1(TR_nd2herrno, 1);
		return (TRY_AGAIN);
	case ND_NO_RECOVERY:
	case ND_BADARG:
	case ND_NOMEM:
		trace1(TR_nd2herrno, 1);
		return (NO_RECOVERY);
	case ND_NO_DATA:
		trace1(TR_nd2herrno, 1);
		return (NO_DATA);
	case ND_NOHOST:
	case ND_NOSERV:
		trace1(TR_nd2herrno, 1);
		return (HOST_NOT_FOUND);
	default:
		trace1(TR_nd2herrno, 1);
		return (NO_RECOVERY);
	}
}

#define	MAXIFS 32
#define	UDP "/dev/udp"

static struct ifinfo *
get_local_info()
{
	int numifs;
	char	*buf;
	struct	ifconf ifc;
	struct	ifreq ifreq, *ifr;
	int fd;
	struct ifinfo *localinfo;
	int i, n;
	struct in_addr netmask;
	struct sockaddr_in *sin;

	if ((fd = open(UDP, O_RDONLY)) < 0) {
		(void) syslog(LOG_ERR,
	    "n2a get_local_info: open to get interface configuration: %m");
		_nderror = ND_OPEN;
		return ((struct ifinfo *)NULL);
	}

#ifdef SIOCGIFNUM
	if (ioctl(fd, SIOCGIFNUM, (char *)&numifs) < 0) {
		perror("ioctl (get number of interfaces)");
		numifs = MAXIFS;
	}
#else
	numifs = MAXIFS;
#endif
	buf = (char *)malloc(numifs * sizeof (struct ifreq));
	if (buf == NULL) {
		(void) syslog(LOG_ERR, "n2a get_local_info: malloc failed: %m");
		close(fd);
		_nderror = ND_NOMEM;
		return ((struct ifinfo *)NULL);
	}
	ifc.ifc_len = numifs * sizeof (struct ifreq);
	ifc.ifc_buf = buf;
	if (ioctl(fd, SIOCGIFCONF, (char *)&ifc) < 0) {
		(void) syslog(LOG_ERR,
	    "n2a get_local_info: ioctl (get interface configuration): %m");
		close(fd);
		free(buf);
		_nderror = ND_SYSTEM;
		return ((struct ifinfo *)NULL);
	}
	ifr = (struct ifreq *)buf;
	numifs = ifc.ifc_len/sizeof (struct ifreq);
	localinfo = (struct ifinfo *)malloc((numifs + 1) *
	    sizeof (struct ifinfo));
	if (localinfo == NULL) {
		(void) syslog(LOG_ERR, "n2a get_local_info: malloc failed: %m");
		close(fd);
		free(buf);
		_nderror = ND_SYSTEM;
		return ((struct ifinfo *)NULL);
	}

	for (i = 0, n = numifs; n > 0; n--, ifr++) {
		short ifrflags;

		ifreq = *ifr;
		if (ioctl(fd, SIOCGIFFLAGS, (char *)&ifreq) < 0) {
			(void) syslog(LOG_ERR,
		    "n2a get_local_info: ioctl (get interface flags): %m");
			continue;
		}
		ifrflags = ifreq.ifr_flags;
		if (((ifrflags & IFF_UP) == 0) ||
		    (ifr->ifr_addr.sa_family != AF_INET))
			continue;

		if (ioctl(fd, SIOCGIFNETMASK, (char *)&ifreq) < 0) {
			(void) syslog(LOG_ERR,
	    "n2a get_local_info: ioctl (get interface netmask): %m");
			continue;
		}
		netmask = ((struct sockaddr_in *) &ifreq.ifr_addr)->sin_addr;

		if (ioctl(fd, SIOCGIFADDR, (char *)&ifreq) < 0) {
			(void) syslog(LOG_ERR,
	    "n2a get_local_info: ioctl (get interface address): %m");
			continue;
		}
		sin = (struct sockaddr_in *) &ifreq.ifr_addr;

		localinfo[i].addr = sin->sin_addr;
		localinfo[i].netmask = netmask;
		i++;
	}
	localinfo[i].addr.s_addr = 0;

	free(buf);
	close(fd);
	return (localinfo);
}

static int
islocal(localinfo, addr)
struct ifinfo *localinfo;
struct in_addr addr;
{
	struct ifinfo *lp;

	if (!localinfo)
	    return (0);

	for (lp = localinfo; lp->addr.s_addr; lp++) {
		if ((addr.s_addr & lp->netmask.s_addr) ==
		    (lp->addr.s_addr & lp->netmask.s_addr)) {
			return (1);
		}
	}
	return (0);
}

/*
 *  Some higher-level routines for determining if an address is
 *  on a local network.
 *
 *      __inet_get_local_interfaces() - get an opaque handle with
 *          with a list of local interfaces
 *      __inet_address_is_local() - return 1 if an address is
 *          on a local network; 0 otherwise
 *      __inet_free_local_interfaces() - free handle that was
 *          returned by __inet_get_local_interfaces()
 *
 *  A typical calling sequence is:
 *
 *      p = __inet_get_local_interfaces();
 *      if (__inet_address_is_local(p, inaddr)) {
 *          ...
 *      }
 *      __inet_free_local_interfaces(p);
 */

/*
 *  Return an opaque pointer to a list of configured interfaces.
 */
void *
__inet_get_local_interfaces()
{
	struct ifinfo *lp;

	lp = get_local_info();
	return ((void *)lp);
}

/*
 *  Free memory allocated by inet_local_interfaces().
 */
void
__inet_free_local_interfaces(void *p)
{
	free(p);
}

/*
 *  Determine if an address is on a local network.
 */
int
__inet_address_is_local(void *p, struct in_addr addr)
{
	struct ifinfo *lp = (struct ifinfo *)p;

	return (islocal(lp, addr));
}

static int
getbroadcastnets(tp, addrs)
	struct netconfig *tp;
	struct in_addr *addrs;
{
	struct ifconf ifc;
	struct ifreq ifreq, *ifr;
	struct sockaddr_in *sin;
	int fd;
	int n, i, numifs;
	char *buf;
	int	use_loopback = 0;

	_nderror = ND_SYSTEM;
	fd = open(tp->nc_device, O_RDONLY);
	if (fd < 0) {
		(void) syslog(LOG_ERR,
	    "broadcast: open to get interface configuration: %m");
		return (0);
	}
#ifdef SIOCGIFNUM
	if (ioctl(fd, SIOCGIFNUM, (char *)&numifs) < 0) {
		perror("ioctl (get number of interfaces)");
		numifs = MAXIFS;
	}
#else
	numifs = MAXIFS;
#endif
	buf = (char *)malloc(numifs * sizeof (struct ifreq));
	if (buf == NULL) {
		(void) syslog(LOG_ERR, "broadcast: malloc failed: %m");
		close(fd);
		return (0);
	}
	ifc.ifc_len = numifs * sizeof (struct ifreq);
	ifc.ifc_buf = buf;
	/*
	 * Ideally, this ioctl should also tell me, how many bytes were
	 * finally allocated, but it doesnt.
	 */
	if (ioctl(fd, SIOCGIFCONF, (char *)&ifc) < 0) {
		(void) syslog(LOG_ERR,
	    "broadcast: ioctl (get interface configuration): %m");
		close(fd);
		return (0);
	}

retry:
	ifr = (struct ifreq *)buf;
	for (i = 0, n = ifc.ifc_len/sizeof (struct ifreq);
		n > 0 && i < MAXBCAST; n--, ifr++) {
		ifreq = *ifr;
		if (ioctl(fd, SIOCGIFFLAGS, (char *)&ifreq) < 0) {
			(void) syslog(LOG_ERR,
		    "broadcast: ioctl (get interface flags): %m");
			continue;
		}
		if (!(ifreq.ifr_flags & IFF_UP) ||
		    (ifr->ifr_addr.sa_family != AF_INET))
			continue;
		if (ifreq.ifr_flags & IFF_BROADCAST) {
			sin = (struct sockaddr_in *)&ifr->ifr_addr;
			if (ioctl(fd, SIOCGIFBRDADDR, (char *)&ifreq) < 0) {
				/* May not work with other implementation */
				addrs[i++] = inet_makeaddr(
				    inet_netof(sin->sin_addr),
				    INADDR_ANY);
			} else {
				addrs[i++] = ((struct sockaddr_in *)
				    &ifreq.ifr_addr)->sin_addr;
			}
			continue;
		}
		if (use_loopback && (ifreq.ifr_flags & IFF_LOOPBACK)) {
			sin = (struct sockaddr_in *)&ifr->ifr_addr;
			addrs[i++] = sin->sin_addr;
			continue;
		}
		if (ifreq.ifr_flags & IFF_POINTOPOINT) {
			if (ioctl(fd, SIOCGIFDSTADDR, (char *)&ifreq) < 0)
				continue;
			addrs[i++] = ((struct sockaddr_in *)
			    &ifreq.ifr_addr)->sin_addr;
			continue;
		}
	}
	if (i == 0 && !use_loopback) {
		use_loopback = 1;
		goto retry;
	}
	free(buf);
	close(fd);
	if (i)
		_nderror = ND_OK;
	return (i);
}

/*
 * This is lifted straigt from libsocket/inet/inet_mkaddr.c.
 * Copied here to avoid our dependency on libsocket. More important
 * to make sure partially static app that use libnsl, but not
 * libsocket, don't get screwed up.
 * If you understand the above paragraph, try to get rid of
 * this copy of inet_makeaddr; if you don;t, leave it alone.
 *
 * Formulate an Internet address from network + host.  Used in
 * building addresses stored in the ifnet structure.
 */
static struct in_addr
inet_makeaddr(net, host)
	u_long net, host;
{
	u_long addr;

	if (net < 128)
		addr = (net << IN_CLASSA_NSHIFT) | (host & IN_CLASSA_HOST);
	else if (net < 65536)
		addr = (net << IN_CLASSB_NSHIFT) | (host & IN_CLASSB_HOST);
	else if (net < 16777216L)
		addr = (net << IN_CLASSC_NSHIFT) | (host & IN_CLASSC_HOST);
	else
		addr = net | host;
	addr = htonl(addr);
	return (*(struct in_addr *)&addr);
}
