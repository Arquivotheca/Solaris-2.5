/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ident  "@(#)pam_headers.h 1.15     94/09/16 SMI"

#ifndef	_UNIX_HEADERS_H
#define	_UNIX_HEADERS_H


/*
 * *******************************************************************

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
 * ******************************************************************** *
 *									*
 *			Unix Scheme Header Files			*
 *									*
 * ******************************************************************** */

#include <sys/types.h>
#include <security/ia_appl.h>
#include <shadow.h>
#include <nss_dbdefs.h>
#include <signal.h>
#include <rpc/key_prot.h>
#include <rpc/rpc.h>
#include <rpcsvc/nis.h>
#include <libintl.h>
#include <locale.h>
#include <crypt.h>
#include <deflt.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>	/* For logfile locking */
#include <nsswitch.h>
#include <rpcsvc/yppasswd.h>
#include <rpcsvc/ypclnt.h>
#include <syslog.h>
#include <rpcsvc/nispasswd.h>	/* new passwd update protocol defines */
#include <ctype.h>	/* isalpha(c), isdigit(c), islower(c), toupper(c) */
#include <stdio.h>
#include <errno.h>
#include <memory.h>
#include <time.h>
#include <pwd.h>
#include <mp.h>
#include <synch.h>
#include <wait.h>

/*
 * Debugging support
 */
#ifdef DEBUG
#define	dprintf printf
#define	dprintf1 printf
#define	dprintf2 printf
#define	dprintf3 printf
#else /* DEBUG */
#define	dprintf(w)
#define	dprintf1(w, x)
#define	dprintf2(w, x, y)
#define	dprintf3(w, x, y, z)
#endif

/*
 * Miscellaneous constants
 */
#define	NUMCP		13	/* number of characters for valid password */
#define	PAM_TRY_AGAIN	1
#define	MAX_INSIST	3	/* 3 chances to enter new passwd */
#define	MINWEEKS	-1	/* minimum weeks before next password change */
#define	MAXWEEKS	-1	/* maximum weeks before password change */
#define	WARNWEEKS	-1	/* number weeks before password expires */
				/* to warn the user */
#define	MINLENGTH	6	/* minimum length for passwords */
#define	ROOTUID		0

/*
 * variables declarations
 */
extern	struct	spwd	*unix_sp;
extern	struct	spwd	*nis_sp;
extern	struct	spwd	*nisplus_sp;
extern	struct	passwd	*nis_pwd;
extern	struct	passwd	*nisplus_pwd;
extern	struct	passwd	*unix_pwd;
extern	mutex_t	_priv_lock;
extern	int	privileged;

/* define error messages */
#define	MSG_FB  "Password file(s)/table busy. Try again later."
#define	MSG_FE  "Unexpected failure. Password file/table unchanged."
#define	MSG_FF  "Unexpected failure. Password file/table missing."
#define	MSG_NP  "Permission denied"
#define	NULLSTRING	""

/*
 * nis+ definition
 */
#define	PKTABLE		"cred.org_dir"
#define	PKTABLELEN	12
#define	PASSTABLE	"passwd.org_dir"
#define	PASSTABLELEN	14

#define	PKMAP   "publickey.byname"
/*
 * PAM textdomain constant
 */
#define	PAMTXD		"SUNW_OST_SYSOSPAM"

/*
 * Various useful files and string constants
 */
#define	PWADMIN		"/etc/default/passwd"

/*
 * Function Declarations
 */
extern struct spwd 	*_np_getspent();
extern void		_np_setspent(), _np_endspent();
extern char		*attr_match();
extern char		*getloginshell();
extern char		*gethomedir();
extern char		*getfingerinfo();
extern struct passwd	*getpwnam_from();
extern struct spwd	*getspnam_from();

#endif	/* _UNIX_HEADERS_H */
