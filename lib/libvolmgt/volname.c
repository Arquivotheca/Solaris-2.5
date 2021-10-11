/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

/*LINTLIBRARY*/

#pragma ident	"@(#)volname.c	1.19	95/08/14 SMI"

#include	<stdio.h>
#include	<string.h>
#include	<dirent.h>
#include	<fcntl.h>
#include	<string.h>
#include	<errno.h>
#include	<libintl.h>
#include	<limits.h>
#include	<unistd.h>
#include	<stdlib.h>
#include	<volmgt.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<sys/dkio.h>
#include	<sys/param.h>
#include	<sys/wait.h>
#include	<sys/mnttab.h>
#include	<sys/vol.h>

#include	"volmgt_private.h"

#define	ALIAS_DIR	"dev/aliases"
#define	HACKNAME_MAX	5

/* a shortcut for checkinf for absolute pathnames */
#define	IS_ABS_PATH(p)	(*(p) == '/')


/*
 * arc approved interface (pending)
 *	- can not be modified without approval from an arc
 *
 * committment level:
 *	uncommitted
 *
 * description:
 *	media_findname: try to come up with the character device when
 *	provided with a starting point.  This interface provides the
 *	application programmer to provide "user friendly" names and
 *	easily determine the "/vol" name.
 *
 * arguments:
 *	start - a string describing a device.  This string can be:
 *		- a full path name to a device (insures it's a
 *		  character device by using getfullrawname()).
 *		- a full path name to a volume management media name
 *		  with partitions (will return the lowest numbered
 *		  raw partition.
 *		- the name of a piece of media (e.g. "fred").
 *		- a symbolic device name (e.g. floppy0, cdrom0, etc)
 *		- a name like "floppy" or "cdrom".  Will pick the lowest
 *		  numbered device with media in it.
 *
 * return value(s):
 *	A pointer to a string that contains the character device
 *	most appropriate to the "start" argument.
 *
 *	NULL indicates that we were unable to find media based on "start".
 *
 *	The string must be free(3)'d.
 *
 * preconditions:
 *	none.
 */
char *
media_findname(char *start)
{
	static char 	*media_findname_work(char *);
	char		*s;


	/*
	 * This is just a wrapper to implement the volmgt_check nastyness.
	 */
#ifdef	DEBUG
	(void) fprintf(stderr, "DEBUG: media_findname(%s): entering\n",
	    start);
#endif

	s = media_findname_work(start);
	/*
	 * If we don't get positive results, we kick volume management
	 * to ask it to look in the floppy drive.
	 */
	if (s == NULL) {
#ifdef	DEBUG
		(void) fprintf(stderr,
		"DEBUG: media_findname: calling volcheck and trying again\n");
#endif
		(void) volmgt_check(NULL);
		s = media_findname_work(start);
	}
#ifdef	DEBUG
	(void) fprintf(stderr, "DEBUG: media_findname: returning \"%s\"\n",
	    s ? s : "<null ptr>");
#endif
	return (s);
}


/*
 * Return a raw name, given a starting point.
 */
