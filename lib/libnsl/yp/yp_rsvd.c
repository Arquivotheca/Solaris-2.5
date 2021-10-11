#ident	"@(#)yp_rsvd.c	1.2	95/07/19 SMI"
/*
 * Copyright 1995 Sun Microsystems Inc.
 */
#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <netconfig.h>
#include <netdir.h>
#include <rpc/rpc.h>

CLIENT *
__yp_clnt_create_rsvdport(const char *hostname, ulong_t prog, ulong_t vers,
		     const char *nettype,
		     const uint_t sendsz, const uint_t recvsz)
{
    struct netconfig *nconf;
    struct netbuf *svcaddr;
    struct t_bind *tbind;
    CLIENT *clnt = NULL;
    int fd;
    const char *nt;

    if (nettype == NULL)
	nt = "udp";
    else
	nt = nettype;
	
    if (strcmp(nt, "udp") && strcmp(nt, "tcp"))
 	return(clnt_create(hostname, prog, vers, nt));

    if ((nconf = getnetconfigent((void *) nt)) == NULL)
	return NULL;

    if ((fd = t_open(nconf->nc_device, O_RDWR, NULL)) == -1) {
	freenetconfigent(nconf);
	return NULL;
    }

    /* Attempt to set reserved port, but we don't care if it fails */
    netdir_options(nconf, ND_SET_RESERVEDPORT, fd, NULL);

    if ((tbind = (struct t_bind *) t_alloc(fd, T_BIND, T_ADDR)) == NULL)
	return NULL;

    svcaddr = &(tbind->addr);

    if (!rpcb_getaddr(prog, vers, nconf, svcaddr, hostname)) {
	t_close(fd);
	t_free((char *) tbind, T_BIND);
	freenetconfigent(nconf);
	return NULL;
    }

    if ((clnt = clnt_tli_create(fd, nconf, svcaddr,
				prog, vers, sendsz, recvsz)) == NULL) {
	t_close(fd);
	t_free((char *) tbind, T_BIND);
    }
    else {
	t_free((char *) tbind, T_BIND);
	clnt_control(clnt, CLSET_FD_CLOSE, NULL);
    }
    freenetconfigent(nconf);
    return(clnt);
}
