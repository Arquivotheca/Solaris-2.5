/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)eject.c	1.25	95/02/22 SMI"

/*
 * Program to eject oen or more pieces of media.
 */

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<sys/fdio.h>
#include	<sys/dkio.h>
#include	<sys/param.h>
#include	<sys/systeminfo.h>
#include	<sys/wait.h>
#include	<dirent.h>
#include	<fcntl.h>
#include	<string.h>
#include	<errno.h>
#include	<locale.h>
#include	<libintl.h>
#include	<unistd.h>
#include	<pwd.h>
#include	<volmgt.h>

#include	"volutil.h"

#ifdef	bool_t
#undef	bool_t
#endif
typedef enum {false=0, true=1}	bool_t;

static char	*prog_name = NULL;
static bool_t	force_eject = false;
static bool_t	do_query = false;


/*
 * Hold over from old eject.
 * returns exit codes:	(KEEP THESE - especially important for query)
 *	0 = -n, -d or eject operation was ok, -q = media in drive
 *	1 = -q only = media not in drive
 *	2 = various parameter errors, etc.
 *	3 = eject ioctl failed
 * New Value (2/94)
 *	4 = eject partially succeeded, but now manually remove media
 */

#define	EJECT_OK		0
#define	EJECT_NO_MEDIA		1
#define	EJECT_PARM_ERR		2
#define	EJECT_IOCTL_ERR		3
#define	EJECT_MAN_EJ		4

#define	CONSOLE			"/dev/console"
#define	BIT_BUCKET		"/dev/null"

/*
 * the openwindows command to run (if we can) -- by full pathname, and
 * just the command name itself
 */
#define	EJECT_POPUP_PATH	"/usr/lib/vold/eject_popup"
#define	EJECT_POPUP		"eject_popup"

#define	OW_WINSYSCK_PATH	"/usr/openwin/bin/winsysck"
#define	OW_WINSYSCK		"winsysck"
#define	OW_WINSYSCK_PROTOCOL	"x11"

#define	AVAIL_MSG		"%s is available\n"
#define	NOT_AVAIL_MSG		"%s is not available\n"

#define	OK_TO_EJECT_MSG		"%s can now be manually ejected\n"

#define	FLOPPY_MEDIA_TYPE	"floppy"
#define	CDROM_MEDIA_TYPE	"cdrom"

#define	MEJECT_PROP		"s-mejectable"
#define	PROP_TRUE		"true"



void
main(int argc, char **argv)
{
	static int	work(char *);
	static void	usage(void);
	static char	*getdefault(void);
	extern int	optind;
	int		c;
	const char	*opts = "dqfn";
	char		*s;
	int		excode;
	int		res;
	bool_t		err_seen = false;
	bool_t		man_eject_seen = false;
	bool_t		do_pdefault = false;
	bool_t		do_paliases = false;



	(void) setlocale(LC_ALL, "");

#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN	"SYS_TEST"
#endif

	(void) textdomain(TEXT_DOMAIN);

	prog_name = argv[0];

	/* process arguments */
	while ((c = getopt(argc, argv, opts)) != EOF) {
		switch (c) {
		case 'd':
			do_pdefault = true;
			break;
		case 'q':
			do_query = true;
			break;
		case 'n':
			do_paliases = true;
			break;
		case 'f':
			force_eject = true;
			break;
		default:
			usage();
			exit(EJECT_PARM_ERR);
		}
	}

	if (do_pdefault) {
		s = getdefault();
		(void) fprintf(stderr,
		    gettext("Default device is: %s\n"),
		    ((s == NULL) ? gettext("nothing inserted") : s));
		exit(EJECT_OK);
	}

	if (do_paliases) {
		media_printaliases();
		exit(EJECT_OK);
	}

	if (argc == optind) {
		/* no argument -- use the default */
		if ((s = getdefault()) == NULL) {
			(void) fprintf(stderr,
			    gettext("No default media available\n"));
			exit(EJECT_NO_MEDIA);
		}
		/* (try to) eject default media */
		excode = work(s);
	} else {
		/* multiple thingys to eject */
		for (; optind < argc; optind++) {
			res = work(argv[optind]);
			if (res == 4) {
				man_eject_seen = true;
			} else if (res != EJECT_OK) {
				err_seen = true;
			}
		}
		if (err_seen) {
			excode = EJECT_IOCTL_ERR;
		} else if (man_eject_seen) {
			excode = EJECT_MAN_EJ;
		} else {
			excode = EJECT_OK;
		}
	}

	exit(excode);
}


