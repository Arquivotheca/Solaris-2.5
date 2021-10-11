/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/
/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ident	"@(#)main.c	1.40	95/02/21 SMI"	/* SVr4.0 1.13.3.1 */

/*  5-20-92	added newroot function */

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pkgstrct.h>
#include <pkginfo.h>
#include <pkglocs.h>
#include <locale.h>
#include <libintl.h>
#include "install.h"
#include <pkglib.h>
#include "libadm.h"
#include "libinst.h"

extern struct cfent **eptlist;
extern int	eptnum;
extern char	*pkgdir;
extern char	**environ;

/* quit.c */
extern void	trap(int signo), quit(int retcode);

/* check.c */
extern void	rckpriv(void);
extern void	rckdepend(void);
extern void	rckrunlevel(void);

/* predepend.c */
extern void	predepend(char *oldpkg);

/* delmap.c */
extern void	delmap(int flag);

/* psvr4ck.c */
extern int	exception_pkg(char *pknname);

/* lockinst.c */
extern int	lockinst(char *util_name, char *pkg_name);
extern void	lockupd(char *place);
extern void	unlockinst(void);

/* doulimit.c */
extern int	assign_ulimit(char *fslimit);
extern int	set_ulimit(char *script, char *err_msg);
extern int	clr_ulimit();

#define	DEFPATH		"/sbin:/usr/sbin:/usr/bin"

#define	ERR_ROOT_SET	"Could not set install root from the environment."
#define	ERR_ROOT_CMD	"Command line install root contends with environment."
#define	ERR_UNSUCC	"(A previous attempt may have been unsuccessful.)"
#define	ERR_LOCKFILE	"unable to create lockfile <%s>"
#define	ERR_CHDIR	"unable to change directory to <%s>"
#define	ERR_PKGINFO	"unable to process pkginfo file <%s>"
#define	ERR_CLASSES	"CLASSES parameter undefined in <%s>"
#define	ERR_TMPFILE	"unable to establish temporary file"
#define	ERR_WTMPFILE	"unable to write temporary file <%s>"
#define	ERR_RMDIR	"unable to remove directory <%s>"
#define	ERR_RMPATH	"unable to remove <%s>"
#define	ERR_PREREMOVE	"preremove script did not complete successfully"
#define	ERR_POSTREMOVE	"postremove script did not complete successfully"
#define	ERR_CASFAIL	"class action script did not complete successfully"
#define	ERR_NOTROOT	"You must be \"root\" for %s to execute properly."
#define	ERR_BADULIMIT	"cannot process invalid ULIMIT value of <%s>."
#define	MSG_NOTEMPTY	"%s <non-empty directory not removed>"
#define	MSG_DIRBUSY	"%s <mount point not removed>"
#define	MSG_SHARED	"%s <shared pathname not removed>"
#define	MSG_SERVER	"%s <server package pathname not removed>"

struct	admin adm; 	/* holds info about installation admin */
int	reboot; 	/* non-zero if reboot required after installation */
int	ireboot;	/* non-zero if immediate reboot required */
int	failflag;	/* non-zero if fatal error has occurred */
int	warnflag;	/* non-zero if non-fatal error has occurred */
int	nointeract;	/* non-zero if no interaction with user should occur */
int	started;
int	nocnflct = 0; 	/* pkgdbmerg needs this set */
int	nosetuid = 0; 	/* pkgdbmerg needs this set */

char	*pkginst; 	/* current package (source) instance to process */

int	dbchg;
char	*msgtext;
char	pkgloc[PATH_MAX];

/*
 * BugID #1136942:
 * The following variable is the name of the device to which stdin
 * is connected during execution of a procedure script. /dev/null is
 * correct for all ABI compliant packages. For non-ABI-compliant
 * packages, the '-o' command line switch changes this to /dev/tty
 * to allow user interaction during these scripts. -- JST
 */
static char 	*script_in = PROC_STDIN;	/* assume ABI compliance */

static char	*client_mntdir; 	/* mount point for client's basedir */
static char	*client_basedir;	/* basedir relative to the client */
static char	*basedir;
static char	pkgbin[PATH_MAX],
		rlockfile[PATH_MAX],
		*admnfile, 		/* file to use for installation admin */
		*tmpdir; 		/* location to place temporary files */

static void	ckreturn(int retcode, char *msg);
static void	rmclass(char *aclass);
static void	usage(void);

