/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#ident	"@(#)fulldevnm.c	1.2	94/06/28 SMI"


#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/vfstab.h>

/*
 * Globals:
 *	getfullrawname - returns a fully-qualified raw device name
 *	getfullblkname - returns a fully-qualified block device name
 *
 * These two routines take a device pathname and return corresponding
 * the raw or block device name.
 *
 * First the device name is fully qualified:
 * 	If the device name does not start with a '/' or starts with
 *	'./' then the current working directory is added to the beginning
 *	of the pathname.
 *
 *	If the device name starts with a '../' then all but the last
 *	sub-directory of the current working directory is added to the
 *	the beginning of the pathname.
 *
 * Second if the fully-qualified device name given is the raw/block
 * device that is being asked for then the fully-qualified device name is
 * returned.
 *
 * Third if an entry is found in /etc/vfstab which matches the given name
 * then the corresponding raw/block device is returned.  This allows
 * non-standard names to be converted (.i.e., block device "/dev/joe" can
 * be converted to raw device "/dev/fred", via this mechanism).
 *
 * Last standard names are converted.  Standard names are those
 * with a '/dsk/' for block or '/rdsk/' for raw sub-directory components
 * in the device name. Or, the filename component has an 'r' for raw or
 * no 'r' for block (e.g., rsd0a <=> sd0a).
 *
 * Caveat:
 * It is assumed that the block and raw devices have the
 * same device number, and this is used to verify the conversion
 * happened corretly.  If this happens not to be true, due to mapping
 * of minor numbers or sometheing, then entries can be put in the
 * the '/etc/vfstab' file to over-ride this checking.
 *
 *
 * Return Values:
 * 	raw/block device name	- (depending on which routine is used)
 *	null string		- When the conversion failed
 *	null pointer		- malloc problems
 *
 * It is up to the user of these routines to free the memory, of
 * the device name or null string returned by these library routines,
 * when appropriate by the application.
 */
#define	GET_BLK	0
#define	GET_RAW	1

/*
 * Externals referenced
 *	malloc()	Allocate a chunk of main memory
 *	free()		Free malloc()ed memory
 */
extern void	*malloc();
extern void	free();


/*
 * getfullname() - Builds a fully qualified pathname.
 *		   This handles . and .. as well.
 *		   NOTE: This is different from realpath(3C) because
 *			 it does not follow links.
 */
static char *
getfullname(char *path)
{
	char	cwd[MAXPATHLEN];
	char	*c;
	char	*wa;
	u_int	len;

	if (*path == '/')
		return (strdup(path));

	if (getcwd(cwd, sizeof (cwd)) == NULL)
		return ("");

	/* handle . and .. */
	if (strncmp(path, "./", 2) == 0)
		/* strip the ./ from the given path */
		path += 2;
	else if (strncmp(path, "../", 3) == 0) {
		/* strip the last directory component from cwd */
		c = strrchr(cwd, '/');
		*c = '\0';

		/* strip the ../ from the given path */
		path += 3;
	}

	/*
	 * Adding 2 takes care of slash and null terminator.
	 */
	len = strlen(cwd) + strlen(path) + 2;
	if ((wa = (char *)malloc(len)) == NULL)
		return (NULL);

	(void) strcpy(wa, cwd);
	(void) strcat(wa, "/");
	return (strcat(wa, path));
}

static char *
getvfsspecial (char *path, int raw_special)
{
	FILE		*fp;
	struct vfstab	vp;
	struct vfstab	ref_vp;

	if ((fp = fopen("/etc/vfstab", "r")) == NULL)
		return (NULL);

	(void) memset(&ref_vp, 0, sizeof (struct vfstab));

	if (raw_special)
		ref_vp.vfs_special = path;
	else
		ref_vp.vfs_fsckdev = path;

	if (getvfsany(fp, &vp, &ref_vp)) {
		(void) fclose (fp);
		return (NULL);
	}

	(void) fclose(fp);

	if (raw_special)
		return (vp.vfs_fsckdev);

	return (vp.vfs_special);
}
/*
 * change the device name to a block device name
 */
