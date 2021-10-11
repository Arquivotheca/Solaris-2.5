/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#ident	"@(#)ifconfig.c	1.27	95/07/07 SMI"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <netdb.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/sockio.h>
#ifdef SYSV
#include <sys/stropts.h>
#endif SYSV

#include <netinet/in.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>

#ifdef NS
#define	NSIP
#include <netns/ns.h>
#include <netns/ns_if.h>
#endif

#ifdef APPLETALK
#include <netat/atalk.h>
#endif APPLETALK

/* The following three include files are for ifconfig -a plumb */
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <libdevinfo.h>

extern char *getmaskbyaddr();

#define MAXIFS	256
#define LOOPBACK_IF "lo0"

struct	ifreq ifr;
struct	sockaddr_in sin = { AF_INET };
struct	sockaddr_in broadaddr;
struct	sockaddr_in netmask = { AF_INET };
struct	sockaddr_in ipdst = { AF_INET };
char	name[30];
int	setaddr;
int	setmask;
int	setbroadaddr;
int	setipdst;
int	s;
int	debug = 0;

void	setifflags(), setifaddr(), setifdstaddr(), setifnetmask();
void	setifmetric(), setifbroadaddr(), setifipdst(), setifether();
void	setifmtu(), inetplumb(), inetunplumb();
void	setdebugflag();
void	printb();
void	status();

extern	void setifrevarp();
char 	*plumbstr = "plumb";

#define	NEXTARG		0xffffff
#define	AF_ANY		(-1)

struct	cmd {
	char	*c_name;
	int	c_parameter;		/* NEXTARG means next argv */
	void	(*c_func)();
	int	c_af;			/* address family restrictions */
} cmds[] = {
	{ "up",		IFF_UP,		setifflags,	AF_ANY } ,
	{ "down",	-IFF_UP,	setifflags,	AF_ANY },
	{ "trailers",	-IFF_NOTRAILERS,setifflags,	AF_ANY },
	{ "-trailers",	IFF_NOTRAILERS,	setifflags,	AF_ANY },
	{ "arp",	-IFF_NOARP,	setifflags,	AF_ANY },
	{ "-arp",	IFF_NOARP,	setifflags,	AF_ANY },
	{ "private",	IFF_PRIVATE,	setifflags,	AF_ANY },
	{ "-private",	-IFF_PRIVATE,	setifflags,	AF_ANY },
	{ "netmask",	NEXTARG,	setifnetmask,	AF_INET },
	{ "metric",	NEXTARG,	setifmetric,	AF_ANY },
	{ "mtu",	NEXTARG,	setifmtu,	AF_ANY },
	{ "broadcast",	NEXTARG,	setifbroadaddr,	AF_ANY },
	{ "ipdst",	NEXTARG,	setifipdst,	AF_NS },
	{ "auto-revarp", 0,		setifrevarp,	AF_ANY },
	{ "plumb",	0,		inetplumb,	AF_ANY },
	{ "unplumb",	0,		inetunplumb,	AF_ANY },
	{ "debug",	0,		setdebugflag,	AF_ANY }, 
	{ 0,		0,		setifether,	AF_UNSPEC },
	{ 0,		0,		setifaddr,	AF_ANY },
	{ 0,		0,		setifdstaddr,	AF_ANY },
	{ 0,		0,		0,		0 },
};

/*
 * XNS support liberally adapted from
 * code written at the University of Maryland
 * principally by James O'Toole and Chris Torek.
 */

void	in_status(), in_getaddr();
#ifdef	NS
void	xns_status(), xns_getaddr();
#endif
#ifdef	APPLETALK
void	at_status(), at_getaddr();
#endif
void	ether_status(), ether_getaddr();
void	Perror(), Perror2();

/* Data structures and defines for ifconfig -a plumb */
const char *class = DDI_NT_NET;	/* DDI class to look for */

static int num_ni = 0;

struct net_interface {
	char *type;	/* Name of type of interface  (le, ie, etc.)    */
	char *name;	/* Qualified name of interface (le0, ie0, etc.) */
};

#define	ni_set_type(i, s)	{ \
	if ((i)->type) \
		free((i)->type); \
	(i)->type = strdup((char *)s); \
}
#define	ni_get_type(i)	((i)->type)

#define	ni_set_name(i, s)	{ \
	if ((i)->name) \
		free((i)->name); \
	(i)->name = strdup(s); \
}
#define	ni_get_name(i)	((i)->name)

#define	ni_new(i)	{ \
	(i) = (struct net_interface *)malloc(sizeof (struct net_interface)); \
	if ((i) == (struct net_interface *)0) { \
		fprintf(stderr,  \
		    "Malloc failure for net_interface allocator\n"); \
		exit(1); \
	} \
	memset((char *)(i), '\0', sizeof (struct net_interface)); \
	num_ni++; \
}

#define	ni_destroy(i) { \
	if ((i)->name != 0) \
		free((i)->name); \
	if ((i)->type != 0) \
		free((i)->type); \
	if ((i) != (struct net_interface *)0) \
		free((char *)(i)); \
}


struct ni_list {
	struct net_interface *nifp;
	struct ni_list *next;
};

/* Access functions for nif_list struct */
#define	first_if(a)	(a)
#define	get_nifp(a)	((a)->nifp)
#define	set_nifp(a, i)	(a)->nifp = (i)
#define	next_if(a)	((a)->next)

#define	nil_new(i)	{ \
	(i) = (struct ni_list *)malloc(sizeof (struct ni_list)); \
	if ((i) == (struct ni_list *)0) { \
		fprintf(stderr,  \
		    "Malloc failure for ni_list allocator\n"); \
		exit(1); \
	} \
	memset((char *)(i), '\0', sizeof (struct ni_list)); \
}

#define	nil_destroy(i) { \
	if ((i) != (struct ni_list *)0) \
		free((char *)(i)); \
}

#define	NIL_NULL 	((struct ni_list *)0)

struct ni_list *nil_head = NIL_NULL;
static void devfs_entry(const char *devfsnm, const char *devfstype,
    dev_info_t *dip, struct ddi_minor_data *dmdp,
    struct ddi_minor_data *dmdap);

