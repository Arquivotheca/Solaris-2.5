/*	Copyright (c) 1984, 1986, 1987, 1988, 1989, 1990, 1991 SMI	*/
/*	  All Rights Reserved						*/


#pragma ident	"@(#)fslib.c	1.6	94/11/15 SMI"

#include	<stdio.h>
#include	<stdarg.h>
#include	<stdlib.h>
#include	<unistd.h>
#include	<libintl.h>
#include	<string.h>
#include	<fcntl.h>
#include	<errno.h>
#include	<syslog.h>
#include	<sys/vfstab.h>
#include	<sys/mnttab.h>
#include	<sys/mntent.h>
#include	<sys/mount.h>
#include	<sys/stat.h>
#include	<sys/param.h>
#include	<signal.h>
#include	"fslib.h"

#define	MNT_LOCK	"/etc/.mnttab.lock"
extern int		errno;

/*
 * Called before /etc/mnttab is read or written to.  This ensures the
 * integrity of /etc/mnttab by using a lock file.  Returns a lock id
 * with which to fsunlock_mnttab() after.
 */
int
fslock_mnttab()
{
	int mlock;

	if ((mlock = open(MNT_LOCK, O_RDWR|O_CREAT|O_TRUNC, 0644)) == -1) {
		perror("fslock_mnttab: open");
		return (-1);
	}

	if (lockf(mlock, F_LOCK, 0L) == -1) {
		perror("fslock_mnttab: lockf");
		(void) close(mlock);
		return (-1);
	}

	return (mlock);
}

/*
 * Undos fslock_mnttab().
 */
void
fsunlock_mnttab(int mlock)
{
	if (mlock >= 0)
		if (close(mlock) == -1)
			perror("fslock_mnttab: close");
}

#define	TIME_MAX 16

/*
 * Add a mnttab entry to MNTTAB.  Include the device id with the
 * mount options and set the time field.
 */
int
fsaddtomtab(struct mnttab *mntin)
{
	FILE 	*mfp;
	struct	stat st;
	char	obuff[MNT_LINE_MAX], *pb = obuff, *opts;
	char	tbuf[TIME_MAX];
	int	mlock;

	if (stat(mntin->mnt_mountp, &st) < 0) {
		perror(mntin->mnt_mountp);
		return (errno);
	}

	opts = mntin->mnt_mntopts;
	if (opts && *opts) {
		(void) strcpy(pb, opts);
		pb += strlen(pb);
		*pb++ = ',';
	}
	(void) sprintf(pb, "%s=%lx", MNTOPT_DEV, st.st_dev);
	mntin->mnt_mntopts = obuff;

	(void) sprintf(tbuf, "%ld", time(0L));
	mntin->mnt_time = tbuf;

	mlock = fslock_mnttab();
	if ((mfp = fopen(MNTTAB, "a+")) == NULL) {
		(void) fprintf(stderr, gettext("fsaddtomtab: can't open %s\n"),
				MNTTAB);
		perror("fopen");
		fsunlock_mnttab(mlock);
		return (errno);
	}
	/*
	 * We need to be paranoid here because we can't be sure that
	 * all other mnttab programs use fslock_mnttab().
	 */
	(void) lockf(fileno(mfp), F_LOCK, 0L);

	(void) fseek(mfp, 0L, SEEK_END); /* guarantee at EOF */

	putmntent(mfp, mntin);
	(void) fclose(mfp);
	fsunlock_mnttab(mlock);
	mntin->mnt_mntopts = opts;
	return (0);
}

/*
 * Remove the last entry in MNTTAB that matches mntin.
 * Returns errno if error.
 */
