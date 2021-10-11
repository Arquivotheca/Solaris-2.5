/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/
/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ident	"@(#)main.c	1.25	95/01/18 SMI"	/* SVr4.0 1.13.3.1 */

/*  5-20-92	added newroot function */

#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <pkginfo.h>
#include <pkgstrct.h>
#include <pkglocs.h>
#include <pkglib.h>
#include <locale.h>
#include <libintl.h>
#include "install.h"
#include "libinst.h"
#include "libadm.h"

extern char	*errstr;
extern char	dbst; 			/* libinst/pkgdbmerg.c */

#define	BASEDIR	"/BASEDIR/"

#define	INSTALF	(*prog == 'i')
#define	REMOVEF	(*prog == 'r')

#define	ERR_ROOT_SET	"Could not set install root from the environment."
#define	ERR_ROOT_CMD	"Command line install root contends with environment."
#define	ERR_CLASSLONG	"classname argument too long"
#define	ERR_CLASSCHAR	"bad character in classname"
#define	ERR_INVAL	"package instance <%s> is invalid"
#define	ERR_NOTINST	"package instance <%s> is not installed"
#define	ERR_MERG	"unable to merge contents file"
#define	ERR_SORT	"unable to sort contents file"
#define	ERR_NOTROOT	"You must be \"root\" for %s to execute properly."
#define	ERR_USAGE0	"usage:\n" \
			"\t%s pkginst path [path ...]\n\t%s -f pkginst\n"
#define	ERR_USAGE1	"usage:\n" \
			"\t%s [-c class] <pkginst> <path>\n" \
			"\t%s [-c class] <pkginst> <path> <specs>\n" \
			"\t   where <specs> may be defined as:\n" \
			"\t\tf <mode> <owner> <group>\n" \
			"\t\tv <mode> <owner> <group>\n" \
			"\t\te <mode> <owner> <group>\n" \
			"\t\td <mode> <owner> <group>\n" \
			"\t\tx <mode> <owner> <group>\n" \
			"\t\tp <mode> <owner> <group>\n" \
			"\t\tc <major> <minor> <mode> <owner> <group>\n" \
			"\t\tb <major> <minor> <mode> <owner> <group>\n" \
			"\t\ts <path>=<srcpath>\n" \
			"\t\tl <path>=<srcpath>\n" \
			"\t%s [-c class] -f pkginst\n"

#define	CMD_SORT	"sort +0 -1"

char	*classname = NULL;

struct cfextra **extlist;

char	*pkginst;
int	eptnum, sortflag, nointeract, nosetuid, nocnflct;
int	warnflag = 0;

void	quit(int retcode);
void	usage(void);

/* ocfile.c */
extern int	relslock(void);	/* Release database lock. */

/* removef.c */
extern void	removef(int argc, char *argv[]);

/* installf.c */
extern int	installf(int argc, char *argv[]);

/* dofinal.c */
extern int	dofinal(FILE *fp, FILE *fpo, int rmflag, char *myclass);

