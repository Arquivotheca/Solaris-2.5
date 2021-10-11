/*
 * Copyright (c) 1990  Mentat Inc.
 * netstat.c 2.2, last change 9/9/91
 */

#pragma ident	"@(#)netstat.c	1.20	95/08/10 SMI"

/*
 * simple netstat based on snmp/mib-2 interface to the TCP/IP stack
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <kstat.h>

#include <sys/types.h>
#include <sys/stream.h>
#include <stropts.h>
#include <sys/strstat.h>
#include <sys/sysmacros.h>
#include <sys/tihdr.h>
#include <sys/tiuser.h>
#include <sys/timod.h>

#include <sys/socket.h>
#include <sys/sockio.h>
#include <netinet/in.h>
#include <net/if.h>

#include <inet/common.h>
#include <inet/mib2.h>
#include <inet/ip.h>
#include <inet/arp.h>
#include <inet/tcp.h>
#include <netinet/igmp_var.h>
#include <netinet/ip_mroute.h>

#include <netdb.h>
#include <nlist.h>
#include <kvm.h>
#include <fcntl.h>
#include <sys/systeminfo.h>

extern	int	errno;

static char 	*routename(), *netname(), *netnamefromaddr(), *portname();
extern char *inet_ntoa();

#ifndef T_CURRENT
#define	T_CURRENT	MI_T_CURRENT
#endif

typedef struct mib_item_s {
	struct mib_item_s	*next_item;
	long			group;
	long			mib_id;
	long			length;
	char			*valp;
} mib_item_t;

#ifdef	USE_STDARG
extern	void	fatal(char *fmt, ...);
#endif
static	mib_item_t	*mibget(int sd);
static	int		mibopen(void);
static char		*octetstr(char *buf, Octet_t *op, int code);
static char		*ipastr(char *buf, IpAddress ipa);
static char		*ipanstr(char *buf, IpAddress ipa, IpAddress mask);
static char		*ipamstr(char *buf, IpAddress ipa);
static char		*ipapstr(char *buf, IpAddress ipa, long port,
					char *proto);
#if 0
static	char		*mibtcp_state(int code);
#endif
static	char		*mitcp_state(int code);

static void	stat_report(mib_item_t *item);
static void	mrt_stat_report(mib_item_t *item);
static void	arp_report(mib_item_t *item);
static void	mrt_report(mib_item_t *item);
static void	if_report(mib_item_t *item, char *ifname, int interval);
static void	ire_report(mib_item_t *item);
static void	tcp_report(mib_item_t *item);
static void	udp_report(mib_item_t *item);
static void	group_report(mib_item_t *item);
static void	igmp_stats(struct igmpstat *igps);
static void	mrt_stats(struct mrtstat *mrts);
static void	k_report(int, char **);
static void	if_interval(mib_item_t *item, char *ifname, int interval);
static void	print_kn(kstat_t *ksp);
static void	m_report();

static	void	fail(int, char *, ...);
static	ulong_t	kstat_named_value(kstat_t *, char *);
static	kid_t	safe_kstat_read(kstat_ctl_t *, kstat_t *, void *);
static int	isnum(char *);

static	int	Aflag = 0;
static	int	Dflag = 0;
static	int	Iflag = 0;
static	int	Kflag = 0;
static	int	Mflag = 0;
static	int	Nflag = 0;
static	int	Rflag = 0;
static	int	Sflag = 0;
static	int	Vflag = 0;
	/* NetToMedia table. TODO move this to the arp command */
static	int	Pflag = 0;
static	int	Gflag = 0;	/* Multicast group membership */
static	int	MMflag = 0;	/* Multicast routing table */

static char usage[] =
		"[ -adgimnprsMv ] [-I interface] [interval] [system] [core]";

static char	*sysnam = (char *)0;
static char	*mem = (char *)0;
static int	af = AF_UNSPEC;
static int	proto = IPPROTO_MAX;	/* all protocols */
static kvm_t		*kd;
static kstat_ctl_t	*kc = NULL;
extern void	unixpr();
extern void	readmem(long, int, int, char *, unsigned, char *);

static struct nlist nl[] = {
#define	N_SO_UX 0
	{"so_ux_list"},
	"",
};
static long	so_ux_list;

#define	protocol_selected(p) (proto == IPPROTO_MAX || proto == (p))

int
main(int argc, char **argv)
{
	char		*name;
	char		*cp;
	mib_item_t	*item;
	int		sd;
	char	*ifname = NULL;
	int	interval = 0;

	name = argv[0];
	argc--, argv++;
	while (argc > 0 && **argv == '-') {
		for (cp = &argv[0][1]; *cp; cp++)
		switch (*cp) {

		case 'a':		/* all connections */
			Aflag++;
			break;

		case 'd':		/* turn on debugging */
			Dflag++;
			break;

		case 'i':		/* interface (ill/ipif report) */
			Iflag++;
			break;

		case 'k':	/* named kstats (undocumented for now) XXX */
			Kflag++;
			break;

		case 'm':		/* streams msg report */
			Mflag++;
			break;

		case 'n':		/* numeric format */
			Nflag++;
			break;

		case 'r':		/* route tables */
			Rflag++;
			break;

		case 's':
			Sflag++;	/* statistics */
			break;

		case 'p':
			Pflag++;	/* arp table */
			break;

		case 'M':
			MMflag++;	/* multicast routing tables */
			break;

		case 'g':
			Gflag++;	/* multicast group membership */
			break;

		case 'v':		/* verbose output format */
			Vflag++;
			break;

		case 'f':
			argv++;
			argc--;
			if (argc < 1) {
				(void) fprintf(stderr,
					"address family not specified\n");
				exit(1);
			}
			if (strcmp(*argv, "inet") == 0)
				af = AF_INET;
			else if (strcmp(*argv, "unix") == 0)
				af = AF_UNIX;
			else {
				(void) fprintf(stderr,
					"%s: unknown address family.\n",
					*argv);
				exit(1);
			}
			break;

		case 'P':
			argv++;
			argc--;
			if (argc < 1) {
				(void) fprintf(stderr,
					"protocol name not specified\n");
				exit(1);
			}
			if (strcmp(*argv, "ip") == 0)
				proto = IPPROTO_IP;
			else if (strcmp(*argv, "icmp") == 0)
				proto = IPPROTO_ICMP;
			else if (strcmp(*argv, "igmp") == 0)
				proto = IPPROTO_IGMP;
			else if (strcmp(*argv, "udp") == 0)
				proto = IPPROTO_UDP;
			else if (strcmp(*argv, "tcp") == 0)
				proto = IPPROTO_TCP;
			else {
				(void) fprintf(stderr,
					"%s: unknown protocol.\n",
					*argv);
				exit(1);
			}
			break;

		case 'I':
			argv++;
			argc--;
			ifname = *argv;
			Iflag++;
			break;

		case 't':
		case 'A':
		case 'u':
		default:
			(void) fprintf(stderr, "usage: %s %s\n", name, usage);
			exit(1);
		}
		argv++, argc--;
	}

	/*
	 * Leftover arguments could be interval, or system/mem args.
	 */
	switch (argc) {
		case	0:
			break;

		case	1:	/* interval or system ? */
			if (Iflag && isnum(*argv))
					interval = atoi(*argv);
			else
					sysnam = *argv;
			argv++;
			break;

		case	2:	/* (interval and system) or (system and core) */
			if (Iflag && isnum(*argv)) {
				interval = atoi(*argv++);
				sysnam = *argv++;
			} else {
				sysnam = *argv++;
				mem = *argv++;
			}
			break;

		case	3:	/* interval system and core ? */
			if (Iflag && isnum(*argv)) {
				interval = atoi(*argv++);
				sysnam = *argv++;
				mem = *argv++;
			} else
				(void) fprintf(stderr, "usage: %s %s\n",
							name, usage);
			break;

		default:
			(void) fprintf(stderr, "usage:  %s %s\n", name, usage);
			exit(1);
	}

	if (interval)
		setbuf(stdout, NULL);

	if (sysnam == NULL && mem == NULL) {	/* live kernel */
		if ((kc = kstat_open()) == NULL)
			fail(1, "kstat_open(): can't open /dev/kstat");
	}

	if ((kd = kvm_open(sysnam, mem, NULL, O_RDONLY, "netstat")) == NULL) {
		(void) fprintf(stderr, "can't open kernel\n");
		exit(1);
	}
	if (kvm_nlist(kd, nl) == -1) {
		(void) fprintf(stderr, "can't nlist kernel\n");
		exit(1);
	}
	so_ux_list = nl[N_SO_UX].n_value;

	if ((af == AF_INET) || (af == AF_UNSPEC)) {
		sd = mibopen();
		if (sd == -1) {
			perror("can't open mib stream");
			exit(1);
		}
		if ((item = mibget(sd)) == (mib_item_t *)0) {
			(void) fprintf(stderr, "mibget() failed\n");
			(void) close(sd);
			exit(1);
		}

		if (!(Iflag || Kflag || Rflag || Sflag ||
				Mflag || MMflag || Pflag || Gflag)) {
			if (Aflag || proto == IPPROTO_UDP)
				udp_report(item);
			if (protocol_selected(IPPROTO_TCP))
				tcp_report(item);
		}
		if (Iflag)
			if_report(item, ifname, interval);
		if (Kflag)
			k_report(argc, argv);
		if (Mflag)
			m_report();
		if (Rflag)
			ire_report(item);
		if (Sflag && MMflag) {
			mrt_stat_report(item);
		} else {
			if (Sflag)
				stat_report(item);
			if (MMflag)
				mrt_report(item);
		}
		if (Gflag)
			group_report(item);
		if (Pflag)
			arp_report(item);
	}

	if (((af == AF_UNIX) || (af == AF_UNSPEC)) &&
	    (!(Iflag || Kflag || Rflag || Sflag || Mflag ||
				MMflag || Pflag || Gflag)))
		unixpr(so_ux_list);

	return (0);
}


