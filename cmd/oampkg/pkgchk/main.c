/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/
/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ident	"@(#)main.c	1.21	95/01/11 SMI"	/* SVr4.0  1.5.1.1	*/

#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pkginfo.h>
#include <pkglocs.h>
#include <sys/types.h>
#include <pkgstrct.h>
#include <pkgtrans.h>
#include <locale.h>
#include <libintl.h>
#include <pkglib.h>
#include "libadm.h"
#include "libinst.h"

#define	MAXPATHS	1024
#define	MALLSIZ		50

#define	MSG_CHK_STRM	"Checking uninstalled stream format package " \
			    "<%s> from <%s>\n"
#define	MSG_CHK_DIR	"Checking uninstalled directory format package " \
			    "<%s> from <%s>\n"

#define	WRN_F_SPOOL	"WARNING: %s is spooled. Ignoring \"f\" argument"

#define	ERR_ROOT_SET	"Could not set install root from the environment."
#define	ERR_ROOT_CMD	"Command line install root contends with environment."
#define	ERR_POPTION	"no pathname included with -p option"
#define	ERR_MAXPATHS	"too many pathnames in option list (limit is %d)"
#define	ERR_NOTADIR	"spool specification <%s> must be a directory"
#define	ERR_NOTROOT	"You must be \"root\" for \"%s -f\" to execute " \
			    "properly."
#define	ERR_USAGE	"usage:\n" \
			"\t%s [-l|vqacnxf] [-R rootdir] [-p path[, ...]] " \
			    "[-i file] [options]\n" \
			"\t%s -d device [-f][-l|v] [-p path[, ...]] " \
			    "[-i file] [pkginst [...]]\n" \
			    "   where options may include ONE of the " \
			    "following:\n " \
			    "\t-m pkgmap [-e envfile]\n\tpkginst [...]\n"

char	**pkg = NULL;
int	pkgcnt = 0;
char	*basedir;
char	*pathlist[MAXPATHS], pkgspool[PATH_MAX];
short	used[MAXPATHS];
short	npaths;

int	aflag = (-1);
int	cflag = (-1);
int	vflag = 0;
int	nflag = 0;
int	lflag = 0;
int	Lflag = 0;
int	fflag = 0;
int	xflag = 0;
int	qflag = 0;
char 	*device;

static char	*allpkg[] = {"all", NULL};
static char	*mapfile,
		*spooldir,
		*envfile;
static int	errflg = 0;

static void	setpathlist(char *file);
static void	usage(void);

extern	char	**environ;

/* checkmap.c */
extern int	checkmap(int maptyp, int uninst, char *mapfile, char *envfile,
				char *pkginst);
/* scriptvfy.c */
extern int	checkscripts(char *inst_dir, int silent);