/* End defines and structure definitions for ifconfig -a plumb */

/* Known address families */
struct afswtch {
	char *af_name;
	short af_af;
	void (*af_status)();
	void (*af_getaddr)();
} afs[] = {
	{ "inet",	AF_INET,	in_status,	in_getaddr },
#ifdef	NS
	{ "ns",		AF_NS,		xns_status,	xns_getaddr },
#endif
#ifdef	APPLETALK
	{ "appletalk",	AF_APPLETALK,	at_status,	at_getaddr },
#endif
	{ "ether",	AF_UNSPEC,	ether_status,	ether_getaddr},
	{ 0,		0,		0,		0 }
};

#define SOCKET_AF(af) 	(((af) == AF_UNSPEC) ? AF_INET : (af))

struct afswtch *afp;	/*the address family being set or asked about*/

#ifdef SYSV
#define bzero(s,n)	memset((s), 0, (n))
#define bcopy(a,b,c)	memcpy((b),(a),(c))
#endif SYSV

main(argc, argv)
	int argc;
	char *argv[];
{
	int af = AF_INET;	/* default address family */
	void foreachinterface(), ifconfig();

	if (argc < 2) {
		fprintf(stderr,
			"usage: ifconfig <interface> | -a | -au | -ad \n");
		fprintf(stderr, "%s%s%s%s%s%s%s%s\n",
		    "\t[ <af> ] [ <address> [ <dest_addr> ] ] [ up ] [ down ]\n",
		    "\t[ auto-revarp ] \n",
		    "\t[ netmask <mask> ] [ broadcast <broad_addr> ]\n",
		    "\t[ metric <n> ]\n",
		    "\t[ mtu <n> ]\n",
		    "\t[ trailers | -trailers ] [private | -private]\n",
		    "\t[ arp | -arp ] \n",
		    "\t[ plumb ] \n",
		    "\t[ unplumb ] \n");
		exit(1);
	}
	argc--, argv++;
	strncpy(name, *argv, sizeof(name));
	argc--, argv++;
	if (argc > 0) {
		struct afswtch *myafp;
		
		for (myafp = afp = afs; myafp->af_name; myafp++)
			if (strcmp(myafp->af_name, *argv) == 0) {
				afp = myafp; argc--; argv++;
				break;
			}
		af = ifr.ifr_addr.sa_family = afp->af_af;
	}
	s = socket(SOCKET_AF(af), SOCK_DGRAM, 0);
	if (s < 0) {
		Perror("socket");
	}
	/*
	 * Special interface names:
	 *	-a	All interfaces
	 *	-au	All "up" interfaces
	 *	-ad	All "down" interfaces
	 */
	if (strcmp(name, "-a") == 0)
		foreachinterface(ifconfig, argc, argv, af, 0, 0);
	else if (strcmp(name, "-au") == 0)
		foreachinterface(ifconfig, argc, argv, af, IFF_UP, 0);
	else if (strcmp(name, "-ad") == 0)
		foreachinterface(ifconfig, argc, argv, af, 0, IFF_UP);
	else if (strcmp(name, "-aD") == 0) {
		debug += 3;
		foreachinterface(ifconfig, argc, argv, af, 0, 0);
	} else if (strcmp(name, "-auD") == 0) {
		debug += 3;
		foreachinterface(ifconfig, argc, argv, af, IFF_UP, 0);
	} else if (strcmp(name, "-adD") == 0) {
		debug += 3;
		foreachinterface(ifconfig, argc, argv, af, 0, IFF_UP);
	} else
		ifconfig(argc, argv, af, (struct ifnet *) NULL);

	return(0);
}

/*
 * For each interface, call (*func)(argc, argv, af, ifrp).
 * Only call function if onflags and offflags are set or clear, respectively,
 * in the interfaces flags field.
 */
void
foreachinterface(func, argc, argv, af, onflags, offflags)
	void (*func)();
	int argc;
	char **argv;
	int af;
	int onflags;
	int offflags;
{
	int n;
	char *buf;
	struct ifconf ifc;
	register struct ifreq *ifrp;
	struct ifreq ifr;
	int numifs;
	unsigned bufsize;
	struct ni_list *nilp, *onilp;
	struct net_interface *nip;
	
	/* 
	 * Special case: 
	 * ifconfig -a plumb should find all network interfaces
	 * in the machine by traversing the devinfo tree.
	 * Also, there is no need to  SIOCGIF* ioctls, since
	 * those interfaces have already been plumbed
	 * See bugid 1112846
	 */
	if (argc > 0  && (strcmp(*argv, plumbstr) == 0)) {
		/*
		 * Look through the kernel's devinfo tree for
		 * network devices
		 */

		devfs_find(class, 
		    (void (*)(const char *devfsnm, 
		    const char *devfstype, const dev_info_t *, 
		    struct ddi_minor_data *, struct ddi_minor_data *)
		    )devfs_entry, 
		    1 /* check aliases */);

		/* 
		 * Now, translate the linked list into
		 * a struct ifreq buffer
		 */
		bufsize = num_ni * sizeof(struct ifreq);
		buf = malloc(bufsize);
		ifc.ifc_len = bufsize;
		ifc.ifc_buf = buf;
		ifrp = ifc.ifc_req;
		for(nilp = first_if(nil_head), n=0; 
		    n < num_ni; nilp = next_if(nilp), n++, ifrp++) {
			nip = get_nifp(nilp);
			strncpy(ifrp->ifr_name, ni_get_name(nip),
				sizeof(ifr.ifr_name));
		}
		
		/*
		 * Now clean up the network interface list 
		 */
		for(nilp = first_if(nil_head); nilp; ) {
			nip = get_nifp(nilp);
			ni_destroy(nip);
			onilp = nilp;
			nilp = next_if(nilp);
			nil_destroy(onilp);
		}
	} else {
		if (ioctl(s, SIOCGIFNUM, (char *)&numifs) < 0) {
			numifs = MAXIFS;
		} 
		bufsize = numifs * sizeof(struct ifreq);
		buf = malloc(bufsize);
		if (buf == NULL) {
			fprintf(stderr, "out of memory\n");
			close(s);
			return;
		}
		ifc.ifc_len = bufsize;
		ifc.ifc_buf = buf;
		if (ioctl(s, SIOCGIFCONF, (char *)&ifc) < 0) {
			perror("ifconfig: SIOCGIFCONF");
			close(s);
			free(buf);
			return;
		}
	}

	ifrp = ifc.ifc_req;
	for (n = ifc.ifc_len / sizeof (struct ifreq); n > 0; n--, ifrp++) {
		/*
		 *	We must close and recreate the socket each time
		 *	since we don't know what type of socket it is now
		 *	(each status function may change it).
		 */
		(void) close(s);

		s = socket(SOCKET_AF(af), SOCK_DGRAM, 0);

		if (s == -1) {
			Perror("socket");
		}

		/* 
		 * Only service interfaces that match the on and off
		 * flags masks.
		 */
		if (onflags || offflags) {
			bzero ((char *) &ifr, sizeof(ifr));
			strncpy(ifr.ifr_name, ifrp->ifr_name, 
				sizeof(ifr.ifr_name));
			if (ioctl(s, SIOCGIFFLAGS, (caddr_t)&ifr) < 0){
				Perror("SIOCGIFFLAGS");
			}
			if ((ifr.ifr_flags & onflags) != onflags) 
				continue;
			if ((~ifr.ifr_flags & offflags) != offflags)
				continue;
		}

		/*
		 *	Reset global state variables to known values.
		 */
		setaddr = setbroadaddr = setipdst = setmask = 0;
		(void) strncpy(name, ifrp->ifr_name, sizeof(name));

		(*func)(argc, argv, af, ifrp);
	}
	free(buf);
}

