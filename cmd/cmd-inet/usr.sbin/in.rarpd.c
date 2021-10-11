/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)in.rarpd.c	1.10	94/05/16 SMI"

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * 		PROPRIETARY NOTICE (Combined)
 * 
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 * 
 * 
 * 
 * 		Copyright Notice 
 * 
 * Notice of copyright on this source code product does not indicate 
 * publication.
 * 
 * 	(c) 1986,1987,1988.1989  Sun Microsystems, Inc
 * 	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 * 	          All rights reserved.
 *  
 */


/*
 * rarpd.c  Reverse-ARP server.
 * Refer to RFC 903 "A Reverse Address Resolution Protocol".
 */

#include	<sys/types.h>
#include	<sys/socket.h>
#include	<sys/sockio.h>
#include	<net/if.h>
#include	<sys/ethernet.h>
#include	<netinet/arp.h>
#include	<netinet/in.h>
#include	<sys/stropts.h>
#include	<sys/dlpi.h>
#include	<stdio.h>
#include	<ctype.h>
#include	<fcntl.h>
#include	<syslog.h>
#include	<dirent.h>
#include	<signal.h>
#include	<netdb.h>

#define	BOOTDIR		"/tftpboot"	/* boot files directory */
#define	DEVDIR		"/dev"		/* devices directory */
#define	DEVIP		"/dev/ip"	/* path to ip driver */
#define	DEVARP		"/dev/arp"	/* path to arp driver */

#define BUFSIZE		2048		/* max receive frame length */
#define	MAXPATHL	128		/* max path length */
#define	MAXHOSTL	128		/* max host name length */
#define	MAXADDRL	128		/* max address length */
#if 	defined(SYSV) && !defined(SIOCGIFCONF_FIXED)
#define MAXIFS		32	/* results in a bufsize of 1024 */
#else
#define MAXIFS		256
#endif

/*
 * XXX
 * DLPI Provider Address Format assumed.
 */
struct	dladdr {
	struct	ether_addr	dl_phys;
	u_short	dl_sap;
};

#define	IPADDRL		sizeof (struct in_addr)

static int
	received,	/* total good packets read */
	bad,		/* packets not understood */
	unknown,	/* unknown ether -> ip address mapping */
	processed,	/* answer known and sent */
	delayed,	/* answer was delayed before sending */
	weird;		/* unexpected, yet valid */

static struct ether_addr my_etheraddr;

static int	dlfd;			/* datalink provider Stream */
static long	my_ipaddr;		/* network order */
static char	*cmdname;		/* command name from av[0] */
static int	dflag = 0;		/* enable diagnostics */
static int	aflag = 0;		/* start rarpd on all interfaces */
static int	d_fd;			/* delay pipe file descriptor */
static char *alarmmsg;			/* alarm() error message */

static u_long	if_netmask;		/* host order */
static u_long	if_ipaddr;		/* host order */
static u_long	if_netnum;		/* host order, with subnet */

static int	ipalloc();		/* allocate IP address */

static void	rarp_request();		/* RARP request handler */
static int	sigchld();		/* child signal handler */

extern char	*malloc(), *inet_ntoa();
extern char *strcpy(), *strncpy();
extern struct dirent *readdir();
extern u_long	inet_addr();
extern struct hostent	*gethostbyname();
extern char *ether_ntoa();
void sigalarm();

struct	rarpreply {
	struct	timeval	tv;
	struct	ether_addr	dest;
	struct	ether_arp	earp;
};

extern	int	optind;
extern	char	*optarg;

