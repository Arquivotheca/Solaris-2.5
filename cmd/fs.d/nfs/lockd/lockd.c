/* Copyright 1991 NCR Corporation - Dayton, Ohio, USA */

#pragma ident	"@(#)lockd.c	1.24	95/09/20 SMI" /* NCR OS2.00.00 1.2 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/
/*
 * 		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information constituting, or
 * derived under license from AT&T's UNIX(r) System V. In addition, portions
 * of such source code were derived from Berkeley 4.3 BSD under license from
 * the Regents of the University of California.
 *
 *
 *
 *  		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 *	Copyright (c) 1986-1989,1994,1995 by Sun Microsystems, Inc
 *	Copyright (c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *	All rights reserved.
 *
 */
/* LM server */

#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <netdir.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <tiuser.h>
#include <stropts.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/netconfig.h>
#include <sys/vnode.h>
#include <sys/stropts.h>
#include <rpc/rpc.h>
#include <nfs/nfs.h>
#include <nfs/nfssys.h>
#include <nfs/lm.h>
#include <stdarg.h>
#include <string.h>
#include <netinet/tcp.h>
#include <libintl.h>

/* definition of transports supported */
#define	OK_TPI_TYPE(_nconf) \
	((_nconf)->nc_semantics == NC_TPI_CLTS || \
	_nconf->nc_semantics == NC_TPI_COTS || \
	_nconf->nc_semantics == NC_TPI_COTS_ORD)

/*
 * Borrow the first unused long field in the netconfig structure
 * to hold lockd state.
 */
#define	nc_closing	nc_unused[0]

#define	BE32_TO_U32(a)	((((u_long)((u_char *)a)[0] & 0xFF) << (u_long)24) | \
		    (((u_long)((u_char *)a)[1] & 0xFF) << (u_long)16) | \
		    (((u_long)((u_char *)a)[2] & 0xFF) << (u_long)8)  | \
		    ((u_long)((u_char *)a)[3] & 0xFF))


struct conn_ind {
	struct conn_ind	* conn_next;
	struct conn_ind	* conn_prev;
	struct t_call	* conn_call;
};


#define	RET_OK	0
#define	RET_ERR	33
/*
 * Function prototypes.
 */
static int	bindit(struct netconfig *, struct netbuf **);
static void	do_one(int nservers, struct netconfig *);
static int	do_all(int nservers);
static void	usage();
static void	poll_for_action();
static	void	remove_from_poll_list(int);
static	void	log_tli_error(char *, int, struct netconfig *);
static int do_poll_cots_action(int fd, int nconf_index);
static int do_poll_clts_action(int fd, struct netconfig *nconf);
static void	add_to_poll_list(int fd, struct netconfig *);
static int reuseaddr(int fd);


/*
 * Define the default maximum number of servers per transport.  This value
 * has not yet been tuned.  It needs to be large enough so that standard
 * tests using blocking locks don't fail.
 */
#define	SERVERS		20

#define	DEBUGVALUE		0
#define	TIMOUT		300	/* One-way RPC connections valid for 5 min. */
#define	RETX_TIMOUT	5	/* Retransmit RPC requests every 5 seconds. */
#define	GRACE		45	/* We have 45 seconds grace period. */
#define	NO_MAX_CONNS_CHECK	-1	/* don't check # of connections */

struct lm_svc_args lsa;
char		*MyName;
/*
 * maximum number of connections allowed.  It is initialized so that
 * checking for the maximum number of connections is disabled
 */
static	int	max_conns_allowed = NO_MAX_CONNS_CHECK;	/* Max connections */
static	int	num_conns;		/* Current number of connections */
static	size_t	end_listen_fds;

static size_t num_fds = 0;		/* number of transport fds opened */
static struct pollfd *poll_array; /* array of poll descriptors for poll() */
static struct netconfig *nconf_polled;	/* matching array transport names */
static int poll_array_size = 0;		/* size of the above two arrays */



/*
 * Called to create and prepare a transport descriptor for in-kernel
 * RPC service.
 * Returns -1 on failure and a valid descriptor on success.
 */
int
transport_open(struct netconfig *nconf)
{
	int fd;
	struct strioctl	strioc;

	if ((nconf == (struct netconfig *) NULL) ||
	    (nconf->nc_device == (char *) NULL)) {
		syslog(LOG_ERR, "bindit:  no netconfig device");
		return (-1);
	}

	/*
	 * Open the transport device.
	 */
	fd = t_open(nconf->nc_device, O_RDWR, (struct t_info *)NULL);
	if (fd == -1) {
#if OK_WITH_TLI
		if (t_errno == TSYSERR && errno == EMFILE &&
		    (nofile_increase(0) == 0)) {
			/* Try again with a higher NOFILE limit. */
			fd = t_open(nconf->nc_device, O_RDWR,
					(struct t_info *)NULL);
		}
#endif
		if (fd == -1) {
			syslog(LOG_ERR, "t_open %s failed:  t_errno %d, %m",
			    nconf->nc_device, t_errno);
			return (-1);
		}
	}

	/*
	 * Pop timod because the RPC module must be as close as possible
	 * to the transport.
	 */
	if (ioctl(fd, I_POP, "timod") < 0) {
		syslog(LOG_ERR, "I_POP of timod failed: %m");
		(void) t_close(fd);
		return (-1);
	}

	if (nconf->nc_semantics == NC_TPI_CLTS) {
		/*
		 * Push rpcmod to filter data traffic to KRPC.
		 */
		if (ioctl(fd, I_PUSH, "rpcmod") < 0) {
			syslog(LOG_ERR, "I_PUSH of rpcmod failed: %m");
			(void) t_close(fd);
			return (-1);
		}
	} else {
		if (ioctl(fd, I_PUSH, "rpcmod") < 0) {
			syslog(LOG_ERR, "I_PUSH of CONS rpcmod failed: %m");
			(void) t_close(fd);
			return (-1);
		}

		strioc.ic_cmd = RPC_SERVER;
		strioc.ic_dp = (char *)0;
		strioc.ic_len = 0;
		strioc.ic_timout = -1;
		/* Tell CONS rpcmod to act like a server stream. */
		if (ioctl(fd, I_STR, &strioc) < 0) {
			syslog(LOG_ERR, "CONS rpcmod set-up ioctl failed: %m");
			(void) t_close(fd);
			return (-1);
		}
	}

	/*
	 * Re-push timod because lockd(1m) will still be doing TLI
	 * operations on the descriptor.
	 */
	if (ioctl(fd, I_PUSH, "timod") < 0) {
		syslog(LOG_ERR, "I_PUSH of timod failed: %m");
		(void) t_close(fd);
		return (-1);
	}

	return (fd);
}

