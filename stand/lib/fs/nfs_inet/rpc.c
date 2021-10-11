#ident	"@(#)rpc.c	1.25	94/08/09 SMI"

/*
 * Copyright (c) 1991-1994 by Sun Microsystems, Inc.
 *
 * This file contains a simple implementation of RPC. Standard XDR is
 * used.
 */

#include <sys/sysmacros.h>
#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/auth_sys.h>
#include <rpc/rpc_msg.h>
#include <sys/t_lock.h>
#include <rpc/clnt.h>
#include <sys/promif.h>
#include <local.h>
#include <nfs_prot.h>
#include <netaddr.h>
#include <sys/sainet.h>
#include <rpc/auth_unix.h>


/*
 * returns: RPC_SUCCESS for success, RPC_* for failure.
 */

static char rpc_xmit_buf[MAX_PKT_SIZE + sizeof (long)];	/* needs alignment */
static char rpc_rcv_buf[NFSBUF_SIZE + sizeof (long)];	/* needs alignment */
	/* This is related to the NFS read size. */

enum clnt_stat
rpc_call(
	u_long		prog,		/* rpc program number to call. */
	u_long		vers,		/* rpc program version */
	u_long		proc,		/* rpc procedure to call */
	xdrproc_t	in_xdr,		/* routine to serialize arguments */
	caddr_t		args,		/* arg vector for remote call */
	xdrproc_t	out_xdr,	/* routine to deserialize results */
	caddr_t		ret,		/* addr of buf to place results in */
	int		rexmit,		/* retransmission interval (secs) */
	int		wait_time,	/* how long (secs) to wait (resp) */
	struct sainet	*net,		/* network addresses */
	u_int		auth)		/* type of authentication wanted. */
{
	/* functions */
	extern void xdrmem_create();
	extern bool_t xdr_opaque_auth();
	extern enum clnt_stat xmit();	/* our network interface. */
	extern u_long pmap_getport();	/* get entry from port list */
	extern AUTH *authnone_create();
	extern AUTH *authunix_create();
	bool_t	rpc_hdr();
	void	rpc_disperr();

	/* variables */
	extern struct opaque_auth _null_auth;
	extern char bp_hostname[]; 	/* our hostname - from bootparams */
	XDR xmit_xdrs, rcv_xdrs;	/* xdr memory */
	AUTH *xmit_auth;		/* our chosen auth cookie */
	u_int fake_gids = 1;		/* fake gids list for auth_unix */
	caddr_t trm_msg, rcv_msg;	/* outgoing/incoming rpc mesgs */
	struct rpc_msg reply;		/* our reply msg header */
	u_int trm_len, rcv_len = 0;	/* mesg lengths */
	enum clnt_stat retval;		/* various return values */
	struct rpc_err rpc_error;	/* to store RPC errors in on rcv. */
	static u_long xid;		/* current xid */
	u_int xmit_len;			/* How much of the buffer we used */
	static u_short sport;		/* Chosn by xmit(), cache here */
	u_short dport;			/* destination port number */
	int nrefreshes = 2;		/* # of times to refresh cred */


	trm_len = MAX_PKT_SIZE;
	trm_msg = (caddr_t)LALIGN(&rpc_xmit_buf[0]);	/* alignment hack */
	rcv_msg = (caddr_t)LALIGN(&rpc_rcv_buf[0]);	/* alignment hack */
	xmit_auth = (AUTH *)0;
	bzero((caddr_t)&rpc_error, sizeof (struct rpc_err));

	/* initialize reply's rpc_msg struct, so we can decode later. */
	reply.acpted_rply.ar_verf = _null_auth;	/* struct copy */
	reply.acpted_rply.ar_results.where = ret;
	reply.acpted_rply.ar_results.proc = out_xdr;

	/* snag the udp port we need. */
	if ((dport = pmap_getport(prog, vers, &(rpc_error.re_status))) == 0)
		goto gt_error;

	/* generate xid - increment */
	if (xid == 0)
		xid = (u_long)(prom_gettime() / 1000);
	else
		xid++;

	/* set up outgoing pkt as xdr modified. */
	xdrmem_create(&xmit_xdrs, trm_msg, trm_len, XDR_ENCODE);

	/* setup rpc header */
	if (rpc_hdr(&xmit_xdrs, xid, prog, vers, proc) !=
	    TRUE) {
		printf("rpc_call: cannot setup rpc header.\n");
		rpc_error.re_status = RPC_FAILED;
		goto gt_error;
	}

	/* setup authentication */
	switch (auth) {
	case AUTH_NONE:
		xmit_auth = authnone_create();
		break;
	case AUTH_UNIX:
		/*
		 * Assumes we've done a BOOTPARAM_WHOAMI to initialize
		 * bp_hostname.
		 */
		if (bp_hostname[0] == '\0') {
			printf("rpc_call: Hostname unknown.\n");
			rpc_error.re_status = RPC_FAILED;
			goto gt_error;
		} else
			xmit_auth = authunix_create(&bp_hostname[0], 0, 1, 1,
			    &fake_gids);

		break;
	default:
		printf("rpc_call: Unsupported authentication type: %d\n",
		    auth);
		rpc_error.re_status = RPC_AUTHERROR;
		goto gt_error;
	/*NOTREACHED*/
	}

	/*
	 * rpc_hdr puts everything in the xmit buffer for the header
	 * EXCEPT the proc. Put it, and our authentication info into
	 * it now, serializing as we go. We will be at the place where
	 * we left off.
	 */
call_again:
	xmit_xdrs.x_op = XDR_ENCODE;
	if ((XDR_PUTLONG(&xmit_xdrs, (long *)&proc) == FALSE) ||
	    (AUTH_MARSHALL(xmit_auth, &xmit_xdrs, NULL) == FALSE) ||
	    ((*in_xdr)(&xmit_xdrs, args) == FALSE)) {
		rpc_error.re_status = RPC_CANTENCODEARGS;
		goto gt_error;
	} else
		xmit_len = (int)XDR_GETPOS(&xmit_xdrs); /* for xmit */

	/*
	 * Right now the outgoing packet should be all serialized and
	 * ready to go...
	 */

	/*
	 * send out the request. xmit() handles retries, timeouts. The
	 * first item in the receive buffer will be the xid. Check if it
	 * is correct.
	 */
	do {
		rpc_error.re_status = xmit(trm_msg, xmit_len, rcv_msg,
		    &rcv_len, &sport, dport, rexmit, wait_time, net);
		if (rpc_error.re_status != RPC_SUCCESS)
			goto gt_error;
		else {
			if (ntohl(*((u_long *)(rcv_msg))) != xid) {
				printf("rpc_call: xid wrong: got (0x%x)"
					" expected: (0x%x). Retrying...\n",
					*(u_long *)(rcv_msg), xid);
			}
		}
	} while (ntohl(*((u_long *)(rcv_msg))) != xid);

	/*
	 * Let's deserialize the data into our 'ret' buffer.
	 */
	xdrmem_create(&rcv_xdrs, rcv_msg, rcv_len, XDR_DECODE);
	if (xdr_replymsg(&rcv_xdrs, &reply) != FALSE) {
		_seterr_reply(&reply, &rpc_error);
		if (rpc_error.re_status == RPC_SUCCESS) {
			if (AUTH_VALIDATE(xmit_auth,
			    &reply.acpted_rply.ar_verf) == FALSE) {
				rpc_error.re_status = RPC_AUTHERROR;
				rpc_error.re_why = AUTH_INVALIDRESP;
			}
			if (reply.acpted_rply.ar_verf.oa_base != 0) {
				xmit_xdrs.x_op = XDR_FREE;
				(void) xdr_opaque_auth(&xmit_xdrs,
				    &(reply.acpted_rply.ar_verf));
			}
		} else {
			/*
			 * as clnt_udp.c says.. let's see if our
			 * credentials need refreshing
			 */
			if (nrefreshes > 0 && AUTH_REFRESH(xmit_auth, NULL)) {
				nrefreshes--;
				goto call_again; /* XXXX looks broke!!! */
			}
		}
	} else
		rpc_error.re_status = RPC_CANTDECODERES;

	/*
	 * reset source port to zero - each rpc_call stands on its own.
	 */
gt_error:
	sport = 0;
	if (xmit_auth != (AUTH *)0) {
		AUTH_DESTROY(xmit_auth);
	}
	if (rpc_error.re_status != RPC_SUCCESS)
		rpc_disperr(&rpc_error);
	return (rpc_error.re_status);
} /* end rpc_call */

