/*
 * Copyright (c) 1985 by Sun Microsystems, Inc.
 *
 */


#ifndef _RPCSVC_YPPASSWD_H
#define	_RPCSVC_YPPASSWD_H

#pragma ident	"@(#)yppasswd.h	1.3	94/09/09 SMI"

#ifndef _PWD_H
#include <pwd.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

#define	YPPASSWDPROG ((u_long)100009)
#define	YPPASSWDVERS ((u_long)1)
#define	YPPASSWDPROC_UPDATE ((u_long)1)

struct yppasswd {
	char *oldpass;		/* old (unencrypted) password */
	struct passwd newpw;	/* new pw structure */
};

int xdr_yppasswd();

#ifdef	__cplusplus
}
#endif

#endif	/* !_RPCSVC_YPPASSWD_H */