int
fsrmfrommtab(struct mnttab *mntin)
{
	FILE 		*fp;
	mntlist_t 	*mlist, *delete;
	int 		mlock;

	if (mntin == NULL)
		return (0);

	mlock = fslock_mnttab();
	if ((fp = fopen(MNTTAB, "r+")) == NULL) {
		(void) fprintf(stderr, gettext("fsrmfrommtab: can't open %s\n"),
				MNTTAB);
		fsunlock_mnttab(mlock);
		return (errno);
	}

	if (lockf(fileno(fp), F_LOCK, 0L) < 0) {
		(void) fprintf(stderr, gettext("fsrmfrommtab: cannot lock %s"),
				MNTTAB);
		perror("");
		(void) fclose(fp);
		fsunlock_mnttab(mlock);
		return (errno);
	}

	/*
	 * Read the entire mnttab into memory.
	 * Remember the *last* instance of the unmounted
	 * mount point (have to take stacked mounts into
	 * account) and make sure that it's not written
	 * back out.
	 */
	if ((mlist = fsmkmntlist(fp)) == NULL) {
		errno = ENOENT;
		goto finish;
	}

	delete = fsgetmlast(mlist, mntin);

	if (delete)
		delete->mntl_flags |= MNTL_UNMOUNT;

	/*
	 * Write the mount list back
	 */
	errno = fsputmntlist(fp, mlist);

finish:
	(void) fclose(fp);
	fsunlock_mnttab(mlock);
	return (errno);
}

/*
 * Locks mnttab, reads all of the entries, unlocks mnttab, and returns the
 * linked list of the entries.
 */
mntlist_t *
fsgetmntlist()
{
	FILE *mfp;
	mntlist_t *mntl;
	int mlock;

	mlock = fslock_mnttab();
	if ((mfp = fopen(MNTTAB, "r+")) == NULL) {
		perror(MNTTAB);
		fsunlock_mnttab(mlock);
		return (NULL);
	}

	if (lockf(fileno(mfp), F_LOCK, 0L) < 0) {
		perror("lockf");
		(void) fclose(mfp);
		fsunlock_mnttab(mlock);
		return (NULL);
	}

	mntl = fsmkmntlist(mfp);

	(void) fclose(mfp);
	fsunlock_mnttab(mlock);
	return (mntl);
}

/*
 * Puts the mntlist out to the mfp mnttab file, except for those with
 * the UNMOUNT bit set.  Expects mfp to be locked.  Returns errno if
 * an error occurred.
 */
int
fsputmntlist(FILE *mfp, mntlist_t *mntl_head)
{
	mntlist_t *mntl;

	(void) signal(SIGHUP,  SIG_IGN);
	(void) signal(SIGQUIT, SIG_IGN);
	(void) signal(SIGINT,  SIG_IGN);

	/* now truncate the mnttab and write almost all of it back */

	rewind(mfp);
	if (ftruncate(fileno(mfp), 0) < 0) {
		(void) fprintf(stderr, gettext("fsrmfrommtab: truncate %s"),
						MNTTAB);
		perror("");
		return (errno);
	}

	for (mntl = mntl_head; mntl; mntl = mntl->mntl_next) {
		if (mntl->mntl_flags & MNTL_UNMOUNT)
			continue;
		putmntent(mfp, mntl->mntl_mnt);
	}
	return (0);
}

static struct mnttab zmnttab = { 0 };

struct mnttab *
fsdupmnttab(struct mnttab *mnt)
{
	struct mnttab *new;

	new = (struct mnttab *) malloc(sizeof (*new));
	if (new == NULL)
		goto alloc_failed;

	*new = zmnttab;
	/*
	 * Allocate an extra byte for the mountpoint
	 * name in case a space needs to be added.
	 */
	new->mnt_mountp = (char *) malloc(strlen(mnt->mnt_mountp) + 2);
	if (new->mnt_mountp == NULL)
		goto alloc_failed;
	(void) strcpy(new->mnt_mountp, mnt->mnt_mountp);

	if ((new->mnt_special = strdup(mnt->mnt_special)) == NULL)
		goto alloc_failed;

	if ((new->mnt_fstype = strdup(mnt->mnt_fstype)) == NULL)
		goto alloc_failed;

	if (mnt->mnt_mntopts != NULL)
		if ((new->mnt_mntopts = strdup(mnt->mnt_mntopts)) == NULL)
			goto alloc_failed;

	if (mnt->mnt_time != NULL)
		if ((new->mnt_time = strdup(mnt->mnt_time)) == NULL)
			goto alloc_failed;

	return (new);

alloc_failed:
	(void) fprintf(stderr, gettext("fsdupmnttab: Out of memory\n"));
	fsfreemnttab(new);
	return (NULL);
}

