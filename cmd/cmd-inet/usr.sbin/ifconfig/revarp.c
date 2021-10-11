/*
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

#ident	"@(#)revarp.c	1.11	94/01/14 SMI"

#include <stdio.h>
#include <fcntl.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <stropts.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/stropts.h>
#include <sys/resource.h>
#include <sys/sockio.h>
#include <sys/dlpi.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <arpa/inet.h>

static ether_addr_t my_etheraddr;
static ether_addr_t etherbroadcast = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

#define	ETHERADDRL	sizeof(ether_addr_t)
#define	IPADDRL		sizeof(struct in_addr)
#define BUFSIZE		10000
#define	MAXPATHL	128
#define MAXIFS		16
#define DEVDIR		"/dev"
#define RARPRETRIES	5
#define RARPTIMEOUT	5
#define	DLPI_TIMEOUT	60

struct	etherdladdr {
	ether_addr_t	dl_phys;
	u_short		dl_sap;
};
 

/* Number of times rarp is sent.
 */
int rarp_retries = RARPRETRIES;
int rarp_timeout = RARPTIMEOUT;

/* Timeout for DLPI acks
 */
int dlpi_timeout = DLPI_TIMEOUT;

/* global flags
 */
extern int debug;

/* globals from ifconfig.c
 */
extern int setaddr;
extern char name[];
extern struct sockaddr_in sin;

#ifdef SYSV
#define signal(s,f)	sigset((s), (f))
#define bzero(s,n)	memset((s), 0, (n))
#define bcopy(a,b,c)	memcpy((b),(a),(c))
#endif SYSV


/*ARGSUSED*/
static void
sigalarm_bind(int xxx)
{
	fprintf(stderr,
		"bind failed: timeout waiting for bind acknowledgement\n");
	fflush(stderr);
	exit(1);
}


/*ARGSUSED*/
static void
sigalarm_revarp(int xxx)
{
	if (--rarp_retries > 0)
		return;
	fprintf(stderr, "%s: auto-revarp failed: no RARP replies received\n",
		name);
	resetifup();
	fflush(stderr);
	exit(1);
}


/*ARGSUSED*/
static void
sigalarm_attach(int xxx)
{
	fprintf(stderr,
		"attach failed: timeout waiting for attach acknowledgement\n");
	fflush(stderr);
	exit(1);
}

/*ARGSUSED*/
static void
sigalarm_phys_addr(int xxx)
{
	fprintf(stderr,
		"phys_addr failed: timeout waiting for phys_addr acknowledgement\n");
	fflush(stderr);
	exit(1);
}

/*ARGSUSED*/
static void
sigalarm_set_phys_addr(int xxx)
{
	fprintf(stderr,
		"set_phys_addr failed: timeout waiting for ok acknowledgement\n");
	fflush(stderr);
	exit(1);
}


/* temporarily set interface flags to IFF_UP so that we can send the revarp,
 * save original flags
 */
setifup()
{
#ifdef SUNOS4
	int s;
	struct ifreq ifr;

	strncpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		Perror("socket");
	}
	if (ioctl(s, SIOCGIFFLAGS, (char *) &ifr) < 0) {
		Perror("SIOCGIFFLAGS");
	}
	origflags = ifr.ifr_flags;
	if ((origflags & IFF_UP) == 0) {
		ifr.ifr_flags |= IFF_UP;
		if (ioctl(s, SIOCSIFFLAGS, (char *) &ifr) < 0) {
			Perror("SIOCSIFFLAGS");
		}
	}
	(void) close(s);
#endif SUNOS4
}
	

/* set datalink flags to their original value
 */
resetifup()
{
#ifdef SUNOS4
	int s;
	struct ifreq ifr;

	if ((origflags & IFF_UP) != 0)
		return;
	strncpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		Perror("socket");
	}
	if (ioctl(s, SIOCGIFFLAGS, (char *) &ifr) < 0) {
		Perror("SIOCGIFFLAGS");
	}
	ifr.ifr_flags = origflags;
	if (ioctl(s, SIOCSIFFLAGS, (char *) &ifr) < 0) {
		Perror("SIOCSIFFLAGS");
	}
	(void) close(s);
#endif SUNOS4
}


/* Get ppa and device from BSD style interface name assuming it is a
 * DLPI type 2 interface.
 */