static int
work(char *arg)
{
	static int	ejectit(char *);
	static bool_t	query(char *, bool_t);
	char 		*name;
	char 		*name1;
	int		excode = EJECT_OK;



	/*
	 * NOTE: media_findname (effectively) does a "volcheck all" if the
	 *  name passed in isn't an absolute pathname, or a name under
	 *  /vol/rdsk. This is not good when the user runs something
	 *  like "eject fd cd" on an intel, since the cd is "all but manually
	 *  ejected" (as it should be), but then "cd" is not found, so
	 *  a "volcheck all" is done, which remounts the floppy!
	 *
	 *  So, we run media_olaliases() first, to try to find the name before
	 *  running volcheck.
	 */

	/* check to see if name is an alias (e.g. "fd" or "cd") */
	if ((name1 = media_oldaliases(arg)) == NULL) {
		name1 = arg;
	}

	/*
	 * name is not an alias -- check for abs. path or
	 * /vol/rdsk name
	 */
	if ((name = media_findname(name1)) == NULL) {
		/*
		 * name is not an alias, an absolute path, or a name
		 *  under /vol/rdsk -- let's just use the name given
		 */
		name = name1;
	}

	/*
	 * Since eject is a suid root program, we must make sure
	 * that the user running us is allowed to eject the media.
	 * All a user has to do to issue the eject ioctl is open
	 * the file for reading, so that's as restrictive as we'll be.
	 */
	if (access(name, R_OK) != 0) {
		perror(name);
		return (EJECT_PARM_ERR);
	}

	if (do_query) {
		if (!query(name, true)) {
			excode = EJECT_NO_MEDIA;
		}
	} else {
		excode = ejectit(name);
	}
	return (excode);
}


static void
usage()
{
	(void) fprintf(stderr,
	    gettext("usage: %s [-fndq] [name | nickname]\n"),
	    prog_name);
	(void) fprintf(stderr,
	    gettext("options:\t-f force eject\n"));
	(void) fprintf(stderr,
	    gettext("\t\t-n show nicknames\n"));
	(void) fprintf(stderr,
	    gettext("\t\t-d show default device\n"));
	(void) fprintf(stderr,
	    gettext("\t\t-q query for media present\n"));
}