main(int argc, char *argv[])
{
	FILE	*fp;
	char	*value, *pt;
	char	script[PATH_MAX],
		path[PATH_MAX],
		param[64],
		*abi_comp_ptr;
	int	i, c, err, fd;
	int	noclsav = 0;	/* no client relative PKGSAV */
	int	nodelete = 0; 	/* do not delete file or run scripts */
	static	char	*ir = NULL;
	void	(*func)();
	extern	char *optarg;
	extern	int  optind;

	(void) setlocale(LC_ALL, "");

#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	(void) umask(0022);
	(void) set_prog_name(argv[0]);

	if (getuid()) {
		progerr(gettext(ERR_NOTROOT), get_prog_name());
		exit(1);
	}

	if (!set_inst_root(getenv("PKG_INSTALL_ROOT"))) {
		progerr(gettext(ERR_ROOT_SET));
		exit(1);
	}

	while ((c = getopt(argc, argv, "Fa:noN:R:?")) != EOF) {
		switch (c) {
		    case 'a':
			admnfile = flex_device(optarg, 0);
			break;

		    case 'n':
			nointeract++;
			break;

		    case 'N':
			(void) set_prog_name(optarg);
			break;

		    case 'o': /* it's an old package */
			script_in = PROC_XSTDIN;
			break;

		    case 'R':
			if (!set_inst_root(optarg)) {
				progerr(gettext(ERR_ROOT_CMD));
				exit(1);
			}
			break;

		    case 'F': /* bugid 1092795 */
			nodelete++;
			break;

		    default:
			usage();
		}
	}

	set_PKGpaths(get_inst_root());	/* set up /var... directories */

	func = signal(SIGINT, trap);
	if (func != SIG_DFL)
		(void) signal(SIGINT, func);
	(void) signal(SIGHUP, trap);

	pkginst = argv[optind++];
	if (optind != argc)
		usage();

	if (!lockinst(get_prog_name(), pkginst))
		quit(99);

	tmpdir = getenv("TMPDIR");
	if (tmpdir == NULL)
		tmpdir = P_tmpdir;

	/*
	 * initialize installation admin parameters
	 */
	setadmin(admnfile);

	(void) sprintf(pkgloc, "%s/%s", get_PKGLOC(), pkginst);
	(void) sprintf(pkgbin, "%s/install", pkgloc);
	(void) sprintf(rlockfile, "%s/!R-Lock!", pkgloc);

	if ((isdir(pkgbin) == 0) && chdir(pkgbin)) {
		progerr(gettext(ERR_CHDIR), pkgbin);
		quit(99);
	}

	echo(gettext("\n## Removing installed package instance <%s>"), pkginst);
	if (access(rlockfile, 0) == 0)
		echo(gettext(ERR_UNSUCC));

	/*
	 * Process all parameters from the pkginfo file
	 * and place them in the execution environment
	 */
	(void) sprintf(path, "%s/pkginfo", pkgloc);
	if ((fp = fopen(path, "r")) == NULL) {
		progerr(gettext(ERR_PKGINFO), path);
		quit(99);
	}

	/* Read the mount table */
	get_mntinfo();

	/* Get mount point of client */
	client_mntdir = getenv("CLIENT_MNTDIR");

	environ = NULL;
	param[0] = '\0';
	while (value = fpkgparam(fp, param)) {
		int validx;
		char *newvalue;

		if (strcmp(param, "PATH")) {
			validx = 0;
			/* Here we interpret the save directory. */
			if (strcmp(param, "PKGSAV") == 0) {
				/*
				 * If in host:path format or marked with the
				 * leading "//", then there is no
				 * client-relative translation and we take it
				 * literally later rather than use fixpath().
				 */
				if (strstr(value, ":/")) {
					noclsav = 1;
				} else if (strstr(value, "//") == value) {
					noclsav = 1;
					validx = 1;
				} else if (is_an_inst_root()) {
					/*
					 * This PKGSAV needs to be made
					 * client-relative.
					 */
					newvalue = fixpath(value);
					free(value);
					value = newvalue;
				}
			}
			putparam(param, value+validx);
		}
		free(value);
		param[0] = '\0';
	}
	(void) fclose(fp);

	/* Now do all the various setups based on ABI compliance */

	/* Read the environment provided by the pkginfo file */
	abi_comp_ptr = getenv("NONABI_SCRIPTS");

	/*
	 * If pkginfo says it's not compliant then set non_abi_scripts.
	 * Oh, for two releases, set it from exception package names as
	 * well.
	 */
	if (exception_pkg(pkginst) ||
	    (abi_comp_ptr && strncmp(abi_comp_ptr, "TRUE", 4) == 0))
		script_in = PROC_XSTDIN;

	/*
	 * Since this is a removal, we can tell whether it's absolute or
	 * not from the resident pkginfo file read above.
	 */
	if ((err = set_basedirs((getenv("BASEDIR") != NULL), adm.basedir,
	    pkginst, nointeract)) != 0)
		quit(err);

	put_path_params();

	/* If client mount point, add it to pkgremove environment */
	if (client_mntdir != NULL)
		putparam("CLIENT_MNTDIR", client_mntdir);

	if ((value = getenv("CLASSES")) != NULL) {
		cl_sets(qstrdup(value));
	} else {
		progerr(gettext(ERR_CLASSES), path);
		quit(99);
	}

	(void) sprintf(path, "%s:%s", DEFPATH, PKGBIN);
	putparam("PATH", path);
	putparam("TMPDIR", tmpdir);

	/*
	 * Check ulimit requirement (provided in pkginfo). The purpose of
	 * this limit is to terminate pathological file growth resulting from
	 * file edits in scripts. It does not apply to files in the pkgmap
	 * and it does not apply to any database files manipulated by the
	 * installation service.
	 */
	if (value = getenv("ULIMIT")) {
		if (assign_ulimit(value) == -1) {
			progerr(gettext(ERR_BADULIMIT), value);
			warnflag++;
		}
	}

#ifndef SUNOS41
	/*
	 *  make sure current runlevel is appropriate
	 */
	rckrunlevel();
#endif

	/*
	 * determine if any packaging scripts provided with
	 * this package will execute as a priviledged user
	 */
	rckpriv();

	/*
	 *  verify package dependencies
	 */
	rckdepend();

	/*
	 *  create lockfile to indicate start of installation
	 */
	started++;
	if ((fd = creat(rlockfile, 0644)) < 0) {
		progerr(gettext(ERR_LOCKFILE), rlockfile);
		quit(99);
	} else
		close(fd);

	echo(gettext("## Processing package information."));
	delmap(0);

	lockupd("preremove");

	/*
	 *  execute preremove script, if any
	 */
	(void) sprintf(script, "%s/preremove", pkgbin);
	if (access(script, 0) == 0) {
		set_ulimit("preremove", gettext(ERR_PREREMOVE));
		echo(gettext("## Executing preremove script."));
		ckreturn(pkgexecl(script_in, PROC_STDOUT, PROC_USER, PROC_GRP,
		    SHELL, script, NULL), gettext(ERR_PREREMOVE));
		clr_ulimit();
	}

	lockupd(gettext("remove"));

	/* reverse order of classes */
	i = cl_getn();
	while (--i >= 0)
		if (!nodelete)
			rmclass(cl_nam(i));

	if (!nodelete)
		rmclass(NULL);

	lockupd("postremove");

	/*
	 *  execute postremove script, if any
	 */
	(void) sprintf(script, "%s/postremove", pkgbin);
	if ((access(script, 0) == 0) && !nodelete) {
		set_ulimit("postremove", gettext(ERR_POSTREMOVE));
		echo(gettext("## Executing postremove script."));
		ckreturn(pkgexecl(script_in, PROC_STDOUT, PROC_USER, PROC_GRP,
		    SHELL, script, NULL), gettext(ERR_POSTREMOVE));
		clr_ulimit();
	}

	echo(gettext("## Updating system information."));
	delmap(1);

	if (!warnflag && !failflag) {
		if (pt = getenv("PREDEPEND"))
			predepend(pt);
		(void) chdir("/");
		if (rrmdir(pkgloc))
			warnflag++;
	}

	unlockinst();

	quit(0);
	/*NOTREACHED*/
#ifdef lint
	return (0);
#endif	/* lint */
}

