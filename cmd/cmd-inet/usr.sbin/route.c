/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 * Copyright (c) 1992 Sun Microsystems Inc.
 */

#ident	"@(#)route.c	1.11	95/03/16 SMI"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/ioctl.h>
#ifndef SYSV
#include <sys/mbuf.h>
#endif SYSV
#include <signal.h>
#include <setjmp.h>

#ifdef SYSV
#include <sys/stream.h>
#include <sys/stropts.h>
#endif SYSV
#include <net/route.h>
#include <netinet/in.h>
#ifdef XNS
#include <netns/ns.h>
#endif XNS
#include <sys/tihdr.h>
#include <sys/tiuser.h>
#include <sys/timod.h>

#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <netdb.h>
#include <fcntl.h>

#include <inet/common.h>
#include <inet/mib2.h>
#include <inet/ip.h>
#include <netinet/igmp_var.h>
#include <netinet/ip_mroute.h>


#ifdef SYSV
#define	index	strchr
#define bzero(s, n)	memset((s), 0, (n))
#define	bcopy(f,t,l)	memcpy((t),(f),(l))
#define signal(s, f)	signal((s), (f))
#endif /* SYSV */

#ifndef MIN
#define	MIN(a,b)	(((a)<(b))?(a):(b))
#endif /* MIN */

struct	rtentry route;
int	s;
int	forcehost, forcenet, doflush, nflag;
struct	sockaddr_in sin = { AF_INET };
struct	in_addr inet_makeaddr();
char	*malloc();
char	*routename();
char	*netname();
extern	char *index();
extern	int getopt();

typedef struct mib_item_s {
	struct mib_item_s	* next_item;
	long			group;
	long			mib_id;
	long			length;
	char			* valp;
} mib_item_t;

static	mib_item_t	*mibget ();

/*
 * XXX 
 */
#ifndef T_CURRENT
#define T_CURRENT       MI_T_CURRENT
#endif

/* XXX - debugging */
int debug;
int nflag;
int fflag;
int verbose;


main(argc, argv)
	int argc;
	char *argv[];
{
	extern char *optarg;
	extern int optind;
	int c;

	while ((c = getopt(argc, argv, "dnfv")) != -1) {
		switch ((char) c) {
		case 'd':
			debug++;
			break;

		case 'n':
			nflag++;
			break;

		case 'f':
			fflag++;
			break;
			
		case 'v':
			verbose++;
			break;
			
		default:
			usage(argv[0]);
			exit(1);
		}
	}
		
#ifdef SYSV
	s = open ("/dev/ip", O_RDONLY);
	if (s == -1) {
		perror("route: open /dev/ip");
		exit(1);
	}
#else
	s = socket(AF_INET, SOCK_RAW, 0);
	if (s < 0) {
		perror("route: socket");
		exit(1);
	}
#endif SYSV

	if (fflag)
		flushroutes();

	if (optind < argc) {
		if (strcmp(argv[optind], "add") == 0)
			newroute(argc - optind, &argv[optind]);
		else if (strcmp(argv[optind], "delete") == 0)
			newroute(argc - optind, &argv[optind]);
		else if (strcmp(argv[optind], "change") == 0)
			changeroute(argc - optind -1, &argv[optind + 1]);
		else
			usage(argv[0]);
	}
	exit(0);
	/* NOTREACHED */
}

usage(s)
	char *s;
{
	fprintf(stderr,
		"usage: %s [-n] [-f] [ cmd [ net | host ] args ]\n",
		s);
}


/*
 * Purge all entries in the routing tables not
 * associated with network interfaces.
 */

