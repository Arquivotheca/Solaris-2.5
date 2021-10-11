/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)ping.c	1.11	94/11/08 SMI"	/* SVr4.0 1.4	*/

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

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/file.h>
#include <sys/signal.h>

#include <net/if.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netdb.h>

#define	MAXWAIT		10	/* max time to wait for response, sec. */
#define	MAXPACKET	65535	/* max packet size */
#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN	64
#endif

#define MULTICAST_NOLOOP	1	/* multicast options */
#define MULTICAST_TTL		2
#define MULTICAST_IF		4


#ifdef SYSV
#define	bzero(s,n)	memset((s), 0, (n))
#define	bcopy(f,t,l)	memcpy((t),(f),(l))
#define signal(s,f)	sigset((s), (f))
#endif /* SYSV */

extern char 	*malloc();
extern char 	*inet_ntoa();

int	verbose;
int	stats;
u_char	packet[MAXPACKET];
int	options, moptions;
extern	int errno;

int s;			/* Socket file descriptor */
int recvs;		/* Recv file descriptor */
struct hostent *hp;	/* Pointer to host info */

struct sockaddr whereto;/* Who to ping */
int datalen = 64 - ICMP_MINLEN;		/* How much data */


char hnamebuf[MAXHOSTNAMELEN];
char *hostname = hnamebuf;

int npackets = 0;
int ntransmitted = 0;		/* sequence # for outbound packets = #sent */
int ident;
# define MAX_ROUTES 9		/* maximum number of source route space */
# define ROUTE_SIZE (IPOPT_OLEN + IPOPT_OFFSET + \
	   		MAX_ROUTES * sizeof(struct in_addr))

int nreceived = 0;		/* # of packets we got back */
int timing = 0;
int tmin = 999999999;
int tmax = 0;
int tsum = 0;			/* sum of all times, for doing average */
int record = 0;			/* true if using record route */
int src_route_echo_reply = 0;	/* true if using echo reply w/ source route */
int strict = 0;			/* true if using strict source route */
int timestamp = 0;		/* true if using timestamp option */
int timestamp_flag = 0;		/* timestamp flag value */
int num_src_route = 0;		/* Number of -x or -X options */
u_long src_routes[MAX_ROUTES];
int use_timestamp = 0;		/* Use timestamp request instead of echo */
int use_udp = 0;		/* Use UDP instead of ICMP */

int interval = 1;		/* interval in seconds between transmissions */
int nflag = 0;			/* true if reverse translation of addresses */

void finish(), catcher();


#ifdef BSD
#define	setbuf(s, b)	setlinebuf((s))
#endif /* BSD */

usage(cmdname)
{
	fprintf(stderr, "usage: %s host [timeout]\n", cmdname);
	fprintf(stderr, 
	"usage: %s -s[drvRlLn] [-I interval] [-t ttl] [-i interface] host [data size] [npackets]\n",
		cmdname);
}

char *progname;

u_long
str2ipaddr(str)
	char *str;
{
	u_long addr;

	addr = inet_addr(str);
	if (addr == -1) {
		hp = gethostbyname(str);
		if (hp && hp->h_length == 4) {
			bcopy(hp->h_addr, (caddr_t)&addr, hp->h_length);
		} else {
			fprintf(stderr, "%s: unknown address %s\n", 
				progname, str);
			exit(1);
		}
	}
	return (addr);
}

/*
 * 			M A I N
 */
