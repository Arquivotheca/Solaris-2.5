/*
 *
 *			fsck.c
 *
 * Cachefs fsck program.
 */

/*	Copyright (c) 1994, by Sun Microsystems, Inc. */
/*	  All Rights Reserved  	*/

#pragma ident "@(#)fsck.c   1.33     95/06/20 SMI"

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 *	PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 *	Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 *  (c) 1986,1987,1988.1989  Sun Microsystems, Inc
 *  (c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *	All rights reserved.
 *
 */

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <varargs.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <wait.h>
#include <ctype.h>
#include <fcntl.h>
#include <ftw.h>
#include <dirent.h>
#include <search.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/mount.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#include <sys/mman.h>
#include <sys/fs/cachefs_fs.h>
#include <syslog.h>
#include "../common/subr.h"
#include "res.h"

char *cfs_opts[] = {
#define		CFSOPT_PREEN		0
		"preen",
#define		CFSOPT_NOCLEAN		1
		"noclean",
#define		CFSOPT_VERBOSE		2
		"verbose",

		NULL
};

/* forward references */
void usage(char *msgp);
void pr_err(char *fmt, ...);
int cfs_check(char *cachedirp, int noclean, int mflag, int verbose);
int clean_flag_test(char *cachedirp);
int cache_label_file(char *cachedirp, struct cache_label *clabelp);
int cache_permissions(char *cachedirp);
int cache_back_mount_dir(char *cachedirp);
int process_fsdir(char *cachedirp, char *namep, res *resp, int verbose);
int process_fsgroup(char *dirp, char *namep, res *resp, int base, int fgsize,
    int fsid, int local, int verbose);
int tree_remove(const char *namep, const struct stat *statp, int type,
    struct FTW *ftwp);
int cache_upgrade(char *cachedirp, int lockid);

#define	FLAGS_FTW (FTW_PHYS | FTW_MOUNT | FTW_DEPTH)

/*
 *
 *			main
 *
 * Description:
 *	Main routine for the cachefs fsck program.
 * Arguments:
 *	argc	number of command line arguments
 *	argv	list of command line arguments
 * Returns:
 *	Returns:
 *		 0	file system is okay and does not need checking
 *		 1	problem unrelated to the file system
 *		32	file system is unmounted and needs checking  (fsck
 *			-m only)
 *		33	file system is already mounted
 *		34	cannot stat device
 *		36	uncorrectable errors detected - terminate normally
 *		37	a signal was caught during processing
 *		39	uncorrectable errors detected - terminate  immediately
 *		40	for root mounted fs, same as 0
 * Preconditions:
 */

main(int argc, char **argv)
{
	int xx;
	int c;
	char *optionp;
	char *valuep;
	int mflag;
	int noclean;
	char *cachedirp;
	int lockid;
	int verbose;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN	"SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	/* verify root running command */
	if (getuid() != 0) {
		fprintf(stderr, gettext(
			"fsck -F cachefs: must be run by root\n"));
		return (1);
	}

	/* process command line options */
	optionp = NULL;
	mflag = 0;
	noclean = 0;
	verbose = 0;
	while ((c = getopt(argc, argv, "mnNo:yY")) != EOF) {
		switch (c) {
		case 'm':	/* check but do not repair */
			mflag = 1;
			break;

		case 'n':	/* answer no to questions */
		case 'N':
			/* ignored */
			break;

		case 'o':
			optionp = optarg;
			while (*optionp) {
				xx = getsubopt(&optionp, cfs_opts, &valuep);
				switch (xx) {
				case CFSOPT_PREEN:
					/* preen is the default mode */
					break;
				case CFSOPT_NOCLEAN:
					noclean = 1;
					break;
				case CFSOPT_VERBOSE:
					verbose++;
					break;
				default:
				case -1:
					pr_err(gettext("unknown option %s"),
					    valuep);
					return (1);
				}
			}
			break;

		case 'y':	/* answer yes to questions */
		case 'Y':
			/* ignored, this is the default */
			break;

		default:
			usage("invalid option");
			return (1);
		}
	}

	/* verify fsck device is specified */
	if (argc - optind < 1) {
		usage(gettext("must specify cache directory"));
		return (1);
	}

	/* save cache directory */
	cachedirp = argv[argc - 1];

	/* ensure cache directory exists */
	if (access(cachedirp, F_OK) != 0) {
		pr_err(gettext("Cache directory %s does not exist."),
		    cachedirp);
		return (39);
	}

	/* lock the cache directory non-shared */
	lockid = cachefs_dir_lock(cachedirp, 0);
	if (lockid == -1) {
		/* exit if could not get the lock */
		return (1);
	}

	/* is the cache directory in use */
	if (cachefs_inuse(cachedirp)) {
		if (noclean) {
			pr_err(gettext("Cache directory %s is in use."),
			    cachedirp);
			xx = 33;
		} else {
			/* assume if in use that it is clean */
			xx = 0;
		}
		cachefs_dir_unlock(lockid);
		return (xx);
	}

	xx = cache_upgrade(cachedirp, lockid);
	if (xx != 0) {
		/* check the file system */
		xx = cfs_check(cachedirp, noclean, mflag, verbose);
	}

	/* unlock the cache directory */
	cachefs_dir_unlock(lockid);

	/* return the status of the file system checking */
	return (xx);
}

/*
 *
 *			usage
 *
 * Description:
 *	Prints a short usage message.
 * Arguments:
 *	msgp	message to include with the usage message
 * Returns:
 * Preconditions:
 */

void
usage(char *msgp)
{
	if (msgp) {
		pr_err("%s", msgp);
	}

	(void) fprintf(stderr,
	    gettext("Usage: fsck -F cachefs [ -o specific_options ] [ -m ] "
	    "cachedir\n"));
}

/*
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
	(void) fprintf(stderr, gettext("fsck -F cachefs: "));
	(void) vfprintf(stderr, fmt, ap);
	(void) fprintf(stderr, "\n");
	va_end(ap);
}

/*
 *
 *			cache_upgrade
 *
 * Description:
 *
 *      See if the current cache is out of date.  If it is, do
 *      whatever magic is necessary to upgrade it.  All such magic
 *      should be encapsulated here!
 *
 * Arguments:
 *
 *	cachedirp	name of the cache directory to check
 *
 * Returns:
 *	Returns:
 *		 0	cache was upgraded and shouldn't be checked
 *		 1	problem unrelated to the file system
 *		36	uncorrectable errors detected - terminate normally
 *		39	uncorrectable errors detected - terminate  immediately
 *		50	cache was already up-to-date (maybe we should fsck it)
 *		51	cache was upgraded (but you should do fsck)
 * Preconditions:
 *	precond(cachedirp)
 */

int
cache_upgrade(char *cachedirp, int lockid)
{
	static int canupgrade[] = {2, 1, 0};
	char labelpath[MAXPATHLEN];
	struct cache_label clabel;
	int i;

	if (((int) strlen(cachedirp) + (int) strlen(CACHELABEL_NAME))
	    >= MAXPATHLEN)
		return (1);

	(void) sprintf(labelpath, "%s/%s", cachedirp, CACHELABEL_NAME);

	if (cachefs_label_file_get(labelpath, &clabel) != 0)
		return (1);

	/* nothing to do if we're current */
	if (clabel.cl_cfsversion == CFSVERSION)
		return (50);

	/* see if it's an old version that we know how to upgrade */
	for (i = 0; canupgrade[i] != 0; i++)
		if (clabel.cl_cfsversion == canupgrade[i])
			break;
	if (canupgrade[i] == 0)
		return (36);

	syslog(LOG_USER | LOG_INFO,
	    gettext("fsck -F cachefs: Recreating cache %s"), cachedirp);

	/* currently, to `upgrade' we delete the old cache */
	if (cachefs_delete_all_cache(cachedirp, 1) != 0)
		return (36);

	/* do any magic necessary to convert the old label to the new one */
	clabel.cl_cfsversion = CFSVERSION;

	/* create the new cache! */
	if (cachefs_create_cache(cachedirp, NULL, &clabel, lockid) != 0)
		return (36);

	return (0);
}

/*
 *
 *			cfs_check
 *
 * Description:
 *	This routine performs the actual checking of the cache
 *	file system.
 *	The file system must be inactive when this routine is called.
 * Arguments:
 *	cachedirp	name of the cache directory to check
 *	noclean		1 means ignore clean flag
 *	mflag		1 means no fixes, only check if mountable
 *	verbose		indicate level of verbosity for diagnostics
 * Returns:
 *	Returns:
 *		 0	file system is okay and does not need checking
 *		 1	problem unrelated to the file system
 *		32	file system is unmounted and needs checking
 *		33	file system is already mounted
 *		34	cannot stat device
 *		36	uncorrectable errors detected - terminate normally
 *		37	a signal was caught during processing
 *		39	uncorrectable errors detected - terminate  immediately
 *		40	for root mounted fs, same as 0, XXX
 * Preconditions:
 *	precond(cachedirp)
 */

int
cfs_check(char *cachedirp, int noclean, int mflag, int verbose)
{
	DIR *dp;
	struct dirent *dep;
	char buf[MAXPATHLEN];
	struct stat statinfo;
	int xx;
	char *namep;
	res *resp;
	struct cache_label clabel;

	/* if checking the clean flag is sufficient */
	if (noclean == 0) {
		/* if the clean flag is set */
		if (clean_flag_test(cachedirp))
			return (0);
	}

	/* if mflag specified then go no farther */
	if (mflag)
		return (32);

	/* check the cache label file for correctness */
	xx = cache_label_file(cachedirp, &clabel);
	if (xx)
		return (xx);

	/* fix permissions on the cache directory */
	xx = cache_permissions(cachedirp);
	if (xx)
		return (xx);

	/* make the back file system mount directory if necessary */
	xx = cache_back_mount_dir(cachedirp);
	if (xx)
		return (xx);

	/* construct the path name of the resource file */
	namep = RESOURCE_NAME;
	xx = strlen(cachedirp) + strlen(namep) + 3;
	if (xx >= MAXPATHLEN) {
		pr_err(gettext("Path name too long %s/%s"),
		    cachedirp, namep);
		return (39);
	}
	sprintf(buf, "%s/%s", cachedirp, namep);

	/* make a res object to operate on the resource file */
	resp = res_create(buf, clabel.cl_maxinodes);
	if (resp == NULL) {
		pr_err(gettext("Could not process resource file %s: %s"),
		    buf, strerror(errno));
		return (39);
	}

	/* open the cache directory */
	if ((dp = opendir(cachedirp)) == NULL) {
		pr_err(gettext("Cannot open directory %s: %s"), cachedirp,
		    strerror(errno));
		res_destroy(resp);
		return (39);
	}

	/* mark all directories */
	while ((dep = readdir(dp)) != NULL) {
		/* ignore . and .. */
		if (strcmp(dep->d_name, ".") == 0 ||
				strcmp(dep->d_name, "..") == 0)
			continue;

		/* check path length */
		xx = strlen(cachedirp) + strlen(dep->d_name) + 3;
		if (xx >= MAXPATHLEN) {
			pr_err(gettext("Path name too long %s/%s"),
			    cachedirp, dep->d_name);
			closedir(dp);
			res_destroy(resp);
			return (39);
		}

		/* stat the file */
		sprintf(buf, "%s/%s", cachedirp, dep->d_name);
		xx = lstat(buf, &statinfo);
		if (xx == -1) {
			if (errno != ENOENT) {
				pr_err(gettext("Cannot stat %s: %s"), cachedirp,
				    strerror(errno));
				closedir(dp);
				res_destroy(resp);
				return (39);
			}
			continue;
		}

		/* if a directory */
		if (S_ISDIR(statinfo.st_mode)) {
			xx = chmod(buf, 0700);
			if (xx == -1) {
				pr_err(gettext("Cannot chmod %s: %s"), buf,
				    strerror(errno));
				closedir(dp);
				res_destroy(resp);
				return (39);
			}
		}
	}

	/* process files in the cache directory */
	rewinddir(dp);
	while ((dep = readdir(dp)) != NULL) {
		/* ignore . and .. */
		if (strcmp(dep->d_name, ".") == 0 ||
				strcmp(dep->d_name, "..") == 0)
			continue;

		/* stat the file */
		sprintf(buf, "%s/%s", cachedirp, dep->d_name);
		xx = lstat(buf, &statinfo);
		if (xx == -1) {
			if (errno != ENOENT) {
				pr_err(gettext("Cannot stat %s: %s"), cachedirp,
				    strerror(errno));
				closedir(dp);
				res_destroy(resp);
				return (39);
			}
			continue;
		}

		/* ignore directories */
		if (S_ISDIR(statinfo.st_mode))
			continue;

		/* if not a link */
		if (!S_ISLNK(statinfo.st_mode)) {
			/* XXX make sure a valid file */
			/* update file and block counts for this file */
			res_addfile(resp, statinfo.st_size);
			continue;
		}

		/* process the file system cache directory */
		xx = process_fsdir(cachedirp, dep->d_name, resp, verbose);
		if (xx) {
			closedir(dp);
			res_destroy(resp);
			return (xx);
		}
	}

	/* look for directories that do not belong */
	rewinddir(dp);
	while ((dep = readdir(dp)) != NULL) {
		/* ignore . and .. */
		if (strcmp(dep->d_name, ".") == 0 ||
				strcmp(dep->d_name, "..") == 0)
			continue;

		/* stat the file */
		sprintf(buf, "%s/%s", cachedirp, dep->d_name);
		xx = lstat(buf, &statinfo);
		if (xx == -1) {
			if (errno != ENOENT) {
				pr_err(gettext("Cannot stat %s: %s"), cachedirp,
				    strerror(errno));
				closedir(dp);
				res_destroy(resp);
				return (39);
			}
			continue;
		}

		/* ignore all but directories */
		if (!S_ISDIR(statinfo.st_mode))
			continue;

		/* ignore directories we have seen */
		if ((statinfo.st_mode & S_IAMB) != 0700)
			continue;

		/* ignore the mount directory */
		if (strcmp(dep->d_name, BACKMNT_NAME) == 0)
			continue;

		/* remove the directory */
		xx = nftw(buf, tree_remove, 3, FLAGS_FTW);
		if (xx != 0) {
			pr_err(gettext("Error walking tree %s."), namep);
			closedir(dp);
			res_destroy(resp);
			return (39);
		}

		pr_err(gettext("Directory removed: %s"), buf);
	}

	/* close the directory */
	closedir(dp);

	/* add one file and one block for the cache directory itself */
	res_addfile(resp, 1);

	/* finish off the resource file processing */
	xx = res_done(resp);
	if (xx == -1) {
		pr_err(gettext("Could not finish resource file %s: %s"),
		    buf, strerror(errno));
		return (39);
	}
	res_destroy(resp);

	/* return success */
	return (0);
}

/*
 *
 *			clean_flag_test
 *
 * Description:
 *	Tests whether or not the clean flag on the file system
 *	is set.
 * Arguments:
 *	cachedirp	name of the the file system cache directory
 * Returns:
 *	Returns 1 if the cache was shut down cleanly, 0 if not.
 * Preconditions:
 *	precond(cachedirp)
 */

int
clean_flag_test(char *cachedirp)
{
	char *namep;
	int xx;
	char buf[MAXPATHLEN];
	int fd;
	struct cache_usage cu;

	/* construct the path name of the resource file */
	namep = RESOURCE_NAME;
	xx = strlen(cachedirp) + strlen(namep) + 3;
	if (xx >= MAXPATHLEN) {
		pr_err(gettext("Path name too long %s/%s"),
		    cachedirp, namep);
		return (39);
	}
	sprintf(buf, "%s/%s", cachedirp, namep);

	/* open the file */
	fd = open(buf, O_RDONLY);
	if (fd == -1) {
		pr_err(gettext("Cannot open %s: %s"), buf, strerror(errno));
		return (0);
	}

	/* read the cache_usage structure */
	xx = read(fd, &cu, sizeof (cu));
	if (xx != sizeof (cu)) {
		pr_err(gettext("Error reading %s: %d %s"), buf,
		    xx, strerror(errno));
		close(fd);
		return (0);
	}
	close(fd);

	/* return state of the cache */
	return ((cu.cu_flags & CUSAGE_ACTIVE) == 0);
}

/*
 *
 *			cache_label_file
 *
 * Description:
 *	This routine performs the checking and fixing up of the
 *	cache label file.
 * Arguments:
 *	cachedirp	name of the cache directory to check
 *	clabelp		cache label contents put here if not NULL
 * Returns:
 *		 0	file system is okay and does not need checking
 *		 1	problem unrelated to the file system
 *		32	file system is unmounted and needs checking
 *		33	file system is already mounted
 *		34	cannot stat device
 *		36	uncorrectable errors detected - terminate normally
 *		37	a signal was caught during processing
 *		39	uncorrectable errors detected - terminate  immediately
 * Preconditions:
 *	precond(cachedirp)
 */

int
cache_label_file(char *cachedirp, struct cache_label *clabelp)
{
	int xx;
	char buf1[MAXPATHLEN];
	char buf2[MAXPATHLEN];
	char *namep;
	struct cache_label clabel1, clabel2;

	namep = CACHELABEL_NAME;

	/* see if path name is too long */
	xx = strlen(cachedirp) + strlen(namep) + 10;
	if (xx >= MAXPATHLEN) {
		pr_err(gettext("Cache directory name %s is too long"),
		    cachedirp);
		return (39);
	}

	/* make a path to the cache label file and its backup copy */
	sprintf(buf1, "%s/%s", cachedirp, namep);
	sprintf(buf2, "%s/%s.dup", cachedirp, namep);

	/* get the contents of the cache label file */
	xx = cachefs_label_file_get(buf1, &clabel1);
	if (xx == -1) {
		/* get the backup cache label file contents */
		xx = cachefs_label_file_get(buf2, &clabel2);
		if (xx == -1) {
			pr_err(gettext("Run cfsadmin and then run fsck."));
			return (39);
		}

		/* write the cache label file */
		xx = cachefs_label_file_put(buf1, &clabel2);
		if (xx == -1) {
			return (39);
		}
		pr_err(gettext("Cache label file %s repaired."), buf1);

		/* copy out the contents to the caller */
		if (clabelp)
			*clabelp = clabel2;

		/* return success */
		return (0);
	}

	/* get the contents of the backup cache label file */
	xx = cachefs_label_file_get(buf2, &clabel2);
	if (xx == -1) {
		/* write the backup cache label file */
		xx = cachefs_label_file_put(buf2, &clabel1);
		if (xx == -1) {
			return (39);
		}
		pr_err(gettext("Cache label file %s repaired."), buf2);
	}

	/* copy out the contents to the caller */
	if (clabelp)
		*clabelp = clabel1;

	/* return success */
	return (0);
}

/*
 *
 *			cache_permissions
 *
 * Description:
 *	Checks the permissions on the cache directory and fixes
 *	them if necessary.
 * Arguments:
 *	cachedirp	name of the cache directory to check
 * Returns:
 *		 0	file system is okay and does not need checking
 *		 1	problem unrelated to the file system
 *		32	file system is unmounted and needs checking
 *		33	file system is already mounted
 *		34	cannot stat device
 *		36	uncorrectable errors detected - terminate normally
 *		37	a signal was caught during processing
 *		39	uncorrectable errors detected - terminate  immediately
 * Preconditions:
 *	precond(cachedirp)
 */

int
cache_permissions(char *cachedirp)
{
	int xx;
	struct stat statinfo;

	/* get info about the cache directory */
	xx = lstat(cachedirp, &statinfo);
	if (xx == -1) {
		pr_err(gettext("Could not stat %s: %s"), cachedirp,
		    strerror(errno));
		return (34);
	}

	/* check the mode bits */
	if ((statinfo.st_mode & S_IAMB) != 0) {

		/* fix the mode bits */
		xx = chmod(cachedirp, 0);
		if (xx == -1) {
			pr_err(gettext("Could not set modes bits on "
			    "cache directory %s: %s"),
			    cachedirp, strerror(errno));
			return (1);
		}
		pr_err(gettext("Mode bits reset on cache directory %s"),
		    cachedirp);
	}

	/* return success */
	return (0);
}

/*
 *
 *			cache_back_mount_dir
 *
 * Description:
 *	Checks for the existance of the back file system mount
 *	directory and creates it if necessary.
 * Arguments:
 *	cachedirp	name of the cache directory containing the dir
 * Returns:
 *		 0	file system is okay and does not need checking
 *		 1	problem unrelated to the file system
 *		32	file system is unmounted and needs checking
 *		33	file system is already mounted
 *		34	cannot stat device
 *		36	uncorrectable errors detected - terminate normally
 *		37	a signal was caught during processing
 *		39	uncorrectable errors detected - terminate  immediately
 * Preconditions:
 *	precond(cachedirp)
 */

int
cache_back_mount_dir(char *cachedirp)
{
	int xx;
	char *namep;
	char buf[MAXPATHLEN];
	struct stat statinfo;

	namep = BACKMNT_NAME;

	/* see if path name is too long */
	xx = strlen(cachedirp) + strlen(namep) + 3;
	if (xx >= MAXPATHLEN) {
		pr_err(gettext("Cache directory name %s is too long"),
		    cachedirp);
		return (39);
	}

	/* make the pathname of the directory */
	sprintf(buf, "%s/%s", cachedirp, namep);

	/* get info on the directory */
	xx = lstat(buf, &statinfo);
	if (xx == -1) {
		/* if an error other than it does not exist */
		if (errno != ENOENT) {
			pr_err(gettext("Error on lstat(2) of %s: %s"),
			    buf, strerror(errno));
			return (39);
		}

		/* make the directory */
		xx = mkdir(buf, 0);
		if (xx == -1) {
			pr_err(gettext("Could not create directory %s"),
			    buf);
			return (39);
		}
		pr_err(gettext("Created directory %s"), buf);
	}

	/* else see if really a directory */
	else if (!S_ISDIR(statinfo.st_mode)) {
		/* get rid of the file */
		xx = unlink(buf);
		if (xx == -1) {
			pr_err(gettext("Cannot remove %s: %s"), buf,
			    strerror(errno));
			return (39);
		}

		/* make the directory */
		xx = mkdir(buf, 0);
		if (xx == -1) {
			pr_err(gettext("Could not create directory %s"),
			    buf);
			return (39);
		}
		pr_err(gettext("Created directory %s"), buf);
	}

	/* return success */
	return (0);
}

/*
 *
 *			process_fsdir
 *
 * Description:
 *	Performs the necessary checking and repair on the
 *	specified file system cache directory.
 *	Calls res_addfile and res_addident as appropriate.
 * Arguments:
 *	cachedirp	name of cache directory
 *	namep		name of link file for the file system cache
 *	resp		res object for res_addfile and res_addident calls
 *	verbose		indicate level of verbosity for diagnostics
 * Returns:
 *		 0	file system is okay and does not need checking
 *		 1	problem unrelated to the file system
 *		32	file system is unmounted and needs checking
 *		33	file system is already mounted
 *		34	cannot stat device
 *		36	uncorrectable errors detected - terminate normally
 *		37	a signal was caught during processing
 *		39	uncorrectable errors detected - terminate  immediately
 * Preconditions:
 *	precond(cachedirp)
 *	precond(namep && is a sym link)
 *	precond(resp)
 */

int
process_fsdir(char *cachedirp, char *namep, res *resp, int verbose)
{
	DIR *dp;
	struct dirent *dep;
	char linkpath[MAXPATHLEN];
	char dirpath[MAXPATHLEN];
	char attrpath[MAXPATHLEN];
	char buf[MAXPATHLEN];
	int xx;
	struct stat statinfo;
	char *op = OPTION_NAME;
	char *atp = ATTRCACHE_NAME;
	int fd;
	struct cachefsoptions cfsopt;
	int base;
	int local;
	char *strp;
	int fsid;
	int error = 0;
	int hashsize = 0;
	ENTRY hitem;

	/* construct the path to the sym link */
	xx = strlen(cachedirp) + strlen(namep) + 3;
	if (xx >= MAXPATHLEN) {
		pr_err(gettext("Pathname too long %s/%s"), cachedirp, namep);
		error = 39;
		goto out;
	}
	sprintf(linkpath, "%s/%s", cachedirp, namep);

	/* read the contents of the link */
	xx = readlink(linkpath, buf, sizeof (buf));
	if (xx == -1) {
		pr_err(gettext("Unable to read link %s: %s"), linkpath,
		    strerror(errno));
		error = 39;
		goto out;
	}
	buf[xx] = '\0';

	/* do a one time check on lengths of files */
	xx = strlen(cachedirp) + strlen(buf) + 20 + 20;
	if (xx >= MAXPATHLEN) {
		pr_err(gettext("Pathname too long %s/%s"), cachedirp, buf);
		error = 39;
		goto out;
	}

	/* construct the path to the directory */
	sprintf(dirpath, "%s/%s", cachedirp, buf);

	/* stat the directory */
	xx = lstat(dirpath, &statinfo);
	if ((xx == -1) || (strtol(buf, NULL, 16) != statinfo.st_ino)) {
		if ((xx == -1) && (errno != ENOENT)) {
			pr_err(gettext("Could not stat %s: %s"), dirpath,
			    strerror(errno));
			error = 39;
		} else
			error = -1;
		goto out;
	}
	fsid = statinfo.st_ino;

	/* construct the name to the options file */
	sprintf(buf, "%s/%s", dirpath, op);

	/* open the options file */
	fd = open(buf, O_RDONLY);
	if (fd == -1) {
		pr_err(gettext("Could not open %s: %s"), buf, strerror(errno));
		error = -1;
		goto out;
	}

	/* read the contents of the option file */
	xx = read(fd, &cfsopt, sizeof (cfsopt));
	if ((xx != sizeof (cfsopt)) || (cfsopt.opt_fgsize <= 0)) {
		close(fd);
		error = -1;
		goto out;
	}
	close(fd);

	/* construct the name to the attrcache directory */
	sprintf(attrpath, "%s/%s", dirpath, atp);

	/* open the attrcache directory */
	if ((dp = opendir(attrpath)) == NULL) {
		pr_err(gettext("Cannot open directory %s: %s"), attrpath,
		    strerror(errno));
		error = -1;
		goto out;
	}

	/* make one pass, counting how big to make the hash table */
	while (readdir(dp) != NULL)
		++hashsize;
	if (hcreate(hashsize + 1000) == 0) {
		pr_err(gettext("Cannot allocate heap space."));
		(void) closedir(dp);
		error = 39;
		goto out;
	}
	rewinddir(dp);

	/* loop reading the contents of the directory */
	while ((dep = readdir(dp)) != NULL) {
		/* ignore . and .. */
		if (strcmp(dep->d_name, ".") == 0 ||
				strcmp(dep->d_name, "..") == 0)
			continue;

		/* check for a reasonable name */
		xx = strlen(dep->d_name);
		if ((xx != 8) && (xx != 9)) {
			/* bad file */
			pr_err(gettext("Unknown file %s/%s"),
				attrpath, dep->d_name);
			closedir(dp);
			error = 39;
			goto out;
		}

		/* derive the base number from the file name */
		if (*(dep->d_name) == 'L') {
			local = 1;
			base = strtol(dep->d_name + 1, &strp, 16);
		} else {
			local = 0;
			base = strtol(dep->d_name, &strp, 16);
		}
		if (*strp != '\0') {
			/* bad file */
			pr_err(gettext("Unknown file %s/%s"),
				attrpath, dep->d_name);
			closedir(dp);
			error = 39;
			goto out;
		}

		/* process the file group */
		error = process_fsgroup(dirpath, dep->d_name, resp,
			base, cfsopt.opt_fgsize, fsid, local, verbose);
		if (error) {
			closedir(dp);
			goto out;
		}
	}
	closedir(dp);

	/* open the fscache directory */
	if ((dp = opendir(dirpath)) == NULL) {
		pr_err(gettext("Cannot open directory %s: %s"), dirpath,
		    strerror(errno));
		error = 39;
		goto out;
	}

	/* loop reading the contents of the directory */
	while ((dep = readdir(dp)) != NULL) {
		/* ignore . and .. */
		if (strcmp(dep->d_name, ".") == 0 ||
				strcmp(dep->d_name, "..") == 0)
			continue;

		/* ignore the options file */
		if (strcmp(dep->d_name, op) == 0)
			continue;

		/* ignore the attrcache directory */
		if (strcmp(dep->d_name, atp) == 0)
			continue;

		/* ignore the root dir symlink */
		if (strcmp(dep->d_name, ROOTLINK_NAME) == 0)
			continue;

		hitem.key = dep->d_name;
		hitem.data = NULL;
		if (hsearch(hitem, FIND) == NULL) {
			sprintf(buf, "%s/%s", dirpath, dep->d_name);
			if (verbose) {
				printf("Unreferenced dir %s\n", buf);
			}
			xx = nftw(buf, tree_remove, 3, FLAGS_FTW);
			if (xx != 0) {
				pr_err(gettext("Could not remove %s"), buf);
				error = 39;
				closedir(dp);
				goto out;
			}
		}
	}
	closedir(dp);

	/* add the options file to the resource */
	res_addfile(resp, 1);

	/* add the directory to the resources */
	res_addfile(resp, 1);

	/* add the sym link to the resources */
	res_addfile(resp, 1);

	/* change the mode on the directory to indicate we visited it */
	xx = chmod(dirpath, 0777);
	if (xx == -1) {
		pr_err(gettext("Cannot chmod %s: %s"), dirpath,
		    strerror(errno));
		error = 39;
		goto out;
	}

out:
	/* free up the heap allocated by the hash functions */
	hdestroy();

	if (error == -1) {
		/* remove the sym link */
		xx = unlink(linkpath);
		if (xx == -1) {
			pr_err(gettext("Unable to remove %s: %s"), linkpath,
			    strerror(errno));
			error = 39;
		} else {
			error = 0;
		}
	}

	return (error);
}

/*
 *
 *			process_fsgroup
 *
 * Description:
 *	Performs the necessary checking and repair on the
 *	specified file group directory.
 *	Calls res_addfile and res_addident as appropriate.
 * Arguments:
 *	dirpath	pathname to fscache directory
 *	namep	name of fsgroup
 *	resp	res object for res_addfile and res_addident calls
 *	base	base offset for file numbers in this directory
 *	fgsize	size of the file groups
 *	fsid	file system id
 *	local	1 if fsgroup dir is a local dir
 *	verbose		indicate level of verbosity for diagnostics
 * Returns:
 *		 0	file system is okay and does not need checking
 *		 1	problem unrelated to the file system
 *		32	file system is unmounted and needs checking
 *		33	file system is already mounted
 *		34	cannot stat device
 *		36	uncorrectable errors detected - terminate normally
 *		37	a signal was caught during processing
 *		39	uncorrectable errors detected - terminate  immediately
 * Preconditions:
 *	precond(dirp)
 *	precond(namep)
 *	precond(resp)
 *	precond(fgsize > 0)
 */

int
process_fsgroup(char *dirp, char *namep, res *resp, int base, int fgsize,
    int fsid, int local, int verbose)
{
	DIR *dp;
	struct dirent *dep;
	char buf[MAXPATHLEN];
	char attrfile[MAXPATHLEN];
	char attrdir[MAXPATHLEN];
	int xx;
	struct stat statinfo;
	char *atp = ATTRCACHE_NAME;
	void *addrp;
	struct attrcache_header *ahp;
	struct attrcache_index *startp;
	struct attrcache_index *aip;
	uchar_t *bitp;
	int offlen;
	int bitlen;
	int fd;
	int offentry;
	int size;
	struct cachefs_metadata *metap;
	int index;
	char *strp;
	uint_t offset;
	int error = 0;
	ENTRY hitem;
	int nffs;
	int lruno;

	/* construct the name to the attribute file and front file dir */
	sprintf(attrfile, "%s/%s/%s", dirp, atp, namep);
	sprintf(attrdir, "%s/%s", dirp, namep);

	/* get the size of the attribute file */
	xx = lstat(attrfile, &statinfo);
	if (xx == -1) {
		pr_err(gettext("Could not stat %s: %s"), attrfile,
		    strerror(errno));
		error = 39;
		goto out;
	}

	offlen = sizeof (struct attrcache_index) * fgsize;
	bitlen = (sizeof (uchar_t) * fgsize + 7) / 8;
	size = statinfo.st_size;
	offentry = sizeof (struct attrcache_header) + offlen + bitlen;

	/* if the attribute file is the wrong size */
	if (size < offentry) {
		error = -1;
		goto out;
	}

	/* open the attribute file */
	fd = open(attrfile, O_RDWR);
	if (fd == -1) {
		pr_err(gettext("Could not open %s: %s"),
			attrfile, strerror(errno));
		error = 39;
		goto out;
	}

	/* mmap the file into our address space */
	addrp = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (addrp == (void *)-1) {
		pr_err(gettext("Could not map %s: %s"),
			attrfile, strerror(errno));
		close(fd);
		error = 39;
		goto out;
	}
	close(fd);

	/* set up pointers into mapped file */
	ahp = (struct attrcache_header *)addrp;
	startp = (struct attrcache_index *)(ahp + 1);
	bitp = (uchar_t *)((char *)startp + offlen);

	/* clear the bitmap */
	memset(bitp, 0, bitlen);

	/* fix number of allocated blocks value if necessary */
	xx = (size + MAXBSIZE - 1) / MAXBSIZE;
	if (xx != ahp->ach_nblks) {
		if (verbose) {
			pr_err(gettext("File %s size wrong, old %d new %d:"
				"corrected."),
				attrfile, ahp->ach_nblks, xx);
		}
		ahp->ach_nblks = xx;
	}
	ahp->ach_nffs = 0;

	/* verify sanity of attribute file */
	ahp->ach_count = 0;
	for (index = 0; index < fgsize; index++) {

		/* get next entry to work on */
		aip = startp + index;

		/* save offset to data */
		offset = aip->ach_offset;
		aip->ach_offset = 0;

		/* if entry not in use */
		if (aip->ach_written == 0)
			continue;
		aip->ach_written = 0;

		/* if offset is out of range or invalid */
		if ((offset < offentry) ||
		    ((size - sizeof (struct cachefs_metadata)) < offset) ||
		    (offset & 3)) {
			pr_err(gettext("Offset %d invalid - index %d"),
			    offset, index);
			continue;
		}

		/* get pointer to meta data */
		metap = (struct cachefs_metadata *)((char *)addrp + offset);

		/* sanity check the meta data */
		if ((metap->md_vattr.va_nodeid != (base + index)) ||
		    (local && ((base + index) != metap->md_lruno)) ||
		    ((metap->md_flags & (MD_FILE | MD_POPULATED)) ==
		    MD_POPULATED) ||
		    ((metap->md_flags & MD_FILE) && (metap->md_lruno == 0)) ||
		    (((metap->md_flags & MD_FILE) == 0) &&
		    (metap->md_lruno != 0))) {
			if (verbose) {
				pr_err(gettext("Metadata corrupted %d"), index);
			}
			continue;
		}

		/* if there is a front file */
		if (metap->md_lruno != 0) {
			/* add to the resource file idents */
			xx = local ||
			    (metap->md_flags & MD_PINNED);
			xx = res_addident(resp, metap->md_lruno, fsid,
			    base + index, xx, 0, !xx,
			    (metap->md_frontblks * MAXBSIZE));
			if (xx == -1) {
				if (verbose) {
					pr_err(gettext(
					    "File %s, bad lruno"), attrfile);
				}
				continue;
			}
			ahp->ach_nffs++;
		}

		/* mark entry as valid */
		aip->ach_written = 1;
		aip->ach_offset = offset;

		/* set bitmap for this entry */
		xx = (offset - offentry) / sizeof (struct cachefs_metadata);
		bitp[xx/8] |= 1 << (xx % 8);

		/* bump number of active entries */
		ahp->ach_count += 1;
	}

	/* open the front file directory if there should be front files */
	nffs = 0;
	dp = NULL;
	if (ahp->ach_nffs > 0) {
		dp = opendir(attrdir);
	}

	/* loop reading the contents of the front file directory */
	while (dp && ((dep = readdir(dp)) != NULL)) {
		/* ignore . and .. */
		if (strcmp(dep->d_name, ".") == 0 ||
				strcmp(dep->d_name, "..") == 0)
			continue;

		/* check for a reasonable name */
		xx = strlen(dep->d_name);
		if ((xx != 8) && (xx != 9)) {
			/* bad file */
			pr_err(gettext("Unknown file %s/%s"),
				attrdir, dep->d_name);
			closedir(dp);
			munmap(addrp, size);
			error = 39;
			goto out;
		}

		sprintf(buf, "%s/%s", attrdir, dep->d_name);

		/* verify a valid file */
		index = strtol(dep->d_name, &strp, 16) - base;
		if ((*strp != '\0') || (index < 0) || (fgsize <= index) ||
		    (startp[index].ach_written == 0)) {
			/* remove the file */
			xx = unlink(buf);
			if (xx == -1) {
				pr_err(gettext("Could not remove file %s: %s"),
				    buf, strerror(errno));
			} else if (verbose) {
				pr_err(gettext("File %s removed."), buf);
			}
			continue;
		}

		/* verify file should be there */
		aip = startp + index;
		offset = aip->ach_offset;
		metap = (struct cachefs_metadata *)((char *)addrp + offset);
		if ((metap->md_flags & MD_FILE) == 0) {
			/* remove the file */
			xx = unlink(buf);
			if (xx == -1) {
				pr_err(gettext("Could not remove file %s: %s"),
				    buf, strerror(errno));
			} else if (verbose) {
				pr_err(gettext("File %s removed."), buf);
			}
			continue;
		}
		nffs++;
	}

	/* close the directory */
	if (dp)
		closedir(dp);

	/* if we did not find the correct number of front files in the dir */
	if (nffs != ahp->ach_nffs) {
		if (verbose) {
			pr_err(gettext("Front file mismatch expected"
				" %d got %d in %s"),
				ahp->ach_nffs, nffs, attrdir);
		}
		error = -1;
	}
	lruno = ahp->ach_lruno;

out:
	/* allocate resources for the attrcache file */
	if (error == 0) {
		/* determine whether or not to put on the lru */
		xx = (nffs == 0) ? 1 : 0;

		error = res_addident(resp, lruno, fsid, base, 0, 1, xx, size);
		if (error == -1) {
			if (verbose) {
				pr_err("%s bad lruno %d\n", attrfile, lruno);
			}
		} else if (nffs > 0) {
			/* indicate that the file group directory is okay */
			hitem.key = strdup(namep);
			hitem.data = NULL;
			if (hsearch(hitem, ENTER) == NULL) {
				pr_err(gettext("Hash table full"));
				error = 39;
			}
		}
	}

	if (error == -1) {
		/* clear any idents we created for this attrcache file */
		for (index = 0; index < fgsize; index++) {
			aip = startp + index;
			if (aip->ach_written == 0)
				continue;
			metap = (struct cachefs_metadata *)((char *)addrp +
				aip->ach_offset);
			if (metap->md_lruno != 0) {
				/* clear the resource file idents */
				xx = local || (metap->md_flags & MD_PINNED);
				res_clearident(resp, metap->md_lruno, !xx,
					(metap->md_frontblks * MAXBSIZE));
				if (verbose) {
					pr_err(gettext("Removed ident %d"),
						metap->md_lruno);
				}
			}
		}

		/* nuke the attrcache file */
		xx = unlink(attrfile);
		if (xx == -1) {
			pr_err(gettext("Unable to remove %s"), attrfile);
			error = 39;
		} else {
			error = 0;
			if (verbose) {
				pr_err(gettext("Removed attrcache %s"),
					attrfile);
			}
		}
	} else if (error == 0) {
		/* add this directory to the resources */
		res_addfile(resp, 1);
	}

	/* unmap the attribute file */
	munmap(addrp, size);

	return (error);
}

/*
 *
 *			tree_remove
 *
 * Description:
 *	Called via the nftw(3c) routine, this routine removes
 *	the specified file.
 * Arguments:
 *	namep	pathname to the file
 *	statp	stat info on the file
 *	type	ftw type information
 *	ftwp	pointer to additional ftw information
 * Returns:
 *	Returns 0 for success or -1 if an error occurs.
 * Preconditions:
 *	precond(namep)
 *	precond(statp)
 *	precond(ftwp)
 */

int
tree_remove(const char *namep, const struct stat *statp, int type,
    struct FTW *ftwp)
{
	int xx;

	switch (type) {
	case FTW_D:
	case FTW_DP:
	case FTW_DNR:
		xx = rmdir(namep);
		if (xx != 0) {
			pr_err(gettext("Could not remove directory %s: %s"),
			    namep, strerror(errno));
			return (-1);
		}
#if 0
		pr_err(gettext("Directory %s removed."), namep);
#endif
		break;

	default:
		xx = unlink(namep);
		if (xx != 0) {
			pr_err(gettext("Could not remove file %s: %s"),
			    namep, strerror(errno));
			return (-1);
		}
#if 0
		pr_err(gettext("File %s removed."), namep);
#endif
		break;
	}

	/* return success */
	return (0);
}