static int
isnum(char *p)
{
	int	len;
	int	i;

	len = strlen(p);
	for (i = 0; i < len; i++)
		if (!isdigit(p[i]))
			return (0);
	return (1);
}


/* --------------------------------- MIBGET -------------------------------- */

static mib_item_t *
mibget(int sd)
{
	char			buf[512];
	int			flags;
	int			i, j, getcode;
	struct strbuf		ctlbuf, databuf;
	struct T_optmgmt_req	*tor = (struct T_optmgmt_req *)buf;
	struct T_optmgmt_ack	*toa = (struct T_optmgmt_ack *)buf;
	struct T_error_ack	*tea = (struct T_error_ack *)buf;
	struct opthdr		*req;
	mib_item_t		*first_item = (mib_item_t *)0;
	mib_item_t		*last_item  = (mib_item_t *)0;
	mib_item_t		*temp;

	tor->PRIM_type = T_OPTMGMT_REQ;
	tor->OPT_offset = sizeof (struct T_optmgmt_req);
	tor->OPT_length = sizeof (struct opthdr);
	tor->MGMT_flags = T_CURRENT;
	req = (struct opthdr *)&tor[1];
	req->level = MIB2_IP;		/* any MIB2_xxx value ok here */
	req->name  = 0;
	req->len   = 0;

	ctlbuf.buf = buf;
	ctlbuf.len = tor->OPT_length + tor->OPT_offset;
	flags = 0;
	if (putmsg(sd, &ctlbuf, (struct strbuf *)0, flags) == -1) {
		perror("mibget: putmsg(ctl) failed");
		goto error_exit;
	}
	/*
	 * each reply consists of a ctl part for one fixed structure
	 * or table, as defined in mib2.h.  The format is a T_OPTMGMT_ACK,
	 * containing an opthdr structure.  level/name identify the entry,
	 * len is the size of the data part of the message.
	 */
	req = (struct opthdr *)&toa[1];
	ctlbuf.maxlen = sizeof (buf);
	j = 1;
	while (1) {
		flags = 0;
		getcode = getmsg(sd, &ctlbuf, (struct strbuf *)0, &flags);
		if (getcode == -1) {
			perror("mibget getmsg(ctl) failed");
			if (Dflag) {
				(void) fprintf(stderr,
					"#   level   name    len\n");
				i = 0;
				for (last_item = first_item; last_item;
					last_item = last_item->next_item)
					(void) printf("%d  %4ld   %5ld   %ld\n",
						++i,
						last_item->group,
						last_item->mib_id,
						last_item->length);
			}
			goto error_exit;
		}
		if (getcode == 0 &&
				ctlbuf.len >= sizeof (struct T_optmgmt_ack) &&
				toa->PRIM_type == T_OPTMGMT_ACK &&
				toa->MGMT_flags == T_SUCCESS &&
				req->len == 0) {
			if (Dflag)
				(void) printf(
		"mibget getmsg() %d returned EOD (level %ld, name %ld)\n",
					j, req->level, req->name);
			return (first_item);		/* this is EOD msg */
		}

		if (ctlbuf.len >= sizeof (struct T_error_ack) &&
				tea->PRIM_type == T_ERROR_ACK) {
			(void) fprintf(stderr,
	"mibget %d gives T_ERROR_ACK: TLI_error = 0x%lx, UNIX_error = 0x%lx\n",
				j, tea->TLI_error, tea->UNIX_error);
			errno = (tea->TLI_error == TSYSERR)
				? tea->UNIX_error : EPROTO;
			goto error_exit;
		}

		if (getcode != MOREDATA ||
				ctlbuf.len < sizeof (struct T_optmgmt_ack) ||
				toa->PRIM_type != T_OPTMGMT_ACK ||
				toa->MGMT_flags != T_SUCCESS) {
			(void) printf(
	"mibget getmsg(ctl) %d returned %d, ctlbuf.len = %d, PRIM_type = %ld\n",
				j, getcode, ctlbuf.len, toa->PRIM_type);
			if (toa->PRIM_type == T_OPTMGMT_ACK)
				(void) printf(
	"T_OPTMGMT_ACK: MGMT_flags = 0x%lx, req->len = %ld\n",
					toa->MGMT_flags, req->len);
			errno = ENOMSG;
			goto error_exit;
		}

		temp = (mib_item_t *)malloc(sizeof (mib_item_t));
		if (!temp) {
			perror("mibget malloc failed");
			goto error_exit;
		}
		if (last_item)
			last_item->next_item = temp;
		else
			first_item = temp;
		last_item = temp;
		last_item->next_item = (mib_item_t *)0;
		last_item->group = req->level;
		last_item->mib_id = req->name;
		last_item->length = req->len;
		last_item->valp = (char *)malloc((int)req->len);
		if (Dflag)
			(void) printf(
	"msg %d:  group = %4ld   mib_id = %5ld   length = %ld\n",
				j, last_item->group, last_item->mib_id,
				last_item->length);

		databuf.maxlen = last_item->length;
		databuf.buf    = last_item->valp;
		databuf.len    = 0;
		flags = 0;
		getcode = getmsg(sd, (struct strbuf *)0, &databuf, &flags);
		if (getcode == -1) {
			perror("mibget getmsg(data) failed");
			goto error_exit;
		} else if (getcode != 0) {
			(void) printf(
"mibget getmsg(data) returned %d, databuf.maxlen = %d, databuf.len = %d\n",
				getcode, databuf.maxlen, databuf.len);
			goto error_exit;
		}
		j++;
	}

error_exit:;
	while (first_item) {
		last_item = first_item;
		first_item = first_item->next_item;
		free(last_item);
	}
	return (first_item);
}


static int
mibopen(void)
{
	int	sd;

	sd = open("/dev/ip", 2);
	if (sd == -1) {
		perror("ip open");
		(void) close(sd);
		return (-1);
	}
	/* must be above ip, below tcp */
	if (ioctl(sd, I_PUSH, "arp") == -1) {
		perror("arp I_PUSH");
		(void) close(sd);
		return (-1);
	}
	if (ioctl(sd, I_PUSH, "tcp") == -1) {
		perror("tcp I_PUSH");
		(void) close(sd);
		return (-1);
	}
	if (ioctl(sd, I_PUSH, "udp") == -1) {
		perror("udp I_PUSH");
		(void) close(sd);
		return (-1);
	}
	return (sd);
}

static char *
octetstr(char *buf, Octet_t *op, int code)
{
	int	i;
	char	*cp;

	cp = buf;
	if (op)
		for (i = 0; i < op->o_length; i++)
			switch (code) {
			case 'd':
				(void) sprintf(cp, "%d.",
					0xff & op->o_bytes[i]);
				cp = strchr(cp, '\0');
				break;
			case 'a':
				*cp++ = op->o_bytes[i];
				break;
			case 'h':
			default:
				(void) sprintf(cp, "%02x:",
							0xff & op->o_bytes[i]);
				cp += 3;
				break;
			}
	if (code != 'a' && cp != buf)
		cp--;
	*cp = '\0';
	return (buf);
}

static char *
ipastr(char *buf, IpAddress ipa)
{
	(void) sprintf(buf, "%s", routename(ipa));
	return (buf);
}

/* For network numbers */
static char *
ipanstr(char *buf, IpAddress ipa, IpAddress mask)
{
	(void) sprintf(buf, "%s", netname(ipa, mask));
	return (buf);
}

/* For network numbers from host address */
static char *
hostanstr(char *buf, IpAddress ipa, IpAddress mask)
{
	(void) sprintf(buf, "%s", netnamefromaddr(ipa, mask));
	return (buf);
}

/* For masks */
static char *
ipamstr(char *buf, IpAddress ipa)
{
	u8	*ip_addr = (u8 *)&ipa;

	(void) sprintf(buf, "%d.%d.%d.%d", ip_addr[0], ip_addr[1], ip_addr[2],
		ip_addr[3]);
	return (buf);
}

static char *
ipapstr(char *buf, IpAddress ipa, long port, char *proto)
{
	if (ipa == 0)
		(void) sprintf(buf, "      *.%s",
					portname((u_long)port, proto));
	else
		(void) sprintf(buf, "%s.%s", routename(ipa),
					portname((u_long)port, proto));
	return (buf);
}


