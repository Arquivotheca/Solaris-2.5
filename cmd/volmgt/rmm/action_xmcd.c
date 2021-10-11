/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident  "@(#)action_xmcd.c	1.5     94/11/22 SMI"

#include	<errno.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<unistd.h>
#include	<fcntl.h>
#include	<pwd.h>
#include	<limits.h>
#include	<signal.h>
#include	<string.h>
#include	<rmmount.h>

#include	<sys/types.h>
#include	<sys/dkio.h>
#include	<sys/cdio.h>
#include	<sys/vtoc.h>
#include	<sys/param.h>
#include	<sys/systeminfo.h>
#include	<sys/stat.h>
#include	<volmgt.h>


/*
 * If this cdrom has audio tracks, start up xmcd.
 */

#define	TRUE	(-1)
#define	FALSE	(0)

static void	setup_user(void);
static void	set_user_env(int, char **);
static void	add_to_args(int *, char ***, int, char **);
static void	clean_args(int *, char **);

extern void	dprintf(const char *, ...);

#ifdef	DEBUG
static void	print_args(char *, int, char **);
#endif


int
action(struct action_arg **aa, int argc, char **argv)
{
	struct cdrom_tochdr	th;
	struct cdrom_tocentry	te;
	unsigned char		i;
	int			fd;
	int			found_audio = FALSE;
	extern char		*rmm_dsodir;
	char			*atype = getenv("VOLUME_ACTION");



	if (strcmp(atype, "insert") != 0) {
		return (FALSE);
	}

	/* ensure caller has specified the program to run */
	if (argc < 1) {
		dprintf("action_xmcd: no program to run!\n");
		return (FALSE);
	}

	if (aa[0]->aa_rawpath == NULL) {
		dprintf("action_xmcd: no rawpath\n");
		return (FALSE);
	}

	dprintf("action_xmcd: raw path = %s\n", aa[0]->aa_rawpath);

	if ((fd = open(aa[0]->aa_rawpath, O_RDONLY)) < 0) {
		dprintf("action_xmcd: open %m\n");
		return (FALSE);
	}

	/* read the TOC (tbl of contents) */
	if (ioctl(fd, CDROMREADTOCHDR, &th) < 0) {
		dprintf("action_xmcd: ioctl %m\n");
		close(fd);
		return (FALSE);
	}

	/* look for audio */
	te.cdte_format = CDROM_MSF;
	for (i = th.cdth_trk0; i < th.cdth_trk1+1; i++) {
		te.cdte_track = i;
		if (ioctl(fd, CDROMREADTOCENTRY, &te) < 0) {
			continue;
		}
		if ((int)te.cdte_datamode == 255) {
			found_audio = TRUE;
			break;
		}
	}
	close(fd);

	if (found_audio == FALSE) {
		dprintf("action_xmcd: no audio\n");
		return (FALSE);
	}

	dprintf("action_xmcd: found audio (%d tracks)\n",
		th.cdth_trk1 - th.cdth_trk0 + 1);

	/*
	 * Set the ENXIO on eject attribute.  This causes xmcd
	 * to get an ENXIO if someone types eject from another
	 * window.  Workman, when started with the -X flag, exits
	 * as soon as it ejects the media, or if it ever sees an
	 * ENXIO from an ioctl.
	 */
	media_setattr(aa[0]->aa_rawpath, "s-enxio", "true");

	/* start xmcd: don't care about errors just fire and forget */
	if (fork() == 0) {
		int		fd;
		int		argc_new = 0;
		char		*argv_new[3];
		char		sympath[MAXNAMELEN];


#ifdef	DEBUG
		fprintf(stderr, "DEBUG: child is running (pid = %d)\n",
			getpid());
#endif
		/* child */
		chdir(rmm_dsodir);

		/* stick his error messages out on the console */
		fd = open("/dev/console", O_RDWR);

		dup2(fd, 0);
		dup2(fd, 1);
		dup2(fd, 2);

#ifdef	DEBUG
		fprintf(stderr, "DEBUG: child: after switching output\n");
#endif
		/* clean up args */
		clean_args(&argc, argv);
#ifdef	DEBUG
		fprintf(stderr, "DEBUG: child: after cleaning up args\n");
#endif

		/*
		 * set_user_env() and setup_user() do similar things, but
		 * the former is new for xmcd and the latter is around
		 * around for historical reasons
		 */
		set_user_env(argc, argv);
		setup_user();
#ifdef	DEBUG
		(void) fprintf(stderr, "DEBUG: child: after setting up env\n");
#endif

		/* remove leading shared-object name (ours!) */
		argc--; argv++;

		/* set up path to use (instead of rawpath) */
		(void) sprintf(sympath, "/vol/dev/aliases/%s",
		    getenv("VOLUME_SYMDEV"));

		/* add to argv passed in */
		argv_new[argc_new++] = strdup("-dev");
		argv_new[argc_new++] = sympath;
		argv_new[argc_new] = NULL;

#ifdef	DEBUG
		(void) fprintf(stderr,
		    "DEBUG: child: about to add to args ...\n");
#endif

		add_to_args(&argc, &argv, argc_new, argv_new);

#ifdef	DEBUG
		print_args("before exec", argc, argv);
#endif
		/* run that hoser */
		execv(argv[0], argv);

		fprintf(stderr, "exec of \"%s\" failed; %s\n", argv[0],
			strerror(errno));

		/* bummer, it failed -- EXIT, don't return!! */
		exit(1);
	}

	/*
	 * we return false here because audio might not be the only thing
	 * on the disk, and we want actions to continue.
	 */
#ifdef	DEBUG
	fprintf(stderr, "DEBUG: sleeping a while -- just wait\n");
	sleep(15);
#endif
	return (FALSE);
}


