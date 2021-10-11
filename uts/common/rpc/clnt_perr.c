/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)clnt_perr.c	1.12	95/01/31 SMI"	/* SVr4.0 1.2	*/

/*
 *  		PROPRIETARY NOTICE (Combined)
 *
 *  This source code is unpublished proprietary information
 *  constituting, or derived under license from AT&T's Unix(r) System V.
 *  In addition, portions of such source code were derived from Berkeley
 *  4.3 BSD under license from the Regents of the University of
 *  California.
 *
 *
 *
 *  		Copyright Notice
 *
 *  Notice of copyright on this source code product does not indicate
 *  publication.
 *
 *  	(c) 1986, 1987, 1988, 1989  Sun Microsystems, Inc.
 *  	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *		All rights reserved.
 */

/*
 * clnt_perror.c
 */
#include <sys/types.h>
#include <sys/t_lock.h>
#include <rpc/types.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>

/*
 * Return an ascii string which matches the RPC clnt stat passed in.
 */
char *
clnt_sperrno(stat)
	enum clnt_stat stat;
{

	switch (stat) {
	case RPC_SUCCESS:
		return ("RPC: Success");
	case RPC_CANTENCODEARGS:
		return ("RPC: Can't encode arguments");
	case RPC_CANTDECODERES:
		return ("RPC: Can't decode result");
	case RPC_CANTSEND:
		return ("RPC: Unable to send");
	case RPC_CANTRECV:
		return ("RPC: Unable to receive");
	case RPC_TIMEDOUT:
		return ("RPC: Timed out");
	case RPC_INTR:
		return ("RPC: Interrupted");
	case RPC_UDERROR:
		return ("RPC: Unitdata error");
	case RPC_VERSMISMATCH:
		return ("RPC: Incompatible versions of RPC");
	case RPC_AUTHERROR:
		return ("RPC: Authentication error");
	case RPC_PROGUNAVAIL:
		return ("RPC: Program unavailable");
	case RPC_PROGVERSMISMATCH:
		return ("RPC: Program/version mismatch");
	case RPC_PROCUNAVAIL:
		return ("RPC: Procedure unavailable");
	case RPC_CANTDECODEARGS:
		return ("RPC: Server can't decode arguments");
	case RPC_SYSTEMERROR:
		return ("RPC: Remote system error");
	case RPC_UNKNOWNHOST:
		return ("RPC: Unknown host");
	case RPC_UNKNOWNPROTO:
		return ("RPC: Unknown protocol");
	case RPC_UNKNOWNADDR:
		return ("RPC: Remote address unknown");
	case RPC_NOBROADCAST:
		return ("RPC: Broadcasting not supported");
	case RPC_PMAPFAILURE:
		return ("RPC: Port mapper failure");
	case RPC_PROGNOTREGISTERED:
		return ("RPC: Program not registered");
	case RPC_N2AXLATEFAILURE:
		return ("RPC: Name to address translation failed");
	case RPC_TLIERROR:
		return ("RPC: TLI error");
	case RPC_FAILED:
		return ("RPC: Failed (unspecified error)");
	case RPC_INPROGRESS:
		return ("RPC: Operation in progress");
	case RPC_STALERACHANDLE:
		return ("RPC: Stale RAC handle");
	case RPC_CANTCONNECT:
		return ("RPC: Couldn't make connection");
	case RPC_XPRTFAILED:
		return ("RPC: Received disconnect from remote");
	case RPC_CANTCREATESTREAM:
		return ("RPC: Can't push RPC module");
	}
	return ("RPC: (unknown error code)");
}