void
ifconfig(argc, argv, af, ifrp)
	int argc;
	char **argv;
	int af;
	register struct ifreq *ifrp;
{
	if (argc == 0) {
		status();
		return;
	}
	while (argc > 0) {
		register struct cmd *p;

		for (p = cmds; p->c_func; p++)
			if (p->c_name) {
				if (strcmp(*argv, p->c_name) == 0)
					break;
			} else {
				if (p->c_af == AF_ANY || 
				    af == p->c_af)
					break;
			}
		if (p->c_name == 0 && setaddr)
			p++;	/* got src, do dst */
		if (p->c_func) {
			if (p->c_parameter == NEXTARG) {
				argc--, argv++;
				if (argc == 0) {
					(void) fprintf(stderr, 
					    "ifconfig: no argument for %s\n",
					    p->c_name);
					exit(1);
				}
				/*
				 *	Call the function if:
				 *
				 *		there's no address family
				 *		restriction
				 *	OR
				 *		we don't know the address yet
				 *		(because we were called from
				 *		main)
				 *	OR
				 *		there is a restriction and
				 *		the address families match
				 */
				if ((p->c_af == AF_ANY)			||
				    (ifrp == (struct ifreq *) NULL)	||
				    (ifrp->ifr_addr.sa_family == p->c_af)
				   )
					(*p->c_func)(*argv);
			} else
				if ((p->c_af == AF_ANY)			||
				    (ifrp == (struct ifreq *) NULL)	||
				    (ifrp->ifr_addr.sa_family == p->c_af)
				   )
					(*p->c_func)(*argv, p->c_parameter);
		}
		argc--, argv++;
	}
	if ((setmask || setaddr) && (af == AF_INET) && 
	     netmask.sin_addr.s_addr != INADDR_ANY) {
		/*
		 * If setting the address and not the mask,
		 * clear any existing mask and the kernel will then
		 * assign the default.  If setting both,
		 * set the mask first, so the address will be
		 * interpreted correctly.
		 */
		strncpy(ifr.ifr_name, name, sizeof (ifr.ifr_name));
		ifr.ifr_addr = *(struct sockaddr *)&netmask;
		if (ioctl(s, SIOCSIFNETMASK, (caddr_t)&ifr) < 0)
			Perror("SIOCSIFNETMASK");
	}
#ifdef NS
	if (setipdst && af == AF_NS) {
		struct nsip_req rq;
		int size = sizeof(rq);

		rq.rq_ns = *(struct sockaddr *) &sin;
		rq.rq_ip = *(struct sockaddr *) &ipdst;

		if (setsockopt(s, 0, SO_NSIP_ROUTE, &rq, size) < 0)
			Perror("Encapsulation Routing");
		setaddr = 0;
	}
#endif
	if (setaddr) {
		ifr.ifr_addr = *(struct sockaddr *) &sin;
		strncpy(ifr.ifr_name, name, sizeof (ifr.ifr_name));
		if (ioctl(s, SIOCSIFADDR, (caddr_t)&ifr) < 0)
			Perror("SIOCSIFADDR");
	}
	if (setbroadaddr) {
		ifr.ifr_addr = *(struct sockaddr *)&broadaddr;
		strncpy(ifr.ifr_name, name, sizeof (ifr.ifr_name));
		if (ioctl(s, SIOCSIFBRDADDR, (caddr_t)&ifr) < 0)
			Perror("SIOCSIFBRDADDR");
	}
}

/*ARGSUSED*/
void
setdebugflag(val, arg)
	char *val;
	int arg;
{
	debug++;
}

/*ARGSUSED*/
void
setifaddr(addr, param)
	char *addr;
	short param;
{
	/*
	 * Delay the ioctl to set the interface addr until flags are all set.
	 * The address interpretation may depend on the flags,
	 * and the flags may change when the address is set.
	 */
	setaddr++;
	(*afp->af_getaddr)(addr, &sin);
}

/*ARGSUSED*/
void
setifnetmask(addr)
	char *addr;
{
	if (strcmp(addr, "+") == 0) {
		setmask = in_getmask( &netmask);
		return;
	}
	in_getaddr(addr, &netmask);
	setmask++;
}

