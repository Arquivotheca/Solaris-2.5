/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ident  "@(#)pam_headers.h 1.15     94/11/01 SMI"

#ifndef	_UNIX_HEADERS_H
#define	_UNIX_HEADERS_H


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
******************************************************************* */


/*
********************************************************************* *
*									*
*			Unix Scheme Header Files			*
*									*
* ******************************************************************** */

#include <sys/types.h>
#include <utmpx.h>
#include <lastlog.h>
#include <signal.h>
#include <pwd.h>
#include <shadow.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>	/* For logfile locking */
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/utsname.h>
#include <utime.h>
#include <termio.h>
#include <sys/stropts.h>
#include <time.h>
#include <sys/param.h>
#include <sys/fcntl.h>
#include <deflt.h>
#include <grp.h>
#include <rpc/rpc.h>
#include <rpc/key_prot.h>
#include <rpc/des_crypt.h>
#include <memory.h>
#include <wait.h>
#include <stdlib.h>
#include <ulimit.h>
#include <rpc/auth.h>
#include <ctype.h>	/* isalpha(c), isdigit(c), islower(c), toupper(c) */
#include <errno.h>
#include <crypt.h>
#include <locale.h>
#include <stdarg.h>
#include <security/ia_appl.h>
#include <syslog.h>

/*
 * String manipulation macros: SCPYN, EQN and ENVSTRNCAT
 */
#define	SCPYN(a, b)	(void) strncpy(a, b, sizeof (a))
#define	EQN(a, b)	(strncmp(a, b, sizeof (a)-1) == 0)
#define	ENVSTRNCAT(to, from) {int deflen; deflen = strlen(to); \
	(void) strncpy((to)+ deflen, (from), sizeof (to) - (1 + deflen)); }

/*
 * Other macros
 */
#define	NMAX	sizeof (utmp.ut_name)
#define	min(a, b)	(((a) < (b)) ? (a) : (b))


/*
 * Various useful files and string constants
 */
#define	DIAL_FILE	"/etc/dialups"
#define	DPASS_FILE	"/etc/d_passwd"
#define	SHELL		"/usr/bin/sh"
#define	SHELL2		"/sbin/sh"
#define	SUBLOGIN	"<!sublogin>"
#define	LASTLOG		"/var/adm/lastlog"
#define	PWADMIN		"/etc/default/passwd"
#define	LOGINADMIN	"/etc/default/login"
#define	PASSWD		"/etc/passwd"
#define	SHADOW		"/etc/shadow"


/*
 * PAM textdomain constant
 */
#define	PAMTXD		"SUNW_OST_SYSOSPAM"

/*
 * Returned status codes for sa_establish_key () utility function
 */

#define	SA_ESTKEY_SUCCESS	0
#define	SA_ESTKEY_NOCREDENTIALS	1
#define	SA_ESTKEY_BADPASSWD	2
#define	SA_ESTKEY_CANTSETKEY	3
#define	SA_ESTKEY_ALREADY	4


/*
 * Miscellaneous constants
 */
#define	ERROR		1
#define	OK		0
#define	ROOTUID		0
#define	SLEEPTIME	4
#define	MAXTRYS		5
#define	MINWEEKS	-1	/* minimum weeks before next password change */
#define	MAXWEEKS	-1	/* maximum weeks before password change */
#define	WARNWEEKS	-1	/* number weeks before password expires */
				/* to warn the user */
#define	MINLENGTH	6	/* minimum length for passwords */
#define	NUMCP		13	/* number of characters for valid password */

/* define dial_pass() messages */
#define	DIALUP_PASSWD_MSG	"Dialup Password: "


/*
 * variables declarations
 */
extern	int	errno;

/*
 * Function Declarations
 */
extern char		*defread(), *basename(), *strdup();
extern int		defopen();


#endif	/* _UNIX_HEADERS_H */
