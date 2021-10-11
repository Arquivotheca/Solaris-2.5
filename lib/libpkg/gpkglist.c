/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*LINTLIBRARY*/
#ident	"@(#)gpkglist.c	1.10	93/03/23 SMI"	/* SVr4.0  1.6.3.1	*/

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <valtools.h>
#include <pkginfo.h>
#include <pkglib.h>
#include "pkglocale.h"

extern char	*pkgdir; 		/* WHERE? */

/* libadm.a */
extern CKMENU	*allocmenu(char *label, int attr);
extern int	ckitem(CKMENU *menup, char *item[], short max, char *defstr,
				char *error, char *help, char *prompt);
extern int	pkgnmchk(register char *pkg, register char *spec,
				int presvr4flg);
extern int	fpkginfo(struct pkginfo *info, char *pkginst);
extern char	*fpkginst(char *pkg, ...);
extern int	setinvis(CKMENU *menup, char *choice);
extern int	setitem(CKMENU *menup, char *choice);

#define	CMDSIZ		512
#define	LSIZE		256
#define	MAXSIZE		128
#define	MALLOCSIZ	128

#define	ERR_MEMORY	"memory allocation failure, errno=%d"
#define	ERR_NOPKG	"no package associated with <%s>"
#define	HEADER		"The following packages are available:"
#define	HELP		"Please enter the package instances you wish to " \
			"process from the list provided (or 'all' to process " \
			"all packages.)"

#define	PROMPT		"Select package(s) you wish to process (or 'all' to " \
			"process all packages)."

char **
gpkglist(char *dir, char **pkg)
{
	struct _choice_ *chp;
	struct pkginfo info;
	char	*inst;
	CKMENU	*menup;
	char	temp[LSIZE];
	char	*savedir, **nwpkg;
	int	i, n;

	savedir = pkgdir;
	pkgdir = dir;

	info.pkginst = NULL; /* initialize for memory handling */
	if (pkginfo(&info, "all", NULL, NULL)) {
		errno = ENOPKG; /* contains no valid packages */
		pkgdir = savedir;
		return (NULL);
	}
	if (pkg[0] == NULL) {
		menup = allocmenu(pkg_gt(HEADER), CKALPHA);
		if (setinvis(menup, "all")) {
			errno = EFAULT;
			return (NULL);
		}
		do {
			/* bug id 1087404 */
			if (!info.pkginst || !info.name || !info.arch ||
			    !info.version)
				continue;
			(void) sprintf(temp, "%s %s\n(%s) %s", info.pkginst,
				info.name, info.arch, info.version);
			if (setitem(menup, temp)) {
				errno = EFAULT;
				return (NULL);
			}
		} while (pkginfo(&info, "all", NULL, NULL) == 0);
		/* clear memory usage by pkginfo */
		(void) pkginfo(&info, NULL, NULL, NULL);
		pkgdir = savedir; 	/* restore pkgdir to orig value */

		nwpkg = (char **) calloc(MALLOCSIZ, sizeof (char **));
		n = ckitem(menup, nwpkg, MALLOCSIZ, "all", NULL,
		    pkg_gt(HELP), pkg_gt(PROMPT));
		if (n) {
			free(nwpkg);
			errno = ((n == 3) ? EINTR : EFAULT);
			pkgdir = savedir;
			return (NULL);
		}
		if (!strcmp(nwpkg[0], "all")) {
			chp = menup->choice;
			for (n = 0; chp; /* void */) {
				nwpkg[n] = strdup(chp->token);
				if ((++n % MALLOCSIZ) == 0) {
					nwpkg = (char **) realloc(nwpkg,
						(n+MALLOCSIZ)* sizeof (char**));
					if (nwpkg == NULL) {
						progerr(pkg_gt(ERR_MEMORY),
						    errno);
						errno = ENOMEM;
						return (NULL);
					}
				}
				chp = chp->next;
				nwpkg[n] = NULL;
			}
		} else {
			for (n = 0; nwpkg[n]; n++)
				nwpkg[n] = strdup(nwpkg[n]);
		}
		(void) setitem(menup, NULL); /* free resources */
		free(menup);
		pkgdir = savedir;
		return (nwpkg);
	}
	(void) pkginfo(&info, NULL, NULL, NULL);
		/* clear memory usage by pkginfo */

	nwpkg = (char **) calloc(MALLOCSIZ, sizeof (char **));

	/*
	 * pkg array contains the instance identifiers to
	 * be selected, or possibly wildcard definitions
	 */
	i = n = 0;
	do {
		if (pkgnmchk(pkg[i], "all", 1)) {
			/* wildcard specification */
			(void) fpkginst(NULL);
			inst = fpkginst(pkg[i], NULL, NULL);
			if (inst == NULL) {
				progerr(pkg_gt(ERR_NOPKG), pkg[i]);
				free(nwpkg);
				nwpkg = NULL;
				errno = ESRCH;
				break;
			}
			do {
				nwpkg[n] = strdup(inst);
				if ((++n % MALLOCSIZ) == 0) {
					nwpkg = (char **) realloc(nwpkg,
						(n+MALLOCSIZ)* sizeof (char**));
					if (nwpkg == NULL) {
						progerr(pkg_gt(ERR_MEMORY),
						    errno);
						errno = ENOMEM;
						return (NULL);
					}
				}
				nwpkg[n] = NULL;
			} while (inst = fpkginst(pkg[i], NULL, NULL));
		} else {
			if (fpkginfo(&info, pkg[i])) {
				progerr(pkg_gt(ERR_NOPKG), pkg[i]);
				free(nwpkg);
				nwpkg = NULL;
				errno = ESRCH;
				break;
			}
			nwpkg[n] = strdup(pkg[i]);
			if ((++n % MALLOCSIZ) == 0) {
				nwpkg = (char **) realloc(nwpkg,
					(n+MALLOCSIZ)* sizeof (char**));
				if (nwpkg == NULL) {
					progerr(pkg_gt(ERR_MEMORY), errno);
					errno = ENOMEM;
					return (NULL);
				}
			}
			nwpkg[n] = NULL;
		}
	} while (pkg[++i]);

	(void) fpkginst(NULL);
	(void) fpkginfo(&info, NULL);
	pkgdir = savedir; 	/* restore pkgdir to orig value */
	return (nwpkg);
}
