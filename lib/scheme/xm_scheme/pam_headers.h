/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ifndef	_UNIX_HEADERS_H
#define	_UNIX_HEADERS_H

#ident  "@(#)pam_headers.h 1.15     94/11/01 SMI"

/*
******************************************************************

	PROPRIETARY NOTICE(Combined)

This source code is unpublished proprietary information
constituting, or derived under license from AT&T's UNIX(r) System V.
In addition, portions of such source code were derived from Berkeley
4.3 BSD under license from the Regents of the University of
California.



		Copyright Notice

Notice of copyright on this source code product does not indicate
publication.

	(c) 1986, 1987, 1988, 1989, 1990, 1991, 1992 Sun Microsystems, Inc
	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
		All rights reserved.
*******************************************************************
*/


/*
******************************************************************** *
*									*
*			Unix Scheme Header Files			*
*									*
* ******************************************************************** */

#include <sys/param.h>
#include <security/ia_appl.h>
#include <pwd.h>
#include <shadow.h>
#include <string.h>
#include <rpc/types.h>
#include <rpc/auth.h>
#include <locale.h>
#include <crypt.h>

/*
 * Various useful files and string constants
 */
#define	DIAL_FILE	"/etc/dialups"
#define	DPASS_FILE	"/etc/d_passwd"
#define	SHELL		"/usr/bin/sh"

/* define dial_pass() messages */
#define	DIALUP_PASSWD_MSG	"Dialup Password: "

/*
 * PAM textdomain constant
 */
#define	PAMTXD		"SUNW_OST_SYSOSPAM"

/*
 * Miscellaneous constants
 */
#define	SLEEPTIME	4
#define	ERROR		1
#define	OK		0
#define	MAXTRYS		5
#define	ROOTUID		0

/*
 * String manipulation macros: SCPYN, EQN and ENVSTRNCAT
 */
#define	SCPYN(a, b)	(void) strncpy(a, b, sizeof (a))

#endif	/* _UNIX_HEADERS_H */