char *
getfullblkname(char *cp)
{
	struct stat	buf;
	char		*dp;
	char		*new_path;
	dev_t		raw_dev;

	if (cp == (char *) 0)
		return (strdup(""));

	/*
	 * Create a fully qualified name.
	 */
	if ((cp = getfullname(cp)) == NULL)
		return (NULL);

	if (*cp == '\0')
		return (strdup(""));

	if (stat(cp, &buf)) {
		free(cp);
		return (strdup(""));
	}

	if (S_ISBLK(buf.st_mode))
		return (cp);

	if (!S_ISCHR(buf.st_mode)) {
		free(cp);
		return (strdup(""));
	}

	if (dp = getvfsspecial(cp, GET_BLK)) {
		free(cp);
		return (strdup(dp));
	}

	raw_dev = buf.st_rdev;

	/*
	 * We have a raw device name, go find the block name.
	 */
	if ((dp = strstr(cp, "/rdsk/")) != NULL ||
	    (dp = strrchr(cp, '/')) != NULL) {
		dp++;
		if ((new_path = (char *)malloc((u_int) strlen(cp))) == NULL) {
			free(cp);
			return (NULL);
		}
		(void) strncpy(new_path, cp, dp - cp);
	} else {
		/* this is not really possible */
		free(cp);
		return (strdup(""));
	}

	if (*dp != 'r') {
		free(cp);
		free(new_path);
		return (strdup(""));
	}

	/* fill in the rest of the unraw name */
	(void) strcpy(new_path + (dp - cp), dp + 1);
	free(cp);

	if (stat(new_path, &buf)) {
		free(new_path);
		return (strdup(""));
	}

	if (!S_ISBLK(buf.st_mode)) {
		free(new_path);
		return (strdup(""));
	}

	if (raw_dev != buf.st_rdev) {
		free(new_path);
		return (strdup(""));
	}

	/* block name was found, return it here */
	return (new_path);
}

/*
 * change the device name to a raw devname
 */
char *
getfullrawname(char *cp)
{
	struct stat	buf;
	char		*dp;
	char		*new_path;
	dev_t		blk_dev;

	if (cp == (char *) 0)
		return (strdup(""));

	/*
	 * Create a fully qualified name.
	 */
	if ((cp = getfullname(cp)) == NULL)
		return (NULL);

	if (*cp == '\0')
		return (strdup(""));

	if (stat(cp, &buf)) {
		free(cp);
		return (strdup(""));
	}

	if (S_ISCHR(buf.st_mode))
		return (cp);

	if (!S_ISBLK(buf.st_mode)) {
		free(cp);
		return (strdup(""));
	}

	if (dp = getvfsspecial(cp, GET_RAW)) {
		free(cp);
		return (strdup(dp));
	}

	blk_dev = buf.st_rdev;

	/*
	 * We have a block device name, go find the raw name.
	 */
	if ((dp = strstr(cp, "/dsk/")) != NULL ||
	    (dp = strrchr(cp, '/')) != NULL) {
		dp++;
		if ((new_path = (char *)malloc((u_int)strlen(cp)+2)) == NULL) {
			free(cp);
			return (NULL);
		}
		(void) strncpy(new_path, cp, dp - cp);
	} else {
		/* this is not really possible */
		free(cp);
		return (strdup(""));
	}

	/* fill in the rest of the raw name */
	new_path[dp - cp] = 'r';
	(void) strcpy(new_path + (dp - cp) + 1, dp);
	free(cp);

	if (stat(new_path, &buf)) {
		free(new_path);
		return (strdup(""));
	}

	if (!S_ISCHR(buf.st_mode)) {
		free(new_path);
		return (strdup(""));
	}

	if (blk_dev != buf.st_rdev) {
		free(new_path);
		return (strdup(""));
	}

	/* raw name was found, return it here */
	return (new_path);
}
