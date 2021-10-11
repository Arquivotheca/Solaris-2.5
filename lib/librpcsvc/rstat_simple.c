#pragma ident	"@(#)rstat_simple.c	1.2	92/07/20 SMI" 

/*
 * Copyright (c) 1985 by Sun Microsystems, Inc.
 */

#include <rpc/rpc.h>
#include <rpcsvc/rstat.h>

rstat(host, statp)
	char *host;
	struct statstime *statp;
{
	return (rpc_call(host, RSTATPROG, RSTATVERS_TIME, RSTATPROC_STATS,
			xdr_void, (char *) NULL,
			xdr_statstime, (char *) statp, (char *) NULL));
}

havedisk(host)
	char *host;
{
	long have;

	if (rpc_call(host, RSTATPROG, RSTATVERS_TIME, RSTATPROC_HAVEDISK,
			xdr_void, (char *) NULL,
			xdr_long, (char *) &have, (char *) NULL) != 0)
		return (-1);
	else
		return (have);
}