/*
 * setup_user:
 *	set a reasonable user and group to run workman as.  The default
 *	is to make it be daemon/other.
 *	The other thing we want to do is make the cwd to be
 *	the user's home directory so all the nice workman databases
 *	work right.
 */

static void
setup_user(void)
{
	struct stat	sb;
	uid_t		uid = 1;	/* daemon */
	gid_t		gid = 1;	/* other */
	struct passwd	*pw;
	char		namebuf[MAXNAMELEN];

	/*
	 * The assumption is that a workstation is being used by
	 * the person that's logged into the console and that they
	 * just inserted the cdrom.  This breaks down on servers,
	 * but the most common case (by far) is someone listening
	 * to a cdrom at thier desk, while logged in and running the
	 * window system.
	 */
	if (stat("/dev/console", &sb) == 0) {
		if (sb.st_uid != 0) {
			uid = sb.st_uid;
		}
	}
	if (uid != 1) {
		if ((pw = getpwuid(uid)) != NULL) {
			gid = pw->pw_gid;
			(void) sprintf(namebuf, "HOME=%s", pw->pw_dir);
			(void) putenv(strdup(namebuf));
		} else {
			(void) putenv("HOME=/tmp");
		}
	}

	(void) setuid(uid);
	(void) seteuid(uid);
	(void) setgid(gid);
	(void) setegid(gid);
}


/*
 * set_user_env -- set up user environment
 */
static void set_user_env(int ac, char **av)
{
	int			i;
	int			display_specified = 0;
	static char		hostname[MAXNAMELEN];
	static char		display[MAXNAMELEN];
	static char		ld_lib_path[MAXNAMELEN];
	static char		xfsp[MAXNAMELEN];
	static char		xufsp[MAXNAMELEN];


#ifdef	DEBUG
	fprintf(stderr, "DEBUG: set_user_env(): entering\n");
	print_args("entry to set_user_env()", ac, av);
#endif

	/* only set display if it wasn't passed in */
	for (i = 0; i < ac; i++) {
		if ((strcmp(av[i], "-display") == 0) &&
		    (ac > (i+1))) {
			display_specified++;
			strcpy(display, av[i+1]);
			break;
		}
	}

#ifdef	DEBUG
	fprintf(stderr, "DEBUG: set_user_env(): display found = %s\n",
		display_specified ? "TRUE" : "FALSE");
#endif

	if (display_specified == 0) {
		(void) sysinfo(SI_HOSTNAME, hostname, MAXNAMELEN);
		(void) sprintf(display, "DISPLAY=%s:0.0", hostname);
		(void) putenv(display);
	}

	(void) sprintf(ld_lib_path, "LD_LIBRARY_PATH=%s:%s:%s:%s",
	    "/usr/lib",
	    "/usr/dist/local/share/SUNWmotif/lib",
	    "/usr/openwin/lib",
	    "/usr/ucblib");
	(void) putenv(ld_lib_path);

	(void) sprintf(xfsp, "XFILESEARCHPATH=%s:%s",
	    "/usr/openwin/lib/app-defaults/%L/%N",
	    "/usr/openwin/lib/app-defaults/%N");
	(void) putenv(xfsp);

	(void) sprintf(xufsp, "XUSERFILESEARCHPATH=%s:%s",
	    "/usr/openwin/lib/app-defaults/%L/%N",
	    "/usr/openwin/lib/app-defaults/%N");
	(void) putenv(xufsp);

	(void) putenv("OPENWINHOME=/usr/openwin");

#ifdef	DEBUG
	(void) fprintf(stderr, "DEBUG: set_user_env(): leaving\n");
#endif

}


/*
 * add src args to dest arg list
 */
static void
add_to_args(
	int *dest_argcp,			/* ptr to dest argc */
	char ***dest_argvp,			/* ptr to dest argv */
	int src_argc,				/* src argc */
	char **src_argv)			/* src argv */
{
	int		dest_argc = *dest_argcp;
	char		**dest_argv = *dest_argvp;
	int		amt;
	char		**arr;
	int		ind;
	int		i;



	/* find size of new array */
	amt = dest_argc + src_argc;

	/* allocate array */
	if ((arr = (char **)malloc((amt + 1) * sizeof (char *))) == NULL) {
		fprintf(stderr, "error: can't allocate space!\n");
		return;		/* just return the orig array ?? */
	}

	/* copy old and new array into our new space */
	ind = 0;
	for (i = 0; i < dest_argc; i++) {
		arr[ind++] = dest_argv[i];
	}
	for (i = 0; i < src_argc; i++) {
		arr[ind++] = src_argv[i];
	}
	arr[ind] = NULL;

	/* return result in place of dest junk */
	*dest_argcp = amt;
	*dest_argvp = arr;
}

/*
 * clean up args:
 *	- ensure av[0] == av[1]
 *	- ensure that *acp is correct
 *	- correct the device crap		-- NOT YET IMPLEMENTED
 */
static void
clean_args(int *acp, char **av)
{
	int	i;


	/* ensure count is correct */
	for (i = 0; i < *acp; i++) {
		if (av[i] == NULL) {
			*acp = i;
			break;
		}
	}
}

#ifdef	DEBUG

static char *
safe(char *str)
{
	static char	*ohoh = "(void ptr!)";


	if (str) {
		return (str);
	}
	return (ohoh);
}


static void
print_args(char *tag, int ac, char **av)
{
	int	i;


	fprintf(stderr, "DEBUG: %s:\n", tag);

	for (i = 0; i < ac; i++) {
		fprintf(stderr, " arg[%d] = \"%s\"\n", i, safe(av[i]));
	}
}

#endif	/* DEBUG */