main(argc, argv)
char *argv[];
{
	struct sockaddr_in from;
	char **av = argv;
	struct sockaddr_in *to = (struct sockaddr_in *) &whereto;
	int on = 1;
	int timeout = 20;
	struct protoent *proto;
	extern char *optarg;
	extern int optind;
	char *cmdname = argv[0];
	char *targethost;
	int c;
	int i;
	unsigned char ttl, loop;
	struct in_addr ifaddr;

	progname = argv[0];

	while ((c = getopt(argc, argv, "drRlLvsnI:ST0123t:i:x:X:YU")) != -1) {
		switch ((char) c) {
		case 'd':
			options |= SO_DEBUG;
			break;

		case 'r':
			options |= SO_DONTROUTE;
			break;

		case 'R':
			record = 1;
			break;

		case 'l':
			src_route_echo_reply = 1;
			strict = 0;
			break;

		case 'S':
			src_route_echo_reply = 1;
			strict = 1;
			break;

		case 'T':
			timestamp = 1;
			break;

		case '0':
		case '1':
		case '2':
		case '3':
			timestamp_flag = (char)c - '0';
			break;

		case 'x':
			strict = 0;
			if (num_src_route >= MAX_ROUTES) {
				fprintf(stderr, "%s: too many source routes\n",
				       progname);
				exit(1);
			}
			src_routes[num_src_route++] = str2ipaddr(optarg);
			break;

		case 'X':
			strict = 1;
			if (num_src_route >= MAX_ROUTES) {
				fprintf(stderr, "%s: too many source routes\n",
				       progname);
				exit(1);
			}
			src_routes[num_src_route++] = str2ipaddr(optarg);
			break;

		case 'v':
			verbose++;
			break;

		case 's':
			stats++;
			break;

		case 'I':
			stats++;
			if (atoi(optarg) == 0) {
				fprintf(stderr, "%s: bad interval: %s\n", 
				       argv[0], optarg);
				exit (1);
			}
			interval = atoi(optarg);
			break;
			
		case 'L':
			moptions |= MULTICAST_NOLOOP;
			loop = 0;
			break;

		case 'n':
			nflag++;
			break;
		case 't':
			moptions |= MULTICAST_TTL;
			i = atoi(optarg);
			if (i > 255) {
				fprintf(stderr, "ttl %u out of range\n", i);
				exit(1);
			}
			ttl = i;
			break;

		case 'i':
			moptions |= MULTICAST_IF;
			ifaddr.s_addr = str2ipaddr(optarg);
			break;

		case 'Y':
			use_timestamp++;
			break;

		case 'U':
			use_udp++;
			break;

		case '?':
			usage(cmdname);
			exit(1);
			break;
		}
	}

	if (optind >= argc) {
		usage(cmdname);
		exit(1);
	}
	targethost = argv[optind];
	optind++;
	if (optind < argc) {
		if (stats) {
			datalen = atoi(argv[optind]);
			optind++;
			if (optind < argc)
				npackets = atoi(argv[optind]);
		} else 
			timeout = atoi(argv[optind]);
	}

	bzero( (char *)&whereto, sizeof(struct sockaddr) );
	to->sin_family = AF_INET;
	to->sin_addr.s_addr = inet_addr(targethost);
	if (to->sin_addr.s_addr != -1 || 
	    !strcmp(targethost,"255.255.255.255")) {
		strcpy(hnamebuf, targethost);
	} else {
		hp = gethostbyname(targethost);
		if (hp && hp->h_length == 4) {
			to->sin_family = hp->h_addrtype;
			bcopy(hp->h_addr, (caddr_t)&to->sin_addr, hp->h_length);
			strcpy(hnamebuf, hp->h_name);
		} else {
			fprintf(stderr, "%s: unknown host %s\n", 
				cmdname, targethost);
			exit(1);
		}
	}

	if (datalen > MAXPACKET - ICMP_MINLEN) {
		fprintf(stderr, "ping: packet size too large\n");
		exit(1);
	}
	if (datalen >= sizeof(struct timeval))
		timing = 1;

	ident = (int)getpid() & 0xFFFF;


	recvs = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (recvs < 0) {
		perror("ping: socket");
		exit(5);
	}
	if (use_udp) {
		s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		to->sin_port = 5678;
	}
	else
		s = recvs;
	if (s < 0) {
		perror("ping: socket");
		exit(5);
	}
	i = 48 * 1024;
	if (i < datalen)
		i = datalen;
	if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, (char *)&i, sizeof(i)) < 0) {
		perror("ping: setsockopt SO_RCVBUF");
	}
	if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, (char *)&i, sizeof(i)) < 0) {
		perror("ping: setsockopt SO_SNDBUF");
	}
	if (options & SO_DEBUG)
		setsockopt(s, SOL_SOCKET, SO_DEBUG, (char *)&on, sizeof(on));
	if (options & SO_DONTROUTE)
		setsockopt(s, SOL_SOCKET, SO_DONTROUTE, (char *)&on, 
			   sizeof(on));
	if (record || src_route_echo_reply || timestamp || num_src_route) {
	    char srr[ROUTE_SIZE + 1];
	    char *srp;
	    int optsize = ROUTE_SIZE;
	    
	    bzero(srr, sizeof(srr));
	    srp = srr;

	    if (src_route_echo_reply && num_src_route == 0) {
	        struct sockaddr_in mine; 

		srp[0] = strict ? IPOPT_SSRR : IPOPT_LSRR;
		srp[1] = 11;
		srp[2] = IPOPT_MINOFF;
		get_myaddress( &mine );
	    	bcopy( (char *)&to->sin_addr, srp+3, sizeof(struct in_addr));
	    	bcopy( (char *)&mine.sin_addr, srp+7, sizeof(struct in_addr));
		optsize -= srp[1];
		srp += srp[1];
	    }
	    if (num_src_route) {
		int i;

		if (src_route_echo_reply) {
			/* Add source route entries for the return path */
			struct sockaddr_in mine; 

			if ((num_src_route + 1)* 2 >= MAX_ROUTES) {
				fprintf(stderr, "%s: too many source routes\n",
				       progname);
				exit(1);
			}
			/* 
			 * Insert the the final destination in the middle 
			 * followed by the return route and finally our
			 * address.
			 */
			src_routes[num_src_route] = to->sin_addr.s_addr;
			for (i = num_src_route + 1; i < 2*num_src_route+1; i++)
				src_routes[i] = src_routes[2*num_src_route-i];
			num_src_route = 2*num_src_route + 1;
			get_myaddress( &mine );
			src_routes[num_src_route++] = mine.sin_addr.s_addr;
		} else {
			/* Insert the the final destination */
			src_routes[num_src_route++] = to->sin_addr.s_addr;
		}
		if (3 + 4 * num_src_route > optsize) {
			fprintf(stderr, "%s: too many source routes\n", argv[0]);
			exit (1);
		}

		srp[0] = strict ? IPOPT_SSRR : IPOPT_LSRR;
		srp[1] = 3 + 4 * num_src_route;
		srp[2] = IPOPT_MINOFF;
		for (i = 0; i < num_src_route; i++) {
			bcopy((char *)&src_routes[i], &srp[3+4*i], 
			      sizeof(u_long));
		}
		optsize -= srp[1];
		srp += srp[1];
	    }
	    if (timestamp) {
		if (optsize < IPOPT_MINOFF) {
			fprintf(stderr, "%s: no room for timestamp option\n", 
				argv[0]);
			exit (1);
		}
	    	srp[0] = IPOPT_TS;
		srp[1] = optsize;
		srp[2] = IPOPT_MINOFF + 1;
		srp[3] = timestamp_flag;
		if ((timestamp_flag & 0x0f) > 1) {
		    struct sockaddr_in mine; 

		    /* Note: BSD/4.X is broken in their check so we have to
		     * bump up this number by at least one.
		     */
		    srp[1] = 12 + 1;
		    get_myaddress( &mine );
		    bcopy( (char *)&to->sin_addr, srp+4, 
			  sizeof(struct in_addr));
		    srp[1] = 20 + 1;
		    bcopy( (char *)&mine.sin_addr, srp+12, 
			  sizeof(struct in_addr));
	        }
		optsize -= srp[1];
		srp += srp[1];
	    }
	    if (record) {
		if (optsize < IPOPT_MINOFF) {
			fprintf(stderr, "%s: no room for record route option\n",
			       argv[0]);
			exit (1);
		}
	    	srp[0] = IPOPT_RR;
		srp[1] = optsize;
		srp[2] = IPOPT_MINOFF;
		optsize -= srp[1];
		srp += srp[1];
	    }
	    optsize = srp - srr;
	    /* Round up to 4 byte boundary */
	    if (optsize & 0x3)
		    optsize = (optsize & ~0x3) + 4;
	    if (setsockopt(s, IPPROTO_IP, IP_OPTIONS, srr, optsize) < 0)
		perror("IP Options");
	}

	if (moptions & MULTICAST_NOLOOP) {
		if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP,
			       (char *)&loop, 1) == -1) {
			perror ("can't disable multicast loopback");
			exit(92);
		}
	}
	if (moptions & MULTICAST_TTL) {
		if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL,
			       (char *)&ttl, 1) == -1) {
			perror ("can't set multicast time-to-live");
			exit(93);
		}
		i = ttl;
		if (setsockopt(s, IPPROTO_IP, IP_TTL,
			       (char *)&i, sizeof(i)) == -1) {
			perror ("can't set time-to-live");
			exit(93);
		}
	}
	if (moptions & MULTICAST_IF) {
		if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF,
			       (char *)&ifaddr, sizeof(ifaddr)) == -1) {
			perror ("can't set multicast source interface");
			exit(94);
		}
	}

	if (stats) {
		if (nflag)
			printf("PING %s (%s): %d data bytes\n", 
			       hostname, inet_ntoa(&to->sin_addr), datalen );
		else
			printf("PING %s: %d data bytes\n", hostname, datalen );
	}

	setbuf(stdout, (char *)0);

	if (stats)
		signal( SIGINT, finish );
	signal(SIGALRM, catcher);

	catcher();	/* start things going */

	for (;;) {
		int len = sizeof (packet);
		int fromlen = sizeof (from);
		int cc;

		if (!stats && ntransmitted > timeout) {
			printf("no answer from %s\n", hostname);
			exit(1);
		}
		if ( (cc=recvfrom(recvs, (char *)packet, len, 0, 
				  (struct sockaddr *)&from, &fromlen)) < 0) {
			if( errno == EINTR )
				continue;
			perror("ping: recvfrom");
			continue;
		}
		pr_pack( packet, cc, &from );
		if (npackets && nreceived >= npackets)
			finish();
	}
	/*NOTREACHED*/
}

