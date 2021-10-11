/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)volutil.c	1.13	94/12/09 SMI"

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
#include	<libgen.h>
#include	<volmgt.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<sys/param.h>
#include	<sys/wait.h>
#include	<sys/mnttab.h>
#include	<sys/vol.h>

#include	"volmgt_private.h"



/*
 * volctl_name -- return name of volctl device
 */
static const char *
volctl_name()
{
	static char	*dev_name = NULL;
	const char	dev_dir[] = "/dev";


	/* see if name hasn't already been set up */
	if (dev_name == NULL) {
		/* set up name */
		if ((dev_name = (char *)malloc(strlen(dev_dir) +
		    strlen(VOLCTLNAME) + 2)) != NULL) {
			(void) strcpy(dev_name, dev_dir);
			(void) strcat(dev_name, "/");
			(void) strcat(dev_name, VOLCTLNAME);
		}
	}

	return (dev_name);
}


/*
 * arc approved interface (pending)
 *	- can not be modified without approval from an arc
 *
 * committment level:
 *	uncommitted
 *
 * description:
 *	volmgt_running: check to see if volume management is running.
 *
 * arguments:
 *	none.
 *
 * return value(s):
 *	TRUE if volume management is running, FALSE if not.
 *
 * preconditions:
 *	none.
 */
int
volmgt_running()
{
	const char	*volctl_dev = volctl_name();
	int		res;


	res = volmgt_inuse((char *)volctl_dev);
#ifdef	DEBUG
	(void) fprintf(stderr, "volmgt_running: returning %s\n",
	    res ? "TRUE" : "FALSE");
#endif
	return (res);

}


/*
 * arc approved interface (pending)
 *	- can not be modified without approval from an arc
 *
 * committment level:
 *	uncommitted
 *
 * description:
 *	volmgt_inuse: check to see if volume management is currently
 *	managing a particular device.
 *
 * arguments:
 *	path - the name of the device in /dev.  For example,
 *	  "/dev/rdiskette".
 *
 * return value(s):
 *	TRUE if volume management is managing the device, FALSE if not.
 *
 * preconditions:
 *	none.
 */
int
volmgt_inuse(char *path)
{
	const char	*volctl_dev = volctl_name();
	struct stat	sb;
	int		fd = -1;
	int		ret_val;



#ifdef	DEBUG
	(void) fprintf(stderr, "volmgt_inuse(%s): entering\n",
	    path != NULL ? path : "<null string>");
#endif
	if (stat(path, &sb) < 0) {
		ret_val = FALSE;
		goto dun;
	}

	if ((fd = open(volctl_dev, O_RDWR)) < 0) {
#ifdef	DEBUG
		perror(volctl_dev);
#endif
		ret_val = FALSE;
		goto dun;
	}

	if (ioctl(fd, VOLIOCINUSE, sb.st_rdev) < 0) {
		ret_val = FALSE;
		goto dun;
	}
	ret_val = TRUE;
dun:
	if (fd >= 0) {
		(void) close(fd);
	}
#ifdef	DEBUG
	(void) fprintf(stderr, "volmgt_inuse: returning %s\n",
	    ret_val ? "TRUE" : "FALSE");
#endif
	return (ret_val);
}


/*
 * arc approved interface (pending)
 *	- can not be modified without approval from an arc
 *
 * committment level:
 *	uncommitted
 *
 * description:
 *	volmgt_check: have volume management look at its devices to check
 *	for media having arrived.  Since volume management can't
 *	automatically check all types of devices, this function is provided
 *	to allow applications to cause the check to happen automatically.
 *
 * arguments:
 *	path - the name of the device in /dev.  For example,
 *	  /dev/rdiskette.  If path is NULL, all "checkable" devices are
 *	  checked.
 *
 * return value(s):
 *	TRUE if media was found in the device, FALSE if not.
 *
 * preconditions:
 *	volume management must be running.
 */