static int
ifnametotype2device(ifname, path)
	char	*ifname;
	char 	*path;
{
	int	i;
	ulong	p = 0;
	int	m = 1;

	/* device name length has already been checked
	 */
	(void) sprintf(path, "%s/%s", DEVDIR, ifname);
	i = strlen(path) - 1;
	while (i >= 0 && '0' <= path[i] && path[i] <= '9') {
		p += (path[i] - '0')*m;
		m *= 10;
		i--;
	}
	path[i + 1] = '\0';
	return(p);
}


/* Get ppa and device from BSD style interface name assuming it is a
 * DLPI type 1 interface. Always returns -1 for the ppa signalling that no 
 * attach is needed.
 */
static int
ifnametotype1device(ifname, path)
	char	*ifname;
	char 	*path;
{
	int	i;
	ulong	p = 0;
	int	m = 1;

	/* device name length has already been checked
	 */
	(void) sprintf(path, "%s/%s", DEVDIR, ifname);
	i = strlen(path) - 1;
	while (i >= 0 && '0' <= path[i] && path[i] <= '9') {
		p += (path[i] - '0')*m;
		m *= 10;
		i--;
	}
	return(p);
}


/*
 * Get ppa, device, and style of device from BSD style interface name.
 * Return the device name in path and the ppa in *ppap
 * Returns 2 for a style 2 device name.
 * Returns 1 for a style 1 device name (signalling that no attach is needed.)
 * Returns -1 on errors.
 */
int
ifname2device_ppa(ifname, path, ppap)
	char	*ifname;
	char 	*path;
	int	*ppap;
{
	struct stat st;
	int ppa;

	ppa = ifnametotype2device(name, path);
	if (stat(path, &st) >= 0) {
		*ppap = ppa;
		return (2);
	}
	if (errno != ENOENT)
		return (-1);
		
	ppa = ifnametotype1device(name, path);
	if (stat(path, &st) >= 0) {
		*ppap = ppa;
		return (1);
	}
	return (-1);
}


setifrevarp(arg, param)
	char *arg;
	int param;
{
	static int		if_fd;
	int			s, flags, ret;
	char			*ctlbuf, *databuf, *cause;
	struct strbuf		ctl, data;
	struct ether_arp	req;
	struct ether_arp	ans;
	struct in_addr		from;
	struct in_addr		answer;
	union DL_primitives	*dlp;
	struct ifreq		ifr;

	if (name[0] == '\0') {
		fprintf(stderr, "setifrevarp: name not set\n");
		exit(1);
	}

	if (debug)
		fprintf(stdout, "setifrevarp interface %s\n", name);

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		Perror("socket");
	}
	strncpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
	if (ioctl(s, SIOCGIFFLAGS, (char *) &ifr) < 0)
		Perror("SIOCGIFFLAGS");

	/* don't try to revarp if we know it won't work
	 */
	if ((ifr.ifr_flags & IFF_LOOPBACK) || (ifr.ifr_flags & IFF_NOARP) ||
					(ifr.ifr_flags & IFF_POINTOPOINT))
		return;

	/* open rarp interface
	 */
	if_fd = rarp_open(name, ETHERTYPE_REVARP, my_etheraddr);
	if (if_fd < 0)
		exit(1);

	/* create rarp request
	 */
	bzero((char *)&req, sizeof(req));
	req.arp_hrd = htons(ARPHRD_ETHER);
	req.arp_pro = htons(ETHERTYPE_IP);
	req.arp_hln = ETHERADDRL;
	req.arp_pln = IPADDRL;
	req.arp_op = htons(REVARP_REQUEST);
	bcopy((char *)my_etheraddr, (char *)&req.arp_sha, ETHERADDRL);
	bcopy((char *)my_etheraddr, (char *)&req.arp_tha, ETHERADDRL);

	/* bring interface up
	 */
	setifup();

