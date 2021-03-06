/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#ifndef	_VOLMGT_H
#define	_VOLMGT_H

#pragma ident	"@(#)volmgt.h	1.4	94/09/27 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>

/*
 * volmgt_check:
 *	have volume management look at its devices to check
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
 */
int volmgt_check(char *path);

/*
 * volmgt_inuse:
 *	check to see if volume management is currently
 *	managing a particular device.
 *
 * arguments:
 *	path - the name of the device in /dev.  For example,
 *	  "/dev/rdiskette".
 *
 * return value(s):
 *	TRUE if volume management is managing the device, FALSE if not.
 */
int volmgt_inuse(char *path);

/*
 * volmgt_running:
 *	check to see if volume management is running.
 *
 * arguments:
 *	none.
 *
 * return value(s):
 *	TRUE if volume management is running, FALSE if not.
 */
int volmgt_running();

/*
 * volmgt_symname:
 *	Returns the volume management symbolic name
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
 */
char 	*volmgt_symname(char *path);

/*
 * volmgt_ownspath:
 *	check to see if the given path is contained in
 *	the volume management name space.
 *
 * arguments:
 *	path - string containing the path.
 *
 * return value(s):
 *	TRUE if the path is owned by volume management, FALSE if not.
 *	Will return FALSE if volume management isn't running.
 *
 */
int	volmgt_ownspath(char *path);

/*
 * volmgt_root:
 *	return the root of where the volume management
 *	name space is mounted.
 *
 * arguments:
 *	none.
 *
 * return value(s):
 *	Returns a pointer to a string containing the path to the
 *	volume management root (e.g. "/vol").
 *	Will return NULL if volume management isn't running.
 */
const char 	*volmgt_root(void);

/*
 * media_findname:
 *	try to come up with the character device when
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
 */
char 	*media_findname(char *start);

/*
 * media_getattr:
 *	returns the value for an attribute for a piece of
 * 	removable media.
 *
 * arguments:
 *	path - Path to the media in /vol.  Can be the block or character
 *		device.
 *
 *	attr - name of the attribute.
 *
 * return value(s):
 *	returns NULL or a pointer to a string that contains the value for
 * 	the requested attribute.
 *
 *	NULL can mean:
 *	 - the media doesn't exist
 *	 - there is no more space for malloc(3)
 *	 - the attribute doesn't exist for the named media
 *	 - the attribute is a boolean and is FALSE
 *
 *	the pointer to the string must be free'd with free(3).
 */
char *media_getattr(char *path, char *attr);

/*
 * media_setattr:
 *	set an attribute for a piece of media to a
 *	particular value.
 *
 * arguments:
 *	path - Path to the media in /vol.  Can be the block or character
 *		device.
 *
 *	attr - name of the attribute.
 *
 *	value - value of the attribute.  If value == "", the flag is
 *	    considered to be a boolean that is TRUE.  If value == 0, it
 *	    is considered to be a FALSE boolean.
 *
 * return value(s):
 *	TRUE on success, FALSE for failure.
 *
 *	Can fail because:
 *		- don't have permission to set the attribute because caller
 *		  is not the owner of the media and attribute is a "system"
 *		  attribute.
 *
 *		- don't have permission to set the attribute because the
 *		  attribute is a "system" attribute and is read-only.
 */
int	media_setattr(char *path, char *attr, char *value);

/*
 * media_getid:
 *	return the "id" of a piece of media.
 *
 * arguments:
 *	path - Path to the media in /vol.  Can be the block or character
 *		device.
 * return value(s):
 *	returns a u_longlong_t that is the "id" of the volume.
 *
 */
u_longlong_t	media_getid(char *path);

#ifdef	__cplusplus
}
#endif

#endif	/* _VOLMGT_H */