/*
 * Free a single mnttab structure
 */
void
fsfreemnttab(struct mnttab *mnt)
{

	if (mnt) {
		if (mnt->mnt_special)
			free(mnt->mnt_special);
		if (mnt->mnt_mountp)
			free(mnt->mnt_mountp);
		if (mnt->mnt_fstype)
			free(mnt->mnt_fstype);
		if (mnt->mnt_mntopts)
			free(mnt->mnt_mntopts);
		if (mnt->mnt_time)
			free(mnt->mnt_time);
		free(mnt);
	}
}

void
fsfreemntlist(mntlist_t *mntl)
{
	mntlist_t *mntl_tmp;

	while (mntl) {
		fsfreemnttab(mntl->mntl_mnt);
		mntl_tmp = mntl;
		mntl = mntl->mntl_next;
		free(mntl_tmp);
	}
}

/*
 * Read the mnttab file and return it as a list of mnttab structs.
 * Returns NULL if there was a memory failure.
 * This routine expects the mnttab file to be locked.
 */
mntlist_t *
fsmkmntlist(FILE *mfp)
{
	struct mnttab 	mnt;
	mntlist_t 	*mhead, *mtail;
	int 		ret;

	mhead = mtail = NULL;

	while ((ret = getmntent(mfp, &mnt)) != -1) {
		mntlist_t	*mp;

		if (ret != 0)		/* bad entry */
			continue;

		mp = (mntlist_t *) malloc(sizeof (*mp));
		if (mp == NULL)
			goto alloc_failed;
		if (mhead == NULL)
			mhead = mp;
		else
			mtail->mntl_next = mp;
		mtail = mp;
		mp->mntl_next = NULL;
		mp->mntl_flags = 0;
		if ((mp->mntl_mnt = fsdupmnttab(&mnt)) == NULL)
			goto alloc_failed;
	}
	return (mhead);

alloc_failed:
	fsfreemntlist(mhead);
	return (NULL);
}

/*
 * Return the last entry that matches mntin's special
 * device and/or mountpt.
 * Helps to be robust here, so we check for NULL pointers.
 */
mntlist_t *
fsgetmlast(mntlist_t *ml, struct mnttab *mntin)
{
	mntlist_t 	*delete = NULL;

	for (; ml; ml = ml->mntl_next) {
		if (mntin->mnt_mountp && mntin->mnt_special) {
			/*
			 * match if and only if both are equal.
			 */
			if ((strcmp(ml->mntl_mnt->mnt_mountp,
					mntin->mnt_mountp) == 0) &&
			    (strcmp(ml->mntl_mnt->mnt_special,
					mntin->mnt_special) == 0))
				delete = ml;
		} else if (mntin->mnt_mountp) {
			if (strcmp(ml->mntl_mnt->mnt_mountp,
					mntin->mnt_mountp) == 0)
				delete = ml;
		} else if (mntin->mnt_special) {
			if (strcmp(ml->mntl_mnt->mnt_special,
					mntin->mnt_special) == 0)
				delete = ml;
	    }
	}
	return (delete);
}


/*
 * Returns the mountlevel of the pathname in cp.  As examples,
 * / => 1, /bin => 2, /bin/ => 2, ////bin////ls => 3, sdf => 0, etc...
 */
int
fsgetmlevel(char *cp)
{
	int	mlevel;
	char	*cp1;

	if (cp == NULL || *cp == NULL || *cp != '/')
		return (0);	/* this should never happen */

	mlevel = 1;			/* root (/) is the minimal case */

	for (cp1 = cp + 1; *cp1; cp++, cp1++)
		if (*cp == '/' && *cp1 != '/')	/* "///" counts as 1 */
			mlevel++;

	return (mlevel);
}

/*
 * Returns non-zero if string s is a member of the strings in ps.
 */
int
fsstrinlist(const char *s, const char **ps)
{
	const char *cp;
	cp = *ps;
	while (cp) {
		if (strcmp(s, cp) == 0)
			return (1);
		ps++;
		cp = *ps;
	}
	return (0);
}