/*
 * 			C A T C H E R
 * 
 * This routine causes another PING to be transmitted, and then
 * schedules another SIGALRM for <interval> second from now.
 * 
 * Bug -
 * 	Our sense of time will slowly skew (ie, packets will not be launched
 * 	exactly at 1-second intervals).  This does not affect the quality
 *	of the delay and loss statistics.
 */
void
catcher()
{
	int waittime;

	pinger();
	if (npackets == 0 || ntransmitted < npackets) {
		alarm(interval);
	} else {
		if (nreceived) {
			waittime = 2 * tmax / 1000;
			if (waittime == 0)
				waittime = 1;
		} else
			waittime = MAXWAIT;
		signal(SIGALRM, finish);
		alarm(waittime);
	}
}

/*
 * 			P I N G E R
 * 
 * Compose and transmit an ICMP ECHO REQUEST packet.  The IP packet
 * will be added on by the kernel.  The ID field is our UNIX process ID,
 * and the sequence number is an ascending integer.  The first 8 bytes
 * of the data portion are used to hold a UNIX "timeval" struct in VAX
 * byte-order, to compute the round-trip time.
 */
pinger()
{
	static u_char outpack[MAXPACKET];
	register struct icmp *icp = (struct icmp *) outpack;
	int i, cc, start;
	register struct timeval *tp = (struct timeval *) &outpack[ICMP_MINLEN];
	register u_char *datap = &outpack[ICMP_MINLEN+sizeof(struct timeval)];

	if (use_timestamp) 
		icp->icmp_type = src_route_echo_reply ? ICMP_TSTAMPREPLY : 
			ICMP_TSTAMP;
	else
		icp->icmp_type = src_route_echo_reply ? ICMP_ECHOREPLY : 
			ICMP_ECHO;
	icp->icmp_code = 0;
	icp->icmp_cksum = 0;
	icp->icmp_seq = ntransmitted++;
	icp->icmp_id = ident;		/* ID */

	cc = datalen+ICMP_MINLEN;		/* skips ICMP portion */
	start = 8;				/* skip 8 for time */

	if (timing || use_timestamp)
		gettimeofday( tp, (struct timezone *) NULL );
	if (use_timestamp) {
		start = 12;
		/* Number of seconds since midnight */
		icp->icmp_otime = (tp->tv_sec % (24*60*60)) * 1000 + 
			tp->tv_usec / 1000;
	}
	for( i=start; i<datalen; i++)	
		*datap++ = i;

	/* Compute ICMP checksum here */
	icp->icmp_cksum = in_cksum( icp, cc );

	/* cc = sendto(s, msg, len, flags, to, tolen) */
	i = sendto( s, (char *)outpack, cc, 0, &whereto, 
		   sizeof(struct sockaddr) );

	if( i < 0 || i != cc )  {
		if( i<0 ) {
		    perror("sendto");
		    if (!stats)
			exit(1);
		}
		printf("ping: wrote %s %d chars, ret=%d\n",
			hostname, cc, i );
		fflush(stdout);
	}
}

