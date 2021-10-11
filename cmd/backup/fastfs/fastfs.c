
#ident "@(#)fastfs.c 1.3 93/07/12"

/*
 * fastfs
 *	user interface to dio (delayed IO) functionality
 */
#include <stdio.h>
#include <locale.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/filio.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef USG
/*
 * make vfstab look like fstab
 */
#include <sys/mnttab.h>

#define	fstab		vfstab
#define	FSTAB		VFSTAB
#define	fs_spec		vfs_special
#define	fs_file		vfs_mountp
#define	setmntent	fopen
#define	endmntent	fclose
#define	mntent		mnttab
#define	mnt_fsname	mnt_special
#define	mnt_dir		mnt_mountp
#define	mnt_type	mnt_fstype
#define	MNTTYPE_42	"ufs"
#define	MNTINFO_DEV	"dev"
#define	MOUNTED		MNTTAB

static struct mntent *
mygetmntent(f, name)
	FILE *f;
	char *name;
{
	static struct mntent mt;
	int status;

	if ((status = getmntent(f, &mt)) == 0)
		return (&mt);

	switch (status) {
	case EOF:	break;		/* normal exit condition */
	case MNT_TOOLONG:
		fprintf(stderr, "%s has a line that is too long\n", name);
		break;
	case MNT_TOOMANY:
		fprintf(stderr, "%s has a line with too many entries\n", name);
		break;
	case MNT_TOOFEW:
		fprintf(stderr, "%s has a line with too few entries\n", name);
		break;
	default:
		fprintf(stderr,
			"Unknown return code, %d, from getmntent() on %s\n",
			status, name);
		break;
	}

	return (NULL);
}

#else
#include <mntent.h>

#define	mygetmntent	getmntent
#endif

/*
 * error return
 */
extern int	errno;

/*
 * command line processing
 */
extern char	*optarg;
extern int	optind;
extern int	opterr;

/*
 * -a = all
 * -v = verbose
 * -f = fast
 * -s = safe
 */
static int all	= 0;
static int verbose = 0;
static int fast	= 0;
static int safe	= 0;

/*
 * exitstatus
 *	0 all ok
 *	1 internal error
 *	2 system call error
 */
static int exitstatus	= 0;

/*
 * list of filenames
 */
struct filename {
	struct filename	*fn_next;
	char		*fn_name;
};
static struct filename	*fnanchor	= 0;

/*
 * for prettyprint
 */
static int firsttime	= 0;

/*
 * no safe's printed
 */
static int no_safes_printed	= 0;

#ifdef __STDC__
static void exitusage(void);
static void printstatusline(char *, char *);
static void printstatus(char *);
static void getmntnames(void);
static void getcmdnames(int, char **, int);
static void setdio(char *);
#else
static void exitusage();
static void printstatusline();
static void printstatus();
static void getmntnames();
static void getcmdnames();
static void setdio();
#endif

main(argc, argv)
	int		argc;
	char		**argv;
{
	int		c;
	struct filename	*fnp;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	exitstatus = 0;

	/*
	 * process command line
	 */
	opterr = 0;
	optarg = 0;

	while ((c = getopt(argc, argv, "avfs")) != -1)
		switch (c) {
		case 'a':
			all = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'f':
			fast = 1;
			break;
		case 's':
			safe = 1;
			break;
		default:
			exitusage();
			break;
		}

#ifdef lint
	c = verbose;
#endif
	if (argc == 1) {
		no_safes_printed = 1;
		all = 1;
	}

	if (all)
		/*
		 * use /etc/mtab
		 */
		getmntnames();
	else
		/*
		 * use command line
		 */
		getcmdnames(argc, argv, optind);

	/*
	 * for each filename, doit
	 */
	for (fnp = fnanchor; fnp; fnp = fnp->fn_next) {
		if (fast || safe)
			setdio(fnp->fn_name);
		else
			printstatus(fnp->fn_name);
	}

	/*
	 * all done
	 */
	exit(exitstatus);
#ifdef lint
	return (0);
#endif
}
/*
 * exitusage
 *	bad command line, give hint
 */
