/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

/*LINTLIBRARY*/
#ident	"@(#)mntinfo.c	1.16	95/06/24"

#include <stdio.h>
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pkgstrct.h>
#include <pkginfo.h>
#include <locale.h>
#include <libintl.h>
#include <pkglib.h>
#include "install.h"
#include "libinst.h"
#include "libadm.h"

#ifdef SUNOS41
#include <mntent.h>
#else
#include <sys/mnttab.h>
#include <sys/mntent.h>
/* XXX - this should be declared in /usr/include/mnttab.h */
extern char	*hasmntopt(struct mnttab *mnt, char *opt);
#endif

int	fs_tab_used  = 0;
int	fs_tab_alloc = 0;

struct	fstable	**fs_tab = NULL;

#define	PKGDBROOT	"/var/sadm"

#define	ERR_FSTAB_MALLOC	"malloc(fs_tab) failed errno=%d"
#define	ERR_FSTAB_REALLOC	"realloc(fs_tab) failed errno=%d"
#define	ERR_NOROOT		"get_mntinfo() identified <%s> as root file " \
				"system instead of <%s> errno %d."

#define	MNTTYPE_CFS	"cachefs"
#define	MNTOPT_BACKFSTYPE	"backfstype"

#ifdef SUNOS41
#define	MOUNT_TABLE	MOUNTED
#define	mnt_special	mnt_fsname
#define	mnt_mountp	mnt_dir
#define	mnt_fstype	mnt_type
#define	mnt_mntopts	mnt_opts
#define	mnttab		mntent
#else
#define	setmntent	fopen
#define	endmntent	fclose
#define	MOUNT_TABLE	MNTTAB
#endif

/*
 * Utilities for getting filesystem information from the mount table.
 *
 * Note: vanilla SVr4 code (pkginstall/dockspace.c) used the output from
 * popen() on the "/etc/mount" command.  However, we need to get more
 * information about mounted filesystems, so we use the C interfaces to
 * the mount table, which also happens to be much faster than running
 * another process.  Since several of the pkg commands need access to the
 * the code has been placed here, to be included in the libinst library.
 */

#define	ALLOC_CHUNK	30

/*
 * fs_tab_ent_comp -	compare fstable entries first by length in reverse
 *			order, then alphabetically.
 */
static int
fs_tab_ent_comp(const void *e1, const void *e2)
{
	struct fstable	*fs1 = *((struct fstable **) e1);
	struct fstable	*fs2 = *((struct fstable **) e2);

	if (fs1->namlen == fs2->namlen)
		return (strcmp(fs1->name, fs2->name));
	else
		return (fs2->namlen - fs1->namlen);
}

/*
 * get_mntinfo - get the mount table, now dynamically allocated.
 */
