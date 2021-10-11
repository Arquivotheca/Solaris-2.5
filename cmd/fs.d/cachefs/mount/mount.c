/*
 *
 *			mount.c
 *
 * Cachefs mount program.
 */

/*	Copyright (c) 1994, by Sun Microsystems, Inc. */
/*	  All Rights Reserved  	*/

#pragma ident "@(#)mount.c   1.27     94/11/11 SMI"

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
#include <varargs.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <wait.h>
#include <ctype.h>
#include <fcntl.h>
#include <fslib.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/mount.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#include <sys/fs/cachefs_fs.h>

char *cfs_opts[] = {
#define	CFSOPT_BACKFSTYPE	0
	"backfstype",
#define	CFSOPT_CACHEDIR		1
	"cachedir",
#define	CFSOPT_CACHEID		2
	"cacheid",
#define	CFSOPT_BACKPATH		3
	"backpath",

#define	CFSOPT_WRITEAROUND	4
	"write-around",
#define	CFSOPT_NONSHARED	5
	"non-shared",

#define	CFSOPT_NOCONST		6
	"noconst",

#define	CFSOPT_LOCALACCESS	7
	"local-access",
#define	CFSOPT_PURGE		8
	"purge",

#define	CFSOPT_RW		9
	"rw",
#define	CFSOPT_RO		10
	"ro",
#define	CFSOPT_SUID		11
	"suid",
#define	CFSOPT_NOSUID		12
	"nosuid",
#define	CFSOPT_REMOUNT		13
	"remount",

#define	CFSOPT_FGSIZE		14
	"fgsize",
#define	CFSOPT_POPSIZE		15
	"popsize",
#define	CFSOPT_ACREGMIN		16
	"acregmin",
#define	CFSOPT_ACREGMAX		17
	"acregmax",
#define	CFSOPT_ACDIRMIN		18
	"acdirmin",
#define	CFSOPT_ACDIRMAX		19
	"acdirmax",
#define	CFSOPT_ACTIMEO		20
	"actimeo",
#define	CFSOPT_CODCONST		21
	"demandconst",

	NULL
};

#define	MNTTYPE_CFS	"cachefs"	/* XXX - to be added to mntent.h */
					/* XXX - and should be cachefs */
#define	CFS_DEF_DIR	"/cache"	/* XXX - should be added to cfs.h */

#define	bad(val) (val == NULL || !isdigit(*val))

#define	VFS_PATH	"/usr/lib/fs"
#define	ALT_PATH	"/etc/fs"

/* forward references */
void usage(char *msgp);
void pr_err(char *fmt, ...);
int set_cfs_args(char *optionp, struct cachefs_mountargs *margsp, int *mflagp,
    char **backfstypepp, char **reducepp);
int get_mount_point(char *cachedirp, char *specp, char **pathpp);
void doexec(char *fstype, char **newargv, char *myname);
char *get_back_fsid(char *specp);
char *get_cacheid(char *, char *);

/*
 *
 *			main
 *
 * Description:
 *	Main routine for the cachefs mount program.
 * Arguments:
 *	argc	number of command line arguments
 *	argv	list of command line arguments
 * Returns:
 *	Returns 0 for success, 1 an error was encountered.
 * Preconditions:
 */

