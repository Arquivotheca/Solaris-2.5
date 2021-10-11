/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/
/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ident	"@(#)main.c	1.57	95/02/21 SMI"	/* SVr4.0 1.19.6.1 */

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <ulimit.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <string.h>
#include <signal.h>
#include <locale.h>
#include <libintl.h>
#include <pkgstrct.h>
#include <pkginfo.h>
#include <pkgdev.h>
#include <pkglocs.h>
#include <pwd.h>
#include <pkglib.h>
#include "libadm.h"
#include "libinst.h"

extern char	**environ;
extern char	*pkgabrv, *pkgname, *pkgarch, *pkgvers, pkgwild[];
extern int	Mntflg;

struct cfextra **extlist;
struct mergstat *mstat;

/* quit.c */
extern void	trap(int signo), quit(int exitval);
/* main.c */
extern void	ckreturn(int retcode, char *msg);
/* getinst.c */
extern char	*getinst(struct pkginfo *info, int npkgs);
/* sortmap.c */
extern int	sortmap(struct cfextra ***extlist, struct mergstat **mstat,
			FILE *pkgmapfp, FILE *mapfp, FILE *tmpfp);
/* pkgdbmerg.c */
extern int	files_installed(void);
/* merginfo.c */
extern void	merginfo(struct cl_attr **pclass);
/* pkgenv.c */
extern int	pkgenv(char *pkginst, char *p_pkginfo, char *p_pkgmap);
/* instvol.c */
extern void	instvol(struct cfextra **extlist, struct mergstat *mstat,
			char *srcinst, int part, int nparts);
/* reqexec.c */
extern int	reqexec(char *script, char *output);
extern int	chkexec(char *script, char *output);
/* predepend.c */
extern void	predepend(char *oldpkg);

/* lockinst.c */
extern int	lockinst(char *util_name, char *pkg_name);
extern void	lockupd(char *place);
extern void	unlockinst(void);

/* psvr4ck.c */
extern int	exception_pkg(char *pknname);

/* check.c */
extern void	cksetuid(void);
extern void	ckconflct(void);
extern void	ckpkgdirs(void);
extern void	ckdirs(void);
extern void	ckspace(void);
extern void	ckdepend(void);
extern void	ckrunlevel(void);
extern void	ckpartial(void);
extern void	ckpkgfiles(void);
extern void	ckpriv(void);
extern void	is_WOS_arch(void);

/* libpkg/gpkgmap.c */
extern void	setmapmode(int mode_no);

/* doulimit.c */
extern int	assign_ulimit(char *fslimit);
extern int	set_ulimit(char *script, char *err_msg);
extern int	clr_ulimit();

static int	vcfile(void);
static int	rdonly(char *p);
static void	copyright(void), usage(void);
static void	unpack(void);
static void	rm_icas(char *casdir);

#define	DEFPATH		"/sbin:/usr/sbin:/usr/bin"
#define	MALSIZ	4	/* best guess at likely maximum value of MAXINST */
#define	LSIZE	256	/* maximum line size supported in copyright file */

#define	ERR_USAGE	"usage: %s [ -o ] [-d device] " \
			"[-m mountpt [-f fstyp]] " \
			"[-R rootdir] [-b bindir] [-a adminf] [-r respf] " \
			"directory pkginst\n"
#define	ERR_CREAT_CONT	"unable to create contents file <%s>"
#define	ERR_ROOT_SET	"Could not set install root from the environment."
#define	ERR_ROOT_CMD	"Command line install root contends with environment."
#define	ERR_MEMORY	"memory allocation failure, errno=%d"
#define	ERR_INTONLY	"unable to install <%s> without user interaction"
#define	ERR_NOREQUEST	"package does not contain an interactive request script"
#define	ERR_LOCKFILE	"unable to create lockfile <%s>"
#define	ERR_PKGINFO	"unable to open pkginfo file <%s>"
#define	ERR_PKGBINREN	"unable to rename <%s>\n\tto <%s>"
#define	ERR_RESPONSE	"unable to open response file <%s>"
#define	ERR_PKGMAP	"unable to open pkgmap file <%s>"
#define	ERR_MKDIR	"unable to make temporary directory <%s>"
#define	ERR_RMDIR	"unable to remove directory <%s> and its contents"
#define	ERR_CHDIR	"unable to change directory to <%s>"
#define	ERR_ADMBD	"%s is already installed at %s. Admin file will " \
			    "force a duplicate installation at %s."
#define	ERR_NEWBD	"%s is already installed at %s. Duplicate " \
			    "installation attempted at %s."