static	char	tcpsbuf[50];

#if 0
static char *
mibtcp_state(int code)
{
	switch (code) {
	case 1:
		return ("MIB2_TCP_closed");
	case 2:
		return ("MIB2_TCP_listen");
	case 3:
		return ("MIB2_TCP_synSent");
	case 4:
		return ("MIB2_TCP_synReceived");
	case 5:
		return ("MIB2_TCP_established");
	case 6:
		return ("MIB2_TCP_finWait1");
	case 7:
		return ("MIB2_TCP_finWait2");
	case 8:
		return ("MIB2_TCP_closeWait");
	case 9:
		return ("MIB2_TCP_lastAck");
	case 10:
		return ("MIB2_TCP_closing");
	case 11:
		return ("MIB2_TCP_timeWait");
	case 12:
		return ("MIB2_TCP_deleteTCB");
	default:
		(void) sprintf(tcpsbuf, "tcp state (%d) unkown", code);
		return (tcpsbuf);
	}
}
#endif

static char *
mitcp_state(int state)
{
	char	*cp;

	switch (state) {
	case TCPS_CLOSED:
		cp = "CLOSED";
		break;
	case TCPS_IDLE:
		cp = "IDLE";
		break;
	case TCPS_BOUND:
		cp = "BOUND";
		break;
	case TCPS_LISTEN:
		cp = "LISTEN";
		break;
	case TCPS_SYN_SENT:
		cp = "SYN_SENT";
		break;
	case TCPS_SYN_RCVD:
		cp = "SYN_RCVD";
		break;
	case TCPS_ESTABLISHED:
		cp = "ESTABLISHED";
		break;
	case TCPS_CLOSE_WAIT:
		cp = "CLOSE_WAIT";
		break;
	case TCPS_FIN_WAIT_1:
		cp = "FIN_WAIT_1";
		break;
	case TCPS_CLOSING:
		cp = "CLOSING";
		break;
	case TCPS_LAST_ACK:
		cp = "LAST_ACK";
		break;
	case TCPS_FIN_WAIT_2:
		cp = "FIN_WAIT_2";
		break;
	case TCPS_TIME_WAIT:
		cp = "TIME_WAIT";
		break;
	default:
		(void) sprintf(tcpsbuf, "UnknownState(%d)", state);
		cp = tcpsbuf;
		break;
	}
	return (cp);
}

static int odd;

static void
prval_init(void)
{
	odd = 0;
}

static void
prval(char *str, int val)
{
	(void) printf("\t%-20s=%6d", str, val);
	if (odd++ & 1)
		(void) printf("\n");
}

static void
prval_end(void)
{
	if (odd++ & 1)
		(void) printf("\n");
}

/* ----------------------------- STAT_REPORT ------------------------------- */
static void
stat_report(mib_item_t *item)
{
	int	jtemp = 0;

	(void) printf("\n");
	for (; item; item = item->next_item) {
		jtemp++;
		if (Dflag) {
			(void) printf("\n--- Entry %d ---\n", jtemp);
			(void) printf(
	"Group = %ld, mib_id = %ld, length = %ld, valp = 0x%lx\n",
				item->group, item->mib_id,
				item->length, (long)item->valp);
		}
		if (item->mib_id != 0)
			continue;

		switch (item->group) {
		case MIB2_IP: {
			mib2_ip_t	*ip = (mib2_ip_t *)item->valp;

			if (!protocol_selected(IPPROTO_IP))
				break;
			(void) printf("\nIP");
			prval_init();
			prval("ipForwarding",	(int)ip->ipForwarding);
			prval("ipDefaultTTL", 	(int)ip->ipDefaultTTL);
			prval("ipInReceives",	(int)ip->ipInReceives);
			prval("ipInHdrErrors",	(int)ip->ipInHdrErrors);
			prval("ipInAddrErrors",	(int)ip->ipInAddrErrors);
			prval("ipInCksumErrs",	(int)ip->ipInCksumErrs);
			prval("ipForwDatagrams", (int)ip->ipForwDatagrams);
			prval("ipForwProhibits", (int)ip->ipForwProhibits);
			prval("ipInUnknownProtos", (int)ip->ipInUnknownProtos);
			prval("ipInDiscards",	(int)ip->ipInDiscards);
			prval("ipInDelivers",	(int)ip->ipInDelivers);
			prval("ipOutRequests",	(int)ip->ipOutRequests);
			prval("ipOutDiscards",	(int)ip->ipOutDiscards);
			prval("ipOutNoRoutes",	(int)ip->ipOutNoRoutes);
			prval("ipReasmTimeout",	(int)ip->ipReasmTimeout);
			prval("ipReasmReqds",	(int)ip->ipReasmReqds);
			prval("ipReasmOKs",	(int)ip->ipReasmOKs);
			prval("ipReasmFails",	(int)ip->ipReasmFails);
			prval("ipReasmDuplicates", (int)ip->ipReasmDuplicates);
			prval("ipReasmPartDups", (int)ip->ipReasmPartDups);
			prval("ipFragOKs",	(int)ip->ipFragOKs);
			prval("ipFragFails",	(int)ip->ipFragFails);
			prval("ipFragCreates",	(int)ip->ipFragCreates);
			prval("ipRoutingDiscards", (int)ip->ipRoutingDiscards);

			prval("tcpInErrs",	(int)ip->tcpInErrs);
			prval("udpNoPorts",	(int)ip->udpNoPorts);
			prval("udpInCksumErrs",	(int)ip->udpInCksumErrs);
			prval("udpInOverflows",	(int)ip->udpInOverflows);
			prval("rawipInOverflows", (int)ip->rawipInOverflows);
			prval_end();
			break;
			}
		case MIB2_ICMP: {
			mib2_icmp_t	*icmp = (mib2_icmp_t *)item->valp;

			if (!protocol_selected(IPPROTO_ICMP))
				break;
			(void) printf("\nICMP");
			prval_init();
			prval("icmpInMsgs",	(int)icmp->icmpInMsgs);
			prval("icmpInErrors",	(int)icmp->icmpInErrors);
			prval("icmpInCksumErrs", (int)icmp->icmpInCksumErrs);
			prval("icmpInUnknowns",	(int)icmp->icmpInUnknowns);
			prval("icmpInDestUnreachs",
						(int)icmp->icmpInDestUnreachs);
			prval("icmpInTimeExcds", (int)icmp->icmpInTimeExcds);
			prval("icmpInParmProbs", (int)icmp->icmpInParmProbs);
			prval("icmpInSrcQuenchs", (int)icmp->icmpInSrcQuenchs);
			prval("icmpInRedirects", (int)icmp->icmpInRedirects);
			prval("icmpInBadRedirects",
						(int)icmp->icmpInBadRedirects);
			prval("icmpInEchos",	(int)icmp->icmpInEchos);
			prval("icmpInEchoReps",	(int)icmp->icmpInEchoReps);
			prval("icmpInTimestamps",
						(int)icmp->icmpInTimestamps);
			prval("icmpInTimestampReps",
						(int)icmp->icmpInTimestampReps);
			prval("icmpInAddrMasks", (int)icmp->icmpInAddrMasks);
			prval("icmpInAddrMaskReps",
						(int)icmp->icmpInAddrMaskReps);
			prval("icmpInFragNeeded", (int)icmp->icmpInFragNeeded);
			prval("icmpOutMsgs",	(int)icmp->icmpOutMsgs);
			prval("icmpOutDrops",	(int)icmp->icmpOutDrops);
			prval("icmpOutErrors",	(int)icmp->icmpOutErrors);
			prval("icmpOutDestUnreachs",
						(int)icmp->icmpOutDestUnreachs);
			prval("icmpOutTimeExcds", (int)icmp->icmpOutTimeExcds);
			prval("icmpOutParmProbs", (int)icmp->icmpOutParmProbs);
			prval("icmpOutSrcQuenchs",
						(int)icmp->icmpOutSrcQuenchs);
			prval("icmpOutRedirects", (int)icmp->icmpOutRedirects);
			prval("icmpOutEchos",	(int)icmp->icmpOutEchos);
			prval("icmpOutEchoReps", (int)icmp->icmpOutEchoReps);
			prval("icmpOutTimestamps",
						(int)icmp->icmpOutTimestamps);
			prval("icmpOutTimestampReps",
					(int)icmp->icmpOutTimestampReps);
			prval("icmpOutAddrMasks", (int)icmp->icmpOutAddrMasks);
			prval("icmpOutAddrMaskReps",
						(int)icmp->icmpOutAddrMaskReps);
			prval("icmpOutFragNeeded",
						(int)icmp->icmpOutFragNeeded);
			prval("icmpInOverflows", (int)icmp->icmpInOverflows);
			prval_end();
			break;
			}
		case MIB2_TCP: {
			mib2_tcp_t	*tcp = (mib2_tcp_t *)item->valp;

			if (!protocol_selected(IPPROTO_TCP))
				break;
			(void) printf("\nTCP");
			prval_init();
			prval("tcpRtoAlgorithm", (int)tcp->tcpRtoAlgorithm);
			prval("tcpRtoMin",	(int)tcp->tcpRtoMin);
			prval("tcpRtoMax",	(int)tcp->tcpRtoMax);
			prval("tcpMaxConn",	(int)tcp->tcpMaxConn);
			prval("tcpActiveOpens",	(int)tcp->tcpActiveOpens);
			prval("tcpPassiveOpens", (int)tcp->tcpPassiveOpens);
			prval("tcpAttemptFails", (int)tcp->tcpAttemptFails);
			prval("tcpEstabResets",	(int)tcp->tcpEstabResets);
			prval("tcpCurrEstab",	(int)tcp->tcpCurrEstab);
			prval("tcpOutSegs",	(int)tcp->tcpOutSegs);
			prval("tcpOutDataSegs",	(int)tcp->tcpOutDataSegs);
			prval("tcpOutDataBytes", (int)tcp->tcpOutDataBytes);
			prval("tcpRetransSegs",	(int)tcp->tcpRetransSegs);
			prval("tcpRetransBytes", (int)tcp->tcpRetransBytes);
			prval("tcpOutAck",	(int)tcp->tcpOutAck);
			prval("tcpOutAckDelayed", (int)tcp->tcpOutAckDelayed);
			prval("tcpOutUrg",	(int)tcp->tcpOutUrg);
			prval("tcpOutWinUpdate", (int)tcp->tcpOutWinUpdate);
			prval("tcpOutWinProbe",	(int)tcp->tcpOutWinProbe);
			prval("tcpOutControl",	(int)tcp->tcpOutControl);
			prval("tcpOutRsts",	(int)tcp->tcpOutRsts);
			prval("tcpOutFastRetrans", (int)tcp->tcpOutFastRetrans);
			prval("tcpInSegs",	(int)tcp->tcpInSegs);
			prval_end();
			prval("tcpInAckSegs",	(int)tcp->tcpInAckSegs);
			prval("tcpInAckBytes",	(int)tcp->tcpInAckBytes);
			prval("tcpInDupAck",	(int)tcp->tcpInDupAck);
			prval("tcpInAckUnsent",	(int)tcp->tcpInAckUnsent);
			prval("tcpInInorderSegs",
						(int)tcp->tcpInDataInorderSegs);
			prval("tcpInInorderBytes",
					(int)tcp->tcpInDataInorderBytes);
			prval("tcpInUnorderSegs",
						(int)tcp->tcpInDataUnorderSegs);
			prval("tcpInUnorderBytes",
					(int)tcp->tcpInDataUnorderBytes);
			prval("tcpInDupSegs",	(int)tcp->tcpInDataDupSegs);
			prval("tcpInDupBytes",	(int)tcp->tcpInDataDupBytes);
			prval("tcpInPartDupSegs",
						(int)tcp->tcpInDataPartDupSegs);
			prval("tcpInPartDupBytes",
					(int)tcp->tcpInDataPartDupBytes);
			prval("tcpInPastWinSegs",
					(int)tcp->tcpInDataPastWinSegs);
			prval("tcpInPastWinBytes",
					(int)tcp->tcpInDataPastWinBytes);
			prval("tcpInWinProbe",	(int)tcp->tcpInWinProbe);
			prval("tcpInWinUpdate",	(int)tcp->tcpInWinUpdate);
			prval("tcpInClosed",	(int)tcp->tcpInClosed);
			prval("tcpRttNoUpdate",	(int)tcp->tcpRttNoUpdate);
			prval("tcpRttUpdate",	(int)tcp->tcpRttUpdate);
			prval("tcpTimRetrans",	(int)tcp->tcpTimRetrans);
			prval("tcpTimRetransDrop", (int)tcp->tcpTimRetransDrop);
			prval("tcpTimKeepalive", (int)tcp->tcpTimKeepalive);
			prval("tcpTimKeepaliveProbe",
						(int)tcp->tcpTimKeepaliveProbe);
			prval("tcpTimKeepaliveDrop",
						(int)tcp->tcpTimKeepaliveDrop);
			prval_end();
			break;
			}
		case MIB2_UDP: {
			mib2_udp_t	*udp = (mib2_udp_t *)item->valp;

			if (!protocol_selected(IPPROTO_UDP))
				break;
			(void) printf("\nUDP\n");
			prval_init();
			prval("udpInDatagrams",	(int)udp->udpInDatagrams);
			prval("udpInErrors",	(int)udp->udpInErrors);
			prval("udpOutDatagrams", (int)udp->udpOutDatagrams);
			prval_end();
			break;
			}
		case EXPER_IGMP: {
			struct igmpstat	*igps = (struct igmpstat *)item->valp;

			if (!protocol_selected(IPPROTO_IGMP))
				break;
			igmp_stats(igps);
			break;
		}
		case EXPER_DVMRP:
			break;
		default:
			(void) printf("unknown group =%6ld\n", item->group);
			break;
		}
	}
	(void) fflush(stdout);
}