main(int argc, char **argv)
{
	char *myname;
	char *optionp;
	char *opigp;
	int mflag;
	int nomnttab;
	int readonly;
	struct cachefs_mountargs margs;
	char *backfstypep;
	char *reducep;
	char *specp;
	int xx;
	int stat_loc;
	char *newargv[20];
	char *mntp;
	pid_t pid;
	int mounted;
	int c;
	int lockid;
	int Oflg;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN	"SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	/* verify root running command */
	if (geteuid() != 0) {
		pr_err(gettext("must be run by root"));
		return (1);
	}

	if (argv[0]) {
		myname = strrchr(argv[0], '/');
		if (myname)
			myname++;
		else
			myname = argv[0];
	} else {
		myname = "path unknown";
	}

	optionp = NULL;
	nomnttab = 0;
	readonly = 0;
	Oflg = 0;

	/* process command line options */
	while ((c = getopt(argc, argv, "mo:Or")) != EOF) {
		switch (c) {
		case 'm':	/* no entry in /etc/mnttab */
			nomnttab = 1;
			break;

		case 'o':
			optionp = optarg;
			break;

		case 'O':
			Oflg++;
			break;

		case 'r':	/* read only mount */
			readonly = 1;
			break;

		default:
			usage("invalid option");
			return (1);
		}
	}

	/* if -o not specified */
	if (optionp == NULL) {
		usage(gettext("\"-o backfstype\" must be specified"));
		return (1);
	}

	/* verify special device and mount point are specified */
	if (argc - optind < 2) {
		usage(gettext("must specify special device and mount point"));
		return (1);
	}

	/* Store mount point and special device. */
	specp = argv[argc - 2];
	mntp = argv[argc - 1];

	/* Initialize default mount values */
	margs.cfs_options.opt_flags = CFS_ACCESS_BACKFS;
	margs.cfs_options.opt_popsize = DEF_POP_SIZE;
	margs.cfs_options.opt_fgsize = DEF_FILEGRP_SIZE;
	margs.cfs_fsid = NULL;
	memset(margs.cfs_cacheid, 0, sizeof (margs.cfs_cacheid));
	margs.cfs_cachedir = CFS_DEF_DIR;
	margs.cfs_backfs = NULL;
	margs.cfs_acregmin = 0;
	margs.cfs_acregmax = 0;
	margs.cfs_acdirmin = 0;
	margs.cfs_acdirmax = 0;
	mflag = 0;
	backfstypep = NULL;

	/* process -o options */
	xx = set_cfs_args(optionp, &margs, &mflag, &backfstypep, &reducep);
	if (xx) {
		return (1);
	}

	/* backfstype has to be specified */
	if (backfstypep == NULL) {
		usage(gettext("\"-o backfstype\" must be specified"));
		return (1);
	}

	/* set default write mode if not specified */
	if ((margs.cfs_options.opt_flags &
	    (CFS_WRITE_AROUND|CFS_DUAL_WRITE)) == 0) {
		margs.cfs_options.opt_flags |= CFS_WRITE_AROUND;
		if (strcmp(backfstypep, "hsfs") == 0)
			mflag |= MS_RDONLY;
	}

	/* if read-only was specified with the -r option */
	if (readonly) {
		mflag |= MS_RDONLY;
	}

	/* if overlay was specified with -O option */
	if (Oflg) {
		mflag |= MS_OVERLAY;
	}

	/* get the fsid of the backfs and the cacheid */
	margs.cfs_fsid = get_back_fsid(specp);
	if (margs.cfs_fsid == NULL) {
		pr_err(gettext("out of memory"));
		return (1);
	}

	/* Make sure the cache is sane */
	if (check_cache(margs.cfs_cachedir))
		return (1);

	/* get the front file system cache id if necessary */
	if (margs.cfs_cacheid[0] == '\0') {
		char *cacheid = get_cacheid(margs.cfs_fsid, mntp);

		if (cacheid == NULL) {
			pr_err(gettext("cacheid too long"));
			return (1);
		}

		strcpy(margs.cfs_cacheid, cacheid);
	}

	/* lock the cache directory shared */
	lockid = cachefs_dir_lock(margs.cfs_cachedir, 1);
	if (lockid == -1) {
		/* exit if could not get the lock */
		return (1);
	}

	/* if no mount point was specified */
	mounted = 0;
	if (margs.cfs_backfs == NULL) {
		mounted = 1;

		/* get a suitable mount point */
		xx = get_mount_point(margs.cfs_cachedir, specp,
		    &margs.cfs_backfs);
		if (xx) {
			cachefs_dir_unlock(lockid);
			return (1);
		}

		/* construct argument list for mounting the back file system */
		xx = 1;
		newargv[xx++] = "mount";
		if (readonly)
			newargv[xx++] = "-r";
		newargv[xx++] = "-m";
		if (reducep) {
			newargv[xx++] = "-o";
			newargv[xx++] = reducep;
		}
		newargv[xx++] = specp;
		newargv[xx++] = margs.cfs_backfs;
		newargv[xx++] = NULL;

		/* fork */
		if ((pid = fork()) == -1) {
			pr_err(gettext("could not fork %s"),
			    strerror(errno));
			cachefs_dir_unlock(lockid);
			return (1);
		}

		/* if the child */
		if (pid == 0) {
			/* do the mount */
			doexec(backfstypep, newargv, myname);
		}

		/* else if the parent */
		else {
			/* wait for the child to exit */
			if (wait(&stat_loc) == -1) {
				pr_err(gettext("wait failed %s"),
				    strerror(errno));
				cachefs_dir_unlock(lockid);
				return (1);
			}

			if (!WIFEXITED(stat_loc)) {
				pr_err(gettext("back mount did not exit"));
				cachefs_dir_unlock(lockid);
				return (1);
			}

			if (WEXITSTATUS(stat_loc) != 0) {
				pr_err(gettext("back mount failed"));
				cachefs_dir_unlock(lockid);
				return (1);
			}
		}
	}

	/* mount the cache file system */
	xx = mount(margs.cfs_backfs, mntp, mflag | MS_DATA, MNTTYPE_CFS,
	    &margs, sizeof (margs));
	if (xx == -1) {
		if (errno == ESRCH) {
			pr_err(gettext("mount failed, options do not match."));
		} else {
			pr_err(gettext("mount failed %s"), strerror(errno));
		}

		/* try to unmount the back file system if we mounted it */
		if (mounted) {
			xx = 1;
			newargv[xx++] = "umount";
			newargv[xx++] = margs.cfs_backfs;
			newargv[xx++] = NULL;

			/* fork */
			if ((pid = fork()) == -1) {
				pr_err(gettext("could not fork: %s"),
				    strerror(errno));
				cachefs_dir_unlock(lockid);
				return (1);
			}

			/* if the child */
			if (pid == 0) {
				/* do the unmount */
				doexec(backfstypep, newargv, "umount");
			}

			/* else if the parent */
			else {
				wait(0);
			}
		}

		cachefs_dir_unlock(lockid);
		return (1);
	}

	/* release the lock on the cache directory */
	cachefs_dir_unlock(lockid);

	/* update mnttab file if necessary */
	if (!nomnttab) {
		struct mnttab mtab;

		/* add entry for front file system */
		mtab.mnt_special = margs.cfs_backfs;
		mtab.mnt_mountp = mntp;
		mtab.mnt_fstype = MNTTYPE_CFS;
		mtab.mnt_mntopts = optionp;
		xx = fsaddtomtab(&mtab);
		if (xx != 0)
			return (1);

		/* if we added the back file system, add it also with ignore */
		if (mounted) {
			mtab.mnt_special = specp;
			mtab.mnt_mountp = margs.cfs_backfs;
			mtab.mnt_fstype = backfstypep;
			if (reducep) {
				opigp = malloc(strlen(reducep) + 20);
				if (opigp == NULL) {
					pr_err(gettext("no more memory"));
					return (1);
				}
				sprintf(opigp, "ignore,%s", reducep);
			} else
				opigp = "ignore";
			mtab.mnt_mntopts = opigp;
			xx = fsaddtomtab(&mtab);
			if (xx != 0)
				return (1);
		}
	}

	/* return success */
	return (0);
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
		pr_err(gettext("%s"), msgp);
	}

	fprintf(stderr,
	    gettext("Usage: mount -F cachefs [generic options] "
	    "-o backfstype=file_system_type[FSTypespecific_options] "
	    "special mount_point\n"));
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
	(void) fprintf(stderr, gettext("mount -F cachefs: "));
	(void) vfprintf(stderr, fmt, ap);
	(void) fprintf(stderr, "\n");
	va_end(ap);
}