#define	ERR_DSTREAM	"unable to unpack datastream"
#define	ERR_DSTREAMSEQ	"datastream seqeunce corruption"
#define	ERR_DSTREAMCNT	"datastream early termination problem"
#define	ERR_RDONLY 	"read-only parameter <%s> cannot be assigned a value"
#define	ERR_REQUEST	"request script did not complete successfully"
#define	WRN_CHKINSTALL	"checkinstall script suspends"
#define	ERR_CHKINSTALL	"checkinstall script did not complete successfully"
#define	ERR_PREINSTALL	"preinstall script did not complete successfully"
#define	ERR_POSTINSTALL	"postinstall script did not complete successfully"
#define	ERR_OPRESVR4	"unable to unlink options file <%s>"
#define	ERR_SYSINFO	"unable to process installed package information, " \
			"errno=%d"
#define	ERR_NOTROOT	"You must be \"root\" for %s to execute properly."
#define	ERR_BADULIMIT	"cannot process invalid ULIMIT value of <%s>."
#define	MSG_INST_ONE	"   %d package pathname is already properly installed."
#define	MSG_INST_MANY	"   %d package pathnames are already properly " \
			"installed."
#define	MSG_BASE_USED	"Using <%s> as the package base directory."

struct admin	adm;
struct pkgdev	pkgdev;

int	nocnflct, nosetuid;
int	dbchg;
int	rprcflag;
int	iflag;
int	dparts = 0;

int	reboot = 0;
int	ireboot = 0;
int	warnflag = 0;
int	failflag = 0;
int	started = 0;
int	update = 0;
int	opresvr4 = 0;
int	nointeract = 0;
int	maxinst = 1;
ulong	pkgmap_blks = 0L;
int	non_abi_scripts = 0;	/* bug id 1136942, not ABI compliant */

char	*pkginst,
	*msgtext,
	*respfile,
	instdir[PATH_MAX],
	pkgloc[PATH_MAX],
	pkgbin[PATH_MAX],
	pkgloc_sav[PATH_MAX],
	pkgsav[PATH_MAX],
	ilockfile[PATH_MAX],
	rlockfile[PATH_MAX],
	savlog[PATH_MAX],
	tmpdir[PATH_MAX];

static char	*ro_params[] = {
	"PATH", "NAME", "PKG", "PKGINST",
	"VERSION", "ARCH",
	"INSTDATE", "CATEGORY",
	NULL
};

/*
 * BugID #1136942:
 * The following variable is the name of the device to which stdin
 * is connected during execution of a procedure script. PROC_STDIN is
 * correct for all ABI compliant packages. For non-ABI-compliant
 * packages, the '-o' command line switch changes this to PROC_XSTDIN
 * to allow user interaction during these scripts. -- JST
 */
static char	*script_in = PROC_STDIN;	/* assume ABI compliance */

char    *rw_block_size = NULL;
static	int	silent = 0;