/* ----------------------------- MRT_STAT_REPORT --------------------------- */
static void
mrt_stat_report(mib_item_t *item)
{
	int	jtemp = 0;

	(void) printf("\n");
	for (; item; item = item->next_item) {
		jtemp++;
		if (Dflag) {
			(void) printf("\n--- Entry %d ---\n", jtemp);
			(void) printf(
		"Group = %ld, mib_id = %ld, length = %ld, valp = 0x%lx\n",
				item->group, item->mib_id,
				item->length, (long)item->valp);
		}
		if (item->mib_id != 0)
			continue;

		switch (item->group) {
		case MIB2_IP:
		case MIB2_ICMP:
		case MIB2_TCP:
		case MIB2_UDP:
		case EXPER_IGMP:
			break;
		case EXPER_DVMRP: {
			struct mrtstat	*mrts = (struct mrtstat *)item->valp;
			mrt_stats(mrts);
			break;
		}
		default:
			(void) printf("unknown group =%6ld\n", item->group);
			break;
		}
	}
	(void) fflush(stdout);
}

/* --------------------- IF_REPORT  -------------------------- */

static void
if_report(mib_item_t *item, char *ifname, int interval)
{
	int		jtemp = 0;
	char		buf1[32];
	char		buf2[32];
	char		buf3[32];
	kstat_t		*ksp;
	mib2_ipAddrEntry_t		*ap;

	if (kc == NULL) {
		(void) fprintf(stderr,
				"-i option only valid for live kernels\n");
		return;
	}

	for (; item; item = item->next_item) {
		++jtemp;
		if (Dflag) {
			(void) printf("\n--- Entry %d ---\n", jtemp);
			(void) printf(
	"Group = %ld, mib_id = %ld, length = %ld, valp = 0x%lx\n",
			item->group, item->mib_id, item->length,
			(long)item->valp);
		}
		if (item->group != MIB2_IP)
			continue;

		switch (item->mib_id) {

		case MIB2_IP_20:
			ap = (mib2_ipAddrEntry_t *)item->valp;

			if (interval)
				if_interval(item, ifname, interval);

(void) printf("%-5.5s %-5.5s%-13.13s %-14.14s %-6.6s %-5.5s %-6.6s %-5.5s",
				"Name", "Mtu", "Net/Dest", "Address",
				"Ipkts", "Ierrs", "Opkts", "Oerrs");
			(void) printf(" %-6.6s %-6.6s\n", "Collis", "Queue");

			for (; (char *)ap < item->valp + item->length; ap++) {

				(void) octetstr(buf1, &ap->ipAdEntIfIndex, 'a');

				if (ifname)
					if (strcmp(ifname, buf1) != 0)
						continue;

				if ((ksp = kstat_lookup(kc, NULL, -1, buf1))
				    != NULL)
					(void) safe_kstat_read(kc, ksp, NULL);

				(void) printf("%-5s %-5lu",
					buf1,
					ap->ipAdEntInfo.ae_mtu);
				if (ap->ipAdEntInfo.ae_flags & IFF_POINTOPOINT)
(void) printf("%-13s ", ipastr(buf2, ap->ipAdEntInfo.ae_pp_dst_addr));
				else
					(void) printf("%-13s ",
						hostanstr(buf2, ap->ipAdEntAddr,
						ap->ipAdEntNetMask));
				(void) printf("%-14s ",
					ipastr(buf3, ap->ipAdEntAddr));
				(void) printf("%-6lu ",
					kstat_named_value(ksp, "ipackets"));
				(void) printf("%-5lu ",
					kstat_named_value(ksp, "ierrors"));
				(void) printf("%-6lu ",
					kstat_named_value(ksp, "opackets"));
				(void) printf("%-5lu ",
					kstat_named_value(ksp, "oerrors"));
				(void) printf("%-6lu ",
					kstat_named_value(ksp, "collisions"));
				(void) printf("%-6lu\n",
					kstat_named_value(ksp, "queue"));
			}
			break;

		case 0:
		case MIB2_IP_22:
		case MIB2_IP_21:
		case EXPER_IP_GROUP_MEMBERSHIP:
			break;

		default:
			(void) printf("unknown group = %ld\n", item->group);
			break;
		}
	}
	(void) fflush(stdout);
}