static int
ejectit(char *name)
{
	static bool_t	winsysck(struct passwd *);
	static bool_t	popup_msg(struct passwd *, char *);
	static bool_t	floppy_in_drive(char *, int);
	static bool_t	manually_ejectable(char *);
	int 		fd;
	bool_t		volmgt_is_running;
	FILE  		*console_fp;
	bool_t		mejectable = false;	/* manually ejectable */
	int		result = EJECT_OK;
	struct passwd	*pw = NULL;
	bool_t		do_manual_console_message = false;


	/*
	 * If volume management is not running, and the device is
	 * mounted, we try to umount the device.  If we fail, we
	 * give up, unless he used the -f flag.
	 */
	volmgt_is_running = (volmgt_running() != 0) ? true : false;
	if (!volmgt_is_running && dev_mounted(name)) {
		if (dev_unmount(name) == 0) {
			if (!force_eject) {
				(void) fprintf(stderr,
gettext("WARNING: can not unmount %s, the file system is (probably) busy\n"),
				    name);
				return (EJECT_PARM_ERR);
			} else {
				(void) fprintf(stderr,
gettext("WARNING: %s has a mounted filesystem, ejecting anyway\n"),
				    name);
			}
		}
	}

	if ((fd = open(name, O_RDONLY|O_NDELAY)) < 0) {
		if (errno == EBUSY) {
			(void) fprintf(stderr,
gettext("%s is busy (try 'eject floppy' or 'eject cdrom'?)\n"),
			    name);
			return (EJECT_PARM_ERR);
		}
		perror(name);
		return (EJECT_PARM_ERR);
	}

	/* see if media is manually ejectable (i.e. we can't do it) */
	if (volmgt_is_running) {
		mejectable = manually_ejectable(name);
	}

	/* try to eject the volume */
	if (ioctl(fd, DKIOCEJECT, 0) < 0) {

		/* check on why eject failed */

		/* check for no floppy in manually ejectable drive */
		if ((errno == ENOSYS) && !volmgt_is_running &&
		    !floppy_in_drive(name, fd)) {
			/* use code below to handle "not present" */
			errno = ENXIO;
		}

		/*
		 * Dump this message to stderr. This handles the
		 * case where the window system is not running
		 * and also works in case the user has run this
		 * via an rlogin to the remote volmgt console.
		 */
		if (errno == ENOSYS) {
			(void) fprintf(stderr, gettext(OK_TO_EJECT_MSG), name);
		}

		if ((errno == ENOSYS) && (!volmgt_is_running || mejectable)) {

			/*
			 * Make sure we know who *really* fired up this
			 * command. We'll need this information to connect
			 * to the user X display.
			 */

			pw = getpwuid(getuid());

#ifdef DEBUG
			if (pw != NULL) {
				(void) fprintf(stderr,
				    "DEBUG: ejectit: username = '%s'\n",
				    pw->pw_name);
				(void) fprintf(stderr,
				    "DEBUG: ejectit: uid = %d\n", pw->pw_uid);
				(void) fprintf(stderr,
				    "DEBUG: ejectit: gid = %d\n", pw->pw_gid);
				(void) fprintf(stderr,
				    "DEBUG: ejectit: euid = %ld\n", geteuid());
				(void) fprintf(stderr,
				    "DEBUG: ejectit: egid = %ld\n", getegid());
			} else {
				(void) fprintf(stderr,
				    "DEBUG: ejectit: getpwuid() failed\n");
			}
#endif

			/*
			 * If user is running some X windows system
			 * we'll display a popup to the console.
			 * If not, dump message to console.
			 *
			 * (To keep from having to actually check for X
			 * running, we'll just try to run the popup, assuming
			 * it will fail if windows are not running.)
			 */
			if ((access(EJECT_POPUP_PATH, X_OK) != 0) ||
			    (access(OW_WINSYSCK_PATH, X_OK) != 0) ||
			    (pw == NULL)) {
				do_manual_console_message = true;
			}

			if (!do_manual_console_message) {
				if (!winsysck(pw)) {
					do_manual_console_message = true;
				}
			}

			if (!do_manual_console_message) {
				if (!popup_msg(pw, name)) {
					do_manual_console_message = true;
				}
			}
			/*
			 * only output to console if requested and it's
			 * not the same as stderr
			 */
			if (do_manual_console_message) {
				char	*ttynm = ttyname(fileno(stderr));

				if ((ttynm != NULL) &&
				    (strcmp(ttynm, CONSOLE) != 0)) {
					console_fp = fopen(CONSOLE, "a");
					if (console_fp != NULL) {
						(void) fprintf(console_fp,
						    gettext(OK_TO_EJECT_MSG),
						    name);
						(void) fclose(console_fp);
					}
				}
			}

			/*
			 * keep track of the fact that this is a manual
			 * ejection
			 */
			result = EJECT_MAN_EJ;

		} else if ((errno == EAGAIN) || (errno == ENODEV) ||
		    (errno == ENXIO)) {
			(void) fprintf(stderr,
			    gettext("%s not present in a drive\n"),
			    name);
			result = EJECT_OK;
		} else {
			perror(name);
			result = EJECT_IOCTL_ERR;
		}
	}

	(void) close(fd);
	return (result);
}


/*
 * return true if a floppy is in the drive, false otherwise
 *
 * this routine assumes that the file descriptor passed in is for
 * a floppy disk.  this works because it's only called if the device
 * is "manually ejectable", which only (currently) occurs for floppies.
 */
static bool_t
floppy_in_drive(char *name, int fd)
{
	int	ival = 0;			/* ioctl return value */
	bool_t	rval = false;			/* return value */


	if (ioctl(fd, FDGETCHANGE, &ival) >= 0) {
		if (!(ival & FDGC_CURRENT)) {
			rval = true;		/* floppy is present */
		}
	} else {
		/* oh oh -- the ioctl failed -- it's not a floppy */
		(void) fprintf(stderr, gettext("%s is not a floppy disk\n"),
		    name);
	}

	return (rval);
}


/*
 * In my experience with removable media drivers so far... the
 * most reliable way to tell if a piece of media is in a drive
 * is simply to open it.  If the open works, there's something there,
 * if it fails, there's not.  We check for two errnos which we
 * want to interpret for the user,  ENOENT and EPERM.  All other
 * errors are considered to be "media isn't there".
 *
 * return true if media found, else false (XXX: was 0 and -1)
 */