/*
 *
 *			set_cfs_args
 *
 * Description:
 *	Parse the comma delimited set of options specified by optionp
 *	and puts the results in margsp, mflagp, and backfstypepp.
 *	A string is constructed of options which are not specific to
 *	cfs and is placed in reducepp.
 *	Pointers to strings are invalid if this routine is called again.
 *	No initialization is done on margsp, mflagp, or backfstypepp.
 * Arguments:
 *	optionp		string of comma delimited options
 *	margsp		option results for the mount dataptr arg
 *	mflagp		option results for the mount mflag arg
 *	backfstypepp	set to name of back file system type
 *	reducepp	set to the option string without cfs specific options
 * Returns:
 *	Returns 0 for success, -1 for an error.
 * Preconditions:
 *	precond(optionp)
 *	precond(margsp)
 *	precond(mflagp)
 *	precond(backfstypepp)
 *	precond(reducepp)
 */

int
set_cfs_args(char *optionp, struct cachefs_mountargs *margsp, int *mflagp,
    char **backfstypepp, char **reducepp)
{
	static char *optstrp = NULL;
	static char *reducep = NULL;
	char *savep, *strp, *valp;
	int badopt;
	int ret;
	int o_rw = 0;
	int o_ro = 0;
	int o_suid = 0;
	int o_nosuid = 0;
	int xx;
	u_long yy;

	/* free up any previous options */
	free(optstrp);
	free(reducep);

	/* make a copy of the options so we can modify it */
	optstrp = strp = strdup(optionp);
	reducep = malloc(strlen(optionp));
	if ((strp == NULL) || (reducep == NULL)) {
		pr_err(gettext("out of memory"));
		return (-1);
	}
	*reducep = '\0';

	/* parse the options */
	badopt = 0;
	ret = 0;
	while (*strp) {
		savep = strp;
		switch (getsubopt(&strp, cfs_opts, &valp)) {

		case CFSOPT_BACKFSTYPE:
			if (valp == NULL)
				badopt = 1;
			else
				*backfstypepp = valp;
			break;

		case CFSOPT_CACHEDIR:
			if (valp == NULL)
				badopt = 1;
			else
				margsp->cfs_cachedir = valp;
			break;

		case CFSOPT_CACHEID:
			if (valp == NULL) {
				badopt = 1;
				break;
			}

			if (strlen(valp) >= (size_t)C_MAX_MOUNT_FSCDIRNAME) {
				pr_err(gettext("name too long"));
				badopt = 1;
				break;
			}

			memset(margsp->cfs_cacheid, 0, C_MAX_MOUNT_FSCDIRNAME);
			strcpy(margsp->cfs_cacheid, valp);
			break;

		case CFSOPT_BACKPATH:
			if (valp == NULL)
				badopt = 1;
			else
				margsp->cfs_backfs = valp;
			break;

		case CFSOPT_WRITEAROUND:
			margsp->cfs_options.opt_flags |= CFS_WRITE_AROUND;
			break;

		case CFSOPT_NONSHARED:
			margsp->cfs_options.opt_flags |= CFS_DUAL_WRITE;
			break;

		case CFSOPT_NOCONST:
			margsp->cfs_options.opt_flags |= CFS_NOCONST_MODE;
			break;

		case CFSOPT_LOCALACCESS:
			margsp->cfs_options.opt_flags &= ~CFS_ACCESS_BACKFS;
			break;

		case CFSOPT_PURGE:
			margsp->cfs_options.opt_flags |= CFS_PURGE;
			break;

		case CFSOPT_RW:
			o_rw = 1;
			*mflagp &= ~MS_RDONLY;
			strcat(reducep, ",");
			strcat(reducep, savep);
			break;

		case CFSOPT_RO:
			o_ro = 1;
			*mflagp |= MS_RDONLY;
			strcat(reducep, ",");
			strcat(reducep, savep);
			break;

		case CFSOPT_SUID:
			o_suid = 1;
			*mflagp &= ~MS_NOSUID;
			strcat(reducep, ",");
			strcat(reducep, savep);
			break;

		case CFSOPT_NOSUID:
			o_nosuid = 1;
			*mflagp |= MS_NOSUID;
			strcat(reducep, ",");
			strcat(reducep, savep);
			break;

		case CFSOPT_REMOUNT:
			*mflagp |= MS_REMOUNT;
			strcat(reducep, ",");
			strcat(reducep, savep);
			break;

		case CFSOPT_FGSIZE:
			if (bad(valp))
				badopt = 1;
			else
				margsp->cfs_options.opt_fgsize = atoi(valp);
			break;

		case CFSOPT_POPSIZE:
			if (bad(valp))
				badopt = 1;
			else
				margsp->cfs_options.opt_popsize =
				    atoi(valp) * 1024;
			break;

		case CFSOPT_ACREGMIN:
			if (bad(valp))
				badopt = 1;
			else
				margsp->cfs_acregmin = strtoul(valp, NULL, 10);
			strcat(reducep, ",");
			strcat(reducep, savep);
			break;

		case CFSOPT_ACREGMAX:
			if (bad(valp))
				badopt = 1;
			else
				margsp->cfs_acregmax = strtoul(valp, NULL, 10);
			strcat(reducep, ",");
			strcat(reducep, savep);
			break;

		case CFSOPT_ACDIRMIN:
			if (bad(valp))
				badopt = 1;
			else
				margsp->cfs_acdirmin = strtoul(valp, NULL, 10);
			strcat(reducep, ",");
			strcat(reducep, savep);
			break;

		case CFSOPT_ACDIRMAX:
			if (bad(valp))
				badopt = 1;
			else
				margsp->cfs_acdirmax = strtoul(valp, NULL, 10);
			strcat(reducep, ",");
			strcat(reducep, savep);
			break;

		case CFSOPT_ACTIMEO:
			if (bad(valp))
				badopt = 1;
			else {
				yy = strtoul(valp, NULL, 10);
				margsp->cfs_acregmin = yy;
				margsp->cfs_acregmax = yy;
				margsp->cfs_acdirmin = yy;
				margsp->cfs_acdirmax = yy;
			}
			strcat(reducep, ",");
			strcat(reducep, savep);
			break;

		case CFSOPT_CODCONST:
			margsp->cfs_options.opt_flags |= CFS_CODCONST_MODE;
			break;

		default:
			/* unknown option, save for the back file system */
			strcat(reducep, ",");
			strcat(reducep, savep);
			break;
		}

		/* if a lexical error occurred */
		if (badopt) {
			pr_err(gettext("invalid argument to option: \"%s\""),
			    savep);
			badopt = 0;
			ret = -1;
		}
	}

	/* check for conflicting options */
	if (o_suid && o_nosuid) {
		pr_err(gettext("suid and nosuid are mutually exclusive"));
		ret = -1;
	}
	if (o_rw && o_ro) {
		pr_err(gettext("rw and ro are mutually exclusive"));
		ret = -1;
	}
	if (margsp->cfs_acregmin > margsp->cfs_acregmax) {
		pr_err(gettext("acregmin cannot be greater than acregmax"));
		ret = -1;
	}
	if (margsp->cfs_acdirmin > margsp->cfs_acdirmax) {
		pr_err(gettext("acdirmin cannot be greater than acdirmax"));
		ret = -1;
	}

	xx = CFS_NOCONST_MODE | CFS_CODCONST_MODE;
	if ((margsp->cfs_options.opt_flags & xx) == xx) {
		pr_err(gettext("only one of \"noconst\" and \"demandconst\""
			" may be specified"));
		ret = -1;
	}

	xx = margsp->cfs_options.opt_flags;
	switch (xx & (CFS_DUAL_WRITE | CFS_WRITE_AROUND)) {
	case 0:
	case CFS_DUAL_WRITE:
	case CFS_WRITE_AROUND:
		break;
	default:
		pr_err(gettext(
		    "only one of non-shared or write-around"
		    " may be specified"));
		ret = -1;
	}

	/* if an error occured */
	if (ret)
		return (-1);

	/* if there are any options which are not mount specific */
	if (*reducep)
		*reducepp = reducep + 1;
	else
		*reducepp = NULL;

	/* return success */
	return (0);
}

