/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)vold_mnt.c	1.22	95/01/17 SMI"

#include	<stdio.h>
#include	<fcntl.h>
#include	<stdlib.h>
#include	<string.h>
#include	<unistd.h>
#include	<sys/stat.h>
#include	<sys/mnttab.h>
#include	<sys/mntent.h>
#include	<syslog.h>
#include	<sys/wait.h>
#include	<time.h>

#include	"vold.h"


static struct 	mnttab *dupmnttab(struct mnttab *);
static void	freemnttab(struct mnttab *);
static int	openmnttab(void);
static int	lockmnttab(void);
static void	unlockmnttab(void);
static int	readmnttab(void);
static int	writemnttab(void);


struct mntlist {
	struct mnttab	*mntl_mnt;
	struct mntlist	*mntl_next;
};

static time_t		mnttab_last_mtime = 0;	/* last seen MNTTAB mtime */
static struct mntlist	*mntl_head = NULL;	/* in-core mount table */
static FILE		*mnt_fp = NULL;


#ifndef TRUE
#define	TRUE	(-1)
#endif

#ifndef	FALSE
#define	FALSE	(0)
#endif



/*
 * Check to see if "path" is used as a special device in a mount.
 * Will match partial paths (e.g. / will match everything).  Returns
 * the first matched path.
 */
char *
mnt_special_test(char *path)
{
	struct mntlist	*mntl;
	char		*rval = NULL;
	int		len;


	if (!openmnttab()) {
		return (NULL);
	}
	if (!lockmnttab()) {
		return (NULL);
	}

	if (!readmnttab()) {
		unlockmnttab();
		return (NULL);
	}

	len = strlen(path);

	for (mntl = mntl_head; mntl; mntl = mntl->mntl_next) {
		if (strncmp(path, mntl->mntl_mnt->mnt_special, len) == 0) {
			rval = strdup(mntl->mntl_mnt->mnt_special);
			break;
		}
	}
	unlockmnttab();
	return (rval);
}


/*
 * Check to see if "path" is used as a mount point in a mount.
 *
 * XXX: not currently used
 */
char *
mnt_mp_test(char *path)
{
	struct mntlist	*mntl;
	char		*rval = NULL;
	int		len;


	if (!openmnttab()) {
		return (NULL);
	}
	if (!lockmnttab()) {
		return (NULL);
	}

	if (!readmnttab()) {
		unlockmnttab();
		return (NULL);
	}

	len = strlen(path);

	for (mntl = mntl_head; mntl; mntl = mntl->mntl_next) {
		if (strncmp(path, mntl->mntl_mnt->mnt_mountp, len) == 0) {
			rval = strdup(mntl->mntl_mnt->mnt_mountp);
			break;
		}
	}
	unlockmnttab();
	return (rval);
}


struct mnttab *
mnt_mnttab(char *special)
{
	struct mntlist	*mntl;
	struct mnttab	*rval = NULL;


	if (!openmnttab()) {
		return (NULL);
	}
	if (!lockmnttab()) {
		return (NULL);
	}

	if (!readmnttab()) {
		unlockmnttab();
		return (NULL);
	}

	for (mntl = mntl_head; mntl; mntl = mntl->mntl_next) {
		if (strcmp(special, mntl->mntl_mnt->mnt_special) == 0) {
			rval = dupmnttab(mntl->mntl_mnt);
			break;
		}
	}
	unlockmnttab();
	return (rval);
}


void
mnt_free_mnttab(struct mnttab *mnt)
{
	freemnttab(mnt);
}


/*
 * rename a mount point in /etc/mnttab
 *
 * XXX: not currently used
 */
void
mnt_mp_rename(char *from, char *to)
{
	struct mntlist	*mntl;


	if (!openmnttab()) {
		return;
	}
	if (!lockmnttab()) {
		return;
	}

	if (!readmnttab()) {
		unlockmnttab();
		return;
	}

	for (mntl = mntl_head; mntl; mntl = mntl->mntl_next) {
		if (strcmp(mntl->mntl_mnt->mnt_mountp, from) == 0) {
			free(mntl->mntl_mnt->mnt_mountp);
			mntl->mntl_mnt->mnt_mountp = strdup(to);
		}
	}
	(void) writemnttab();
	unlockmnttab();
}