static int
bindit(struct netconfig *nconf, struct netbuf **addr)
{
	int fd;
	struct t_bind * ntb;
	struct t_bind tb;
	struct nd_hostserv hs;
	struct nd_addrlist *addrlist;

	if ((fd = transport_open(nconf)) == -1)
		return (-1);

	hs.h_host = HOST_SELF;
	hs.h_serv = "lockd";
	addrlist = (struct nd_addrlist *)NULL;
	if (netdir_getbyname(nconf, &hs, &addrlist) != 0) {
		syslog(LOG_ERR,
			"netdir_getbyname (transport %s, host/serv %s/%s), %m",
			nconf->nc_netid, hs.h_host, hs.h_serv);
		(void) t_close(fd);
		return (-1);
	}

	if (strcmp(nconf->nc_proto, "tcp") == 0) {
		/*
		 * If we're running over TCP, then set the
		 * SO_REUSEADDR option so that we can bind
		 * to our preferred address even if a previous
		 * lockd has left connections in FIN_WAIT states.
		 * This is somewhat bogus, but otherwise you have
		 * to wait 2 minutes to restart lockd after killing it.
		 */
		if (reuseaddr(fd) == -1) {
			syslog(LOG_WARNING,
			"couldn't set SO_REUSEADDR option on transport");
		}
	}

	if (nconf->nc_semantics == NC_TPI_CLTS)
		tb.qlen = 0;
	else
		tb.qlen = 5;

	/* LINTED pointer alignment */
	ntb = (struct t_bind *) t_alloc(fd, T_BIND, T_ALL);
	if (ntb == (struct t_bind *) NULL) {
		syslog(LOG_ERR, "t_alloc failed:  t_errno %d, %m", t_errno);
		(void) t_close(fd);
		netdir_free((void *)addrlist, ND_ADDRLIST);
		return (-1);
	}

	/*
	 * XXX - what about the space tb->addr.buf points to? This should
	 * be either a memcpy() to/from the buf fields, or t_alloc(fd,T_BIND,)
	 * should't be called with T_ALL.
	 */
	tb.addr = *(addrlist->n_addrs);		/* structure copy */

	if (t_bind(fd, &tb, ntb) == -1) {
		syslog(LOG_ERR, "t_bind failed:  t_errno %d, %m", t_errno);
		(void) t_free((char *) ntb, T_BIND);
		netdir_free((void *)addrlist, ND_ADDRLIST);
		(void) t_close(fd);
		return (-1);
	}

	/* make sure we bound to the right address */
	if (tb.addr.len != ntb->addr.len ||
	    memcmp(tb.addr.buf, ntb->addr.buf, tb.addr.len) != 0) {
		syslog(LOG_ERR, "t_bind to wrong address");
		(void) t_free((char *) ntb, T_BIND);
		netdir_free((void *)addrlist, ND_ADDRLIST);
		(void) t_close(fd);
		return (-1);
	}

	*addr = &ntb->addr;
	netdir_free((void *)addrlist, ND_ADDRLIST);

	return (fd);
}

int
reuseaddr(int fd)
{
	struct t_optmgmt req, resp;
	struct opthdr *opt;
	char reqbuf[128];
	int *ip;

	/* LINTED pointer alignment */
	opt = (struct opthdr *)reqbuf;
	opt->level = SOL_SOCKET;
	opt->name = SO_REUSEADDR;
	opt->len = sizeof (int);

	/* LINTED pointer alignment */
	ip = (int *)&reqbuf[sizeof (struct opthdr)];
	*ip = 1;

	req.flags = T_NEGOTIATE;
	req.opt.len = sizeof (struct opthdr) + opt->len;
	req.opt.buf = (char *)opt;

	resp.flags = 0;
	resp.opt.buf = reqbuf;
	resp.opt.maxlen = sizeof (reqbuf);

	if (t_optmgmt(fd, &req, &resp) < 0 || resp.flags != T_SUCCESS) {
		t_error("t_optmgmt");
		return (-1);
	}
	return (0);
}

static void
log_tli_error(char *tli_name, int fd, struct netconfig *nconf)
{
	int error;

	/*
	 * Save the error code across syslog(), just in case syslog()
	 * gets its own error and, therefore, overwrites errno.
	 */
	error = errno;
	if (t_errno == TSYSERR) {
		syslog(LOG_ERR, "%s(file descriptor %d/transport %s) %m",
			tli_name, fd, nconf->nc_proto);
	} else {
		syslog(LOG_ERR,
			"%s(file descriptor %d/transport %s) TLI error %d",
			tli_name, fd, nconf->nc_proto, t_errno);
	}
	errno = error;
}


int
convert_nconf_to_knconf(struct netconfig *nconf, struct knetconfig *knconf)
{
	struct stat
		sb;

	if (stat(nconf->nc_device, &sb) < 0) {
		syslog(LOG_ERR,
		    "can't find device for transport %s\n", nconf->nc_device);
		return (RET_ERR);
	}
	knconf->knc_semantics = nconf->nc_semantics;
	knconf->knc_protofmly = nconf->nc_protofmly;
	knconf->knc_proto = nconf->nc_proto;
	knconf->knc_rdev = sb.st_rdev;

	return (RET_OK);
}

