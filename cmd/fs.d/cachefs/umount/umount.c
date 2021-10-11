/*
 * -----------------------------------------------------------------
 *
 *			umount.c
 *
 * CFS specific umount command.
 */

#pragma ident "@(#)umount.c   1.11     94/11/11 SMI"

/*
 *  Copyright (c) 1994, by Sun Microsystems, Inc.
 *	All rights reserved.
 */

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <varargs.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <wait.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/mount.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#include <sys/fs/cachefs_fs.h>
#include <fslib.h>

/* forward references */
void pr_err(char *fmt, ...);
void usage(char *msgp);

/*
 * -----------------------------------------------------------------
 *
 *			main
 *
 * Description:
 *	Main routine for the cfs umount program.
 * Arguments:
 *	argc	number of command line arguments
 *	argv	list of command line arguments
 * Returns:
 *	Returns 0 for success or 1 if an error occurs.
 * Preconditions:
 */

int
main(int argc, char **argv)
{
	char *strp;
	int xx;
	int ret;
	struct mnttab mget;
	struct mnttab mref;
	FILE *finp;
	char mnt_front[PATH_MAX];
	char mnt_back[PATH_MAX];
	char *p, mnt_frontns[PATH_MAX];

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN	"SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	/* must be root */
	if (geteuid() != 0) {
		pr_err(gettext("must be run by root"));
		return (1);
	}

	/* make sure what to unmount is specified */
	if (argc != 2) {
		usage("one argument must be specified");
		return (1);
	}
	if (strlen(argv[1]) >= (size_t)PATH_MAX) {
		pr_err(gettext("name too long"));
		return (1);
	}
	strcpy(mnt_front, argv[1]);
	mnt_back[0] = '\0';

	/*
	 * An unmount from autofs may have a
	 * space appended to the path.
	 * Trim it off before attempting to
	 * match the mountpoint in mnttab
	 */
	strcpy(mnt_frontns, mnt_front);
	p = &mnt_frontns[strlen(mnt_frontns) - 1];
	if (*p == ' ')
		*p = '\0';

	/* get the mount point and the back file system mount point */
	finp = fopen(MNTTAB, "r+");
	if (finp) {
		if (lockf(fileno(finp), F_LOCK, 0L) < 0) {
			pr_err(gettext("cannot lock mnttab %s"),
			    strerror(errno));
			fclose(finp);
			return (1);
		}

		mntnull(&mref);
		mref.mnt_mountp = mnt_frontns;
		mref.mnt_fstype = "cachefs";
		ret = getmntany(finp, &mget, &mref);
		if (ret != -1) {
			strcpy(mnt_back, mget.mnt_special);
		} else {
			mref.mnt_special = mref.mnt_mountp;
			mref.mnt_mountp = NULL;
			rewind(finp);
			ret = getmntany(finp, &mget, &mref);
			if (ret != -1) {
				strcpy(mnt_front, mget.mnt_mountp);
				strcpy(mnt_back, mget.mnt_special);
			} else {
				pr_err(gettext("warning: %s not in mnttab"),
				    mref.mnt_special);
			}
		}
		fclose(finp);
	}

	/* try to unmount the file system */
	if (umount(mnt_front) == -1) {
		pr_err(gettext("%s %s"), mnt_front, strerror(errno));
		return (1);
	}

	/* remove the mnttab entry */
	mntnull(&mget);
	mget.mnt_mountp = mnt_frontns;
	fsrmfrommtab(&mget);

	/* if we do not know the name of the back file system mount point */
	if (mnt_back[0] == '\0') {
		/* all done */
		return (0);
	}

	/*
	 * If the back file system was mounted on a directory with a
	 * parent name of BACKMNT_NAME then we assume that we
	 * mounted it and that it is okay to try to umount it.
	 */
	if (strstr(mnt_back, BACKMNT_NAME) == NULL)
		return (0);

	/* invoke the umount command on the back file system */
	xx = execl("/sbin/umount", "/sbin/umount", mnt_back, NULL);
	pr_err(gettext("could not exec /sbin/umount on back file system %s"),
	    strerror(errno));
	return (1);
}

/*
 * -----------------------------------------------------------------
 *
 *			pr_err
 *
 * Description:
 *	Prints an error message to stderr.
 * Arguments:
 *	fmt	printf style format
 *	...	arguments for fmt
 * Returns:
 * Preconditions:
 *	precond(fmt)
 */

void
pr_err(char *fmt, ...)
{
	va_list ap;

	va_start(ap);
	(void) fprintf(stderr, gettext("umount -F cachefs: "));
	(void) vfprintf(stderr, fmt, ap);
	(void) fprintf(stderr, "\n");
	va_end(ap);
}

/*
 * -----------------------------------------------------------------
 *
 *			usage
 *
 * Description:
 *	Prints a usage message.
 * Arguments:
 *	An optional additional message to be displayed.
 * Returns:
 * Preconditions:
 */

void
usage(char *msgp)
{
	if (msgp)
		pr_err(gettext("%s"), msgp);
	fprintf(stderr, gettext("Usage: umount -F cachefs dir"));
}
