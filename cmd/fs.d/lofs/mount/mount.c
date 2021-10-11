#pragma ident	"@(#)mount.c	1.10	94/03/07 SMI"

/*
 * Copyright (c) 1985 Sun Microsystems, Inc.
 */

#define	LOFS
#define	MNTTYPE_LOFS "lofs"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/fstyp.h>
#include <sys/fsid.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#include <sys/mount.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <fslib.h>

#define	RET_OK		0
#define	RET_ERR		33

extern int errno;
extern int optind;
extern char *optarg;

void usage();
char *hasmntopt();


char *myopts[] = {
#define	RO 0
	"ro",
#define	RW 1
	"rw",
	NULL
};

char *myname;
char typename[64];

int mflg;
int errflag;

char fstype[] = MNTTYPE_LOFS;

/*
 * usage: mount -F lofs [-r] [-o options] fsname dir
 *
 * This mount program is exec'ed by /usr/sbin/mount if '-F lofs' is
 * specified.
 */
main(argc, argv)
	int argc;
	char **argv;
{
	int c;
	char *fsname;		/* Directory being mounted */
	char *dir;		/* Directory being mounted on */
	int flags = 0;
	struct mnttab mnt;
	int	fs_ind;
	char *options, *value;

	myname = strrchr(argv[0], '/');
	myname = myname ? myname+1 : argv[0];
	(void) sprintf(typename, "%s %s", MNTTYPE_LOFS, myname);
	argv[0] = typename;

	while ((c = getopt(argc, argv, "o:rmO")) != EOF) {
		switch (c) {
		case '?':
			errflag++;
			break;

		case 'o':
			options = optarg;
			while (*options != '\0') {
				switch (getsubopt(&options, myopts, &value)) {
				case RO:
					flags |= MS_RDONLY;
					break;
				case RW:
					flags &= ~(MS_RDONLY);
					break;
				default:
					usage();
					/* NOTREACHED */
				}
			}
			break;
		case 'O':
			flags |= MS_OVERLAY;
			break;
		case 'r':
			flags |= MS_RDONLY;
			break;

		case 'm':
			mflg++;
			break;

		default:
			usage();
		}
	}
	if ((argc - optind != 2) || errflag) {
		usage();
	}
	fsname = argv[argc - 2];
	dir = argv[argc - 1];

	if (geteuid() != 0) {
		fprintf(stderr, "%s, not super user\n", myname);
		exit(RET_ERR);
	}

	if ((fs_ind = sysfs(GETFSIND, MNTTYPE_LOFS)) < 0) {
		(void) fprintf(stderr, "mount: ");
		perror("sysfs");
		exit(RET_ERR);
	}

	signal(SIGHUP,  SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGINT,  SIG_IGN);

	if (mount(fsname, dir, flags | MS_FSS, fs_ind, 0, 0) < 0) {
		(void) fprintf(stderr, "mount: ");
		perror(fsname);
		exit(RET_ERR);
	}

	if (!mflg) {
		mnt.mnt_special = fsname;
		mnt.mnt_mountp = dir;
		mnt.mnt_fstype = fstype;
		mnt.mnt_mntopts = (flags & MS_RDONLY) ? MNTOPT_RO : MNTOPT_RW;

		if (fsaddtomtab(&mnt))
			exit(RET_ERR);
	}
	exit(0);
	/* NOTREACHED */
}

void
usage()
{
	(void) fprintf(stderr,
	    "Usage: mount -F lofs [-r] [-o ro | rw] dir mountpoint\n");
	exit(RET_ERR);
}
