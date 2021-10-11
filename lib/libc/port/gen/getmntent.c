/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)getmntent.c	1.15	94/11/15 SMI"	/* SVr4.0 1.4	*/

/*LINTLIBRARY*/
#ifdef __STDC__
#pragma weak getmntany = _getmntany
#pragma weak getmntent = _getmntent
#pragma weak hasmntopt = _hasmntopt
#endif
#include	"synonyms.h"
#include	<stdio.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<sys/mnttab.h>
#include 	<errno.h>
#include	<stdlib.h>
#include	<thread.h>
#include	<synch.h>
#include	<mtlib.h>

#define	GETTOK(xx, ll)\
	if ((mp->xx = strtok(ll, sepstr)) == NULL)\
		return (MNT_TOOFEW);\
	if (strcmp(mp->xx, dash) == 0)\
		mp->xx = NULL
#define	GETTOK_R(xx, ll, tmp)\
	if ((mp->xx = (char *)strtok_r(ll, sepstr, tmp)) == NULL)\
		return (MNT_TOOFEW);\
	if (strcmp(mp->xx, dash) == 0)\
		mp->xx = NULL
#define	DIFF(xx)\
	(mrefp->xx != NULL && (mgetp->xx == NULL ||\
	    strcmp(mrefp->xx, mgetp->xx) != 0))
#define	SDIFF(xx, typem, typer)\
	(mgetp->xx == NULL || stat(mgetp->xx, &statb) == -1 ||\
	(statb.st_mode & S_IFMT) != typem ||\
	    statb.st_rdev != typer)

extern char	*fgets();
extern char	*strtok();

static char	*statline = NULL;
#ifdef __STDC__
static const char	sepstr[] = " \t\n";
static const char	dash[] = "-";
#else
static  char	sepstr[] = " \t\n";
static  char	dash[] = "-";
#endif

int
getmntany(fd, mgetp, mrefp)
	register FILE	*fd;
	register struct	mnttab	*mgetp;
	register struct mnttab	*mrefp;
{
	register int	ret, bstat;
	register mode_t	bmode;
	register dev_t	brdev;
	struct stat	statb;

	if (mrefp->mnt_special && stat(mrefp->mnt_special, &statb) == 0 &&
	    ((bmode = (statb.st_mode & S_IFMT)) == S_IFBLK ||
	    bmode == S_IFCHR)) {
		bstat = 1;
		brdev = statb.st_rdev;
	} else
		bstat = 0;

	while ((ret = getmntent(fd, mgetp)) == 0 &&
		((bstat == 0 && DIFF(mnt_special)) ||
		(bstat == 1 && SDIFF(mnt_special, bmode, brdev)) ||
		DIFF(mnt_mountp) ||
		DIFF(mnt_fstype) ||
		DIFF(mnt_mntopts) ||
		DIFF(mnt_time)))
		;

	return (ret);
}

int
getmntent(fd, mp)
	register FILE	*fd;
	register struct mnttab	*mp;
{
	register int	ret;
	static	thread_key_t	mnt_key = 0;
	char	*tmp, *line;

	if (_thr_main()) {
		if (statline == NULL)
			statline = (char *)malloc(MNT_LINE_MAX);
		line = statline;
	} else {
		line = (char *)_tsdalloc(&mnt_key, MNT_LINE_MAX);
	}

	if (line == 0) {
		errno = ENOMEM;
		return (0);
	}

	/* skip leading spaces and comments */
	if ((ret = getline(line, fd)) != 0)
		return (ret);

	/* split up each field */
	GETTOK_R(mnt_special, line, &tmp);
	GETTOK_R(mnt_mountp, NULL, &tmp);
	GETTOK_R(mnt_fstype, NULL, &tmp);
	GETTOK_R(mnt_mntopts, NULL, &tmp);
	GETTOK_R(mnt_time, NULL, &tmp);

	/* check for too many fields */
	if ((char *)strtok_r(NULL, sepstr, &tmp) != NULL)
		return (MNT_TOOMANY);

	return (0);
}

static int
getline(lp, fd)
	register char	*lp;
	register FILE	*fd;
{
	register int	ret;
	register char	*cp;

	while ((lp = fgets(lp, MNT_LINE_MAX, fd)) != NULL) {
		if (strlen(lp) == MNT_LINE_MAX-1 && lp[MNT_LINE_MAX-2] != '\n')
			return (MNT_TOOLONG);

		for (cp = lp; *cp == ' ' || *cp == '\t'; cp++)
			;

		if (*cp != '#' && *cp != '\n')
			return (0);
	}
	return (-1);
}

int
mntopt(p)
char **p;
{
char *cp = *p;
char *retstr;

	while (*cp && isspace(*cp))
		cp++;

	retstr = cp;
	while (*cp && *cp != ',')
		cp++;

	if (*cp) {
		*cp = '\0';
		cp++;
	}

	*p = cp;
	return ((int)retstr);
}

char *
hasmntopt(mnt, opt)
register struct mnttab *mnt;
register char *opt;
{
	char tmpopts[256];
	char *f, *opts = tmpopts;

	strcpy(opts, mnt->mnt_mntopts);
	f = (char *)mntopt(&opts);
	for (; *f; f = (char *)mntopt(&opts)) {
		if (strncmp(opt, f, strlen(opt)) == 0)
			return (f - tmpopts + mnt->mnt_mntopts);
	}
	return (NULL);
}
