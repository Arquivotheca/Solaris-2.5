#ident	"@(#)getrpcport.c	1.2	92/03/24 SMI"

/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
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
	memcpy((char *) &addr.sin_addr, hp->h_addr, hp->h_length);
	addr.sin_family = AF_INET;
	addr.sin_port =  0;
	return ((int) pmap_getport(&addr, prognum, versnum, proto));
}
