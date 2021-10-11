/*
 * Copyright (c) 1986-1992 by Sun Microsystems Inc.
 */

#include <sys/types.h>
#include <sys/file.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <tiuser.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdir.h>
#include <netdb.h>
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>

CLIENT *__clnt_tp_create_bootstrap();
int __rpcb_getaddr_bootstrap();
struct hostent *__files_gethostbyname();

static char *__map_addr();

/*
 * __clnt_tp_create_bootstrap()
 *
 * This routine is NOT TRANSPORT INDEPENDENT.
 *
 * It relies on the local /etc/hosts file for hostname to address
 * translation and does it itself instead of calling netdir_getbyname
 * thereby avoids recursion.
 */
CLIENT *
__clnt_tp_create_bootstrap(hostname, prog, vers, nconf)
	char *hostname;
	u_long prog, vers;
	struct netconfig    *nconf;
{
	CLIENT *cl;
	struct netbuf	*svc_taddr;
	struct sockaddr_in	*sa;
	int fd;

	if (nconf == (struct netconfig *)NULL)
		return (NULL);
	if ((fd = t_open(nconf->nc_device, O_RDWR, NULL)) == -1)
		return (NULL);
	svc_taddr = (struct netbuf *) malloc(sizeof (struct netbuf));
	if (! svc_taddr) {
		t_close(fd);
		return (NULL);
	}
	sa = (struct sockaddr_in *)calloc(1, sizeof (struct sockaddr_in));
	if (! sa) {
		t_close(fd);
		free(svc_taddr);
		return (NULL);
	}
	svc_taddr->maxlen = svc_taddr->len = sizeof (*sa);
	svc_taddr->buf = (char *)sa;
	if (__rpcb_getaddr_bootstrap(prog,
		vers, nconf, svc_taddr, hostname) == FALSE)
		return (NULL);
	cl = clnt_tli_create(fd, nconf, svc_taddr, prog, vers, 0, 0);
	return (cl);
}

/*
 * __rpcb_getaddr_bootstrap()
 *
 * This is our internal function that replaces rpcb_getaddr(). We
 * build our own to prevent calling netdir_getbyname() which could
 * recurse to the nameservice.
 */
int
__rpcb_getaddr_bootstrap(program, version, nconf, address, hostname)
	u_long program;
	u_long version;
	struct netconfig *nconf;
	struct netbuf *address; /* populate with the taddr of the service */
	char *hostname;
{
	char *svc_uaddr;
	struct hostent *hent;
	struct sockaddr_in *sa;
	struct netbuf rpcb_taddr;
	struct sockaddr_in local_sa;
	unsigned long inaddr;
	unsigned short inport;
	int h1, h2, h3, h4, p1, p2;

	/* Get the address of the RPCBIND at hostname */
	hent = __files_gethostbyname(hostname);
	if (hent == (struct hostent *)NULL) {
		rpc_createerr.cf_stat = RPC_UNKNOWNHOST;
		return (FALSE);
	}
	local_sa.sin_family = AF_INET;
	local_sa.sin_port = htons(111); /* RPCBIND port */
	memcpy((char *)&(local_sa.sin_addr.s_addr),
		hent->h_addr_list[0], sizeof (local_sa.sin_addr.s_addr));
	rpcb_taddr.buf = (char *)&local_sa;
	rpcb_taddr.maxlen = sizeof (struct sockaddr_in);
	rpcb_taddr.len = rpcb_taddr.maxlen;

	svc_uaddr = __map_addr(nconf, &rpcb_taddr, program, version);
	if (! svc_uaddr)
		return (FALSE);

/* do a local uaddr2taddr and stuff in the memory supplied by the caller */
	sscanf(svc_uaddr, "%d.%d.%d.%d.%d.%d", &h1, &h2, &h3, &h4, &p1, &p2);
	inaddr = (h1 << 24) + (h2 << 16) + (h3 << 8) + h4;
	sa = (struct sockaddr_in *)address->buf;
	inport = (p1 << 8) + p2;
	sa->sin_port = htons(inport);
	sa->sin_family = AF_INET;
	sa->sin_addr.s_addr = htonl(inaddr);
	return (TRUE);
}

/*
 * __map_addr()
 *
 */