/*
 * 			P R _ T Y P E
 *
 * Convert an ICMP "type" field to a printable string.
 */
char *
pr_type( t )
register int t;
{
	static char *ttab[] = {
		"Echo Reply",
		"ICMP 1",
		"ICMP 2",
		"Dest Unreachable",
		"Source Quench",
		"Redirect",
		"ICMP 6",
		"ICMP 7",
		"Echo",
		"ICMP 9",
		"ICMP 10",
		"Time Exceeded",
		"Parameter Problem",
		"Timestamp",
		"Timestamp Reply",
		"Info Request",
		"Info Reply",
		"Netmask Request",
		"Netmask Reply"
	};

	if( t < 0 || t > 16 )
		return("OUT-OF-RANGE");

	return(ttab[t]);
}



/*
 *			P R _ N A M E
 *
 * Return a string name for the given IP address.
 */
char *pr_name(addr)
    struct in_addr addr;
{
	char *inet_ntoa();
	struct hostent *phe;
	static struct in_addr prev_addr;
	static char buf[256];

	if (addr.s_addr != prev_addr.s_addr) {
		if (nflag || (phe = gethostbyaddr((char *)&addr.s_addr, 
						  4, AF_INET)) == NULL)
			(void) sprintf(buf, "%s", inet_ntoa(addr));
		else
			(void) sprintf(buf, "%s (%s)", phe->h_name, 
				       inet_ntoa(addr));
		prev_addr = addr;
	}
	return(buf);
}