/*ARGSUSED*/
void
setifbroadaddr(addr)
	char *addr;
{
	/*
	 *	This doesn't set the broadcast address at all.  Rather, it
	 *	gets, then sets the interface's address, relying on the fact
	 *	that resetting the address will reset the broadcast address.
	 */
	if (strcmp(addr, "+") == 0) {
		if (!setaddr) {
			/*
			 * If we do not already have an address to set,
			 * then we just read the interface to see if it
			 * is already set.
			 */
			strncpy(ifr.ifr_name, name, sizeof (ifr.ifr_name));
			if (ioctl(s, SIOCGIFADDR, (caddr_t)&ifr) < 0) {
				if (errno != EADDRNOTAVAIL)
					Perror("SIOCGIFADDR");
				return;
			}
			sin = * ( (struct sockaddr_in *)&ifr.ifr_addr);
			setaddr++;

		}
		/*
		 *	Turn off setbraodcast to allow for the (rare)
		 *	case of a user saying
		 *	ifconfig ... broadcast <number> ... broadcast +
		 */
		setbroadaddr = 0;
		return;
	}
	(*afp->af_getaddr)(addr, &broadaddr);
	setbroadaddr++;
}

/*ARGSUSED*/
void
setifipdst(addr)
	char *addr;
{
	in_getaddr(addr, &ipdst);
	setipdst++;
}

/*ARGSUSED*/
void
setifdstaddr(addr, param)
	char *addr;
	int param;
{

	(*afp->af_getaddr)(addr, &ifr.ifr_addr);
	strncpy(ifr.ifr_name, name, sizeof (ifr.ifr_name));
	if (ioctl(s, SIOCSIFDSTADDR, (caddr_t)&ifr) < 0)
		Perror("SIOCSIFDSTADDR");
}

/*ARGSUSED*/
void
setifflags(vname, value)
	char *vname;
	int value;
{

	strncpy(ifr.ifr_name, name, sizeof (ifr.ifr_name));
	if (ioctl(s, SIOCGIFFLAGS, (caddr_t)&ifr) < 0)
		Perror("SIOCGIFFLAGS");
	if (value < 0) {
		value = -value;
		ifr.ifr_flags &= ~value;
	} else
		ifr.ifr_flags |= value;
	strncpy(ifr.ifr_name, name, sizeof (ifr.ifr_name));
	if (ioctl(s, SIOCSIFFLAGS, (caddr_t)&ifr) < 0)
		Perror("SIOCSIFFLAGS");
}

/*ARGSUSED*/
void
setifmetric(val)
	char *val;
{
	strncpy(ifr.ifr_name, name, sizeof (ifr.ifr_name));
	ifr.ifr_metric = atoi(val);
	if (ioctl(s, SIOCSIFMETRIC, (caddr_t)&ifr) < 0)
		Perror("SIOCSIFMETRIC");
}

/*ARGSUSED*/
void
setifmtu(val)
	char *val;
{
	strncpy(ifr.ifr_name, name, sizeof (ifr.ifr_name));
	ifr.ifr_metric = atoi(val);
	if (ioctl(s, SIOCSIFMTU, (caddr_t)&ifr) < 0)
		Perror("SIOCSIFMTU");
}

/*ARGSUSED*/
void
setifether(addr)
	char *addr;
{
#ifdef SYSV
	int	savedflags;
	int	ip_interface;	/* Used by IP? */

	ether_addr_t *ea, *ether_aton();

	ea = ether_aton(addr);
	if (ea == NULL) {
		fprintf(stderr, "ifconfig: %s: bad address\n", addr);
		exit(1);
	}

	/*
	 * If IP/ARP are using this interface then bring the interface down
	 * and up again after the physical address has been changed. 
	 */
	if (dlpi_set_address(name, ea) == -1) {
		fprintf(stderr, "ifconfig: failed setting mac address on %s\n",
			name);
	}

	strncpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
	if (ioctl(s, SIOCGIFFLAGS, (caddr_t)&ifr) < 0) 
		ip_interface = 0;
	else
		ip_interface = 1;

	if (ip_interface) {
		/*
		 * Make arp aware of the new Ethernet address by bringing 
		 * the interface down and up again.
		 */
		savedflags = ifr.ifr_flags;
		ifr.ifr_flags &= ~IFF_UP;

		if (ioctl(s, SIOCSIFFLAGS, (caddr_t)&ifr) < 0) {
			Perror("SIOCSIFFLAGS");
			exit(1);
		}

		ifr.ifr_flags = savedflags;
		if (ioctl(s, SIOCSIFFLAGS, (caddr_t)&ifr) < 0) {
			Perror("SIOCSIFFLAGS");
			exit(1);
		}
	}
#else
	printf("setifether option not yet supported.\n");
#endif	/* SYSV */
}

#define	IFFBITS \
"\020\1UP\2BROADCAST\3DEBUG\4LOOPBACK\5POINTOPOINT\6NOTRAILERS\7RUNNING\10NOARP\11PROMISC\12ALLMULTI\13INTELLIGENT\14MULTICAST\15MULTI_BCAST\16UNNUMBERED\020PRIVATE"

/*
 * Print the status of the interface.  If an address family was
 * specified, show it and it only; otherwise, show them all.
 */
void
status()
{
	register struct afswtch *p = afp;
	register int flags;

	strncpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
	if (ioctl(s, SIOCGIFFLAGS, (caddr_t)&ifr) < 0) {
		Perror("SIOCGIFFLAGS");
		exit(1);
	}
	flags = ifr.ifr_flags;
	printf("%s: ", name);
	printb("flags", (unsigned short) flags, IFFBITS);
	strncpy(ifr.ifr_name, name, sizeof ifr.ifr_name);
	if (ioctl(s, SIOCGIFMETRIC, (caddr_t)&ifr) < 0)
		Perror("SIOCGIFMETRIC");
	else {
		if (ifr.ifr_metric)
			printf(" metric %d", ifr.ifr_metric);
	}
	if (ioctl(s, SIOCGIFMTU, (caddr_t)&ifr) >= 0)
		printf(" mtu %d", ifr.ifr_metric);
	putchar('\n');
	if ((p = afp) != NULL) {
		(*p->af_status)(1, flags);
	} else for (p = afs; p->af_name; p++) {
		ifr.ifr_addr.sa_family = p->af_af;
		(*p->af_status)(0, flags);
	}
}

