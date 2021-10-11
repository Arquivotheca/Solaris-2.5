/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#ident	"@(#)dumpfstab.c 1.0 91/11/22 SMI"

#ident	"@(#)dumpfstab.c 1.3 94/08/10"

#include <errno.h>
#include "dump.h"

/*
 * File system mount table input routines.  We handle a
 * a combination of BSD and SVR4 formats by coding functions
 * to explicitly read the SVR4 vfstab file and using
 * #define's to build a routine to read both BSD files
 * (fstab and mtab) and SVR4's mnttab file.  Internally
 * we keep everything in the common (mtab/mnttab) format.
 */
static struct pmntent {
	struct mntent	*pm_mnt;
	struct pmntent	*pm_next;
} *mnttable;

#define	mntstrdup(s)	((s) ? strdup((s)) : "")

#ifdef __STDC__
#ifdef USG
static struct mntent *mygetmntent(FILE *, char *);
#endif
static struct pmntent *addmtab(char *, struct pmntent *);
static struct mntent *allocmntent(struct mntent *);
#else /* !__STDC__ */
#ifdef USG
static struct pmntent *addmtab();
static struct mntent *mygetmntent();
#endif
static struct mntent *allocmntent();
static int idatesort();
#endif

#ifdef USG
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
		msg(gettext("%s has a line that is too long\n"), name);
		break;
	case MNT_TOOMANY:
		msg(gettext("%s has a line with too many entries\n"), name);
		break;
	case MNT_TOOFEW:
		msg(gettext("%s has a line with too few entries\n"), name);
		break;
	default:
		msg(gettext(
			"Unknown return code, %d, from getmntent() on %s\n"),
			status, name);
		break;
	}

	return (NULL);
}

/*
 * Read in SVR4 vfstab-format table.
 */
static struct pmntent *
addvfstab(tablename, pm)
	char	*tablename;
	struct pmntent *pm;
{
	struct mnttab *mnt;
	struct vfstab vfs;
	FILE	*tp;
	int status;

	tp = fopen(tablename, "r");
	if (tp == (FILE *)0) {
		msg(gettext("Cannot open %s for dump table information.\n"),
			tablename);
		return ((struct pmntent *)0);
	}
	while ((status = getvfsent(tp, &vfs)) == 0) {
		if (vfs.vfs_fstype == (char *)0 ||
		    strcmp(vfs.vfs_fstype, MNTTYPE_42) != 0)
			continue;

		/*LINTED [rvalue = malloc() and therefore aligned]*/
		mnt = (struct mnttab *)xmalloc(sizeof (*mnt));
		mnt->mnt_fsname = mntstrdup(vfs.vfs_special);
		mnt->mnt_dir = mntstrdup(vfs.vfs_mountp);
		mnt->mnt_type = mntstrdup(vfs.vfs_fstype);
		mnt->mnt_opts = mntstrdup(vfs.vfs_mntopts);

		if (mnttable == (struct pmntent *)0)
			/*LINTED [rvalue = malloc()]*/
			mnttable = pm = (struct pmntent *)xmalloc(sizeof (*pm));
		else {
			/*LINTED [rvalue = malloc()]*/
			pm->pm_next = (struct pmntent *)xmalloc(sizeof (*pm));
			pm = pm->pm_next;
		}
		pm->pm_mnt = mnt;
		pm->pm_next = (struct pmntent *)0;
	}

	switch (status) {
	case EOF:	break;		/* normal exit condition */
	case VFS_TOOLONG:
		msg(gettext("%s has a line that is too long\n"), tablename);
		break;
	case VFS_TOOMANY:
		msg(gettext("%s has a line with too many entries\n"),
			tablename);
		break;
	case VFS_TOOFEW:
		msg(gettext("%s has a line with too few entries\n"), tablename);
		break;
	default:
		msg(gettext(
			"Unknown return code, %d, from getvfsent() on %s\n"),
			status, tablename);
		break;
	}
	(void) fclose(tp);
	return (pm);
}
#else !USG
#define	mygetmntent	getmntent
#endif

static struct mntent *
allocmntent(mnt)
	register struct mntent *mnt;
{
	register struct mntent *new;

	/*LINTED [rvalue = malloc() and therefore aligned]*/
	new = (struct mntent *)xmalloc(sizeof (*mnt));
	new->mnt_fsname = mntstrdup(mnt->mnt_fsname);	/* mnt_special */
	new->mnt_dir = mntstrdup(mnt->mnt_dir);		/* mnt_mountp  */
	new->mnt_type = mntstrdup(mnt->mnt_type);	/* mnt_fstype  */
	new->mnt_opts = mntstrdup(mnt->mnt_opts);	/* mnt_mntopts */
#ifndef USG
	new->mnt_freq = mnt->mnt_freq;
#endif
	return (new);
}