flushroutes()
{
	int sd;	/* mib stream */
	mib_item_t	* item;
	mib2_ipRouteEntry_t * rp;
	struct rtentry rt;
	struct sockaddr_in *sin;

	sd = open("/dev/ip", O_RDWR);
	if (sd == -1) {
		perror("can't open mib stream");
		exit(1);
	}

	if ((item = mibget(sd)) == nilp(mib_item_t)) {
		fprintf(stderr, "mibget() failed\n");
		close(sd);
		exit(1);
	}
	
	printf("Flushing routing table:\n");

	for ( ;item ; item=item->next_item) {
		/* skip all the other trash that comes up the mib stream */
		if ((item->group != MIB2_IP) || (item->mib_id != MIB2_IP_21))
			continue;

		rp = (mib2_ipRouteEntry_t *)item->valp;
		
		while ((u_long) rp < (u_long) (item->valp + item->length)) {
			if ((rp->ipRouteInfo.re_ire_type == IRE_GATEWAY) ||
			    (rp->ipRouteInfo.re_ire_type == IRE_NET) ||
			    (rp->ipRouteInfo.re_ire_type == IRE_ROUTE_ASSOC) |
			    (rp->ipRouteInfo.re_ire_type == 
			     IRE_ROUTE_REDIRECT)) {
				/* got one - flush it */
				bzero ((char *) &rt, sizeof(rt));
				
				sin = (struct sockaddr_in *) &rt.rt_dst;
				sin->sin_family = AF_INET;
				bcopy ((char *) &rp->ipRouteDest,
				       (char *) &sin->sin_addr,
				       sizeof(struct sockaddr_in));
				if (rp->ipRouteMask == (IpAddress)-1)
					printf ("%-20.20s ",
						routename(&rt.rt_dst));
				else
					printf ("%-20.20s ",
						netname(&rt.rt_dst));

				sin = (struct sockaddr_in *) &rt.rt_gateway;
				sin->sin_family = AF_INET;
				bcopy ((char *) &rp->ipRouteNextHop,
				       (char *) &sin->sin_addr,
				       sizeof(struct sockaddr_in));
				printf("%-20.20s", routename(&rt.rt_gateway));
				fflush(stdout);

				rt.rt_flags |= RTF_GATEWAY;
				if (rp->ipRouteMask == (IpAddress)-1)
					rt.rt_flags |= RTF_HOST;

				if (ioctl(s, SIOCDELRT, &rt) == -1)
					error("ioctl SIOCDELRT");
				else
					printf("done\n");
			}
			rp++;
		}
	}
}

static jmp_buf NameTimeout;
int nametime = 20;		/* seconds to wait for name server */

nametimeout()  {longjmp(NameTimeout, 1);}

char domain[MAXHOSTNAMELEN + 1];
int firstdomain = 1;
char *
routename(sa)
	struct sockaddr *sa;
{
	register char *cp;
	static char line[50];
	struct hostent *hp = (struct hostent *) 0;
	char *ns_print();
	struct in_addr in;

	if (firstdomain) {
		firstdomain = 0;
		if (gethostname(domain, MAXHOSTNAMELEN) == 0 &&
		    (cp = index(domain, '.')))
			(void) strcpy(domain, cp + 1);
		else
			domain[0] = 0;
	}

	switch (sa->sa_family) {

	case AF_INET:
		in = ((struct sockaddr_in *)sa)->sin_addr;
		
		if (in.s_addr == 0) {
			strcpy (line, "default");
			return(line);
		}
		
		if (!nflag) {
			signal(SIGALRM, (void (*)())nametimeout);
			alarm(nametime);
			if (setjmp(NameTimeout) == 0) {
				hp = gethostbyaddr((char *)&in, 
						   sizeof (struct in_addr),
						   AF_INET);
				alarm(0);
			}
			if (hp) {
				if ((cp = index(hp->h_name, '.')) &&
				    !strcmp(cp + 1, domain))
					*cp = 0;
				strcpy(line, hp->h_name);
				return(line);
			}
		}

		sprintf(line, "%s", inet_ntoa(in));
		return (line);
		break;


#ifdef XNS
 	case AF_NS:
		return (ns_print((struct sockaddr_ns *)sa));
#endif XNS


 	default:
		{
			u_short *s = (u_short *)sa->sa_data;
			
			sprintf(line, "af %d: %x %x %x %x %x %x %x", sa->sa_family,
				s[0], s[1], s[2], s[3], s[4], s[5], s[6]);
			return (line);
			break;
		}
	}
}

