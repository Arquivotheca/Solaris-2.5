/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)action_filemgr.c	1.13	94/11/22 SMI"

/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

/*
 * action_filemgr -- filemgr interface routines for rmmount
 *
 * This shared lib allows rmmount to communicate with filemgr.
 * This is done by communicating over a named pipe (see defines for
 * REM_DIR and NOTIFY_NAME).
 *
 * For insertion, a file is placed in REM_DIR named after the symbolic
 * name for the media (e.g. "cdrom0").  This file contains the mount
 * point of the media and the device name where it's located.  We then
 * send a "signal" over the named pipe (NOTIFY_NAME), which instruct
 * filemgr to look for new files in REM_DIR.
 *
 * For CD-ROMs we only notify filemgr for media that are mounted (i.e. *not*
 * for data-only CDs such as music CDs).  For floppies we notify filemgr
 * even if the floppy isn't mounted, since it'll probably want to
 * format it.
 *
 * The following environment variables must be present:
 *
 *	VOLUME_MEDIATYPE	media type (e.g. "cdrom" or "floppy")
 *	VOLUME_ACTION		action to take (e.g. "insert", "eject")
 *	VOLUME_SYMDEF		symbolic name (e.g. "cdrom0", "floppy1")
 *	VOLUME_NAME		volume name (e.g. "unnamed_cdrom")
 */


#include	<sys/types.h>
#include	<sys/stat.h>
#include	<sys/dkio.h>
#include	<sys/cdio.h>
#include	<sys/vtoc.h>
#include	<sys/param.h>
#include	<rpc/types.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<unistd.h>
#include	<fcntl.h>
#include	<string.h>
#include	<dirent.h>
#include	<rmmount.h>
#include	<signal.h>

/*
 * Tell file manager about the new media.
 */


/* for debug messages -- from rmmount */
extern char	*prog_name;
extern pid_t	prog_pid;


#define	REM_DIR		"/tmp/.removable"	/* dir where filemgr looks */
#define	NOTIFY_NAME	"notify"		/* named pipe to talk over */


static void	insert_media(struct action_arg **, char *);
static void	eject_media(struct action_arg **, char *);
static int	notify_clients(void);

extern void	dprintf(const char *, ...);
extern int	makepath(char *, mode_t);


/*ARGSUSED*/
int
action(struct action_arg **aa, int argc, char **argv)
{
	char		*media_type = getenv("VOLUME_MEDIATYPE");
	char		*atype = getenv("VOLUME_ACTION");
	int		rval;


	dprintf("action_filemgr: media type '%s'\n", media_type);

	if (strcmp(atype, "insert") == 0) {
		insert_media(aa, media_type);
	} else if (strcmp(atype, "eject") == 0) {
		eject_media(aa, media_type);
	}

	rval = notify_clients();

	/*
	 * if it's eject, always return false because we want
	 * the other actions to happen.
	 */
	if (strcmp(atype, "eject") == 0) {
		return (FALSE);
	}

	return (rval);
}


static void
insert_media(struct action_arg **aa, char *media_type)
{
	char		namebuf[MAXNAMELEN];
	char		*symdev = getenv("VOLUME_SYMDEV");
	FILE		*fp;
	char		*rdev = NULL;
	char		*mountp = NULL;
	char		*s;
	int		ai;



	/* scan all supplied pieces, stopping at the first mounted one */
	for (ai = 0; aa[ai]->aa_path; ai++) {

		if (aa[ai]->aa_mountpoint == NULL) {
			continue;	/* not mounted -- keep looking */
		}

		/* found a mounted piece */

		/* save a copy of the mount directory name */
		mountp = strdup(aa[ai]->aa_mountpoint);

		/* save the raw device name (if any) */
		if (aa[ai]->aa_rawpath) {
			rdev = aa[ai]->aa_rawpath;
		} else {
			rdev = "none";
		}

		/*
		 * This gets rid of the partition name (if any).
		 * We do this so that filemgr is positioned
		 * above the partitions.
		 */
		if (aa[ai]->aa_partname != NULL) {
			if ((s = strrchr(mountp, '/')) != NULL) {
				*s = '\0';
			}
		}
		break;
	}

	/* if no mount point found */
	if (mountp == NULL) {

		/* skip telling filemgr about unmounted CD-ROMs */
		if (strcmp(media_type, "cdrom") == 0) {
			return;			/* all done */
		}

		/* use the volume name as the "mount point" entry */
		mountp = strdup(getenv("VOLUME_NAME"));

		/* save the raw device name (if any) */
		if (aa[0]->aa_rawpath) {
			rdev = aa[0]->aa_rawpath;
		} else {
			rdev = "none";
		}
	}

	/* time to notify filemgr of new media */

	(void) makepath(REM_DIR, 0777);	/* filemgr needs to creat/write here */

	/* create the file that filemgr will examine */
	(void) sprintf(namebuf, "%s/%s", REM_DIR, symdev);
	if ((fp = fopen(namebuf, "w")) == NULL) {
		dprintf("action_filemgr: cannot write %s; %m\n", namebuf);
	} else {
		(void) fprintf(fp, "%s %s", mountp, rdev);
		(void) fclose(fp);
	}
	free(mountp);
}


