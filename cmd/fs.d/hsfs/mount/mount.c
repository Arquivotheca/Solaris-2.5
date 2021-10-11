/*	@(#)mount.c 1.12 94/04/11  SMI */

/*
 * mount_hsfs: fsname dir type opts
 * Copyright (c) 1989 by Sun Microsystem, Inc.
 */

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>	/* defines F_LOCK for lockf */
#include <stdlib.h>	/* for getopt(3) */
#include <signal.h>
#include <locale.h>
#include <fslib.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/mnttab.h>
#include <sys/mount.h>
#include <sys/fs/hsfs_susp.h>
#include <sys/fs/hsfs_rrip.h>

extern int errno;
extern int optind;
extern char *optarg;

#define	FSTYPE		"hsfs"
#define	MNTOPT_DEV	"dev"
#define	NAME_MAX	64
#define	READONLY	0
#define	NORRIP		1
#define	NOSUID 		2
#define	NOTRAILDOT	3
#define	TRAILDOT	4
#define	NOMAPLCASE	5
#define	MAPLCASE	6

static int nrrflag	= 0;	/* no rock ridge flag */
static int roflag	= 0;
static int mflg		= 0;	/* don't update /etc/mnttab flag */
static int nosuidflag	= 0;	/* don't allow setuid execution */
static int notraildotflag = 0;	/* truncate trailing dots in filenames */
				/*	for ISO CDs */
static int nomaplcaseflag = 0;	/* don't map filenames to lower case */

static char fstype[] = FSTYPE;

static char typename[NAME_MAX], *myname;
static char *myopts[] = {
	"ro",
	"nrr",
	"nosuid",
	"notraildot",
	"traildot",
	"nomaplcase",
	"maplcase",
	NULL
};


static void do_mount(char *, char *, int, int);
static void rpterr(char *, char *);
static void usage(void);

int
main(int argc, char **argv)
{
	char *options, *value;
	char *special, *mountp;
	struct mnttab mm;
	int c;
	char	obuff[256];
	int hsfs_flags;

	int flags;
	int Oflg = 0;   /* Overlay mounts */

	(void) setlocale(LC_ALL, "");

#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	myname = strrchr(argv[0], '/');
	if (myname)
		myname++;
	else
		myname = argv[0];
	sprintf(typename, "%s %s", fstype, myname);
	argv[0] = typename;

	/* check for proper arguments */

	while ((c = getopt(argc, argv, "o:rmO")) != EOF) {
		switch (c) {
			case 'o':
				options = optarg;
				while (*options != '\0')
				switch (getsubopt(&options, myopts, &value)) {
					case READONLY:
						roflag++;
						break;
					case NORRIP:
						nrrflag++;
						break;
					case NOSUID:
						nosuidflag++;
						break;
					case NOTRAILDOT:
						notraildotflag = 1;
						break;
					case TRAILDOT:
						notraildotflag = 0;
						break;
					case NOMAPLCASE:
						nomaplcaseflag = 1;
						break;
					case MAPLCASE:
						nomaplcaseflag = 0;
						break;
					default:
#define	ILLSUBMSG	gettext("%s: illegal -o suboptions: %s\n")
#define	MISSUBMSG	gettext("%s: missing suboption\n")
						if (value)
							fprintf(stderr,
								ILLSUBMSG,
								typename,
								value);
						else
							fprintf(stderr,
								MISSUBMSG,
								typename);
						usage();
				}
				break;
			case 'O':
				Oflg++;
				break;
			case 'r':
				roflag++;
				break;
			case 'm':
				mflg++;
				break;
		}
	}

	if ((argc - optind) != 2)
		usage();

	special = argv[optind++];
	mountp = argv[optind++];

	if (roflag == 0) {
		(void) fprintf(stderr,
		    gettext("%s: %s must be mounted readonly\n"),
		    myname, special);
		usage();
	}

	if (geteuid() != 0) {
		fprintf(stderr, gettext("%s: not super user\n"), myname);
		exit(31+2);
	}

	flags = MS_RDONLY;

	flags |= Oflg ? MS_OVERLAY : 0;
	flags |= (nosuidflag ? MS_NOSUID : 0);

	hsfs_flags = 0;
	if (nrrflag)
		hsfs_flags |= HSFSMNT_NORRIP;
	if (notraildotflag)
		hsfs_flags |= HSFSMNT_NOTRAILDOT;
	if (nomaplcaseflag)
		hsfs_flags |= HSFSMNT_NOMAPLCASE;

	signal(SIGHUP,  SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGINT,  SIG_IGN);
	/*
	 *	Perform the mount.
	 *	Only the low-order bit of "roflag" is used by the system
	 *	calls (to denote read-only or read-write).
	 */
	do_mount(special, mountp, flags, hsfs_flags);
	if (!mflg) {
		mm.mnt_special = special;
		mm.mnt_mountp = mountp;
		mm.mnt_fstype = fstype;
		mm.mnt_mntopts = obuff; *obuff = 0;

		if (nrrflag)
			(void) strcpy(mm.mnt_mntopts, "ro,nrr");
		else
			(void) strcpy(mm.mnt_mntopts, "ro");
		if (nosuidflag)
			(void) strcat(mm.mnt_mntopts, ",nosuid");
		if (notraildotflag)
			(void) strcat(mm.mnt_mntopts, ",notraildot");
		if (nomaplcaseflag)
			(void) strcat(mm.mnt_mntopts, ",nomaplcase");
		if (fsaddtomtab(&mm))
			exit(1);
	}
	exit(0);
	/* NOTREACHED */
}


