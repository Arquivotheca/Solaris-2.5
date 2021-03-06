/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)cpiostat.c	1.6	92/09/29 SMI"	/* SVr4.0 1.1	*/
#define	_STYPES
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mkdev.h>

struct cpioinfo {
	o_dev_t st_dev;
	o_ino_t st_ino;
	o_mode_t	st_mode;
	o_nlink_t	st_nlink;
	o_uid_t st_uid;
	o_gid_t st_gid;
	o_dev_t st_rdev;
	off_t	st_size;
	time_t	st_modtime;
} tmpinfo;


o_dev_t
convert(dev)
dev_t dev;
{
	major_t maj, min;

	maj = major(dev);	/* get major number */
	min = minor(dev);	/* get minor number */

	/* make old device number */
	return ((maj << 8) | min);
}

struct cpioinfo *
svr32stat(fname)
char *fname;
{
	struct stat stbuf;

	if (stat(fname, &stbuf) < 0) {
		stbuf.st_ino = 0;
	}
	tmpinfo.st_dev = convert(stbuf.st_dev);
	tmpinfo.st_ino = stbuf.st_ino;
	tmpinfo.st_mode = stbuf.st_mode;
	tmpinfo.st_nlink = stbuf.st_nlink;
	tmpinfo.st_uid = stbuf.st_uid;
	tmpinfo.st_gid = stbuf.st_gid;
	tmpinfo.st_rdev = convert(stbuf.st_rdev);
	tmpinfo.st_size = stbuf.st_size;
	tmpinfo.st_modtime = stbuf.st_mtime;
	return(&tmpinfo);
}

struct cpioinfo *
svr32lstat(fname)
char *fname;
{
	struct stat stbuf;

	if(!lstat(fname, &stbuf)) {
		tmpinfo.st_dev = convert(stbuf.st_dev);
		tmpinfo.st_ino = stbuf.st_ino;
		tmpinfo.st_mode = stbuf.st_mode;
		tmpinfo.st_nlink = stbuf.st_nlink;
		tmpinfo.st_uid = stbuf.st_uid;
		tmpinfo.st_gid = stbuf.st_gid;
		tmpinfo.st_rdev = convert(stbuf.st_rdev);
		tmpinfo.st_size = stbuf.st_size;
		tmpinfo.st_modtime = stbuf.st_mtime;
		return(&tmpinfo);
	}
#if _3b2
	tmpinfo.st_ino = 0;
	return(&tmpinfo);
#else
	return((struct cpioinfo *)0);
#endif
}
