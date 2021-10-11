/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)dockspace.c	1.25	94/10/21 SMI"	/* SVr4.0 1.7.1.1 */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#ifdef SUNOS41
#include <sys/vfs.h>
#else
#include <sys/statvfs.h>
#endif
#include <limits.h>
#include <locale.h>
#include <libintl.h>
#include <pkgstrct.h>
#include "install.h"
#include <pkglib.h>
#include "libadm.h"
#include "libinst.h"

extern struct cfextra **extlist;
extern char	pkgloc[];
extern char	pkgloc_sav[];
extern char	instdir[];
extern int	update;
/*
 * Imported from libinst/eptstat.c
 */
extern struct fstable	**fs_tab;
extern int	fs_tab_used, fs_tab_alloc;

#define	LSIZE		256
#define	LIM_BFREE	150L
#define	LIM_FFREE	25L

#define	WRN_STATVFS	"WARNING: unable to stat filesystem mounted on <%s>"

#define	WRN_NEEDBLK	"%lu free blocks are needed in the %s filesystem, "
#define	WRN_HAVEBLK	"but only %lu blocks are currently available."
#define	WRN_BLKMIN	"A minimum limit of %ld blocks is required."

#define	WRN_NEEDFILE	"%lu free file nodes are needed in the %s filesystem, "
#define	WRN_HAVEFILE	"but only %lu file nodes are currently available."
#define	WRN_FILEMIN	"A minimum limit of %ld file nodes is required."

#ifdef SUNOS41
#define	f_frsize	f_bsize
#define	f_favail	f_ffree
#endif

#define	TYPE_BLCK	0
#define	TYPE_NODE	1
static void	warn(int type, char *name, ulong need, ulong avail,
			long limit);
static int	fsys_stat(int n);
static int	readmap(int *error);
static int	readspace(char *spacefile, int *error, int op);

int
dockspace(char *spacefile)
{
	char	old_space[PATH_MAX];
	long	bfree, ffree;
	int	i, error;

	error = 0;

	/*
	 * Note!  The code to statvfs each mounted filesystem
	 * has been moved to the function fsys_stat(),
	 * called by readmap() and readspace(), below.
	 * This was done because doing it here caused the
	 * automounter to mount up everything, even filesystems
	 * that the package didn't require.  Not to mention
	 * the performance penalty.  Instead, we will
	 * only statvfs filesystems that the package explicitly
	 * requires, in the routine readmap().
	 *
	 * Also, vanilla SVr4 code used the output from popen()
	 * on the "/etc/mount" command.  However, we need to get more
	 * information about mounted filesystems, so we use the C
	 * interfaces to the mount table, which also happens to be
	 * much faster than running another process.  Since several
	 * of the pkg commands need access to the mount table, this
	 * code is now in libinst.  However, mount table info is needed
	 * at the time the base directory is determined, so the call
	 * to get the mount table information is in main.c
	 */

	if (update && pkgloc_sav && *pkgloc_sav) {
		sprintf(old_space, "%s/install/space", pkgloc_sav);
		if (!access(old_space, R_OK))
			(void) readspace(old_space, &error, -1);
	}

	if (readmap(&error) || readspace(spacefile, &error, 1))
		return (-1);

	for (i = 0; i < fs_tab_used; ++i) {
		if ((!fs_tab[i]->fused) && (!fs_tab[i]->bused))
			continue; /* not used by us */
		bfree = (long) fs_tab[i]->bfree - (long) fs_tab[i]->bused;
		/* bug id 1091292 */
		if (bfree < LIM_BFREE) {
			warn(TYPE_BLCK, fs_tab[i]->name, fs_tab[i]->bused,
				fs_tab[i]->bfree, LIM_BFREE);
			error++;
		}
		/* bug id 1091292 */
		if ((long) fs_tab[i]->ffree == -1L)
			continue;
		ffree = (long) fs_tab[i]->ffree - (long) fs_tab[i]->fused;
		if (ffree < LIM_FFREE) {
			warn(TYPE_NODE, fs_tab[i]->name, fs_tab[i]->fused,
				fs_tab[i]->ffree, LIM_FFREE);
			error++;
		}
	}
	return (error);
}

