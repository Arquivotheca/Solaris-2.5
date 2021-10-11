/*
 * Copyright (c) 1984 - 1991 by Sun Microsystems, Inc.
 */

#ident	"@(#)pmap_svc.c	1.12	92/11/04 SMI"

#ifndef lint
static	char sccsid[] = "@(#)pmap_svc.c 1.23 89/04/05 Copyr 1984 Sun Micro";
#endif

/*
 * pmap_svc.c
 * The server procedure for the version 2 portmaper.
 * All the portmapper related interface from the portmap side.
 */

#ifdef PORTMAP
#include <stdio.h>
#include <rpc/rpc.h>
#include <rpc/pmap_prot.h>
#include <rpc/rpcb_prot.h>
#include "rpcbind.h"

#ifdef CHECK_LOCAL
#include <netdb.h>
#include <netinet/in.h>
#include <net/if.h>		/* to find local addresses */
#define	BSD_COMP		/* XXX - so that it includes <sys/sockio.h> */
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netdir.h>

#ifndef INADDR_LOOPBACK		/* Some <netinet/in.h> files do not have this */
#define	INADDR_LOOPBACK		(u_long)0x7F000001
#endif
static bool_t chklocal();
static int getlocal();
#endif  /* CHECK_LOCAL */

static PMAPLIST *find_service_pmap();
static bool_t pmapproc_change();
static bool_t pmapproc_getport();
static bool_t pmapproc_dump();

/*
 * Called for all the version 2 inquiries.
 */
void
pmap_service(rqstp, xprt)
	register struct svc_req *rqstp;
	register SVCXPRT *xprt;
{
	rpcbs_procinfo(RPCBVERS_2_STAT, rqstp->rq_proc);
	switch (rqstp->rq_proc) {
	case PMAPPROC_NULL:
		/*
		 * Null proc call
		 */
#ifdef RPCBIND_DEBUG
		fprintf(stderr, "PMAPPROC_NULL\n");
#endif
		if ((!svc_sendreply(xprt, (xdrproc_t) xdr_void, NULL)) &&
			debugging) {
			if (doabort) {
				rpcbind_abort();
			}
		}
		break;

	case PMAPPROC_SET:
		/*
		 * Set a program, version to port mapping
		 */
		pmapproc_change(rqstp, xprt, rqstp->rq_proc);
		break;

	case PMAPPROC_UNSET:
		/*
		 * Remove a program, version to port mapping.
		 */
		pmapproc_change(rqstp, xprt, rqstp->rq_proc);
		break;

	case PMAPPROC_GETPORT:
		/*
		 * Lookup the mapping for a program, version and return its
		 * port number.
		 */
		pmapproc_getport(rqstp, xprt);
		break;

	case PMAPPROC_DUMP:
		/*
		 * Return the current set of mapped program, version
		 */
#ifdef RPCBIND_DEBUG
		fprintf(stderr, "PMAPPROC_DUMP\n");
#endif
		pmapproc_dump(rqstp, xprt);
		break;

	case PMAPPROC_CALLIT:
		/*
		 * Calls a procedure on the local machine. If the requested
		 * procedure is not registered this procedure does not return
		 * error information!!
		 * This procedure is only supported on rpc/udp and calls via
		 * rpc/udp. It passes null authentication parameters.
		 */
		rpcbproc_callit_com(rqstp, xprt, PMAPPROC_CALLIT, PMAPVERS);
		break;

	default:
		svcerr_noproc(xprt);
		break;
	}
}

/*
 * returns the item with the given program, version number. If that version
 * number is not found, it returns the item with that program number, so that
 * the port number is now returned to the caller. The caller when makes a
 * call to this program, version number, the call will fail and it will
 * return with PROGVERS_MISMATCH. The user can then determine the highest
 * and the lowest version number for this program using clnt_geterr() and
 * use those program version numbers.
 */
static PMAPLIST *
find_service_pmap(prog, vers, prot)
	u_long prog;
	u_long vers;
	u_long prot;
{
	register PMAPLIST *hit = NULL;
	register PMAPLIST *pml;

	for (pml = list_pml; pml != NULL; pml = pml->pml_next) {
		if ((pml->pml_map.pm_prog != prog) ||
			(pml->pml_map.pm_prot != prot))
			continue;
		hit = pml;
		if (pml->pml_map.pm_vers == vers)
			break;
	}
	return (hit);
}