/*
 * Called to set up a lock manager server over a particular transport.
 */
static void
do_one(int nservers, struct netconfig *nconf)
{
	int sock;
	struct netbuf *retaddr;
	struct knetconfig knconf;

	sock = bindit(nconf, &retaddr);
	if (sock < 0) {
		syslog(LOG_ERR,
		"Cannot establish LM service over %s: bind problem. Exiting.",
			nconf->nc_device);
		exit(1);
	}

	rpcb_unset(NLM_PROG, NLM_VERS, nconf);
	rpcb_unset(NLM_PROG, NLM_VERS2, nconf);
	rpcb_unset(NLM_PROG, NLM_VERS3, nconf);
	rpcb_unset(NLM_PROG, NLM4_VERS, nconf);

	rpcb_set(NLM_PROG, NLM_VERS, nconf, retaddr);
	rpcb_set(NLM_PROG, NLM_VERS2, nconf, retaddr);
	rpcb_set(NLM_PROG, NLM_VERS3, nconf, retaddr);
	rpcb_set(NLM_PROG, NLM4_VERS, nconf, retaddr);

	lsa.fd = sock;
	lsa.n_fmly = strcmp(nconf->nc_protofmly, NC_INET) == 0 ?
		LM_INET : LM_LOOPBACK;
#ifdef LOOPBACK_LOCKING
	if (lsa.n_fmly == LM_LOOPBACK) {
		lsa.n_proto = LM_NOPROTO;	/* need to add this */
	} else
#endif
	lsa.n_proto = strcmp(nconf->nc_proto, NC_TCP) == 0 ?
		LM_TCP : LM_UDP;
	convert_nconf_to_knconf(nconf, &knconf);
	lsa.n_rdev = knconf.knc_rdev;
	lsa.max_threads = nservers;

	/*
	 * only pass information about CLTS transports into kernel at
	 * this time.  COTS must wait for a connection to be proceses
	 */
	(void) signal(SIGSYS, SIG_IGN);
	if (nconf->nc_semantics == NC_TPI_CLTS) {
		/* Don't drop core if the NFS or KLM modules aren't loaded. */
		if (lsa.debug >= 1) {
			printf("%s: (%d)  Calling LM_SVC. netid= %s\n",
				MyName, getpid(), nconf->nc_netid);
		}
		/* Make a lmsvc system call (Not implemented in libc). */
		if (_nfssys(LM_SVC, &lsa) < 0) {
			syslog(LOG_ERR,
	"Cannot establish LM service over <file desc. %d, protocol %s> : %m.",
				sock, nconf->nc_proto);
		exit(1);
		}
	}

	if (lsa.debug >= 1) {
		printf("%s: (%d)  Returned from LM_SVC.\n",
		    MyName, getpid());
	}

	/*
	 * We successfully set up `nservers' copies of the server over this
	 * transport. Add this descriptor to the list of those being polled.
	 * Also add COTS transports to the poll list in order to listen for
	 * connections.
	 */
	add_to_poll_list(sock, nconf);

	if (lsa.debug >= 1) {
		printf("%s: (%d)  Returning from do_one().\n",
			MyName, getpid());
	}
}

/*
 * Set up the LM service over all the supported transports.
 * Returns -1 for failure, 0 for success.
 */
static int
do_all(int nservers)
{
	int nlaunched = 0;
	struct netconfig *nconf;
	NCONF_HANDLE *nc;

	if ((nc = setnetconfig()) == (NCONF_HANDLE *) NULL) {
		syslog(LOG_ERR, "setnetconfig failed: %m");
		return (-1);
	}
	while (nconf = getnetconfig(nc)) {
		if (lsa.debug >= 4) {
			printf(
	"%s: do_all: flag= %d, semantics= %d, protofmly= %s, proto= %s\n",
				MyName, nconf->nc_flag, nconf->nc_semantics,
				nconf->nc_protofmly, nconf->nc_proto);
		}
		if (!(nconf->nc_flag & NC_VISIBLE)) {
			if (lsa.debug >= 4) {
				printf("%s: do_all: skip invisible proto %s\n",
				MyName, nconf->nc_proto);
			}
			continue;
		}

		if (!OK_TPI_TYPE(nconf)) {
			if (lsa.debug >= 4) {
				printf("%s: do_all: skip proto %s\n",
					MyName, nconf->nc_proto);
			}
			continue;
		}

		if (strcmp(nconf->nc_protofmly, NC_INET) != 0 &&
			strcmp(nconf->nc_protofmly, NC_LOOPBACK) != 0) {
			continue;
		}

		do_one(nservers, nconf);
		nlaunched += nservers;
	}
	endnetconfig(nc);

	if (lsa.debug >= 4) {
		printf("%s: do_all: %d servers launched\n",
			MyName, nlaunched);
	}

	return (0);
}

static void
usage()
{
	fprintf(stderr,
	    gettext("usage:  %s [-t timeout] [-g graceperiod]\n"),
		MyName);
	exit(1);
}