static char *
media_findname_work(char *start)
{
	extern char		*getfullrawname(char *);
	static void		volmgt_deref_link(char *, char *, char *);
	char			namebuf[MAXNAMELEN+1]; /* XXX: big enough? */
	char			*rv;
	char			*s;
	char			linkbuf[MAXNAMELEN+1];
	char			*nameptr;
	struct stat		sb;
	int			n;
	int			i;
	static const char	*vold_root = NULL;
	static char		vold_alias_dir[MAXPATHLEN+1];
	char			*res = NULL;



#ifdef	DEBUG
	(void) fprintf(stderr, "DEBUG: media_findname_work(%s): entering\n",
	    start);
#endif

	if (vold_root == NULL) {
		vold_root = volmgt_root();
		(void) sprintf(vold_alias_dir, "%s/%s", vold_root, ALIAS_DIR);
	}

	/*
	 * if this is an absolute path name then
	 *  if it's a symlink deref it
	 *  if it's a raw device then we're done
	 *  else if it's a directory then look for a dev under it
	 */
	if (IS_ABS_PATH(start)) {

		/* try to get data on name passed in */
		if (lstat(start, &sb) < 0) {
#ifdef	DEBUG
			(void) fprintf(stderr,
	"DEBUG: media_findname_work: lstat failed on \"%s\" (errno %d)\n",
			    start, errno);
#endif
			goto dun;
		}

		/*
		 * if is this a link to something else (e.g. ".../floppy0")
		 * and it's in the volmgt namespace, then deref it
		 */
		if (S_ISLNK(sb.st_mode) && (strncmp(start, vold_alias_dir,
		    strlen(vold_alias_dir)) == 0)) {

			/* it's a symlink */
			if ((n = readlink(start, linkbuf, MAXNAMELEN)) <= 0) {
				/* we can't read the link */
#ifdef	DEBUG
				(void) fprintf(stderr,
	"DEBUG: media_findname_work: readlink(\"%s\") failed (errno %d)\n",
				    start, errno);
#endif
				goto dun;
			}
			linkbuf[n] = NULLC;

			/* dereference the link */
			volmgt_deref_link(namebuf, start, linkbuf);

			/* stat where "start" pointed at */
			if (stat(namebuf, &sb) < 0) {
#ifdef	DEBUG
				(void) fprintf(stderr,
	"DEBUG: media_findname_work: stat failed on \"%s\" (errno %d)\n",
				    namebuf, errno);
#endif
				goto dun;
			}
			nameptr = namebuf;

		} else {
			nameptr = start;
		}

		/* do we already have a char-spcl device ?? */
		if (S_ISCHR(sb.st_mode)) {
			res = strdup(nameptr);
			goto dun;
		}

		/* not a char-spcl device -- is it a dir ?? */
		if (S_ISDIR(sb.st_mode)) {
			/* open the dir and find first char-spcl device */
			if ((s = getrawpart0(nameptr)) != NULL) {
				res = s;
				goto dun;
			}
		}

		/* try to get the char-spcl name if this is a blk-spcl */
		if ((rv = getfullrawname(nameptr)) == NULL) {
#ifdef	DEBUG
			(void) fprintf(stderr,
		"DEBUG: media_findname_work: getfullrawname(%s) failed\n",
			    nameptr);
#endif
			goto dun;
		}

		/* stat the fullrawname device (to see if it's char-spcl) */
		if (stat(rv, &sb) < 0) {
#ifdef	DEBUG
			(void) fprintf(stderr,
		"DEBUG: media_findname_work: can't stat \"%s\" (errno %d)\n",
			    rv, errno);
#endif
			goto dun;
		}

		/* have we found the char-spcl device ?? */
		if (S_ISCHR(sb.st_mode)) {
			res = rv;		/* already malloc'ed */
			goto dun;
		}

		/* fullrawname not a char-spcl device -- is it a dir ?? */
		if (S_ISDIR(sb.st_mode)) {
			/* open dir and find first char-spcl device */
			if ((s = getrawpart0(nameptr)) != NULL) {
				res = s;
				goto dun;
			}
		}

		/* having a full pathname didn't help us */
		goto dun;	/* give up -- pathnamename not found */
	}

	/*
	 * we're going to try just a regular volume name now
	 */
	(void) sprintf(namebuf, "%s/rdsk/%s", vold_root, start);

	if (stat(namebuf, &sb) == 0) {

		/* is it a char-spcl device */
		if (S_ISCHR(sb.st_mode)) {
			res = strdup(namebuf);
			goto dun;
		}

		/* not a char-spcl device -- is it a dir ?? */
		if (S_ISDIR(sb.st_mode)) {
			/* open dir and find first char-spcl device */
			if ((s = getrawpart0(namebuf)) != NULL) {
				res = s;
				goto dun;
			}
		}
	}

	/*
	 * Ok, now we check to see if it's an alias.
	 * Note here that in the case of an alias, we prefer
	 * to return what the alias (symbolic link) points
	 * at, rather than the symbolic link.  Makes for
	 * nicer printouts and such.
	 */
	(void) sprintf(namebuf, "%s/%s", vold_alias_dir, start);

	if (stat(namebuf, &sb) == 0) {

		/* is this a char-spcl device ?? */
		if (S_ISCHR(sb.st_mode)) {
			/* it's probably a link, so ... */
			if ((n = readlink(namebuf,
			    linkbuf, MAXNAMELEN)) <= 0) {
				/* not a link */
				res = strdup(namebuf);
			} else {
				/* it was a link */
				linkbuf[n] = NULLC;
				res = strdup(linkbuf);
			}
			goto dun;
		}

		/* not a char-spcl device -- is it a dir ?? */
		if (S_ISDIR(sb.st_mode)) {
			/* it's probably a link, so ... */
			if ((n = readlink(namebuf,
			    linkbuf, MAXNAMELEN)) <= 0) {
				/* open dir, finding first char-spcl dev */
				s = getrawpart0(namebuf);
			} else {
				/* it was a link */
				linkbuf[n] = NULLC;
				/* open dir, finding first char-spcl dev */
				s = getrawpart0(linkbuf);
			}
			if (s != NULL) {
				res = s;
				goto dun;
			}
		}
	}


	/*
	 * Ok, well maybe that's not it.  Let's try the
	 * hackname alias.
	 */

	/*
	 * This creates the "hack" name.  The model
	 * is that xx# has the alias xx.  So, cdrom#
	 * and floppy# (the most frequent case) can
	 * be referred to as cdrom and floppy.
	 * We poke at what we consider to be a reasonable number of
	 * devices (currently 5) before giving up.
	 */

	for (i = 0; i < HACKNAME_MAX; i++) {

		(void) sprintf(namebuf, "%s/%s%d", vold_alias_dir, start, i);

		if (stat(namebuf, &sb) == 0) {

			/* is it a char-spcl device ?? */
			if (S_ISCHR(sb.st_mode)) {
				/* it's probably a link, so... */
				if ((n = readlink(namebuf,
				    linkbuf, MAXNAMELEN)) <= 0) {
					/* it wasn't a link */
					res = strdup(namebuf);
				} else {
					/* it was a link */
					linkbuf[n] = NULLC;
					res = strdup(linkbuf);
				}
				goto dun;
			}

			/* not a char-spcl device -- is it a dir ?? */
			if (S_ISDIR(sb.st_mode)) {
				/* it's probably a link, so ... */
				if ((n = readlink(namebuf,
				    linkbuf, MAXNAMELEN)) <= 0) {
					/* get fist char-spcl dev in dir */
					s = getrawpart0(namebuf);
				} else {
					/* it was a link */
					linkbuf[n] = NULLC;
					/* get fist char-spcl dev in dir */
					s = getrawpart0(linkbuf);
				}
				if (s != NULL) {
					res = s;
					goto dun;
				}
			}
		}
	}

#ifdef	DEBUG
	(void) fprintf(stderr,
	    "DEBUG: media_findname_work: %s didn't match any test!\n",
	    start);
#endif

dun:

#ifdef	DEBUG
	(void) fprintf(stderr,
	    "DEBUG: media_findname_work: returning \"%s\"\n",
	    res ? res : "<null ptr>");

#endif
	return (res);
}