main(ac, av)
int ac;
char *av[];
{
	struct in_addr	addr;
	struct hostent	*hp;
	char		*hostname;
	int		c;
	int		fd;
	struct ifreq	*reqbuf;
	struct ifreq	*ifr;
	int		n;
	char		*device;
	char		devbuf[MAXPATHL];
	int		unit;
	struct ifconf	ifconf;
	int numifs;
	unsigned bufsize;

	cmdname = av[0];

	while ((c = getopt(ac, av, "ad")) != -1)
		switch (c) {
		case 'a':
			aflag = 1;
			break;

		case 'd':
			dflag = 1;
			break;

		default:
			usage();
		}

	if (aflag) {
		if ((ac - optind) > 2)
			usage ();

		/*
		 * Open the IP provider.
		 */
		if ((fd = open(DEVIP, 0)) < 0)
			syserr(DEVIP);

		/*
		 * Ask IP for the list of configured interfaces.
		 */
#ifdef SIOCGIFNUM
		if (_ioctl(fd, SIOCGIFNUM, (char *)&numifs) < 0) {
			numifs = MAXIFS;
		}
#else
		numifs = MAXIFS;
#endif
		bufsize = numifs * sizeof(struct ifreq);
		reqbuf = (struct ifreq *)malloc(bufsize);
		if (reqbuf == NULL) {
			error("out of memory");
		}
		ifconf.ifc_len = bufsize;
		ifconf.ifc_buf = (caddr_t)reqbuf;
		if (_ioctl(fd, SIOCGIFCONF, (char*) &ifconf) < 0)
			syserr("SIOCGIFCONF");
/*
		if (strioctl(fd, SIOCGIFCONF, -1, sizeof (reqbuf),
			(char*) reqbuf) < 0)
			syserr("SIOCGIFCONF");
*/

		/*
		 * Start a rarpd service on each interface.
		 */
		for (ifr = ifconf.ifc_req; ifconf.ifc_len > 0;
		     ifr++, ifconf.ifc_len-=sizeof(struct ifreq)) {
			if (ioctl (fd, SIOCGIFFLAGS, (char *) ifr) < 0) {
				syserr ("ioctl SIOCGIFFLAGS");
				exit (1);
			}
			if ((ifr->ifr_flags & IFF_LOOPBACK) ||
			    !(ifr->ifr_flags & IFF_BROADCAST) ||
			    !(ifr->ifr_flags & IFF_UP) ||
			    (ifr->ifr_flags & IFF_NOARP) ||
			    (ifr->ifr_flags & IFF_POINTOPOINT))
				continue;

			getdevice(ifr->ifr_name, devbuf);
			device = devbuf;
			unit = getunit(ifr->ifr_name); 
			do_rarp(device, unit);
		}
		(void) free((char *)reqbuf);
	} else switch (ac - optind) {
		case 2:
			device = av[optind];
			unit = atoi(av[optind + 1]);
			do_rarp(device, unit);
			break;
			
		default:
			usage();
	}

	exit(0);
	/* NOTREACHED */
}

/*
 * Pick out leading alphabetic part of string 's'.
 */
getdevice(s, dev)
u_char	*s;
u_char	*dev;
{
	while (isalpha(*s))
		*dev++ = *s++;
	*dev = '\0';
}

/*
 * Pick out trailing numeric part of string 's' and return int.
 */
getunit(s)
char	*s;
{
	char	intbuf[128];
	char	*p = intbuf;

	while (isalpha(*s))
		s++;
	while (isdigit(*s))
		*p++ = *s++;
	*p = '\0';
	return (atoi(intbuf));
}