main(int argc, char *argv[])
{
	extern char	*optarg;
	extern int	optind;

	static char *cpio_names[] = {
	    "root",
	    "root.cpio",
	    "reloc",
	    "reloc.cpio",
	    "root.Z",
	    "root.cpio.Z",
	    "reloc.Z",
	    "reloc.cpio.Z",
	    0
	};

	struct pkginfo *prvinfo;
	FILE	*mapfp, *tmpfp;
	FILE	*fp;
	int	c, n, err, init_install = 0, called_by_gui = 0;
	long	clock;
	int	npkgs, part, nparts;
	char	*pt,
		*value,
		*srcinst,
		*device,
		**np, /* bug id 1096956 */
		*admnfile = NULL,
		*abi_comp_ptr;
	char	path[PATH_MAX],
		cmdbin[PATH_MAX],
		p_pkginfo[PATH_MAX],
		p_pkgmap[PATH_MAX],
		script[PATH_MAX],
		cbuf[64],
		path2ck[PATH_MAX], 	/* bug id 1096956 */
		param[64];
	char	*client_basedir;
	void	(*func)();
	int	is_comp_arch; 		/* bug id 1096956 */
	struct cl_attr	**pclass = NULL;
	struct	stat	statb;
	struct	statvfs	svfsb;

	(void) memset(path, '\0', sizeof (path));
	(void) memset(cmdbin, '\0', sizeof (cmdbin));
	(void) memset(script, '\0', sizeof (script));
	(void) memset(cbuf, '\0', sizeof (cbuf));
	(void) memset(path2ck, '\0', sizeof (path2ck));
	(void) memset(param, '\0', sizeof (param));

	(void) setlocale(LC_ALL, "");

#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);
	(void) setbuf(stdout, NULL);

	setmapmode(MAPINSTALL);

	(void) set_prog_name(argv[0]);

	(void) umask(0022);

	device = NULL;

	if (!set_inst_root(getenv("PKG_INSTALL_ROOT"))) {
		progerr(gettext(ERR_ROOT_SET));
		exit(1);
	}

	while ((c = getopt(argc, argv,
	    "B:N:CSR:Mf:p:d:m:b:r:Iinoa:?")) != EOF) {
		switch (c) {

		    case 'B':
			rw_block_size = optarg;
			break;

		    case 'S':
			silent++;
			break;

		    case 'I':
			init_install++;
			break;

		    case 'N':
			(void) set_prog_name(optarg);
			break;

		    case 'M':
			Mntflg++;
			break;

		    case 'p':
			dparts = ds_getinfo(optarg);
			break;

		    case 'i':
			iflag++;
			break;

		    case 'f':
			pkgdev.fstyp = optarg;
			break;

		    case 'b':
			(void) strcpy(cmdbin, optarg);
			break;

		    case 'd':
			device = flex_device(optarg, 1);
			break;

		    case 'm':
			pkgdev.mount = optarg;
			pkgdev.rdonly++;
			pkgdev.mntflg++;
			break;

		    case 'r':
			respfile = flex_device(optarg, 2);
			break;

		    case 'n':
			nointeract++;
			break;

		    case 'a':
			admnfile = flex_device(optarg, 0);
			break;

		    /* This is an old non-ABI package */
		    case 'o':
			non_abi_scripts++;
			break;

		    case 'C':
			(void) checksum_off();
			break;

		    case 'R':
			if (!set_inst_root(optarg)) {
				progerr(gettext(ERR_ROOT_CMD));
				exit(1);
			}
			break;

		    default:
			usage();
		}
	}

	if (iflag && (respfile == NULL))
		usage();

	if (device) {
		if (pkgdev.mount)
			pkgdev.bdevice = device;
		else
			pkgdev.cdevice = device;
	}
	if (pkgdev.fstyp && !pkgdev.mount) {
		progerr(gettext("-f option requires -m option"));
		usage();
	}

	/* BEGIN DATA GATHERING PHASE */
	set_PKGpaths(get_inst_root());	/* define /var... directories */

	pkgdev.dirname = argv[optind++];
	srcinst = argv[optind++];
	if (optind != argc)
		usage();

	(void) pkgparam(NULL, NULL);  /* close up prior pkg file if needed */

	/*
	 * Initialize installation admin parameters by reading
	 * the adminfile.
	 */
	setadmin(admnfile);

	func = signal(SIGINT, trap);
	if (func != SIG_DFL)
		(void) signal(SIGINT, func);
	(void) signal(SIGHUP, trap);

	ckdirs();	/* create /var... directories if necessary */
	tzset();

	(void) sprintf(instdir, "%s/%s", pkgdev.dirname, srcinst);

	if (pt = getenv("TMPDIR"))
		(void) sprintf(tmpdir, "%s/installXXXXXX", pt);
	else
		(void) strcpy(tmpdir, "/tmp/installXXXXXX");
	if ((mktemp(tmpdir) == NULL) || mkdir(tmpdir, 0771)) {
		progerr(gettext(ERR_MKDIR), tmpdir);
		quit(99);
	}

	/*
	 * If the environment has a CLIENT_BASEDIR, that takes precedence
	 * over anything we will construct. We need to save it here because
	 * in three lines, the current environment goes away.
	 */
	(void) set_env_cbdir();	/* copy over environ */

	pt = getenv("TZ");
	environ = NULL;		/* Now putparam can be used */

	if (init_install)
		putparam("PKG_INIT_INSTALL", "TRUE");

	if (pt)
		putparam("TZ", pt);

	putparam("INST_DATADIR", pkgdev.dirname);

	if (non_abi_scripts)
		putparam("NONABI_SCRIPTS", "TRUE");

	if (!cmdbin[0])
		(void) strcpy(cmdbin, PKGBIN);
	(void) sprintf(path, "%s:%s", DEFPATH, cmdbin);
	putparam("PATH", path);
	putparam("OAMBASE", OAMBASE);

	(void) sprintf(p_pkginfo, "%s/%s", instdir, PKGINFO);
	(void) sprintf(p_pkgmap, "%s/%s", instdir, PKGMAP);

	/*
	 * This tests the pkginfo and pkgmap files for validity and
	 * puts all delivered pkginfo variables (except for PATH) into
	 * our environment. This is where a delivered pkginfo BASEDIR
	 * would come from. See set_basedirs() below.
	 */
	if (pkgenv(srcinst, p_pkginfo, p_pkgmap))
		quit(1);

	echo("\n%s", pkgname);
	echo("(%s) %s", pkgarch, pkgvers);

	/*
	 *  if this script was invoked by 'pkgask', just
	 *  execute request script and quit
	 */
	if (iflag) {
		if (pkgdev.cdevice) {
			unpack();
			if (!silent)
				copyright();
		}
		(void) sprintf(path, "%s/install/request", instdir);
		if (access(path, 0)) {
			progerr(gettext(ERR_NOREQUEST));
			quit(1);
		}
		ckreturn(reqexec(path, respfile), gettext(ERR_REQUEST));

		if (warnflag || failflag) {
			(void) unlink(respfile);
			echo(gettext("\nResponse file <%s> was not created."),
			    respfile);
		} else
			echo(gettext("\nResponse file <%s> was created."),
			    respfile);
		quit(0);
	}

	/*
	 * OK, now we're serious. Verify existence of the contents file then
	 * initialize and lock the state file.
	 */
	if (!vcfile())
		quit(99);

	if (!lockinst(get_prog_name(), srcinst))
		quit(99);

	/* Now do all the various setups based on ABI compliance */

	/* Read the environment (from pkginfo or '-o') ... */
	abi_comp_ptr = getenv("NONABI_SCRIPTS");

	/* ... and if it's old protocol, set port appropriately. */
	if (abi_comp_ptr && strncmp(abi_comp_ptr, "TRUE", 4) == 0) {
		script_in = PROC_XSTDIN;
		non_abi_scripts = 1;
	}

	/*
	 * Until on1095, set it from exception package names as
	 * well.
	 */
	else if (exception_pkg(srcinst)) {
		putparam("NONABI_SCRIPTS", "TRUE");
		script_in = PROC_XSTDIN;
		non_abi_scripts = 1;
	}

	/*
	 * At this point, script_in, non_abi_scripts & the environment are
	 * all set correctly for the ABI status of the package.
	 */

	if (pt = getenv("MAXINST"))
		maxinst = atol(pt);

	/*
	 *  verify that we are not trying to install an
	 *  INTONLY package with no interaction
	 */
	if (pt = getenv("INTONLY")) {
		if (iflag || nointeract) {
			progerr(gettext(ERR_INTONLY), pkgabrv);
			quit(1);
		}
	}

	if (!silent && !pkgdev.cdevice)
		copyright();

	if (getuid()) {
		progerr(gettext(ERR_NOTROOT), get_prog_name());
		quit(1);
	}

	/*
	 * inspect the system to determine if any instances of the
	 * package being installed already exist on the system
	 */
	npkgs = 0;
	prvinfo = (struct pkginfo *) calloc(MALSIZ, sizeof (struct pkginfo));
	if (prvinfo == NULL) {
		progerr(gettext(ERR_MEMORY), errno);
		quit(99);
	}
	for (;;) {
		if (pkginfo(&prvinfo[npkgs], pkgwild, NULL, NULL)) {
			if ((errno == ESRCH) || (errno == ENOENT))
				break;
			progerr(gettext(ERR_SYSINFO), errno);
			quit(99);
		}
		if ((++npkgs % MALSIZ) == 0) {
			prvinfo = (struct pkginfo *) realloc(prvinfo,
				(npkgs+MALSIZ) * sizeof (struct pkginfo));
			if (prvinfo == NULL) {
				progerr(gettext(ERR_MEMORY), errno);
				quit(99);
			}
		}
	}

	if (npkgs > 0) {
		/*
		 * since an instance of this package already exists on
		 * the system, we must interact with the user to determine
		 * if we should overwrite an instance which is already
		 * installed, or possibly install a new instance of
		 * this package
		 */
		pkginst = getinst(prvinfo, npkgs);
	} else {
		/*
		 * the first instance of a package on the system is
		 * always identified by the package abbreviation
		 */
		pkginst = pkgabrv;
	}

	(void) sprintf(pkgloc, "%s/%s", get_PKGLOC(), pkginst);
	(void) sprintf(pkgbin, "%s/install", pkgloc);
	(void) sprintf(pkgsav, "%s/save", pkgloc);
	(void) sprintf(ilockfile, "%s/!I-Lock!", pkgloc);
	(void) sprintf(rlockfile, "%s/!R-Lock!", pkgloc);
	(void) sprintf(savlog, "%s/logs/%s", get_PKGADM(), pkginst);

	putparam("PKGINST", pkginst);
	putparam("PKGSAV", pkgsav);

	/*
	 * Be sure request script has access to PKG_INSTALL_ROOT if there is
	 * one
	 */
	put_path_params();

	/*
	 * If this version and architecture is already installed, merge the
	 * installed and installing parameters and inform all procedure
	 * scripts by defining UPDATE in the environment.
	 */
	if (update) {
		/*
		 * Read in parameter values from the instance which is
		 * currently installed
		 */
		(void) sprintf(p_pkginfo, "%s/%s", pkgloc, PKGINFO);
		if ((fp = fopen(p_pkginfo, "r")) == NULL) {
			progerr(gettext(ERR_PKGINFO), p_pkginfo);
			quit(99);
		}
		param[0] = '\0';

		/*
		 * Now scan the installed pkginfo and merge that environment
		 * with the installing environment according to the following
		 * rules.
		 *
		 *	1. CLASSES is a union of the installed and
		 *	   installing CLASSES lists.
		 *	2. The installed BASEDIR takes precedence. If
		 *	   it doesn't agree with an administratively
		 *	   imposed BASEDIR, an ERROR is issued.
		 *	3. All other installing parameters are preserved.
		 *	4. All installed parameters are added iff they
		 *	   do not overwrite an existing installing parameter.
		 */
		while (value = fpkgparam(fp, param)) {
			if (strcmp(param, "CLASSES") == 0)
				(void) setlist(&pclass, qstrdup(value));
			else if (strcmp("BASEDIR", param) == 0) {
				if (adm.basedir && *(adm.basedir) &&
				    strchr("/$", *(adm.basedir))) {
					char *dotptr;
					/* Get srcinst down to a name. */
					if (dotptr = strchr(srcinst, '.'))
						*dotptr = '\000';
					if (strcmp(value, adm.basedir) != 0) {
						progerr(gettext(ERR_ADMBD),
						    srcinst, value,
						    adm.basedir);
						quit(4);
					}
				} else if (ADM(basedir, "ask"))
					/*
					 * If it's going to ask later, let it
					 * know that it *must* agree with the
					 * BASEDIR we just picked up.
					 */
					adm.basedir = "update";
				putparam(param, value);
			} else if (getenv(param) == NULL)
				putparam(param, value);
			param[0] = '\0';
		}
		(void) fclose(fp);
		putparam("UPDATE", "yes");

		/*
		 * If we are updating a pkg, then we need to remove the "old"
		 * pkgloc so that any scripts that got removed in the new
		 * version aren't left around.  So we rename it here to
		 * .save.pkgloc, then in quit() we can restore our state, or
		 * remove it.
		 */
		if (pkgloc[0] && !access(pkgloc, F_OK)) {
			(void) sprintf(pkgloc_sav, "%s/.save.%s", get_PKGLOC(),
			    pkginst);
			if (pkgloc_sav[0] && !access(pkgloc_sav, F_OK))
				(void) rrmdir(pkgloc_sav);
			if (rename(pkgloc, pkgloc_sav) == -1) {
				progerr(gettext(ERR_PKGBINREN), pkgloc,
					pkgloc_sav);
				quit(99);
			}
		}
	}

	/*
	 *  determine if the package has been partially
	 *  installed on or removed from this system
	 */
	ckpartial();

#ifndef SUNOS41
	/*
	 *  make sure current runlevel is appropriate
	 */
	ckrunlevel();
#endif
	if (pkgdev.cdevice) {
		/* get first volume which contains info files */
		unpack();
		if (!silent)
			copyright();
	}

	/*
	 * get the mount table info and store internally.
	 */
	get_mntinfo();

	lockupd("request");

	/*
	 * if no response file has been provided,
	 * initialize response file by executing any
	 * request script provided by this package
	 */
	if (respfile == NULL) {
		(void) sprintf(path, "%s/install/request", instdir);
		ckreturn(reqexec(path, NULL), gettext(ERR_REQUEST));
	}

	/* BEGIN ANALYSIS PHASE */
	lockupd("checkinstall");

	/* Execute checkinstall script if one is provided. */
	(void) sprintf(script, "%s/install/checkinstall", instdir);
	if (access(script, 0) == 0) {
		int retval;

		echo(gettext("## Executing checkinstall script."));
		retval = chkexec(script, respfile);
		if (retval == 3) {
			echo(gettext(WRN_CHKINSTALL));
			ckreturn(4, NULL);
		} else
			ckreturn(retval, gettext(ERR_CHKINSTALL));
	}

	/*
	 * look for all parameters in response file which begin
	 * with a capital letter, and place them in the
	 * environment
	 */
	if (respfile) {
		/* bug id 1080695 */
		char resppath[PATH_MAX];
		char *locbasedir;

		if (isdir(respfile) == 0)
			(void) sprintf(resppath, "%s/%s", respfile, pkginst);
		else
			(void) strcpy(resppath, respfile);

		if ((fp = fopen(resppath, "r")) == NULL) {
			progerr(gettext(ERR_RESPONSE), respfile);
			quit(99);
		}
		param[0] = '\0';
		while (value = fpkgparam(fp, param)) {
			if (!isupper(param[0])) {
				param[0] = '\0';
				continue;
			}
			if (rdonly(param)) {
				progerr(gettext(ERR_RDONLY), param);
				param[0] = '\0';
				continue;
			}

			/*
			 * If this is an update, and the response file
			 * specifies the BASEDIR, make sure it matches the
			 * existing installation base. If it doesn't, we have
			 * to quit.
			 */
			if (update && strcmp("BASEDIR", param) == 0) {
				locbasedir = getenv("BASEDIR");
				if (locbasedir &&
				    strcmp(value, locbasedir) != 0) {
					char *dotptr;
					/* Get srcinst down to a name. */
					if (dotptr = strchr(srcinst, '.'))
						*dotptr = '\000';
					progerr(gettext(ERR_NEWBD),
					    srcinst, locbasedir, value);
					quit(99);
				}
			}

			putparam(param, value);
			param[0] = '\0';
		}
		(void) fclose(fp);
	}

	lockupd(gettext("analysis"));

	/*
	 * Determine package base directory and client base directory
	 * if appropriate. Then encapsulate them for future retrieval.
	 */
	if ((err = set_basedirs(isreloc(instdir), adm.basedir, pkginst,
	    nointeract)) != 0)
		quit(err);

	if (is_a_basedir()) {
		mkbasedir(!nointeract, get_basedir());
		echo(MSG_BASE_USED, get_basedir());
	}

	/*
	 * Store PKG_INSTALL_ROOT, BASEDIR & CLIENT_BASEDIR in our
	 * environment for later use by procedure scripts.
	 */
	put_path_params();

	/*
	 * the following two checks are done in the corresponding
	 * ck() routine, but are repeated here to avoid re-processing
	 * the database if we are administered to not include these
	 * processes
	 */
	if (ADM(setuid, "nochange"))
		nosetuid++;	/* Clear setuid/gid bits. */
	if (ADM(conflict, "nochange"))
		nocnflct++;	/* Don't install conflicting files. */

	/*
	 * Get the filesystem space information for the filesystem on which
	 * the "contents" file resides.
	 */
	if (!access(get_PKGADM(), F_OK)) {
		if (statvfs(get_PKGADM(), &svfsb) == -1) {
			progerr(gettext("statvfs(%s) failed"), get_PKGADM());
			logerr("(errno %d)", errno);
			quit(99);
		}
	} else {
		svfsb.f_bsize = 8192;
		svfsb.f_frsize = 1024;
	}

	/*
	 * Get the number of blocks used by the pkgmap, ocfile()
	 * needs this to properly determine its space requirements.
	 */
	if (stat(p_pkgmap, &statb) == -1) {
		progerr(gettext("unable to get space usage of <%s>"),
		    p_pkgmap);
		quit(99);
	}

	pkgmap_blks = nblk(statb.st_size, svfsb.f_bsize, svfsb.f_frsize);

	/*
	 * Merge information in memory with the "contents" file;
	 * this creates a temporary version of the "contents"
	 * file.
	 */
	if (!ocfile(&mapfp, &tmpfp, pkgmap_blks))
		quit(99);

	/*
	 * if cpio is being used,  tell pkgdbmerg since attributes will
	 * have to be check and repaired on all file and directories
	 */
	for (np = cpio_names; *np != NULL; np++) {
		(void) sprintf(path2ck, "%s/%s", instdir, *np);
		if (iscpio(path2ck, &is_comp_arch)) {
			is_WOS_arch();
			break;
		}
	}

	/* Establish the class list and the class attributes. */
	cl_sets(getenv("CLASSES"));
	find_CAS(I_ONLY, pkgbin, instdir);

	if ((fp = fopen(p_pkgmap, "r")) == NULL) {
		progerr(gettext(ERR_PKGMAP), p_pkgmap);
		quit(99);
	}

	/*
	 * This modifies the path list entries in memory to reflect
	 * how they should look after the merg is complete
	 */
	nparts = sortmap(&extlist, &mstat, fp, mapfp, tmpfp);

	if ((n = files_installed()) > 0) {
		if (n > 1)
			echo(gettext(MSG_INST_MANY), n);
		else
			echo(gettext(MSG_INST_ONE), n);
	}

	/*
	 * Check ulimit requirement (provided in pkginfo). The purpose of
	 * this limit is to terminate pathological file growth resulting from
	 * file edits in scripts. It does not apply to files in the pkgmap
	 * and it does not apply to any database files manipulated by the
	 * installation service.
	 */
	if (pt = getenv("ULIMIT")) {
		if (assign_ulimit(pt) == -1) {
			progerr(gettext(ERR_BADULIMIT), pt);
			quit(99);
		}
	}

	/*
	 *  verify package information files are not corrupt
	 */
	ckpkgfiles();

	/*
	 *  verify package dependencies
	 */
	ckdepend();

	/*
	 *  Check space requirements.
	 */
	ckspace();

	/*
	 * Determine if any objects provided by this package conflict with
	 * the files of previously installed packages.
	 */
	ckconflct();

	/*
	 * Determine if any objects provided by this package will be
	 * installed with setuid or setgid enabled.
	 */
	cksetuid();

	/*
	 * Determine if any packaging scripts provided with this package will
	 * execute as a priviledged user.
	 */
	ckpriv();

	/*
	 *  Verify neccessary package installation directories exist.
	 */
	ckpkgdirs();

	/*
	 * If we have assumed that we were installing setuid or conflicting
	 * files, and the user chose to do otherwise, we need to read in the
	 * package map again and re-merg with the "contents" file
	 */
	if (rprcflag)
		nparts = sortmap(&extlist, &mstat, fp, mapfp, tmpfp);

	(void) fclose(fp);

	/* BEGIN INSTALLATION PHASE */
	echo(gettext("\nInstalling %s as <%s>\n"), pkgname, pkginst);
	started++;

	/*
	 * This replaces the contents file with recently created temp version
	 * which contains information about the objects being installed.
	 * Under old lock protocol it closes both files and releases the
	 * locks. Beginning in Solaris 2.7, this lock method should be
	 * reviewed.
	 */
	if (n = (swapcfile(mapfp, tmpfp, (dbchg ? pkginst : NULL))) ==
	    RESULT_WRN)
		warnflag++;
	else if (n == RESULT_ERR)
		quit(99);

	/*
	 * Create install-specific lockfile to indicate start of
	 * installation. This is really just an information file. If the
	 * process dies, the initial lockfile (from lockinst(), is
	 * relinquished by the kernel, but this one remains in support of the
	 * post-mortem.
	 */
	if (creat(ilockfile, 0644) < 0) {
		progerr(gettext(ERR_LOCKFILE), ilockfile);
		quit(99);
	}

	(void) time(&clock);
	(void) cftime(cbuf, "%b %d \045Y \045H:\045M", &clock);
	putparam("INSTDATE", qstrdup(cbuf));

	/*
	 *  Store information about package being installed;
	 *  modify installation parameters as neccessary and
	 *  copy contents of 'install' directory into $pkgloc
	 */
	merginfo(pclass);

	if (opresvr4) {
		/*
		 * we are overwriting a pre-svr4 package, so remove the file
		 * in /usr/options now
		 */
		(void) sprintf(path, "%s/%s.name", get_PKGOLD(), pkginst);
		if (unlink(path) && (errno != ENOENT)) {
			progerr(gettext(ERR_OPRESVR4), path);
			warnflag++;
		}
	}

	lockupd("preinstall");

	/*
	 * Execute preinstall script, if one was provided with the
	 * package. We check the package to avoid running an old
	 * preinstall script if one was provided with a prior instance.
	 */
	(void) sprintf(script, "%s/install/preinstall", instdir);
	if (access(script, 0) == 0) {
		/* execute script residing in pkgbin instead of media */
		(void) sprintf(script, "%s/preinstall", pkgbin);
		if (access(script, 0) == 0) {
			set_ulimit("preinstall", gettext(ERR_PREINSTALL));
			echo(gettext("## Executing preinstall script."));
			ckreturn(pkgexecl(script_in, PROC_STDOUT, PROC_USER,
			    PROC_GRP, SHELL, script, NULL),
			    gettext(ERR_PREINSTALL));
			clr_ulimit();
			unlink(script);	/* no longer needed. */
		}
	}

	/*
	 * Check delivered package for a postinstall script while
	 * we're still on volume 1.
	 */
	(void) sprintf(script, "%s/install/postinstall", instdir);
	if (access(script, 0) == 0)
		(void) sprintf(script, "%s/postinstall", pkgbin);
	else
		script[0] = '\000';

	lockupd(gettext("install"));

	/*
	 *  install package one part (volume) at a time
	 */
	part = 1;
	while (part <= nparts) {
		if ((part > 1) && pkgdev.cdevice)
			unpack();
		instvol(extlist, mstat, srcinst, part, nparts);

		if (part++ >= nparts)
			break;
	}

	/*
	 * Now that all install class action scripts have been used, we
	 * delete them from the package directory.
	 */
	rm_icas(pkgbin);

	lockupd("postinstall");

	/*
	 * Execute postinstall script, if any
	 */
	if (*script && access(script, 0) == 0) {
		set_ulimit("postinstall", gettext(ERR_POSTINSTALL));
		echo(gettext("## Executing postinstall script."));
		ckreturn(pkgexecl(script_in, PROC_STDOUT, PROC_USER,
		    PROC_GRP, SHELL, script, NULL),
		    gettext(ERR_POSTINSTALL));
		clr_ulimit();
		unlink(script);	/* no longer needed */
	}

	if (!warnflag && !failflag) {
		if (pt = getenv("PREDEPEND"))
			predepend(pt);
		(void) unlink(rlockfile);
		(void) unlink(ilockfile);
		(void) unlink(savlog);
	}

	(void) unlockinst();	/* release generic lock */

	quit(0);
	/*NOTREACHED*/
#ifdef lint
	return (0);
#endif	/* lint */
}