/*
 * Return the name of the network whose address is given.
 * The address is assumed to be that of a net or subnet, not a host.
 */
char *
netname(sa)
	struct sockaddr *sa;
{
	char *cp = 0;
	static char line[50];
	struct netent *np = (struct netent *) 0;
	struct hostent *hp = (struct hostent *) hp;
	u_long net, mask;
	register i;
	int subnetshift;
	struct in_addr in;

	if (firstdomain) {
		firstdomain = 0;
		if (gethostname(domain, MAXHOSTNAMELEN) == 0 &&
		    (cp = index(domain, '.')))
			(void) strcpy(domain, cp + 1);
		else
			domain[0] = 0;
	}

	switch (sa->sa_family) {

	case AF_INET:
		in = ((struct sockaddr_in *)sa)->sin_addr;

		/* the all-zeros address is named "default" */
		in.s_addr = ntohl(in.s_addr);
		if (in.s_addr == 0) {
			strcpy (line, "default");
			return(line);
			
		}
		
		if (!nflag) {
			/* first lookup network in the hosts database */
			signal(SIGALRM, (void (*)())nametimeout);
			alarm(nametime);
			if (setjmp(NameTimeout) == 0) {
				hp = gethostbyaddr((char *)&in,
						   sizeof (struct in_addr),
						   AF_INET);
				alarm(0);
			}
			if (hp) {
				if ((cp = index(hp->h_name, '.')) &&
				    !strcmp(cp + 1, domain))
					*cp = 0;
				strcpy (line, hp->h_name);
				return(line);
			}
			
			/* next, look it up in the networks database */
			if (IN_CLASSA(i)) {
				mask = IN_CLASSA_NET;
				subnetshift = 8;
			} else if (IN_CLASSB(i)) {
				mask = IN_CLASSB_NET;
				subnetshift = 8;
			} else {
				mask = IN_CLASSC_NET;
				subnetshift = 4;
			}
			/*
			 * If there are more bits than the standard mask
			 * would suggest, subnets must be in use.
			 * Guess at the subnet mask, assuming reasonable
			 * width subnet fields.
			 */
			while (in.s_addr &~ mask)
#ifdef SYSV
				/* compiler doesn't sign-extend */
				mask = (mask | ((long) mask >> subnetshift));
#else
			mask = (long)mask >> subnetshift;
#endif /* SYSV */
			net = in.s_addr & mask;
			while ((mask & 1) == 0)
				mask >>= 1, net >>= 1;
			np = getnetbyaddr(net, AF_INET);
			if (np) {
				strcpy (line, np->n_name);
				return(line);
			}
		}

		/* do numeric */
#define C(x)	((x) & 0xff)
		if ((in.s_addr & 0xffffff) == 0)
			sprintf(line, "%u",
				C(in.s_addr >> 24));
		else if ((in.s_addr & 0xffff) == 0)
			sprintf(line, "%u.%u", 
				C(in.s_addr >> 24),
				C(in.s_addr >> 16));
		else if ((in.s_addr & 0xff) == 0)
			sprintf(line, "%u.%u.%u", 
				C(in.s_addr >> 24),
				C(in.s_addr >> 16), 
				C(in.s_addr >> 8));
		else
			sprintf(line, "%u.%u.%u.%u",
				C(in.s_addr >> 24),
				C(in.s_addr >> 16),
				C(in.s_addr >> 8),
				C(in.s_addr));
		return(line);
		break;


# ifdef XNS
	case AF_NS:
		return (ns_print((struct sockaddr_ns *)sa));
		break;
# endif XNS

	default:
	    {	u_short *s = (u_short *)sa->sa_data;

		sprintf(line, "af %d: %x %x %x %x %x %x %x", sa->sa_family,
			s[0], s[1], s[2], s[3], s[4], s[5], s[6]);
		break;
	    }
	}
	return (line);
}