static char *
__map_addr(nc, rpcb_taddr, prog, ver)
	struct netconfig	*nc;		/* Our transport	*/
	struct netbuf	*rpcb_taddr; /* RPCBIND address */
	u_long			prog, ver;	/* Name service Prog/vers */
{
	register CLIENT *client;
	RPCB 		parms;		/* Parameters for RPC binder	  */
	enum clnt_stat	clnt_st;	/* Result from the rpc call	  */
	int		fd;		/* Stream file descriptor	  */
	char 		*ua = NULL;	/* Universal address of service	  */
	struct timeval	tv;		/* Timeout for our rpcb call	  */

	/*
	 * First we open a connection to the remote rpcbind process.
	 */
	if ((fd = t_open(nc->nc_device, O_RDWR, NULL)) == -1) {
		return (NULL);
	}

	client = clnt_tli_create(fd, nc, rpcb_taddr, RPCBPROG, RPCBVERS, 0, 0);
	if (! client) {
		t_close(fd);
		rpc_createerr.cf_stat = RPC_TLIERROR;
		return (NULL);
	}

	/*
	 * Now make the call to get the NIS service address.
	 */
	tv.tv_sec = 10;
	tv.tv_usec = 0;
	parms.r_prog = prog;
	parms.r_vers = ver;
	parms.r_netid = nc->nc_netid;	/* not needed */
	parms.r_addr = "";	/* not needed; just for xdring */
	parms.r_owner = "";	/* not needed; just for xdring */
	clnt_st = clnt_call(client, RPCBPROC_GETADDR, xdr_rpcb, (char *)&parms,
					    xdr_wrapstring, (char *)&ua, tv);

	if (clnt_st == RPC_SUCCESS) {

		clnt_destroy(client);
		t_close(fd);
		if (*ua == '\0') {
			xdr_free(xdr_wrapstring, (char *)&ua);
			return (NULL);
		}
		return (ua);
	} else if (((clnt_st == RPC_PROGVERSMISMATCH) ||
			(clnt_st == RPC_PROGUNAVAIL) ||
			(clnt_st == RPC_TIMEDOUT)) &&
			(strcmp(nc->nc_protofmly, NC_INET) == 0)) {
		/*
		 * version 3 not available. Try version 2
		 * The assumption here is that the netbuf
		 * is arranged in the sockaddr_in
		 * style for IP cases.
		 */
		u_short	port;
		struct sockaddr_in	*sa;
		struct netbuf 		remote;
		int		protocol;
		char	buf[32];
		char	*res;

		clnt_control(client, CLGET_SVC_ADDR, (char *) &remote);
		sa = (struct sockaddr_in *)(remote.buf);
		protocol = strcmp(nc->nc_proto, NC_TCP) ?
				IPPROTO_UDP : IPPROTO_TCP;
		port = (u_short) pmap_getport(sa, prog, ver, protocol);

		if (port != 0) {
			/* print s_addr (and port) in host byte order */
			sa->sin_addr.s_addr = ntohl(sa->sin_addr.s_addr);
			sprintf(buf, "%d.%d.%d.%d.%d.%d",
				(sa->sin_addr.s_addr >> 24) & 0xff,
				(sa->sin_addr.s_addr >> 16) & 0xff,
				(sa->sin_addr.s_addr >>  8) & 0xff,
				(sa->sin_addr.s_addr) & 0xff,
				(port >> 8) & 0xff,
				port & 0xff);
			res = strdup(buf);
		} else
			res = NULL;
		clnt_destroy(client);
		t_close(fd);
		return (res);
	}
	clnt_destroy(client);
	t_close(fd);
	return (NULL);
}

#define	bcmp(s1, s2, len)	memcmp(s1, s2, len)
#define	bcopy(s1, s2, len)	memcpy(s2, s1, len)

#define	MAXALIASES	35

static FILE *hostf = NULL;
static char line[BUFSIZ+1];
static struct in_addr hostaddr;
static struct hostent host;
static char *host_aliases[MAXALIASES];
static char *host_addrs[] = {
	(char *)&hostaddr,
	NULL
};
char	*_host_file = "/etc/hosts";

static char *any();

static struct hostent *__files_gethostent();

struct hostent *
__files_gethostbyname(nam)
	register char *nam;
{
	register struct hostent *hp;
	register char **cp;

	if (hostf == NULL && (hostf = fopen(_host_file, "r")) == NULL)
		return (NULL);
	rewind(hostf);
	while (hp = __files_gethostent()) {
		if (strcasecmp(hp->h_name, nam) == 0)
			break;
		for (cp = hp->h_aliases; cp != 0 && *cp != 0; cp++)
			if (strcasecmp(*cp, nam) == 0)
				goto found;
	}
found:
	return (hp);
}

static struct hostent *
__files_gethostent()
{
	char *p;
	register char *cp, **q;
	u_long theaddr;

	if (hostf == NULL)
		return (NULL);
again:
	if ((p = fgets(line, BUFSIZ, hostf)) == NULL)
		return (NULL);
	if (*p == '#')
		goto again;
	cp = any(p, "#\n");
	if (cp == NULL)
		goto again;
	*cp = '\0';
	cp = any(p, " \t");
	if (cp == NULL)
		goto again;
	*cp++ = '\0';
	/* THIS STUFF IS INTERNET SPECIFIC */
	host.h_addr_list = host_addrs;
	theaddr = inet_addr(p);
	bcopy(&theaddr, host.h_addr_list[0], sizeof (u_long));
	host.h_length = sizeof (u_long);
	host.h_addrtype = AF_INET;
	while (*cp == ' ' || *cp == '\t')
		cp++;
	host.h_name = cp;
	q = host.h_aliases = host_aliases;
	cp = any(cp, " \t");
	if (cp != NULL)
		*cp++ = '\0';
	while (cp && *cp) {
		if (*cp == ' ' || *cp == '\t') {
			cp++;
			continue;
		}
		if (q < &host_aliases[MAXALIASES - 1])
			*q++ = cp;
		cp = any(cp, " \t");
		if (cp != NULL)
			*cp++ = '\0';
	}
	*q = NULL;
	return (&host);
}

static char *
any(cp, match)
	register char *cp;
	char *match;
{
	register char *mp, c;

	while (c = *cp) {
		for (mp = match; *mp; mp++)
			if (*mp == c)
				return (cp);
		cp++;
	}
	return ((char *)0);
}
