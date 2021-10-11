#pragma ident	"@(#)auth_time.c	1.6	94/10/19 SMI"

/*
 *	auth_time.c
 *
 * This module contains the private function __rpc_get_time_offset()
 * which will return the difference in seconds between the local system's
 * notion of time and a remote server's notion of time. This must be
 * possible without calling any functions that may invoke the name
 * service. (netdir_getbyxxx, getXbyY, etc). The function is used in the
 * synchronize call of the authdes code to synchronize clocks between
 * NIS+ clients and their servers.
 *
 * Note to minimize the amount of duplicate code, portions of the
 * synchronize() function were folded into this code, and the synchronize
 * call becomes simply a wrapper around this function. Further, if this
 * function is called with a timehost it *DOES* recurse to the name
 * server so don't use it in that mode if you are doing name service code.
 *
 *	Copyright (c) 1992 Sun Microsystems Inc.
 *	All rights reserved.
 *
 * Side effects :
 *	When called a client handle to a RPCBIND process is created
 *	and destroyed. Two strings "netid" and "uaddr" are malloc'd
 *	and returned. The SIGALRM processing is modified only if
 *	needed to deal with TCP connections.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <netdir.h>
#include <string.h>
#include <netconfig.h>
#include <netdb.h>
#include <signal.h>
#include <sys/errno.h>
#include <sys/poll.h>
#include <rpc/rpc.h>
#include <rpc/nettype.h>
#undef NIS
#include <rpcsvc/nis.h>


#ifdef TESTING
#define	msg(x)	printf("ERROR: %s\n", x)
/* #define msg(x) syslog(LOG_ERR, "%s", x) */
#else
#define	msg(x)
#endif

static int saw_alarm = 0;

static void
alarm_hndler(s)
	int	s;
{
	saw_alarm = 1;
}

/*
 * The internet time server defines the epoch to be Jan 1, 1900
 * whereas UNIX defines it to be Jan 1, 1970. To adjust the result
 * from internet time-service time, into UNIX time we subtract the
 * following offset :
 */
#define	NYEARS	(1970 - 1900)
#define	TOFFSET ((u_long)60*60*24*(365*NYEARS + (NYEARS/4)))

/*
 * free_eps()
 *
 * Free the strings that were strduped into the eps structure.
 */
static void
free_eps(eps, num)
	endpoint	eps[];
	int		num;
{
	int		i;

	for (i = 0; i < num; i++) {
		free(eps[i].uaddr);
		free(eps[i].proto);
		free(eps[i].family);
	}
}

/*
 * get_server()
 *
 * This function constructs a nis_server structure description for the
 * indicated hostname.
 */
static nis_server *
get_server(host, srv, eps, maxep)
	char		*host;	/* name of the time host	*/
	nis_server	*srv;	/* nis_server struct to use.	*/
	endpoint	eps[];	/* array of endpoints		*/
	int		maxep;	/* max array size		*/
{
	int			num_ep = 0, i;
	struct netconfig	*nc;
	void			*nch;
	struct nd_hostserv	hs;
	struct nd_addrlist	*addrs;

	if (! host)
		return (NULL);
	hs.h_host = host;
	hs.h_serv = "rpcbind";
	nch = setnetconfig();
	while (nc = getnetconfig(nch)) {
		if ((nc->nc_flag & NC_VISIBLE) == 0)
			continue;
		if (! netdir_getbyname(nc, &hs, &addrs)) {
			for (i = 0; (i < (addrs->n_cnt)) && (num_ep < maxep);
								i++, num_ep++) {
				eps[num_ep].uaddr =
					taddr2uaddr(nc, &(addrs->n_addrs[i]));
				eps[num_ep].family =
					strdup(nc->nc_protofmly);
				eps[num_ep].proto =
					strdup(nc->nc_proto);
			}
			netdir_free((char *)addrs, ND_ADDRLIST);
		}
	}
	endnetconfig(nch);

	srv->name = (nis_name) host;
	srv->ep.ep_len = num_ep;
	srv->ep.ep_val = eps;
	srv->key_type = NIS_PK_NONE;
	srv->pkey.n_bytes = NULL;
	srv->pkey.n_len = 0;
	return (srv);
}