newroute(argc, argv)
	int argc;
	char *argv[];
{
	struct sockaddr_in *sin;
	char *cmd, *dest, *gateway;
	int ishost, metric = 0, ret, attempts, oerrno;
	struct hostent *hp;
	extern int errno;

	cmd = argv[0];
	if (argc < 3) {
                printf("usage: %s destination gateway ...\n", cmd);
                return;
        }
	if ((strcmp(argv[1], "host")) == 0) {
		forcehost++;
		argc--, argv++;
	} else if ((strcmp(argv[1], "net")) == 0) {
		forcenet++;
		argc--, argv++;
	}
	if (*cmd == 'a') {
		if (argc != 4) {
			printf("usage: %s destination gateway metric\n", cmd);
			printf("(metric of 0 if gateway is this host)\n");
			return;
		}
		metric = atoi(argv[3]);
	} else {
		if (argc < 3) {
			printf("usage: %s destination gateway\n", cmd);
			return;
		}
	}
	sin = (struct sockaddr_in *)&route.rt_dst;
	ishost = getaddr(argv[1], &route.rt_dst, &hp, &dest, forcenet);
	if (forcehost)
		ishost = 1;
	if (forcenet)
		ishost = 0;
	sin = (struct sockaddr_in *)&route.rt_gateway;
	(void) getaddr(argv[2], &route.rt_gateway, &hp, &gateway, 0);
	route.rt_flags = RTF_UP;
	if (ishost)
		route.rt_flags |= RTF_HOST;
	if (metric > 0)
		route.rt_flags |= RTF_GATEWAY;
	for (attempts = 1; ; attempts++) {
		errno = 0;
		if ((ret = ioctl(s, *cmd == 'a' ? SIOCADDRT : SIOCDELRT,
		     (caddr_t)&route)) == 0)
			break;
		if (errno != ENETUNREACH && errno != ESRCH)
			break;
		if (hp && hp->h_addr_list[1]) {
			hp->h_addr_list++;
			bcopy(hp->h_addr_list[0], (caddr_t)&sin->sin_addr,
			    hp->h_length);
		} else
			break;
	}
	oerrno = errno;
	printf("%s %s %s: gateway %s", cmd, ishost? "host" : "net",
		dest, gateway);
	if (attempts > 1 && ret == 0)
	    printf(" (%s)",
		inet_ntoa(((struct sockaddr_in *)&route.rt_gateway)->sin_addr));
	if (ret == 0)
		printf("\n");
	else {
		printf(": ");
		fflush(stdout);
		errno = oerrno;
		error(0);
	}
}

changeroute(argc, argv)
	int argc;
	char *argv[];
{
	printf("not supported\n");
}

error(cmd)
	char *cmd;
{

	if (errno == ESRCH)
		fprintf(stderr, "not in table\n");
	else if (errno == EBUSY)
		fprintf(stderr, "entry in use\n");
	else if (errno == ENOBUFS)
		fprintf(stderr, "routing table overflow\n");
	else
		perror(cmd);
}

char *
savestr(s)
	char *s;
{
	char *sav;

	sav = malloc(strlen(s) + 1);
	if (sav == NULL) {
		fprintf(stderr, "route: out of memory\n");
		exit(1);
	}
	strcpy(sav, s);
	return (sav);
}

/*
 * Interpret an argument as a network address of some kind,
 * returning 1 if a host address, 0 if a network address.
 */