do_rarp(device, unit)
char *device;
int	unit;
{
	struct strbuf ctl, data;
	char	ctlbuf[BUFSIZE];
	char	databuf[BUFSIZE];
	struct	timeval	tv;
	struct	rarpreply	rr;
	char	*cause;
	struct	ether_arp	ans;
	struct ether_addr	shost;
	int	flags;
	union	DL_primitives	*dlp;
	struct	dladdr	*dladdrp;
	int	delay_fd[2];
	struct	in_addr	addr;
	int	i;

	/*
	 * Open datalink provider and get our ethernet address.
	 */
	dlfd = rarp_open(device, unit, ETHERTYPE_REVARP,
		&my_etheraddr);

	/*
	 * Get our IP address and netmask from directory service.
	 */
	get_ifdata(device, unit, &if_ipaddr, &if_netmask);

	/* 
	 * Use IP address of the interface.
	 */
	if_netnum = if_ipaddr & if_netmask;
	if_ipaddr = htonl (if_ipaddr);
	memcpy((char *)&my_ipaddr, (char *) &if_ipaddr, IPADDRL);
	if_ipaddr = ntohl(if_ipaddr);

	if (dflag) {
		memcpy((caddr_t) &addr.s_addr, (caddr_t) &my_ipaddr,
			IPADDRL);
		debug("starting rarp service on device %s address %s",
			 device, inet_ntoa(addr));
	}
		
	if (!dflag) {
		/*
		 * Background
		 */
		while (dlfd < 3) 
			dlfd = dup(dlfd);
		switch (fork ()) {
			case -1:	/* error */
				syserr("fork");

			case 0:		/* child */
				break;

			default:	/* parent */
				return;
		}
		for (i = 0; i < 3; i++) {
			(void) close(i);
		}
		(void) open("/", O_RDONLY, 0);
		(void) dup2(0, 1);
		(void) dup2(0, 2);
		/*
		 * Detach terminal
		 */
		if (setsid() < 0)
			syserr("setsid");
	}

	(void) openlog(cmdname, LOG_PID, LOG_DAEMON);

	/*
	 * Fork off a delayed responder which uses a pipe.
	 */
	if (pipe(delay_fd))
		syserr("pipe");

	switch (fork()) {
		case -1:
			syserr("fork");
			break;

		case 0:	/* child */
			(void) close(delay_fd[1]);	/* no writing */
			d_fd = delay_fd[0];
			while (1) {
				if (read(d_fd, &rr, sizeof (rr)) != sizeof (rr))
					syserr("read");
				received++;
				gettimeofday(&tv, NULL);
				if (tv.tv_sec < rr.tv.tv_sec)
					sleep(rr.tv.tv_sec - tv.tv_sec);
				if (rarp_write(dlfd, &rr) < 0)
					error("rarp_write error");
			}
			break;

		default:	/* parent */
			(void) close(delay_fd[0]);	/* no reading */
			d_fd = delay_fd[1];
			break;
	}

	/*
	 * read RARP packets and respond to them.
	 */
	while (1) {
		ctl.len = 0;
		ctl.maxlen = BUFSIZE;
		ctl.buf = ctlbuf;
		data.len = 0;
		data.maxlen = BUFSIZ;
		data.buf = databuf;
		flags = 0;

		if (getmsg(dlfd, &ctl, &data, &flags) < 0)
			syserr("getmsg");

		/*
		 * Validate DL_UNITDATA_IND.
		 */

		dlp = (union DL_primitives*) ctlbuf;

		memcpy((char *) &ans, databuf, sizeof (struct ether_arp));

		cause = NULL;
		if (ctl.len == 0)
			cause = "missing control part of message";
		else if (ctl.len < 0)
			cause = "short control part of message";
		else if (dlp->dl_primitive != DL_UNITDATA_IND)
			cause = "not unitdata_ind";
		else if (flags & MORECTL)
			cause = "MORECTL flag";
		else if (flags & MOREDATA)
			cause = "MOREDATA flag";
		else if (ctl.len < DL_UNITDATA_IND_SIZE)
			cause = "short unitdata_ind";
		else if (data.len < sizeof (struct ether_arp))
			cause = "short ether_arp";
		else if (ans.arp_hrd != htons(ARPHRD_ETHER))
			cause = "hrd";
		else if (ans.arp_pro != htons(ETHERTYPE_IP))
			cause = "pro";
		else if (ans.arp_hln != ETHERADDRL)
			cause = "hln";
		else if (ans.arp_pln != IPADDRL)
			cause = "pln";
		if (cause) {
			if (dflag)
				debug("receive check failed: cause: %s",
					cause);
			bad++;
			continue;
		}

		/*
		 * Good request.
		 */
		received++;

		/*
		 * Pick out the ethernet source address of this RARP request.
		 */
		dladdrp = (struct dladdr *) ((char *) ctlbuf
			+ dlp->unitdata_ind.dl_src_addr_offset);
		memcpy((caddr_t) &shost, (caddr_t) &dladdrp->dl_phys,
		       ETHERADDRL);


		/*
		 * Handle the request.
		 */
		switch (ntohs(ans.arp_op)) {
		case REVARP_REQUEST:
			rarp_request(&ans, &shost);
			break;

		case ARPOP_REQUEST:
			arp_request(&ans, &shost);
			break;

		case REVARP_REPLY:
			if (dflag)
				debug("REVARP_REPLY ignored");
			break;

		case ARPOP_REPLY:
			if (dflag)
				debug("ARPOP_REPLY ignored");
			break;

		default:
			if (dflag)
				debug("unknown opcode 0x%x", ans.arp_op);
			bad++;
			break;
		}
	}
}

/* 
 * Reverse address determination and allocation code.
 */