/*
 * Print the IP protocol
 */
char *
pr_protocol(p)
{
    static char buf[20];

    switch (p) {
    	case IPPROTO_ICMP:	return("icmp");
    	case IPPROTO_TCP:	return("tcp");
    	case IPPROTO_UDP:	return("udp");
    }
    sprintf(buf,"prot %d",p);
    return(buf);
}


/*
 *			P R _ P A C K
 *
 * Print out the packet, if it came from us.  This logic is necessary
 * because ALL readers of the ICMP socket get a copy of ALL ICMP packets
 * which arrive ('tis only fair).  This permits multiple copies of this
 * program to be run without having intermingled output (or statistics!).
 */
pr_pack( buf, cc, from )
char *buf;
int cc;
struct sockaddr_in *from;
{
	struct ip *ip;
	register struct icmp *icp;
	register long *lp = (long *) packet;
	register int i;
	struct timeval tv;
	struct timeval *tp;
	int hlen, triptime;
	struct sockaddr_in *to = (struct sockaddr_in *) &whereto;
	long w;
	static char *unreach[] = {
	    "Net Unreachable",
	    "Host Unreachable",
	    "Protocol Unreachable",
	    "Port Unreachable",
	    "Fragmentation needed and DF set",
	    "Source Route Failed"
	};
	static char *redirect[] = {
	    "Net",
	    "Host",
	    "TOS Net",
	    "TOS Host"
	};
	static char *timexceed[] = {
	    "Time exceeded in transit",
	    "Time exceeded during reassembly",
        };
	gettimeofday( &tv, (struct timezone *) NULL );