main(ac, av)
	int ac;
	char **av;
{
	int i;
	int pid;
	int nservers = SERVERS;
	char *dir = "/";
	struct rlimit rl;

	MyName = *av;

	if (geteuid() != 0) {
		fprintf(stderr, gettext("must be run as root.\n"));
		exit(1);
	}

	lsa.version = LM_SVC_CUR_VERS;
	lsa.debug = DEBUGVALUE;
	lsa.timout = TIMOUT;
	lsa.grace = GRACE;
	lsa.retransmittimeout = RETX_TIMOUT;

	while ((i = getopt(ac, av, "d:g:T:t:")) != EOF)
		switch (i) {
		case 'd':
			(void) sscanf(optarg, "%d", &lsa.debug);
			break;
		case 'g':
			(void) sscanf(optarg, "%d", &lsa.grace);
			if (lsa.grace < 0) {
				lsa.grace = 0;
			}
			break;
		case 'T':
			(void) sscanf(optarg, "%d", &lsa.timout);
			if (lsa.timout < 0) {
				lsa.timout = 0;
			}
			break;
		case 't':
			/* set retransmissions timeout value */
			(void) sscanf(optarg, "%d", &lsa.retransmittimeout);
			if (lsa.retransmittimeout < 0) {
				lsa.retransmittimeout = 0;
			}
			break;
		default:
			usage();
			/* NOTREACHED */
		}

	if (optind < ac) {
		nservers = atoi(av[optind++]);
		if (nservers <= 0) {
			fprintf(stderr,
		    gettext("The number of servers must be greater than 0.\n"));
			exit(1);
		}
	}
	if (optind < ac) {
		usage();
		/* NOTREACHED */
	}
	if (lsa.debug >= 1) {
		printf(
	"%s: debug= %d, timout= %d, retrans= %d, grace= %d, nservers= %d\n\n",
		    MyName, lsa.debug, lsa.timout, lsa.retransmittimeout,
		    lsa.grace, nservers);
	}
	/*
	 * Set current and root dir to server root
	 */
	if (chroot(dir) < 0) {
		fprintf(stderr, gettext("%s: can't chroot to %s: %s.\n"),
			MyName, dir, strerror(errno));
		exit(1);
	}
	if (chdir(dir) < 0) {
		fprintf(stderr, gettext("%s: can't chdir to %s: %s.\n"),
			MyName, dir, strerror(errno));
		exit(1);
	}

	/*
	 * Background
	 */
	if (lsa.debug == 0) {
		pid = fork();
		if (pid < 0) {
			fprintf(stderr, gettext("%s: can't fork: %s.\n"),
				MyName, strerror(errno));
			exit(1);
		}
		if (pid != 0)
			exit(0);
		/*
		 * Close existing file descriptors, open "/dev/null" as
		 * standard input, output, and error, and detach from
		 * controlling terminal.
		 */
		(void) getrlimit(RLIMIT_NOFILE, &rl);
		for (i = 0; i < rl.rlim_max; i++)
			(void) close(i);
		(void) open("/dev/null", O_RDONLY);
		(void) open("/dev/null", O_WRONLY);
		(void) dup(1);
		(void) setsid();
	}

	openlog(MyName, LOG_PID | LOG_NDELAY, LOG_DAEMON);

	if (do_all(nservers) == -1) {
		syslog(LOG_ERR, "Could not start any server threads.");
		return (1);
	}

	end_listen_fds = num_fds;

	/*
	 * Poll for non-data control events on the transport descriptors.
	 */
	poll_for_action();

	/*
	 * If we get here, something failed in poll_for_action().
	 * Exit(1).
	 */
	syslog(LOG_ERR, "poll_for_action: returned");
	return (1);
}

/*
 * poll on the open transport descriptors for events and errors.
 */
static void
poll_for_action()
{
	int nfds;
	int i;

	/*
	 * Keep polling until all transports have been closed. When this
	 * happens, we return. num_fds is decremented in
	 * remove_from_pool_list
	 */
	while (num_fds != 0) {

		nfds = poll(poll_array, num_fds, INFTIM);
		switch (nfds) {
			case 0: continue;

			case -1:
				/*
				 * Some errors from poll could be
				 * due to temporary conditions, and we try to
				 * be robust in the face of them. Other
				 * errors (should never happen in theory)
				 * are fatal (eg. EINVAL, EFAULT).
				 */
				switch (errno) {
					case EAGAIN:
					case ENOMEM:
						(void) sleep(10);
						continue;
					default:
						(void) syslog(LOG_ERR,
						"poll failed: %m. Exiting");
						exit(1);
				}
			default:
				break;
		}

		/*
		 * Go through the poll list looking for events.
		 */
		for (i = 0; (i < num_fds) && nfds > 0; i++) {
			if (poll_array[i].revents) {
				nfds--;
				/*
				 * We have a message, so try to read it.
				 * Record the error return in errno,
				 * so that syslog(LOG_ERR, "...%m")
				 * dumps the corresponding error string.
				 */
				if (nconf_polled[i].nc_semantics ==
					NC_TPI_CLTS) {
					errno = do_poll_clts_action(
						poll_array[i].fd,
						&nconf_polled[i]);
				} else {
					errno = do_poll_cots_action(
						poll_array[i].fd, i);
				}

				if (errno == 0)
					continue;
				/*
				 * Most returned error codes mean that there is
				 * fatal condition which we can only deal with
				 * by closing the transport.
				 */
				if (errno != EAGAIN && errno != ENOMEM) {
					(void) syslog(LOG_ERR,
		"Error (%m) reading descriptor %d/transport %s. Closing it.",
						poll_array[i].fd,
						nconf_polled[i].nc_proto);
					t_close(poll_array[i].fd);
					remove_from_poll_list(poll_array[i].fd);
				} else if (errno == ENOMEM) {
					(void) sleep(5);
				}
			}
		}
	}
	(void) syslog(LOG_ERR,
		"All transports have been closed with errors. Exiting.");

	/* main will return (1), which will result in exit(1) */

}

/*
 * Allocate poll/transport array entries for this descriptor.
 */
