#pragma ident	"@(#)_stat.c	1.9	95/05/31 SMI" 

#include <sys/errno.h>
#include <sys/syscall.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/param.h>
#include "compat.h"
#include "s5sysmacros.h"

#define ST_FSTYPSZ	16	/* array size for file system type name */

struct ts {
	long	tv_sec;		/* seconds */
	long	tv_nsec;	/* nanoseconds */
};

struct n_stat {
        unsigned long   st_dev;
        long            st_pad1[3];     /* reserved for network id */
        unsigned long   st_ino;
        unsigned long   st_mode;
        unsigned long   st_nlink;
        long            st_uid;
        long            st_gid;
        unsigned long   st_rdev;
        long            st_pad2[2];
        long            st_size;
        long            st_pad3;        /* future off_t expansion */
        struct ts       st_atim;
        struct ts       st_mtim;
        struct ts       st_ctim;
        long            st_blksize;
        long            st_blocks;
        char            st_fstype[ST_FSTYPSZ];
        long            st_pad4[8];     /* expansion area */
 
};


int fstat(fd, buf)
int fd;
struct stat *buf;
{
	return(bc_fstat(fd, buf));
}

int bc_fstat(fd, buf)
int fd;
struct stat *buf;
{
	int ret;
	struct n_stat nb;
	extern int errno;	

	if (buf == 0) {
		errno = EFAULT;
		return(-1);
	}
		
	if ((ret = _syscall(SYS_fstat, fd, &nb)) == -1)
		return(ret);

	if (fd_get(fd) != -1) {
		buf->st_size = getmodsize(buf->st_size, 
			sizeof(struct compat_utmp), sizeof(struct utmp));
	}

	if (ret == 0)
		cpstatbuf(buf, &nb);

	return(ret);
}

int stat_com(sysnum, path, buf)
int sysnum;
char *path;
struct stat *buf;
{
	int fd, ret;
	struct n_stat nb;

	if (strcmp(path, "/etc/mtab") == 0) {
/*
 * stat the real mnttab, or the "parsed" mtab
 * created by open?
 *
 * for now, stat the real mnttab.
 */

/*
 *		fd = open_mnt("/etc/mnttab", "mtab", O_RDONLY);
 *		ret = fstat(fd, buf);
 *		close(fd);
 *		return(ret);
 */
		ret = stat_com(sysnum, "/etc/mnttab", buf);
		return(ret);
	}
	else if (strcmp(path, "/etc/fstab") == 0) {
		fd = open_mnt("/etc/vfstab", "fstab", O_RDONLY);
		ret = fstat(fd, buf);
		close(fd);
		return(ret);
	}
	else if (strcmp(path, "/var/adm/utmp") == 0 ||
	    strcmp(path, "/var/adm/wtmp") == 0) {
		if ((ret = _syscall(sysnum, path, &nb)) == -1)
			return(-1);
		else {
			buf->st_size = getmodsize(buf->st_size, 
		       	    sizeof(struct compat_utmp), sizeof(struct utmp));
			cpstatbuf(buf, &nb);
			return(ret);
		}
	} else if (_strstr(path, "/lib/locale/") != 0) {
		fd = open(path, O_RDONLY);
		ret = fstat(fd, buf);
		close(fd);
		return(ret);
	} else {
		if ((ret = _syscall(sysnum, path, &nb)) != -1)
			cpstatbuf(buf, &nb);
		return(ret);
	}
}


/*
 * Common code to copy xstat buf to BSD style buf
 */
cpstatbuf(bsdbuf, nbuf)
    struct stat         *bsdbuf;
    struct n_stat       *nbuf;
{
        bsdbuf->st_dev = (dev_t) cmpdev(nbuf->st_dev);
        bsdbuf->st_ino = nbuf->st_ino;
        bsdbuf->st_mode = (unsigned short) nbuf->st_mode;
        bsdbuf->st_nlink = (short) nbuf->st_nlink;
        bsdbuf->st_uid = (uid_t) nbuf->st_uid;
        bsdbuf->st_gid = (gid_t) nbuf->st_gid;
        bsdbuf->st_rdev = (dev_t) cmpdev(nbuf->st_rdev);
        bsdbuf->st_size = nbuf->st_size;
        bsdbuf->st_atime = nbuf->st_atim.tv_sec;
        bsdbuf->st_mtime = nbuf->st_mtim.tv_sec;
        bsdbuf->st_ctime = nbuf->st_ctim.tv_sec;
        bsdbuf->st_blksize = nbuf->st_blksize;
        bsdbuf->st_blocks = nbuf->st_blocks;
 
}