/*
 * This function deletes all install class action scripts from the package
 * directory on the root filesystem.
 */
void
rm_icas(char *cas_dir)
{
	DIR	*pdirfp;
	struct	dirent *dp;
	char path[PATH_MAX];

	if ((pdirfp = opendir(cas_dir)) == NULL)
		return;

	while ((dp = readdir(pdirfp)) != NULL) {
		if (dp->d_name[0] == '.')
			continue;

		if (dp->d_name[0] == 'i' && dp->d_name[1] == '.') {
			(void) sprintf(path, "%s/%s", cas_dir, dp->d_name);
			unlink(path);
		}
	}
	(void) closedir(pdirfp);
}

void
ckreturn(int retcode, char *msg)
{
	switch (retcode) {
	    case 2:
	    case 12:
	    case 22:
		warnflag++;
		if (msg)
			progerr(msg);
		/*FALLTHRU*/
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

/*
 * This function verifies that the contents file is in place. If it is - no
 * change. If it isn't - this creates it.
 */
static int
vcfile(void)
{
	int	fd;
	char 	contents[PATH_MAX];

	(void) sprintf(contents, "%s/contents", get_PKGADM());
	if ((fd = open(contents, O_WRONLY | O_CREAT | O_EXCL, 0644)) < 0) {
		if (errno == EEXIST) {
			return (1);	/* Contents file is already there. */
		} else {	/* Can't make it. */
			progerr(gettext(ERR_CREAT_CONT), contents);
			logerr("(errno %d)", errno);
			return (0);
		}
	} else {	/* Contents file wasn't there, but is now. */
		echo(gettext("## Software contents file initialized"));
		(void) close(fd);
		return (1);
	}
}

#define	COPYRIGHT "install/copyright"

static void
copyright(void)
{
	FILE	*fp;
	char	line[LSIZE];
	char	path[PATH_MAX];

	/* Compose full path for copyright file */
	(void) sprintf(path, "%s/%s", instdir, COPYRIGHT);

	if ((fp = fopen(path, "r")) == NULL) {
		if (getenv("VENDOR") != NULL)
			echo(getenv("VENDOR"));
	} else {
		while (fgets(line, LSIZE, fp))
			(void) fprintf(stdout, "%s", line); /* bug #1083713 */
		(void) fclose(fp);
	}
}


static int
rdonly(char *p)
{
	int	i;

	for (i = 0; ro_params[i]; i++) {
		if (strcmp(p, ro_params[i]) == 0)
			return (1);
	}
	return (0);
}

static void
unpack(void)
{
	/*
	 * read in next part from stream, even if we decide
	 * later that we don't need it
	 */
	if (dparts < 1) {
		progerr(gettext(ERR_DSTREAMCNT));
		quit(99);
	}
	if ((access(instdir, 0) == 0) && rrmdir(instdir)) {
		progerr(gettext(ERR_RMDIR), instdir);
		quit(99);
	}
	if (mkdir(instdir, 0755)) {
		progerr(gettext(ERR_MKDIR), instdir);
		quit(99);
	}
	if (chdir(instdir)) {
		progerr(gettext(ERR_CHDIR), instdir);
		quit(99);
	}
	dparts--;
	if (ds_next(pkgdev.cdevice, instdir)) {
		progerr(gettext(ERR_DSTREAM));
		quit(99);
	}
	if (chdir(get_PKGADM())) {
		progerr(gettext(ERR_CHDIR), get_PKGADM());
		quit(99);
	}
}

static void
usage(void)
{
	(void) fprintf(stderr, gettext(ERR_USAGE), get_prog_name());
	exit(1);
}