static void
rarp_request(rp, shostp)
struct ether_arp *rp;
struct ether_addr	*shostp;
{
	u_long tpa;
	struct	rarpreply	rr;

	if (dflag) {
		debug("RARP_REQUEST for %s",
			 ether_ntoa(rp->arp_tha.ether_addr_octet));
	}
    
	/* 
	 * third party lookups are rare and wonderful
	 */
	if (memcmp((char *) &rp->arp_sha, (char *) &rp->arp_tha, ETHERADDRL) || 
	    memcmp((char *) &rp->arp_sha, (char *) shostp, ETHERADDRL)) {
		if (dflag)
			debug("weird (3rd party lookup)");
		weird++;
	}

	/*
	 * fill in given parts of reply packet
	 */
	memcpy((char *) &rp->arp_sha, (char *) &my_etheraddr, ETHERADDRL);
	memcpy((char *) rp->arp_spa, (char *) &my_ipaddr, IPADDRL);

	/*
	 * If a good address is stored in our lookup tables, return it
	 * immediately or after a delay.  Store it our kernel's ARP cache.
	 */
	if (get_ipaddr(rp->arp_tha, rp->arp_tpa)) {
		unknown++;
		return;
	}

	add_arp((char *) rp->arp_tpa, (char *) &rp->arp_tha);

	rp->arp_op = htons(REVARP_REPLY);

	if (dflag) {
		struct in_addr addr;

		memcpy((char *) &addr, rp->arp_tpa, IPADDRL);
		debug("good lookup, maps to %s", inet_ntoa(addr));
	}

	/*
	 * Create rarpreply structure.
	 */
	gettimeofday(&rr.tv, NULL);
	rr.tv.tv_sec += 3;	/* delay */
	memcpy((caddr_t) &rr.dest, (caddr_t) shostp, ETHERADDRL);
	memcpy((caddr_t) &rr.earp, (caddr_t) rp, sizeof (struct ether_arp));

	/*
	 * If this is diskless and we're not its bootserver, let the
	 * bootserver reply first by delaying a while.
	 */
	memcpy((char *) &tpa, (char *) rp->arp_tpa, IPADDRL);
	if (mightboot(ntohl(tpa))) {
		if (rarp_write(dlfd, &rr) < 0)
			syslog(LOG_ERR, "Bad rarp_write:  %m");
		if (dflag)
			debug("immediate reply sent");
	} else {
		delayed++;
		if (write(d_fd, (char *) &rr, sizeof (rr)) != sizeof (rr))
			if (dflag)
				debug("error writing to pipe");
	}

	processed++;
	return;
}

/*
 * Download an ARP entry into our kernel.
 */
static
add_arp(ip, eap)
char *ip;  /* IP address pointer */
struct	ether_addr	*eap;
{
	struct arpreq ar;
	struct sockaddr_in	*sin;
	int	fd;

	/*
	 * Common part of query or set
	 */
	memset((caddr_t)&ar, sizeof (ar), '\0');
	ar.arp_pa.sa_family = AF_INET;
	sin = (struct sockaddr_in *)&ar.arp_pa;
	memcpy((char *) &sin->sin_addr, ip, IPADDRL);

	/*
	 * Open the IP provider.
	 */
	if ((fd = open(DEVARP, 0)) < 0)
		syserr(DEVARP);

	/*
	 * Set the entry
	 */
	memcpy(ar.arp_ha.sa_data, (char*) eap, ETHERADDRL);
	ar.arp_flags = 0;
	strioctl(fd, SIOCDARP, -1, sizeof (struct arpreq), (char *) &ar);
	if (strioctl(fd, SIOCSARP, -1, sizeof (struct arpreq), (char *) &ar) < 0)
		syserr("SIOCSARP");

	(void) close(fd);
}

/*
 * The RARP spec says we must be able to process ARP requests,
 * even through the packet type is RARP.  Let's hope this feature
 * is not heavily used.
 */
static
arp_request(rp, shostp)
struct ether_arp *rp;
struct ether_addr	*shostp;
{
	struct	rarpreply	rr;

	if (dflag)
		debug("ARPOP_REQUEST");

	if (memcmp((char *) &my_ipaddr, (char *) rp->arp_tpa, IPADDRL))
		return;

