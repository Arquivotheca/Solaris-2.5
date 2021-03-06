/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T */
/*	All Rights Reserved   */

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/
/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ident	"@(#)quit.c	1.15	95/01/18 SMI"	/* SVr4.0 1.11.2.3 */

/*  5-20-92	newroot function added */

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/utsname.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <pkgdev.h>
#include <pkglocs.h>
#include <locale.h>
#include <libintl.h>
#include "install.h"
#include <pkglib.h>
#include "libadm.h"
#include "libinst.h"

#define	MSG_NOCHANGE	"No changes were made to the system."

#define	WRN_NOMAIL	"WARNING: unable to send e-mail notification"
#define	WRN_FLMAIL	"WARNING: e-mail notification may have failed"

#define	ERR_PKGBINREN	"unable to rename <%s>\n\tto <%s>"

/* lockinst.c */
extern void	unlockinst(void);

extern struct	admin adm;
extern struct	pkgdev pkgdev;

extern int	dparts;
extern int	started;
extern int	update;
extern int	iflag;
extern int	failflag;
extern int	warnflag;
extern int	reboot;
extern int	ireboot;

extern char	tmpdir[];
extern char	pkgloc[];
extern char	pkgloc_sav[];
extern char	*msgtext;
extern char	*pkginst;
extern char	*pkgname;

extern void	trap(int signo), quit(int exitval);
static void	mailmsg(int retcode), quitmsg(int retcode);

void
trap(int signo)
{
	if ((signo == SIGINT) || (signo == SIGHUP))
		quit(3);
	else
		quit(1);
}

void
quit(int retcode)
{
	(void) signal(SIGINT, SIG_IGN);

	if (retcode != 99) {
		if ((retcode % 10) == 0) {
			if (failflag)
				retcode += 1;
			else if (warnflag)
				retcode += 2;
		}

		if (ireboot)
			retcode = (retcode % 10) + 20;
		if (reboot)
			retcode = (retcode % 10) + 10;
	}

	/*
	 * In the event that this quit() was called prior to completion of
	 * the task, do an unlockinst() just in case.
	 */
	unlockinst();

	/* fix bug #1082589 that deletes root file */
	if (tmpdir[0] != NULL)
		(void) rrmdir(tmpdir);

	/* send mail to appropriate user list */
	mailmsg(retcode);

	/* display message about this installation */
	quitmsg(retcode);

	/*
	 * No need to umount device since calling process
	 * was responsible for original mount
	 */

	if (!update) {
		if (!started && pkgloc[0]) {
			(void) chdir("/");
			/* fix bug #1082589 that deletes root file */
			if (pkgloc[0])
				(void) rrmdir(pkgloc);
		}
	} else {
		if (!started) {
			/*
			 * If we haven't started, but have already done
			 * the <PKGINST>/install directory rename, then
			 * remove the new <PKGINST>/install directory
			 * and rename <PKGINST>/install.save back to
			 * <PKGINST>/install.
			 */
			if (pkgloc_sav[0] && !access(pkgloc_sav, F_OK)) {
				if (pkgloc[0] && !access(pkgloc, F_OK))
					(void) rrmdir(pkgloc);
				if (rename(pkgloc_sav, pkgloc) == -1) {
					progerr(gettext(ERR_PKGBINREN),
						pkgloc_sav, pkgloc);
				}
			}
		} else {
			if (pkgloc_sav[0] && !access(pkgloc_sav, F_OK))
				(void) rrmdir(pkgloc_sav);
		}
	}

	if (dparts > 0)
		ds_skiptoend(pkgdev.cdevice);
	(void) ds_close(1);
	exit(retcode);
}

static void
quitmsg(int retcode)
{
	(void) putc('\n', stderr);
	if (iflag)
		ptext(stderr, qreason(0, retcode, started));
	else if (pkginst)
		ptext(stderr, qreason(1, retcode, started), pkginst);

	if (retcode && !started)
		ptext(stderr, gettext(MSG_NOCHANGE));
}

static void
mailmsg(int retcode)
{
	struct utsname utsbuf;
	FILE	*pp;
	char	*cmd;

	if (!started || iflag || (adm.mail == NULL))
		return;

	cmd = calloc(strlen(adm.mail) + sizeof (MAILCMD) + 2, sizeof (char));
	if (cmd == NULL) {
		logerr(gettext(WRN_NOMAIL));
		return;
	}

	(void) sprintf(cmd, "%s %s", MAILCMD, adm.mail);
	if ((pp = popen(cmd, "w")) == NULL) {
		logerr(gettext(WRN_NOMAIL));
		return;
	}

	if (msgtext)
		ptext(pp, msgtext);

	(void) strcpy(utsbuf.nodename, gettext("(unknown)"));
	(void) uname(&utsbuf);
	ptext(pp, qreason(2, retcode, started), pkgname, utsbuf.nodename,
	    pkginst);

	if (pclose(pp))
		logerr(gettext(WRN_FLMAIL));
}
