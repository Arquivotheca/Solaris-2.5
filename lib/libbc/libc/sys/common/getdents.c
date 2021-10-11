#include <unistd.h>
#include <sys/types.h>
#include <sys/dirent.h>
#include <sys/syscall.h>
#include <sys/param.h>
#include <sys/errno.h>

#define	ALIGN	4

extern int errno;

struct n_dirent{
	unsigned long	d_ino;
	long		d_off;
	unsigned short	d_reclen;
	char		d_name[1];
};


int getdents(fd, buf, bytes)
int fd;
char *buf;
int bytes;
{
	return(bc_getdents(fd, buf, bytes));
}


int bc_getdents(fd, buf, bytes)
int fd;
char *buf;
int bytes;
{
	int ret, nbytes;
	char *nbuf;
	struct dirent *dir;
	struct n_dirent *ndir;
	int count=0;
	off_t last_off;
	
	if (buf == (char *)0 || buf == (char *)-1) {
		errno = EFAULT;
		return(-1);
	}

	nbytes = bytes; /* buffer can only be as large as user expects */

	if ((nbuf = (char *)malloc(nbytes)) == NULL) {
		return(-1);
	}
	
	if ((ret = _syscall(SYS_getdents, fd, nbuf, nbytes)) == -1) {
		free(nbuf);
		return(ret);
	}


	dir = (struct dirent *)buf;
	ndir = (struct n_dirent *)nbuf;	/* source directory format */

	while ((((int)(((char *)dir) + sizeof(struct n_dirent) +
	    strlen(ndir->d_name) + ALIGN) & ~(ALIGN - 1)) <
	    (int)(buf + bytes)) &&
	    ((char *)ndir + sizeof(struct n_dirent) <= (nbuf + ret))) {
		dir->d_off = ndir->d_off;
		dir->d_fileno = ndir->d_ino;	/* NOT VALID */
		dir->d_namlen = strlen(ndir->d_name);
		dir->d_reclen = (short)((sizeof(struct dirent) - MAXNAMLEN + 
		    dir->d_namlen + ALIGN) & -ALIGN);
		strncpy(dir->d_name, ndir->d_name, dir->d_namlen);
		dir->d_name[dir->d_namlen] = '\0';
		count += dir->d_reclen;
		last_off = ndir->d_off;
		dir  = (struct dirent *)((char *)dir + 
				    ((int)( dir->d_reclen)));
		ndir = (struct n_dirent *)((char *)ndir +
				    ((int)(ndir->d_reclen)));
	}

	/*
	 * Seek to the next entry in the directory. If all entries
	 * in ndir were not copied to dir, the next getdents syscall
	 * will start reading from there.
	 */
	(void)lseek(fd, last_off, SEEK_SET);
	free(nbuf);
	return(count);
}