	rp->arp_op = ARPOP_REPLY;
	memcpy((char *) &rp->arp_sha, (char *) &my_etheraddr, ETHERADDRL);
	memcpy((char *) rp->arp_spa, (char *) &my_ipaddr, IPADDRL);
	memcpy((char *) &rp->arp_tha, (char *) &my_etheraddr, ETHERADDRL);

	add_arp((char *) rp->arp_tpa, &rp->arp_tha);

	/*
	 * Create rarp reply structure.
	 */
	memcpy((caddr_t) &rr.dest, (caddr_t) shostp, ETHERADDRL);
	memcpy((caddr_t) &rr.earp, (caddr_t) rp, sizeof (struct ether_arp));

	if (rarp_write(dlfd, &rr) < 0)
		error("rarp_write error");
}

/*
 * OPEN the datalink provider device, ATTACH to the unit,
 * and BIND to the revarp type.
 * Return the resulting descriptor.
 */
static int
rarp_open(device, unit, type, etherp)
char *device;
int	unit;
u_short type;
struct ether_addr	*etherp;
{
	register int fd;
	char	path[MAXPATHL];
	union DL_primitives *dlp;
	char	buf[BUFSIZE];
	struct	strbuf	ctl, data;
	int	flags;
	struct	ether_addr	*eap;

	/*
	 * Prefix the device name with "/dev/" if it doesn't
	 * start with a "/" .
	 */
	if (*device == '/')
		(void) sprintf(path, "%s", device);
	else
		(void) sprintf(path, "%s/%s", DEVDIR, device);

	/*
	 * Open the datalink provider.
	 */
	if ((fd = open(path, O_RDWR)) < 0)
		syserr(path);

	/*
	 * Issue DL_INFO_REQ and check DL_INFO_ACK for sanity.
	 */
	dlp = (union DL_primitives*) buf;
	dlp->info_req.dl_primitive = DL_INFO_REQ;

	ctl.buf = (char *) dlp;
	ctl.len = DL_INFO_REQ_SIZE;

	if (putmsg(fd, &ctl, NULL, 0) < 0)
		syserr("putmsg");

	(void) signal(SIGALRM, sigalarm);

	alarmmsg = "DL_INFO_REQ failed: timeout waiting for DL_INFO_ACK";
	(void) alarm(10);

	ctl.buf = (char *) dlp;
	ctl.len = 0;
	ctl.maxlen = BUFSIZE;
	flags = 0;
	if (getmsg(fd, &ctl, NULL, &flags) < 0)
		syserr("getmsg");

	(void) alarm(0);
	(void) signal(SIGALRM, SIG_DFL);

	/*
	 * Validate DL_INFO_ACK reply.
	 */
	if (ctl.len < sizeof (ulong))
		error("DL_INFO_REQ failed:  short reply to DL_INFO_REQ");

	if (dlp->dl_primitive != DL_INFO_ACK)
		error("DL_INFO_REQ failed:  dl_primitive 0x%x received",
			dlp->dl_primitive);

	if (ctl.len < DL_INFO_ACK)
		error("DL_INFO_REQ failed:  short info_ack:  %d bytes",
			ctl.len);

	if (dlp->info_ack.dl_version != DL_VERSION_2)
		error("DL_INFO_ACK:  incompatible version:  %d",
			dlp->info_ack.dl_version);

	if (dlp->info_ack.dl_sap_length != -2)
		error("DL_INFO_ACK:  incompatible dl_sap_length:  %d",
			dlp->info_ack.dl_sap_length);

	if ((dlp->info_ack.dl_service_mode & DL_CLDLS) == 0)
		error("DL_INFO_ACK:  incompatible dl_service_mode:  0x%x",
			dlp->info_ack.dl_service_mode);


	/*
	 * Issue DL_ATTACH_REQ.
	 */
	dlp = (union DL_primitives*) buf;
	dlp->attach_req.dl_primitive = DL_ATTACH_REQ;
	dlp->attach_req.dl_ppa = unit;

	ctl.buf = (char *) dlp;
	ctl.len = DL_ATTACH_REQ_SIZE;

	if (putmsg(fd, &ctl, NULL, 0) < 0)
		syserr("putmsg");

	(void) signal(SIGALRM, sigalarm);
	alarmmsg = "DL_ATTACH_REQ failed: timeout waiting for DL_OK_ACK";

