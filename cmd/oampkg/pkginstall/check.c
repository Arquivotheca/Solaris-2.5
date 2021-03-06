/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/
/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ident	"@(#)check.c	1.21	95/04/06 SMI"	/* SVr4.0 1.13.1.1 */

/*  5-20-92	added newroot functions */

#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <wait.h>
#include <stdlib.h>
#include <sys/stat.h>	/* mkdir declaration is here? */
#include <unistd.h>
#include <errno.h>
#ifndef SUNOS41
#include <utmp.h>
#endif
#include <dirent.h>
#include <sys/types.h>
#include <locale.h>
#include <libintl.h>
#include <pkgstrct.h>
#include <pkglocs.h>
#include "install.h"
#include <pkglib.h>
#include "libadm.h"
#include "libinst.h"

extern struct admin adm;
extern struct cfextra **extlist;
extern struct mergstat *mstat;
extern int	ckquit, nointeract, nocnflct, nosetuid, rprcflag;
extern char	ilockfile[], rlockfile[], instdir[], savlog[],
		tmpdir[], pkgloc[], pkgloc_sav[], pkgbin[], pkgsav[],
		errbuf[], *pkginst, *msgtext;

extern int	dockspace(char *spacefile);
extern void	quit(int exitval);
static char	ask_cont[100];

#define	DISPSIZ	20	/* number of items to display on one page */

#define	MSG_RUNLEVEL	"\\nThe current run-level of this machine is <%s>, " \
			"which is not a run-level suggested for installation " \
			"of this package.  Suggested run-levels (in order of " \
			"preference) include:"
#define	HLP_RUNLEVEL	"If this package is not installed in a run-level " \
			"which has been suggested, it is possible that the " \
			"package may not install or operate properly.  If " \
			"you wish to follow the run-level suggestions, " \
			"answer 'n' to stop installation of the package."
#define	MSG_STATECHG	"\\nTo change states, execute\\n\\tshutdown -y " \
			"-i%s -g0\\nafter exiting the installation process. " \
			"Please note that after changing states you " \
			"may have to mount appropriate filesystem(s) " \
			"in order to install this package."

#define	ASK_CONFLICT	"Do you want to install these conflicting files"
#define	MSG_CONFLICT	"\\nThe following files are already installed on the " \
			"system and are being used by another package:"
#define	MSG_ROGUE	"\\n* - conflict with a file which does not " \
			"belong to any package."
#define	HLP_CONFLICT	"If you choose to install conflicting files, the " \
			"files listed above will be overwritten and/or have " \
			"their access permissions changed.  If you choose " \
			"not to install these files, installation will " \
			"proceed but these specific files will not be " \
			"installed.  Note that sane operation of the " \
			"software being installed may require these files " \
			"be installed; thus choosing to not to do so may " \
			"cause inapropriate operation.  If you wish to stop " \
			"installation of this package, enter 'q' to quit."

#define	ASK_SETUID	"Do you want to install these as setuid/setgid files"
#define	MSG_SETUID	"\\nThe following files are being installed with " \
			"setuid and/or setgid permissions:"
#define	MSG_OVERWR	"\\n* - overwriting a file which is also " \
			"setuid/setgid."
#define	HLP_SETUID	"The package being installed appears to contain " \
			"processes which will have their effective user or " \
			"group ids set upon execution.  History has shown " \
			"that these types of processes can be a source of " \
			"security problems on your system.  If you choose " \
			"not to install these as setuid files, installation " \
			"will proceed but these specific files will be " \
			"installed as regular files with setuid and/or " \
			"setgid permissions reset.  Note that sane " \
			"operation of the software being installed may " \
			"require that these files be installed with setuid " \
			"or setgid permissions as delivered; thus choosing " \
			"to install them as regular files may cause " \
			"inapropriate operation.  If you wish to stop " \
			"installation of this package, enter 'q' to quit."
#define	MSG_PARTINST	"\\nThe installation of this package was previously " \
			"terminated and installation was never successfully " \
			"completed."
#define	MSG_PARTREM	"\\nThe removal of this package was terminated at " \
			"some point in time, and package removal was only " \
			"partially completed."
#define	HLP_PARTIAL	"Installation of partially installed packages is " \
			"normally allowable, but some packages providers " \
			"may suggest that a partially installed package be " \
			"completely removed before re-attempting " \
			"installation.  Check the documentation provided " \
			"with this package, and then answer 'y' if you feel " \
			"it is advisable to continue the installation process."

