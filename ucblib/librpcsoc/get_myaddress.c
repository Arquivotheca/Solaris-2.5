#pragma ident	"@(#)get_myaddress.c	1.3	94/03/08 SMI" 

/*
 * get_myaddress.c
 *
 * Get client's IP address via ioctl.  This avoids using the NIS.
 * Copyright (C) 1990, Sun Microsystems, Inc.
 */

#include <rpc/types.h>
#include <rpc/pmap_prot.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <stdio.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/syslog.h>
#include <errno.h>

/*
 * don't use gethostbyname, which would invoke NIS
 */
get_myaddress(addr)
	struct sockaddr_in *addr;
{
	int s;
	char buf[BUFSIZ*8];
	struct ifconf ifc;
	struct ifreq ifreq, *ifr;
	int len;
	int ret;

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
	    (void) syslog(LOG_ERR, "get_myaddress: socket: %m");
	    exit(1);
	}
	ifc.ifc_len = sizeof (buf);
	ifc.ifc_buf = buf;
	do {
		ret = ioctl(s, SIOCGIFCONF, (char *)&ifc);
	} while (ret < 0 && (errno == EINTR || errno == EAGAIN));
	if (ret < 0) {
		(void) syslog(LOG_ERR,
		    "get_myaddress: ioctl (get interface configuration): %m");
		exit(1);
	}
	ifr = ifc.ifc_req;
	for (len = ifc.ifc_len; len; len -= sizeof (ifreq)) {
		ifreq = *ifr;
		do {
			ret = ioctl(s, SIOCGIFFLAGS, (char *)&ifreq);
		} while (ret < 0 && (errno == EINTR || errno == EAGAIN));
		if (ret < 0) {
			(void) syslog(LOG_ERR, "get_myaddress: ioctl: %m");
			exit(1);
		}
		if ((ifreq.ifr_flags & IFF_UP) &&
		    ifr->ifr_addr.sa_family == AF_INET) {
			*addr = *((struct sockaddr_in *)&ifr->ifr_addr);
			addr->sin_port = htons(PMAPPORT);
			break;
		}
		ifr++;
	}
	(void) close(s);
}