main(int argc, char **argv)
{
	FILE	*mapfp, *tmpfp, *pp;
	struct mergstat *mstat;
	struct cfent *ept;
	char	*pt;
	int	c, n, dbchg, err;
	int	fflag = 0;
	char	*cmd;
	char	line[1024];
	char	*prog;

	char	outbuf[PATH_MAX];

	extern char	*optarg;
	extern int	optind;

	(void) signal(SIGHUP, exit);
	(void) signal(SIGINT, exit);
	(void) signal(SIGQUIT, exit);

	(void) setlocale(LC_ALL, "");

#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	prog = set_prog_name(argv[0]);

	if (getuid()) {
		progerr(gettext(ERR_NOTROOT), prog);
		exit(1);
	}

	get_mntinfo();

	if (!set_inst_root(getenv("PKG_INSTALL_ROOT"))) {
		progerr(gettext(ERR_ROOT_SET));
		exit(1);
	}

	while ((c = getopt(argc, argv, "c:fR:?")) != EOF) {
		switch (c) {
		    case 'f':
			fflag++;
			break;

		    case 'c':
			classname = optarg;
			/* validate that classname is acceptable */
			if (strlen(classname) > (size_t)CLSSIZ) {
				progerr(gettext(ERR_CLASSLONG));
				exit(1);
			}
			for (pt = classname; *pt; pt++) {
				if (!isalpha(*pt) && !isdigit(*pt)) {
					progerr(gettext(ERR_CLASSCHAR));
					exit(1);
				}
			}
			break;

		    case 'R':	/* added for newroot option */
			if (!set_inst_root(optarg)) {
				progerr(gettext(ERR_ROOT_CMD));
				exit(1);
			}
			break;

		    default:
			usage();
		}
	}

	(void) set_PKGpaths(get_inst_root());

	sortflag = 0;
	if ((pkginst = argv[optind++]) == NULL)
		usage();

	/*
	 * The following is used to setup the environment. Note that the
	 * variable 'BASEDIR' is only meaningful for this utility if there
	 * is an install root, recorded in PKG_INSTALL_ROOT. Otherwise, this
	 * utility can create a file or directory anywhere unfettered by
	 * the basedir associated with the package instance.
	 */
	if ((err = set_basedirs(0, NULL, pkginst, 1)) != 0)
		quit(err);

	if (INSTALF)
		mkbasedir(0, get_basedir());

	if (fflag) {
		/* installf and removef must only have pkginst */
		if (optind != argc)
			usage();
	} else {
		/*
		 * installf and removef must have at minimum
		 * pkginst & pathname specified on command line
		 */
		if (optind >= argc)
			usage();
	}
	if (REMOVEF) {
		if (classname)
			usage();
	}
	if (pkgnmchk(pkginst, "all", 0)) {
		progerr(gettext(ERR_INVAL), pkginst);
		exit(1);
	}
	if (fpkginst(pkginst, NULL, NULL) == NULL) {
		progerr(gettext(ERR_NOTINST), pkginst);
		exit(1);
	}

	if (!ocfile(&mapfp, &tmpfp, 0L))
		exit(1);

	if (fflag)
		dbchg = dofinal(mapfp, tmpfp, REMOVEF, classname);
	else {
		if (INSTALF) {
			dbst = INST_RDY;
			if (installf(argc-optind, &argv[optind]))
				quit(1);
		} else {
			dbst = RM_RDY;
			removef(argc-optind, &argv[optind]);
		}

		/*
		 * alloc an array to hold information about how each
		 * entry in memory matches with information already
		 * stored in the "contents" file
		 */
		mstat = (struct mergstat *) calloc((unsigned int)eptnum,
			sizeof (struct mergstat));

		dbchg = pkgdbmerg(mapfp, tmpfp, extlist, mstat, 0);
		if (dbchg < 0) {
			progerr(gettext(ERR_MERG));
			quit(99);
		}
	}
	if (dbchg) {
		if ((n = swapcfile(mapfp, tmpfp, pkginst)) == RESULT_WRN)
			warnflag++;
		else if (n == RESULT_ERR)
			quit(99);

		relslock();	/* Unlock the database. */
	}

	if (REMOVEF && !fflag) {
		for (n = 0; extlist[n]; n++) {
			ept = &(extlist[n]->cf_ent);

			if (!mstat[n].shared) {
				/*
				 * Only output paths that can be deleted.
				 * so need to skip if the object is owned
				 * by a remote server.
				 */
				if (ept->pinfo &&
				    (ept->pinfo->status == '%'))
					continue;

				c = 0;
				if (is_a_cl_basedir() &&
				    !is_an_inst_root()) {
					c = strlen(get_client_basedir());
					(void) sprintf(outbuf, "%s/%s\n",
					    get_basedir(),
					    &(ept->path[c]));
				} else if (is_an_inst_root())
					(void) sprintf(outbuf, "%s/%s\n",
					    get_inst_root(),
					    &(ept->path[c]));
				else
					(void) sprintf(outbuf, "%s\n",
					    &(ept->path[c]));
				canonize(outbuf);
				(void) printf("%s", outbuf);
			}
		}
	} else if (INSTALF && !fflag) {
		for (n = 0; extlist[n]; n++) {
			ept = &(extlist[n]->cf_ent);

			if (strchr("dxcbp", ept->ftype)) {
				(void) averify(1, &ept->ftype,
				    ept->path, &ept->ainfo);
			}
		}
	}

	/* Sort the contents files if needed */
	if (sortflag) {
		int n;

		warnflag += (ocfile(&mapfp, &tmpfp, 0L)) ? 0 : 1;
		if (!warnflag) {
			cmd = (char *)malloc(strlen(CMD_SORT) +
				strlen(get_PKGADM()) + strlen("/contents") + 5);
			(void) sprintf(cmd,
			    "%s %s/contents", CMD_SORT, get_PKGADM());
			pp = popen(cmd, "r");
			if (pp == NULL) {
				(void) fclose(mapfp);
				(void) fclose(tmpfp);
				free(cmd);
				progerr(gettext(ERR_SORT));
				return (1);
			}
			while (fgets(line, 1024, pp) != NULL) {
				if (line[0] != DUP_ENTRY)
					(void) fputs(line, tmpfp);
			}
			free(cmd);
			(void) pclose(pp);
			if ((n = swapcfile(mapfp, tmpfp, pkginst)) ==
			    RESULT_WRN)
				warnflag++;
			else if (n == RESULT_ERR)
				quit(99);

			relslock();	/* Unlock the database. */
		}
	}
	return (warnflag ? 1 : 0);
}

void
quit(int n)
{
	exit(n);
}

void
usage(void)
{
	char *prog = get_prog_name();

	if (REMOVEF) {
		(void) fprintf(stderr, gettext(ERR_USAGE0), prog, prog);
	} else {
		(void) fprintf(stderr, gettext(ERR_USAGE1), prog, prog, prog);
	}
	exit(1);
}
