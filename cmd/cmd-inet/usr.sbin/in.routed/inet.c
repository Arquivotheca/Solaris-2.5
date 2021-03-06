/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)inet.c	1.6	92/08/14 SMI"	/* SVr4.0 1.1	*/

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
 * Temporarily, copy these routines from the kernel,
 * as we need to know about subnets.
 */
#include "defs.h"

extern struct interface *ifnet;

/* 
 * inet_makeaddr(), inet_netof() and inet_lnaof() are in system libraries 
 * but these routines are different in that they know about the subnet masks
 * for all the interfaces.
 */

u_long
inet_netmask(addr)
	u_long addr;
{
	u_long mask;

	if (IN_CLASSA(addr))
		mask = IN_CLASSA_NET;
	else if (IN_CLASSB(addr))
		mask = IN_CLASSB_NET;
	else
		mask = IN_CLASSC_NET;
	return (mask);
}

static u_long
inet_subnetmask(addr)
	u_long addr;
{
	register struct interface *ifp;
	u_long mask;

	mask = inet_netmask(addr);

	for (ifp = ifnet; ifp; ifp = ifp->int_next) {
		if ((ifp->int_flags & IFF_UP) == 0)
			continue;
		if ((ifp->int_netmask & addr) == ifp->int_net) {
			mask = ifp->int_subnetmask;
			/*
			 * Prefer the netmask for non-pt-pt interfaces
			 * if there are multiple interfaces with matching
			 * network number.
			 */
			if (ifp->int_flags & IFF_POINTOPOINT)
				continue;
			break;
		}
	}
	return (mask);
}

/*
 * Formulate an Internet address from network + host.
 */
struct in_addr
inet_makeaddr(net, host)
	u_long net, host;
{
	register u_long mask;
	u_long addr;

	 /*
	  * Must also handle networks right-justified for old kernels!
	  */
	if (net < 128)
		net <<= IN_CLASSA_NSHIFT;
	else if (net < 65536)
		net <<= IN_CLASSB_NSHIFT;
	else if (net < 0xff0000)
		net <<= IN_CLASSC_NSHIFT;

	mask = ~inet_subnetmask(net);
	addr = net | (host & mask);
	addr = htonl(addr);
	return (*(struct in_addr *)&addr);
}

/*
 * Return the network number from an internet address.
 */
inet_netof(in)
	struct in_addr in;
{
	register u_long i = ntohl(in.s_addr);

	return (i & inet_subnetmask(i));
}

/*
 * Return the host portion of an internet address.
 */
inet_lnaof(in)
	struct in_addr in;
{
	register u_long i = ntohl(in.s_addr);

	return (i & ~inet_subnetmask(i));
}


/*
 * Return RTF_HOST if the address is
 * for an Internet host, RTF_SUBNET for a subnet,
 * 0 for a network.
 */
inet_rtflags(sin)
	struct sockaddr_in *sin;
{
	register u_long i = ntohl(sin->sin_addr.s_addr);
	register u_long netmask, subnetmask;
	int flags = 0;

	netmask = inet_netmask(i);
	subnetmask = inet_subnetmask(i);

	if (i & ~subnetmask)
		flags |= RTF_HOST;
	if (subnetmask != netmask)
		flags |= RTF_SUBNET;
	return(flags);
}

/*
 * Return true if a route to subnet of route rt should be sent to dst.
 * Send it only if dst is on the same logical network,
 * or the route is the "internal" route for the net.
 */
inet_sendsubnet(rt, dst)
	struct rt_entry *rt;
	struct sockaddr_in *dst;
{
	register u_long r =
	    ntohl(((struct sockaddr_in *)&rt->rt_dst)->sin_addr.s_addr);
	register u_long d = ntohl(dst->sin_addr.s_addr);

	if (IN_CLASSA(r)) {
		if ((r & IN_CLASSA_NET) == (d & IN_CLASSA_NET)) {
			if ((r & IN_CLASSA_HOST) == 0)
				return ((rt->rt_state & RTS_INTERNAL) == 0);
			return (1);
		}
		if (r & IN_CLASSA_HOST)
			return (0);
		return ((rt->rt_state & RTS_INTERNAL) != 0);
	} else if (IN_CLASSB(r)) {
		if ((r & IN_CLASSB_NET) == (d & IN_CLASSB_NET)) {
			if ((r & IN_CLASSB_HOST) == 0)
				return ((rt->rt_state & RTS_INTERNAL) == 0);
			return (1);
		}
		if (r & IN_CLASSB_HOST)
			return (0);
		return ((rt->rt_state & RTS_INTERNAL) != 0);
	} else {
		if ((r & IN_CLASSC_NET) == (d & IN_CLASSC_NET)) {
			if ((r & IN_CLASSC_HOST) == 0)
				return ((rt->rt_state & RTS_INTERNAL) == 0);
			return (1);
		}
		if (r & IN_CLASSC_HOST)
			return (0);
		return ((rt->rt_state & RTS_INTERNAL) != 0);
	}
}
