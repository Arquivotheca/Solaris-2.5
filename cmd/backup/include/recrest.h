
/*	@(#)recrest.h 1.5 93/04/28	*/

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

/*
 * this defines the data passed between the recover and restore programs
 */
#define	TAPEREC 	'1'		/* tape label specification */
#define	DUMPREC 	'2'		/* dump specification */
#define	FILEREC 	'3'		/* individual file specification */
#define	DIRREC  	'4'		/* directory stats */
#define	XRESTORE	'5'		/* full restore with `x' option */
#define	RRESTORE	'6'		/* full restore with `r' option */
#define	NOTIFYREC	'7'		/* notification parameter */
#define	LINKREC		'8'		/* create a hard link */
#define	COPYREC		'9'		/* make a copy of the previous file */

struct dirstats {
	long		dir_uid;
	long		dir_gid;
	unsigned long	dir_mode;
	long		dir_atime;
	long		dir_mtime;
};

/*
 * files used across invocations of restore.  These are unlinked
 * by recover at the end of a sequence of full restores
 */
#define	RESTORESYMTABLE	"restoresymtable"
#define	RESTOREDEVICE	"restore_default_device"
