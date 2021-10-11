#pragma ident	"@(#)mount.c	1.11	94/08/30 SMI"

/*
 * Copyright (c) 1988 Sun Microsystems, Inc.
 */

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mnttab.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <locale.h>
#include <sys/stat.h>
#include <fslib.h>

#define	MNTTYPE_TMPFS	"tmpfs"

#define	MNTOPT_DEV	"dev"

extern time_t	time();
extern int errno;

struct tmpfs_args {
	u_int anonmax;
	u_int flags;
};

#define	FSSIZE	0
#define	VERBOSE	1
#define	RO	2
#define	RW	3
#define	NOSUID	4
#define	SUID	5

char *myopts[] = {
	"size",
	"vb",
	"ro",
	"rw",
	"nosuid",
	"suid",
	NULL
};

int	nmflg = 0;
int	rwflg = 0;

main(argc, argv)
	int argc;
	char *argv[];
{
	struct mnttab mnt;
	int c;
	char *myname;
	char optbuf[256];
	char typename[64];
	char *options, *value;
	extern int optind;
	extern char *optarg;
	int error = 0;
	int verbose = 0;
	int nosuid = 0;
	int fs_size = 0;
	int optsize = 0;
	int mflg = 0;
	int optcnt = 0;
	extern int getopt();
	extern int getsubopt();
	struct tmpfs_args targs;
	struct stat st;

	(void) setlocale(LC_ALL, "");

#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	myname = strrchr(argv[0], '/');
	myname = myname ? myname + 1 : argv[0];
	(void) sprintf(typename, "%s_%s", MNTTYPE_TMPFS, myname);
	argv[0] = typename;

	optbuf[0] = '\0';
	(void) strcat(optbuf, "rw");	/* RO tmpfs not supported... */
	while ((c = getopt(argc, argv, "?o:mO")) != EOF) {
		switch (c) {
		case 'V':
			verbose++;
			break;
		case '?':
			error++;
			break;
		case 'm':
			nmflg++;
			break;
		case 'O':
			mflg |= MS_OVERLAY;
			break;
		case 'o':
			options = optarg;
			while (*options != '\0') {
				switch (getsubopt(&options, myopts, &value)) {
				case RO:
					(void) fprintf(stderr,
gettext("%s: read-only mount is not supported\n"),
					    typename);
					    error++;
					    break;
				case RW:
					rwflg++;
					break;
				case FSSIZE:
					if (value) {
						fs_size = convnum(value);
						if (fs_size < 0) {
							(void) fprintf(stderr,
gettext("%s: value %s for option \"%s\" is invalid\n"),
typename, value, myopts[FSSIZE]);
							error++;
							break;
						}
						targs.anonmax = fs_size;
						optcnt++;
						if (verbose)
							(void) fprintf(stderr,
gettext("setting fs_size to %d\n"), fs_size);
					} else {
						(void) fprintf(stderr,
gettext("%s: option \"%s\" requires value\n"), typename, myopts[FSSIZE]);
						error++;
					}
					break;
				case VERBOSE:
					verbose++;
					break;
				case NOSUID:
					mflg |= MS_NOSUID;
					nosuid++;
					break;
				case SUID:
					mflg &= ~MS_NOSUID;
					nosuid = 0;
					break;
				default:
					(void) fprintf(stderr,
gettext("%s: illegal -o suboption -- %s\n"), typename, value);
					error++;
					break;
				}
			}
			if (nosuid)
				strcat(optbuf, ",nosuid");
			if (fs_size) {
				(void) sprintf(optbuf, "%s,size=%d", optbuf,
				    fs_size);
				if (--optcnt)
					(void) strcat(optbuf, ",");
				optsize = sizeof (struct tmpfs_args);
				if (verbose)
					(void) fprintf(stderr, "optbuf:%s\n",
					    optbuf);
			}
			if (options[0] && !error) {
				(void) strcat(optbuf, options);
				if (verbose)
					(void) fprintf(stderr, "optbuf:%s\n",
					    optbuf);
			}
			if (verbose && optsize)
				(void) fprintf(stderr, "optsize:%d optbuf:%s\n",
				    optsize, optbuf);
			break;
		}
	}

	if (geteuid() != 0) {
		(void) fprintf(stderr, gettext("Must be root to use mount\n"));
		exit(32);
	}

	if (verbose && !error) {
		char *optptr;

		(void) fprintf(stderr, "%s", typename);
		for (optcnt = 1; optcnt < argc; optcnt++) {
			optptr = argv[optcnt];
			if (optptr)
				(void) fprintf(stderr, " %s", optptr);
		}
		(void) fprintf(stderr, "\n");
	}

	if (argc - optind != 2 || error) {
		(void) fprintf(stderr,
		    gettext("Usage: %s [-o size] swap mount_point\n"),
		    typename);
		exit(32);
	}

	mnt.mnt_special = argv[optind++];
	mnt.mnt_mountp = argv[optind++];
	mnt.mnt_fstype = MNTTYPE_TMPFS;

	if (optsize) {
		mnt.mnt_mntopts = (char *)&targs;
		mflg |= MS_DATA;
	} else {
		mnt.mnt_mntopts = NULL;
		mflg |= MS_DATA;	/* could be MS_FSS */
	}

	signal(SIGHUP,  SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGINT,  SIG_IGN);

	if (verbose) {
		(void) fprintf(stderr, "mount(%s, \"%s\", %d, %s",
		    mnt.mnt_special, mnt.mnt_mountp, mflg, MNTTYPE_TMPFS);
		if (optsize)
			(void) fprintf(stderr, ", \"%s\", %d)\n",
			    optbuf, strlen(optbuf));
		else
			(void) fprintf(stderr, ")\n");
	}
	if (mount(mnt.mnt_special, mnt.mnt_mountp, mflg, MNTTYPE_TMPFS,
	    mnt.mnt_mntopts, optsize)) {
		if (errno == EBUSY)
			(void) fprintf(stderr,
			    gettext("mount: %s already mounted\n"),
			    mnt.mnt_mountp);
		else
			perror("mount");
		exit(32);
	}

	if (!nmflg && fsaddtomtab(&mnt))
		exit(32);
	exit(0);
	/* NOTREACHED */
}

int
convnum(char *str)
{
	u_int num = 0;
	char *c;

	c = str;

	/*
	 * Convert str to number
	 */
	while ((*c >= '0') && (*c <= '9'))
		num = num * 10 + *c++ - '0';

	/*
	 * Terminate on null
	 */
	while (*c > 0) {
		switch (*c++) {

		/*
		 * convert from kilobytes
		 */
		case 'k':
		case 'K':
			num *= 1024;
			continue;

		/*
		 * convert from megabytes
		 */
		case 'm':
		case 'M':
			num *= 1024 * 1024;
			continue;

		default:
			return (-1);
		}
	}
	return (num);
}