#define	HLP_SPACE	"It appears that there is not enough free space on " \
			"your system in which to install this package.  It " \
			"is possible that one or more filesystems are not " \
			"properly mounted.  Neither installation of the " \
			"package nor its operation can be guaranteed under " \
			"these conditions.  If you choose to disregard this " \
			"warning, enter 'y' to continue the installation " \
			"process."
#define	HLP_DEPEND	"The package being installed has indicated a " \
			"dependency on the existence (or non-existence) " \
			"of another software package.  If this dependency is " \
			"not met before continuing, the package may not " \
			"install or operate properly.  If you wish to " \
			"disregard this dependency, answer 'y' to continue " \
			"the installation process."

#define	MSG_PRIV	"\\nThis package contains scripts which will be " \
			"executed with super-user permission during the " \
			"process of installing this package."
#define	HLP_PRIV	"During the installation of this package, certain " \
			"scripts provided with the package will execute with " \
			"super-user permission.  These scripts may modify or " \
			"otherwise change your system without your " \
			"knowledge.  If you are certain of the origin and " \
			"trustworthiness of the package being installed, " \
			"answer 'y' to continue the installation process."

#define	ASK_CONT	"Do you want to continue with the installation of <%s>"
#define	HLP_CONT	"If you choose 'y', installation of this package " \
			"will continue.  If you want to stop installation " \
			"of this package, choose 'n'."

#define	MSG_MKPKGDIR	"unable to make packaging directory <%s>"
#define	MSG_XFRSAVDIR	"unable to transfer prior contents to save " \
			"directory <%s>"
#define	MSG_XFRINSTDIR	"unable to transfer prior contents to install " \
			"directory <%s/install>"

#define	MSG_CKCONFL	"## Checking for conflicts with packages already " \
			"installed."
#define	MSG_CKDEPEND	"## Verifying package dependencies."
#define	MSG_CKSPACE	"## Verifying disk space requirements."
#define	MSG_CKUID	"## Checking for setuid/setgid programs."

#define	MSG_SCRFND	"Package scripts were found."
#define	MSG_UIDFND	"Setuid/setgid processes detected."
#define	MSG_ATTRONLY	"!%s %s <attribute change only>"

#define	MSG_CONTDISP	"[Hit <RETURN> to continue display]"

#define	ERR_NO_RUNST	"unable to determine current run-state"
#define	ERR_DEPFAILED	"Dependency checking failed."
#define	ERR_SPCFAILED	"Space checking failed."
#define	ERR_CNFFAILED	"Conflict checking failed."
#define	ERR_BADFILE	"packaging file <%s> is corrupt"

void
ckpartial(void)
{
	char	ans[MAX_INPUT];
	int	n;

	if (ADM(partial, "nocheck"))
		return;

	if (access(ilockfile, 0) == 0) {
		sprintf(ask_cont, gettext(ASK_CONT), pkginst);

		msgtext = gettext(MSG_PARTINST);
		ptext(stderr, msgtext);
		if (ADM(partial, "quit"))
			quit(4);
		if (nointeract)
			quit(5);
		msgtext = NULL;

		ckquit = 0;
		if (n = ckyorn(ans, NULL, NULL, gettext(HLP_PARTIAL),
		    ask_cont))
			quit(n);
		if (ans[0] != 'y')
			quit(3);
		ckquit = 1;
	}

	if (access(rlockfile, 0) == 0) {
		sprintf(ask_cont, gettext(ASK_CONT), pkginst);

		msgtext = gettext(MSG_PARTREM);
		ptext(stderr, msgtext);
		if (ADM(partial, "quit"))
			quit(4);
		if (nointeract)
			quit(5);
		msgtext = NULL;

		ckquit = 0;
		if (n = ckyorn(ans, NULL, NULL, gettext(HLP_PARTIAL),
		    ask_cont))
			quit(n);
		if (ans[0] != 'y')
			quit(3);
		ckquit = 1;
	}
}