	(void) alarm(10);

	ctl.buf = (char *) dlp;
	ctl.len = 0;
	ctl.maxlen = BUFSIZE;
	flags = 0;
	if (getmsg(fd, &ctl, NULL, &flags) < 0)
		syserr("getmsg");

	(void) alarm(0);
	(void) signal(SIGALRM, SIG_DFL);

	/*
	 * Validate DL_OK_ACK reply.
	 */
	if (ctl.len < sizeof (ulong))
		error("DL_ATTACH_REQ failed:  short reply to attach request");

	if (dlp->dl_primitive == DL_ERROR_ACK)
		error("DL_ATTACH_REQ failed:  dl_errno %d unix_errno %d",
			dlp->error_ack.dl_errno, dlp->error_ack.dl_unix_errno);

	if (dlp->dl_primitive != DL_OK_ACK)
		error("DL_ATTACH_REQ failed:  dl_primitive 0x%x received",
			dlp->dl_primitive);

	if (ctl.len < DL_OK_ACK_SIZE)
		error("attach failed:  short ok_ack:  %d bytes",
			ctl.len);
	
	/*
	 * Issue DL_BIND_REQ.
	 */
	dlp = (union DL_primitives*) buf;
	dlp->bind_req.dl_primitive = DL_BIND_REQ;
	dlp->bind_req.dl_sap = type;
	dlp->bind_req.dl_max_conind = 0;
	dlp->bind_req.dl_service_mode = DL_CLDLS;
	dlp->bind_req.dl_conn_mgmt = 0;
	dlp->bind_req.dl_xidtest_flg = 0;

	ctl.buf = (char*) dlp;
	ctl.len = DL_BIND_REQ_SIZE;

	if (putmsg(fd, &ctl, NULL, 0) < 0)
		syserr("putmsg");

	(void) signal(SIGALRM, sigalarm);

	alarmmsg = "DL_BIND_REQ failed:  timeout waiting for DL_BIND_ACK";
	(void) alarm(10);

	ctl.buf = (char*) dlp;
	ctl.len = 0;
	ctl.maxlen = BUFSIZE;
	flags = 0;
	if (getmsg(fd, &ctl, NULL, &flags) < 0)
		syserr("getmsg");

	(void) alarm(0);
	(void) signal(SIGALRM, SIG_DFL);

	/*
	 * Validate DL_BIND_ACK reply.
	 */
	if (ctl.len < sizeof (ulong))
		error("DL_BIND_REQ failed:  short reply");

	if (dlp->dl_primitive == DL_ERROR_ACK)
		error("DL_BIND_REQ failed:  dl_errno %d unix_errno %d",
			dlp->error_ack.dl_errno, dlp->error_ack.dl_unix_errno);

	if (dlp->dl_primitive != DL_BIND_ACK)
		error("DL_BIND_REQ failed:  dl_primitive 0x%x received",
			dlp->dl_primitive);

	if (ctl.len < DL_BIND_ACK_SIZE)
		error("DL_BIND_REQ failed:  short bind acknowledgement received");

	if (dlp->bind_ack.dl_sap != type)
		error("DL_BIND_REQ failed:  returned dl_sap %d != requested sap %d",
			dlp->bind_ack.dl_sap, type);

	/*
	 * Issue DL_PHYS_ADDR_REQ to get our local ethernet address.
	 */
	dlp = (union DL_primitives*) buf;
	dlp->physaddr_req.dl_primitive = DL_PHYS_ADDR_REQ;
	dlp->physaddr_req.dl_addr_type = DL_CURR_PHYS_ADDR;

	ctl.buf = (char*) dlp;
	ctl.len = DL_PHYS_ADDR_REQ_SIZE;

	if (putmsg(fd, &ctl, NULL, 0) < 0)
		syserr("putmsg");

	(void) signal(SIGALRM, sigalarm);

	alarmmsg = "DL_PHYS_ADDR_REQ failed:  timeout waiting for DL_PHYS_ADDR_ACK";
	(void) alarm(10);

	ctl.buf = (char*) dlp;
	ctl.len = 0;
	ctl.maxlen = BUFSIZE;
	flags = 0;
	if (getmsg(fd, &ctl, NULL, &flags) < 0)
		syserr("getmsg");