getaddr(s, sin, hpp, name, isnet)
	char *s;
	struct sockaddr_in *sin;
	struct hostent **hpp;
	char **name;
	int isnet;
{
	struct hostent *hp;
	struct netent *np;
	u_long val;

	*hpp = 0;
	if (strcmp(s, "default") == 0) {
		sin->sin_family = AF_INET;
		sin->sin_addr = inet_makeaddr(0, INADDR_ANY);
		*name = "default";
		return(0);
	}
	sin->sin_family = AF_INET;
	val = inet_addr(s);
	if (val != -1) {
		sin->sin_addr.s_addr = val;
		*name = s;
		return(inet_lnaof(sin->sin_addr) != INADDR_ANY);
	}
	if (isnet == 0) {
		val = inet_addr(s);
		if (val != -1) {
			sin->sin_addr.s_addr = val;
			*name = s;
			return(inet_lnaof(sin->sin_addr) != INADDR_ANY);
		}
	}
	val = inet_network(s);
	if (val != -1) {
		sin->sin_addr = inet_makeaddr(val, INADDR_ANY);
		*name = s;
		return(0);
	}
	np = getnetbyname(s);
	if (np) {
		sin->sin_family = np->n_addrtype;
		sin->sin_addr = inet_makeaddr(np->n_net, INADDR_ANY);
		*name = savestr(np->n_name);
		return(0);
	}
	hp = gethostbyname(s);
	if (hp) {
		*hpp = hp;
		sin->sin_family = hp->h_addrtype;
		bcopy(hp->h_addr, &sin->sin_addr, hp->h_length);
		*name = savestr(hp->h_name);
		return(1);
	}
	fprintf(stderr, "%s: bad value\n", s);
	exit(1);
}

short ns_nullh[] = {0,0,0};
short ns_bh[] = {-1,-1,-1};

# ifdef XNS
char *
ns_print(sns)
struct sockaddr_ns *sns;
{
	struct ns_addr work;
	union { union ns_net net_e; u_long long_e; } net;
	u_short port;
	static char mybuf[50], cport[10], chost[25];
	char *host = "";
	register char *p; register u_char *q; u_char *q_lim;

	work = sns->sns_addr;
	port = ntohs(work.x_port);
	work.x_port = 0;
	net.net_e  = work.x_net;
	if (ns_nullhost(work) && net.long_e == 0) {
		if (port ) {
			sprintf(mybuf, "*.%xH", port);
			upHex(mybuf);
		} else
			sprintf(mybuf, "*.*");
		return (mybuf);
	}

	if (bcmp(ns_bh, work.x_host.c_host, 6) == 0) { 
		host = "any";
	} else if (bcmp(ns_nullh, work.x_host.c_host, 6) == 0) {
		host = "*";
	} else {
		q = work.x_host.c_host;
		sprintf(chost, "%02x%02x%02x%02x%02x%02xH",
			q[0], q[1], q[2], q[3], q[4], q[5]);
		for (p = chost; *p == '0' && p < chost + 12; p++);
		host = p;
	}
	if (port)
		sprintf(cport, ".%xH", htons(port));
	else
		*cport = 0;

	sprintf(mybuf,"%xH.%s%s", ntohl(net.long_e), host, cport);
	upHex(mybuf);
	return(mybuf);
}
# endif XNS

upHex(p0)
char *p0;
{
	register char *p = p0;
	for (; *p; p++) switch (*p) {

	case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
		*p += ('A' - 'a');
	}
}