static void
add_to_poll_list(fd, nconf)
	int fd;
	struct netconfig *nconf;
{
	/*
	 * If the arrays are full, allocate new ones.
	 */
	if (num_fds == poll_array_size) {
		int tpas;
		struct pollfd *tpa;
		struct netconfig *tnp;

		if (poll_array_size != 0) {
			/*
			 * If arrays have already been actually allocated,
			 * save the references to them, and double the size of
			 * the new arrays.
			 */
			tpas = poll_array_size;
			tpa = poll_array;
			tnp = nconf_polled;
			poll_array_size = 2 * num_fds;
		} else {
			/*
			 * No arrays allocated, so set the size of new arrays to
			 * one entry.
			 */
			tpas = 0;
			poll_array_size = 1;
		}

		/*
		 * Allocate new arrays.
		 */
		poll_array = (struct pollfd *)
			malloc(poll_array_size * sizeof (struct pollfd));
		nconf_polled = (struct netconfig *)
			malloc(poll_array_size * sizeof (struct netconfig));

		/*
		 * Copy the data of the old ones into new arrays, and
		 * free the old ones.
		 */
		if (tpas != 0) {
			(void) memcpy((char *)poll_array, (char *)tpa,
				num_fds * sizeof (struct pollfd));
			(void) memcpy((char *)nconf_polled, (char *)tnp,
				num_fds * sizeof (struct netconfig));

			free((void *)tpa);
			free((void *)tnp);
		}
	}

	/*
	 * Set the descriptor and event list. All possible events are
	 * polled for.
	 */
	poll_array[num_fds].fd = fd;
	poll_array[num_fds].events =
		POLLIN|POLLRDNORM|POLLRDBAND|POLLPRI;
	/*
	 * Copy the transport data over too.
	 */
	nconf_polled[num_fds] = *nconf;

	/*
	 * Set the descriptor to non-blocking. Avoids a race
	 * between data arriving on the stream and then having it
	 * flushed before we can read it.
	 */
	if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
		(void) syslog(LOG_ERR,
	"fcntl(file desc. %d/transport %s, F_SETFL, O_NONBLOCK): %m. Exiting",
			num_fds, nconf->nc_proto);
		exit(1);
	}

	/*
	 * Count this descriptor. Currently, num_fds is never decremented.
	 * An empty slot in the poll_array is marked by setting the fd field
	 * to -1 per the man page for poll(2).
	 */
	++num_fds;
}

static void
remove_from_poll_list(int fd)
{
	int i;
	int num_to_copy;

	for (i = 0; i < num_fds; i++) {
		if (poll_array[i].fd == fd) {
			--num_fds;
			num_to_copy = num_fds - i;
			memcpy((void *)&poll_array[i],
				(void *)&poll_array[i+1],
				num_to_copy * sizeof (struct pollfd));
			memset((void *)&poll_array[num_fds], 0,
				sizeof (struct pollfd));
			memcpy((void *)&nconf_polled[i],
				(void *)&nconf_polled[i+1],
				num_to_copy * sizeof (struct netconfig));
			memset((void *)&nconf_polled[num_fds], 0,
				sizeof (struct netconfig));
			return;
		}
	}
	syslog(LOG_ERR, "attempt to remove nonexistent fd from poll list");

}

/*
 * Called to read and interpret the event on the descriptor.
 * Returns 0 if successful, or a UNIX error code if failure.
 */
