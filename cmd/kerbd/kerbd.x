/* #ident        "@(#)kerbd.x 1.2	91/07/11 SMI" */

%/*
% *  RPC protocol information for kerbd, the usermode daemon which
% *  assists the kernel when handling kerberos ticket generation and
% *  validation.  It is kerbd which actually communicates with the
% *  kerberos KDC.
% *
% *  File generated from kerbd.x 1.2 91/07/11 SMI
% *
% *  Copyright 1990,1991 Sun Microsystems, Inc.
% */

#ifdef RPC_HDR
%#include <kerberos/krb.h>
#endif RPC_HDR

typedef opaque	TICKET<MAX_KTXT_LEN>;
typedef string	KNAME<ANAME_SZ>;
typedef string	KINST<INST_SZ>;
typedef string	KREALM<REALM_SZ>;

struct ksetkcred_arg {
	u_long		cksum;			/* checksum to be sealed */
	KNAME		sname;			/* server name */
	KINST		sinst;			/* server instance */
	KREALM		srealm;			/* server realm */
};

struct ksetkcred_resd {
	TICKET		ticket;			/* ticket for server */
	des_block	key;			/* session key */
};

/*  status is a Kerberos error number */
union ksetkcred_res switch (int status) {
    case KSUCCESS:
	ksetkcred_resd	res;
    default:
	void;
};

struct kgetkcred_arg {
	TICKET		ticket;			/* ticket received */
	KNAME		sname;			/* server name */
	KINST		sinst;			/* server instance */
	u_long		faddr;			/* client IP address */
};

struct kgetkcred_resd {
	/* start of data from AUTH_DAT */
	KINST		sinst;			/* updated server instance */
	u_int		k_flags;		/* Flags from ticket */
	KNAME		pname;			/* Principal's name */
	KINST		pinst;			/* His Instance */
	KREALM		prealm;			/* His Realm */
	u_long		checksum;		/* Data checksum (opt) */
	des_block	session;		/* Session Key */
	int		life;			/* Life of ticket */
	u_long		time_sec;		/* Time ticket issued */
	u_long		address;		/* Address in ticket */
	TICKET		reply;			/* Auth reply (opt) */
	/* end of data from AUTH_DAT */
};

/*  status is a Kerberos error number */
union kgetkcred_res switch (int status) {
    case KSUCCESS:
	kgetkcred_resd	res;
    default:
	void;
};

struct kgetucred_arg {
	KNAME		pname;
};

const KUCRED_MAXGRPS = 32;
struct kerb_ucred {
	u_int	uid;
	u_int	gid;
	u_int	grplist<KUCRED_MAXGRPS>;	
};

enum ucred_stat { UCRED_OK, UCRED_UNKNOWN };

union kgetucred_res switch (enum ucred_stat status) {
    case UCRED_OK:
	kerb_ucred	cred;
    case UCRED_UNKNOWN:
	void;
};

/*
 *  The server accepts requests only from the loopback address.
 *  Unix authentication is used, and the port must be in the reserved range.
 */
program KERBPROG {
    version KERBVERS {

	/*
	 *  Called by the client to generate the ticket and session key.
	 */
	kgetkcred_res	KGETKCRED(kgetkcred_arg)		= 1;

	/*
	 *  Called by the server to get verify ticket and get the client's
	 *  credentials.
	 */
	ksetkcred_res	KSETKCRED(ksetkcred_arg)		= 2;

	/*
	 *  Called by the server to get the UNIX credentials which match
	 *  the supplied kerberos credentials for the client.
	 */
	kgetucred_res	KGETUCRED(kgetucred_arg)		= 3;
    } = 4;
} = 100078;