static void
rpterr(char *bs, char *mp)
{
	switch (errno) {
	case EPERM:
		fprintf(stderr, gettext("%s: not super user\n"), myname);
		break;
	case ENXIO:
		fprintf(stderr, gettext("%s: %s no such device\n"), myname, bs);
		break;
	case ENOTDIR:
		fprintf(stderr,
gettext("%s: %s not a directory\n\tor a component of %s is not a directory\n"),
		    myname, mp, bs);
		break;
	case ENOENT:
		fprintf(stderr,
		    gettext("%s: %s or %s, no such file or directory\n"),
		    myname, bs, mp);
		break;
	case EINVAL:
		fprintf(stderr, gettext("%s: %s is not an hsfs file system.\n"),
		    typename, bs);
		break;
	case EBUSY:
		fprintf(stderr,
		    gettext("%s: %s is already mounted, %s is busy,\n"),
		    myname, bs, mp);
fprintf(stderr, gettext("\tor allowable number of mount points exceeded\n"));
		break;
	case ENOTBLK:
		fprintf(stderr,
		    gettext("%s: %s not a block device\n"), myname, bs);
		break;
	case EROFS:
		fprintf(stderr, gettext("%s: %s write-protected\n"),
		    myname, bs);
		break;
	case ENOSPC:
		fprintf(stderr,
		    gettext("%s: %s is corrupted. needs checking\n"),
		    myname, bs);
		break;
	default:
		perror(myname);
		fprintf(stderr, gettext("%s: cannot mount %s\n"), myname, bs);
	}
}


static void
do_mount(char *special, char *mountp, int flag, int hsfs_flags)
{
	if (mount(special, mountp, flag | MS_DATA,
		fstype, (caddr_t) &hsfs_flags, sizeof (hsfs_flags)) == -1) {
		rpterr(special, mountp);
		exit(31+2);
	}
}


static void
usage()
{
	char *opts;

opts = "{-r | -o ro | -o nrr | -o nosuid | -o notraildot | -o nomaplcase}";
	(void) fprintf(stdout,
gettext("hsfs usage: mount [-F hsfs] %s {special | mount_point}\n"), opts);
	(void) fprintf(stdout,
gettext("hsfs usage: mount [-F hsfs] %s special mount_point\n"), opts);
	exit(32);
}