/*
 * rename a special device in /etc/mnttab
 */
void
mnt_special_rename(char *from, char *to)
{
	struct mntlist	*mntl;


	if (!openmnttab()) {
		return;
	}
	if (!lockmnttab()) {
		return;
	}

	if (!readmnttab()) {
		unlockmnttab();
		return;
	}

	for (mntl = mntl_head; mntl; mntl = mntl->mntl_next) {
		if (strcmp(mntl->mntl_mnt->mnt_special, from) == 0) {
			free(mntl->mntl_mnt->mnt_special);
			mntl->mntl_mnt->mnt_special = strdup(to);
		}
	}
	(void) writemnttab();
	unlockmnttab();
}


int
mnt_add(char *special, char *mountp, char *fstype, char *mntopts)
{
	struct stat	st;			/* for statting mountp */
	struct mntlist	*mntl;
	struct mntlist	*mntl_new;
	struct mntlist	*mntl_prev = NULL;
	struct mntlist	*mntl_next = NULL;
	struct mnttab	*mnt;
	char		buf[BUFSIZ];		/* temp string buf */



	if (!openmnttab()) {
		return (-1);
	}
	if (!lockmnttab()) {
		return (-1);
	}
	if (!readmnttab()) {
		unlockmnttab();
		return (-1);
	}

	/*
	 * build the new entry
	 */
	mnt = (struct mnttab *)calloc(1, sizeof (struct mnttab));
	if (mnt == NULL) {
		goto mnterr;
	}

	if ((mnt->mnt_special = strdup(special)) == NULL) {
		goto mnterr;
	}
	if ((mnt->mnt_mountp = strdup(mountp)) == NULL) {
		goto mnterr;
	}
	if ((mnt->mnt_fstype = strdup(fstype)) == NULL) {
		goto mnterr;
	}
	if (stat(mountp, &st) < 0) {
		goto mnterr;
	}
	(void) sprintf(buf, "%s,%s,%s=%lx", mntopts, MNTOPT_NOQUOTA,
	    MNTOPT_DEV, st.st_dev);
	if ((mnt->mnt_mntopts = strdup(buf)) == NULL) {
		goto mnterr;
	}
	(void) sprintf(buf, "%ld", time(0));
	if ((mnt->mnt_time = strdup(buf)) == NULL) {
		goto mnterr;
	}

	/*
	 * build an mntl
	 */
	mntl_new = (struct mntlist *)malloc(sizeof (struct mntlist));
	if (mntl_new == NULL) {
		goto mnterr;
	}
	mntl_new->mntl_mnt = mnt;

	/*
	 * find the end of the list.  While we're doing it, we just
	 * make sure we don't already have an entry for this mountpoint.
	 * If we do, we delete it.
	 */
	for (mntl = mntl_head; mntl; mntl = mntl_next) {
		mntl_next = mntl->mntl_next;
		if (strcmp(mntl->mntl_mnt->mnt_mountp, mountp) == 0) {
			freemnttab(mntl->mntl_mnt);
			if (mntl_prev) {
				free(mntl);
				mntl_prev->mntl_next = mntl_next;
			} else {
				free(mntl_head);
				mntl_head = mntl_next;
			}
			continue;
		}
		mntl_prev = mntl;
	}

	/*
	 * link it in
	 */
	mntl_prev->mntl_next = mntl_new;
	mntl_new->mntl_next = 0;

	(void) writemnttab();
	unlockmnttab();
	return (0);

mnterr:
	warning(gettext("can't add %s to %s; %m"), mountp, MNTTAB);
	return (-1);
}