void
in_status(force, flags)
	int force;
	int flags;
{
	struct sockaddr_in *sin;

	strncpy(ifr.ifr_name, name, sizeof (ifr.ifr_name));
	if (ioctl(s, SIOCGIFADDR, (caddr_t)&ifr) < 0) {
		if (errno == EADDRNOTAVAIL || errno == EAFNOSUPPORT) {
			if (!force)
				return;
			bzero((char *)&ifr.ifr_addr, sizeof(ifr.ifr_addr));
		} else
			Perror("SIOCGIFADDR");
	}
	sin = (struct sockaddr_in *)&ifr.ifr_addr;
	printf("\tinet %s ", inet_ntoa(sin->sin_addr));
	strncpy(ifr.ifr_name, name, sizeof (ifr.ifr_name));
	if (ioctl(s, SIOCGIFNETMASK, (caddr_t)&ifr) < 0) {
		if (errno != EADDRNOTAVAIL)
			Perror("SIOCGIFNETMASK");
		bzero((char *)&ifr.ifr_addr, sizeof(ifr.ifr_addr));
	} else
		netmask.sin_addr =
		    ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;
	if (flags & IFF_POINTOPOINT) {
		strncpy(ifr.ifr_name, name, sizeof (ifr.ifr_name));
		if (ioctl(s, SIOCGIFDSTADDR, (caddr_t)&ifr) < 0) {
			if (errno == EADDRNOTAVAIL)
			    bzero((char *)&ifr.ifr_addr, sizeof(ifr.ifr_addr));
			else
			    Perror("SIOCGIFDSTADDR");
		}
		sin = (struct sockaddr_in *)&ifr.ifr_dstaddr;
		printf("--> %s ", inet_ntoa(sin->sin_addr));
	}
	printf("netmask %x ", ntohl(netmask.sin_addr.s_addr));
	if (flags & IFF_BROADCAST) {
		strncpy(ifr.ifr_name, name, sizeof (ifr.ifr_name));
		if (ioctl(s, SIOCGIFBRDADDR, (caddr_t)&ifr) < 0) {
			if (errno == EADDRNOTAVAIL)
			    bzero((char *)&ifr.ifr_addr, sizeof(ifr.ifr_addr));
			else
			    Perror("SIOCGIFADDR");
		}
		sin = (struct sockaddr_in *)&ifr.ifr_addr;
		if (sin->sin_addr.s_addr != 0)
			printf("broadcast %s", inet_ntoa(sin->sin_addr));
	}
	putchar('\n');
}


#ifdef	NS
/*ARGSUSED*/
void
xns_status(force, flags)
	int force;
	int flags;
{
	struct sockaddr_ns *sns;

	close(s);
	s = socket(AF_NS, SOCK_DGRAM, 0);
	if (s < 0) {
		if (errno == EAFNOSUPPORT || errno == EPROTONOSUPPORT)
			return;
		Perror("ns socket");
	}
	strncpy(ifr.ifr_name, name, sizeof ifr.ifr_name);
	if (ioctl(s, SIOCGIFADDR, (caddr_t)&ifr) < 0) {
		if (errno == EADDRNOTAVAIL || errno == EAFNOSUPPORT) {
			if (!force)
				return;
			bzero((char *)&ifr.ifr_addr, sizeof(ifr.ifr_addr));
		} else
			Perror("SIOCGIFADDR");
	}
	sns = (struct sockaddr_ns *)&ifr.ifr_addr;
	printf("\tns %s ", ns_ntoa(sns->sns_addr));
	if (flags & IFF_POINTOPOINT) { /* by W. Nesheim@Cornell */
		strncpy(ifr.ifr_name, name, sizeof ifr.ifr_name);
		if (ioctl(s, SIOCGIFDSTADDR, (caddr_t)&ifr) < 0) {
			if (errno == EADDRNOTAVAIL)
			    bzero((char *)&ifr.ifr_addr, sizeof(ifr.ifr_addr));
			else
			    Perror("SIOCGIFDSTADDR");
		}
		sns = (struct sockaddr_ns *)&ifr.ifr_dstaddr;
		printf("--> %s ", ns_ntoa(sns->sns_addr));
	}
	putchar('\n');
}
#endif 

#ifdef APPLETALK
char *
at_ntoa(sad)
	struct a_addr sad;
{
	static char buf[256];
	(void) sprintf( buf, "%d.%d", sad.at_Net, sad.at_Node);
	return(buf);
}
#endif AAPLETALK

/*ARGSUSED*/
void
at_status(force, flags)
	int force;
	int flags;
{
#ifdef APPLETALK
	struct sockaddr_at *sat;

	close(s);
	s = socket(AF_APPLETALK, SOCK_DGRAM, 0);
	if (s < 0) {
		if (errno == EAFNOSUPPORT || errno == EPROTONOSUPPORT)
			return;
		Perror("appletalk socket");
	}
	strncpy(ifr.ifr_name, name, sizeof ifr.ifr_name);
	if (ioctl(s, SIOCGIFADDR, (caddr_t)&ifr) < 0) {
		if (errno == EADDRNOTAVAIL || errno == EAFNOSUPPORT) {
			if (!force)
				return;
			bzero((char *)&ifr.ifr_addr, sizeof(ifr.ifr_addr));
		} else
			Perror("SIOCGIFADDR");
	}
	sat = (struct sockaddr_at *)&ifr.ifr_addr;
	printf("\tappletalk %s ", at_ntoa(sat->at_addr));
	if (flags & IFF_POINTOPOINT) {
		strncpy(ifr.ifr_name, name, sizeof ifr.ifr_name);
		if (ioctl(s, SIOCGIFDSTADDR, (caddr_t)&ifr) < 0) {
			if (errno == EADDRNOTAVAIL)
			    bzero((char *)&ifr.ifr_addr, sizeof(ifr.ifr_addr));
			else
			    Perror("SIOCGIFDSTADDR");
		}
		sat = (struct sockaddr_at *)&ifr.ifr_dstaddr;
		printf("--> %s ", at_ntoa(sat->at_addr));
	}
	putchar('\n');
#endif APPLETALK
}