struct	ifstat {
	int	ipackets;
	int	ierrors;
	int	opackets;
	int	oerrors;
	int	collisions;
};

static struct	ifstat	zerostat = {
	0, 0, 0, 0, 0
};

static void
if_interval(mib_item_t *item, char *ifname, int interval)
{
	char	ifbuf[32];
	char	buf[32];
	kstat_t *ksp;
	mib2_ipAddrEntry_t		*ap;
	struct	ifstat	old, new;
	struct	ifstat	oldsum, newsum;
	struct	ifstat	t;

	/*
	 * Find the "right" entry.
	 */
	ap = (mib2_ipAddrEntry_t *)item->valp;
	for (; (char *)ap < item->valp + item->length; ap++) {

		(void) octetstr(ifbuf, &ap->ipAdEntIfIndex, 'a');

		if (ifname) {
			if (strcmp(ifname, ifbuf) == 0)
				break;
		} else if (strcmp(ifbuf, "lo0") != 0) {
			ifname = ifbuf;
			break;
		}
	}

	(void) printf("    input   %-6.6s    output        ", ifname);
	(void) printf("   input  (Total)    output\n");
	(void) printf("%-7.7s %-5.5s %-7.7s %-5.5s %-6.6s ",
		"packets", "errs", "packets", "errs", "colls");
	(void) printf("%-7.7s %-5.5s %-7.7s %-5.5s %-6.6s\n",
		"packets", "errs", "packets", "errs", "colls");

	old = zerostat;
	new = zerostat;
	oldsum = zerostat;

	while (1) {

		newsum = zerostat;

		ap = (mib2_ipAddrEntry_t *)item->valp;
		for (; (char *)ap < item->valp + item->length; ap++) {

			(void) octetstr(buf, &ap->ipAdEntIfIndex, 'a');

			ksp = kstat_lookup(kc, NULL, -1, buf);
			if (ksp && ksp->ks_type == KSTAT_TYPE_NAMED)
				(void) safe_kstat_read(kc, ksp, NULL);

			t.ipackets	= kstat_named_value(ksp, "ipackets");
			t.ierrors	= kstat_named_value(ksp, "ierrors");
			t.opackets	= kstat_named_value(ksp, "opackets");
			t.oerrors	= kstat_named_value(ksp, "oerrors");
			t.collisions	= kstat_named_value(ksp, "collisions");

			if (strcmp(buf, ifname) == 0)
				new = t;

			newsum.ipackets += t.ipackets;
			newsum.ierrors += t.ierrors;
			newsum.opackets += t.opackets;
			newsum.oerrors += t.oerrors;
			newsum.collisions += t.collisions;
		}

		(void) printf("%-7d %-5d %-7d %-5d %-6d ",
			new.ipackets - old.ipackets,
			new.ierrors - old.ierrors,
			new.opackets - old.opackets,
			new.oerrors - old.oerrors,
			new.collisions - old.collisions);
		(void) printf("%-7d %-5d %-7d %-5d %-6d\n",
			newsum.ipackets - oldsum.ipackets,
			newsum.ierrors - oldsum.ierrors,
			newsum.opackets - oldsum.opackets,
			newsum.oerrors - oldsum.oerrors,
			newsum.collisions - oldsum.collisions);

		old = new;
		oldsum = newsum;

		(void) sleep((unsigned)interval);
	}
}

/* --------------------- GROUP_REPORT (netstat -g) ------------------------- */

static void
group_report(mib_item_t *item)
{
	int		jtemp = 0;
	char		buf1[32];
	char		buf2[32];
	ip_member_t	*ipmp;

	for (; item; item = item->next_item) {
		jtemp++;
		if (Dflag) {
			(void) printf("\n--- Entry %d ---\n", jtemp);
			(void) printf(
	"Group = %ld, mib_id = %ld, length = %ld, valp = 0x%lx\n",
			item->group, item->mib_id, item->length,
			(long)item->valp);
		}
		if (item->group != MIB2_IP)
			continue;

		switch (item->mib_id) {

		case EXPER_IP_GROUP_MEMBERSHIP:
			ipmp = (ip_member_t *)item->valp;
			if (Dflag)
		(void) printf("%ld records for ipGroupMember:\n",
					item->length/sizeof (ip_member_t));

		(void) printf("Group Memberships\n");
		(void) printf("Interface Group                RefCnt\n");
		(void) printf("--------- -------------------- ------\n");
			while ((char *)ipmp < item->valp + item->length) {
				(void) printf("%-9s %-20s %6lu\n",
				octetstr(buf1,
					&ipmp->ipGroupMemberIfIndex,
					'a'),
				ipastr(buf2, ipmp->ipGroupMemberAddress),
					ipmp->ipGroupMemberRefCnt);
				ipmp++;
			}
			break;

		case 0:
		case MIB2_IP_20:
		case MIB2_IP_22:
		case MIB2_IP_21:
			break;

		default:
			(void) printf("unknown group = %ld\n", item->group);
			break;
		}
	}
	(void) fflush(stdout);
}

/* --------------------- ARP_REPORT (netstat -i) -------------------------- */

static void
arp_report(mib_item_t *item)
{
	int		jtemp = 0;
	char		buf1[32];
	char		buf2[32];
	char		buf3[32];
	char		buf4[32];
	char		xbuf[64];
	mib2_ipNetToMediaEntry_t	*np;
	int		flags;

	for (; item; item = item->next_item) {
		jtemp++;
		if (Dflag) {
			(void) printf("\n--- Entry %d ---\n", jtemp);
			(void) printf(
	"Group = %ld, mib_id = %ld, length = %ld, valp = 0x%lx\n",
			item->group, item->mib_id, item->length,
			(long)item->valp);
		}
		if (item->group != MIB2_IP)
			continue;

		switch (item->mib_id) {

		case MIB2_IP_22:
			np = (mib2_ipNetToMediaEntry_t *)item->valp;
			if (Dflag)
		(void) printf("%lu records for ipNetToMediaEntryTable:\n",
				item->length/sizeof (mib2_ipNetToMediaEntry_t));
			(void) printf("Net to Media Table\n");
(void) printf("Device   IP Address               Mask      ");
(void) printf("Flags   Phys Addr \n");
(void) printf("------ -------------------- --------------- ");
(void) printf("----- ---------------\n");
			while ((char *)np < item->valp + item->length) {
				buf4[0] = '\0';
				flags = np->ipNetToMediaInfo.ntm_flags;
				if (flags & ACE_F_PERMANENT)
					(void) strcat(buf4, "S");
				if (flags & ACE_F_PUBLISH)
					(void) strcat(buf4, "P");
				if (flags & ACE_F_DYING)
					(void) strcat(buf4, "D");
				if (!(flags & ACE_F_RESOLVED))
					(void) strcat(buf4, "U");
				if (flags & ACE_F_MAPPING)
					(void) strcat(buf4, "M");
				(void) printf("%-6s %-20s %-15s %-5s %s\n",
				octetstr(buf1, &np->ipNetToMediaIfIndex,
						'a'),
				ipastr(buf2, np->ipNetToMediaNetAddress),
				octetstr(buf3,
					&np->ipNetToMediaInfo.ntm_mask, 'd'),
				buf4,
				octetstr(xbuf,
					&np->ipNetToMediaPhysAddress, 'h'));
				np++;
			}
			break;

		case 0:
		case MIB2_IP_20:
		case MIB2_IP_21:
		case EXPER_IP_GROUP_MEMBERSHIP:
			break;

		default:
			(void) printf("unknown group = %ld\n", item->group);
			break;
		}
	}
	(void) fflush(stdout);
}

/* ------------------------- IRE_REPORT (netstat -r) ------------------------ */