main(int argc, char *argv[])
{
	int	pkgfmt = 0;	/* Makes more sense as a pointer, but */
				/*    18N is compromised. */
	char	*tmpdir,
		file[PATH_MAX+1];
	int	c, n;
	extern int	optind;
	extern char	*optarg;
	char	*prog;

	(void) setlocale(LC_ALL, "");

#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	prog = set_prog_name(argv[0]);

	if (!set_inst_root(getenv("PKG_INSTALL_ROOT"))) {
		progerr(gettext(ERR_ROOT_SET));
		exit(1);
	}

	while ((c = getopt(argc, argv, "R:e:p:d:nLli:vam:cqxf?")) != EOF) {
		switch (c) {
		    case 'p':
			pathlist[npaths] = strtok(optarg, " , ");
			if (pathlist[npaths++] == NULL) {
				progerr(gettext(ERR_POPTION));
				exit(1);
			}
			while (pathlist[npaths] = strtok(NULL, " , ")) {
				if (npaths++ >= MAXPATHS) {
					progerr(gettext(ERR_MAXPATHS),
					    MAXPATHS);
					exit(1);
				}
			}
			break;

		    case 'd':
			device = flex_device(optarg, 1);
			break;

		    case 'n':
			nflag++;
			break;

		    case 'f':
			if (getuid()) {
				progerr(gettext(ERR_NOTROOT), prog);
				exit(1);
			}
			fflag++;
			break;

		    case 'i':
			setpathlist(optarg);
			break;

		    case 'v':
			vflag++;
			break;

		    case 'l':
			lflag++;
			break;

		    case 'L':
			Lflag++;
			break;

		    case 'x':
			if (aflag < 0)
				aflag = 0;
			if (cflag < 0)
				cflag = 0;
			xflag++;
			break;

		    case 'q':
			qflag++;
			break;

		    case 'a':
			if (cflag < 0)
				cflag = 0;
			aflag = 1;
			break;

		    case 'c':
			if (aflag < 0)
				aflag = 0;
			cflag = 1;
			break;

		    case 'e':
			envfile = optarg;
			break;

		    case 'm':
			mapfile = optarg;
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

	if (lflag || Lflag) {
		/* we're only suppose to list information */
		if ((cflag >= 0) || (aflag >= 0) ||
		qflag || xflag || fflag || nflag || vflag)
			usage();
	}

	pkg = &argv[optind];
	pkgcnt = (argc - optind);

	set_PKGpaths(get_inst_root());

	environ = NULL;		/* Sever the parent environment. */

	errflg = 0;
	if (mapfile) {
		/* check for incompatable options */
		if (device || pkgcnt)
			usage();
		put_path_params();	/* Restore what's needed. */

		if (checkmap(0, (device != NULL), mapfile, envfile, NULL))
			errflg++;
	} else if (device) {
		/* check for incompatable options */
		if ((cflag >= 0) || (aflag >= 0) || is_an_inst_root())
			usage();
		if (qflag || xflag || nflag || envfile)
			usage();

		tmpdir = NULL;
		if ((spooldir = devattr(device, "pathname")) == NULL)
			spooldir = device;
		if (isdir(spooldir)) {
			tmpdir = spooldir = qstrdup(tmpnam(NULL));
			if (fflag) {
				logerr(gettext("WRN_F_SPOOL"), pkg);
				fflag = 0;
			}
			if (mkdir(spooldir, 0755)) {
				progerr(
				    gettext("unable to make directory <%s>"),
				    spooldir);
				exit(99);
			}
			if (n = pkgtrans(device, spooldir, pkg, PT_SILENT))
				exit(n);
			pkg = gpkglist(spooldir, allpkg);
			pkgfmt = 0;
		} else {
			pkg = gpkglist(spooldir, pkgcnt ? pkg : allpkg);
			pkgfmt = 1;
		}

		/*
		 * At this point pkg[] is the list of packages to check. They
		 * are in directory format in spooldir.
		 */
		if (pkg == NULL) {
			progerr(
			    gettext("No packages selected for verification."));
			exit(1);
		}

		aflag = 0;

		for (n = 0; pkg[n]; n++) {
			int errmode;
			char locenv[PATH_MAX];

			errmode = 0;

			if (pkgfmt)
				(void) printf(
				    gettext(MSG_CHK_DIR), pkg[n], device);
			else
				(void) printf(
				    gettext(MSG_CHK_STRM), pkg[n], device);

			(void) sprintf(pkgspool, "%s/%s", spooldir, pkg[n]);
			(void) sprintf(file, "%s/install", pkgspool);
			/* Here we check the install scripts. */
			(void) printf(
			    gettext("## Checking control scripts.\n"));
			(void) checkscripts(file, 0);
			/* Verify consistency with the pkgmap. */
			(void) printf(
			    gettext("## Checking package objects.\n"));
			(void) sprintf(file, "%s/pkgmap", pkgspool);
			(void) sprintf(locenv, "%s/pkginfo", pkgspool);
			envfile = locenv;

			/*
			 * NOTE : checkmap() frees the environ data and
			 * pointer when it's through with them.
			 */
			if (checkmap(0, (device != NULL), file, envfile,
			    pkg[n]))
				errflg++;
			(void) printf(
			    gettext("## Checking is complete.\n"));
		}
		if (tmpdir)
			(void) rrmdir(tmpdir);
	} else {
		if (envfile)
			usage();

		put_path_params();	/* Restore what's needed. */

		(void) sprintf(file, "%s/contents", get_PKGADM());
		if (checkmap(1, (device != NULL), file, NULL, NULL))
			errflg++;
	}
	exit(errflg ? 1 : 0);
	/*NOTREACHED*/
#ifdef lint
	return (0);
#endif	/* lint */
}

static void
setpathlist(char *file)
{
	FILE *fplist;
	char pathname[PATH_MAX+1];

	if ((fplist = fopen(file, "r")) == NULL) {
		progerr(gettext("unable to open input file <%s>"), file);
		exit(1);
	}
	while (fscanf(fplist, "%s", pathname) == 1) {
		pathlist[npaths] = qstrdup(pathname);
		if (npaths++ > MAXPATHS) {
			progerr(
			    gettext("too many pathnames in list, limit is %d"),
			    MAXPATHS);
			exit(1);
		}
	}
	(void) fclose(fplist);
}

void
quit(int n)
{
	exit(n);
	/*NOTREACHED*/
}

static void
usage(void)
{
	char *prog = get_prog_name();

	(void) fprintf(stderr, gettext(ERR_USAGE), prog, prog);
	exit(1);
	/*NOTREACHED*/
}