static void
rmclass(char *aclass)
{
	struct cfent	*ept;
	FILE	*fp;
	char	*tmpfile;
	char	script[PATH_MAX];
	char	path[PATH_MAX];
	int	i, n;
	char	*tmp_path;
	char	*save_path = NULL;
	char	*ir = NULL;

	if (aclass == NULL) {
		for (i = 0; i < eptnum; i++) {
			if (eptlist[i] != NULL)
				rmclass(eptlist[i]->pkg_class);
		}
		return;
	}

	/* locate class action script to execute */
	(void) sprintf(script, "%s/r.%s", pkgbin, aclass);
	if (access(script, 0)) {
		(void) sprintf(script, "%s/r.%s", PKGSCR, aclass);
		if (access(script, 0))
			script[0] = '\0';
	}
	if (script[0]) {
		tmpfile = tempnam(tmpdir, "RMLIST");
		if (tmpfile == NULL) {
			progerr(gettext(ERR_TMPFILE));
			quit(99);
		}
		if ((fp = fopen(tmpfile, "w")) == NULL) {
			progerr(gettext(ERR_WTMPFILE), tmpfile);
			quit(99);
		}
	}
	echo(gettext("## Removing pathnames in class <%s>"), aclass);

	/* process paths in reverse order */
	i = eptnum;
	while (--i >= 0) {
		ept = eptlist[i];
		if ((ept == NULL) || strcmp(aclass, ept->pkg_class))
			continue;

		/* save the path, and prepend the ir */
		if (is_an_inst_root()) {
			save_path = ept->path;
			tmp_path = fixpath(ept->path);
			ept->path = tmp_path;
		}

		if (!ept->ftype)
			echo(gettext(MSG_SHARED), ept->path);
		else if (script[0]) {
			(void) fprintf(fp, "%s\n", ept->path);
		} else if (strchr("dx", ept->ftype)) {
			(void) strcpy(path, ept->path);

			if (rmdir(path)) {
				/* remove directory */
				if (ept->pinfo->status == '%')
					/*
					 * Directory is owned by a server
					 * on the network.
					 */
					echo(gettext(MSG_SERVER), path);
				else if (errno == EBUSY)
					echo(gettext(MSG_DIRBUSY), path);
				else if (errno == EEXIST)
					echo(gettext(MSG_NOTEMPTY), path);
				else if (errno != ENOENT) {
					progerr(gettext(ERR_RMDIR), path);
					warnflag++;
				}
			} else
				echo("%s", path);

		} else {
			(void) strcpy(path, ept->path);

			if (unlink(path)) {
				if (ept->pinfo->status == '%')
					/*
					 * File does not exist.  Note
					 * if it is owned by a server.
					 */
					echo(gettext(MSG_SERVER), path);
				else if (errno != ENOENT) {
					progerr(gettext(ERR_RMPATH), path);
					warnflag++;
				}
			} else
				echo("%s", path);
		}

		/* restore the original path */

		if (is_an_inst_root())
			ept->path = save_path;

		/*
		 * free memory allocated for this entry memory used for
		 * pathnames will be freed later by a call to pathdup()
		 */
		free(ept);
		eptlist[i] = NULL;
	}
	if (script[0]) {
		(void) fclose(fp);
		set_ulimit(script, gettext(ERR_CASFAIL));
		ckreturn(pkgexecl(tmpfile, CAS_STDOUT, CAS_USER,
		    CAS_GRP, SHELL, script, NULL), gettext(ERR_CASFAIL));
		clr_ulimit();
		if (isfile(NULL, tmpfile) == 0) {
			if (unlink(tmpfile) == -1)
				progerr(gettext(ERR_RMPATH), tmpfile);
		}
	}
}

static void
ckreturn(int retcode, char *msg)
{
	switch (retcode) {
	    case 2:
	    case 12:
	    case 22:
		warnflag++;
		/*FALLTHRU*/
		if (msg)
			progerr(msg);
	    case 10:
	    case 20:
		if (retcode >= 10)
			reboot++;
		if (retcode >= 20)
			ireboot++;
		/*FALLTHRU*/
	    case 0:
		break; /* okay */

	    case -1:
		retcode = 99;
		/*FALLTHRU*/
	    case 99:
	    case 1:
	    case 11:
	    case 21:
	    case 4:
	    case 14:
	    case 24:
	    case 5:
	    case 15:
	    case 25:
		if (msg)
			progerr(msg);
		/*FALLTHRU*/
	    case 3:
	    case 13:
	    case 23:
		quit(retcode);
		/* NOT REACHED */
	    default:
		if (msg)
			progerr(msg);
		quit(1);
	}
}

static void
usage(void)
{
	(void) fprintf(stderr,
	    gettext("usage: %s [-a admin] [-o] [-n] pkginst\n"),
	    get_prog_name());
	exit(1);
}