staticf mib_item_t * 
mibget (sd)
	int		sd;
{
	char			buf[512];
	int			flags;
	int			i, j, getcode;
	struct strbuf		ctlbuf, databuf;
	struct T_optmgmt_req	* tor = (struct T_optmgmt_req *)buf;
	struct T_optmgmt_ack	* toa = (struct T_optmgmt_ack *)buf;
	struct T_error_ack	* tea = (struct T_error_ack *)buf;
	struct opthdr		* req;
	mib_item_t		* first_item = nilp(mib_item_t);
	mib_item_t		* last_item  = nilp(mib_item_t);
	mib_item_t		* temp;
	
	tor->PRIM_type = T_OPTMGMT_REQ;
	tor->OPT_offset = sizeof(struct T_optmgmt_req);
	tor->OPT_length = sizeof(struct opthdr);
	tor->MGMT_flags = T_CURRENT;
	req = (struct opthdr *)&tor[1];
	req->level = MIB2_IP;		/* any MIB2_xxx value ok here */
	req->name  = 0;
	req->len   = 0;

	ctlbuf.buf = buf;
	ctlbuf.len = tor->OPT_length + tor->OPT_offset;
	flags = 0;
	if (putmsg(sd, &ctlbuf, nilp(struct strbuf), flags) == -1) {
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
	ctlbuf.maxlen = sizeof(buf);
	for (j=1; ; j++) {
		flags = 0;
		getcode = getmsg(sd, &ctlbuf, nilp(struct strbuf), &flags);
		if (getcode == -1) {
			perror("mibget getmsg(ctl) failed");
			if (debug) {
				fprintf(stderr, "#   level   name    len\n");
				i = 0;
				for (last_item = first_item; last_item; 
					last_item = last_item->next_item)
					printf("%d  %4d   %5d   %d\n", ++i,
						last_item->group, 
						last_item->mib_id, 
						last_item->length);
			}
			goto error_exit;
		}
		if (getcode == 0
		&& ctlbuf.len >= sizeof(struct T_optmgmt_ack)
		&& toa->PRIM_type == T_OPTMGMT_ACK
		&& toa->MGMT_flags == T_SUCCESS
		&& req->len == 0) {
			if (debug)
				printf("mibget getmsg() %d returned EOD (level %d, name %d)\n", 
				       j, req->level, req->name);
			return first_item;		/* this is EOD msg */
		}

		if (ctlbuf.len >= sizeof(struct T_error_ack)
		&& tea->PRIM_type == T_ERROR_ACK) {
			fprintf(stderr, 
			"mibget %d gives T_ERROR_ACK: TLI_error = 0x%x, UNIX_error = 0x%x\n",
				j, getcode, tea->TLI_error, tea->UNIX_error);
			errno = (tea->TLI_error == TSYSERR)
				? tea->UNIX_error : EPROTO;
			goto error_exit;
		}
			
		if (getcode != MOREDATA
		|| ctlbuf.len < sizeof(struct T_optmgmt_ack)
		|| toa->PRIM_type != T_OPTMGMT_ACK
		|| toa->MGMT_flags != T_SUCCESS) {
			printf(
			"mibget getmsg(ctl) %d returned %d, ctlbuf.len = %d, PRIM_type = %d\n",
				 j, getcode, ctlbuf.len, toa->PRIM_type);
			if (toa->PRIM_type == T_OPTMGMT_ACK)
				printf(
				"T_OPTMGMT_ACK: MGMT_flags = 0x%x, req->len = %d\n", 
					toa->MGMT_flags, req->len);
			errno = ENOMSG;
			goto error_exit;
		}

		temp = (mib_item_t *)malloc(sizeof(mib_item_t));
		if (!temp) {
			perror("mibget malloc failed");
			goto error_exit;
		}
		if (last_item)
			last_item->next_item = temp;
		else
			first_item = temp;
		last_item = temp;
		last_item->next_item = nilp(mib_item_t);
		last_item->group = req->level;
		last_item->mib_id = req->name;
		last_item->length = req->len;
		last_item->valp = (char *)malloc(req->len);
		if (debug)
			printf(
			"msg %d:  group = %4d   mib_id = %5d   length = %d\n", 
				j, last_item->group, last_item->mib_id, 
				last_item->length);

		databuf.maxlen = last_item->length;
		databuf.buf    = last_item->valp;
		databuf.len    = 0;
		flags = 0;
		getcode = getmsg(sd, nilp(struct strbuf), &databuf, &flags);
		if (getcode == -1) {
			perror("mibget getmsg(data) failed");
			goto error_exit;
		} else if (getcode != 0) {
			printf(
			"mibget getmsg(data) returned %d, databuf.maxlen = %d, databuf.len = %d\n",
				 getcode, databuf.maxlen, databuf.len);
			goto error_exit;
		}
	}

error_exit:;
	while (first_item) {
		last_item = first_item;
		first_item = first_item->next_item;
		free(last_item);
	}
	return first_item;
}