/*
 * __rpc_get_time_offset()
 *
 * This function uses a nis_server structure to contact the a remote
 * machine (as named in that structure) and returns the offset in time
 * between that machine and this one. This offset is returned in seconds
 * and may be positive or negative.
 *
 * The first time through, a lot of fiddling is done with the netconfig
 * stuff to find a suitable transport. The function is very aggressive
 * about choosing UDP or at worst TCP if it can. This is because
 * those transports support both the RCPBIND call and the internet
 * time service.
 *
 * Once through, *uaddr is set to the universal address of
 * the machine and *netid is set to the local netid for the transport
 * that uaddr goes with. On the second call, the netconfig stuff
 * is skipped and the uaddr/netid pair are used to fetch the netconfig
 * structure and to then contact the machine for the time.
 *
 * td = "server" - "client"
 */
int
__rpc_get_time_offset(td, srv, thost, uaddr, netid)
	struct timeval	*td;	 /* Time difference			*/
	nis_server	*srv;	 /* NIS Server description 		*/
	char		*thost;	 /* if no server, this is the timehost	*/
	char		**uaddr; /* known universal address		*/
	char		**netid; /* known network identifier		*/
{
	CLIENT			*clnt; 		/* Client handle 	*/
	struct netbuf		*addr;		/* address 		*/
	void			*nc_handle;	/* Netconfig "state"	*/
	struct netconfig	*nc;		/* Various handles	*/
	endpoint		*ep,		/* useful endpoints	*/
				*useep = NULL;	/* endpoint of xp	*/
	char			*useua = NULL,	/* uaddr of selected xp	*/
				*useid = NULL;	/* netid of selected xp	*/
	int			epl, i;		/* counters		*/
	enum clnt_stat		status;		/* result of clnt_call	*/
	u_long			thetime, delta;
	int			needfree = 0;
	struct timeval		tv;
	int			rtime_fd = -1, time_valid, flag = 0;
	int			udp_ep = -1, tcp_ep = -1;
	int			a1, a2, a3, a4;
	char			ut[64], idbuf[64], ipuaddr[64];
	endpoint		teps[32];
	nis_server		tsrv;
	void			(*oldsig)() = NULL; /* old alarm handler */


	nc = NULL;
	td->tv_sec = 0;
	td->tv_usec = 0;

	/*
	 * First check to see if we need to find and address for this
	 * server.
	 */
	if (*uaddr == NULL) {
		if ((srv != NULL) && (thost != NULL)) {
			msg("both timehost and srv pointer used!");
			return (0);
		}
		if (! srv) {
			srv = get_server(thost, &tsrv, teps, 32);
			if (! srv) {
				msg("unable to contruct server data.");
				return (0);
			}
			needfree = 1;	/* need to free data in endpoints */
		}

		nc_handle = (void *) setnetconfig();
		if (! nc_handle) {
			msg("unable to get netconfig info.");
			if (needfree)
				free_eps(teps, tsrv.ep.ep_len);
			return (0);
		}

		ep = srv->ep.ep_val;
		epl = srv->ep.ep_len;

		/* Identify the TCP and UDP endpoints */
		for (i = 0;
			(i < epl) && ((udp_ep == -1) || (tcp_ep == -1)); i++) {
			if (strcasecmp(ep[i].proto, "udp") == 0)
				udp_ep = i;
			if (strcasecmp(ep[i].proto, "tcp") == 0)
				tcp_ep = i;
		}

		while ((nc = getnetconfig(nc_handle)) != NULL) {

			/* Is it a visible transport ? */
			if ((nc->nc_flag & NC_VISIBLE) == 0)
				continue;

			/* Check to see if it is UDP or TCP */
			if (strcasecmp(nc->nc_protofmly, "inet") == 0) {
				if (((udp_ep > -1) &&
				    strcasecmp(nc->nc_proto, "udp") == 0)) {
					useep = &ep[udp_ep];
					useua = ep[udp_ep].uaddr;
					useid = nc->nc_netid;
					break;
				}
				if (((tcp_ep > -1) &&
				    strcasecmp(nc->nc_proto, "tcp") == 0)) {
					useep = &ep[tcp_ep];
					useua = ep[tcp_ep].uaddr;
					useid = nc->nc_netid;
					/* if we've got no choice */
					if (udp_ep == -1)
						break;
				}
			}
			/* Check to see is we talk this protofmly, protocol */
			for (i = 0; i < epl; i++) {
				if ((strcasecmp(nc->nc_protofmly,
						ep[i].family) == 0) &&
				    (strcasecmp(nc->nc_proto,
						ep[i].proto) == 0))
					break;
			}

			/* Was it one of our transports ? */
			if (i == epl)
				continue;	/* No */

			/*
			 * If we found a non IP transport hold it for
			 * now but keep looking.
			 */
			useep = &ep[i]; /* Consider this endpoint */
			useua = ep[i].uaddr;
			useid = nc->nc_netid;
		}

		if (! useep) {
			msg("no acceptable transport endpoints.");
			endnetconfig(nc_handle);
			if (needfree)
				free_eps(teps, tsrv.ep.ep_len);
			return (0);
		} else {
			strncpy(idbuf, useid, 63);
			useid = &idbuf[0];
			endnetconfig(nc_handle);
		}
	}

	if (*netid)
		useid = *netid;

	if ((nc = getnetconfigent(useid)) == NULL) {
		msg("unable to locate netconfig info for netid.");
		if (needfree)
			free_eps(teps, tsrv.ep.ep_len);
		return (0);
	}

	/*
	 * Create a tli address from the uaddr
	 */
	if (*uaddr)
		useua = *uaddr;

	/* Fixup test for NIS+ */
	if (strcasecmp(nc->nc_protofmly, "inet") == 0) {
		sscanf(useua, "%d.%d.%d.%d.", &a1, &a2, &a3, &a4);
		sprintf(ipuaddr, "%d.%d.%d.%d.0.111", a1, a2, a3, a4);
		useua = &ipuaddr[0];
	}

	addr = uaddr2taddr(nc, useua);
	if (! addr) {
		msg("unable to translate uaddr to taddr.");
		freenetconfigent(nc);
		if (needfree)
			free_eps(teps, tsrv.ep.ep_len);
		return (0);
	}

	/*
	 * Create the client handle to rpcbind. Note we always try
	 * version 3 since that is the earliest version that supports
	 * the RPCB_GETTIME call. Also it is the version that comes
	 * standard with SVR4. Since most everyone supports TCP/IP
	 * we could consider trying the rtime call first.
	 */
	clnt = clnt_tli_create(RPC_ANYFD, nc, addr, RPCBPROG, RPCBVERS, 0, 0);
	if (! clnt) {
		msg("unable to create client handle to rpcbind.");
		netdir_free((char *)(addr), ND_ADDR);
		freenetconfigent(nc);
		if (needfree)
			free_eps(teps, tsrv.ep.ep_len);
		return (0);
	}

	tv.tv_sec = 5;
	tv.tv_usec = 0;
	time_valid = 0;
	status = clnt_call(clnt, RPCBPROC_GETTIME, xdr_void, NULL,
					xdr_u_long, (char *)&thetime, tv);
	/*
	 * The only error we check for is anything but success. In
	 * fact we could have seen PROGMISMATCH if talking to a 4.1
	 * machine (pmap v2) or TIMEDOUT if the net was busy.
	 */
	if (status == RPC_SUCCESS)
		time_valid = 1;
	else if (strcasecmp(nc->nc_protofmly, "inet") == 0) {

		/*
		 * free previous address for the RPC binder
		 */
		netdir_free((char *)(addr), ND_ADDR);

		/*
		 * Convert PMAP address into timeservice address
		 * We take advantage of the fact that we "know" what
		 * the universal address looks like for inet transports.
		 *
		 * We also know that the internet timeservice is always
		 * listening on port 37.
		 */
		sscanf(useua, "%d.%d.%d.%d.", &a1, &a2, &a3, &a4);
		sprintf(ut, "%d.%d.%d.%d.0.37", a1, a2, a3, a4);
		addr = uaddr2taddr(nc, ut);
		if (! addr) {
			msg("cannot convert timeservice uaddr to taddr.");
			goto error;
		}

		rtime_fd = t_open(nc->nc_device, O_RDWR, NULL);
		if (rtime_fd == -1) {
			msg("unable to open fd to network.");
			goto error;
		}

		if (t_bind(rtime_fd, NULL, NULL) < 0) {
			msg("unable to bind an endpoint to fd.");
			goto error;
		}

		/*
		 * Now depending on whether or not we're talking to
		 * UDP we set a timeout or not.
		 */
		if (nc->nc_semantics == NC_TPI_CLTS) {
			struct t_unitdata tu_data;
			struct pollfd pfd;
			int res;

			tu_data.addr = *addr;
			tu_data.udata.buf = (char *)&thetime;
			tu_data.udata.len = sizeof (thetime);
			tu_data.udata.maxlen = tu_data.udata.len;
			tu_data.opt.len = 0;
			tu_data.opt.maxlen = 0;
			if (t_sndudata(rtime_fd, &tu_data) == -1) {
				msg("udp : t_sndudata failed.");
				goto error;
			}
			pfd.fd = rtime_fd;
			pfd.events = POLLIN | POLLPRI | POLLRDNORM | POLLRDBAND;

			do {
				res = poll(&pfd, 1, 10000);
			} while (res < 0);
			if ((res <= 0) || (pfd.revents & POLLNVAL))
				goto error;
			if (t_rcvudata(rtime_fd, &tu_data, &flag) < 0) {
				msg("t_rvcdata failed on udp transport.");
				goto error;
			}
			time_valid = 1;
		} else {
			struct t_call sndcall;

			sndcall.addr = *addr;
			sndcall.opt.len = sndcall.opt.maxlen = 0;
			sndcall.udata.len = sndcall.udata.maxlen = 0;

			oldsig = (void (*)())signal(SIGALRM, alarm_hndler);
			saw_alarm = 0; /* global tracking the alarm */
			alarm(20); /* only wait 20 seconds */
			if (t_connect(rtime_fd, &sndcall, NULL) == -1) {
				msg("failed to connect to tcp endpoint.");
				goto error;
			}
			if (saw_alarm) {
				msg("alarm caught it, must be unreachable.");
				goto error;
			}
			if (t_rcv(rtime_fd, (char *)&thetime,
			    sizeof (thetime), &flag) != sizeof (thetime)) {
				if (saw_alarm) {
					/*EMPTY*/
					msg("timed out TCP call.");
				} else {
					/*EMPTY*/
					msg("wrong size of results returned");
				}

				goto error;
			}
			time_valid = 1;
		}
		if (time_valid) {
			thetime = ntohl(thetime);
			thetime = thetime - TOFFSET; /* adjust to UNIX time */
		} else
			thetime = 0;
	}

	gettimeofday(&tv, 0);

error:
	/*
	 * clean up our allocated data structures.
	 */
	if (addr)
		netdir_free((char *)(addr), ND_ADDR);

	if (rtime_fd != -1)
		(void) t_close(rtime_fd);

	if (clnt)
		clnt_destroy(clnt);

	if (nc)
		freenetconfigent(nc);

	if (oldsig) {
		alarm(0);	/* reset that alarm if its outstanding */
		signal(SIGALRM, oldsig);
	}

	/*
	 * note, don't free uaddr strings until after we've made a
	 * copy of them.
	 */
	if (time_valid) {
		if (! *netid) {
			*netid = strdup(useid);
			*uaddr = strdup(useua);
		}

		/* Round to the nearest second */
		tv.tv_sec += (tv.tv_sec > 500000) ? 1 : 0;
		delta = (thetime > tv.tv_sec) ? thetime - tv.tv_sec :
						tv.tv_sec - thetime;
		td->tv_sec = (thetime < tv.tv_sec) ? - delta : delta;
		td->tv_usec = 0;
	} else {
		/*EMPTY*/
		msg("unable to get the server's time.");
	}

	if (needfree)
		free_eps(teps, tsrv.ep.ep_len);

	return (time_valid);
}