	(void) alarm(0);
	(void) signal(SIGALRM, SIG_DFL);

	/*
	 * Validate DL_PHYS_ADDR_ACK reply.
	 */
	if (ctl.len < sizeof (ulong))
		error("DL_PHYS_ADDR_REQ failed:  short reply");

	if (dlp->dl_primitive == DL_ERROR_ACK)
		error("DL_PHYS_ADDR_REQ failed:  dl_errno %d unix_errno %d",
			dlp->error_ack.dl_errno, dlp->error_ack.dl_unix_errno);

	if (dlp->dl_primitive != DL_PHYS_ADDR_ACK)
		error("DL_PHYS_ADDR_REQ failed:  dl_primitive 0x%x received",
			dlp->dl_primitive);

	if (ctl.len < DL_PHYS_ADDR_ACK_SIZE)
		error("DL_PHYS_ADDR_REQ failed:  short ack received");

	if (dlp->physaddr_ack.dl_addr_length != ETHERADDRL)
		error("DL_PHYS_ADDR_ACK failed:  incompatible dl_addr_length:  %d",
		dlp->physaddr_ack.dl_addr_length);

	/*
	 * Save our ethernet address.
	 */
	eap = (struct ether_addr *) ((char *) dlp +
		dlp->physaddr_ack.dl_addr_offset);
	memcpy((caddr_t) etherp, (caddr_t) eap, ETHERADDRL);

	if (dflag)
		debug("device %s ethernetaddress %s",
			device, ether_ntoa(etherp));

	return (fd);
}

static int
rarp_write(fd, rrp)
int	fd;
struct	rarpreply	*rrp;
{
	struct	strbuf	ctl, data;
	union	DL_primitives	*dlp;
	char	ctlbuf[BUFSIZE];
	struct	dladdr	*dladdrp;

	/*
	 * Construct DL_UNITDATA_REQ.
	 */
	dlp = (union DL_primitives*) ctlbuf;
	dlp->unitdata_req.dl_primitive = DL_UNITDATA_REQ;
	dlp->unitdata_req.dl_dest_addr_length = sizeof (struct dladdr);
	dlp->unitdata_req.dl_dest_addr_offset = DL_UNITDATA_REQ_SIZE;
	dlp->unitdata_req.dl_priority.dl_min = 0;
	dlp->unitdata_req.dl_priority.dl_max = 0;
	dladdrp = (struct dladdr *) (ctlbuf + DL_UNITDATA_REQ_SIZE);
	dladdrp->dl_sap = ETHERTYPE_REVARP;
	memcpy((caddr_t) &dladdrp->dl_phys, (caddr_t) &rrp->dest,
		ETHERADDRL);
	
	/*
	 * Send DL_UNITDATA_REQ.
	 */
	ctl.len = DL_UNITDATA_REQ_SIZE + sizeof (struct dladdr);
	ctl.buf = (char*) dlp;
	data.len = sizeof (struct ether_arp);
	data.buf = (char *) &rrp->earp;
	return (putmsg(fd, &ctl, &data, 0));
}

/*
 * See if we have a TFTP boot file for this guy. Filenames in TFTP 
 * boot requests are of the form <ipaddr> for Sun-3's and of the form
 * <ipaddr>.<arch> for all other architectures.  Since we don't know
 * the client's architecture, either format will do.
 */
int
mightboot(ipa)
u_long ipa;
{
	char path[MAXPATHL];
	DIR *dirp;
	struct dirent *dp;

	(void) sprintf(path, "%s/%08X", BOOTDIR, ipa);

	/*
	 * Try a quick access() first.
	 */
	if (access(path, 0) == 0)
		return (1);

	/*
	 * Not there, do it the slow way by
	 * reading through the directory.
	 */

	(void) sprintf(path, "%08X", ipa);

	if (!(dirp = opendir(BOOTDIR)))
		return 0;

	while ((dp = readdir (dirp)) != (struct dirent *) 0) {
		if (strncmp(dp->d_name, path, 8) != 0)
			continue;
		if ((strlen(dp->d_name) != 8) && (dp->d_name[8] != '.'))
			continue;
		break;
	}
	
	(void) closedir (dirp);

	return (dp? 1: 0);
}

/*
 * Get our IP address and local netmask.
 */