rarp_retry:
	/* send the request
	 */
	if (rarp_write(if_fd, &req, etherbroadcast) < 0) {
		Perror("rarp_write");
	}

	if (debug)
		fprintf(stdout, "rarp sent\n");

	(void) signal(SIGALRM, sigalarm_revarp);
	(void) alarm(rarp_timeout);

	/* read the answers
	 */
	if ((databuf = malloc(BUFSIZE)) == NULL) {
		fprintf(stderr, "malloc() failed\n");
		resetifup();
		return;
	}
	if ((ctlbuf = malloc(BUFSIZE)) == NULL) {
		fprintf(stderr, "malloc() failed\n");
		resetifup();
		return;
	}
	for (;;) {
		ctl.len = 0;
		ctl.maxlen = BUFSIZE;
		ctl.buf = ctlbuf;
		data.len = 0;
		data.maxlen = BUFSIZ;
		data.buf = databuf;
		flags = 0;
		if ((ret = getmsg(if_fd, &ctl, &data, &flags)) < 0) {
			if (errno == EINTR) {
				if (debug)
					fprintf(stdout, "rarp retry\n");
				goto rarp_retry;
			}
			perror("ifconfig: getmsg");
			resetifup();
			return;
		}
		if (debug)
		    fprintf(stdout,
			"rarp: ret[%d] ctl.len[%d] data.len[%d] flags[%d]\n",
				ret, ctl.len, data.len, flags);

		/* Validate DL_UNITDATA_IND.
		 */
		dlp = (union DL_primitives*)ctlbuf;
		if (debug) {
			fprintf(stdout, "rarp: dl_primitive[%d]\n",
				dlp->dl_primitive);
			if (dlp->dl_primitive == DL_ERROR_ACK)
			    fprintf(stdout,
				"rarp: err ak: dl_errno %d unix_errno %d\n",
				dlp->error_ack.dl_errno,
				dlp->error_ack.dl_unix_errno);

			if (dlp->dl_primitive == DL_UDERROR_IND)
			    fprintf(stdout,
				"rarp: ud err: err[%d] len[%d] off[%d]\n",
				dlp->uderror_ind.dl_errno,
				dlp->uderror_ind.dl_dest_addr_length,
				dlp->uderror_ind.dl_dest_addr_offset);
		}
		bcopy(databuf, (char *)&ans, sizeof(struct ether_arp));
		cause = NULL;
		if (ret & MORECTL)
			cause = "MORECTL flag";
		else if (ret & MOREDATA)
			cause = "MOREDATA flag";
		else if (ctl.len == 0)
			cause = "missing control part of message";
		else if (ctl.len < 0)
			cause = "short control part of message";
		else if (dlp->dl_primitive != DL_UNITDATA_IND)
			cause = "not unitdata_ind";
		else if (ctl.len < DL_UNITDATA_IND_SIZE)
			cause = "short unitdata_ind";

		/* XXX would be nice to check the ether_type here
		 */
		else if (data.len < sizeof(struct ether_arp))
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
			(void) fprintf(stderr,
				"sanity check failed; cause: %s\n", cause);
			continue;
		}

		switch (ntohs(ans.arp_op)) {
		case ARPOP_REQUEST:
			if (debug)
				fprintf(stdout, "Got an arp request\n");
			break;

		case ARPOP_REPLY:
			if (debug)
				fprintf(stdout, "Got an arp reply.\n");
			break;

		case REVARP_REQUEST:
			if (debug)
				fprintf(stdout, "Got an rarp request.\n");
			break;

		case REVARP_REPLY:
			bcopy(ans.arp_tpa, &answer, sizeof(answer));
			bcopy(ans.arp_spa, &from, sizeof(from));
			if (debug) {
				fprintf(stdout, "answer: %s",
					inet_ntoa(answer));
				fprintf(stdout, " [from %s]\n",
					inet_ntoa(from));
			}
			sin.sin_addr.s_addr = answer.s_addr;
			setaddr++;
			resetifup();
			return;

		default:
			(void) fprintf(stderr, "unknown opcode 0x%xd\n",
				ans.arp_op);
			break;
		}
	}
}

