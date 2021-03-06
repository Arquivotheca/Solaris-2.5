/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/
/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ident  "@(#)checkmap.c	1.21	95/01/18 SMI"	/* SVr4.0  1.2.3.1	*/

/*  5-20-92	added newroot function */

#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <pkgstrct.h>
#include <pkglocs.h>
#include <locale.h>
#include <libintl.h>
#include <pkglib.h>
#include "libadm.h"
#include "libinst.h"

extern int	qflag, lflag, Lflag, pkgcnt;
extern short	npaths;

extern char	*errstr, *basedir, *pathlist[], **pkg, **environ;

extern short	used[];

/* ocfile.c */
extern int	socfile(FILE **fp);	/* simple open & lock of DB. */
extern int	relslock(void);		/* unlock the database. */

/* ckentry.c */
extern int	ckentry(int envflag, int maptyp, struct cfent *ept, FILE *fp);

#define	nxtentry(p) \
		(maptyp ? srchcfile(p, "*", fp, NULL) : gpkgmap(p, fp))

#define	MALLSIZ		50

#define	MSG_ARCHIVE	"NOTE: some pathnames are in private formats " \
			    "which cannot be verified"
#define	WRN_NOPKG	"WARNING: no pathnames were associated with <%s>"
#define	WRN_NOPATH	"WARNING: no information associated with pathname <%s>"
#define	ERR_NOENTRIES	"no entries processed for <%s>"
#define	ERR_NOMEM	"unable to allocate dynamic memory, errno=%d"
#define	ERR_PKGMAP	"unable to open pkgmap file <%s>"
#define	ERR_ENVFILE	"unable to open environment file <%s>"

static struct cfent entry;

static int	shellmatch(char *spec, char *path);
static int	selpath(char *path);
static int	selpkg(char *p);

/*
 * This routine checks all files which are referenced in the pkgmap which is
 * identified by the mapfile arg. When the package is installed, the mapfile
 * may be the contents file or a separate pkgmap (maptyp tells the function
 * which it is). The variable uninst tells the function whether the package
 * is in the installed state or not. The envfile entry is usually a pkginfo
 * file, but it could be any environment parameter list.
 */