/*
 * Remove the file containing the relevant mount info.
 *
 * NOTE: the action_arg array and the media_type arg are passed in,
 * even though not used, for possible future use (and symetry with
 * insert_media() (&^)).
 */
/*ARGSUSED*/
static void
eject_media(struct action_arg **aa, char *media_type)
{
	char	*symdev = getenv("VOLUME_SYMDEV");
	char	namebuf[MAXNAMELEN];


	(void) sprintf(namebuf, "%s/%s", REM_DIR, symdev);
	if (unlink(namebuf) < 0) {
		dprintf("action_filemgr: unlink %s; %m\n", namebuf);
	}
}


/*
 * Notify interested parties of change in the state.  Interested
 * parties are ones that put a "notify" named pipe in the
 * directory.  We'll open it up and write a character down it.
 *
 * Return TRUE or FALSE
 */
static bool_t
notify_clients()
{
	DIR		*dirp;
	struct dirent	*dp;
	size_t		len;
	int		fd;
	char		c = 'a';	/* character to write */
	char		namebuf[MAXPATHLEN];
	struct stat	sb;
	int		rval = FALSE;
	void		(*osig)();

	if ((dirp = opendir(REM_DIR)) == NULL) {
		dprintf("%s(%d): opendir failed on %s; %m\n",
		    prog_name, prog_pid, REM_DIR);
		return (FALSE);
	}

	osig = signal(SIGPIPE, SIG_IGN);

	len = strlen(NOTIFY_NAME);

	/*
	 * Read through the directory looking for names that start
	 * with "notify".  If we find one, open it and write a
	 * character to it.  If we get an error when we open it,
	 * we assume that the process on the other end of the named
	 * pipe has gone away, so we get rid of the file.
	 */
	while (dp = readdir(dirp)) {
		if (strncmp(dp->d_name, NOTIFY_NAME, len) != 0)
			continue;

		(void) sprintf(namebuf, "%s/%s", REM_DIR, dp->d_name);

		if (stat(namebuf, &sb) < 0) {
			dprintf("%s(%d) stat failed for %s; %m\n",
			    prog_name, prog_pid, namebuf);
			continue;
		}

		/* make sure it's a named pipe */
		if ((sb.st_mode & S_IFMT) != S_IFIFO) {
			dprintf("%s(%d) %s is not a fifo\n",
			    prog_name, prog_pid, namebuf);
			continue;
		}

		if ((fd = open(namebuf, O_WRONLY|O_NDELAY)) < 0) {
			dprintf("%s(%d) open failed for %s; %m\n",
			    prog_name, prog_pid, namebuf);

			/*
			 * If we couldn't open the file, assume that
			 * the process on the other end has died.
			 */
			if (unlink(namebuf) < 0) {
				dprintf("%s(%d) unlink failed for %s; %m\n",
				    prog_name, prog_pid, namebuf);
			}
			continue;
		}
		if (write(fd, &c, 1) < 0) {
			dprintf("%s(%d) write failed for %s; %m\n",
			    prog_name, prog_pid, namebuf);
			close(fd);
			continue;
		}
		close(fd);

		rval = TRUE;	/* we found something */
	}
	closedir(dirp);

	signal(SIGPIPE, osig);

	return (rval);
}
