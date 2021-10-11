#pragma ident	"@(#)mount.c	1.8	94/01/04 SMI"

/*
 * Copyright (c) 1988 Sun Microsystems, Inc.
 */

#include <locale.h>
#include <stdio.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mnttab.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <fslib.h>

#define	MNTTYPE_PCFS	"pcfs"

extern time_t	time();
extern time_t	timezone;
extern int	daylight;

extern int errno;

#define	RO	0
#define	RW	1

int roflag = 0;
static char *myopts[] = {
	"ro",
	"rw",
	NULL
};

struct pcfs_args {
	int	secondswest;	/* seconds west of Greenwich */
	int	dsttime;	/* type of dst correction */
} tz;

main(argc, argv)
	int argc;
	char *argv[];
{
	struct mnttab mnt;
	int c;
	char *myname;
	char typename[64];
	char *options, *value;
	extern int optind;
	extern char *optarg;
	int error = 0;
	int verbose = 0;
	int mflg = 0;
	int optcnt = 0;
	extern int getopt();
	extern int getsubopt();
	extern int atoi();

	myname = strrchr(argv[0], '/');
	myname = myname ? myname + 1 : argv[0];
	(void) sprintf(typename, "%s_%s", MNTTYPE_PCFS, myname);
	argv[0] = typename;

	while ((c = getopt(argc, argv, "Vvr?o:O")) != EOF) {
		switch (c) {
		case 'V':
		case 'v':
			verbose++;
			break;
		case '?':
			error++;
			break;
		case 'r':
			roflag++;
			break;
		case 'o':
			options = optarg;
			while (*options != '\0') {
				switch (getsubopt(&options, myopts, &value)) {
				case RO:
					roflag++;
					break;
				case RW:
					break;
				default:
					(void) fprintf(stderr,
				    gettext("%s: illegal -o suboption -- %s\n"),
					    typename, value);
					error++;
					break;
				}
			}
			break;
		case 'O':
			mflg |= MS_OVERLAY;
			break;
		}
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
		/*
		 * don't hint at options yet (none are really supported)
		 */
		(void) fprintf(stderr,
		    gettext("Usage: %s special mount_point\n"), typename);
		exit (32);
	}

	mnt.mnt_special = argv[optind++];
	mnt.mnt_mountp = argv[optind++];
	mnt.mnt_fstype = MNTTYPE_PCFS;

	(void) tzset();
	tz.secondswest = timezone;
	tz.dsttime = daylight;
	mflg |= MS_DATA;
	if (roflag) {
		mflg |= MS_RDONLY;
		mnt.mnt_mntopts = "ro";
	} else {
		mnt.mnt_mntopts = "rw";
	}

	signal(SIGHUP,  SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGINT,  SIG_IGN);

	if (verbose) {
		(void) fprintf(stderr, "mount(%s, \"%s\", %d, %s",
		    mnt.mnt_special, mnt.mnt_mountp, mflg, MNTTYPE_PCFS);
	}
	if (mount(mnt.mnt_special, mnt.mnt_mountp, mflg, MNTTYPE_PCFS,
	    (char *)&tz, sizeof (struct pcfs_args))) {
		if (errno == EBUSY) {
			fprintf(stderr, gettext
("mount: %s is already mounted, %s is busy,\n\tor allowable number of mount points exceeded\n"),
			    mnt.mnt_special, mnt.mnt_mountp);
		} else
			perror("mount");
		exit (32);
	}

	if (fsaddtomtab(&mnt))
		exit(1);
	exit(0);
	/* NOTREACHED */
}