int
volmgt_check(char *path)
{
	const char	*volctl_dev = volctl_name();
	struct stat	sb;
	int		fd = -1;
	int		ret_val;



#ifdef	DEBUG
	(void) fprintf(stderr, "volmgt_check(%s): entering\n",
	    path != NULL ? path : "<null string>");
#endif

	if (path) {
		if (stat(path, &sb) < 0) {
			ret_val = FALSE;
			goto dun;
		}
	}

	if ((fd = open(volctl_dev, O_RDWR)) < 0) {
#ifdef	DEBUG
		perror(volctl_dev);
#endif
		ret_val = FALSE;
		goto dun;
	}

	/* if "no device" specified, that means "all devices" */
	if (path == NULL) {
		sb.st_rdev = NODEV;
	}

	if (ioctl(fd, VOLIOCCHECK, sb.st_rdev) < 0) {
		ret_val = FALSE;
		goto dun;
	}
	ret_val = TRUE;
dun:
	if (fd >= 0) {
		(void) close(fd);
	}
#ifdef	DEBUG
	(void) fprintf(stderr, "volmgt_check: returning %s\n",
	    ret_val != NULL ? "TRUE" : "FALSE");
#endif
	return (ret_val);
}


/*
 * arc approved interface (pending)
 *	- can not be modified without approval from an arc
 *
 * committment level:
 *	uncommitted
 *
 * description:
 *	volmgt_ownspath: check to see if the given path is contained in
 *	the volume management name space.
 *
 * arguments:
 *	path - string containing the path.
 *
 * return value(s):
 *	TRUE if the path is owned by volume management, FALSE if not.
 *	Will return FALSE if volume management isn't running.
 *
 * preconditions:
 *	none.
 */
int
volmgt_ownspath(char *path)
{
	static const char	*vold_root = NULL;
	static uint		vold_root_len;
	int			ret_val;



	if (vold_root == NULL) {
		vold_root = volmgt_root();
		vold_root_len = strlen(vold_root);
	}

	if (strncmp(path, vold_root, vold_root_len) == 0) {
		ret_val = TRUE;
	} else {
		ret_val = FALSE;
	}
	return (ret_val);
}


/*
 * arc approved interface (pending)
 *	- can not be modified without approval from an arc
 *
 * committment level:
 *	uncommitted
 *
 * description:
 *	volmgt_root: return the root of where the volume management
 *	name space is mounted.
 *
 * arguments:
 *	none.
 *
 * return value(s):
 *	Returns a pointer to a static string containing the path to the
 *	volume management root (e.g. "/vol").
 *	Will return NULL if volume management isn't running.
 *
 * preconditions:
 *	none.
 */
const char *
volmgt_root()
{
	static char	vold_root[MAXPATHLEN] = "";
	const char	*volctl_dev = volctl_name();
	int		fd = -1;



	if (*vold_root != NULLC) {
		goto dun;
	}

	if ((fd = open(volctl_dev, O_RDWR)) < 0) {
#ifdef	DEBUG
		perror(volctl_dev);
#endif
		/* a guess is better than nothing? */
		(void) strcpy(vold_root, DEFAULT_ROOT);
		goto dun;
	}

	if (ioctl(fd, VOLIOCROOT, vold_root) < 0) {
#ifdef	DEBUG
		(void) fprintf(stderr,
		"volmgt_root: ioctl(VOLIOCROOT) on \"%s\" failed (errno %d)\n",
		    volctl_dev, errno);
#endif
		(void) strcpy(vold_root, DEFAULT_ROOT);
		goto dun;
	}

dun:
	if (fd >= 0) {
		(void) close(fd);
	}
	return ((const char *)vold_root);
}


/*
 * arc approved interface (pending)
 *	- can not be modified without approval from an arc
 *
 * committment level:
 *	uncommitted
 *
 * description:
 *	volmgt_symname: Returns the volume management symbolic name
 *	for a given device.  If an application wants to determine
 *	what the symbolic name (e.g. "floppy0") for the /dev/rdiskette
 *	device would be, this is the function to use.
 *
 * arguments:
 *	path - a string containing the /dev device name.  For example,
 *	"/dev/diskette" or "/dev/rdiskette".
 *
 * return value(s):
 *	pointer to a string containing the symbolic name.
 *
 *	NULL indicates that volume management isn't managing that device.
 *
 *	The string must be free(3)'d.
 *
 * preconditions:
 *	none.
 */