static bool_t
pmapproc_change(rqstp, xprt, op)
	struct svc_req *rqstp;
	register SVCXPRT *xprt;
	unsigned long op;
{
	PMAP reg;
	RPCB rpcbreg;
	int ans;
	struct sockaddr_in *who;
	extern bool_t map_set(), map_unset();

	if (!svc_getargs(xprt, (xdrproc_t) xdr_pmap, (char *)&reg)) {
		svcerr_decode(xprt);
		return (FALSE);
	}
	who = svc_getcaller(xprt);

#ifdef RPCBIND_DEBUG
	fprintf(stderr, "%s request for (%lu, %lu) : ",
		op == PMAPPROC_SET ? "PMAP_SET" : "PMAP_UNSET",
		reg.pm_prog, reg.pm_vers);
#endif

#ifdef CHECK_LOCAL
	/*
	 * To check whether the request came from a local server.  If this
	 * cannot be tested, we assign that call as "unknown".
	 */
	if (chklocal(who->sin_addr) == FALSE) {
		ans = FALSE;
		goto done_change;
	}
	if (ntohs(who->sin_port) >= IPPORT_RESERVED)
		rpcbreg.r_owner = "unknown";
	else
		rpcbreg.r_owner = "superuser";
#else
	rpcbreg.r_owner = "unknown";
#endif

	if ((op == PMAPPROC_SET) && (reg.pm_port < IPPORT_RESERVED) &&
	    (ntohs(who->sin_port) >= IPPORT_RESERVED)) {
		ans = FALSE;
		goto done_change;
	}
	rpcbreg.r_prog = reg.pm_prog;
	rpcbreg.r_vers = reg.pm_vers;

	if (op == PMAPPROC_SET) {
		char buf[32];

		sprintf(buf, "0.0.0.0.%d.%d", (reg.pm_port >> 8) & 0xff,
			reg.pm_port & 0xff);
		rpcbreg.r_addr = buf;
		if (reg.pm_prot == IPPROTO_UDP) {
			rpcbreg.r_netid = udptrans;
		} else if (reg.pm_prot == IPPROTO_TCP) {
			rpcbreg.r_netid = tcptrans;
		} else {
			ans = FALSE;
			goto done_change;
		}
		ans = map_set(&rpcbreg, rpcbreg.r_owner);
	} else if (op == PMAPPROC_UNSET) {
		bool_t ans1, ans2;

		rpcbreg.r_addr = NULL;
		rpcbreg.r_netid = tcptrans;
		ans1 = map_unset(&rpcbreg, rpcbreg.r_owner);
		rpcbreg.r_netid = udptrans;
		ans2 = map_unset(&rpcbreg, rpcbreg.r_owner);
		ans = ans1 || ans2;
	} else {
		ans = FALSE;
	}
done_change:
	if ((!svc_sendreply(xprt, (xdrproc_t) xdr_long, (caddr_t) &ans)) &&
	    debugging) {
		fprintf(stderr, "portmap: svc_sendreply\n");
		if (doabort) {
			rpcbind_abort();
		}
	}
#ifdef RPCBIND_DEBUG
	fprintf(stderr, "%s\n", ans == TRUE ? "succeeded" : "failed");
#endif
	if (op == PMAPPROC_SET)
		rpcbs_set(RPCBVERS_2_STAT, ans);
	else
		rpcbs_unset(RPCBVERS_2_STAT, ans);
	return (TRUE);
}

/* ARGSUSED */
static bool_t
pmapproc_getport(rqstp, xprt)
	struct svc_req *rqstp;
	register SVCXPRT *xprt;
{
	PMAP reg;
	int port = 0;
	PMAPLIST *fnd;
#ifdef RPCBIND_DEBUG
	char *uaddr;
#endif

	if (!svc_getargs(xprt, (xdrproc_t) xdr_pmap, (char *)&reg)) {
		svcerr_decode(xprt);
		return (FALSE);
	}
#ifdef RPCBIND_DEBUG
	uaddr =  taddr2uaddr(rpcbind_get_conf(xprt->xp_netid),
			    svc_getrpccaller(xprt));
	fprintf(stderr, "PMAP_GETPORT request for (%lu, %lu, %s) from %s :",
		reg.pm_prog, reg.pm_vers,
		reg.pm_prot == IPPROTO_UDP ? "udp" : "tcp", uaddr);
	free(uaddr);
#endif
	fnd = find_service_pmap(reg.pm_prog, reg.pm_vers, reg.pm_prot);
	if (fnd) {
		char serveuaddr[32], *ua;
		int h1, h2, h3, h4, p1, p2;
		char *netid;

		if (reg.pm_prot == IPPROTO_UDP) {
			ua = udp_uaddr;
			netid = udptrans;
		} else {
			ua = tcp_uaddr; /* To get the len */
			netid = tcptrans;
		}
		if (ua == NULL) {
			goto sendreply;
		}
		if (sscanf(ua, "%d.%d.%d.%d.%d.%d", &h1, &h2, &h3,
				&h4, &p1, &p2) == 6) {
			p1 = (fnd->pml_map.pm_port >> 8) & 0xff;
			p2 = (fnd->pml_map.pm_port) & 0xff;
			sprintf(serveuaddr, "%d.%d.%d.%d.%d.%d",
				h1, h2, h3, h4, p1, p2);
			if (is_bound(netid, serveuaddr)) {
				port = fnd->pml_map.pm_port;
			} else { /* this service is dead; delete it */
				delete_prog(reg.pm_prog);
			}
		}
	}
sendreply:
	if ((!svc_sendreply(xprt, (xdrproc_t) xdr_long, (caddr_t)&port)) &&
			debugging) {
		(void) fprintf(stderr, "portmap: svc_sendreply\n");
		if (doabort) {
			rpcbind_abort();
		}
	}
#ifdef RPCBIND_DEBUG
	fprintf(stderr, "port = %d\n", port);
#endif
	rpcbs_getaddr(RPCBVERS_2_STAT, reg.pm_prog, reg.pm_vers,
		reg.pm_prot == IPPROTO_UDP ? udptrans : tcptrans,
		port ? udptrans : "");

	return (TRUE);
}

