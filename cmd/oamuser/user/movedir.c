/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)movedir.c	1.4	95/05/10 SMI"       /* SVr4.0 1.4 */

#include <sys/types.h>
#include <stdio.h>
#include <userdefs.h>
#include "messages.h"

#define	SBUFSZ	256

extern int access(), rm_files();
extern void errmsg();

static char cmdbuf[SBUFSZ];	/* buffer for system call */

/*
	Move directory contents from one place to another
*/
int
move_dir(from, to, login)
char *from;			/* directory to move files from */
char *to;			/* dirctory to move files to */
char *login;			/* login id of owner */
{
	size_t len = 0;
	register rc = EX_SUCCESS;
	/*
	 ******* THIS IS WHERE SUFFICIENT SPACE CHECK GOES
	*/

	if (access(from, 00) == 0) {	/* home dir exists */
		/* move all files */
		(void) sprintf(cmdbuf,
			"cd %s && find . -user %s -print | cpio -pd %s",
			from, login, to);

		if (system(cmdbuf) != 0) {
			errmsg(M_NOSPACE, from, to);
			return (EX_NOSPACE);
		}

		/*
		Check that to dir is not a subdirectory of from
		*/
		len = strlen(from);
		if (strncmp(from, to, len) == 0 &&
		    strncmp(to+len, "/", 1) == 0) {
			errmsg(M_RMFILES);
			return (EX_HOMEDIR);
		}

		/* Remove the files in the old place */
		rc = rm_files(from, login);

	}

	return (rc);
}