char *
volmgt_symname(char *path)
{
	const char		*volctl_dev = volctl_name();
	int			fd = -1;
	struct stat 		sb;
	struct vioc_symname	sn;
	char			*result = NULL;
	char			symbuf[VOL_SYMNAME_LEN+1] = "";



#ifdef	DEBUG
	(void) fprintf(stderr, "volmgt_symname(%s): entering\n", path);
#endif

	if (stat(path, &sb) != 0) {
#ifdef	DEBUG
		(void) fprintf(stderr,
		    "volmgt_symname error: can't stat \"%s\" (errno %d)\n",
		    path, errno);
#endif
		goto dun;
	}

	if ((fd = open(volctl_dev, O_RDWR)) < 0) {
#ifdef	DEBUG
		(void) fprintf(stderr,
		    "volmgt_symname error: can't open \"%s\" (errno %d)\n",
		    volctl_dev, errno);
#endif
		goto dun;
	}

	sn.sn_dev = sb.st_rdev;
	sn.sn_symname = symbuf;
	sn.sn_pathlen = VOL_SYMNAME_LEN;
	if (ioctl(fd, VOLIOCSYMNAME, &sn) == 0) {
		result = strdup(symbuf);
	}
#ifdef	DEBUG
	else {
		(void) fprintf(stderr,
		    "volmgt_symname: ioctl(VOLIOCSYMNAME) failed (errno %d)\n",
		    errno);
	}
#endif

dun:
	if (fd >= 0) {
		(void) close(fd);
	}

#ifdef	DEBUG
	(void) fprintf(stderr, "volmgt_symname: returning \"%s\"\n",
	    result != NULL ? result : "<null ptr>");
#endif

	return (result);
}


/*
 * arc approved interface (pending)
 *	- can not be modified without approval from an arc
 *
 * committment level:
 *	uncommitted
 *
 * description:
 *	volmgt_symdev: Returns the device given the volume management
 *	symbolic name. If an application wants to determine
 *	what the device associated with a particular symbolic name
 *	might be, this is the function to use.
 *
 * arguments:
 *	path - a string containing the symbolic device name.  For example,
 *	"cdrom0" or "floppy0".
 *
 * return value(s):
 *	pointer to a string containing the /dev name.
 *
 *	NULL indicates that volume management isn't managing that device.
 *
 *	The string must be free(3)'d.
 *
 * preconditions:
 *	none.
 */
char *
volmgt_symdev(char *symname)
{
	const char		*volctl_dev = volctl_name();
	int			fd = -1;
	struct vioc_symdev	sd;
	char			*result = NULL;
	char			devbuf[VOL_SYMDEV_LEN+1] = "";


#ifdef	DEBUG
	(void) fprintf(stderr, "volmgt_symdev(%s): entering\n", symname);
#endif

	if ((fd = open(volctl_dev, O_RDWR)) < 0) {
#ifdef	DEBUG
		(void) fprintf(stderr,
		    "volmgt_symdev error: can't open \"%s\" (errno %d)\n",
		    volctl_dev, errno);
#endif
		goto dun;
	}

	sd.sd_symname = symname;
	sd.sd_symdevname = devbuf;
	sd.sd_pathlen = VOL_SYMDEV_LEN;
	if (ioctl(fd, VOLIOCSYMDEV, &sd) == 0) {
		result = strdup(devbuf);
	}
#ifdef	DEBUG
	else {
		(void) fprintf(stderr,
		    "volmgt_symdev: VOLIOCSYMDEV ioctl failed (errno %d)\n",
		    errno);
	}
#endif

dun:
	if (fd >= 0) {
		(void) close(fd);
	}

#ifdef	DEBUG
	(void) fprintf(stderr, "volmgt_symdev: returning \"%s\"\n",
	    result != NULL ? result : "<null ptr>");
#endif

	return (result);
}


/*
 * This is an ON Consolidation Private interface.
 *
 * Is the specified path mounted?
 *
 * This function is really inadequate for ejection testing.  For example,
 * I could have /dev/fd0a mounted and eject /dev/fd0c, and it would be
 * ejected.  There needs to be some better way to make this check, although
 * short of looking up the mounted dev_t in the kernel mount table and
 * building in all kinds of knowledge into this function,  I'm not sure
 * how to do it.
 */
