/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)getxby_door.c	1.4	95/04/17 SMI"

#ifdef PIC

#include "synonyms.h"
#include "shlib.h"
#include <pwd.h>
#include <nss_dbdefs.h>
#include <stdio.h>
#include <synch.h>
#include <mtlib.h>
#include <sys/param.h>
#include <fcntl.h>

#include <getxby_door.h>
#include <sys/door.h>

/*
 *
 * Routine that actually performs the door call.
 * Note that we cache a file descriptor.  We do
 * the following to prevent disasters:
 *
 * 1) Never use 0,1 or 2; if we get this from the open
 *    we dup it upwards.
 *
 * 2) Set the close on exec flags so descriptor remains available
 *    to child processes.
 *
 * 3) Verify that the door is still the same one we had before
 *    by using door_info on the client side.
 *
 *
 *  int _nsc_trydoorcall(void *dptr, int *bufsize, int *actualsize);
 *
 *      *dptr           IN: points to arg buffer OUT: points to results buffer
 *      *bufsize        IN: overall size of buffer OUT: overall size of buffer
 *      *actualsize     IN: size of call data OUT: size of return data
 *
 *  Note that *dptr may change if provided space as defined by *bufsize is
 *  inadequate.  In this case the door call mmaps more space and places
 *  the answer there and sets dptr to contain a pointer to the space, which
 *  should be freed with munmap.
 *
 *  Returns 0 if the door call reached the server, -1 if contact was not made.
 *
 */

extern int errno;

#ifdef _REENTRANT
static mutex_t	_door_lock = DEFAULTMUTEX;
#endif

int
_nsc_trydoorcall(nsc_data_t **dptr, int *ndata, int *adata)
{
	static	int 		doorfd = -1;
	static	door_info_t 	real_door;
	door_info_t 		my_door;
	int 			ndid = 0;

	/*
	 * the first time in we try and open and validate the door.
	 * the validations are that the door must have been
	 * created with the name service door cookie and
	 * that the file attached to the door is owned by root
	 * and readonly by user, group and other.  If any of these
	 * validations fail we refuse to use the door.
	 */

	if (doorfd == -1) {
		mutex_lock(&_door_lock);
		/*
		 * if we still have no door descriptor open it
		 * otherwise avoid the race condition and drop
		 * the lock.
		 */
		if (doorfd == -1) {
			int		tbc[3];
			int		i;
			if ((doorfd = _open(NAME_SERVICE_DOOR, O_RDONLY, 0))
			    == -1) {
				mutex_unlock(&_door_lock);
				return (NOSERVER);
			}

			/*
			 * dup up the file descriptor if we have 0 - 2
			 * to avoid problems with shells stdin/out/err
			 */
			i = 0;
			while (doorfd < 3) { /* we have a reserved fd */
				tbc[i++] = doorfd;
				if ((doorfd = dup(doorfd)) < 0) {
					perror("dup door fd");
					while (i--)
						close(tbc[i]);
					doorfd = -1;
					mutex_unlock(&_door_lock);
					return (NOSERVER);
				}
			}
			while (i--)
				close(tbc[i]);
			/*
			 * mark this door descriptor as close on exec
			 */
			(void) fcntl(doorfd, F_SETFD, FD_CLOEXEC);
			if (_door_info(doorfd, &real_door) == -1) {
				mutex_unlock(&_door_lock);
				return (EBADF);
			}
			if ((real_door.di_attributes & DOOR_REVOKED) ||
			    (real_door.di_data !=
					(door_ptr_t)NAME_SERVICE_DOOR_COOKIE)) {
				close(doorfd);
				doorfd = -1;
				mutex_unlock(&_door_lock);
				return (NOSERVER);
			}
		}
		mutex_unlock(&_door_lock);
	} else {
		if ((_door_info(doorfd, &my_door) == -1) ||
		    (my_door.di_attributes & DOOR_REVOKED) ||
		    (my_door.di_data != (door_ptr_t)NAME_SERVICE_DOOR_COOKIE) ||
		    (my_door.di_uniqifier != real_door.di_uniqifier)) {
			close(doorfd);
			doorfd = -1;
			return (NOSERVER);
		}
	}

	if (_door_call(doorfd, dptr, ndata, adata, &ndid) == -1) {
		return (NOSERVER);
	}
	if (*adata == 0 || *dptr == NULL) {
		return (NOSERVER);
	}

	return ((*dptr)->nsc_ret.nsc_return_code);
}


#endif PIC