void
get_mntinfo(void)
{
	static 	char 	*rn = "/";
	FILE		*pp;
	struct	mnttab	mtbuf;
	struct	mnttab	*mt = &mtbuf;
	struct	fstable	*nfte;
	char		*p;

	if ((pp = setmntent(MOUNT_TABLE, "r")) == NULL) {
		progerr(gettext("unable to open mount table %s"), MOUNT_TABLE);
		return;
	}

#ifdef SUNOS41
	while ((mt = getmntent(pp)) != NULL)
#else
	while (!getmntent(pp, mt))
#endif
	{
		char *real_fstype;

		/*
		 * Allocate an fstable entry for this mnttab entry.
		 */
		if ((nfte = (struct fstable *)malloc(sizeof (struct fstable)))
		    == NULL) {
			progerr(gettext("malloc(nfte) failed errno=%d"),
			    errno);
			return;
		}
		memset((char *)nfte, '\0', sizeof (struct fstable));

		/*
		 * Get the length of the 'mount point' name.
		 */
		nfte->namlen = strlen(mt->mnt_mountp);
		/*
		 * Allocate space for the 'mount point' name.
		 */
		if ((nfte->name = malloc(nfte->namlen+1)) == NULL) {
			progerr(gettext("malloc(name) failed errno=%d"),
			    errno);
			return;
		}
		(void) strcpy(nfte->name, mt->mnt_mountp);

		if (hasmntopt(mt, MNTOPT_RO) == NULL)
			nfte->writeable = 1;
		else
			nfte->writeable = 0;

		/* Deal with cachefs remote. This is a temporary fix. */
		if (strcmp(mt->mnt_fstype, MNTTYPE_CFS) == 0) {
			/*
			 * This isn't a great test, but it's as good as we'll
			 * get for Solaris 2.5. If it's cachefs and read-only
			 * and there's a hostname in the remote name,
			 * it's from a server.
			 */
			if (nfte->writeable == 0 &&
			    strchr(mt->mnt_special, ':'))
				real_fstype = MNTTYPE_NFS;
			else {
				real_fstype = MNTTYPE_UFS;
			}
		} else
			real_fstype = mt->mnt_fstype;

		if ((nfte->remote_name = malloc(strlen(mt->mnt_special)+1)) ==
		    NULL) {
			progerr(
			    gettext("malloc(remote_name) failed errno=%d"),
			    errno);
			return;
		}
		(void) strcpy(nfte->remote_name, mt->mnt_special);

		if ((nfte->fstype = malloc(strlen(real_fstype)+1)) == NULL) {
			progerr(gettext("malloc(fstype) failed errno=%d"),
			    errno);
			return;
		}
		(void) strcpy(nfte->fstype, real_fstype);

		/*
		 * Add the entry to the fs_tab, growing this if needed.
		 */
		if (fs_tab_used >= fs_tab_alloc) {
			if (fs_tab_alloc == 0) {
				fs_tab_alloc = ALLOC_CHUNK;
				if ((fs_tab = (struct fstable **)
				    malloc(sizeof (struct fstable *) *
				    fs_tab_alloc)) == NULL) {
					progerr(gettext(ERR_FSTAB_MALLOC),
					    errno);
					return;
				}
			} else {
				fs_tab_alloc += ALLOC_CHUNK;
				if ((fs_tab = (struct fstable **)
				    realloc((void *) fs_tab,
				    sizeof (struct fstable *) *
				    fs_tab_alloc)) == NULL) {
					progerr(gettext(ERR_FSTAB_REALLOC),
					    errno);
					return;
				}
			}
		}
		fs_tab[fs_tab_used++] = nfte;
	}
	endmntent(pp);

	/*
	 * Now that we have the complete list of mounted filesystems, we
	 * sort the mountpoints in reverse order based on the length of
	 * the 'mount point' name.
	 */
	qsort(fs_tab, fs_tab_used, sizeof (struct fstable *), fs_tab_ent_comp);
	if (strcmp(fs_tab[fs_tab_used-1]->name, rn) != 0)
		progerr(gettext(ERR_NOROOT), fs_tab[fs_tab_used-1]->name,
		    rn, errno);
}

/*
 * fsys - given a path, return the table index of the filesystem
 *	  the file resides on.
 *	(Lifted from vanilla SVr4 code, from pkginstall/dockspace.c)
 */