	ip = (struct ip *) buf;
	hlen = ip->ip_hl << 2;
	if (cc < hlen + ICMP_MINLEN) {
		if (verbose)
			printf("packet too short (%d bytes) from %s\n", cc,
				pr_name(from->sin_addr));
		return;
	}
	cc -= hlen;
	icp = (struct icmp *)(buf + hlen);
	if (ip->ip_p == 0) {
		/*
		 * Assume that we are running on a pre-4.3BSD system
		 * such as SunOS before 4.0
		 */
		 icp = (struct icmp *)buf;
	}
	switch (icp->icmp_type) {
	  case ICMP_UNREACH:
	    ip = &icp->icmp_ip;
	    if (ip->ip_dst.s_addr == to->sin_addr.s_addr || verbose) {
		printf("ICMP %s from gateway %s\n", 
			unreach[icp->icmp_code], pr_name(from->sin_addr));
		printf(" for %s from %s", pr_protocol(ip->ip_p), 
					  pr_name(ip->ip_src) );
		printf(" to %s", pr_name(ip->ip_dst) );
		if (ip->ip_p == IPPROTO_TCP || ip->ip_p == IPPROTO_UDP)
		    printf(" port %d\n", 
		    	ntohs(((u_short *)ip)[sizeof(struct ip)/2 + 1]) );
		else
		    printf("\n");
	    }
	    break;

	  case ICMP_REDIRECT:
	    ip = &icp->icmp_ip;
	    if (ip->ip_dst.s_addr == to->sin_addr.s_addr || verbose) {
		printf("ICMP %s redirect from gateway %s\n", 
			redirect[icp->icmp_code], pr_name(from->sin_addr));
		printf(" to %s", pr_name(icp->icmp_gwaddr) );
		printf(" for %s\n", pr_name(ip->ip_dst) );
	    }
	    break;

	  case ICMP_ECHOREPLY:
	    if ( icp->icmp_id != ident )
		return;			/* 'Twas not our ECHO */

	    if (!stats) {
		printf("%s is alive\n", hostname);
		exit(0);
	    }
	    tp = (struct timeval *)&icp->icmp_data[0];
	    printf("%d bytes from %s: ", cc, pr_name(from->sin_addr));
	    printf("icmp_seq=%d. ", icp->icmp_seq );
	    if (timing) {
		tvsub( &tv, tp );
		triptime = tv.tv_sec*1000+(tv.tv_usec/1000);
		printf("time=%d. ms\n", triptime );
		tsum += triptime;
		if ( triptime < tmin )
			tmin = triptime;
		if ( triptime > tmax )
			tmax = triptime;
	    } else
		putchar('\n');
	    nreceived++;
	    break;

    case ICMP_SOURCEQUENCH:
    case ICMP_PARAMPROB:
	    ip = &icp->icmp_ip;
	    if (ip->ip_dst.s_addr == to->sin_addr.s_addr || verbose) {
		printf("ICMP %s from %s\n", 
			pr_type(icp->icmp_type), pr_name(from->sin_addr));
		if (icp->icmp_type == ICMP_PARAMPROB) 
			printf(" in byte %d (value 0x%x)", icp->icmp_pptr,
			       *((char *)ip + icp->icmp_pptr) );
		printf(" for %s from %s", pr_protocol(ip->ip_p), 
					  pr_name(ip->ip_src) );
		printf(" to %s", pr_name(ip->ip_dst) );
		if (ip->ip_p == IPPROTO_TCP || ip->ip_p == IPPROTO_UDP)
		    printf(" port %d\n", 
		    	ntohs(((u_short *)ip)[sizeof(struct ip)/2 + 1]) );
		else
		    printf("\n");
	    }
	    break;
	    
    case ICMP_TIMXCEED:
	    ip = &icp->icmp_ip;
	    if (ip->ip_dst.s_addr == to->sin_addr.s_addr || verbose) {
		printf("ICMP %s from %s\n", 
			timexceed[icp->icmp_code], pr_name(from->sin_addr));
		printf(" for %s from %s", pr_protocol(ip->ip_p), 
					  pr_name(ip->ip_src) );
		printf(" to %s", pr_name(ip->ip_dst) );
		if (ip->ip_p == IPPROTO_TCP || ip->ip_p == IPPROTO_UDP)
		    printf(" port %d\n", 
		    	ntohs(((u_short *)ip)[sizeof(struct ip)/2 + 1]) );
		else
		    printf("\n");
	    }
	    break;
	    
    case ICMP_TSTAMPREPLY:
	    if ( icp->icmp_id != ident )
		return;			/* 'Twas not our ECHO */

	    printf("%d bytes from %s: ", cc, pr_name(from->sin_addr));
	    printf("icmp_seq=%d, ", icp->icmp_seq );
	    printf("%d, %d, %d\n", icp->icmp_otime, icp->icmp_rtime, 
		   icp->icmp_ttime);
	    break;
	    

    case 9:
    case 10:
	    /* Router discovery messages */
	    return;

    case ICMP_ECHO:
    case ICMP_TSTAMP:
    case ICMP_IREQ:
    case ICMP_MASKREQ:
	    /* These were never passed out from the SunOS 4.X kernel. */
	    return;

	  default:
	    if (verbose) {
		printf("%d bytes from %s:\n", cc,
			pr_name(from->sin_addr));
		printf("icmp_type=%d (%s) ",
				icp->icmp_type, pr_type(icp->icmp_type) );
		printf("icmp_code=%d\n", icp->icmp_code );
		for( i=0; i<12; i++)
		    printf("x%2.2x: x%8.8x\n", i*sizeof(long), *lp++ );
	    }
	    break;
	}
	buf += sizeof(struct ip);
	hlen -= sizeof(struct ip);
	if (verbose && hlen > 0)
		pr_options(buf, hlen);
}