static bool_t
query(char *name, bool_t doprint)
{
	int		fd;
	int		rval;			/* FDGETCHANGE return value */
	enum dkio_state	state;



	if ((fd = open(name, O_RDONLY|O_NONBLOCK)) < 0) {
		if ((errno == EPERM) || (errno == ENOENT)) {
			if (doprint) {
				perror(name);
			}
		} else {
			if (doprint) {
				(void) fprintf(stderr, gettext(NOT_AVAIL_MSG),
				    name);
			}
		}
		return (false);
	}

	rval = 0;
	if (ioctl(fd, FDGETCHANGE, &rval) >= 0) {
		/* hey, it worked, what a deal, it must be a floppy */
		(void) close(fd);
		if (!(rval & FDGC_CURRENT)) {
			if (doprint) {
				(void) fprintf(stderr, gettext(AVAIL_MSG),
				    name);
			}
			return (true);
		}
		if (rval & FDGC_CURRENT) {
			if (doprint) {
				(void) fprintf(stderr,	gettext(NOT_AVAIL_MSG),
				    name);
			}
			return (false);
		}
	}

again:
	state = DKIO_NONE;
	if (ioctl(fd, DKIOCSTATE, &state) >= 0) {
		/* great, the fancy ioctl is supported. */
		if (state == DKIO_INSERTED) {
			if (doprint) {
				(void) fprintf(stderr, gettext(AVAIL_MSG),
				    name);
			}
			(void) close(fd);
			return (true);
		}
		if (state == DKIO_EJECTED) {
			if (doprint) {
				(void) fprintf(stderr,	gettext(NOT_AVAIL_MSG),
				    name);
			}
			(void) close(fd);
			return (false);
		}
		/*
		 * Silly retry loop.
		 */
		(void) sleep(1);
		goto again;
	}
	(void) close(fd);

	/*
	 * Ok, we've tried the non-blocking/ioctl route.  The
	 * device doesn't support any of our nice ioctls, so
	 * we'll just say that if it opens it's there, if it
	 * doesn't, it's not.
	 */
	if ((fd = open(name, O_RDONLY)) < 0) {
		if (doprint) {
			(void) fprintf(stderr, gettext(NOT_AVAIL_MSG), name);
		}
		return (false);
	}

	(void) close(fd);
	if (doprint) {
		(void) fprintf(stderr, gettext(AVAIL_MSG), name);
	}
	return (true);	/* success */
}


/*
 * The assumption is that someone typed eject to eject some piece
 * of media that's currently in a drive.  So, what we do is
 * check for floppy then cdrom.  If there's nothing in either,
 * we just return NULL.
 */
static char *
getdefault()
{
	char		*s;


	if ((s = media_findname(FLOPPY_MEDIA_TYPE)) != NULL) {
		if (query(s, false)) {
			return (s);
		}
	}
	if ((s = media_findname(CDROM_MEDIA_TYPE)) != NULL) {
		if (query(s, false)) {
			return (s);
		}
	}
	if ((s = media_oldaliases(FLOPPY_MEDIA_TYPE)) != NULL) {
		if (query(s, false)) {
			return (s);
		}
	}
	if ((s = media_oldaliases(CDROM_MEDIA_TYPE)) != NULL) {
		if (query(s, false)) {
			return (s);
		}
	}
	return (NULL);
}


/*
 * Check to see if the specified device is manually ejectable, using
 * the media_getattr() call
 */
static bool_t
manually_ejectable(char *dev_path)
{
	char		*eprop;


	if ((eprop = media_getattr(dev_path, MEJECT_PROP)) == 0) {
		/* equivilent to false ? */
		return (false);
	}

	/* return result based on string returned */
	return ((strcmp(eprop, PROP_TRUE) == 0) ? true : false);

}


/*
 * Use a popup window to display the "manually ejectable"
 * message for X86 machines.
 *
 * return flase if the popup fails, else return true
 */