static int
do_poll_clts_action(int fd, struct netconfig *nconf)
{
	int ret;
	int flags;

	static struct t_unitdata *unitdata = NULL;
	static struct t_uderr *uderr = NULL;
	static int oldfd = -1;
	int error;
	struct nd_hostservlist *host = NULL;
	struct strbuf ctl[1], data[1];

	/*
	 * We just need to have some space to consume the
	 * message in the event we can't use the TLI interface to do the
	 * job.
	 *
	 * We flush the message using getmsg(). For the control part
	 * we allocate enough for any TPI header plus 32 bytes for address
	 * and options. For the data part, there is nothing magic about
	 * the size of the array, but 256 bytes is probably better than
	 * 1 byte, and we don't expect any data portion anyway.
	 *
	 * If the array sizes are too small, we handle this because getmsg()
	 * (called to consume the message) will return MOREDATA|MORECTL.
	 * Thus we just call getmsg() until it's read the message.
	 */
	char ctlbuf[sizeof (union T_primitives) + 32];
	char databuf[256];

	/*
	 * Only read the descriptor if it is connectionless.
	 */
	if (nconf->nc_semantics == NC_TPI_CLTS) {

		/*
		 * If this is the same descriptor as the last time
		 * do_poll_action was called, we can save some
		 * de-allocation and allocation.
		 */
		if (oldfd != fd) {
			oldfd = fd;

			if (unitdata) {
				t_free((char *)unitdata, T_UNITDATA);
				unitdata = NULL;
			}
			if (uderr) {
				t_free((char *)uderr, T_UDERROR);
				uderr = NULL;
			}
		}

		/*
		 * Allocate a unitdata structure for receiving the event.
		 */
		if (unitdata == NULL) {
			unitdata = (struct t_unitdata *)
				t_alloc(fd, T_UNITDATA, T_ALL);
			if (unitdata == NULL) {
				if (t_errno == TSYSERR) {
					/*
					 * Save the error code across
					 * syslog(), just in case
					 * syslog() gets its own error
					 * and therefore overwrites errno.
					 */
					error = errno;
					(void) syslog(LOG_ERR,
	"t_alloc(file descriptor %d/transport %s, T_UNITDATA) failed: %m",
						fd, nconf->nc_proto);
					return (error);
				} else {
					(void) syslog(LOG_ERR,
"t_alloc(file descriptor %d/transport %s, T_UNITDATA) failed TLI error %d",
						fd, nconf->nc_proto, t_errno);
					goto flush_it;
				}
			}
		}
try_again:
		flags = 0;

		/*
		 * The idea is we wait for T_UNITDATA_IND's. Of course,
		 * we don't get any, because rpcmod filters them out.
		 * However, we need to call t_rcvudata() to let TLI
		 * tell us we have a T_UDERROR_IND.
		 *
		 * algorithm is:
		 * 	t_rcvudata(), expecting TLOOK.
		 * 	t_look(), expecting T_UDERR.
		 * 	t_rcvuderr(), expecting success (0).
		 * 	expand destination address into ASCII,
		 *	and dump it.
		 */

		ret = t_rcvudata(fd, unitdata, &flags);
		if (ret == 0 || t_errno == TBUFOVFLW) {
			(void) syslog(LOG_WARNING,
"t_rcvudata(file descriptor %d/transport %s) got unexpected data, %d bytes",
				fd, nconf->nc_proto, unitdata->udata.len);

			/*
			 * Even though we don't expect any data, in case we do,
			 * keep reading until there is no more.
			 */
			if (flags & T_MORE)
				goto try_again;

			return (0);
		}

		switch (t_errno) {
			case TNODATA:
				return (0);
			case TSYSERR:
				/*
				 * System errors are returned to caller.
				 * Save the error code across
				 * syslog(), just in case
				 * syslog() gets its own error
				 * and therefore overwrites errno.
				 */
				error = errno;
				(void) syslog(LOG_ERR,
			"t_rcvudata(file descriptor %d/transport %s) %m",
					fd, nconf->nc_proto);
				return (error);
			case TLOOK:
				break;
			default:
				(void) syslog(LOG_ERR,
		"t_rcvudata(file descriptor %d/transport %s) TLI error %d",
					fd, nconf->nc_proto, t_errno);
				goto flush_it;
		}

		ret = t_look(fd);
		switch (ret) {
			case 0:
				return (0);
			case -1:
				/*
				 * System errors are returned to caller.
				 */
				if (t_errno == TSYSERR) {
					/*
					 * Save the error code across
					 * syslog(), just in case
					 * syslog() gets its own error
					 * and therefore overwrites errno.
					 */
					error = errno;
					(void) syslog(LOG_ERR,
				"t_look(file descriptor %d/transport %s) %m",
						fd, nconf->nc_proto);
					return (error);
				} else {
					(void) syslog(LOG_ERR,
			"t_look(file descriptor %d/transport %s) TLI error %d",
						fd, nconf->nc_proto, t_errno);
					goto flush_it;
				}
			case T_UDERR:
				break;
			default:
				(void) syslog(LOG_WARNING,
	"t_look(file descriptor %d/transport %s) returned %d not T_UDERR (%d)",
				    fd, nconf->nc_proto, ret, T_UDERR);
		}

		if (uderr == NULL) {
			uderr = (struct t_uderr *)t_alloc(fd, T_UDERROR, T_ALL);
			if (uderr == NULL) {
				if (t_errno == TSYSERR) {
					/*
					 * Save the error code across
					 * syslog(), just in case
					 * syslog() gets its own error
					 * and therefore overwrites errno.
					 */
					error = errno;
					(void) syslog(LOG_ERR,
"t_alloc(file descriptor %d/transport %s, T_UDERROR) failed: %m",
					fd, nconf->nc_proto);
					return (error);
				} else {
				    (void) syslog(LOG_ERR,
"t_alloc(file descriptor %d/transport %s, T_UDERROR) failed TLI error: %d",
					fd, nconf->nc_proto, t_errno);
				    goto flush_it;
				}
			}
		}

		ret = t_rcvuderr(fd, uderr);
		if (ret == 0) {

			/*
			 * Save the datagram error in errno, so that the
			 * %m argument to syslog picks up the error string.
			 */
			errno = uderr->error;

			/*
			 * Log the datagram error, then log the host that
			 * probably triggerred. Cannot log both in the
			 * same transaction because of packet size limitations
			 * in /dev/log.
			 */

			(void) syslog(LOG_WARNING,
"LM response over <file descriptor %d/transport %s> generated error: %m",
				fd, nconf->nc_proto);

			/*
			 * Try to map the client's address back to a
			 * name.
			 */
			ret = netdir_getbyaddr(nconf, &host, &uderr->addr);
			if (ret != -1 && host && host->h_cnt > 0 &&
			    host->h_hostservs) {
				(void) syslog(LOG_WARNING,
"Bad LM response was sent to client with host name: %s; service port: %s",
					host->h_hostservs->h_host,
					host->h_hostservs->h_serv);
			} else {
				int i, j;
				char *buf;
				char *hex = "0123456789abcdef";

				/*
				 * Mapping failed, print the whole thing
				 * in ASCII hex.
				 */
				buf = (char *)malloc(uderr->addr.len * 2 + 1);
				for (i = 0, j = 0; i < uderr->addr.len;
					i++, j += 2) {
				    buf[j] = hex[((uderr->addr.buf[i]) >> 4)
					& 0xf];
				    buf[j+1] = hex[uderr->addr.buf[i] & 0xf];
				}
				buf[j] = '\0';

				(void) syslog(LOG_WARNING,
	"Bad LM response was sent to client with transport address: 0x%s",
					buf);
				free((void *)buf);
			}

			if (ret == 0 && host != NULL)
				netdir_free(host, ND_HOSTSERVLIST);
			return (0);
		}

		switch (t_errno) {
			case TNOUDERR:
				goto flush_it;
			case TSYSERR:
				/*
				 * System errors are returned to caller.
				 * Save the error code across
				 * syslog(), just in case
				 * syslog() gets its own error
				 * and therefore overwrites errno.
				 */
				error = errno;
				(void) syslog(LOG_ERR,
			"t_rcvuderr(file descriptor %d/transport %s) %m",
					fd, nconf->nc_proto);
				return (error);
			default:
				(void) syslog(LOG_ERR,
		"t_rcvuderr(file descriptor %d/transport %s) TLI error %d",
					fd, nconf->nc_proto, t_errno);
				goto flush_it;
		}
	}

flush_it:

	/*
	 * If we get here, then we could not cope with whatever message
	 * we attempted to read, so flush it. If we did read a message,
	 * and one isn't present, that is all right, because fd is in
	 * nonblocking mode.
	 */
	(void) syslog(LOG_ERR,
	"Flushing one input message from <file descriptor %d/transport %s>",
		fd, nconf->nc_proto);

	/*
	 * Read and discard the message. Do this this until there is
	 * no more control/data in the message or until we get an error.
	 */
	do {
		ctl->maxlen = sizeof (ctlbuf);
		ctl->buf = ctlbuf;
		data->maxlen = sizeof (databuf);
		data->buf = databuf;
		flags = 0;
		ret = getmsg(fd, ctl, data, &flags);
		if (ret == -1)
			return (errno);
	} while (ret != 0);

	return (0);
}