/*
 *		P R _ O P T I O N S
 * Print out the ip options.
 */
pr_options(opt, optlength)
	unsigned char *opt;
	int optlength;
{
    int curlength;

    printf("  IP options: ");
    while (optlength > 0) {
       curlength = opt[1];
       switch (*opt) {
         case IPOPT_EOL:
	    optlength = 0;
	    break;
	 
	 case IPOPT_NOP:
	    opt++;
	    optlength--;
	    continue;
	 
	 case IPOPT_RR:
	    printf(" <record route> ");
	    ip_rrprint(opt, curlength, 1);
	    break;
	 
	 case IPOPT_TS:
	    printf(" <time stamp> ");
	    ip_tsprint(opt, curlength);
	    break;
	 
	 case IPOPT_SECURITY:
	    printf(" <security>");
	    break;
	 
	 case IPOPT_LSRR:
	    printf(" <loose source route> ");
	    ip_rrprint(opt, curlength, 0);
	    break;
	 
	 case IPOPT_SATID:
	    printf(" <stream id>");
	    break;
	 
	 case IPOPT_SSRR:
	    printf(" <strict source route> ");
	    ip_rrprint(opt, curlength, 0);
	    break;
	 
	 default:
	    printf(" <option %d, len %d>", *opt, curlength);
	    break;
       }
       /*
        * Following most options comes a length field
	*/
	opt += curlength;
	optlength -= curlength;
    }
    printf("\n");
  }


/*
 * Print out a recorded route option.
 */
ip_rrprint(opt, length, rrflag)
    unsigned char *opt;
    int length;
    int rrflag;		/* Supress Current and onwards */
{
    int pointer;
    struct in_addr addr;

    opt += IPOPT_OFFSET;
    length -= IPOPT_OFFSET;

    pointer = *opt++;
    pointer -= IPOPT_MINOFF;
    length--;
    while (length > 0) {
	if (pointer == 0 && rrflag) {
	    printf(" (End of record)");
	    break;
	}
	bcopy((char *)opt, (char *)&addr, sizeof(addr));
	printf( "%s", pr_name(addr) );
	if (pointer == 0)
	    printf( "(Current)");
	opt += sizeof(addr);
	length -= sizeof(addr);
	pointer -= sizeof(addr);
	if (length >0)
	    printf( ", ");
    }
}


/*
 * Print out a timestamp option.
 */
ip_tsprint(opt, length)
    unsigned char *opt;
    int length;
{
    int pointer;
    struct in_addr addr;
    u_long time;
    int address_present;
    int rrflag;		/* End at current entry? */

    switch (opt[3] & 0x0f) {
    case 0:
	    address_present = 0;
	    rrflag = 1;
	    break;
    case 1:
	    address_present = 1;
	    rrflag = 1;
	    break;
    case 2:
    case 3:
	    address_present = 1;
	    rrflag = 0;
	    break;
    default:
	    printf("(Bad flag value: 0x%x)", opt[3] & 0x0f);
	    return;
    }
    if (opt[3] & 0xf0) 
	    printf("(Overflow: %d) ", opt[3] >> 4);

    opt += IPOPT_OFFSET;
    length -= IPOPT_OFFSET;

    pointer = *opt++;
    pointer -= (IPOPT_MINOFF+1);
    opt++; length--;			/* Skip overflow/flag byte */
    length--;
    while (length > 0) {
	if (length < 4) 
	    break;
	if (pointer == 0 && rrflag) {
	    printf(" (End of record)");
	    break;
	}
	if (address_present) {
	    bcopy((char *)opt, (char *)&addr, sizeof(addr));
	    printf( "%s: ", pr_name(addr) );
    	}
	opt += sizeof(addr);
	length -= sizeof(addr);
	pointer -= sizeof(addr);
	bcopy((char *)opt, (char *)&time, sizeof(time));
	printf( "%d", time);
	if (pointer == 0)
	    printf( "(Current)");
	opt += sizeof(time);
	length -= sizeof(time);
	pointer -= sizeof(time);
	if (length >0)
	    printf( ", ");
    }
}


/*
 *			I N _ C K S U M
 *
 * Checksum routine for Internet Protocol family headers (C Version)
 *
 */