/*
 * deref the link (in link_buf) read from path_buf into res_buf
 *
 * if there's any problem then just return the contents of the link buffer
 */
static void
volmgt_deref_link(char *res_buf, char *path_buf, char *link_buf)
{
	static char	*volmgt_dirname(char *);
	char		buf[MAXPATHLEN+1];
	char		*path_dirname;


	if (IS_ABS_PATH(link_buf)) {

		/* degenerate case -- link is okay the way it is */
		(void) strcpy(res_buf, link_buf);

	} else {

		/* link pathname is relative */

		/* get a writable copy of the orig path */
		(void) strcpy(buf, path_buf);

		/* get the dir from the orig path */
		if ((path_dirname = volmgt_dirname(buf)) == NULL) {

			/* oh oh -- just use the link contents */
			(void) strcpy(res_buf, link_buf);

		} else {

			/* concat the orig dir with the link path */
			(void) sprintf(res_buf, "%s/%s", path_dirname,
			    link_buf);

		}
	}
}


/*
 * return the dirname part of a path (i.e. all but last component)
 *
 * NOTE: may destuctively change "path" (i.e. it may write a null over
 *	the last slash in the path to convert it into a dirname)
 */
static char *
volmgt_dirname(char *path)
{
	char	*cp;


	/* find the last seperator in the path */
	if ((cp = strrchr(path, '/')) == NULL) {
		/* must be just a local name -- use the local dir */
		return (".");
	}

	/* replace the last slash with a null */
	*cp = NULLC;

	/* return all but the last component */
	return (path);
}