static void
conn_close_oldest(void)
{
	int fd;
	int i1;

	/*
	 * Find the oldest connection that is not already in the
	 * process of shutting down.
	 */
	for (i1 = end_listen_fds; /* no conditional expression */; i1++) {
		if (i1 >= num_fds)
			return;
		if (nconf_polled[i1].nc_closing == 0)
			break;
	}
	syslog(LOG_WARNING, "too many connections (%d), releasing oldest (%d)",
		num_conns, poll_array[i1].fd);
	fd = poll_array[i1].fd;
	if (nconf_polled[i1].nc_semantics == NC_TPI_COTS) {
		/*
		 * For politeness, send a T_DISCON_REQ to the transport
		 * provider.  We close the stream anyway.
		 */
		(void) t_snddis(fd, (struct t_call *)0);
		num_conns--;
		remove_from_poll_list(fd);
		t_close(fd);
	} else {
		/*
		 * For orderly release, we do not close the stream
		 * until the T_ORDREL_IND arrives to complete
		 * the handshake.
		 */
		if (t_sndrel(fd) == 0)
			nconf_polled[i1].nc_closing = 1;
	}
}

static boolean_t
conn_get(int fd, struct netconfig *nconf, struct conn_ind **connp)
{
	struct conn_ind	*conn;
	struct conn_ind	*next_conn;

	conn = (struct conn_ind *) malloc(sizeof (*conn));
	if (conn == NULL) {
		syslog(LOG_ERR, "malloc for listen indication failed");
		return (FALSE);
	}

	/* LINTED pointer alignment */
	conn->conn_call = (struct t_call *) t_alloc(fd, T_CALL, T_ALL);
	if (conn->conn_call == NULL) {
		free((char *)conn);
		log_tli_error("t_alloc", fd, nconf);
		return (FALSE);
	}

	if (t_listen(fd, conn->conn_call) == -1) {
		log_tli_error("t_listen", fd, nconf);
		t_free((char *)conn->conn_call, T_CALL);
		free((char *)conn);
		return (FALSE);
	}

	if (conn->conn_call->udata.len > 0) {
		syslog(LOG_WARNING,
	"rejecting inbound connection(%s) with %d bytes of connect data",
			nconf->nc_proto, conn->conn_call->udata.len);

		conn->conn_call->udata.len = 0;
		t_snddis(fd, conn->conn_call);
		t_free((char *)conn->conn_call, T_CALL);
		free((char *)conn);
		return (FALSE);
	}

	if ((next_conn = *connp) != NULL) {
		next_conn->conn_prev->conn_next = conn;
		conn->conn_next = next_conn;
		conn->conn_prev = next_conn->conn_prev;
		next_conn->conn_prev = conn;
	} else {
		conn->conn_next = conn;
		conn->conn_prev = conn;
		*connp = conn;
	}
	return (TRUE);
}

static int
discon_get(int fd, struct netconfig *nconf, struct conn_ind **connp)
{
	struct conn_ind	*conn;
	struct t_discon	discon;

	discon.udata.buf = (char *)0;
	discon.udata.maxlen = 0;
	if (t_rcvdis(fd, &discon) == -1) {
		log_tli_error("t_rcvdis", fd, nconf);
		return (-1);
	}

	conn = *connp;
	if (conn == NULL)
		return (0);

	do {
		if (conn->conn_call->sequence == discon.sequence) {
			if (conn == *connp && conn->conn_next == conn)
				*connp = (struct conn_ind *)0;
			else {
				conn->conn_next->conn_prev = conn->conn_prev;
				conn->conn_prev->conn_next = conn->conn_next;
			}
			break;
		}
		conn = conn->conn_next;
	} while (conn != *connp);

	return (0);
}