static void
ire_report(mib_item_t *item)
{
	int			jtemp = 0;
	mib2_ipRouteEntry_t	*rp;
	char			buf1[32];
	char			buf2[32];
	char			buf3[32];
	char			buf4[32];
	char			flags[5];

	for (; item; item = item->next_item) {
		jtemp++;
		if (Dflag) {
			(void) printf("\n--- Entry %d ---\n", jtemp);
	(void) printf("Group = %ld, mib_id = %ld, length = %ld, valp = 0x%lx\n",
			item->group, item->mib_id,
			item->length, (long)item->valp);
		}
		if (item->group != MIB2_IP || item->mib_id != MIB2_IP_21)
			continue;

		rp = (mib2_ipRouteEntry_t *)item->valp;
		if (Dflag)
			(void) printf("%lu records for ipRouteEntryTable:\n",
				item->length/sizeof (mib2_ipRouteEntry_t));

		if (Vflag) {
			(void) printf("\nIRE Table:\n");
(void) printf("  Destination             Mask           Gateway          ");
(void) printf("Device Mxfrg  Rtt  Ref Flg  Out  In/Fwd\n");
(void) printf("-------------------- --------------- -------------------- ");
(void) printf("------ ----- ----- --- --- ----- ------\n");
		} else {
			(void) printf("\nRouting Table:\n");
			(void) printf(
"  Destination           Gateway           Flags  Ref   Use   Interface\n");
			(void) printf(
"-------------------- -------------------- ----- ----- ------ ---------\n");
		}
		while ((char *)rp < item->valp + item->length) {
			int 	network = 1;

			(void) strcpy(flags, "U");
			if (rp->ipRouteInfo.re_ire_type == IRE_GATEWAY ||
			    rp->ipRouteInfo.re_ire_type == IRE_NET ||
			    rp->ipRouteInfo.re_ire_type == IRE_ROUTE_ASSOC ||
			    rp->ipRouteInfo.re_ire_type == IRE_ROUTE_REDIRECT)
				(void) strcat(flags, "G");
			if (rp->ipRouteMask == (IpAddress)-1) {
				(void) strcat(flags, "H");
				network = 0;
			}
			if (rp->ipRouteInfo.re_ire_type == IRE_ROUTE_REDIRECT)
				(void) strcat(flags, "D");
			if (rp->ipRouteInfo.re_ire_type == IRE_ROUTE)
				/* Address resolution */
				(void) strcat(flags, "A");
			if (rp->ipRouteInfo.re_ire_type == IRE_BROADCAST)
				(void) strcat(flags, "B");	/* Broadcast */
			if (rp->ipRouteInfo.re_ire_type == IRE_LOCAL)
				(void) strcat(flags, "L");	/* Local */

			if (!(Aflag ||
				(rp->ipRouteInfo.re_ire_type != IRE_ROUTE &&
				rp->ipRouteInfo.re_ire_type != IRE_BROADCAST &&
				rp->ipRouteInfo.re_ire_type != IRE_LOCAL))) {
				rp++;
				continue;
			}
			if (Vflag)
				(void) printf(
		"%-20s %-15s %-20s %-6s %5lu%c%5lu %3lu %-4s%6lu%6lu\n",
				network ?
				ipanstr(buf1, rp->ipRouteDest,
					rp->ipRouteMask) :
				ipastr(buf1, rp->ipRouteDest),
				ipamstr(buf2, rp->ipRouteMask),
				rp->ipRouteNextHop
				? ipastr(buf3, rp->ipRouteNextHop) : "   --",
				octetstr(buf4, &rp->ipRouteIfIndex, 'a'),
				rp->ipRouteInfo.re_max_frag,
				rp->ipRouteInfo.re_frag_flag ? '*' : ' ',
				rp->ipRouteInfo.re_rtt,
				rp->ipRouteInfo.re_ref,
				flags,
				rp->ipRouteInfo.re_obpkt,
				rp->ipRouteInfo.re_ibpkt);
			else
		(void) printf("%-20s %-20s  %-4s  %4lu%7lu  %s\n",
				network ?
				ipanstr(buf1, rp->ipRouteDest,
					rp->ipRouteMask) :
				ipastr(buf1, rp->ipRouteDest),
				rp->ipRouteNextHop
				? ipastr(buf3, rp->ipRouteNextHop) : "    --",
				flags,
				rp->ipRouteInfo.re_ref,
				rp->ipRouteInfo.re_obpkt +
				rp->ipRouteInfo.re_ibpkt,
				octetstr(buf4, &rp->ipRouteIfIndex, 'a'));
			rp++;
		}
		break;
	}
	(void) fflush(stdout);
}

/* ------------------------------ TCP_REPORT------------------------------- */

static void
tcp_report(mib_item_t *item)
{
	int			jtemp = 0;
	mib2_tcpConnEntry_t	*tp;
	char			buf1[32];
	char			buf2[32];

	for (; item; item = item->next_item) {
		jtemp++;
		if (Dflag) {
			(void) printf("\n--- Entry %d ---\n", jtemp);
			(void) printf(
	"Group = %ld, mib_id = %ld, length = %ld, valp = 0x%lx\n",
				item->group, item->mib_id,
				item->length, (long)item->valp);
		}
		if (Dflag)
			(void) printf("%lu records for tcpConnEntryTable:\n",
				item->length/sizeof (mib2_tcpConnEntry_t));

		if (item->group != MIB2_TCP || item->mib_id != MIB2_TCP_13)
			continue;

		tp = (mib2_tcpConnEntry_t *)item->valp;
		(void) printf("\nTCP\n");
		if (Vflag) {
(void) printf("Local/Remote Address Swind  Snext     Suna ");
(void) printf("  Rwind  Rnext     Rack    Rto   Mss  State\n");
(void) printf("-------------------- ----- -------- -------- ");
(void) printf("----- -------- -------- ----- ----- ------\n");
		} else {
(void) printf("   Local Address        Remote Address    ");
(void) printf("Swind Send-Q Rwind Recv-Q  State\n");
(void) printf("-------------------- -------------------- ");
(void) printf("----- ------ ----- ------ -------\n");
		}

		while ((char *)tp < item->valp + item->length) {
			if (!(Aflag || tp->tcpConnEntryInfo.ce_state >=
			    TCPS_ESTABLISHED)) {
				tp++;
				continue;
			}
			if (Vflag)
				(void) printf(
		"%-20s\n%-20s %5lu %08lx %08lx %5lu %08lx %08lx %5lu %5lu %s\n",
					ipapstr(buf1,
						tp->tcpConnLocalAddress,
						tp->tcpConnLocalPort, "tcp"),
					ipapstr(buf2,
						tp->tcpConnRemAddress,
						tp->tcpConnRemPort, "tcp"),
					tp->tcpConnEntryInfo.ce_swnd,
					tp->tcpConnEntryInfo.ce_snxt,
					tp->tcpConnEntryInfo.ce_suna,
					tp->tcpConnEntryInfo.ce_rwnd,
					tp->tcpConnEntryInfo.ce_rnxt,
					tp->tcpConnEntryInfo.ce_rack,
					tp->tcpConnEntryInfo.ce_rto,
					tp->tcpConnEntryInfo.ce_mss,
					mitcp_state(
						tp->tcpConnEntryInfo.ce_state));
			else {
				int sq = (int)tp->tcpConnEntryInfo.ce_snxt -
					(int)tp->tcpConnEntryInfo.ce_suna - 1;
				int rq = (int)tp->tcpConnEntryInfo.ce_rnxt -
					(int)tp->tcpConnEntryInfo.ce_rack;

			(void) printf("%-20s %-20s %5lu %6d %5lu %6d %s\n",
					ipapstr(buf1, tp->tcpConnLocalAddress,
						tp->tcpConnLocalPort, "tcp"),
					ipapstr(buf2, tp->tcpConnRemAddress,
						tp->tcpConnRemPort, "tcp"),
					tp->tcpConnEntryInfo.ce_swnd,
					(sq >= 0) ? sq : 0,
					tp->tcpConnEntryInfo.ce_rwnd,
					(rq >= 0) ? rq : 0,
					mitcp_state(
						tp->tcpConnEntryInfo.ce_state));
			}
			tp++;
		}
		break;
	}
	(void) fflush(stdout);
}

/* ------------------------------- UDP_REPORT------------------------------- */

static void
udp_report(mib_item_t *item)
{
	int			jtemp = 0;
	char			buf1[32];
	mib2_udpEntry_t		*ude;

	for (; item; item = item->next_item) {
		jtemp++;
		if (Dflag) {
			(void) printf("\n--- Entry %d ---\n", jtemp);
			(void) printf(
	"Group = %ld, mib_id = %ld, length = %ld, valp = 0x%lx\n",
				item->group, item->mib_id,
				item->length, (long)item->valp);
		}
		if (item->group != MIB2_UDP || item->mib_id != MIB2_UDP_5)
			continue;

		if (Dflag)
			(void) printf("%lu records for udpEntryTable:\n",
				item->length/sizeof (mib2_udpEntry_t));
		ude = (mib2_udpEntry_t *)item->valp;
		(void) printf("\nUDP\n   Local Address      State\n");
		(void) printf("-------------------- -------\n");
		/*	xxx.xxx.xxx.xxx,pppp  sss... */
		while ((char *)ude < item->valp + item->length) {
			char	*cp;
			switch (ude->udpEntryInfo.ue_state) {
			case MIB2_UDP_unbound:
				cp = "Unbound";
				break;
			case MIB2_UDP_idle:
				cp = "Idle";
				break;
			default:
				cp = "Unknown";
				break;
			}
			(void) printf("%-20s  %s\n",
				ipapstr(buf1,
				ude->udpLocalAddress, ude->udpLocalPort,
				"udp"),
				cp);
			ude++;
		}
		break;
	}
	(void) fflush(stdout);
}

static char *
plural(int n)
{

	return (n != 1 ? "s" : "");
}

static char *
pluraly(int n)
{
	return (n != 1 ? "ies" : "y");
}

static char *
plurales(int n)
{
	return (n != 1 ? "es" : "");
}

#define	PLURAL(n) plural((int)n)
#define	PLURALY(n) pluraly((int)n)
#define	PLURALES(n) plurales((int)n)