get_ifdata(dev, unit, ipp, maskp)
char	*dev;
int	unit;
u_long *ipp, *maskp;
{
	int	fd;
	struct	ifreq	ifr;
	struct	sockaddr_in	*sin;

	sin = (struct sockaddr_in *) &ifr.ifr_addr;

	/*
	 * Open the IP provider.
	 */
	if ((fd = open(DEVIP, 0)) < 0)
		syserr(DEVIP);

	/*
	 * Ask IP for our IP address.
	 */
	sprintf(ifr.ifr_name, "%s%d", dev, unit);
	if (strioctl(fd, SIOCGIFADDR, -1, sizeof (struct ifreq),
		(char*) &ifr) < 0)
		syserr("SIOCGIFADDR");
	*ipp = ntohl(sin->sin_addr.s_addr);

	if (dflag)
		debug("device %s address %s",
			dev, inet_ntoa(sin->sin_addr));

	/*
	 * Ask IP for our netmask.
	 */
	if (strioctl(fd, SIOCGIFNETMASK, -1, sizeof (struct ifreq),
		(char*) &ifr) < 0)
		syserr("SIOCGIFNETMASK");
	*maskp = ntohl(sin->sin_addr.s_addr);

	if (dflag)
		debug("device %s subnet mask %s",
			dev, inet_ntoa(sin->sin_addr));

	/*
	 * Thankyou ip.
	 */
	(void) close (fd);
}

/*
 * Translate ethernet address to IP address.
 * Return 0 on success, nonzero on failure.
 */
static int
get_ipaddr(e, ipp)
struct ether_addr e;
u_char *ipp;
{
	char host [MAXHOSTL];
	struct hostent *hp;
	struct in_addr addr;
	char	**p;

	/*
	 * Translate ethernet address to hostname
	 * and IP address.
	 */
	if (ether_ntohost(host, e.ether_addr_octet) != 0
	    || !(hp = gethostbyname(host))
	    || hp->h_addrtype != AF_INET
	    || hp->h_length != IPADDRL) {
		if (dflag) 
			debug("could not map hardware address to IP address");
		return 1;
	}
	/*
	 * Find the IP address on the right net.
	 */
	for (p = hp->h_addr_list; *p; p++) {
		memcpy((char *) &addr, *p, IPADDRL);
		if ((ntohl(addr.s_addr) & if_netmask) == if_netnum)
			break;
	}
	if ((ntohl(addr.s_addr) & if_netmask) != if_netnum) {
		if (dflag)
			debug("got host entry but no IP address on this net");
		return 1;
	}

	/*
	 * Return the correct IP address.
	 */
	memcpy((char *) ipp, (char *) &addr, IPADDRL);

	return 0;
}

void
sigalarm()
{
	extern	char	*alarmmsg;

	error(alarmmsg);
}

strioctl(fd, cmd, timout, len, dp)
int	fd;
int	cmd;
int	timout;
int	len;
char	*dp;
{
	struct	strioctl	si;

	si.ic_cmd = cmd;
	si.ic_timout = timout;
	si.ic_len = len;
	si.ic_dp = dp;
	return (ioctl(fd, I_STR, &si));
}

usage()
{
	extern	char	*cmdname;

	error("Usage:  %s [ -ad ] device unit", cmdname);
}

syserr(s)
char	*s;
{
	extern	char	*cmdname;

	(void) fprintf(stderr, "%s:  ", cmdname);
	perror(s);
	syslog(LOG_ERR, s);
	exit(1);
}

/* VARARGS1 */
error(fmt, a1, a2, a3, a4)
char	*fmt, *a1, *a2, *a3, *a4;
{
	extern	char	*cmdname;

	(void) fprintf(stderr, "%s:  ", cmdname);
	(void) fprintf(stderr, fmt, a1, a2, a3, a4);
	(void) fprintf(stderr, "\n");
	syslog(LOG_ERR, fmt, a1, a2, a3, a4);
	exit(1);
}

/* VARARGS1 */
debug(fmt, a1, a2, a3, a4)
char	*fmt, *a1, *a2, *a3, *a4;
{
	extern	char	*cmdname;

	(void) fprintf(stderr, "%s:  ", cmdname);
	(void) fprintf(stderr, fmt, a1, a2, a3, a4);
	(void) fprintf(stderr, "\n");
}