int
dlpi_open_attach(ifname)
	char	*ifname;
{
	int			fd;
	char			path[MAXPATHL];
	union DL_primitives	*dlp;
	char			*buf;
	struct strbuf		ctl;
	int			style;
	int			ppa, flags;
	int			tmp;

	style = ifname2device_ppa(ifname, path, &ppa);
	if (style < 0) {
		/* Not found */
		errno = ENXIO;
		return (-1);
	}

	if (debug)
		fprintf(stdout, "device %s, ppa %d\n", path, ppa);

	/* Open the datalink provider.
	 */
	if ((fd = open(path, O_RDWR)) < 0) {
		return(-1);
	}

	/* Allocate required buffers
	 */
	if ((buf = malloc(BUFSIZE)) == NULL) {
		fprintf(stderr, "malloc() failed\n");
		(void) close(fd);
		return(-1);
	}

	if (style == 2) {
		/* Issue DL_ATTACH_REQ
		 */
		dlp = (union DL_primitives*)buf;
		dlp->attach_req.dl_primitive = DL_ATTACH_REQ;
		dlp->attach_req.dl_ppa = ppa;
		ctl.buf = (char *)dlp;
		ctl.len = DL_ATTACH_REQ_SIZE;
		if (putmsg(fd, &ctl, NULL, 0) < 0) {
			perror("ifconfig: putmsg");
			(void) close(fd);
			free(buf);
			return(-1);
		}
		
		/* start timeout for DL_OK_ACK reply
		 */
		(void) signal(SIGALRM, sigalarm_attach);
		(void) alarm(dlpi_timeout);
		
		/* read reply
		 */
		ctl.buf = (char *)dlp;
		ctl.len = 0;
		ctl.maxlen = BUFSIZE;
		flags = 0;
		if ((tmp = getmsg(fd, &ctl, NULL, &flags)) < 0) {
			perror("ifconfig: getmsg");
			(void) close(fd);
			free(buf);
			return(-1);
		}
		if (debug) {
			fprintf(stdout,
				"ok_ack: ret[%d] ctl.len[%d] flags[%d]\n",
				tmp, ctl.len, flags);
		}
		
		/* got reply - turn off alarm
		 */
		(void) alarm(0);
		(void) signal(SIGALRM, SIG_DFL);
		
		/* Validate DL_OK_ACK reply.
		 */
		if (ctl.len < sizeof(ulong)) {
			fprintf(stderr,
				"attach failed:  short reply to attach request\n");
			free(buf);
			return(-1);
		}
		
		if (dlp->dl_primitive == DL_ERROR_ACK) {
			if (debug)
				fprintf(stderr, "attach failed:  dl_errno %d unix_errno %d\n",
					dlp->error_ack.dl_errno,
					dlp->error_ack.dl_unix_errno);
			(void) close(fd);
			free(buf);
			errno = ENXIO;
			return(-1);
		}
		if (dlp->dl_primitive != DL_OK_ACK) {
			fprintf(stderr,
				"attach failed:  unrecognizable dl_primitive %d received",
				dlp->dl_primitive);
			(void) close(fd);
			free(buf);
			return(-1);
		}
		if (ctl.len < DL_OK_ACK_SIZE) {
			fprintf(stderr,
				"attach failed:  short attach acknowledgement received\n");
			(void) close(fd);
			free(buf);
			return(-1);
		}
		if (dlp->ok_ack.dl_correct_primitive != DL_ATTACH_REQ) {
			fprintf(stderr,
				"attach failed:  returned prim %d != requested prim %d\n",
				dlp->ok_ack.dl_correct_primitive, DL_ATTACH_REQ);
			(void) close(fd);
			free(buf);
			return(-1);
		}
		
		if (debug)
			fprintf(stdout, "attach done\n");
	}
	free(buf);
	return (fd);
}