/*ARGSUSED*/
void
ether_status(force, flags)
	int force;
	int flags;
{
#ifdef SYSV
	char *ether_ntoa();
	ether_addr_t	eas;

	/* Gross hack to avoid getting errors from /dev/lo0" */
	if (strcmp(name, LOOPBACK_IF) == 0)
		return;

	if (dlpi_get_address(name, &eas) == 0) {
		printf("\tether %s ", ether_ntoa(&eas));
		putchar('\n');
	}
#endif /* SYSV */
}

/* TODO fix include files so that they can be included here! */
#ifdef notdef
#include <inet/common.h>
#include <inet/ip.h>
#include <inet/arp.h>
#endif

#ifndef	ARP_MOD_NAME
#define	ARP_MOD_NAME	"arp"
#endif

#ifndef	IP_DEV_NAME
#define	IP_DEV_NAME	"/dev/ip"
#endif

#ifndef	IP_MOD_NAME
#define	IP_MOD_NAME	"ip"
#endif

#define	MAXPATHL	128


static void
plumb_one_device (dev_name, ppa, mux_fd)
	char	* dev_name;
	int	ppa;
	int	mux_fd;
{
	int	arp_fd = -1;
	int	ip_fd = -1;
	int	arp_muxid, ip_muxid;

	/* Open the device and push IP */
	if ((ip_fd = open(dev_name, 0)) == -1)
		Perror2("open", dev_name);

	if (ioctl(ip_fd, I_PUSH, IP_MOD_NAME) == -1)
		Perror2("I_PUSH", IP_MOD_NAME);

	if (ioctl(ip_fd, IF_UNITSEL, (char *)&ppa) == -1)
		Perror("I_UNITSEL for ip");

	/* Check if IFF_NOARP is set */
	strncpy(ifr.ifr_name, name, sizeof ifr.ifr_name);
	if (ioctl(ip_fd, SIOCGIFFLAGS, (caddr_t)&ifr) < 0)
		Perror("SIOCGIFFLAGS");

	if (!(ifr.ifr_flags & IFF_NOARP)) {
		if ((arp_fd = open(dev_name, 0)) == -1)
			Perror2("open", dev_name);

		if (ioctl(arp_fd, I_PUSH, ARP_MOD_NAME) == -1)
			Perror2("I_PUSH", ARP_MOD_NAME);

		if (ioctl(ip_fd, I_PUSH, ARP_MOD_NAME) == -1)
			Perror2("I_PUSH", ARP_MOD_NAME);

		/* Note that arp has no support for transparent ioctls */
		if (strioctl(arp_fd, IF_UNITSEL, (char *)&ppa, 
			     sizeof(ppa)) == -1)
			Perror("I_UNITSEL for arp");

		if ((arp_muxid = ioctl(mux_fd, I_PLINK, arp_fd)) == -1)
			Perror("I_PLINK for arp");

		ifr.ifr_arp_muxid = arp_muxid;
		if (debug)
			printf("arp muxid = %d\n", arp_muxid);
	}
	if ((ip_muxid = ioctl(mux_fd, I_PLINK, ip_fd)) == -1) {
		if (!(ifr.ifr_flags & IFF_NOARP))
			(void)ioctl(mux_fd, I_PUNLINK, ip_muxid);
		Perror("I_PLINK for ip");
	}
	ifr.ifr_ip_muxid = ip_muxid;
	if (debug)
		printf("ip muxid = %d\n", ip_muxid);
	if (ioctl(mux_fd, SIOCSIFMUXID, (caddr_t)&ifr) < 0)
		Perror("SIOCSIFMUXID");

	close(arp_fd);
	close(ip_fd);
}

static foundif;
static char savedname[30];

/*ARGSUSED*/
void
matchif(argc, argv, af, ifrp)
	int argc;
	char **argv;
	int af;
	register struct ifreq *ifrp;
{
	if (strcmp(savedname, name) == 0)
		foundif = 1;
}

/*ARGSUSED*/
void 
inetunplumb(arg)
	char *arg;
{
	int ip_muxid, arp_muxid;
	int mux_fd;
	int arp;

	mux_fd = open(IP_DEV_NAME, 2);
	if (mux_fd == -1) {
		Perror2("open", IP_DEV_NAME);
		exit(1);
	}
	strncpy(ifr.ifr_name, name, sizeof ifr.ifr_name);
	if (ioctl(mux_fd, SIOCGIFFLAGS, (caddr_t)&ifr) < 0) {
		Perror("SIOCGIFFLAGS");
		exit(1);
	}
	arp = !(ifr.ifr_flags & IFF_NOARP);
	if (ioctl(mux_fd, SIOCGIFMUXID, (caddr_t)&ifr) < 0) {
		Perror("SIOCGIFMUXID");
		exit(1);
	}
	if (arp) {
		arp_muxid = ifr.ifr_arp_muxid;
		if (debug) printf("arp_muxid %d\n", arp_muxid);
		if (ioctl(mux_fd, I_PUNLINK, arp_muxid) < 0) {
			Perror("I_PUNLINK for arp");
			exit (1);
		}
	}
	ip_muxid = ifr.ifr_ip_muxid;
	if (debug) printf("ip_muxid %d\n", ip_muxid);
	if (ioctl(mux_fd, I_PUNLINK, ip_muxid) < 0) {
		Perror("I_PUNLINK for ip");
		exit (1);
	}
	(void) close(mux_fd);
}

