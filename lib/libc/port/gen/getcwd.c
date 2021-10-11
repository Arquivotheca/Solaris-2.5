/*
 * Copyright (c) 1992 Sun Microsystems, Inc. All rights reserved.
 */

#ident	"@(#)getcwd.c	1.16	95/09/25 SMI"	/* from SunOS4.1 1.20 */

/*LINTLIBRARY*/

/*
 * getcwd() returns the pathname of the current working directory. On error
 * an error message is copied to pathname and null pointer is returned.
 */

#ifdef	__STDC__
#pragma weak getcwd = _getcwd
#endif

#include "synonyms.h"

#include <sys/param.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#include <errno.h>
#include <string.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

#ifdef _REENTRANT
static mutex_t cwd_lock = DEFAULTMUTEX;
#endif _REENTRANT

struct lastpwd {	/* Info regarding the previous call to getcwd */
	dev_t dev;
	ino_t ino;
	size_t namelen;
	char name[MAXPATHLEN];
};

static struct lastpwd *lastone = NULL;	/* Cached entry */
static int pathsize;		/* pathname length */

extern char *calloc();
extern long strtol();

/*LINTLIBRARY*/

char *
getcwd(pathname, size)
	char *pathname;
	size_t size;
{
	char pathbuf[MAXPATHLEN];	/* temporary pathname buffer */
	char *pnptr = &pathbuf[sizeof (pathbuf) - 1]; /* pathname pointer */
	char curdir[MAXPATHLEN];	/* current directory buffer */
	char *dptr = curdir;		/* directory pointer */
	char *prepend();		/* prepend dirname to pathname */
	dev_t cdev, rdev;		/* current & root device number */
	ino_t cino, rino;		/* current & root inode number */
	DIR *dirp;			/* directory stream */
	struct dirent *dir;		/* directory entry struct */
	struct stat d, dd;		/* file status struct */
	dev_t newone_dev;		/* Device number of the new pwd */
	ino_t newone_ino;		/* Inode number of the new pwd */
	int alloc;			/* if 1, buffer was allocated */
	int saverr;			/* save errno */

	if (size == 0) {
		errno = EINVAL;
		return (NULL);
	}

	alloc = 0;
	if (pathname == NULL)  {
		if ((pathname = (char *) malloc((unsigned) size)) == NULL) {
			errno = ENOMEM;
			return (NULL);
		}
		alloc = 1;
	}

	_mutex_lock(&cwd_lock);
	pathsize = 0;
	*pnptr = '\0';
	strcpy(dptr, "./");
	dptr += 2;
	if (stat(curdir, &d) < 0) {
		goto errout;
	}

	/* Cache the pwd entry */
	if (lastone == NULL) {
		lastone = (struct lastpwd *) calloc(1, sizeof (struct lastpwd));
	} else if ((d.st_dev == lastone->dev) && (d.st_ino == lastone->ino)) {
		if ((stat(lastone->name, &dd) == 0) &&
		    (d.st_dev == dd.st_dev) && (d.st_ino == dd.st_ino)) {
			/* Make sure the size is big enough */
			if (lastone->namelen < size) {   /* save length? */
				/* Cache hit. */
				strcpy(pathname, lastone->name);
				_mutex_unlock(&cwd_lock);
				return (pathname);
			} else {
				errno = ERANGE;
				goto errout;
			}
		}
	}

	newone_dev = d.st_dev;
	newone_ino = d.st_ino;

	if (stat("/", &dd) < 0) {
		goto errout;
	}
	rdev = dd.st_dev;
	rino = dd.st_ino;
	for (;;) {
		dev_t crdev;
		cino = d.st_ino;
		cdev = d.st_dev;
		crdev = d.st_rdev;
		strcpy(dptr, "../");
		dptr += 3;
		if ((dirp = opendir(curdir)) == NULL) {
			goto errout;
		}
		if (fstat(dirp->dd_fd, &d) == -1) {
			saverr = errno;
			closedir(dirp);
			errno = saverr;
			goto errout;
		}
		if (cdev == d.st_dev && crdev == d.st_rdev) {
			if (cino == d.st_ino) {
				/* reached root directory */
				closedir(dirp);
				break;
			}
			if (cino == rino && cdev == rdev) {
				/*
				 * This case occurs when '/' is loopback
				 * mounted on the root filesystem somewhere.
				 */
				goto do_mount_pt;
			}
			do {
				if ((dir = readdir(dirp)) == NULL) {
					saverr = errno;
					closedir(dirp);
					errno = saverr;
					goto errout;
				}
			} while (dir->d_ino != cino);
		} else { /* It is a mount point */
			char tmppath[MAXPATHLEN];

do_mount_pt:
			/*
			 * Get the path name for the given dev number
			 */
			if (getdevinfo(cino, cdev, d.st_ino, d.st_dev,
			    tmppath)) {
				closedir(dirp);
				pnptr = prepend(tmppath, pnptr);
				break;
			}

			do {
				if ((dir = readdir(dirp)) == NULL) {
					saverr = errno;
					closedir(dirp);
					errno = saverr;
					goto errout;
				}
				strcpy(dptr, dir->d_name);
				(void) lstat(curdir, &dd);
			} while (dd.st_ino != cino || dd.st_dev != cdev);
		}
		pnptr = prepend("/", prepend(dir->d_name, pnptr));
		closedir(dirp);
	}
	if (*pnptr == '\0') {	/* current dir == root dir */
		if (size > 1) {
			strcpy(pathname, "/");
		} else {
			errno = ERANGE;
			goto errout;
		}
	} else {
		if (size > strlen(pnptr)) {
			strcpy(pathname, pnptr);
		} else {
			errno = ERANGE;
			goto errout;
		}
	}
	lastone->dev = newone_dev;
	lastone->ino = newone_ino;
	strcpy(lastone->name, pathname);
	lastone->namelen = strlen(pathname);
	_mutex_unlock(&cwd_lock);
	return (pathname);

errout:
	if (alloc)
		free(pathname);
	_mutex_unlock(&cwd_lock);
	return (NULL);
}

