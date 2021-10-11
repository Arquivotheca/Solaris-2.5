#ident	"@(#)dbinfo.c 1.5 93/07/12"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include <config.h>
#include "defs.h"
#include "rpcdefs.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/stat.h>
#ifdef USG
/*
 * make mnttab look like mstab
 */
#include <sys/mnttab.h>

#define	setmntent	fopen
#define	endmntent	fclose
#define	mntent		mnttab
#define	mnt_fsname	mnt_special
#define	mnt_dir		mnt_mountp
#define	mnt_type	mnt_fstype
#define	MOUNTED		MNTTAB

static struct mntent *
mygetmntent(f, name)
	FILE *f;
	char *name;
{
	static struct mntent mt;
	int status;

	if ((status = getmntent(f, &mt)) == 0)
		return (&mt);

	switch (status) {
	case EOF:	break;		/* normal exit condition */
	case MNT_TOOLONG:
		fprintf(stderr, gettext("%s has a line that is too long\n"),
			name);
		break;
	case MNT_TOOMANY:
		fprintf(stderr, gettext(
			"%s has a line with too many entries\n"), name);
		break;
	case MNT_TOOFEW:
		fprintf(stderr, gettext(
			"%s has a line with too few entries\n"), name);
		break;
	default:
		fprintf(stderr, gettext(
			"Unknown return code, %d, from getmntent() on %s\n"),
			status, name);
		break;
	}

	return (NULL);
}
#else
#include <mntent.h>

#define	mygetmntent	getmntent
#endif

/*
 * Support for info about the filesystem the database lives on
 */

/*ARGSUSED*/
bool_t
xdr_dbinfo(xdrs, notused)
	XDR *xdrs;
	void *notused;
{
	char hostname[BCHOSTNAMELEN];
	char cwd[MAXPATHLEN];
	struct stat statbuf;
	dev_t mydev;
	ino_t curino;
	char mntpath[MAXPATHLEN];
	FILE *mnttab;
	struct mntent *ment;
	char *rbuf;
	long size;

	if (gethostname(hostname, BCHOSTNAMELEN) < 0) {
		(void) fprintf(stderr, gettext("cannot determine host name\n"));
		return (FALSE);
	}

	if (getcwd(cwd, MAXPATHLEN) == NULL) {
		(void) fprintf(stderr, gettext(
		    "cannot determine initial current working directory\n"));
		return (FALSE);
	}

	if (stat(".", &statbuf) < 0) {
		(void) fprintf(stderr, gettext(
			"cannot obtain current working directory status\n"));
		return (FALSE);
	}
	mydev = statbuf.st_dev;
	curino = statbuf.st_ino;
	for (;;) {
		if (stat("..", &statbuf) < 0) {
			(void) fprintf(stderr, gettext(
				"cannot obtain parent directory status\n"));
			return (FALSE);
		}
		if (statbuf.st_dev != mydev || statbuf.st_ino == curino)
		    break;
		curino = statbuf.st_ino;

		if (chdir("..") < 0) {
			(void) fprintf(stderr,
				gettext("cannot cd to parent directory\n"));
			return (FALSE);
		}
	}
	if (getcwd(mntpath, MAXPATHLEN) == NULL) {
		(void) fprintf(stderr, gettext(
			"cannot determine new current working directory\n"));
		return (FALSE);
	}

	if ((mnttab = setmntent(MOUNTED, "r")) == NULL) {
		(void) fprintf(stderr,
			gettext("cannot open mount table `%s'\n"), MOUNTED);
		return (FALSE);
	}
	while ((ment = mygetmntent(mnttab, MOUNTED)) != NULL) {
		if (ment->mnt_dir && strcmp(ment->mnt_dir, mntpath) == 0)
			break;
	}
	if (ment == NULL) {
		(void) endmntent(mnttab);
		(void) fprintf(stderr, gettext(
			"cannot find mount table entry for %s\n"), mntpath);
		return (FALSE);
	}

	if (ment->mnt_fsname == (char *)0)
		ment->mnt_fsname = "";
	rbuf = malloc((unsigned)strlen(hostname)+strlen(cwd)+strlen(mntpath)+
			strlen(ment->mnt_fsname)+5);
	if (rbuf == (char *)0) {
		(void) endmntent(mnttab);
		(void) fprintf(stderr,
			gettext("%s: out of memory\n"), "xdr_dbinfo");
		return (FALSE);
	}
	(void) sprintf(rbuf, "%s %s %s %s\n", hostname, cwd, mntpath,
			ment->mnt_fsname);
	size = strlen(rbuf)+1;
	(void) endmntent(mnttab);

	if (!xdr_bytes(xdrs, &rbuf, (u_int *)&size, MAXPATHLEN))
		return (FALSE);
	return (TRUE);
}