#ifndef SUNOS41
void
ckrunlevel(void)
{
	struct utmp utmp;
	struct utmp *putmp;
	char	ans[MAX_INPUT], *pt, *istates, *pstate;
	int	n;
	char	*uxstate;

	if (ADM(runlevel, "nocheck"))
		return;
	pt = getenv("ISTATES");
	if (pt == NULL)
		return;

	utmp.ut_type = RUN_LVL;
	putmp = getutid(&utmp);
	if (putmp == NULL) {
		progerr(gettext(ERR_NO_RUNST));
		quit(99);
	}

	sprintf(ask_cont, gettext(ASK_CONT), pkginst);

	uxstate = strtok(&putmp->ut_line[10], " \t\n");

	istates = qstrdup(pt);
	if ((pt = strtok(pt, " \t\n, ")) == NULL)
		return; /* no list is no list */
	pstate = pt;
	do {
		if (strcmp(pt, uxstate) == 0) {
			free(istates);
			return;
		}
	} while (pt = strtok(NULL, " \t\n, "));

	msgtext = gettext(MSG_RUNLEVEL);
	ptext(stderr, msgtext, uxstate);
	pt = strtok(istates, " \t\n, ");
	do {
		ptext(stderr, "\\t%s", pt);
	} while (pt = strtok(NULL, " \t\n, "));
	free(istates);
	if (ADM(runlevel, "quit"))
		quit(4);
	if (nointeract)
		quit(5);
	msgtext = NULL;

	ckquit = 0;
	if (n = ckyorn(ans, NULL, NULL, gettext(HLP_RUNLEVEL),
	    ask_cont))
		quit(n);
	ckquit = 1;

	if (ans[0] == 'n') {
		ptext(stderr, gettext(MSG_STATECHG), pstate);
		quit(3);
	} else if (ans[0] != 'y')
		quit(3);
}
#endif

void
ckdepend(void)
{
	int	n;
	char	ans[MAX_INPUT];
	char	path[PATH_MAX];

	if (ADM(idepend, "nocheck"))
		return;

	(void) sprintf(path, "%s/install/depend", instdir);
	if (access(path, 0))
		return; /* no dependency file provided by package */

	echo(gettext(MSG_CKDEPEND));
	if (dockdeps(path, 0)) {
		sprintf(ask_cont, gettext(ASK_CONT), pkginst);
		msgtext = gettext(ERR_DEPFAILED);
		if (ADM(idepend, "quit"))
			quit(4);
		if (nointeract)
			quit(5);
		msgtext = NULL;

		ckquit = 0;
		if (n = ckyorn(ans, NULL, NULL, gettext(HLP_DEPEND),
		    ask_cont))
			quit(n);
		if (ans[0] != 'y')
			quit(3);
		ckquit = 1;
	}
}

void
ckspace(void)
{
	int	n;
	char	ans[MAX_INPUT];
	char	path[PATH_MAX];

	if (ADM(space, "nocheck"))
		return;

	echo(gettext(MSG_CKSPACE));
	(void) sprintf(path, "%s/install/space", instdir);
	if (access(path, 0) == 0)
		n = dockspace(path);
	else
		n = dockspace(NULL);

	if (n) {
		msgtext = gettext(ERR_SPCFAILED);
		sprintf(ask_cont, gettext(ASK_CONT), pkginst);
		if (ADM(space, "quit"))
			quit(4);
		if (nointeract)
			quit(5);
		msgtext = NULL;

		ckquit = 0;
		if (n = ckyorn(ans, NULL, NULL, gettext(HLP_SPACE),
		    ask_cont))
			quit(n);
		if (ans[0] != 'y')
			quit(3);
		ckquit = 1;
	}
}

void
ckdirs(void)
{
	char	path[PATH_MAX];

	if (mkpath(get_PKGADM())) {
		progerr(gettext(MSG_MKPKGDIR), get_PKGADM());
		quit(99);
	}
	(void) sprintf(path, "%s/admin", get_PKGADM());
	if (mkpath(path)) {
		progerr(gettext(MSG_MKPKGDIR), path);
		quit(99);
	}
	(void) sprintf(path, "%s/logs", get_PKGADM());
	if (mkpath(path)) {
		progerr(gettext(MSG_MKPKGDIR), path);
		quit(99);
	}
	if (mkpath(PKGSCR)) {
		progerr(gettext(MSG_MKPKGDIR), PKGSCR);
		quit(99);
	}
	if (mkpath(get_PKGLOC())) {
		progerr(gettext(MSG_MKPKGDIR), get_PKGLOC());
		quit(99);
	}
}