static bool_t
popup_msg(struct passwd *pw, char *name)
{
	pid_t		pid;
	int		exit_code = -1;
	bool_t		ret_val = false;
	int		fd;
	char		ld_lib_path[MAXNAMELEN];
	char		home_dir[MAXNAMELEN];



	/*
	 * fork a simple X Windows program to display gui for
	 * notifying the user that the specified media must be
	 * manually removed.
	 */

	if ((pid = fork()) < 0) {
		(void) fprintf(stderr,
		    gettext("error: can't fork a process (errno %d)\n"),
		    errno);
		goto dun;
	}

	if (pid == 0) {

		/*
		 * Error messages to console
		 */

		if ((fd = open(CONSOLE, O_RDWR)) >= 0) {
			(void) dup2(fd, fileno(stdin));
			(void) dup2(fd, fileno(stdout));
			(void) dup2(fd, fileno(stderr));
		}

		/*
		 * Set up the users environment.
		 */

		(void) putenv("DISPLAY=:0.0");
		(void) putenv("OPENWINHOME=/usr/openwin");

		(void) sprintf(ld_lib_path, "LD_LIBRARY_PATH=%s",
		    "/usr/openwin/lib");
		(void) putenv(ld_lib_path);

		/*
		 * We need to set $HOME so the users .Xauthority file
		 * can be located. This is especially needed for a user
		 * user MIT Magic Cookie authentication security.
		 */

		(void) sprintf(home_dir, "HOME=%s", pw->pw_dir);
		(void) putenv(home_dir);

		/*
		 * We need the X application to be able to connect to
		 * the user's display so we better run as if we are
		 * the user (effectively).
		 * Don't want x program doing anything nasty.
		 *
		 * Note - have to set gid stuff first as effective uid
		 *	  must belong to root for this to work correctly.
		 */

		(void) setgid(pw->pw_gid);
		(void) setegid(pw->pw_gid);
		(void) setuid(pw->pw_uid);
		(void) seteuid(pw->pw_uid);

#ifdef DEBUG
		(void) fprintf(stderr,
		    "DEBUG: \"%s\" being execl'ed with name = \"%s\"\n",
		    EJECT_POPUP_PATH, name);
#endif

		(void) execl(EJECT_POPUP_PATH, EJECT_POPUP, "-n", name, NULL);

		(void) fprintf(stderr,
		    gettext("error: exec of \"%s\" failed (errno = %d)\n"),
		    EJECT_POPUP_PATH, errno);
		exit(-1);

	}

	/* the parent -- wait for the child */
	if (waitpid(pid, &exit_code, 0) == pid) {
		if (WIFEXITED(exit_code)) {
			if (WEXITSTATUS(exit_code) == 0) {
				ret_val = true;
			}
		}
	}

dun:
	/* all done */
#ifdef	DEBUG
	(void) fprintf(stderr, "DEBUG: popup_msg() returning %s\n",
	    ret_val ? "true" : "false");
#endif
	return (ret_val);
}


/*
 * Use a popup window to display the "manually ejectable"
 * message for X86 machines.
 *
 * return flase if the popup fails, else return true
 */
static bool_t
winsysck(struct passwd *pw)
{
	pid_t		pid;
	int		exit_code = -1;
	bool_t		ret_val = false;
	int		fd;
	char		home_dir[MAXNAMELEN];
	char		ld_lib_path[MAXNAMELEN];



	if ((pid = fork()) < 0) {
		(void) fprintf(stderr,
		    gettext("error: can't fork a process (errno %d)\n"),
		    errno);
		goto dun;
	}

	if (pid == 0) {

		/*
		 * error messages to console
		 */

#ifndef	DEBUG
		if ((fd = open(BIT_BUCKET, O_RDWR)) >= 0) {
			(void) dup2(fd, fileno(stdin));
			(void) dup2(fd, fileno(stdout));
			(void) dup2(fd, fileno(stderr));
		}
#endif

		/*
		 * set up the users environment
		 */
		(void) putenv("DISPLAY=:0.0");
		(void) putenv("OPENWINHOME=/usr/openwin");

		(void) sprintf(ld_lib_path, "LD_LIBRARY_PATH=%s",
		    "/usr/openwin/lib");
		(void) putenv(ld_lib_path);

		/*
		 * we need to set $HOME so the users .Xauthority file
		 * can be located. This is especially needed for a user
		 * user MIT Magic Cookie authentication security
		 */
		(void) sprintf(home_dir, "HOME=%s", pw->pw_dir);
		(void) putenv(home_dir);

		/*
		 * We need the X application to be able to connect to
		 * the user's display so we better run as if we are
		 * the user (effectively).
		 * Don't want x program doing anything nasty.
		 *
		 * Note - have to set gid stuff first as effective uid
		 *	  must belong to root for this to work correctly.
		 */
		(void) setgid(pw->pw_gid);
		(void) setegid(pw->pw_gid);
		(void) setuid(pw->pw_uid);
		(void) seteuid(pw->pw_uid);

#ifdef DEBUG
		(void) fprintf(stderr,
		    "DEBUG: \"%s\" being execl'ed with protocol = \"%s\"\n",
		    OW_WINSYSCK_PATH, OW_WINSYSCK_PROTOCOL);
#endif

		(void) execl(OW_WINSYSCK_PATH, OW_WINSYSCK,
		    OW_WINSYSCK_PROTOCOL, NULL);

		(void) fprintf(stderr,
		    gettext("error: exec of \"%s\" failed (errno = %d)\n"),
		    OW_WINSYSCK_PATH, errno);
		exit(-1);

	}

	/* the parent -- wait for the child */
	if (waitpid(pid, &exit_code, 0) == pid) {
		if (WIFEXITED(exit_code)) {
			if (WEXITSTATUS(exit_code) == 0) {
				ret_val = true;
			}
		}
	}

dun:
	/* all done */
#ifdef	DEBUG
	(void) fprintf(stderr, "DEBUG: winsysck() returning %s\n",
	    ret_val ? "true" : "false");
#endif
	return (ret_val);
}