/*
 * prepend() tacks a directory name onto the front of a pathname.
 */
static char *
prepend(dirname, pathname)
	register char *dirname;
	register char *pathname;
{
	register int i;			/* directory name size counter */

	for (i = 0; *dirname != '\0'; i++, dirname++)
		continue;
	if ((pathsize += i) < MAXPATHLEN)
		while (i-- > 0)
			*--pathname = *--dirname;
	return (pathname);
}

/*
 * Gets the path name for the given device number. Returns 1 if
 * successful, else returns 0.
 */
static int
getdevinfo(ino, dev, parent_ino, parent_dev, path)
	ino_t ino;
	dev_t dev;
	ino_t parent_ino;
	dev_t parent_dev;
	char *path;
{
	struct mnttab mntent, *mnt;
	FILE *mounted;
	dev_t mntdev;
	char *str, *strp;
	char *equal;
	struct stat statb;
	int retval = 0;

	/*
	 * It reads the device id from /etc/mnttab file and compares it
	 * with the given dev/ino combination.
	 */
	if ((mounted = fopen(MNTTAB, "r")) == NULL)
		return (retval);

	mnt = &mntent;

	while (getmntent(mounted, mnt) == 0) {
		if (hasmntopt(mnt, MNTOPT_IGNORE) || !isdevice(mnt, dev))
			continue;

		/* Verify once again */
		if ((lstat(mnt->mnt_mountp, &statb) < 0) ||
		    (statb.st_dev != dev) ||
		    (statb.st_ino != ino))
			continue;
		/*
		 * verify that the parent dir is correct (may not
		 * be if there are loopback mounts around.)
		 */
		strcpy(path, mnt->mnt_mountp);
		strcat(path, "/..");
		if ((lstat(path, &statb) < 0) ||
		    (statb.st_dev != parent_dev) ||
		    (statb.st_ino != parent_ino))
			continue;
		strp = strrchr(path, '/');
		*strp = '\0';	/* Delete /.. */
		retval = 1;
		break;
	}

	(void) fclose(mounted);

	return (retval);
}

isdevice(mnt, dev)
	struct mnttab *mnt;
	dev_t dev;
{
	char *str, *equal;
	struct stat st;

	if (str = hasmntopt(mnt, MNTOPT_DEV)) {
		if (equal = strchr(str, '=')) {
			if ((dev_t) strtol(++equal, (char **) NULL, 16) == dev)
				return (1);
			else
				return (0);
		}
	}

	if (lstat(mnt->mnt_mountp, &st) < 0)
		return (0);

	return (st.st_dev == dev);
}