void
igmp_stats(struct igmpstat *igps)
{
	(void) printf("IGMP:\n");
	(void) printf(" %10u message%s received\n",
		igps->igps_rcv_total, PLURAL(igps->igps_rcv_total));
	(void) printf(" %10u message%s received with too few bytes\n",
		igps->igps_rcv_tooshort, PLURAL(igps->igps_rcv_tooshort));
	(void) printf(" %10u message%s received with bad checksum\n",
		igps->igps_rcv_badsum, PLURAL(igps->igps_rcv_badsum));
	(void) printf(" %10u membership quer%s received\n",
		igps->igps_rcv_queries, PLURALY(igps->igps_rcv_queries));
	(void) printf(
" %10u membership quer%s received with invalid field(s)\n",
		igps->igps_rcv_badqueries, PLURALY(igps->igps_rcv_badqueries));
	(void) printf(" %10u membership report%s received\n",
		igps->igps_rcv_reports, PLURAL(igps->igps_rcv_reports));
	(void) printf(
" %10u membership report%s received with invalid field(s)\n",
		igps->igps_rcv_badreports, PLURAL(igps->igps_rcv_badreports));
	(void) printf(
" %10u membership report%s received for groups to which we belong\n",
		igps->igps_rcv_ourreports, PLURAL(igps->igps_rcv_ourreports));
	(void) printf(" %10u membership report%s sent\n",
		igps->igps_snd_reports, PLURAL(igps->igps_snd_reports));
}

void
mrt_stats(struct mrtstat *mrts)
{
	(void) printf("multicast routing:\n");
	(void) printf(" %10lu packet%s potentially forwarded\n",
		mrts->mrts_fwd_in, PLURAL(mrts->mrts_fwd_in));
	(void) printf(" %10lu resulting packet%s sent out\n",
		mrts->mrts_fwd_out, PLURAL(mrts->mrts_fwd_out));
	(void) printf(" %10lu packet%s not sent out due to lack of resources\n",
		mrts->mrts_fwd_drop, PLURAL(mrts->mrts_fwd_drop));
	(void) printf(" %10lu multicast route lookup%s\n",
		mrts->mrts_mrt_lookups, PLURAL(mrts->mrts_mrt_lookups));
	(void) printf(" %10lu multicast route cache miss%s\n",
		mrts->mrts_mrt_misses, PLURALES(mrts->mrts_mrt_misses));
	(void) printf(" %10lu group address lookup%s\n",
		mrts->mrts_grp_lookups, PLURAL(mrts->mrts_grp_lookups));
	(void) printf(" %10lu group address cache miss%s\n",
		mrts->mrts_grp_misses, PLURALES(mrts->mrts_grp_misses));
	(void) printf(" %10lu datagram%s with no route for origin\n",
		mrts->mrts_no_route, PLURAL(mrts->mrts_no_route));
	(void) printf(" %10lu datagram%s with malformed tunnel options\n",
		mrts->mrts_bad_tunnel, PLURAL(mrts->mrts_bad_tunnel));
	(void) printf(" %10lu datagram%s with no room for tunnel options\n",
		mrts->mrts_cant_tunnel, PLURAL(mrts->mrts_cant_tunnel));
}

/* --------------------- MRT_REPORT (netstat -M) -------------------------- */

static void
mrt_report(mib_item_t *item)
{
	int		jtemp = 0;
	struct vifinfo	*vip;
	vifi_t		vifi, prev_vifi;
	struct mrtctl	*mrtcp;

	for (; item; item = item->next_item) {
		jtemp++;
		if (Dflag) {
			(void) printf("\n--- Entry %d ---\n", jtemp);
			(void) printf(
	"Group = %ld, mib_id = %ld, length = %ld, valp = 0x%lx\n",
			item->group, item->mib_id, item->length,
			(long)item->valp);
		}
		if (item->group != EXPER_DVMRP)
			continue;

		switch (item->mib_id) {

		case EXPER_DVMRP_VIF:
			vip = (struct vifinfo *)item->valp;
			if (Dflag)
				(void) printf("%lu records for ipVifTable:\n",
					item->length/sizeof (struct vifinfo));
			(void) printf("\nVirtual Interface Table\n%s%s",
				" Vif Threshold Local-Address        ",
				"Remote-Address        Groups\n");

			prev_vifi = (vifi_t)-1;
			while ((char *)vip < item->valp + item->length) {
				if (vip->vifi_lcl_addr.s_addr == 0) {
					vip++;
					prev_vifi = (vifi_t)-1;
					continue;
				}
				if (vip->vifi_vifi == prev_vifi) {
					/* Only print the group member */
					(void) printf("%57s", "");
				} else {
					(void) printf(" %2u     %3u    %-20s",
						vip->vifi_vifi,
						vip->vifi_threshold,
				routename(vip->vifi_lcl_addr.s_addr));
					(void) printf(" %-20s ",
			(vip->vifi_flags & VIFF_TUNNEL) ?
			routename(vip->vifi_rmt_addr.s_addr) : "");
				}
				if ((vip->vifi_flags & VIFF_TUNNEL) == 0 &&
					vip->vifi_grp_addr.s_addr != 0)
					(void) printf(" %-20s",
			routename(vip->vifi_grp_addr.s_addr));
				(void) printf("\n");
				prev_vifi = vip->vifi_vifi;
				vip++;
			}
			break;

		case EXPER_DVMRP_MRT:
			mrtcp = (struct mrtctl *)item->valp;
			if (Dflag)
				(void) printf("%lu records for ipMrtTable:\n",
					item->length/sizeof (struct vifinfo));
			(void) printf("\nMulticast Routing Table\n%s",
				" Origin-Subnet       In-Vif  Out-Vifs\n");
			while ((char *)mrtcp < item->valp + item->length) {
				(void) printf(" %-20s  %2u   ",
					netname(mrtcp->mrtc_origin.s_addr,
						mrtcp->mrtc_originmask.s_addr),
					mrtcp->mrtc_parent);
				for (vifi = 0; vifi < MAXVIFS; ++vifi) {
					if (VIFM_ISSET(vifi,
							mrtcp->mrtc_children)) {
						(void) printf(" %u%c",
							vifi,
							VIFM_ISSET(vifi,
							mrtcp->mrtc_leaves) ?
							'*' : ' ');
					}
				}
				(void) printf("\n");
				mrtcp++;
			}
			break;

		case 0:
			break;

		default:
			(void) printf("unknown group = %ld\n", item->group);
			break;
		}
	}
	(void) fflush(stdout);
}

/* XXX fix av, ac in main() */
static void
k_report(int argc, char **argv)
{
	kstat_t *ksp;

	if (kc == NULL) {
		(void) fprintf(stderr,
				"-k option only valid for live kernels\n");
		return;
	}

	if (argc > 0) {
		while (argc > 0) {
			ksp = kstat_lookup(kc, NULL, -1, *argv);
			if (ksp && ksp->ks_type == KSTAT_TYPE_NAMED) {
				(void) safe_kstat_read(kc, ksp, NULL);
				print_kn(ksp);
			}
			argc--;
			argv++;
		}
		return;
	}

	for (ksp = kc->kc_chain; ksp; ksp = ksp->ks_next) {
		if (ksp->ks_type == KSTAT_TYPE_NAMED) {
			(void) safe_kstat_read(kc, ksp, NULL);
			print_kn(ksp);
		}
	}
}

static void
print_kn(kstat_t *ksp)
{
	kstat_named_t *knp;
	int	i, col;

	(void) printf("%s:\n", ksp->ks_name);

	col = 0;
	knp = KSTAT_NAMED_PTR(ksp);
	for (i = 0; i < ksp->ks_ndata; i++) {
		(void) printf("%s %lu ", knp[i].name, knp[i].value.ul);
		col += strlen(knp[i].name);
		col += 4;	/* approx. */
		if (col >= 60) {
			(void) printf("\n");
			col = 0;
		}
	}

	if (col > 0)
		(void) printf("\n");
	(void) printf("\n");
}

/*
 * Get the stats for the cache named 'name'.  If prefix != 0, then
 * interpret the name as a prefix, and sum up stats for all caches
 * named 'name*'.
 */
static void
kmem_cache_stats(char *title, char *name, int prefix, int *total_bytes)
{
	int len;
	int alloc, total_alloc = 0;
	int alloc_fail, total_alloc_fail = 0;
	int buf_size = 0;
	int buf_avail;
	int buf_total;
	int buf_max, total_buf_max = 0;
	int buf_inuse, total_buf_inuse = 0;
	kstat_t *ksp;
	char buf[256];

	if (prefix)
		len = strlen(name);
	else
		len = 256;

	for (ksp = kc->kc_chain; ksp != NULL; ksp = ksp->ks_next) {

		if (strcmp(ksp->ks_class, "kmem_cache") != 0)
			continue;

		if (strncmp(ksp->ks_name, name, len) != 0)
			continue;

		safe_kstat_read(kc, ksp, NULL);

		alloc		= kstat_named_value(ksp, "alloc");
		alloc_fail	= kstat_named_value(ksp, "alloc_fail");
		buf_size	= kstat_named_value(ksp, "buf_size");
		buf_avail	= kstat_named_value(ksp, "buf_avail");
		buf_total	= kstat_named_value(ksp, "buf_total");
		buf_max		= kstat_named_value(ksp, "buf_max");
		buf_inuse	= buf_total - buf_avail;

		if (Vflag && prefix) {
			sprintf(buf, "%s%s", title, ksp->ks_name + len);
			(void) printf("    %-18s %6d %9d %11d %11d\n", buf,
				buf_inuse, buf_max, alloc, alloc_fail);
		}

		total_alloc		+= alloc;
		total_alloc_fail	+= alloc_fail;
		total_buf_max		+= buf_max;
		total_buf_inuse		+= buf_inuse;
		*total_bytes		+= buf_inuse * buf_size;
	}

	if (buf_size == 0) {
		printf("%-22s [couldn't find statistics for %s]\n",
			title, name);
		return;
	}

	if (Vflag && prefix)
		sprintf(buf, "%s_%s", title, "total");
	else
		sprintf(buf, "%s", title);

	(void) printf("%-22s %6d %9d %11d %11d\n", buf,
		total_buf_inuse, total_buf_max, total_alloc, total_alloc_fail);
}

