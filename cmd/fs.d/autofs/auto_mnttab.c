/*
 *	auto_mnttab.c
 *
 *	Copyright (c) 1988-1993 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)auto_mnttab.c	1.7	94/10/27 SMI"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <locale.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/vfs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mnttab.h>
#include <sys/mntent.h>
#include <sys/mount.h>
#include <sys/signal.h>
#include <sys/utsname.h>
#include <sys/tiuser.h>
#include <sys/fs/autofs.h>
#include <sys/utsname.h>
#include <rpc/rpc.h>
#include <rpcsvc/nfs_prot.h>
#include "automount.h"

static struct mntlist *mkmntlist();
static int get_devid(struct mnttab *);

extern void fix_mnttab(struct mnttab *);
extern void del_mnttab(char *);
extern void trim(char *);
extern void pr_msg(const char *, ...);

/*
 * Can't use the matching routine in ../fslib.c because of the space hacks
 * in the mount point name.
 * Delete a single entry from the mnttab
 * given its mountpoint.
 * Make sure the last instance in the file is deleted.
 */
void
del_mnttab(mntpnt)
	char *mntpnt;
{
	FILE *mnttab;
	struct mntlist *delete;
	struct mntlist *mntl_head, *mntl;
	char *p, c;
	int mlock = fslock_mnttab();

	mnttab = fopen(MNTTAB, "r+");
	if (mnttab == NULL) {
		pr_msg("%s: %m", MNTTAB);
		fsunlock_mnttab(mlock);
		return;
	}
	if (lockf(fileno(mnttab), F_LOCK, 0L) < 0) {
		pr_msg("%s: Cannot lock %s: %m", MNTTAB);
		(void) fclose(mnttab);
		fsunlock_mnttab(mlock);
		return;
	}

	/*
	 * Read the list of mounts
	 */
	mntl_head = mkmntlist(mnttab);
	if (mntl_head == NULL)
		goto done;

	/*
	 * Remove appended space
	 */
	p = &mntpnt[strlen(mntpnt) - 1];
	c = *p;
	if (c == ' ')
		*p = '\0';

	/*
	 * Mark the last entry that matches the
	 * mountpoint for deletion.
	 */
	for (mntl = mntl_head; mntl; mntl = mntl->mntl_next) {
		if (strcmp(mntl->mntl_mnt->mnt_mountp, mntpnt) == 0)
			delete = mntl;
	}
	*p = c;
	if (delete)
		delete->mntl_flags |= MNTL_UNMOUNT;

	/*
	 * Write the mount list back
	 */
	(void) fsputmntlist(mnttab, mntl_head);

done:
	fsfreemntlist(mntl_head);
	(void) fclose(mnttab);
	fsunlock_mnttab(mlock);
}


#define	TIME_MAX 16
#define	RET_ERR  1

/*
 * Can't use the matching routine in ../fslib.c because of the space hacks
 * in the mount point name.
 * Add a new entry to the /etc/mnttab file.
 * Include the device id with the mount options.
 */

add_mnttab(mnt)
	struct mnttab *mnt;
{
	FILE *fd;
	char tbuf[TIME_MAX];
	char *opts;
	struct stat st;
	char obuff[256], *pb = obuff;
	int len, mlock;
	char real_mntpnt[MAXPATHLEN + 1];

	/*
	 * the passed in mountpoint may have a space
	 * at the end of it. If so, it is important that
	 * the stat is done without changing it.
	 */
	if (stat(mnt->mnt_mountp, &st) < 0) {
		pr_msg("%s: %m", MNTTAB);
		return (ENOENT);
	}

	len = strlen(mnt->mnt_mountp);
	if (mnt->mnt_mountp[len - 1] == ' ') {
		(void) strncpy(real_mntpnt, mnt->mnt_mountp, len - 1);
		real_mntpnt[len - 1] = '\0';
	} else
		(void) strcpy(real_mntpnt, mnt->mnt_mountp);
	mnt->mnt_mountp = real_mntpnt;

	mlock = fslock_mnttab();
	fd = fopen(MNTTAB, "a");
	if (fd == NULL) {
		pr_msg("%s: %m", MNTTAB);
		fsunlock_mnttab(mlock);
		return (RET_ERR);
	}

	if (lockf(fileno(fd), F_LOCK, 0L) < 0) {
		pr_msg("%s: %m", MNTTAB);
		(void) fclose(fd);
		fsunlock_mnttab(mlock);
		return (RET_ERR);
	}

	opts = mnt->mnt_mntopts;
	if (opts && *opts) {
		(void) strcpy(pb, opts);
		trim(pb);
		pb += strlen(pb);
		*pb++ = ',';
	}

	(void) sprintf(pb, "%s=%lx", MNTOPT_DEV, st.st_dev);
	mnt->mnt_mntopts = obuff;

	(void) sprintf(tbuf, "%ld", time(0L));
	mnt->mnt_time = tbuf;

	(void) fseek(fd, 0L, 2); /* guarantee at EOF */

	putmntent(fd, mnt);

	mnt->mnt_mntopts = opts;
	(void) fclose(fd);
	fsunlock_mnttab(mlock);
	return (0);
}