/*
 * This function runs through the list of "old" aliases to
 * see if someone is calling a device by an old name before
 * the glory of volume management.
 */

struct alias {
	char	*alias;
	char	*name;
};

static struct alias volmgt_aliases[] = {
	{ "fd", "floppy0" },
	{ "fd0", "floppy0" },
	{ "fd1", "floppy1" },
	{ "diskette", "floppy0" },
	{ "diskette0", "floppy0" },
	{ "diskette1", "floppy1" },
	{ "rdiskette", "floppy0" },
	{ "rdiskette0", "floppy0" },
	{ "rdiskette1", "floppy1" },
	{ "cd", "cdrom0" },
	{ "cd0", "cdrom0" },
	{ "cd1", "cdrom1" },
	{ "sr", "cdrom0" },
	{ "sr0", "cdrom0" },
	{ "/dev/sr0", "cdrom0" },
	{ "/dev/rsr0", "cdrom0" },
	{ "", ""}
};


/*
 * NOTE: the cdrom path /dev/rdsk/c0t6d0s2 is hardcoded here (;&(
 *
 *	This is bad for two reasons:
 *		1. the CDROM can be (and often is on Intel) in a different
 *		   locations (e.g. target 5, or 1, or even on an IDE drive!)
 *		2. The default slice (now 2, was 0) is hard-coded in  -- this
 *		   would be easier to fix, but will wait for a more general
 *		   fix (i.e. we should either be able to query a device to
 *		   find it's default slice, or always be able to query any
 *		   slice -- like the Sparc SSS!)
 */
static struct alias device_aliases[] = {
	{ "fd", "/dev/rdiskette" },
	{ "fd0", "/dev/rdiskette" },
	{ "fd1", "/dev/rdiskette1" },
	{ "diskette", "/dev/rdiskette" },
	{ "diskette0", "/dev/rdiskette0" },
	{ "diskette1", "/dev/rdiskette1" },
	{ "rdiskette", "/dev/rdiskette" },
	{ "rdiskette0", "/dev/rdiskette0" },
	{ "rdiskette1", "/dev/rdiskette1" },
	{ "floppy", "/dev/rdiskette" },
	{ "floppy0", "/dev/rdiskette0" },
	{ "floppy1", "/dev/rdiskette1" },
	{ "cd", "/dev/rdsk/c0t6d0s2" },
	{ "cdrom", "/dev/rdsk/c0t6d0s2" },
	{ "cd0", "/dev/rdsk/c0t6d0s2" },
	{ "sr", "/dev/rdsk/c0t6d0s2" },
	{ "sr0", "/dev/rdsk/c0t6d0s2" },
	{ "/dev/sr0", "/dev/rdsk/c0t6d0s2" },
	{ "/dev/rsr0", "/dev/rdsk/c0t6d0s2" },
	{ "c0t6d0s2", "/dev/rdsk/c0t6d0s2" },
	{ "", ""}
};


/*
 * This is an ON Consolidation Private interface.
 */