/* XXX: not currently used */
void
mnt_mp_del(char *mountp)
{
	struct mntlist	*mntl;
	struct mntlist	*mntl_prev;
	struct mntlist	*mntl_next;
	int		modified = FALSE;


	if (!openmnttab()) {
		return;
	}
	if (!lockmnttab()) {
		return;
	}

	if (!readmnttab()) {
		unlockmnttab();
		return;
	}

	mntl_prev = NULL;

	for (mntl = mntl_head; mntl != NULL; mntl = mntl->mntl_next) {
		if (strcmp(mntl->mntl_mnt->mnt_mountp, mountp) == 0) {
			mntl_next = mntl->mntl_next;
			freemnttab(mntl->mntl_mnt);
			if (mntl_prev) {
				free(mntl);
				mntl_prev->mntl_next = mntl_next;
			} else {
				free(mntl_head);
				mntl_head = mntl_next;
			}
			modified = TRUE;
		}
		mntl_prev = mntl;
	}
	if (modified) {
		(void) writemnttab();
	}
	unlockmnttab();
}


/* XXX: not currently used */
void
mnt_special_del(char *special)
{
	struct mntlist	*mntl;
	struct mntlist	*mntl_prev;
	struct mntlist	*mntl_next = NULL;
	bool_t		modified = FALSE;


	if (!openmnttab()) {
		return;
	}
	if (!lockmnttab()) {
		return;
	}

	if (!readmnttab()) {
		unlockmnttab();
		return;
	}

	mntl_prev = NULL;

	for (mntl = mntl_head; mntl != NULL; mntl = mntl->mntl_next) {
		if (strcmp(mntl->mntl_mnt->mnt_special, special) == 0) {
			mntl_next = mntl->mntl_next;
			freemnttab(mntl->mntl_mnt);
			if (mntl_prev) {
				free(mntl);
				mntl_prev->mntl_next = mntl_next;
			} else {
				free(mntl_head);
				mntl_head = mntl_next;
			}
			modified = TRUE;
		}
		mntl_prev = mntl;
	}
	if (modified) {
		(void) writemnttab();
	}
	unlockmnttab();
}


/*
 * Release resources allocated by the mnt_* routines.
 *
 * XXX: not currently used
 */
void
mnt_done()
{
	struct mntlist	*mntl;
	struct mntlist	*mntl_next = NULL;


	if (!openmnttab()) {
		return;
	}
	if (!lockmnttab()) {
		return;
	}

	if (mntl_head == NULL) {
		return;
	}

	for (mntl = mntl_head; mntl != NULL; mntl = mntl_next) {
		mntl_next = mntl->mntl_next;
		freemnttab(mntl->mntl_mnt);
		free(mntl);
	}
	mntl_head = NULL;
	mnttab_last_mtime = (time_t)0;
	unlockmnttab();
	(void) fclose(mnt_fp);
	mnt_fp = NULL;
}


/*
 * open mnttab
 */
static int
openmnttab()
{
	if (mnt_fp == NULL) {
		if ((mnt_fp = fopen(MNTTAB, "r+")) == NULL) {
			warning(gettext("can't open %s for r/w; %m\n"),
			    MNTTAB);
			return (FALSE);
		}
		(void) fcntl(fileno(mnt_fp), F_SETFD, 1); /* close on exec */
	}
	return (TRUE);
}


/*
 * lock /etc/mnttab
 */
static int
lockmnttab()
{
	struct flock fl;


	(void) memset(&fl, 0, sizeof (fl));
	fl.l_type = F_WRLCK;
	if (fcntl(fileno(mnt_fp), F_SETLKW, &fl) < 0) {
		warning(gettext("lock of %s failed; %m\n"), MNTTAB);
		return (FALSE);
	}
	return (TRUE);
}


/*
 * unlock /etc/mnttab
 */
static void
unlockmnttab()
{
	struct flock fl;


	(void) memset(&fl, 0, sizeof (fl));
	fl.l_type = F_UNLCK;
	if (fcntl(fileno(mnt_fp), F_SETLKW, &fl) < 0) {
		warning(gettext("unlock of %s failed; %m\n"), MNTTAB);
		return;
	}
}


/*
 * read the mount table into memory. caller must manage the lock.
 */