void
ckpkgdirs(void)
{
	if (mkpath(pkgloc)) {
		progerr(gettext(MSG_MKPKGDIR), pkgloc);
		quit(99);
	}
	if (mkpath(pkgbin)) {
		progerr(gettext(MSG_MKPKGDIR), pkgbin);
		quit(99);
	}

	/*
	 * Bring over the contents of the old save and install
	 * directories if they are there.
	 */
	if (pkgloc_sav[0]) {
		char cmd[(sizeof ("/usr/bin/cp -r %s/save %s") - 4) +
		    PATH_MAX*2 + 1];
		int status;
		int exit_no = 0;

		/*
		 * Even though it takes longer, we use a recursive copy here
		 * because this is an update and if the current pkgadd fails
		 * for any reason, we don't want to lose this data. Later, we
		 * may optimize this for speed.
		 */
		sprintf(cmd, "/usr/bin/cp -r %s/save %s", pkgloc_sav, pkgsav);
		status = system(cmd);
		if (status == -1 || WEXITSTATUS(status)) {
			progerr(gettext(MSG_XFRSAVDIR), pkgsav);
			quit(99);
		}

		sprintf(cmd, "/usr/bin/cp -r %s/install %s", pkgloc_sav,
		    pkgloc);
		status = system(cmd);
		if (status == -1 || WEXITSTATUS(status)) {
			progerr(gettext(MSG_XFRINSTDIR), pkgloc);
			quit(99);
		}
	} else if (mkpath(pkgsav)) {
		progerr(gettext(MSG_MKPKGDIR), pkgsav);
		quit(99);
	}
}

void
ckconflct(void)
{
	int	i, n, count, has_a_rogue = 0;
	char	ans[MAX_INPUT];

	if (ADM(conflict, "nochange")) {
		nocnflct++;
		return;
	} else if (ADM(conflict, "nocheck"))
		return;

	echo(gettext(MSG_CKCONFL));
	count = 0;
	for (i = 0; extlist[i]; i++) {
		struct cfent *ept;

		if (extlist[i]->cf_ent.ftype == 'i')
			continue;

		ept = &(extlist[i]->cf_ent);

		if (is_remote_fs(ept->path, &(extlist[i]->fsys_value)) &&
		    !is_fs_writeable(ept->path, &(extlist[i]->fsys_value)))
			continue;

		if (!mstat[i].shared)
			continue;
		if (ept->ftype == 'e')
			continue;
		if (mstat[i].rogue)
			has_a_rogue = 1;

		if (mstat[i].contchg) {
			if (!count++)
				ptext(stderr, gettext(MSG_CONFLICT));
			else if (!nointeract && ((count % DISPSIZ) == 0)) {
				echo(gettext(MSG_CONTDISP));
				(void) getc(stdin);
			}
			/*
			 * NOTE : The leading "!" in this string forces
			 * puttext() to print leading white space.
			 */
			ptext(stderr, "!%s %s",
			    (mstat[i].rogue) ? "*" : " ", ept->path);
		} else if (mstat[i].attrchg) {
			if (!count++)
				ptext(stderr, gettext(MSG_CONFLICT));
			else if (!nointeract && ((count % DISPSIZ) == 0)) {
				echo(gettext(MSG_CONTDISP));
				(void) getc(stdin);
			}
			ptext(stderr, gettext(MSG_ATTRONLY),
			    (mstat[i].rogue) ? "*" : " ", ept->path);
		}
	}

	if (count) {
		if (has_a_rogue)
			ptext(stderr, gettext(MSG_ROGUE));

		msgtext = gettext(ERR_CNFFAILED);
		if (ADM(conflict, "quit"))
			quit(4);
		if (nointeract)
			quit(5);
		msgtext = NULL;

		if (n = ckyorn(ans, NULL, NULL, gettext(HLP_CONFLICT),
		    gettext(ASK_CONFLICT)))
			quit(n);
		if (ans[0] == 'n') {
			ckquit = 0;
			sprintf(ask_cont, gettext(ASK_CONT), pkginst);
			if (n = ckyorn(ans, NULL, NULL, gettext(HLP_CONT),
			    ask_cont))
				quit(n);
			if (ans[0] != 'y')
				quit(3);
			ckquit = 1;
			nocnflct++;
			rprcflag++;
		}
	}
}