static void
warn(int type, char *name, ulong need, ulong avail, long limit)
{
	logerr(gettext("WARNING:"));
	if (type == TYPE_BLCK) {
		logerr(gettext(WRN_NEEDBLK), need, name);
		logerr(gettext(WRN_HAVEBLK), avail);
		logerr(gettext(WRN_BLKMIN), limit);
	} else {
		logerr(gettext(WRN_NEEDFILE), need, name);
		logerr(gettext(WRN_HAVEFILE), avail);
		logerr(gettext(WRN_FILEMIN), limit);
	}
}

static int
fsys_stat(int n)
{
#ifdef SUNOS41
	struct statfs svfsb;
#else
	struct statvfs svfsb;
#endif
	if (n == BADFSYS)
		return (1);

	/*
	 * This is the code that was in the function dockspace(),
	 * above.  At this point, we know we need information
	 * about a particular filesystem, so we can do the
	 * statvfs() now.  For performance reasons, we only want to
	 * stat the filesystem once, at the first time we need to,
	 * and so we can key on whether or not we have the
	 * block size for that filesystem.
	 */
	if (fs_tab[n]->bsize != 0)
		return (0);

#ifdef SUNOS41
	if (statfs(fs_tab[n]->name, &svfsb))
#else
	if (statvfs(fs_tab[n]->name, &svfsb))
#endif
	{
		logerr(gettext(WRN_STATVFS), fs_tab[n]->name);
		return (1);
	}

	/*
	 * statvfs returns number of fragment size blocks
	 * so will change this to number of 512 byte blocks
	 */
	fs_tab[n]->bsize  = svfsb.f_bsize;
	fs_tab[n]->frsize = svfsb.f_frsize;
	fs_tab[n]->bfree  = (((long) svfsb.f_frsize > 0) ?
		howmany(svfsb.f_frsize, DEV_BSIZE) :
		howmany(svfsb.f_bsize, DEV_BSIZE)) * svfsb.f_bavail;
	fs_tab[n]->ffree  = ((long) svfsb.f_favail > 0) ?
			    svfsb.f_favail : svfsb.f_ffree;

	return (0);
}