void
inetplumb()
{
	char		path[MAXPATHL];
	int		style;
	int		ppa;
	int 		mux_fd;
	int		fd;

	/* Gross hack to avoid "/dev/sle0:: no such file or directory" */
	if (strchr(name, ':'))
		return;

	/* Check if the interface already exists */
	foundif = 0;
	strcpy(savedname, name);
	foreachinterface(matchif, 0, (char **)0, AF_INET, 0, 0);
	/* Restore */
	strcpy(name, savedname);
	strncpy(ifr.ifr_name, name, sizeof ifr.ifr_name); /* For Perror */

	if (foundif) {
		return;
	}
	/* Verify that ppa is valid */
	fd = dlpi_open_attach(name);
	if (fd < 0) {
		/* Not found */
		Perror("plumb");
		return;
	}

	if (debug)
		printf("opened & attached %s\n", name);
	(void) close(fd);

	style = ifname2device_ppa(name, path, &ppa);
	if (style < 0) {
		/* Not found */
		Perror("plumb");
	}
	mux_fd = open(IP_DEV_NAME, 2);
	if (mux_fd == -1) {
		Perror2("open", IP_DEV_NAME);
		exit(1);
	}
	plumb_one_device(path, ppa, mux_fd);
	(void) close(mux_fd);
}

void
Perror(cmd)
	char *cmd;
{
	int save_errno;

	save_errno = errno;
	fprintf(stderr, "ifconfig: ");
	errno = save_errno;
	switch (errno) {

	case ENXIO:
		fprintf(stderr, "%s: %s: no such interface\n",
			cmd, ifr.ifr_name);
		break;

	case EPERM:
		fprintf(stderr, "%s: %s: permission denied\n",
			cmd, ifr.ifr_name);
		break;

	default: {
		char buf[BUFSIZ];

		sprintf(buf, "%s: %s", cmd, ifr.ifr_name);
		perror(buf);
	}
	}
	exit(1);
	/* NOTREACHED */
}

void
Perror2(cmd, str)
	char *cmd;
	char *str;
{
	int save_errno;

	save_errno = errno;
	fprintf(stderr, "ifconfig: ");
	errno = save_errno;
	switch (errno) {

	case ENXIO:
		fprintf(stderr, "%s: %s: no such interface\n",
			cmd, str);
		break;

	case EPERM:
		fprintf(stderr, "%s: %s: permission denied\n",
			cmd, str);
		break;

	default: {
		char buf[BUFSIZ];

		sprintf(buf, "%s: %s", cmd, str);
		perror(buf);
	}
	}
	exit(1);
	/* NOTREACHED */
}

struct	in_addr inet_makeaddr();

void
in_getaddr(s, saddr)
	char *s;
	struct sockaddr *saddr;
{
	register struct sockaddr_in *sin = (struct sockaddr_in *)saddr;
	struct hostent *hp;
	struct netent *np;
	u_long val;

	bzero((char *)saddr, sizeof *saddr);
	sin->sin_family = AF_INET;

	/*
	 *	Try to catch attempts to set the broadcast address to all 1's.
	 */
	if (strcmp(s,"255.255.255.255") == 0 || 
	    (strtoul(s, (char **) NULL, 0) == (unsigned long) 0xffffffff)) {
		sin->sin_addr.s_addr = 0xffffffff;
		return;
	}

	val = inet_addr(s);
	if (val != -1) {
		sin->sin_addr.s_addr = val;
		return;
	}
	hp = gethostbyname(s);
	if (hp) {
		sin->sin_family = hp->h_addrtype;
		bcopy(hp->h_addr, (char *)&sin->sin_addr, hp->h_length);
		return;
	}
	np = getnetbyname(s);
	if (np) {
		sin->sin_family = np->n_addrtype;
		sin->sin_addr = inet_makeaddr(np->n_net, INADDR_ANY);
		return;
	}
	fprintf(stderr, "ifconfig: %s: bad address\n", s);
	exit(1);
}

/*
 * Print a value a la the %b format of the kernel's printf
 */
void
printb(s, v, bits)
	char *s;
	register char *bits;
	register unsigned short v;
{
	register int i, any = 0;
	register char c;

	if (bits && *bits == 8)
		printf("%s=%o", s, v);
	else
		printf("%s=%x", s, v);
	bits++;
	if (bits) {
		putchar('<');
		while ((i = *bits++) != 0) {
			if (v & (1 << (i-1))) {
				if (any)
					putchar(',');
				any = 1;
				for (; (c = *bits) > 32; bits++)
					putchar(c);
			} else
				for (; *bits > 32; bits++)
					;
		}
		putchar('>');
	}
}

#ifdef	NS
/*ARGSUSED*/
void
xns_getaddr(addr, saddr)
char *addr;
struct sockaddr *saddr;
{
	struct sockaddr_ns *sns = (struct sockaddr_ns *)saddr;
	struct ns_addr ns_addr();

	bzero((char *)saddr, sizeof *saddr);
	sns->sns_family = AF_NS;
	sns->sns_addr = ns_addr(addr);
}
#endif

#ifdef	APPLETALK
/*ARGSUSED*/
void
at_getaddr(addr, saddr)
char *addr;
struct sockaddr *saddr;
{
	struct sockaddr_at *sat = (struct sockaddr_at *)saddr;

	bzero((char *)saddr, sizeof *saddr);
	sat->at_family = AF_APPLETALK;
	sat->at_addr.at_Net = atoi(addr);
}
#endif	APPLETALK

#ifdef SYSV
extern ether_addr_t *ether_aton();
#else
extern struct ether_addr *ether_aton();
#endif SYSV

void
ether_getaddr(addr, saddr)
char *addr;
struct sockaddr *saddr;
{
#ifdef SYSV
	ether_addr_t *eaddr;
#else
	struct ether_addr *eaddr;
#endif SYSV

	bzero((char *)saddr, sizeof *saddr);
	saddr->sa_family = AF_UNSPEC;
	/*
	 * call the library routine to do the conversion.
	 */
	eaddr = ether_aton(addr);
	if (eaddr == NULL) {
		fprintf(stderr, "ifconfig: %s: bad address\n", addr);
		exit(1);
	}
	bcopy(eaddr, saddr->sa_data, sizeof(*eaddr));
}

/*
 * Look in the NIS for the network mask.
 * returns true if we found one to set.
 */