static void
m_report(void)
{
	int total_bytes = 0;

	(void) printf("streams allocation:\n");
	(void) printf("%63s\n", "cumulative  allocation");
	(void) printf("%63s\n", "current   maximum       total    failures");

	kmem_cache_stats("streams", "stream_head_cache", 0, &total_bytes);
	kmem_cache_stats("queues", "queue_cache", 0, &total_bytes);
	kmem_cache_stats("msg", "streams_msg", 1, &total_bytes);
	kmem_cache_stats("linkblk", "linkinfo_cache", 0, &total_bytes);
	kmem_cache_stats("strevent", "strevent_cache", 0, &total_bytes);
	kmem_cache_stats("syncq", "syncq_cache", 0, &total_bytes);
	kmem_cache_stats("qband", "qband_cache", 0, &total_bytes);

	(void) printf("\n%d Kbytes allocated for streams data\n",
		total_bytes / 1024);
}

/* --------------------------------- */

static char *
routename(u_long addr)
{
	register char  *cp;
	static char	line[MAXHOSTNAMELEN + 1];
	struct hostent *hp;
	static char	domain[MAXHOSTNAMELEN + 1];
	static int	first = 1;

	if (first) {
		first = 0;
		if (sysinfo(SI_HOSTNAME, domain, MAXHOSTNAMELEN) != -1 &&
			(cp = strchr(domain, '.'))) {
			(void) strcpy(domain, cp + 1);
		} else
			domain[0] = 0;
	}
	cp = 0;
	if (!Nflag) {
		hp = gethostbyaddr((char *)&addr, sizeof (u_long), AF_INET);
		if (hp) {
			if ((cp = strchr(hp->h_name, '.')) != 0 &&
					strcmp(cp + 1, domain) == 0)
				*cp = 0;
			cp = hp->h_name;
		}
	}
	if (cp)
		(void) strcpy(line, cp);
	else {
		struct in_addr in;

		in.s_addr = addr;
		(void) strcpy(line, inet_ntoa(in));
	}
	return (line);
}

/*
 * Return the name of the network whose address is given. The address is
 * assumed to be that of a net or subnet, not a host.
 */
static char *
netname(u_long addr, u_long mask)
{
	char		*cp = 0;
	static char	line[50];
	struct netent	*np = 0;
	struct hostent	*hp;
	u_long		net;
	int		subnetshift;

	if (addr == INADDR_ANY)
		return ("default");
	if (!Nflag && addr) {
		if (mask == 0) {
			if (IN_CLASSA(addr)) {
				mask = (u_long)IN_CLASSA_NET;
				subnetshift = 8;
			} else if (IN_CLASSB(addr)) {
				mask = (u_long)IN_CLASSB_NET;
				subnetshift = 8;
			} else {
				mask = (u_long)IN_CLASSC_NET;
				subnetshift = 4;
			}
			/*
			 * If there are more bits than the standard mask
			 * would suggest, subnets must be in use. Guess at
			 * the subnet mask, assuming reasonable width subnet
			 * fields.
			 */
			while (addr & ~mask)
				/* compiler doesn't sign extend! */
				mask = (mask | ((long)mask >> subnetshift));
		}
		net = addr & mask;
		while ((mask & 1) == 0)
			mask >>= 1, net >>= 1;
		np = getnetbyaddr(net, AF_INET);
		if (np && np->n_net == net)
			cp = np->n_name;
		else {
			/*
			 * Look for subnets in hosts map.
			 * gethostbyaddr takes network order; above
			 * wanted host order.
			 */
			addr = htonl(addr);
			hp = gethostbyaddr((char *)&addr, sizeof (u_long),
				AF_INET);
			if (hp)
				cp = hp->h_name;
			addr = ntohl(addr);
		}
	}
	if (cp)
		(void) strcpy(line, cp);
	else {
		struct in_addr in;

		in.s_addr = addr;
		(void) strcpy(line, inet_ntoa(in));
	}
	return (line);
}

/*
 * Return the name of the network whose address is given.
 * The address is assumed to be a host address.
 */
static char *
netnamefromaddr(u_long addr, u_long mask)
{
	char		*cp = 0;
	static char	line[50];
	struct netent	*np = 0;
	struct hostent	*hp;
	u_long		net;
	u_long		netshifted;
	int		subnetshift;
	struct in_addr in;

	addr = ntohl(addr);
	mask = ntohl(mask);
	if (addr == INADDR_ANY)
		return ("default");

	/* Figure out network portion of address (with host portion = 0) */
	if (addr) {
		/* Try figuring out mask if unknown (all 0s). */
		if (mask == 0) {
			if (IN_CLASSA(addr)) {
				mask = (u_long)IN_CLASSA_NET;
				subnetshift = 8;
			} else if (IN_CLASSB(addr)) {
				mask = (u_long)IN_CLASSB_NET;
				subnetshift = 8;
			} else {
				mask = (u_long)IN_CLASSC_NET;
				subnetshift = 4;
			}
			/*
			 * If there are more bits than the standard mask
			 * would suggest, subnets must be in use. Guess at
			 * the subnet mask, assuming reasonable width subnet
			 * fields.
			 */
			while (addr & ~mask)
				/* compiler doesn't sign extend! */
				mask = (mask | ((long)mask >> subnetshift));
		}
		net = netshifted = addr & mask;
		while ((mask & 1) == 0)
			mask >>= 1, netshifted >>= 1;
	}
	else
		net = netshifted = 0;

	/* Try looking up name unless -n was specified. */
	if (!Nflag) {
		np = getnetbyaddr(netshifted, AF_INET);
		if (np && np->n_net == netshifted)
			cp = np->n_name;
		else {
			/*
			 * Look for subnets in hosts map.
			 * gethostbyaddr takes network order; above
			 * wanted host order.
			 */
			addr = htonl(net);
			hp = gethostbyaddr((char *)&addr, sizeof (u_long),
						AF_INET);
			if (hp)
				cp = hp->h_name;
		}

		if (cp) {
			(void) strcpy(line, cp);
			return (line);
		}
		/*
		 * No name found for net: fallthru and return in decimal
		 * dot notation.
		 */
	}

	in.s_addr = htonl(net);
	(void) strcpy(line, inet_ntoa(in));
	return (line);
}

/*
 * Pretty print a port number. If the Nflag was
 * specified, use numbers instead of names.
 */
static char *
portname(u_long port, char *proto)
{
	struct servent *sp = 0;
	static char	line[80];
	char		*cp;

	cp = line;
	if (!Nflag && port)
		sp = getservbyport(htons(port), proto);
	if (sp || port == 0)
		(void) sprintf(cp, "%.8s", sp ? sp->s_name : "*");
	else
		(void) sprintf(cp, "%d", port);
	return (line);
}

/* ARGSUSED */
void
readmem(long addr, int mode, int proc, char *buffer, u_int size, char *name)
{
	if (kvm_read(kd, addr, buffer, size) != size) {
		perror("netstat: kvm_read");
		(void) fprintf(stderr, "netstat: can't read '%s'\n", name);
		exit(1);
	}
}

static void
fail(int do_perror, char *message, ...)
{
	va_list args;

	va_start(args, message);
	(void) fprintf(stderr, "netstat: ");
	(void) fprintf(stderr, message, args);
	va_end(args);
	if (do_perror)
		(void) fprintf(stderr, ": %s", strerror(errno));
	(void) fprintf(stderr, "\n");
	exit(2);
}

/*
 * Return value of named statistic for given kstat_named kstat.
 * Return 0 if named statistic is not in list.
 */
static ulong_t
kstat_named_value(kstat_t *ksp, char *name)
{
	kstat_named_t *knp;

	if (ksp == NULL)
		return (0);

	knp = kstat_data_lookup(ksp, name);
	if (knp != NULL)
		return (knp->value.ul);
	else
		return (0);
}

kid_t
safe_kstat_read(kstat_ctl_t *kc, kstat_t *ksp, void *data)
{
	kid_t kstat_chain_id = kstat_read(kc, ksp, data);

	if (kstat_chain_id == -1)
		fail(1, "kstat_read(%x, '%s') failed", kc, ksp->ks_name);
	return (kstat_chain_id);
}