static int
readmap(int *error)
{
	struct cfextra *ext;
	struct cfent *ept;
	struct stat statbuf;
	char	tpath[PATH_MAX];
	long	blk;
	int	i, n;

#if 0	/* No wanted 3/24/93 */
	/*
	 * Handle the pkgmap file, the space check in ocfile() insures that
	 * the contents file is counted by now.
	 */
	(void) sprintf(tpath, "%s/pkgmap", instdir);
	if (stat(tpath, &statbuf) != -1) {
		(void) sprintf(tpath, "%s/pkgmap", pkgloc);
		n = fsys(tpath);

		if (!fsys_stat(n)) {
			if (!is_remote_fs_n(n) && fs_tab[n]->writeable) {
				fs_tab[n]->fused++;
				fs_tab[n]->bused += nblk(statbuf.st_size,
					fs_tab[n]->bsize,
					fs_tab[n]->frsize);
			}
		} else {
			(*error)++;
		}
	} else {
		(*error)++;
	}
#endif	/* 0 */

	/*
	 * Handle the installation files (ftype i) that are in the
	 * pkgmap/eptlist.
	 */
	for (i = 0; (ext = extlist[i]) != NULL; i++) {
		ept = &(ext->cf_ent);

		if (ept->ftype != 'i')
			continue;

		/*
		 * These paths are treated differently from the others
		 * since their full pathnames are not included in the
		 * pkgmap.
		 */
		if (strcmp(ept->path, "pkginfo") == 0)
			(void) sprintf(tpath, "%s/%s", pkgloc, ept->path);
		else
			(void) sprintf(tpath, "%s/install/%s", pkgloc,
			    ept->path);

		/* If we haven't done an fsys(), do one */
		if (ext->fsys_value == BADFSYS)
			ext->fsys_value = fsys(tpath);

		if (fsys_stat(ext->fsys_value)) {
			(*error)++;
			continue;
		}

		/*
		 * Don't accumulate space requirements on read-only
		 * remote filesystems.
		 */
		if (!is_remote_fs_n(ext->fsys_value) &&
		    !is_fs_writeable_n(ext->fsys_value))
			continue;

		fs_tab[ext->fsys_value]->fused++;
		if (ept->cinfo.size != BADCONT)
			blk = nblk(ept->cinfo.size,
			    fs_tab[ext->fsys_value]->bsize,
			    fs_tab[ext->fsys_value]->frsize);
		else
			blk = 0;
		fs_tab[ext->fsys_value]->bused += blk;
	}

	/*
	 * Handle the other files in the eptlist.
	 */
	for (i = 0; (ext = extlist[i]) != NULL; i++) {
		ept = &(extlist[i]->cf_ent);

		if (ept->ftype == 'i')
			continue;

		/*
		 * Don't accumulate space requirements on read-only
		 * remote filesystems.
		 */
		if (!is_remote_fs(ept->path, &(ext->fsys_value)) &&
		    !is_fs_writeable(ept->path, &(ext->fsys_value)))
			continue;

		/* At this point we know we have a good fsys_value. */
		if (fsys_stat(ext->fsys_value)) {
			(*error)++;
			continue;
		}

		if (stat(ept->path, &statbuf)) {
			/* path cannot be accessed */
			fs_tab[ext->fsys_value]->fused++;
			if (strchr("dxs", ept->ftype))
				blk =
				    nblk((long)fs_tab[ext->fsys_value]->bsize,
				    fs_tab[ext->fsys_value]->bsize,
				    fs_tab[ext->fsys_value]->frsize);
			else if (ept->cinfo.size != BADCONT)
				blk = nblk(ept->cinfo.size,
				    fs_tab[ext->fsys_value]->bsize,
				    fs_tab[ext->fsys_value]->frsize);
			else
				blk = 0;
		} else {
			/* path already exists */
			if (strchr("dxs", ept->ftype))
				blk = 0;
			else if (ept->cinfo.size != BADCONT) {
				blk = nblk(ept->cinfo.size,
				    fs_tab[ext->fsys_value]->bsize,
				    fs_tab[ext->fsys_value]->frsize);
				blk -= nblk(statbuf.st_size,
				    fs_tab[ext->fsys_value]->bsize,
				    fs_tab[ext->fsys_value]->frsize);
				/*
				 * negative blocks show room freed, but since
				 * order of installation is uncertain show
				 * 0 blocks usage
				 */
				if (blk < 0)
					blk = 0;
			} else
				blk = 0;
		}
		fs_tab[ext->fsys_value]->bused += blk;
	}
	return (0);
}

static int
readspace(char *spacefile, int *error, int op)
{
	FILE	*fp;
	char	*pt, path[PATH_MAX], line[LSIZE];
	long	blocks, nodes;
	int	n;

	if (spacefile == NULL)
		return (0);

	if ((fp = fopen(spacefile, "r")) == NULL) {
		progerr(gettext("unable to open spacefile %s"), spacefile);
		return (-1);
	}

	while (fgets(line, LSIZE, fp)) {
		for (pt = line; isspace(*pt); /* void */)
			pt++;
		if ((*line == '#') || !*line)
			continue;

		(void) sscanf(line, "%s %ld %ld", path, &blocks, &nodes);
		mappath(2, path);
		basepath(path, get_basedir(), get_inst_root());
		canonize(path);

		n = fsys(path);
		if (fsys_stat(n)) {
			(*error)++;
			continue;
		}

		/*
		 * Don't accumulate space requirements on read-only
		 * remote filesystems. (NOTE: For some reason, this
		 * used to check for !remote && read only. If this
		 * blows up later, then maybe that was correct -- JST)
		 */
		 if (is_remote_fs_n(n) && !is_fs_writeable_n(n))
			continue;

		fs_tab[n]->bused += (blocks * op);
		fs_tab[n]->fused += (nodes * op);
	}
	(void) fclose(fp);
	return (0);
}