static void
cots_listen_event(int fd, int nconf_index)
{
	struct t_call *call;
	struct conn_ind	*conn;
	struct conn_ind	*conn_head;
	int event;
	struct netconfig *nconf = &nconf_polled[nconf_index];
	struct knetconfig knconf;
	int new_fd;
	struct t_optmgmt req, resp;
	struct opthdr *opt;
	char reqbuf[128];

	conn_head = (struct conn_ind *)0;
	conn_get(fd, nconf, &conn_head);

	while ((conn = conn_head) != NULL) {
		conn_head = conn->conn_next;
		if (conn_head == conn)
			conn_head = (struct conn_ind *)0;
		else {
			conn_head->conn_prev = conn->conn_prev;
			conn->conn_prev->conn_next = conn_head;
		}
		call = conn->conn_call;
		free((char *)conn);

		/*
		 * If we have already accepted the maximum number of
		 * connections allowed on the command line, then drop
		 * the oldest connection (for any protocol) before
		 * accepting the new connection.  Unless explicitly
		 * set on the command line, max_conns_allowed is
		 * NO_MAX_CONNS_CHECK
		 */
		if ((max_conns_allowed != NO_MAX_CONNS_CHECK) &&
			(num_conns >= max_conns_allowed))
			conn_close_oldest();

		/*
		 * Create a new transport endpoint for the same proto as
		 * the listener.
		 */
		new_fd = transport_open(nconf);
		if (new_fd == -1) {
			call->udata.len = 0;
			t_snddis(fd, call);
			t_free((char *)call, T_CALL);
			continue;
		}

		/* Bind to a generic address/port for the accepting stream. */
		if (t_bind(new_fd, (struct t_bind *)NULL,
		    (struct t_bind *)NULL) == -1) {
			log_tli_error("t_bind", new_fd, nconf);
			call->udata.len = 0;
			t_snddis(fd, call);
			t_free((char *)call, T_CALL);
			t_close(new_fd);
			continue;
		}

		while (t_accept(fd, new_fd, call) == -1) {
			if (t_errno != TLOOK) {
				log_tli_error("t_accept", fd, nconf);
				call->udata.len = 0;
				t_snddis(fd, call);
				t_free((char *)call, T_CALL);
				t_close(new_fd);
				goto do_next_conn;
			}
			while (event = t_look(fd)) {
				switch (event) {
				case T_LISTEN:
					conn_get(fd, nconf, &conn_head);
					continue;
				case T_DISCONNECT:
					discon_get(fd, nconf, &conn_head);
					continue;
				default:
					syslog(LOG_ERR,
			"unexpected event 0x%x during accept processing (%s)",
						event, nconf->nc_proto);
					call->udata.len = 0;
					t_snddis(fd, call);
					t_free((char *)call, T_CALL);
					t_close(new_fd);
					goto do_next_conn;
				}
			}
		}

		if (strcmp(nconf->nc_proto, "tcp") == 0) {
			/*
			 * Disable the Nagle algorithm on TCP connections.
			 */
			/* LINTED pointer alignment */
			opt = (struct opthdr *)reqbuf;
			opt->level = IPPROTO_TCP;
			opt->name = TCP_NODELAY;
			opt->len = sizeof (int);

			/* LINTED pointer alignment */
			*(int *)((char *)opt + sizeof (*opt)) = 1;

			req.flags = T_NEGOTIATE;
			req.opt.len = sizeof (*opt) + opt->len;
			req.opt.buf = (char *)opt;

			resp.flags = 0;
			resp.opt.buf = reqbuf;
			resp.opt.maxlen = sizeof (reqbuf);

			if (t_optmgmt(fd, &req, &resp) < 0 ||
			    resp.flags != T_SUCCESS) {
				syslog(LOG_ERR,
		"couldn't set NODELAY option for proto %s: t_errno = %d, %m",
					nconf->nc_proto, t_errno);
			}
		}

		/* Tell KRPC about the new stream. */
		lsa.n_fmly = strcmp(nconf->nc_protofmly, NC_INET) == 0 ?
			LM_INET : LM_LOOPBACK;
		lsa.n_proto = strcmp(nconf->nc_proto, NC_TCP) == 0 ?
			LM_TCP : LM_UDP;
		convert_nconf_to_knconf(nconf, &knconf);
		lsa.n_rdev = knconf.knc_rdev;
		lsa.fd = new_fd;
		if (_nfssys(LM_SVC, &lsa) < 0) {
			syslog(LOG_ERR,
"Cannot establish LM service over <fd. %d, protocol %s> : %m. Continuing",
				new_fd, nconf->nc_proto);
			break;
		}

		t_free((char *)call, T_CALL);

		/*
		 * Poll on the new descriptor so that we get disconnect
		 * and orderly release indications.
		 */
		num_conns++;
		add_to_poll_list(new_fd, nconf);

		/* Reset nconf in case it has been moved. */
		nconf = &nconf_polled[nconf_index];
do_next_conn:;
	}
}

static int
do_poll_cots_action(int fd, int nconf_index)
{
	char buf[256];
	int event;
	int i1;
	int flags;
	struct netconfig *nconf = &nconf_polled[nconf_index];

	while (event = t_look(fd)) {
		switch (event) {
		case T_LISTEN:
			cots_listen_event(fd, nconf_index);
			break;

		case T_DATA:
			/*
			 * Receive a private notification from CONS rpcmod.
			 */
			i1 = t_rcv(fd, buf, sizeof (buf), &flags);
			if (i1 == -1) {
				syslog(LOG_ERR, "t_rcv failed");
				break;
			}
			if (i1 < sizeof (int))
				break;
			i1 = BE32_TO_U32(buf);
			if (i1 == 1 || i1 == 2) {
				/*
				 * This connection has been idle for too long,
				 * so release it as politely as we can.
				 */
				if (nconf->nc_semantics == NC_TPI_COTS) {
					(void) t_snddis(fd, (struct t_call *)0);
					goto fdclose;
				}
				/*
				 * For NC_TPI_COTS_ORD, the stream is closed
				 * and removed from the poll list when the
				 * T_ORDREL is received from the provider.  We
				 * don't wait for it here because it may take
				 * a while for the transport to shut down.
				 */
				if (t_sndrel(fd) == -1) {
					syslog(LOG_ERR,
					"unable to send orderly release %m");
				}
				nconf->nc_closing = 1;
			} else
				syslog(LOG_ERR,
				"unexpected event from CONS rpcmod %d", i1);
			break;

		case T_ORDREL:
			/* Perform an orderly release. */
			if (t_rcvrel(fd) == 0)
				(void) t_sndrel(fd);
			else
				log_tli_error("t_rcvrel", fd, nconf);
			goto fdclose;

		case T_DISCONNECT:
			if (t_rcvdis(fd, (struct t_discon *)NULL) == -1)
				log_tli_error("t_rcvdis", fd, nconf);
			goto fdclose;

		case T_ERROR:
			goto fdclose;

		default:
			syslog(LOG_ERR,
	"unexpected TLI event (0x%x) on connection-oriented transport(%s,%d)",
				event, nconf->nc_proto, fd);
fdclose:
			num_conns--;
			remove_from_poll_list(fd);
			t_close(fd);
			return (0);
		}
	}

	return (0);
}