static int
readmnttab()
{
	struct mntlist	*mntl;
	struct mntlist	*mntl_prev;
	struct mntlist	*mntl_next = NULL;
	struct stat	sb;
	struct mnttab	mnt;
	char		buf1[CTBSIZE];
	char		buf2[CTBSIZE];


	/*
	 * If we have already have an incore copy, make sure it's
	 * more recent than the file on disk.
	 */
	if (mntl_head != NULL) {

		if (stat(MNTTAB, &sb) < 0) {
			warning(gettext("can't stat %s; %m\n"), MNTTAB);
			return (FALSE);
		}

		/*
		 * looks like it's fine in-core.
		 */

		debug(10,
	"readmnttab: mnttab: previous mtime %24.24s, mtime now %24.24s\n",
		    ctime_r(&mnttab_last_mtime, buf1, CTBSIZE),
		    ctime_r(&sb.st_mtime, buf2, CTBSIZE));

		if (sb.st_mtime == mnttab_last_mtime) {
			/* the file hasn't been modified */
			debug(10, "readmnttab: using internal mnttab\n");
			return (TRUE);
		}

		/*
		 * looks like we need to reread it.  free previously
		 * allocated memory.
		 */

		mnttab_last_mtime = sb.st_mtime; /* save last mod time */

		debug(7,
	"readmnttab: dumping in-memory mnttab (rereading file)\n");

		for (mntl = mntl_head; mntl; mntl = mntl_next) {
			mntl_next = mntl->mntl_next;
			freemnttab(mntl->mntl_mnt);
			free(mntl);
		}
		mntl_head = NULL;

		/*
		 * Unfortunatly, we have to close /etc/mnttab and
		 * then reopen it...
		 */
		unlockmnttab();
		(void) fclose(mnt_fp);
		mnt_fp = NULL;
		if (!openmnttab()) {
			return (FALSE);
		}
		if (!lockmnttab()) {
			return (FALSE);
		}
	}

	/*
	 * save the "last time read".  t one to avoid
	 * roundoff errors and be safe.
	 */
	mntl_prev = 0;

	/*
	 * Read the mount table into memory.
	 */
	rewind(mnt_fp);
	while (getmntent(mnt_fp, &mnt) == 0) {
		mntl = (struct mntlist *)malloc(sizeof (*mntl));
		if (mntl_head == NULL) {
			mntl_head = mntl;
		} else {
			mntl_prev->mntl_next = mntl;
		}
		mntl_prev = mntl;
		mntl->mntl_next = NULL;
		mntl->mntl_mnt = dupmnttab(&mnt);
	}

	return (TRUE);
}


/*
 * write out the mounttab
 *
 * (why does this return a value that nobody uses? -- wld)
*/
static int
writemnttab()
{
	struct mntlist	*mntl;
	struct stat	sb;


	/*
	 * rewind our file pointer and truncate /etc/mnttab.
	 * we do the ftruncate to avoid garbage at the end
	 * of the file if it shrinks.
	 */
#ifdef	DEBUG
	debug(11, "writemnttab: entering\n");
#endif
	rewind(mnt_fp);
	if (ftruncate(fileno(mnt_fp), 0) < 0) {
		warning(gettext("can not truncate %s; %m\n"), MNTTAB);
		return (FALSE);
	}

	/*
	 * Write mnttab back out.
	 */
	for (mntl = mntl_head; mntl; mntl = mntl->mntl_next) {
		if (putmntent(mnt_fp, mntl->mntl_mnt) <= 0) {
			warning(gettext("can not write %s; %m\n"), MNTTAB);
			return (FALSE);
		}
	}
	if (fflush(mnt_fp) != 0) {
		warning(gettext("can not flush %s; %m\n"), MNTTAB);
		return (FALSE);
	}

	/* save the new "last modified" time */
	(void) stat(MNTTAB, &sb);		/* this shouldn't fail */
#ifdef	DEBUG
	{
		char	tbuf[CTBSIZE];
		debug(11, "writemnttab: setting last mtime to %24.24s\n",
		    ctime_r(&sb.st_mtime, tbuf, CTBSIZE));
	}
#endif
	mnttab_last_mtime = sb.st_mtime;

	return (TRUE);
}