/*
 *
 *			get_mount_point
 *
 * Description:
 *	Makes a suitable mount point for the back file system.
 *	The name of the mount point created is stored in a malloced
 *	buffer in pathpp
 * Arguments:
 *	cachedirp	the name of the cache directory
 *	specp		the special name of the device for the file system
 *	pathpp		where to store the mount point
 * Returns:
 *	Returns 0 for success, -1 for an error.
 * Preconditions:
 *	precond(cachedirp)
 *	precond(specp)
 *	precond(pathpp)
 */

int
get_mount_point(char *cachedirp, char *specp, char **pathpp)
{
	char *strp;
	char *namep;
	struct stat stat1;
	struct stat stat2;
	int xx;
	int index;
	int max;

	/* make a copy of the special device name */
	specp = strdup(specp);
	if (specp == NULL) {
		pr_err(gettext("out of memory"));
		return (-1);
	}

	/* convert the special device name into a file name */
	strp = specp;
	while (strp = strchr(strp, '/')) {
		*strp = '_';
	}

	/* get some space for the path name */
	strp = malloc(MAXPATHLEN);
	if (strp == NULL) {
		pr_err(gettext("out of memory"));
		return (-1);
	}

	/* see if the mount directory is valid */
	sprintf(strp, "%s/%s", cachedirp, BACKMNT_NAME);
	xx = stat(strp, &stat1);
	if ((xx == -1) || !S_ISDIR(stat1.st_mode)) {
		pr_err(gettext("%s is not a valid cache."), strp);
		return (-1);
	}

	/* find a directory name we can use */
	max = 10000;
	namep = strp + strlen(strp);
	for (index = 1; index < max; index++) {

		/* construct a directory name to consider */
		if (index == 1)
			sprintf(namep, "/%s", specp);
		else
			sprintf(namep, "/%s_%d", specp, index);

		/* stat the directory */
		xx = stat(strp, &stat2);

		/* if the stat failed */
		if (xx == -1) {
			/* if the dir does not exist we can use it */
			if (errno == ENOENT)
				break;

			/* any other error we are hosed */
			pr_err(gettext("could not stat %s %s"),
			    strp, strerror(errno));
			return (-1);
		}

		/* if it is a dir that is not being used, then we can use it */
		if (S_ISDIR(stat2.st_mode) && (stat1.st_dev == stat2.st_dev)) {
			*pathpp = strp;
			return (0);
		}
	}

	/* if the search failed */
	if (index >= max) {
		pr_err(gettext("could not create a directory"));
		return (-1);
	}

	/* create the directory */
	if (mkdir(strp, 0755) == -1) {
		pr_err(gettext("could not make directory %s"), strp);
		return (-1);
	}

	/* return success */
	*pathpp = strp;
	return (0);
}