in_cksum(addr, len)
u_short *addr;
int len;
{
	register int nleft = len;
	register u_short *w = addr;
	register u_short answer;
	u_short odd_byte = 0;
	register int sum = 0;

	/*
	 *  Our algorithm is simple, using a 32 bit accumulator (sum),
	 *  we add sequential 16 bit words to it, and at the end, fold
	 *  back all the carry bits from the top 16 bits into the lower
	 *  16 bits.
	 */
	while( nleft > 1 )  {
		sum += *w++;
		nleft -= 2;
	}

	/* mop up an odd byte, if necessary */
	if( nleft == 1 ) {
		*(u_char *)(&odd_byte) = *(u_char *)w;
		sum += odd_byte;
	}

	/*
	 * add back carry outs from top 16 bits to low 16 bits
	 */
	sum = (sum >> 16) + (sum & 0xffff);	/* add hi 16 to low 16 */
	sum += (sum >> 16);			/* add carry */
	answer = ~sum;				/* truncate to 16 bits */
	return (answer);
}

/*
 * 			T V S U B
 * 
 * Subtract 2 timeval structs:  out = out - in.
 * 
 * Out is assumed to be >= in.
 */
tvsub( out, in )
register struct timeval *out, *in;
{
	if( (out->tv_usec -= in->tv_usec) < 0 )   {
		out->tv_sec--;
		out->tv_usec += 1000000;
	}
	out->tv_sec -= in->tv_sec;
}

/*
 *			F I N I S H
 *
 * Print out statistics, and give up.
 * Heavily buffered STDIO is used here, so that all the statistics
 * will be written with 1 sys-write call.  This is nice when more
 * than one copy of the program is running on a terminal;  it prevents
 * the statistics output from becomming intermingled.
 */
void
finish()
{
	printf("\n----%s PING Statistics----\n", hostname );
	printf("%d packets transmitted, ", ntransmitted );
	printf("%d packets received, ", nreceived );
	if (ntransmitted)
		if( nreceived <= ntransmitted)
			printf("%d%% packet loss", 
			  (int) (((ntransmitted-nreceived)*100) /
			  ntransmitted));
		else
			printf("%.2f times amplification", 
			  (double)nreceived / (double)ntransmitted);
	printf("\n");
	if (nreceived && timing)
	    printf("round-trip (ms)  min/avg/max = %d/%d/%d\n",
		tmin,
		tsum / nreceived,
		tmax );
	fflush(stdout);
	exit(0);
}



#if 	defined(SYSV) && !defined(SIOCGIFCONF_FIXED)
#define MAXIFS	32	/* results in a bufsize of 1024 */
#else
#define MAXIFS	256
#endif

/*
 * Get host's IP address via ioctl.
 */

static
get_myaddress(addr)
	struct sockaddr_in *addr;
{
	int soc;
	char *buf;
	struct ifconf ifc;
	struct ifreq ifreq, *ifr;
	int len;
	int numifs;
	unsigned bufsize;

	if ((soc = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror ("ping: socket(AF_INET, SOCK_DGRAM, 0)");
		exit(1);
	}
#ifdef SIOCGIFNUM
	if (ioctl(soc, SIOCGIFNUM, (char *)&numifs) < 0) {
		numifs = MAXIFS;
	}
#else
	numifs = MAXIFS;
#endif
	bufsize = numifs * sizeof(struct ifreq);
	buf = malloc(bufsize);
	if (buf == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	bzero((char *) &ifc, sizeof(ifc));
	ifc.ifc_len = bufsize;
	ifc.ifc_buf = &buf[0];
	if (ioctl(soc, SIOCGIFCONF, (char *)&ifc) < 0) {
		perror ("ping: ioctl (SIOCGIFCONF)");
		exit(1);
	}
	ifr = ifc.ifc_req;
	for (len = ifc.ifc_len; len; len -= sizeof ifreq) {
		ifreq = *ifr;
		if (ioctl(soc, SIOCGIFFLAGS, (char *)&ifreq) < 0) {
			perror  ("ping: ioctl (SIOCGIFFLAGS)");
			exit(1);
		}
		if ((ifreq.ifr_flags & IFF_UP) &&
		    (ifreq.ifr_flags & IFF_LOOPBACK) == 0 &&
		    ifr->ifr_addr.sa_family == AF_INET) {
			bzero ((char *) addr, sizeof (struct sockaddr_in));
			*addr = *((struct sockaddr_in *)&ifr->ifr_addr);
			break;
		}
		ifr++;
	}
	(void) close(soc);
	(void) free(buf);
}

