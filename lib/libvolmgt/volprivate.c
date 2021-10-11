/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)volprivate.c	1.6	94/11/01 SMI"

/*
 * routines in this module are meant to be called by other libvolmgt
 * routines only
 */

#include	<stdio.h>
#include	<string.h>
#include	<dirent.h>
#include	<fcntl.h>
#include	<string.h>
#include	<errno.h>
#include	<libintl.h>
#include	<limits.h>
#include	<unistd.h>
#include	<stdlib.h>
#include	<ctype.h>
#include	<volmgt.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<sys/dkio.h>
#include	<sys/param.h>
#include	<sys/wait.h>
#include	<sys/mnttab.h>
#include	<sys/vol.h>

#include	"volmgt_private.h"

/*
 * We have been passed a path which (presumably) is a volume.
 * We look through the directory until we find a name which is
 * a character device.
 */
char *
getrawpart0(char *path)
{
	DIR		*dirp = NULL;
	struct dirent	*dp;
	static char	fname[MAXNAMLEN+1];
	struct stat	sb;
	char		*res;



	/* open the directory */
	if ((dirp = opendir(path)) == NULL) {
		res = NULL;
		goto dun;
	}

	/* scan the directory */
	while (dp = readdir(dirp)) {

		/* skip "." and ".." */
		if (strcmp(dp->d_name, ".") == 0) {
			continue;
		}
		if (strcmp(dp->d_name, "..") == 0) {
			continue;
		}

		/* create a pathname for this device */
		(void) sprintf(fname, "%s/%s", path, dp->d_name);
		if (stat(fname, &sb) < 0) {
			continue;		/* this shouldn't happen */
		}
		/* check for a char-spcl device */
		if (S_ISCHR(sb.st_mode)) {
			res = strdup(fname);
			goto dun;
		}
	}

	/* raw part not found */
	res = NULL;
dun:
	if (dirp != NULL) {
		closedir(dirp);
	}
	return (res);
}


/*
 * fix the getfull{raw,blk}name problem for the fd and diskette case
 *
 * return value is malloc'ed, and must be free'd
 *
 * no match gets a malloc'ed null string
 */

char *
volmgt_getfullblkname(char *n)
{
	extern char	*getfullblkname(char *);
	char		*rval;
	char		namebuf[PATH_MAX+1];
	char		*s;
	int		c;
	char		*res;



	/* try to get full block-spcl device name */
	rval = getfullblkname(n);
	if ((rval != NULL) && (*rval != NULLC)) {
		/* found it */
		res = rval;
		goto dun;
	}

	/* we have a null-string result */
	if (rval != NULL) {
		/* free null string */
		free(rval);
	}

	/* ok, so we either have a bad device or a floppy */

	/* try the rfd# form */
	if ((s = strstr(n, "/rfd")) != NULL) {
		c = *++s;			/* save the 'r' */
		*s = NULLC;			/* replace it with a null */
		(void) strcpy(namebuf, n);	/* save first part of it */
		*s++ = c;			/* give the 'r' back */
		(void) strcat(namebuf, s);	/* copy, skipping the 'r' */
		res = strdup(namebuf);
		goto dun;
	}

	/* try the rdiskette form */
	if ((s = strstr(n, "/rdiskette")) != NULL) {
		c = *++s;			/* save the 'r' */
		*s = NULLC;			/* replace it with a null */
		(void) strcpy(namebuf, n);	/* save first part of it */
		*s++ = c;			/* give the 'r' back */
		(void) strcat(namebuf, s);	/* copy, skipping the 'r' */
		res = strdup(namebuf);
		goto dun;
	}

	/* no match found */
	res = strdup("");

dun:
	return (res);
}


char *
volmgt_getfullrawname(char *n)
{
	extern char	*getfullrawname(char *);
	char		*rval;
	char		namebuf[PATH_MAX+1];
	char		*s;
	int		c;
	char		*res;


	/* try to get full char-spcl device name */
	rval = getfullrawname(n);
	if ((rval != NULL) && (*rval != NULLC)) {
		/* found it */
		res = rval;
		goto dun;
	}

	/* we have a null-string result */
	if (rval) {
		/* free null string */
		free(rval);
	}

	/* ok, so we either have a bad device or a floppy */

	/* try the fd# form */
	if ((s = strstr(n, "/fd")) != NULL) {
		c = *++s;			/* save the 'f' */
		*s = NULLC;			/* replace it with a null */
		(void) strcpy(namebuf, n);	/* save first part of it */
		*s = c;				/* put the 'f' back */
		(void) strcat(namebuf, "r");	/* insert an 'r' */
		(void) strcat(namebuf, s);	/* copy the rest */
		res = strdup(namebuf);
		goto dun;
	}

	/* try the diskette form */
	if ((s = strstr(n, "/diskette")) != NULL) {
		c = *++s;			/* save at 'd' */
		*s = NULLC;			/* replace it with a null */
		(void) strcpy(namebuf, n);	/* save first part */
		*s = c;				/* put the 'd' back */
		(void) strcat(namebuf, "r");	/* insert an 'r' */
		(void) strcat(namebuf, s);	/* copy the rest */
		res = strdup(namebuf);
		goto dun;
	}

	/* no match found */
	res = strdup("");
dun:
	return (res);
}


/*
 * take a name of the form /dev/{rdsk,dsk}/c#t# and complete the name
 * using "reasonable" defaults.
 *
 * result (if non-null) is malloc'ed and must be free'd
 */
char *
volmgt_completename(char *name)
{
	char 	*s;
	int	ctlr = 0;
	int	targ = 0;
	int	disk = 0;
	int	slice = 0;
	char	namebuf[MAXPATHLEN+1];
	int	c;


	/* look for "...dsk/..." */
	if ((s = strstr(name, "dsk/")) == NULL) {
		return (NULL);
	}

	/* try to get controller, target, disk, and slice */
	(void) sscanf(s, "dsk/c%dt%dd%ds%d", &ctlr, &targ, &disk, &slice);

	/* skip to the '/' in "...dsk/..." */
	s += strlen("dsk");

	/* replace '/' with a null */
	c = *s;
	*s = NULLC;

	/* build up the name again, filling in where not present earlier */
	(void) sprintf(namebuf, "%s/c%dt%dd%ds%d", name, ctlr, targ, disk,
	    slice);

	/* put the '/' back */
	*s = c;

	/* return result */
	return (strdup(namebuf));
}