/*
 *
 *			doexec
 *
 * Description:
 *	Execs the specified program with the specified command line arguments.
 *	This function never returns.
 * Arguments:
 *	fstype		type of file system
 *	newargv		command line arguments
 *	progp		name of program to exec
 * Returns:
 * Preconditions:
 *	precond(fstype)
 *	precond(newargv)
 */

void
doexec(char *fstype, char *newargv[], char *progp)
{
	char	full_path[PATH_MAX];
	char	alter_path[PATH_MAX];
	char	*vfs_path = VFS_PATH;
	char	*alt_path = ALT_PATH;

	/* build the full pathname of the fstype dependent command. */
	sprintf(full_path, "%s/%s/%s", vfs_path, fstype, progp);
	sprintf(alter_path, "%s/%s/%s", alt_path, fstype, progp);

	/* if the program exists */
	if (access(full_path, 0) == 0) {
		/* invoke the program */
		execv(full_path, &newargv[1]);

		/* if wrong permissions */
		if (errno == EACCES) {
			pr_err(gettext("cannot execute %s %s"),
			    full_path, strerror(errno));
		}

		/* if it did not work and the shell might make it */
		if (errno == ENOEXEC) {
			newargv[0] = "sh";
			newargv[1] = full_path;
			execv("/sbin/sh", &newargv[0]);
		}
	}

	/* try the alternate path */
	execv(alter_path, &newargv[1]);

	/* if wrong permissions */
	if (errno == EACCES) {
		pr_err(gettext("cannot execute %s %s"),
		    alter_path, strerror(errno));
	}

	/* if it did not work and the shell might make it */
	if (errno == ENOEXEC) {
		newargv[0] = "sh";
		newargv[1] = alter_path;
		execv("/sbin/sh", &newargv[0]);
	}

	pr_err(gettext("operation not applicable to FSType %s"), fstype);
	exit(1);
}

