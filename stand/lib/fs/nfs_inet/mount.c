#ident	"@(#)mount.c	1.20	94/08/09 SMI"

/*
 * Copyright (c) 1991-1994 by Sun Microsystems, Inc.
 *
 * nfs_mountroot(): mounts the root filesystem.
 */

#include <sys/utsname.h>
#include <sys/types.h>
#include <rpc.h>
#include <rpc/auth.h>
#include <sys/t_lock.h>
#include <rpc/clnt.h>
#include <rpc/xdr.h>
#include <rpc/rpc_msg.h>
#undef NFSSERVER
#include <nfs_prot.h>
#include <mount.h>
#include <local.h>
#include <sys/promif.h>

/* our root file handle */
struct nfs_file roothandle;

/*
 * This routine will open a device as it is known by the V2 OBP. It
 * then goes thru the stuff necessary to initialize the network device,
 * get our network address, bootparams, and finally actually go and
 * get the root filehandle. Sound like fun? Suuurrrree. Take a look.
 *
 * Returns 0 if things worked. -1 if we crashed and burned.
 */
int
boot_nfs_mountroot(char *str)
{
	/* functions */
	extern enum clnt_stat rpc_call();
	extern bool_t xdr_path();
	extern bool_t xdr_fhstatus();
	extern bool_t whoami();		/* our hostname */
	extern bool_t getfile();	/* bootparams */
	extern void	nfs_error();	/* NFS error messages */
	extern bool_t network_open(char *);	/* open network device */

	/* variables */
	enum clnt_stat status;		/* rpc call return status */
	char root_hostname[SYS_NMLN];	/* our root hostname */
	char server_path[SYS_NMLN];	/* the root's path */
	char *root_path = &server_path[0];	/* to make XDR happy */
	extern int network_up;			/* set if network is up */
	extern struct nfs_file roothandle;	/* our root's filehandle */
	struct fhstatus root_tmp;		/* to pass to rpc/xdr */
	register int rexmit;		/* retransmission interval */
	register int resp_wait;		/* how long to wait for a resp */

	if (network_up == 0) {
		if (network_open(str) == FALSE) {
			(void) printf("nfs_mountroot: cannot open network \
device.\n");
			return (-1);
		}
	}

	/* initialization */
	bzero((caddr_t)&root_hostname, SYS_NMLN);
	bzero((caddr_t)&server_path, SYS_NMLN);
	bzero((caddr_t)&root_tmp, sizeof (root_tmp));

	/* get our hostname */
	if (whoami() == FALSE)
		return (-1);

	/* get our bootparams. */
	if (getfile("root", (caddr_t)&root_hostname,
	    (caddr_t)&server_path) == FALSE)
		return (-1);

	/* mount root */
	printf("root server: %s\n", root_hostname);
	printf("root directory: %s\n", server_path);

	/*
	 * Wait up to 16 secs for first response, retransmitting expon.
	 */
	rexmit = 0;	/* default retransmission interval */
	resp_wait = 16;
	do {
		status = rpc_call((u_long)MOUNTPROG, (u_long)MOUNTVERS,
		    (u_long)MOUNTPROC_MNT, xdr_path, (caddr_t)&root_path,
		    xdr_fhstatus, (caddr_t)&(root_tmp), rexmit, resp_wait,
		    0, AUTH_UNIX);
		if (status == RPC_TIMEDOUT) {
			printf("boot: %s:%s mount server not responding.\n",
			    root_hostname, server_path);
		}
		rexmit = resp_wait;
		resp_wait = 0;	/* use default wait time. */
	} while (status == RPC_TIMEDOUT);

	if ((status == RPC_SUCCESS) && (root_tmp.fhs_status == 0)) {
		/*
		 * Since the mount succeeded, we'll mark the roothandle's
		 * status as NFS_OK, and its type as NFDIR. If these
		 * points aren't the case, then we wouldn't be here.
		 */
		bcopy((caddr_t)&(root_tmp.fhstatus_u.fhs_fhandle),
		    (caddr_t)&(roothandle.fh), FHSIZE);
		roothandle.status = NFS_OK;
		roothandle.type = NFDIR;
		roothandle.offset = (u_long)0;	/* it's a directory! */
		return (0);
	} else {
		nfs_error(root_tmp.fhs_status);
		return (-1);
	}
/*NOTREACHED*/
}

/*
 * xdr routines used by mount.
 */

xdr_fhstatus(XDR *xdrs, struct fhstatus *fhsp)
{
	if (!xdr_int(xdrs, (int *)&fhsp->fhs_status))
		return (FALSE);
	if (fhsp->fhs_status == 0) {
		if (!xdr_fhandle(xdrs, &(fhsp->fhstatus_u.fhs_fhandle)))
			return (FALSE);
	}
	return (TRUE);
}

xdr_fhandle(XDR *xdrs, nfs_fh *fhp)
{
	if (xdr_opaque(xdrs, (char *) fhp, NFS_FHSIZE)) {
		return (TRUE);
	}
	return (FALSE);
}



bool_t
xdr_path(XDR *xdrs, char **pathp)
{
	if (xdr_string(xdrs, pathp, 1024)) {
		return (TRUE);
	}
	return (FALSE);
}