static struct mnttab *
dupmnttab(struct mnttab *mnt)
{
	struct mnttab *new;


	if ((new = (struct mnttab *)malloc(sizeof (*new))) == NULL) {
		goto alloc_failed;
	}
	(void) memset((char *)new, 0, sizeof (*new));

	if ((new->mnt_special = strdup(mnt->mnt_special)) == NULL) {
		goto alloc_failed;
	}
	if ((new->mnt_mountp = strdup(mnt->mnt_mountp)) == NULL) {
		goto alloc_failed;
	}
	if ((new->mnt_fstype = strdup(mnt->mnt_fstype)) == NULL) {
		goto alloc_failed;
	}

	/* mnt_mntopts and mnt_time can conceivably be null */
	if (mnt->mnt_mntopts != NULL) {
		if ((new->mnt_mntopts = strdup(mnt->mnt_mntopts)) == NULL) {
			goto alloc_failed;
		}
	}
	if (mnt->mnt_time != NULL) {
		if ((new->mnt_time = strdup(mnt->mnt_time)) == NULL) {
			goto alloc_failed;
		}
	}

	return (new);

alloc_failed:
	warning(gettext("dumpmnttab: no memory\n"));
	return (NULL);
}


static void
freemnttab(struct mnttab *mnt)
{
	free(mnt->mnt_special);
	free(mnt->mnt_mountp);
	free(mnt->mnt_fstype);
	if (mnt->mnt_mntopts != NULL) {
		free(mnt->mnt_mntopts);
	}
	if (mnt->mnt_time) {
		free(mnt->mnt_time);
	}
	free(mnt);
}


/*
 * call umount on each volume we've mounted, then umount our root dir
 *
 * if any of the umounts of managed volumes fails, ignore them, but return
 * the status of umounting our root dir
 *
 * return 0 for success, else non-zero
 *
 * ignore the return codes from truning to unmount managed mendia (it
 * may be busy), but return the status from trying to unmount our root dir
 */
int
umount_all(char *root_dir)
{
	static int	call_umount(char *);
	uint_t		len = strlen(root_dir);
	struct mntlist	*mntl;
	int		res = 0;



	if (!openmnttab()) {
		goto dun;
	}
	if (!lockmnttab()) {
		goto dun;
	}
	if (!readmnttab()) {
		unlockmnttab();
		goto dun;
	}
	unlockmnttab();

	/* scan for mnt entries that use our root dir */
	for (mntl = mntl_head; mntl; mntl = mntl->mntl_next) {
		if (strncmp(root_dir, mntl->mntl_mnt->mnt_special, len) == 0) {
			(void) call_umount(mntl->mntl_mnt->mnt_mountp);


		}
	}

	res = call_umount(root_dir);
dun:
	return (res);
}


/*
 * call the umount program for the specified directory
 *
 * called from child during program termination (e.g., so debug(), et. al.,
 *	can't be used)
 *
 * return 0 for success, else non-zero
 */
static int
call_umount(char *dir)
{
	pid_t	pid;
	int	stat;
	int	fd;
	int	ret_val;




#ifdef	MT
	pid = fork1();
#else
	pid = fork();
#endif
	if (pid < 0) {
		return (1);			/* no processes? */
	}

	if (pid == 0) {
		/* the child -- call umount */

		/* redirect stdout/stderr to the bit bucket */
		fd = open("/dev/null", O_RDWR);
		(void) dup2(fd, fileno(stdout));
		(void) dup2(fd, fileno(stderr));

		/* now call umount to do the real work */
		(void) execl("/etc/umount", "umount", dir, 0);

		/* oh oh -- shouldn't reach here! */
		syslog(LOG_ERR, gettext("exec of umount failed; %m\n"),
		    dir);
		exit(1);
	}

	/* the parent -- wait for the child */
	if (waitpid(pid, &stat, 0) != pid) {
		/* couldn't get child status -- return error */
		return (1);
	}

	/* get return value */
	if (WIFEXITED(stat) && (WEXITSTATUS(stat) == 0)) {
		ret_val = 0;
	} else {
		ret_val = 1;
	}

	/* return status of child */
	return (ret_val);
}
