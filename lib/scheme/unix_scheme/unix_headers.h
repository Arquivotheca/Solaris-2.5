/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ident  "@(#)unix_headers.h 1.13     94/06/09 SMI"

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
#include <nss_dbdefs.h>
#include <nsswitch.h>
#include <synch.h>

#include <rpcsvc/ypclnt.h>
#include <sys/file.h>
#include <errno.h>
#include <netdir.h>
#include <netconfig.h>
#include <rpcsvc/yppasswd.h>

#include <rpcsvc/nis.h>

/* new passwd update protocol defines */
#include <rpcsvc/nispasswd.h>

#define	PKMAP   "publickey.byname"

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
 * nis+ definition
 */
#define	PASSTABLE	"passwd.org_dir"
#define	PASSTABLELEN	14
#define	PKTABLE		"cred.org_dir"
#define	PKTABLELEN	12


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
#define	PAM_TRY_AGAIN	1
#define	MAX_INSIST	3	/* 3 chances to enter new passwd */

/* define dial_pass() messages */
#define	DIALUP_PASSWD_MSG	"Dialup Password: "

/* define error messages */
#define	MSG_NP  "Permission denied"
#define	MSG_FE  "Unexpected failure. Password file/table unchanged."
#define	MSG_FF  "Unexpected failure. Password file/table missing."
#define	MSG_FB  "Password file(s)/table busy. Try again later."
#define	NULLSTRING	""

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
 * variables declarations
 */
extern	int	optind;
extern	char	*optarg;
extern	struct	passwd	*unix_pwd;
extern	struct	spwd	*unix_sp;
extern	struct	passwd	*nis_pwd;
extern	struct	spwd	*nis_sp;
extern	struct	passwd	*nisplus_pwd;
extern	struct	spwd	*nisplus_sp;
extern	struct	nis_result	*passwd_res;
extern	struct	nis_result	*cred_res;
extern	char	curcryptsecret[];
extern	int	privileged;
extern	mutex_t	_priv_lock;

/*
 * Function Declarations
 */
extern char		*defread(), *basename(), *strdup();
extern int		defopen();
extern int		getsecretkey();
extern int		key_setnet();
extern int 		ruserok();
extern char		*crypt();
extern char		*strrchr(), *strchr(), *strcat();
extern char		*attr_match();
extern int		display_errmsg();
extern int		get_authtok();
extern char		*sa_get_passwd();
extern struct passwd	*getpwnam_from();
extern struct spwd	*getspnam_from();
extern void		endspent();
extern void		endpwent();
extern void		populate_age();
extern char		*getloginshell();
extern char		*getfingerinfo();
extern char		*gethomedir();
extern bool_t		npd_makeclnthandle();
extern bool_t		__get_cmnkey();
extern void		__gen_dhkeys();
extern nispasswd_status nispasswd_auth();
extern int		nispasswd_pass();
extern void		__npd_free_errlist();
extern int		xdecrypt();
extern int		xencrypt();
extern int		attr_find();
extern int		getpublickey();
extern int		yp_update();
extern int		sa_getall();
extern int		get_ns();
extern int		ck_perm();
extern int		update_authtok_file();
extern int		update_authtok_nis();
extern int		update_authtok_nisplus();
extern void		free_resp();
extern void		setup_getattr();
extern void		setup_setattr();
extern void		free_setattr();
extern struct spwd 	*_np_getspent();
extern void		_np_setspent(), _np_endspent();
extern bool_t		__nis_isadmin();
extern void		setusershell();


#endif	/* _UNIX_HEADERS_H */
