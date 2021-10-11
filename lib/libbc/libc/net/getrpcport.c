#pragma ident	"@(#)getrpcport.c	1.6	92/07/20 SMI" 

/*
 * Copyright (c) 1985 by Sun Microsystems, Inc.
 */

#include <stdio.h>
#include <rpc/rpc.h>
#include <netdb.h>
#include <sys/socket.h>

getrpcport(host, prognum, versnum, proto)
	char *host;
{
	struct sockaddr_in addr;
	struct hostent *hp;

	if ((hp = gethostbyname(host)) == NULL)
		return (0);
	bcopy(hp->h_addr, (char *) &addr.sin_addr, hp->h_length);
	addr.sin_family = AF_INET;
	addr.sin_port =  0;
	return (pmap_getport(&addr, prognum, versnum, proto));
}