static int
dlpi_bind(fd, sap, eaddr)
	int	fd;
	u_long	sap;
	u_char	*eaddr;
{
	union DL_primitives	*dlp;
	char			*buf;
	struct strbuf		ctl;
	int			flags;
	int			tmp;

	/* Allocate required buffers
	 */
	if ((buf = malloc(BUFSIZE)) == NULL) {
		fprintf(stderr, "malloc() failed\n");
		return(-1);
	}
	/* Issue DL_BIND_REQ
	 */
	dlp = (union DL_primitives*)buf;
	dlp->bind_req.dl_primitive = DL_BIND_REQ;
	dlp->bind_req.dl_sap = sap;
	dlp->bind_req.dl_max_conind = 0;
	dlp->bind_req.dl_service_mode = DL_CLDLS;
	dlp->bind_req.dl_conn_mgmt = 0;
	dlp->bind_req.dl_xidtest_flg = 0;
	ctl.buf = (char *)dlp;
	ctl.len = DL_BIND_REQ_SIZE;
	if (putmsg(fd, &ctl, NULL, 0) < 0) {
		perror("ifconfig: putmsg");
		free(buf);
		return(-1);
	}

	/* start timeout for DL_BIND_ACK reply
	 */
	(void) signal(SIGALRM, sigalarm_bind);
	(void) alarm(dlpi_timeout);

	/* read reply
	 */
	ctl.buf = (char *)dlp;
	ctl.len = 0;
	ctl.maxlen = BUFSIZE;
	flags = 0;
	if ((tmp = getmsg(fd, &ctl, NULL, &flags)) < 0) {
		perror("ifconfig: getmsg");
		free(buf);
		return(-1);
	}
	if (debug) {
		fprintf(stdout,
		    "bind_ack: ret[%d] ctl.len[%d] flags[%d]\n",
			tmp, ctl.len, flags);
	}

	/* got reply - turn off alarm
	 */
	(void) alarm(0);
	(void) signal(SIGALRM, SIG_DFL);

	/* Validate DL_BIND_ACK reply.
	 */
	if (ctl.len < sizeof(ulong)) {
		fprintf(stderr, "bind failed:  short reply to bind request\n");
		free(buf);
		return(-1);
	}

	if (dlp->dl_primitive == DL_ERROR_ACK) {
		fprintf(stderr, "bind failed:  dl_errno %d unix_errno %d\n",
			dlp->error_ack.dl_errno, dlp->error_ack.dl_unix_errno);
		free(buf);
		return(-1);
	}
	if (dlp->dl_primitive != DL_BIND_ACK) {
		fprintf(stderr,
			"bind failed:  unrecognizable dl_primitive %d received\n",
			dlp->dl_primitive);
		free(buf);
		return(-1);
	}
	if (ctl.len < DL_BIND_ACK_SIZE) {
		fprintf(stderr,
			"bind failed:  short bind acknowledgement received\n");
		free(buf);
		return(-1);
	}
	if (dlp->bind_ack.dl_sap != sap) {
		fprintf(stderr,
			"bind failed:  returned dl_sap %d != requested sap %d\n",
			dlp->bind_ack.dl_sap, sap);
		free(buf);
		return(-1);
	}
	/* copy ethernet address
	 */
	bcopy((char *)(buf + dlp->bind_ack.dl_addr_offset), (char *)eaddr,
		ETHERADDRL);

	free(buf);
	return (0);
}

static int
dlpi_get_phys(fd, eaddr)
	int	fd;
	u_char	*eaddr;
{
	union DL_primitives	*dlp;
	char			*buf;
	struct strbuf		ctl;
	int			flags;
	int			tmp;

	/* Allocate required buffers
	 */
	if ((buf = malloc(BUFSIZE)) == NULL) {
		fprintf(stderr, "malloc() failed\n");
		return(-1);
	}
	/* Issue DL_PHYS_ADDR_REQ
	 */
	dlp = (union DL_primitives*)buf;
	dlp->physaddr_req.dl_primitive = DL_PHYS_ADDR_REQ;
	dlp->physaddr_req.dl_addr_type = DL_CURR_PHYS_ADDR;
	ctl.buf = (char *)dlp;
	ctl.len = DL_PHYS_ADDR_REQ_SIZE;
	if (putmsg(fd, &ctl, NULL, 0) < 0) {
		perror("ifconfig: putmsg");
		free(buf);
		return(-1);
	}

	/* start timeout for DL_PHYS_ADDR_ACK reply
	 */
	(void) signal(SIGALRM, sigalarm_phys_addr);
	(void) alarm(dlpi_timeout);

	/* read reply
	 */
	ctl.buf = (char *)dlp;
	ctl.len = 0;
	ctl.maxlen = BUFSIZE;
	flags = 0;
	if ((tmp = getmsg(fd, &ctl, NULL, &flags)) < 0) {
		perror("ifconfig: getmsg");
		free(buf);
		return(-1);
	}
	if (debug) {
		fprintf(stdout,
		    "phys_addr_ack: ret[%d] ctl.len[%d] flags[%d]\n",
			tmp, ctl.len, flags);
	}

	/* got reply - turn off alarm
	 */
	(void) alarm(0);
	(void) signal(SIGALRM, SIG_DFL);

	/* Validate DL_PHYS_ADDR_ACK reply.
	 */
	if (ctl.len < sizeof(ulong)) {
		fprintf(stderr, 
			"phys_addr failed:  short reply to phys_addr request\n");
		free(buf);
		return(-1);
	}

	if (dlp->dl_primitive == DL_ERROR_ACK) {
		/*
		 * Do not print errors for DL_UNSUPPORTED and DL_NOTSUPPORTED
		 */
		if (dlp->error_ack.dl_errno != DL_UNSUPPORTED &&
		    dlp->error_ack.dl_errno != DL_NOTSUPPORTED) {
			fprintf(stderr, "phys_addr failed:  dl_errno %d unix_errno %d\n",
				dlp->error_ack.dl_errno, dlp->error_ack.dl_unix_errno);
		}
		free(buf);
		return(-1);
	}
	if (dlp->dl_primitive != DL_PHYS_ADDR_ACK) {
		fprintf(stderr,
			"phys_addr failed:  unrecognizable dl_primitive %d received\n",
			dlp->dl_primitive);
		free(buf);
		return(-1);
	}
	if (ctl.len < DL_PHYS_ADDR_ACK_SIZE) {
		fprintf(stderr,
			"phys_addr failed:  short phys_addr acknowledgement received\n");
		free(buf);
		return(-1);
	}
	/* Check length of address. */
	if (dlp->physaddr_ack.dl_addr_length != ETHERADDRL)
		return (-1);

	/* copy ethernet address
	 */
	bcopy((char *)(buf + dlp->physaddr_ack.dl_addr_offset), (char *)eaddr,
		ETHERADDRL);

	free(buf);
	return (0);
}