/*
 * rpc_hdr: sets the fields in the rpc msg header.
 *
 * Returns: TRUE on success, FALSE if failure.
 */
bool_t
rpc_hdr(
	XDR *xdrs,
	u_long xid,
	u_long prog,
	u_long vers,
	u_long proc)
{
	/* variables */
	struct rpc_msg call_msg;

	/* setup header */
	call_msg.rm_xid = xid;
	call_msg.rm_direction = (u_long)CALL;
	call_msg.rm_call.cb_rpcvers = (u_long)RPC_MSG_VERSION;
	call_msg.rm_call.cb_prog = (u_long)prog;
	call_msg.rm_call.cb_vers = (u_long)vers;

	/* xdr the header. */
	if (xdr_callhdr(xdrs, &call_msg) == FALSE)
		return (FALSE);
	else
		return (TRUE);
}

/*
 * display error messages
 */
void
rpc_disperr(stat)
	struct rpc_err *stat;
{
	switch (stat->re_status) {
	case RPC_CANTENCODEARGS:
		printf("RPC: Can't encode arguments.\n");
		break;
	case RPC_CANTDECODERES:
		printf("RPC: Can't decode result.\n");
		break;
	case RPC_CANTSEND:
		printf("RPC: Unable to send.\n");
		break;
	case RPC_CANTRECV:
		printf("RPC: Unable to receive.\n");
		break;
	case RPC_TIMEDOUT:
		printf("RPC: Timed out.\n");
		break;
	case RPC_VERSMISMATCH:
		printf("RPC: Incompatible versions of RPC.\n");
		break;
	case RPC_AUTHERROR:
		printf("RPC: Authentication error:\n");
		switch (stat->re_why) {
		case AUTH_BADCRED:
			printf("remote: bogus credentials (seal broken).\n");
			break;
		case AUTH_REJECTEDCRED:
			printf("remote: client should begin new session.\n");
		case AUTH_BADVERF:
			printf("remote: bogus verifier (seal broken).\n");
			break;
		case AUTH_REJECTEDVERF:
			printf("remote: verifier expired or was replayed.\n");
			break;
		case AUTH_TOOWEAK:
			printf("remote: rejected due to security reasons.\n");
			break;
		case AUTH_INVALIDRESP:
			printf("local: bogus response verifier.\n");
			break;
		case AUTH_FAILED:
		default:
			printf("local: unknown error.\n");
			break;
		}
		break;
	case RPC_PROGUNAVAIL:
		printf("RPC: Program unavailable.\n");
		break;
	case RPC_PROGVERSMISMATCH:
		printf("RPC: Program/version mismatch.\n");
		break;
	case RPC_PROCUNAVAIL:
		printf("RPC: Procedure unavailable.\n");
		break;
	case RPC_CANTDECODEARGS:
		printf("RPC: Server can't decode arguments.\n");
		break;
	case RPC_SYSTEMERROR:
		printf("RPC: Remote system error.\n");
		break;
	case RPC_UNKNOWNHOST:
		printf("RPC: Unknown host.\n");
		break;
	case RPC_UNKNOWNPROTO:
		printf("RPC: Unknown protocol.\n");
		break;
	case RPC_PMAPFAILURE:
		printf("RPC: Port mapper failure.\n");
		break;
	case RPC_PROGNOTREGISTERED:
		printf("RPC: Program not registered.\n");
		break;
	case RPC_FAILED:
		printf("RPC: Failed (unspecified error).\n");
		break;
	default:
		printf("RPC: (unknown error code).\n");
		break;
	}
}