/* ARGSUSED */
static bool_t
pmapproc_dump(rqstp, xprt)
	struct svc_req *rqstp;
	register SVCXPRT *xprt;
{
	if (!svc_getargs(xprt, (xdrproc_t)xdr_void, NULL)) {
		svcerr_decode(xprt);
		return (FALSE);
	}
	if ((!svc_sendreply(xprt, (xdrproc_t) xdr_pmaplist_ptr,
			(caddr_t)&list_pml)) && debugging) {
		(void) fprintf(stderr, "portmap: svc_sendreply\n");
		if (doabort) {
			rpcbind_abort();
		}
	}
	return (TRUE);
}

#ifdef CHECK_LOCAL
/*
 * XXX: This is all socket depedent stuff and one will have to link it
 * libsocket to get the socket interface.
 */

/* how many interfaces could there be on a computer? */
#define	MAX_LOCAL 16
static int num_local = -1;
static struct in_addr addrs[MAX_LOCAL];

static bool_t
chklocal(taddr)
	struct in_addr	taddr;
{
	int		i;
	struct in_addr	iaddr, tmpaddr;

	tmpaddr.s_addr = ntohl(taddr.s_addr);
	if (tmpaddr.s_addr == INADDR_LOOPBACK)
		return (TRUE);
	if (num_local == -1) {
		num_local = getlocal();
		if (debugging)
			fprintf(stderr,
			"portmap: %d interfaces detected.\n", num_local);
	}
	for (i = 0; i < num_local; i++) {
		iaddr.s_addr = ntohl(addrs[i].s_addr);
		if (memcmp((char *) &tmpaddr, (char *) &(iaddr),
			sizeof (struct in_addr)) == 0)
			return (TRUE);
	}
	return (FALSE);
}

static int
getlocal()
{
	struct ifconf	ifc;
	struct ifreq	ifreq, *ifr;
	int		n, j, sock;
	char		buf[UDPMSGSIZE];

	ifc.ifc_len = UDPMSGSIZE;
	ifc.ifc_buf = buf;
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		return (FALSE);
	if (ioctl(sock, SIOCGIFCONF, (char *) &ifc) < 0) {
		perror("portmap:ioctl SIOCGIFCONF");
		(void) close(sock);
		return (FALSE);
	}
	ifr = ifc.ifc_req;
	j = 0;
	for (n = ifc.ifc_len / sizeof (struct ifreq); n > 0; n--, ifr++) {
		ifreq = *ifr;
		if (ioctl(sock, SIOCGIFFLAGS, (char *) &ifreq) < 0) {
			perror("portmap:ioctl SIOCGIFFLAGS");
			continue;
		}
		if ((ifreq.ifr_flags & IFF_UP) &&
			ifr->ifr_addr.sa_family == AF_INET) {
			if (ioctl(sock, SIOCGIFADDR, (char *) &ifreq) < 0) {
				perror("SIOCGIFADDR");
			} else {
				addrs[j] = ((struct sockaddr_in *)
						& ifreq.ifr_addr)->sin_addr;
				j++;
			}
		}
		if (j >= (MAX_LOCAL - 1))
			break;
	}
	(void) close(sock);
	return (j);
}

#endif /* CHECK_LOCAL */
#endif /* PORTMAP */