static int
dlpi_set_phys(fd, eaddr)
	int	fd;
	u_char	*eaddr;
{
	union DL_primitives	*dlp;
	char			*buf;
	struct strbuf		ctl;
	int			flags;
	int			tmp;

	/* Allocate required buffers
	 */
	if ((buf = malloc(BUFSIZE)) == NULL) {
		fprintf(stderr, "malloc() failed\n");
		return(-1);
	}
	/* Issue DL_SET_PHYS_ADDR_REQ
	 */
	dlp = (union DL_primitives*)buf;
	dlp->set_physaddr_req.dl_primitive = DL_SET_PHYS_ADDR_REQ;
	dlp->set_physaddr_req.dl_addr_length = ETHERADDRL;
	dlp->set_physaddr_req.dl_addr_offset = DL_SET_PHYS_ADDR_REQ_SIZE;
	/* copy ethernet address
	 */
	bcopy((caddr_t)eaddr,
		(caddr_t) (buf + dlp->physaddr_ack.dl_addr_offset),
		ETHERADDRL);
	ctl.buf = (char *)dlp;
	ctl.len = DL_SET_PHYS_ADDR_REQ_SIZE + ETHERADDRL;
	if (putmsg(fd, &ctl, NULL, 0) < 0) {
		perror("ifconfig: putmsg");
		free(buf);
		return(-1);
	}

	/* start timeout for DL_SET_PHYS_ADDR_ACK reply
	 */
	(void) signal(SIGALRM, sigalarm_set_phys_addr);
	(void) alarm(dlpi_timeout);

	/* read reply
	 */
	ctl.buf = (char *)dlp;
	ctl.len = 0;
	ctl.maxlen = BUFSIZE;
	flags = 0;
	if ((tmp = getmsg(fd, &ctl, NULL, &flags)) < 0) {
		perror("ifconfig: getmsg");
		free(buf);
		return(-1);
	}
	if (debug) {
		fprintf(stdout,
		    "set_phys_addr_ack: ret[%d] ctl.len[%d] flags[%d]\n",
			tmp, ctl.len, flags);
	}

	/* got reply - turn off alarm
	 */
	(void) alarm(0);
	(void) signal(SIGALRM, SIG_DFL);

	/* Validate DL_OK_ACK reply.
	 */
	if (ctl.len < sizeof(ulong)) {
		fprintf(stderr, 
			"set_phys_addr failed:  short reply to set_phys_addr request\n");
		free(buf);
		return(-1);
	}

	if (dlp->dl_primitive == DL_ERROR_ACK) {
		fprintf(stderr, "set_phys_addr failed:  dl_errno %d unix_errno %d\n",
			dlp->error_ack.dl_errno, dlp->error_ack.dl_unix_errno);
		free(buf);
		return(-1);
	}
	if (dlp->dl_primitive != DL_OK_ACK) {
		fprintf(stderr,
			"set_phys_addr failed:  unrecognizable dl_primitive %d received\n",
			dlp->dl_primitive);
		free(buf);
		return(-1);
	}
	if (ctl.len < DL_OK_ACK_SIZE) {
		fprintf(stderr,
			"set_phys_addr failed:  short ok acknowledgement received\n");
		free(buf);
		return(-1);
	}

	free(buf);
	return (0);
}


