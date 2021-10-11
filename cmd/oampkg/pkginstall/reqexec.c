/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)reqexec.c	1.13	94/10/21 SMI"	/* SVr4.0 1.4.1.1 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>	/* creat() declaration */
#include <pwd.h>
#include <grp.h>
#include <locale.h>
#include <libintl.h>
#include <pkglib.h>
#include "install.h"
#include "libadm.h"
#include "libinst.h"
#include "pkginstall.h"

extern char	*respfile, tmpdir[];
extern int	nointeract;

extern void	quit(int exitval);

static int	do_exec(char *script, char *output, char *inport, char
		    *alt_user);
static char	path[PATH_MAX];
static int	fd;
static gid_t	instgid;
static uid_t	instuid;

/* from main.c */
extern int	non_abi_scripts;

#define	ERR_TMPRESP	"unable to create temporary response file"
#define	ERR_RMRESP	"unable to remove response file <%s>"
#define	ERR_ACCRESP	"unable to access response file <%s>"
#define	ERR_CRERESP	"unable to create response file <%s>"
#define	ERR_INTR	"Interactive request script supplied by package"
#define	ERR_BADUSER	"unable to find user %s or %s."

int
reqexec(char *script, char *output)
{
	if (access(script, 0) != 0)
		return (0);

	if (nointeract) {
		ptext(stderr, gettext(ERR_INTR));
		return (5);
	}

	if (output == NULL) {
		/* place output in temporary file */
		(void) sprintf(path, "%s/respXXXXXX", tmpdir);
		respfile = mktemp(path);
		if (respfile == NULL) {
			progerr(gettext(ERR_TMPRESP));
			return (99);
		}
		respfile = qstrdup(respfile);
	} else {
		respfile = output;
		if ((access(respfile, 0) == 0) && unlink(respfile)) {
			progerr(gettext(ERR_RMRESP), respfile);
			return (99);
		}
	}

	/*
	 * create a zero length response file which is only writable
	 * by the non-privileged installation user-id, but is readable
	 * by the world
	 */
	if ((fd = creat(respfile, 0644)) < 0) {
		progerr(gettext(ERR_CRERESP), respfile);
		return (99);
	}
	(void) close(fd);

	/*
	 * NOTE : For 2.7 uncomment the non_abi_scripts line and delete
	 * the one below it.
	 */
	return (do_exec(script, output, REQ_STDIN,
	/*    non_abi_scripts ? CHK_USER_NON : CHK_USER_ALT)); */
	    CHK_USER_NON));
}

int
chkexec(char *script, char *output)
{
	if (output == NULL) {
		/* place output in temporary file */
		(void) sprintf(path, "%s/respXXXXXX", tmpdir);
		respfile = mktemp(path);
		if (respfile == NULL) {
			progerr(gettext(ERR_TMPRESP));
			return (99);
		}
		respfile = qstrdup(respfile);

		/*
		 * create a zero length response file which is only writable
		 * by the non-priveledged installation user-id, but is readable
		 * by the world
		 */
		if ((fd = creat(respfile, 0644)) < 0) {
			progerr(gettext(ERR_CRERESP), respfile);
			return (99);
		}
		(void) close(fd);
	} else {
		respfile = output;
		if ((access(respfile, 0) != 0)) {
			progerr(gettext(ERR_ACCRESP), respfile);
			return (7);
		}
	}

	return (do_exec(script, output, CHK_STDIN, CHK_USER_ALT));
}

static int
do_exec(char *script, char *output, char *inport, char *alt_user)
{
	char		*uname;
	struct passwd	*pwp;
	struct group	*grp;

	gid_t instgid = (gid_t) 1; /* other */
	uid_t instuid;

	int	retcode = 0;

	if ((pwp = getpwnam(CHK_USER)) != (struct passwd *) NULL) {
		instuid = pwp->pw_uid;
		uname = CHK_USER;
	} else if ((pwp = getpwnam(alt_user)) != (struct passwd *) NULL) {
		instuid = pwp->pw_uid;
		uname = alt_user;
	} else {
		ptext(stderr, gettext(ERR_BADUSER), CHK_USER, CHK_USER_ALT);
		return (1);
	}

	if ((grp = getgrnam(CHK_GRP)) != (struct group *) NULL)
		instgid = grp->gr_gid;

	(void) chown(respfile, instuid, instgid);

	retcode = pkgexecl(inport, CHK_STDOUT, uname, CHK_GRP, SHELL,
	    script, respfile, NULL);

	return (retcode);
}