int
dev_mounted(char *path)
{
	static FILE 	*fp = NULL;
	struct mnttab	mnt;
	char		*bn = NULL;
	int		ret_val;


#ifdef	DEBUG
	(void) fprintf(stderr, "dev_mounted(%s): entering\n", path);
#endif

	if ((bn = (char *)volmgt_getfullblkname(path)) == NULL) {
#ifdef	DEBUG
		(void) fprintf(stderr,
		    "dev_mounted: volmgt_getfullblkname failed\n");
#endif
		ret_val = 0;
		goto dun;
	}

	if ((fp = fopen(MNTTAB, "r")) == NULL) {
		/* mtab is gone... let him go */
#ifdef	DEBUG
		perror(MNTTAB);
#endif
		ret_val = 0;
		goto dun;
	}

	while (getmntent(fp, &mnt) == 0) {
		if (strcmp(mnt.mnt_special, bn) == 0) {
			ret_val = 1;		/* entry found! */
			goto dun;
		}
	}
	ret_val = 0;

dun:
	if (bn != NULL) {
		free(bn);
	}
	if (fp != NULL) {
		(void) fclose(fp);
	}
#ifdef	DEBUG
	(void) fprintf(stderr, "dev_mounted: returning %s\n",
		ret_val ? "TRUE" : "FALSE");
#endif
	return (ret_val);
}


static char *
vol_basename(char *path)
{
	char	*cp;


	/* check for the degenerate case */
	if (strcmp(path, "/") == 0) {
		return (path);
	}

	/* look for the last slash in the name */
	if ((cp = strrchr(path, '/')) == NULL) {
		/* no slash */
		return (path);
	}

	/* ensure something is after the slash */
	if (*++cp != NULLC) {
		return (cp);
	}

	/* a name that ends in slash -- back up until previous slash */
	while (cp != path) {
		if (*--cp == '/') {
			return (--cp);
		}
	}

	/* the only slash is the end of the name */
	return(path);
}


static int
get_media_info(char *path, char **mtypep, int *mnump)
{
	FILE		*fp = NULL;
	struct mnttab	mnt;
	int		ret_val = FALSE;
	int		mnt_entry_found = FALSE;



#ifdef	DEBUG
	(void) fprintf(stderr, "get_media_info(%s): entering\n", path);
#endif

	if ((fp = fopen(MNTTAB, "r")) == NULL) {
		/* mtab is gone... let him go */
#ifdef	DEBUG
		(void) fprintf(stderr, "error: can't open %s (errno %d)\n",
		    MNTTAB, errno);
#endif
		goto dun;
	}

	while (getmntent(fp, &mnt) == 0) {
		if (strcmp(mnt.mnt_special, path) == 0) {
			mnt_entry_found = TRUE;
			break;
		}
	}

	/* if we found the entry then disect it */
	if (mnt_entry_found) {
		char		*cp;
		char		*mtype;
		char		*mnt_dir;
		int		mtype_len;
		DIR		*dirp = NULL;
		struct dirent	*dp;
		char		*volname;


		/* get the first part of the mount point (e.g. "floppy") */
		cp = mnt.mnt_mountp;
		if (*cp++ != '/') {
			goto dun;
		}
		mtype = cp;
		if ((cp = strchr(mtype, '/')) == NULL) {
			goto dun;
		}
		*cp++ = NULLC;
		mnt_dir = mnt.mnt_mountp;	/* save dir path */

		/* get the volume name (e.g. "unnamed_floppy") */
		volname = cp;

		/* scan for the symlink that points to our volname */
		if ((dirp = opendir(mnt_dir)) == NULL) {
			goto dun;
		}
		mtype_len = strlen(mtype);
		while ((dp = readdir(dirp)) != NULL) {
			char		path[2 * (MAXNAMELEN+1)];
			char		linkbuf[MAXPATHLEN+4];
			int		lb_len;
			struct stat	sb;


			if (strncmp(dp->d_name, mtype, mtype_len) != 0) {
				continue;	/* not even close */
			}

			(void) sprintf(path, "%s/%s", mnt_dir,
			    dp->d_name);
			if (lstat(path, &sb) < 0) {
				continue;	/* what? */
			}
			if (!S_ISLNK(sb.st_mode)) {
				continue;	/* not our baby */
			}
			if ((lb_len = readlink(path, linkbuf,
			    sizeof (linkbuf))) < 0) {
				continue;
			}
			linkbuf[lb_len] = NULLC; /* null terminate */
			if ((cp = vol_basename(linkbuf)) == NULL) {
				continue;
			}
			/* now we have the name! */
			if (strcmp(cp, volname) == 0) {
				/* found it !! */
				if (sscanf(dp->d_name + mtype_len, "%d",
				    mnump) == 1) {
					*mtypep = strdup(mtype);
					ret_val = TRUE;
				}
				break;
			}
		}
		(void) closedir(dirp);
	}

dun:

	if (fp != NULL) {
		(void) fclose(fp);
	}
#ifdef	DEBUG
	if (ret_val) {
		(void) fprintf(stderr, "get_media_info: mtype=%s, mnum=%d\n",
		    *mtypep, *mnump);
	} else {
		(void) fprintf(stderr, "get_media_info: FAILED\n");
	}
#endif
	return (ret_val);
}