/*
 *
 *			get_back_fsid
 *
 * Description:
 *	Determines a unique identifier for the back file system.
 * Arguments:
 *	specp	the special file of the back fs
 * Returns:
 *	Returns a malloc string which is the unique identifer
 *	or NULL on failure.  NULL is only returned if malloc fails.
 * Preconditions:
 *	precond(specp)
 */

char *
get_back_fsid(char *specp)
{
	return (strdup(specp));
}

/*
 *
 *			get_cacheid
 *
 * Description:
 *	Determines an identifier for the front file system cache.
 *	The returned string points to a static buffer which is
 *	overwritten on each call.
 *	The length of the returned string is < C_MAX_MOUNT_FSCDIRNAME.
 * Arguments:
 *	fsidp	back file system id
 *	mntp	front file system mount point
 * Returns:
 *	Returns a pointer to the string identifier, or NULL if the
 *	identifier was overflowed.
 * Preconditions:
 *	precond(fsidp)
 *	precond(mntp)
 */

char *
get_cacheid(char *fsidp, char *mntp)
{
	char *c1;
	static char buf[PATH_MAX];
	char mnt_copy[PATH_MAX];

	/* strip off trailing space in mountpoint -- autofs fallout */
	(void) strcpy(mnt_copy, mntp);
	c1 = mnt_copy + strlen(mnt_copy) - 1;
	if (*c1 == ' ')
		*c1 = '\0';

	if ((strlen(fsidp) + strlen(mnt_copy) + 2) >= (size_t) PATH_MAX)
		return (NULL);

	strcpy(buf, fsidp);
	strcat(buf, ":");
	strcat(buf, mnt_copy);
	c1 = buf;
	while ((c1 = strpbrk(c1, "/")) != NULL)
		*c1 = '_';
	return (buf);
}


/*
 *
 *			check_cache
 *
 * Description:
 *	Checks the cache we are about to use.
 * Arguments:
 *	cachedirp	cachedirectory to check
 * Returns:
 *	Returns 0 for success, -1 for an error.
 * Preconditions:
 */
check_cache(cachedirp)
	char *cachedirp;
{
	char *fsck_argv[4];
	int status = 0;
	pid_t pid;

	fsck_argv[1] = "fsck";
	fsck_argv[2] = cachedirp;
	fsck_argv[3] = NULL;

	/* fork */
	if ((pid = fork()) == -1) {
		pr_err(gettext("could not fork %s"),
		    strerror(errno));
		return (1);
	}

	if (pid == 0) {
		/* do the fsck */
		doexec("cachefs", fsck_argv, "fsck");
	} else {
		/* wait for the child to exit */
		if (wait(&status) == -1) {
			pr_err(gettext("wait failed %s"),
			    strerror(errno));
			return (1);
		}

		if (!WIFEXITED(status)) {
			pr_err(gettext("cache fsck did not exit"));
			return (1);
		}

		if (WEXITSTATUS(status) != 0) {
			pr_err(gettext("cache fsck mount failed"));
			return (1);
		}
	}
	return (0);
}