/*
 * Replace an existing entry with a new one - a remount.
 * Need to keep this here because of the get_devid() call.
 */
void
fix_mnttab(mnt)
	struct mnttab *mnt;
{
	FILE *mnttab;
	struct mntlist *mntl_head, *mntl;
	struct mntlist *found;
	char *opts;
	char tbuf[TIME_MAX];
	char *newopts, *pn;
	int mlock;

	mlock = fslock_mnttab();

	mnttab = fopen(MNTTAB, "r+");
	if (mnttab == NULL) {
		pr_msg("%s: %m", MNTTAB);
		fsunlock_mnttab(mlock);
		return;
	}
	if (lockf(fileno(mnttab), F_LOCK, 0L) < 0) {
		pr_msg("%s: Cannot lock %s: %m", MNTTAB);
		(void) fclose(mnttab);
		fsunlock_mnttab(mlock);
		return;
	}

	/*
	 * Read the list of mounts
	 */
	mntl_head = mkmntlist(mnttab);
	if (mntl_head == NULL)
		goto done;

	/*
	 * Find the last entry that matches the
	 * mountpoint.
	 */
	found = NULL;
	for (mntl = mntl_head; mntl; mntl = mntl->mntl_next) {
		if (strcmp(mntl->mntl_mnt->mnt_mountp, mnt->mnt_mountp) == 0)
			found = mntl;
	}
	if (found == NULL) {
		pr_msg("Cannot find mntpnt for %s", mnt->mnt_mountp);
		goto done;
	}

	newopts = (char *) malloc(256);
	if (newopts == NULL) {
		pr_msg("fix_mnttab: no memory");
		goto done;
	}

	pn = newopts;
	opts = mnt->mnt_mntopts;
	if (opts && *opts) {
		(void) strcpy(pn, opts);
		trim(pn);
		pn += strlen(pn);
		*pn++ = ',';
	}

	(void) sprintf(pn, "%s=%x", MNTOPT_DEV, get_devid(found->mntl_mnt));
	mnt->mnt_mntopts = newopts;

	(void) sprintf(tbuf, "%ld", time(0L));
	mnt->mnt_time = tbuf;

	fsfreemnttab(found->mntl_mnt);
	found->mntl_mnt = fsdupmnttab(mnt);

	/*
	 * Write the mount list back
	 */
	(void) fsputmntlist(mnttab, mntl_head);

done:
	fsfreemntlist(mntl_head);
	(void) fclose(mnttab);
	fsunlock_mnttab(mlock);
}

/*
 * Scan the mount option string and extract
 * the hex device id from the "dev=" string.
 * If the string isn't found get it from the
 * filesystem stats.
 */
static int
get_devid(mnt)
	struct mnttab *mnt;
{
	int val = 0;
	char *equal;
	char *str;
	struct stat st;

	if (str = hasmntopt(mnt, MNTOPT_DEV)) {
		if (equal = strchr(str, '='))
			val = strtol(equal + 1, (char **) NULL, 16);
		else
			syslog(LOG_ERR, "Bad device option '%s'", str);
	}

	if (val == 0) {		/* have to stat the mountpoint */
		if (stat(mnt->mnt_mountp, &st) < 0)
			syslog(LOG_ERR, "stat %s: %m", mnt->mnt_mountp);
		else
			val = st.st_dev;
	}

	return (val);
}

/*
 * Read the mnttab file and return it as a list of mnttab structs.
 * Can't use the similar routine in ../fslib.c because of the get_devid()
 * requirement.
 */
static struct mntlist *
mkmntlist(mnttab)
	FILE *mnttab;
{
	struct mnttab mnt;
	struct mntlist *mntl_head = NULL;
	struct mntlist *mntl_prev = NULL;
	struct mntlist *mntl;

	while (getmntent(mnttab, &mnt) == 0) {
		mntl = (struct mntlist *) malloc(sizeof (*mntl));
		if (mntl == NULL)
			goto alloc_failed;
		if (mntl_head == NULL)
			mntl_head = mntl;
		else
			mntl_prev->mntl_next = mntl;
		mntl_prev = mntl;
		mntl->mntl_next = NULL;
		mntl->mntl_dev = get_devid(&mnt);
		mntl->mntl_flags = 0;
		mntl->mntl_mnt = fsdupmnttab(&mnt);
		if (mntl->mntl_mnt == NULL)
			goto alloc_failed;
	}

	return (mntl_head);

alloc_failed:
	fsfreemntlist(mntl_head);
	return (NULL);
}

struct mntlist *
getmntlist()
{
	FILE *mnttab;
	struct mntlist *mntl;
	int mlock = fslock_mnttab();

	mnttab = fopen(MNTTAB, "r+");
	if (mnttab == NULL) {
		pr_msg("%s: %m", MNTTAB);
		fsunlock_mnttab(mlock);
		return (NULL);
	}

	if (lockf(fileno(mnttab), F_LOCK, 0L) < 0) {
		pr_msg("cannot lock %s: %m", MNTTAB);
		(void) fclose(mnttab);
		fsunlock_mnttab(mlock);
		return (NULL);
	}

	mntl = mkmntlist(mnttab);

	(void) fclose(mnttab);
	fsunlock_mnttab(mlock);
	return (mntl);
}