/*
 * This is an ON Consolidation Private interface.
 */

/*
 * Forks off rmmount and (in essence) returns the result
 *
 * a return value of 0 means failure, non-zero means success
 */
int
dev_unmount(char *path)
{
	pid_t		pid;			/* forked proc's pid */
	char		*bn;			/* block name */
	int		rval;			/* proc's return value */
	int		ret_val = FALSE;	/* what we return */
	int		use_rmm = FALSE;
	char		*vr;
	const char	*etc_umount = "/etc/umount";
	const char	*rmm = "/usr/sbin/rmmount";



#ifdef	DEBUG
	(void) fprintf(stderr, "dev_unmount(%s): entering\n", path);
#endif

	if ((bn = (char *)volmgt_getfullblkname(path)) == NULL) {
#ifdef	DEBUG
		(void) fprintf(stderr,
		    "dev_unmount: volmgt_getfullblkname failed\n");
#endif
		goto dun;
	}

	/* decide of we should use rmmount to unmount the media */
	if (volmgt_running()) {
		/* at least volmgt is running */
		vr = (char *)volmgt_root();
		if (strncmp(bn, vr, strlen(vr)) == 0) {
			/* the block path is rooted in /vol */
			use_rmm = TRUE;
		}
	}

	/* create a child to unmount the path */
	if ((pid = fork()) < 0) {
#ifdef	DEBUG
		(void) fprintf(stderr,
		    "error in dev_umount: fork failed (errno %d)\n",
		    errno);
#endif
		goto dun;
	}

	if (pid == 0) {
#ifndef	DEBUG
		int		fd;
#endif
		char		env_buf[MAXPATHLEN];
		char		*mtype;
		int		mnum;

#ifndef	DEBUG
		/* get rid of those nasty err messages */
		if ((fd = open("/dev/null", O_RDWR)) >= 0) {
			(void) dup2(fd, fileno(stdin));
			(void) dup2(fd, fileno(stdout));
			(void) dup2(fd, fileno(stderr));
		}
#endif

		if (use_rmm) {
			/* set up environment vars */
			(void) putenv("VOLUME_ACTION=eject");
			(void) sprintf(env_buf, "VOLUME_PATH=%s", bn);
			(void) putenv(strdup(env_buf));
			(void) sprintf(env_buf, "VOLUME_NAME=%s",
			    vol_basename(bn));
			(void) putenv(strdup(env_buf));
			if (get_media_info(bn, &mtype, &mnum)) {
				(void) sprintf(env_buf,
				    "VOLUME_MEDIATYPE=%s", mtype);
				(void) putenv(strdup(env_buf));
				(void) sprintf(env_buf, "VOLUME_SYMDEV=%s%d",
				    mtype, mnum);
				(void) putenv(strdup(env_buf));
				free(mtype);
			}
			(void) execl(rmm, rmm, NULL);
		} else {
			(void) execl(etc_umount, etc_umount, bn, NULL);
		}
#ifdef	DEBUG
		(void) fprintf(stderr,
		    "error in dev_umount: exec failed (errno %d)\n",
		    errno);
#endif
		exit(-1);
		/*NOTREACHED*/
	}

	/* wait for the umount command to exit */
	if (waitpid(pid, &rval, 0) == pid) {
		if (WIFEXITED(rval)) {
			if (WEXITSTATUS(rval) == 0) {
				ret_val = TRUE;	/* success */
			}
		}
	}

dun:
	if (bn != NULL) {
		free(bn);
	}

#ifdef	DEBUG
	(void) fprintf(stderr, "dev_unmount: returning %s\n",
	    ret_val ? "TRUE" : "FALSE");
#endif

	return (ret_val);
}