char *
media_oldaliases(char *start)
{
	struct alias	*s, *ns;
	char		*p;
	char		*res;



#ifdef	DEBUG
	(void) fprintf(stderr, "DEBUG: media_oldaliases(%s): entering\n",
	    start);
#endif

	for (s = device_aliases; *s->alias != NULLC; s++) {
		if (strcmp(start, s->alias) == 0) {
			break;
		}
	}

	/* we don't recognize that alias at all */
	if (*s->alias == NULLC) {
#ifdef	DEBUG
		(void) fprintf(stderr, "DEBUG: media_oldaliases: failed\n");
#endif
		res = NULL;
		goto dun;
	}

	/* if volume management isn't running at all, give him back the name */
	if (!volmgt_running()) {
#ifdef	DEBUG
		(void) fprintf(stderr,
		    "DEBUG: media_oldaliases: no vold!\n");
#endif
		res = strdup(s->name);
		goto dun;
	}
	/*
	 * If volume management is managing that device, look up the
	 * volume management name.
	 */
	if (volmgt_inuse(s->name)) {
		for (s = volmgt_aliases; *s->alias != NULLC; s++) {
			if (strcmp(start, s->alias) == 0) {
				res = strdup(s->name);
				goto dun;
			}
		}
#ifdef	DEBUG
		(void) fprintf(stderr, "DEBUG: media_oldaliases: failed\n");
#endif
		res = NULL;
		goto dun;
	}

	/*
	 * If volume management isn't managing the device, it's possible
	 * that he's given us an alias that we should recognize, but the
	 * default name is wrong.  For example a user might have his
	 * cdrom on controller 1, being managed by volume management,
	 * but we would think it isn't because volmgt_inuse just told
	 * us that c0t6d0s2 isn't being managed.  So, before we return
	 * the /dev name, we'll test the alias out using media_findname.
	 * If media_findname can't make sense out of the alias, it probably
	 * means that we really, really aren't managing the device and
	 * should just return the /dev name.  Whew.  Isn't this grody?
	 */

	for (ns = volmgt_aliases; *ns->alias != NULLC; ns++) {
		if (strcmp(start, ns->alias) == 0) {
			if ((p = media_findname_work(ns->name))) {
				res = p;
				goto dun;
			} else {
				break;
			}
		}
	}

	res = strdup(s->name);
dun:
#ifdef	DEBUG
	(void) fprintf(stderr, "DEBUG: media_oldaliases: returning %s\n",
		res ? res : "<null ptr>");
#endif
	return (res);
}


/*
 * This is an ON Consolidation Private interface.
 *
 * Print out the aliases available to the program user.  Changes
 * depending in whether volume management is running.
 */
void
media_printaliases(void)
{
	struct alias		*s;
	DIR			*dirp;
	struct dirent		*dp;
	char			namebuf[MAXNAMELEN]; /* XXX: big enough? */
	char			*p;
	static const char	*vold_root = NULL;



	if (vold_root == NULL) {
		vold_root = volmgt_root();
	}

	if (!volmgt_running()) {
		/* no volume management */
		for (s = device_aliases; *s->alias != NULLC; s++) {
			(void) printf("\t%s -> %s\n", s->alias, s->name);
		}
		return;
	}

	for (s = volmgt_aliases; *s->alias != NULLC; s++) {
		(void) printf("\t%s -> %s\n", s->alias, s->name);
	}

	(void) sprintf(namebuf, "%s/%s", vold_root, ALIAS_DIR);
	if ((dirp = opendir(namebuf)) == NULL) {
		return;
	}
	while (dp = readdir(dirp)) {
		if (strcmp(dp->d_name, ".") == 0) {
			continue;
		}
		if (strcmp(dp->d_name, "..") == 0) {
			continue;
		}
		if ((p = media_findname(dp->d_name)) != NULL) {
			(void) printf("\t%s -> %s\n", dp->d_name, p);
		}
	}
	(void) closedir(dirp);
}