int
in_getmask(saddr)
	struct sockaddr_in *saddr;
{
	char *out;
	u_char *bytes;
	int val;
	char key[128];
	struct in_addr net;
	u_long i;
#define OUTBUFLEN 1024
	char outbuf[OUTBUFLEN];	/* buffer for getmaskbyaddr() */

	if (!setaddr) {
		/*
		 * If we do not already have an address to set, then we just
		 * read the interface to see if it is already set.
		 */
		strncpy(ifr.ifr_name, name, sizeof (ifr.ifr_name));
		if (ioctl(s, SIOCGIFADDR, (caddr_t)&ifr) < 0) {
			if (errno != EADDRNOTAVAIL)
				printf("Need net number for mask\n");
			return(0);
		}
		sin = * ( (struct sockaddr_in *)&ifr.ifr_addr);
	}
	i = ntohl(sin.sin_addr.s_addr);
	bytes = (u_char *)(&net);
	if (IN_CLASSA(i)) {
		net.s_addr = htonl(i & IN_CLASSA_NET);
		sprintf(key, "%d", bytes[0]);
	}
	else if (IN_CLASSB(i)) {
		net.s_addr = htonl(i & IN_CLASSB_NET);
		sprintf(key, "%d.%d", bytes[0], bytes[1]);
	}
	else {
		net.s_addr = htonl(i & IN_CLASSC_NET);
		sprintf(key, "%d.%d.%d", bytes[0], bytes[1], bytes[2]);
	}
	out = getmaskbyaddr(key,outbuf,OUTBUFLEN);
	if (out == NULL) {
		  /*
		   * If at first not found, try unshifted too
		   */
		strcpy( key, inet_ntoa(net));
		out = getmaskbyaddr(key, outbuf, OUTBUFLEN);
		if (out == NULL) {
			saddr->sin_addr.s_addr = INADDR_ANY;
			return(0);
		}
	}
	val = (int)inet_addr(out);
	if (val == -1) {
		printf("Invalid netmask for %s: %s\n", key, out);
		return(0);
	}
	saddr->sin_family = AF_INET;
	saddr->sin_addr.s_addr = val;
	printf("Setting netmask of %s to %s\n", name,
				inet_ntoa(saddr->sin_addr) );
	return(1);
}
	
#ifdef SYSV
strioctl(s, cmd, buf, buflen)
	int s;
	int cmd;
	char *buf;
	int buflen;
{
	struct strioctl ioc;

	memset((char *)&ioc, 0, sizeof(ioc));
	ioc.ic_cmd = cmd;
	ioc.ic_timout = 0;
	ioc.ic_len = buflen;
	ioc.ic_dp = buf;
	return (ioctl(s, I_STR, (char *)&ioc));
}
#else
strioctl(s, cmd, buf, buflen)
	int s;
	int cmd;
	char *buf;
	int buflen;
{
	return (ioctl(s, cmd, buf));
}
#endif

/*
 * devfs_entry -- routine called when an network devfs entry is found
 *
 * This routine is called by devfs_find() when a matching devfs entry is found.
 * It is passed the name of the devfs entry.
 */
static void
devfs_entry(const char *devfsnm, const char *devfstype,
    dev_info_t *dip, struct ddi_minor_data *dmdp,
    struct ddi_minor_data *dmdap)
{
	struct net_interface *nip;
	struct ni_list *nilp, *tnilp;
	char *name;
	char *cname, *dev_ddm_name;
	char last_char;

	/*
	 * Look for network devices only
	 */
	if (strcmp(devfstype, class) != 0)
		return;

	if (debug > 2) {
		fprintf(stderr, "Examining %s (%s) (%d) \n", devfsnm, devfstype,
			dmdp->type);
	}
	    
	    
	/*
	 * If the ddi_minor_data name doesn't match the devinfo node name,
	 * use the ddi_minor_data name. This is needed for devices
	 * like HappyMeal that name devinfo names like
	 * SUNW,hme...:hme.
	 */
	name = (char *)(local_addr(DEVI(dip)->devi_name));
	if (dmdp->type == DDM_ALIAS &&
	    strcmp((char *)local_addr(dmdap->ddm_name), name)) {
		if (debug > 2)
			fprintf(stderr,
				"Alias name mismatch: %s - %s\n",
				(char *)local_addr(dmdap->ddm_name),
				name);
		name = (char *)local_addr(dmdap->ddm_name);
	}

	if (dmdp->type == DDM_MINOR) {
		dev_ddm_name = (char *)local_addr(dmdp->ddm_name);
		last_char = dev_ddm_name[strlen(dev_ddm_name)-1];
		if (debug > 2)
			fprintf(stderr,
				"Minor name/unit: %s/%d\n",
				dev_ddm_name, (int)last_char);
		if (last_char < '0' || last_char > '9') {
			return;
		}
	}

	if (debug > 2) {
		fprintf(stderr, "nets: devfs_entry: got %s %s\n",
		    devfsnm, devfstype);
		fprintf(stderr, "name %s intno %d\n", name,
			DEVI(dip)->devi_instance);
	}
	/* Now, let's add the node to the interface list */
	ni_new(nip);
	ni_set_type(nip, name);
	cname = malloc(strlen(ni_get_type(nip) + 3));
	if (dmdp->type == DDM_MINOR) {
		strcpy(cname, (const char *)local_addr(dmdp->ddm_name));
	} else {
		memset(cname, '\0', strlen(ni_get_type(nip) + 3));
		sprintf(cname, "%s%d", ni_get_type(nip), 
		    DEVI(dip)->devi_instance);
	}
	if (debug > 2) {
		fprintf(stderr, "Found %s (%s/%d)\n",
		    cname, name, DEVI(dip)->devi_instance);
	}
	ni_set_name(nip, cname);
	free(cname);

	nil_new(nilp);
	set_nifp(nilp, nip);
	if (nil_head == NIL_NULL)  {
		nil_head = nilp;
	} else {
		tnilp = first_if(nil_head);
		while (next_if(tnilp) != NIL_NULL)
			tnilp = next_if(tnilp);
		next_if(tnilp) = nilp;
	}

	return;
}