int
fsys(char *path)
{
	register int i;
	char	real_path[PATH_MAX];
	char	*path2use = NULL;
	char	*cp = NULL;
	int	pathlen;

	path2use = path;

	real_path[0] = '\0';

	(void) realpath(path, real_path);

	if (real_path[0]) {
		cp = strstr(path, real_path);
		if (cp != path || cp == NULL)
			path2use = real_path;
	}

	pathlen = strlen(path2use);

	/*
	 * The following algorithm scans the list of attached file systems
	 * for the one containing path. At this point the file names in
	 * fs_tab[] are sorted by decreasing length to facilitate the
	 * scan. The first for() scans past all the file system names too
	 * short to contain path. The second for() does the actual
	 * string comparison. It tests first to assure that the comparison
	 * is against a complete token by testing assuring that the end of
	 * the filesystem name aligns with the end of a token in path2use
	 * (ie: '/' or NULL) then it does a string compare. -- JST
	 */
	for (i = 0; i < fs_tab_used && fs_tab[i]->namlen > pathlen; i++)
		;
	for (; i < fs_tab_used; i++) {
		int fs_namelen = fs_tab[i]->namlen;
		char term_char = path2use[fs_namelen];

		/*
		 * If we're putting the file "/a/kernel" into the
		 * filesystem "/a", then fs_namelen == 2 and term_char == '/'.
		 * If, we're putting "/etc/termcap" into "/",
		 * fs_namelen == 1 and term_char (unfortunately) == 'e'. In
		 * the case of fs_namelen == 1, we check to make sure the
		 * filesystem is "/" and if it is, we have a guaranteed
		 * fit, otherwise we do the string compare. -- JST
		 */
		if (fs_namelen == 1 && *(fs_tab[i]->name) == '/')
			return (i);
		else if ((term_char == '/' || term_char == NULL) &&
		    strncmp(fs_tab[i]->name, path2use, fs_namelen) == 0)
			return (i);
	}

	progerr(gettext("fsys(): fell out of loop looking for <%s>"),
	    path2use);
	/* NOTREACHED */
}

/*
 * is_fs_writeable_n - given an fstab index, return 1
 *	if it's writeable, 0 if read-only.
 */
int
is_fs_writeable_n(int n)
{
	return (fs_tab[n]->writeable);
}

/*
 * is_remote_fs_n - given an fstab index, return 1
 *	if it's a remote filesystem, 0 if local.
 *
 *	Note: we treat loopback mounts as remote filesystems.
 *	That is, anything that's not "pure" UFS is considered
 *	to be remote.
 *
 *	Also Note: Upon exit, a valid fsys() is guaranteed. This is
 *	an interface requirement.
 */
int
is_remote_fs_n(int n)
{
	if (strcmp(fs_tab[n]->fstype, MNTTYPE_NFS) == 0 ||
	    strcmp(fs_tab[n]->fstype, MNTTYPE_LO) == 0)
		return (1);
	return (0);
}

/*
 * is_fs_writeable - given a cfent entry, return 1
 *	if it's writeable, 0 if read-only.
 *
 *	Note: Upon exit, a valid fsys() is guaranteed. This is
 *	an interface requirement.
 */
int
is_fs_writeable(char *path, int *fsys_value)
{
	if (*fsys_value == BADFSYS)
		*fsys_value = fsys(path);

	return (is_fs_writeable_n(*fsys_value));
}

/*
 * is_remote_fs - given a cfent entry, return 1
 *	if it's a remote filesystem, 0 if local.
 *
 *	Note: we treat loopback mounts as remote filesystems.
 *	That is, anything that's not "pure" UFS is considered
 *	to be remote.
 *
 *	Also Note: Upon exit, a valid fsys() is guaranteed. This is
 *	an interface requirement.
 */
int
is_remote_fs(char *path, int *fsys_value)
{
	if (*fsys_value == BADFSYS)
		*fsys_value = fsys(path);

	return (is_remote_fs_n(*fsys_value));
}

/*
 * get_remote_path - given a filesystem table index, return the
 *	path of the filesystem on the remote system.  Otherwise,
 *	return NULL if it's a local filesystem.
 */
char *
get_remote_path(int n)
{
	char	*p;

	if (!is_remote_fs_n(n))
		return (NULL); 	/* local */
	p = strchr(fs_tab[n]->remote_name, ':');
	if (!p)
		p = fs_tab[n]->remote_name; 	/* Loopback */
	else
		p++; 	/* remote */
	return (p);
}

/*
 * get_mount_point - given a filesystem table index, return the
 *	path of the mount point.  Otherwise,
 *	return NULL if it's a local filesystem.
 */
char *
get_mount_point(int n)
{
	if (!is_remote_fs_n(n))
		return (NULL); 	/* local */
	return (fs_tab[n]->name);
}