/* Open the datalink provider device and bind to the REVARP type.
 * Return the resulting descriptor.
 */
int
rarp_open(ifname, type, e)
	char			*ifname;
	u_long			type;
	ether_addr_t		e;
{
	int			fd;

	fd = dlpi_open_attach(ifname);
	if (fd < 0) {
		fprintf(stderr, "ifconfig: could not open device for %s\n",
			ifname);
		return (-1);
	}

	if (dlpi_bind(fd, type, (u_char *)e) < 0) {
		(void) close(fd);
		return (-1);
	}

	if (debug)
		fprintf(stdout, "device %s ethernetaddress %s\n", ifname,
			ether_ntoa(e));

	return(fd);
}

static int
rarp_write(fd, r, dhost)
	int			fd;
	struct ether_arp	*r;
	ether_addr_t		dhost;
{
	struct strbuf		ctl, data;
	union DL_primitives	*dlp;
	struct etherdladdr	*dlap;
	char			*ctlbuf;
	char			*databuf;
	int			ret;

	/* Construct DL_UNITDATA_REQ.
	 */
	if ((ctlbuf = malloc(BUFSIZE)) == NULL) {
		fprintf(stderr, "malloc() failed\n");
		return;
	}
	dlp = (union DL_primitives*)ctlbuf;
	dlp->unitdata_req.dl_primitive = DL_UNITDATA_REQ;
	dlp->unitdata_req.dl_dest_addr_length = ETHERADDRL + sizeof(u_short);
	dlp->unitdata_req.dl_dest_addr_offset = DL_UNITDATA_REQ_SIZE;
	dlp->unitdata_req.dl_priority.dl_min = 0;
	dlp->unitdata_req.dl_priority.dl_max = 0;

	/*
	 * XXX FIXME
	 * Assume a specific dlpi address format.
	 */
	dlap = (struct etherdladdr *)(ctlbuf + DL_UNITDATA_REQ_SIZE);
	bcopy((char *)dhost, (char *)dlap->dl_phys, ETHERADDRL);
	dlap->dl_sap = (u_short)(ETHERTYPE_REVARP);

	/* Send DL_UNITDATA_REQ.
	 */
	if ((databuf = malloc(BUFSIZE)) == NULL) {
		fprintf(stderr, "malloc() failed\n");
		return(-1);
	}
	ctl.len = DL_UNITDATA_REQ_SIZE + ETHERADDRL + sizeof(u_short);
	ctl.buf = (char *)dlp;
	ctl.maxlen = BUFSIZE;
	bcopy((char *)r, databuf, sizeof(struct ether_arp));
	data.len = sizeof(struct ether_arp);
	data.buf = databuf;
	data.maxlen = BUFSIZE;
	ret = putmsg(fd, &ctl, &data, 0);
	free(ctlbuf);
	free(databuf);
	return(ret);
}

#ifdef SYSV
dlpi_set_address(ifname, ea)
	char *ifname;
	ether_addr_t	*ea;
{
	int 	fd;

	fd = dlpi_open_attach(ifname);
	if (fd < 0) {
		fprintf(stderr, "ifconfig: could not open device for %s\n",
			ifname);
		return (-1);
	}
	if (dlpi_set_phys(fd, (u_char *)ea) < 0) {
		(void) close(fd);
		return (-1);
	}
	(void) close(fd);

}

int
dlpi_get_address(ifname, ea)
	char *ifname;
	ether_addr_t	*ea;
{
	int 	fd;

	fd = dlpi_open_attach(ifname);
	if (fd < 0) {
		/* Do not report an error */
		return (-1);
	}

	if (dlpi_get_phys(fd, (u_char *)ea) < 0) {
		(void) close(fd);
		return (-1);
	}
	(void) close(fd);
	return (0);
}

#endif /* SYSV */