int
checkmap(int maptyp, int uninst, char *mapfile, char *envfile, char *pkginst)
{
	FILE *fp;
	struct pinfo *pinfo;
	int	n, count, selected, errflg;
	char	param[64], *value;
	char	*cl = NULL;

	if (envfile != NULL) {
		if ((fp = fopen(envfile, "r")) == NULL) {
			progerr(gettext(ERR_ENVFILE), envfile);
			return (-1);
		}
		param[0] = '\0';
		while (value = fpkgparam(fp, param)) {
			if (strcmp("PATH", param) != 0) {
				/*
				 * If checking an uninstalled package, we
				 * only want two parameters. If we took all
				 * of them, including path definitions, we
				 * wouldn't be looking in the right places in
				 * the reloc and root directories.
				 */
				if (uninst) {
					if ((strncmp("PKG_SRC_NOVERIFY", param,
					    16) == 0) && value && *value) {
						logerr(gettext(MSG_ARCHIVE));
						putparam(param, value);
					}
					if ((strncmp("CLASSES", param,
					    7) == 0) && value && *value)
						putparam(param, value);
				} else
					putparam(param, value);
			}
			free(value);
			param[0] = '\0';
		}
		(void) fclose(fp);
		basedir = getenv("BASEDIR");
	}

	/*
	 * If we are using a contents file for the map, this locks the
	 * contents file in order to freeze the database and assure it
	 * remains synchronized with the file system against which it is
	 * being compared. There is no practical way to lock another pkgmap
	 * on some unknown medium so we don't bother.
	 */
	if (maptyp) {	/* If this is the contents file. */
		if (!socfile(&fp)) {
			progerr(gettext(ERR_PKGMAP), "contents");
			return (-1);
		}
	} else {
		if ((fp = fopen(mapfile, "r")) == NULL) {
			progerr(gettext(ERR_PKGMAP), mapfile);
			return (-1);
		}
	}

	if ((cl = getenv("CLASSES")) != NULL)
		cl_sets(qstrdup(cl));

	errflg = count = 0;
	while ((n = nxtentry(&entry)) != 0) {
		if (n < 0) {
			logerr(gettext("ERROR: garbled entry"));
			logerr(gettext("pathname: %s"),
			    (entry.path && *entry.path) ? entry.path :
			    "Unknown");
			logerr(gettext("problem: %s"),
			    (errstr && *errstr) ? errstr : "Unknown");
			exit(99);
		}
		if (n == 0)
			break; /* done with file */

		/*
		 * The class list may not be complete for good reason, so
		 * there's no complaining if this returns an index of -1.
		 */
		if (cl != NULL)
			entry.pkg_class_idx = cl_idx(entry.pkg_class);

		if (maptyp) {
			/*
			 * check to see if the entry we just read
			 * is associated with one of the packages
			 * we have listed on the command line
			 */
			selected = 0;
			pinfo = entry.pinfo;
			while (pinfo) {
				if (selpkg(pinfo->pkg)) {
					selected++;
					break;
				}
				pinfo = pinfo->next;
			}
			if (!selected)
				continue; /* not selected */
		}

		/*
		 * Check to see if the pathname associated with the entry
		 * we just read is associated with the list of paths we
		 * supplied on the command line
		 */
		if (!selpath(entry.path))
			continue; /* not selected */

		/*
		 * Determine if this is a package object wanting
		 * verification. Metafiles are always checked, otherwise, we
		 * rely on the class to discriminate.
		 */
		if (entry.ftype != 'i')
			/* If there's no class list... */
			if (cl != NULL)
				/*
				 * ... or this entry isn't in that class list
				 * or it's in a private format, then don't
				 * check it.
				 */
				if (entry.pkg_class_idx == -1 ||
				    cl_svfy(entry.pkg_class_idx) == NOVERIFY)
					continue;

		count++;
		if (ckentry((envfile ? 1 : 0), maptyp, &entry, fp))
			errflg++;
	}
	(void) fclose(fp);

	if (maptyp)
		relslock();

	if (envfile) {
		/* free up environment resources */
		for (n = 0; environ[n]; n++)
			free(environ[n]);
		free(environ);
		environ = NULL;
	}
	if (!count && pkginst) {
		progerr(gettext(ERR_NOENTRIES), pkginst);
		errflg++;
	}
	if (maptyp) {
		/*
		 * make sure each listed package was associated with
		 * an entry from the prototype or pkgmap
		 */
		(void) selpkg(NULL);
	}
	if (!qflag && !lflag && !Lflag) {
		/*
		 * make sure each listed pathname was associated with an entry
		 * from the prototype or pkgmap
		 */
		(void) selpath(NULL);
	}
	return (errflg);
}

static int
selpkg(char *p)
{
	static char *selected;
	register int i;

	if (p == NULL) {
		if (selected == NULL) {
			if (pkgcnt) {
				for (i = 0; i < pkgcnt; ++i)
					logerr(gettext(WRN_NOPKG), pkg[i]);
			}
		} else {
			for (i = 0; i < pkgcnt; ++i)
				if (selected[i] == NULL)
					logerr(gettext(WRN_NOPKG), pkg[i]);
		}
		return (0); /* return value not important */
	} else if (pkgcnt == 0)
		return (1);
	else if (selected == NULL) {
		selected =
		    (char *) calloc((unsigned) (pkgcnt+1), sizeof (char));
		if (selected == NULL) {
			progerr(gettext(ERR_NOMEM), errno);
			exit(99);
			/*NOTREACHED*/
		}
	}

	for (i = 0; i < pkgcnt; ++i) {
		if (pkgnmchk(p, pkg[i], 0) == 0) {
			if (selected != NULL)
				selected[i] = 'b';
			return (1);
		}
	}
	return (0);
}

static int
selpath(char *path)
{
	int n;

	if (!npaths)
		return (1); /* everything is selectable */

	for (n = 0; n < npaths; n++) {
		if (path == NULL) {
			if (!used[n])
				logerr(gettext(WRN_NOPATH), pathlist[n]);
		} else if (!shellmatch(pathlist[n], path)) {
			used[n] = 1;
			return (1);
		}
	}
	return (0); /* not selected */
}

static int
shellmatch(char *spec, char *path)
{
	while (*spec && (*spec == *path)) {
		spec++, path++;
	}
	if ((*spec == *path) || (*spec == '*'))
		return (0);
	return (1);
}