static void
#ifdef __STDC__
exitusage(void)
#else
exitusage()
#endif
{
	(void) fprintf(stderr, "usage: fastfs [-vfs] [-a] [file system ...]\n");
	exit(1);
}
/*
 * printstatusline
 * 	prettyprint the status line
 */
static void
printstatusline(fn, mode)
	char	*fn;
	char	*mode;
{
	if (firsttime++ == 0)
		(void) printf("%-20s %-10s\n", "Filesystem", "Mode");
	(void) printf("%-20s %-10s\n", fn, mode);
}
/*
 * printstatus
 *	get and prettyprint file system lock status
 */
static void
printstatus(fn)
	char		*fn;
{
	int		fd;
	int		dioval;

	fd = open(fn, O_RDONLY);
	if (fd == -1) {
		perror(fn);
		exitstatus = 2;
		return;
	}
#ifdef USG
	if (ioctl(fd, _FIOGDIO, &dioval) == -1) {
#else
	if (ioctl(fd, FIODIOS, &dioval) == -1) {
#endif
		perror(fn);
		(void) close(fd);
		exitstatus = 2;
		return;
	}
	if (dioval)
		printstatusline(fn, "fast");
	else
		if (no_safes_printed == 0)
			printstatusline(fn, "safe");
	(void) close(fd);
}
/*
 * getmntnames
 *	file names from /etc/mtab
 */
static void
#ifdef __STDC__
getmntnames(void)
#else
getmntnames()
#endif
{
	int		fnlen;
	struct filename	*fnp;
	struct filename	*fnpc;
	FILE		*mnttab;
	struct mntent	*mntp;

	fnpc = fnanchor;

	mnttab = setmntent(MOUNTED, "r");
	while ((mntp = mygetmntent(mnttab, MOUNTED)) != NULL) {
		if (mntp->mnt_type == (char *)0 ||
		    strcmp(mntp->mnt_type, MNTTYPE_42) != 0)
			continue;
		if (mntp->mnt_dir == (char *)0)
			mntp->mnt_dir = "";
		fnlen = strlen(mntp->mnt_dir) + 1;
		fnp = (struct filename *)malloc(sizeof (struct filename));
		fnp->fn_name = malloc((u_int)fnlen);
		(void) strcpy(fnp->fn_name, mntp->mnt_dir);
		fnp->fn_next = NULL;
		if (fnpc)
			fnpc->fn_next = fnp;
		else
			fnanchor = fnp;
		fnpc = fnp;
	}
	(void) endmntent(mnttab);
}
/*
 * getcmdnames
 *	file names from command line
 */
static void
getcmdnames(argc, argv, i)
	int	argc;
	char	**argv;
	int	i;
{
	struct filename	*fnp;
	struct filename	*fnpc;

	for (fnpc = fnanchor; i < argc; ++i) {
		fnp = (struct filename *)malloc(sizeof (struct filename));
		fnp->fn_name = *(argv+i);
		fnp->fn_next = NULL;
		if (fnpc)
			fnpc->fn_next = fnp;
		else
			fnanchor = fnp;
		fnpc = fnp;
	}
}
/*
 * setdio
 *	set the dio mode
 */
static void
setdio(fn)
	char		*fn;
{
	int		fd;
	int		dioval;

	fd = open(fn, O_RDONLY);
	if (fd == -1) {
		perror(fn);
		exitstatus = 2;
		return;
	}

	if (fast)
		dioval = 1;
	if (safe)
		dioval = 0;

#ifdef USG
	if (ioctl(fd, _FIOSDIO, &dioval) == -1) {
#else
	if (ioctl(fd, FIODIO, &dioval) == -1) {
#endif
		perror(fn);
		exitstatus = 2;
	}
	(void) close(fd);
}