void
mnttabread()
{
	struct pmntent *pm = (struct pmntent *)0;

	if (mnttable != (struct pmntent *)0)
		return;
	/*
	 * Read in the file system mount tables.  Order
	 * is important as the first matched entry is used
	 * if the target device/filesystem is not mounted.
	 * We try fstab or vfstab first, then mtab or mnttab.
	 */
#ifdef USG
	pm = addvfstab(VFSTAB, pm);
	(void) addmtab(MOUNTED, pm);
#else
	pm = addmtab(FSTAB, pm);
	(void) addmtab(MTAB, pm);
#endif
}

static struct pmntent *
addmtab(tablename, pm)
	char	*tablename;
	struct pmntent *pm;
{
	struct mntent *mnt;
	FILE	*tp;

	tp = setmntent(tablename, "r");
	if (tp == (FILE *)0) {
		msg(gettext("Cannot open %s for dump table information.\n"),
			tablename);
		return ((struct pmntent *)0);
	}
	while (mnt = mygetmntent(tp, tablename)) {
		if (mnt->mnt_type == (char *)0 ||
		    strcmp(mnt->mnt_type, MNTTYPE_42) != 0)
			continue;

		mnt = allocmntent(mnt);
		if (mnttable == (struct pmntent *)0)
			/*LINTED [rvalue = malloc()]*/
			mnttable = pm = (struct pmntent *)xmalloc(sizeof (*pm));
		else {
			/*LINTED [rvalue = malloc()]*/
			pm->pm_next = (struct pmntent *)xmalloc(sizeof (*pm));
			pm = pm->pm_next;
		}
		pm->pm_mnt = mnt;
		pm->pm_next = (struct pmntent *)0;
	}
	(void) endmntent(tp);
	return (pm);
}

/*
 * Search in fstab and potentially mtab for a file name.
 * If "mounted" is non-zero, the target file system must
 * be mounted in order for the search to succeed.
 * This file name can be either the special or the path file name.
 *
 * The entries in either fstab or mtab are the BLOCK special names,
 * not the character special names.
 * The caller of mnttabsearch assures that the character device
 * is dumped (that is much faster)
 *
 * The file name can omit the leading '/'.
 */
struct mntent *
mnttabsearch(key, mounted)
	char	*key;
	int	mounted;
{
	register struct pmntent *pm;
	register struct mntent *mnt = (struct mntent *)0;
	struct mntent *first = (struct mntent *)0;
	char *s;
	char *gotreal;
	char path[MAXPATHLEN];

	for (pm = mnttable; pm; pm = pm->pm_next) {
		mnt = pm->pm_mnt;
		if (strcmp(mnt->mnt_dir, key) == 0)
			goto found;
		if (strcmp(mnt->mnt_fsname, key) == 0)
			goto found;
		if ((s = rawname(mnt->mnt_fsname)) != NULL &&
		    strcmp(s, key) == 0)
			goto found;
		gotreal = realpath(mnt->mnt_dir, path);
		if (gotreal && strcmp(path, key) == 0)
			goto found;
		if (key[0] != '/') {
			if (*mnt->mnt_fsname == '/' &&
			    strcmp(mnt->mnt_fsname + 1, key) == 0)
				goto found;
			if (*mnt->mnt_dir == '/' &&
			    strcmp(mnt->mnt_dir + 1, key) == 0)
				goto found;
			if (gotreal && *path == '/' &&
			    strcmp(path + 1, key) == 0)
				goto found;
		}
		continue;
found:
		/*
		 * Found a match; return immediately if
		 * it is mounted (valid), otherwise just
		 * record if it's the first matched entry.
		 */
		if (ismounted(mnt->mnt_fsname, mnt->mnt_dir) > 0)
			return (mnt);
		else if (first == (struct mntent *)0)
			first = mnt;
	}
	/*
	 * If we get here, there were either
	 * no matches, or no matched entries
	 * were mounted.  Return failure if
	 * we were supposed to find a mounted
	 * entry, otherwise return the first
	 * matched entry (or null).
	 */
	if (mounted)
		return ((struct mntent *)0);
	return (first);
}

static struct pmntent *current;
static int set;

void
#ifdef __STDC__
setmnttab(void)
#else
setmnttab()
#endif
{
	current = mnttable;
	set = 1;
}

struct mntent *
#ifdef __STDC__
getmnttab(void)
#else
getmnttab()
#endif
{
	struct pmntent *pm;

	if (!set)
		setmnttab();
	pm = current;
	if (current) {
		current = current->pm_next;
		return (pm->pm_mnt);
	}
	return ((struct mntent *)0);
}