void
cksetuid(void)
{
	int	i, n, count, overwriting = 0;
	char	ans[MAX_INPUT];

	/* See if the administrative defaults already resolve this check. */
	if (ADM(setuid, "nocheck"))
		return;

	if (ADM(setuid, "nochange")) {
		nosetuid++;	/* Do not install processes as setuid/gid. */
		return;
	}

	/* The administrative defaults require review of the package. */
	echo(gettext(MSG_CKUID));
	count = 0;
	for (i = 0; extlist[i]; i++) {
		int overwr;

		/*
		 * Provide the administrator with info as to whether there is
		 * already a setuid process in place. This is only necessary
		 * to help the administrator decide whether or not to lay
		 * down the process, it doesn't have anything to do with the
		 * administrative defaults.
		 */
		if (mstat[i].osetuid || mstat[i].osetgid) {
			overwr = 1;
			overwriting = 1;
		} else
			overwr = 0;

		if (mstat[i].setuid || mstat[i].setgid) {
			if (!count++)
				ptext(stderr, gettext(MSG_SETUID));
			else if (!nointeract && ((count % DISPSIZ) == 0)) {
				echo(gettext(MSG_CONTDISP));
				(void) getc(stdin);
			}
			/*
			 * NOTE : The leading "!" in these strings forces
			 * puttext() to print leading white space.
			 */
			if (mstat[i].setuid && mstat[i].setgid)
				ptext(stderr, gettext(
				    "!%s %s <setuid %s setgid %s>"),
				    (overwr) ? "*" : " ",
				    extlist[i]->cf_ent.path,
				    extlist[i]->cf_ent.ainfo.owner,
				    extlist[i]->cf_ent.ainfo.group);
			else if (mstat[i].setuid)
				ptext(stderr, gettext(
				    "!%s %s <setuid %s>"),
				    (overwr) ? "*" : " ",
				    extlist[i]->cf_ent.path,
				    extlist[i]->cf_ent.ainfo.owner);
			else if (mstat[i].setgid)
				ptext(stderr, gettext(
				    "!%s%s <setgid %s>"),
				    (overwr) ? "*" : " ",
				    extlist[i]->cf_ent.path,
				    extlist[i]->cf_ent.ainfo.group);
		}
	}

	if (count) {
		if (overwriting)
			ptext(stderr, gettext(MSG_OVERWR));

		msgtext = gettext(MSG_UIDFND);
		if (ADM(setuid, "quit"))
			quit(4);
		if (nointeract)
			quit(5);
		msgtext = NULL;

		if (n = ckyorn(ans, NULL, NULL, gettext(HLP_SETUID),
		    gettext(ASK_SETUID)))
			quit(n);
		if (ans[0] == 'n') {
			ckquit = 0;
			sprintf(ask_cont, gettext(ASK_CONT), pkginst);
			if (n = ckyorn(ans, NULL, NULL, gettext(HLP_CONT),
			    ask_cont))
				quit(n);
			if (ans[0] != 'y')
				quit(3);
			ckquit = 1;
			nosetuid++;
			rprcflag++;
		}
	}
}

void
ckpriv(void)
{
	struct dirent *dp;
	DIR	*dirfp;
	int	n, found;
	char	ans[MAX_INPUT], path[PATH_MAX];

	if (ADM(action, "nocheck"))
		return;

	(void) sprintf(path, "%s/install", instdir);
	if ((dirfp = opendir(path)) == NULL)
		return;

	found = 0;
	while ((dp = readdir(dirfp)) != NULL) {
		if (strcmp(dp->d_name, "preinstall") == 0 ||
		    strcmp(dp->d_name, "postinstall") == 0 ||
		    strncmp(dp->d_name, "i.", 2) == 0) {
			found++;
			break;
		}
	}
	(void) closedir(dirfp);

	if (found) {
		ptext(stderr, gettext(MSG_PRIV));
		msgtext = gettext(MSG_SCRFND);
		sprintf(ask_cont, gettext(ASK_CONT), pkginst);
		if (ADM(action, "quit"))
			quit(4);
		if (nointeract)
			quit(5);
		msgtext = NULL;

		ckquit = 0;
		if (n = ckyorn(ans, NULL, NULL, gettext(HLP_PRIV),
		    ask_cont))
			quit(n);
		if (ans[0] != 'y')
			quit(3);
		ckquit = 1;
	}
}

void
ckpkgfiles(void)
{
	register int i;
	struct cfent	*ept;
	int	errflg;
	char	source[PATH_MAX];

	errflg = 0;
	for (i = 0; extlist[i]; i++) {
		ept = &(extlist[i]->cf_ent);
		if (ept->ftype != 'i')
			continue;

		if (ept->ainfo.local) {
			(void) sprintf(source, "%s/%s", instdir,
				ept->ainfo.local);
		} else if (strcmp(ept->path, PKGINFO) == 0) {
			(void) sprintf(source, "%s/%s", instdir, ept->path);
		} else {
			(void) sprintf(source, "%s/install/%s", instdir,
				ept->path);
		}
		if (cverify(0, &ept->ftype, source, &ept->cinfo)) {
			errflg++;
			progerr(gettext(ERR_BADFILE), source);
			logerr(errbuf);
		}
	}
	if (errflg)
		quit(99);
}
