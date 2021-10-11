/*
 * Copyright (c) 1993-1994 Sun Microsystems, Inc.  All Rights Reserved.
 */

#ident "@(#)compfsops.c 1.1     94/07/19 SMI"
/*
 *	Composite filesystem for secondary boot
 *	that uses mini-MS-DOS filesystem
 */

#include <sys/param.h>
#include <sys/vnode.h>
#include <sys/sysmacros.h>
#include <sys/promif.h>
#include <sys/stat.h>

#include <sys/fs/pc_fs.h>
#include <sys/fs/pc_label.h>
#include <sys/fs/pc_dir.h>
#include <sys/fs/pc_node.h>

#include <sys/bootdef.h>
#include <sys/bootvfs.h>
#include <sys/bootdebug.h>

#define	MAPFILE	"\\SOLARIS.MAP"
#define	COMPFS_AUGMENT	0x1000	/* set by c flag in SOLARIS.MAP */
#define	COMPFS_TEXT	0x2000	/* set by t flag in SOLARIS.MAP */
#define	whitespace(C)	((unsigned char)(C) <= ' ' ? 1 : 0)

struct map_entry {
	char	*target;
	char	*source;
	int	flags;
	struct map_entry *link;
};

static char mapfile[] = MAPFILE;
static struct map_entry *map_listh = 0;
static struct map_entry *map_listt = 0;

static int	cpfs_mapped(char *, char **, int *);
static void	cpfs_build_map(void);

static int	cpfsdebuglevel = 0;
static int	compfsverbose = 1;

/*
 *  Function prototypes
 */

extern	int	init_disketteio(void);

/*
 * exported functional prototypes
 */
static int	boot_compfs_mountroot(char *str);
static int	boot_compfs_open(char *filename, int flags);
static int	boot_compfs_close(int fd);
static int	boot_compfs_read(int fd, caddr_t buf, int size);
static off_t	boot_compfs_lseek(int, off_t, int);
static int	boot_compfs_fstat(int fd, struct stat *stp);
static void	boot_compfs_closeall(int flag);

struct boot_fs_ops boot_compfs_ops = {
	"compfs",
	boot_compfs_mountroot,
	boot_compfs_open,
	boot_compfs_close,
	boot_compfs_read,
	boot_compfs_lseek,
	boot_compfs_fstat,
	boot_compfs_closeall,
};

extern struct boot_fs_ops *extendfs_ops;
extern struct boot_fs_ops *origfs_ops;

static struct compfsfile *compfshead = NULL;
static int	compfs_filedes = 1;	/* 0 is special */

typedef struct compfsfile {
	struct compfsfile *forw;	/* singly linked */
	int	fd;		/* the fd given out to caller */
	int	ofd;		/* original filesystem fd */
	int	efd;		/* extended filesystem fd */
	off_t	offset;		/* for lseek maintenance */
	int	o_size;		/* in original filesystem */
	int	e_size;		/* in extended filesystem */
	int	flags;
} compfsfile_t;

/*
 * Given an fd, do a search (in this case, linear linked list search)
 * and return the matching compfsfile pointer or NULL;
 * By design, when fd is -1, we return a free compfsfile structure (if any).
 */

static struct compfsfile *
get_fileptr(int fd)
{	struct compfsfile *fileptr = compfshead;

	while (fileptr != NULL) {
		if (fd == fileptr->fd)
			break;
		fileptr = fileptr->forw;
	}
#ifdef COMPFS_OPS_DEBUG
	if (cpfsdebuglevel > 6)
		printf("compfs: returning fileptr 0x%x\n", fileptr);
#endif
	return (fileptr);
}

static void
cpfs_build_map(void)
{
	int	mapfid;
	struct map_entry *mlp;
	char	*bp;
	char	*bufend;
	char	*cp;
	char	buffer[PC_SECSIZE];
	char	dospath[MAXNAMELEN];
	char	fspath[MAXNAMELEN];
	int	rcount;
	int	dosplen, fsplen;
	int	flags = 0;

	if ((mapfid = (*extendfs_ops->fsw_open)(mapfile, flags)) < 0) {
#ifdef COMPFS_OPS_DEBUG
		if (cpfsdebuglevel > 2)
			printf("compfs: open %s file failed\n", mapfile);
#endif
		return;
	}

	if (!(rcount = (*extendfs_ops->fsw_read)(mapfid, buffer, PC_SECSIZE))) {
		goto mapend;
	}
	bp = buffer;
	bufend = buffer + rcount;

	do {	/* for each line in map file */

		*fspath = '\0';
		fsplen = 0;
		*dospath = '\0';
		dosplen = 0;

		while (whitespace(*bp)) {
			if (++bp >= bufend) {
				if (!(rcount = (*extendfs_ops->fsw_read)(mapfid,
				    buffer, PC_SECSIZE)))
					goto mapend;
				bp = buffer;
				bufend = buffer + rcount;
			}
		}
		if (*bp == '#')
			/* skip over comment lines */
			goto mapskip;

		cp = fspath;
		if (*bp != '/') {
			/*
			 * fs pathname does not begin with '/'
			 * so prepend with current path. More Work??
			 */
		}
		while (!whitespace(*bp) && fsplen < MAXNAMELEN) {
			*cp++ = *bp++;
			if (bp >= bufend) {
				if (!(rcount = (*extendfs_ops->fsw_read)(mapfid,
				    buffer, PC_SECSIZE)))
					goto mapend;
				bp = buffer;
				bufend = buffer + rcount;
			}
			fsplen++;
		}
		*cp = '\0';

		while (whitespace(*bp)) {
			if (*bp == '\n')
				goto mapskip;
			if (++bp >= bufend) {
				if (!(rcount = (*extendfs_ops->fsw_read)(mapfid,
				    buffer, PC_SECSIZE)))
					goto mapend;
				bp = buffer;
				bufend = buffer + rcount;
			}
		}

		cp = dospath;
		if (*bp != '/' && *bp != '\\') {
			/*
			 * DOS pathname does not begin with '\'
			 * so prepend with root
			 */
			*cp++ = '\\';
			dosplen = 1;
		}
		while (!whitespace(*bp) && dosplen < MAXNAMELEN) {
			*cp = *bp++;
			if (bp >= bufend) {
				if (!(rcount = (*extendfs_ops->fsw_read)(mapfid,
				    buffer, PC_SECSIZE)))
					goto mapend;
				bp = buffer;
				bufend = buffer + rcount;
			}
			if (*cp >= 'a' && *cp <= 'z')
				*cp = *cp - 'a' + 'A';
			cp++;
			dosplen++;
		}
		*cp = '\0';

		mlp = (struct map_entry *) bkmem_alloc(
		    sizeof (struct map_entry) + fsplen + dosplen + 2);

		cp = (char *)(mlp + 1);
		bcopy(fspath, cp, fsplen + 1);
		mlp->target = cp;

		cp = (char *)(cp + fsplen + 1);
		bcopy(dospath, cp, dosplen + 1);
		mlp->source = cp;

		mlp->flags = 0;
		while (*bp != '\n') {
			if (*bp == 'c')
				mlp->flags |= COMPFS_AUGMENT;
			else if (*bp == 't')
				mlp->flags |= COMPFS_TEXT;
			if (++bp >= bufend) {
				if (!(rcount = (*extendfs_ops->fsw_read)(mapfid,
				    buffer, PC_SECSIZE)))
					goto mapend;
				bp = buffer;
				bufend = buffer + rcount;
			}
		}
		/*
		 * insert new entry into linked list
		 */
		mlp->link = NULL;
		if (!map_listh)
			map_listh = mlp;
		if (map_listt)
			map_listt->link = mlp;
		map_listt = mlp;

		if (compfsverbose || cpfsdebuglevel > 2) {
			if (mlp->flags & COMPFS_AUGMENT)
				printf("compfs: %s augmented with %s\n",
				    fspath, dospath);
			else
				printf("compfs: %s mapped to %s\n",
				    fspath, dospath);
		}

mapskip:
		while (*bp != '\n')
			if (++bp >= bufend) {
				if (!(rcount = (*extendfs_ops->fsw_read)(mapfid,
				    buffer, PC_SECSIZE)))
					goto mapend;
				bp = buffer;
				bufend = buffer + rcount;
			}
	} while (1);
mapend:
	(*extendfs_ops->fsw_close)(mapfid);

	if (compfsverbose || cpfsdebuglevel > 2) {
		printf("\n");
		if (cpfsdebuglevel & 1)
			goany();
	}
}


static int
cpfs_mapped(char *str, char **dos_str, int *flagp)
{
	struct map_entry *mlp;

	for (mlp = map_listh; mlp; mlp = mlp->link) {
		if (strcmp(mlp->target, str) == 0) {
			*dos_str = mlp->source;
			*flagp = mlp->flags;
			return (1);
		}
	}
	return (0);
}

static int
boot_compfs_open(char *str, int flags)
{
	struct compfsfile *fileptr;
	char	*dos_str;
	int	mflags;
	int	retcode;
	int	dont_bother = 0;
	struct stat	statbuf;
	int	new = 0;

#ifdef COMPFS_OPS_DEBUG
	if (cpfsdebuglevel > 4) {
		printf("compfs_open(): open %s flag=0x%x\n",
			str, flags);
		if (cpfsdebuglevel & 1)
			goany();
	}
#endif

	if ((fileptr = get_fileptr(0)) == NULL) {
		fileptr = (compfsfile_t *)
			bkmem_alloc(sizeof (compfsfile_t));
		new++;
	}
	fileptr->flags = 0;
	fileptr->offset = 0;
	fileptr->o_size = 0;
	fileptr->e_size = 0;

	if (cpfs_mapped(str, &dos_str, &mflags)) {
		retcode = (*extendfs_ops->fsw_open)(dos_str, flags);
		if (retcode >= 0) {
			if (!(mflags & COMPFS_AUGMENT)) {
				/* complete replacement, don't bother */
				dont_bother = 1;
				fileptr->ofd = -1;
			}
			fileptr->flags = mflags;
		}
		fileptr->efd = retcode;
	} else	{
		dos_str = "";
		fileptr->efd = -1;
	}

	if (!dont_bother)
		fileptr->ofd = (*origfs_ops->fsw_open)(str, flags);

	if (fileptr->ofd < 0 && fileptr->efd < 0) {
		if (fileptr->ofd >= 0)
			(*origfs_ops->fsw_close)(fileptr->ofd);
		if (fileptr->efd >= 0)
			(*extendfs_ops->fsw_close)(fileptr->efd);
		if (new)
			bkmem_free(fileptr, sizeof (compfsfile_t));
		return (-1);
	} else {
		fileptr->fd = compfs_filedes++;
	}

	/*
	 * establish size information for seek and read
	 */

	if (fileptr->ofd >= 0) {
		(*origfs_ops->fsw_fstat)(fileptr->ofd, &statbuf);
		fileptr->o_size = statbuf.st_size;
	}
	if (fileptr->efd >= 0) {
		(*extendfs_ops->fsw_fstat)(fileptr->efd, &statbuf);
		fileptr->e_size = statbuf.st_size;
	}
	if (new) {
		fileptr->forw = compfshead;
		compfshead = fileptr;
	}

#ifdef COMPFS_OPS_DEBUG
	if (cpfsdebuglevel > 2) {
		printf("compfs_open(): compfs fd = %d, origfs file \"%s\" fd"
			" %d, dos file \"%s\" fd %d\n",
			fileptr->fd, str, fileptr->ofd, dos_str, fileptr->efd);
		printf("origfs file size %d, dos file size %d\n",
			fileptr->o_size, fileptr->e_size);
		if (cpfsdebuglevel & 1)
			goany();
	}
#endif

	return (fileptr->fd);
}

/*
 * compfs_lseek()
 *
 * We maintain an offset at this level for composite file system.
 * This requires us keeping track the file offsets here and
 * in read() operations in consistent with the normal semantics.
 */

static off_t
boot_compfs_lseek(int fd, off_t addr, int whence)
{
	struct compfsfile *fileptr;

#ifdef COMPFS_OPS_DEBUG
	if (cpfsdebuglevel > 4) {
		printf("compfs_lseek(): fd %d addr=%d, whence=%d\n",
			fd, addr, whence);
		if (cpfsdebuglevel & 1)
			goany();
	}
#endif

	fileptr = get_fileptr(fd);
	if (fileptr == NULL)
		return (-1);

	switch (whence) {
	case SEEK_CUR:
		fileptr->offset += addr;
		break;
	case SEEK_SET:
		fileptr->offset = addr;
		break;
	default:
	case SEEK_END:
		printf("compfs_lseek(): invalid whence value %d\n", whence);
		break;
	}

	/*
	 * A seek beyond origfs EOF implies reading the auxiliary
	 * (DOS) file of the composite filesystem.
	 * This is okay since this is a read-only filesystem.
	 * Actual "file offset seek" is done when read() is involved.
	 */

	/*
	 * Let's do the lseek motion on the low level file system.
	 */

	if (fileptr->ofd >= 0) {
		if (fileptr->offset < fileptr->o_size) {
			if ((*origfs_ops->fsw_lseek)(fileptr->ofd,
			    fileptr->offset, SEEK_SET) < 0)
				return (-1);
		} else {
			if (fileptr->efd < 0)
				return (0);	/* easy */
			if ((*extendfs_ops->fsw_lseek)(fileptr->efd,
			    fileptr->offset-fileptr->o_size, SEEK_SET) < 0)
				return (-1);
		}
	} else if (fileptr->efd >= 0) {
		if ((*extendfs_ops->fsw_lseek)(fileptr->efd, fileptr->offset,
		    SEEK_SET) < 0)
			return (-1);
	}

#ifdef COMPFS_OPS_DEBUG
	if (cpfsdebuglevel > 4) {
		printf("compfs_lseek(): new offset %d\n", fileptr->offset);
		if (cpfsdebuglevel & 1)
			goany();
	}
#endif

	return (0);
}

/*
 * compfs_fstat() only supports size and mode at present time.
 */

static int
boot_compfs_fstat(int fd, struct stat *stp)
{
	struct compfsfile *fileptr;
	struct stat sbuf;

#ifdef COMPFS_OPS_DEBUG
	if (cpfsdebuglevel > 4)
		printf("compfs_fstat(): fd =%d\n", fd);
#endif

	fileptr = get_fileptr(fd);
	if (fileptr == NULL) {
		printf("compfs_fstat(): no such fd %d\n", fd);
#ifdef COMPFS_OPS_DEBUG
		goany();
#endif
		return (-1);
	}

	if (fileptr->ofd >= 0) {
		if ((*origfs_ops->fsw_fstat)(fileptr->ofd, stp) < 0)
			return (-1);
	} else {
		stp->st_mode = 0;
		stp->st_size = 0;
	}

	if (fileptr->efd >= 0) {
		if ((*extendfs_ops->fsw_fstat)(fileptr->efd, &sbuf) < 0)
			return (-1);
		stp->st_size += sbuf.st_size;
		stp->st_mode |= sbuf.st_mode;
	}

	return (0);
}

/*
 * Special dos-text adjustment processing:
 *  converting "\r\n" to " \n" presumably for ASCII files.
 */

static void
dos_text(char *p, int count)
{	int i, j;

	for (i = count - 1, j = (int) p; i > 0; i--, j++)
		if (*(char *) j == '\r' && *(char *)(j + 1) == '\n')
			*(char *) j = ' ';
}

/*
 * compfs_read()
 */

static int
boot_compfs_read(int fd, caddr_t buf, int count)
{
	struct compfsfile *fileptr;
	int	pcretcode;
	int	retcode = -1;

	fileptr = get_fileptr(fd);
	if (fileptr == NULL) {
		printf("compfs_read(): no such fd %d\n", fd);
#ifdef COMPFS_OPS_DEBUG
		goany();
#endif
		return (-1);
	}

	/*
	 * we seek to the right place before reading
	 */
#ifdef COMPFS_OPS_DEBUG
	if (cpfsdebuglevel > 4) {
		printf("compfs_read(): offset at %d (osz=%d, esz=%d)\n",
			fileptr->offset, fileptr->o_size, fileptr->e_size);
		if (cpfsdebuglevel & 1)
			goany();
	}
#endif
	if (fileptr->ofd >= 0) {
		if (fileptr->offset < fileptr->o_size) {
			retcode = (*origfs_ops->fsw_read)(fileptr->ofd,
				buf, count);
#ifdef COMPFS_OPS_DEBUG
			if (cpfsdebuglevel > 4) {
				printf("compfs_read(): origfs read"
					" returned %d\n", retcode);
			}
#endif
			if (retcode > 0)
				fileptr->offset += retcode;
			if (retcode == count || fileptr->efd < 0 || retcode < 0)
				return (retcode);
			/*
			 * assert
			 * fileptr->o_size == fileptr->offset;
			 */

			pcretcode = (*extendfs_ops->fsw_read)(fileptr->efd,
				buf + retcode, count - retcode);
#ifdef COMPFS_OPS_DEBUG
			if (cpfsdebuglevel > 2) {
				printf("compfs_read(): followup dos read"
					" returned %d\n", pcretcode);
				if (cpfsdebuglevel & 1)
					goany();
			}
#endif
			if (pcretcode < 0)
				return (pcretcode);
			if (fileptr->flags & COMPFS_TEXT)
				(void) dos_text(buf+retcode, pcretcode);
			fileptr->offset += pcretcode;
			return (retcode + pcretcode);
		} else {
			if (fileptr->efd < 0)
				return (0);	/* easy */
			pcretcode = (*extendfs_ops->fsw_read)(fileptr->efd, buf,
				count);
			if (pcretcode > 0) {
				if (fileptr->flags & COMPFS_TEXT)
					(void) dos_text(buf, pcretcode);
				fileptr->offset += pcretcode;
			}
			return (pcretcode);
		}
	} else if (fileptr->efd >= 0) {
		retcode = (*extendfs_ops->fsw_read)(fileptr->efd, buf, count);
		if (retcode > 0) {
			fileptr->offset += retcode;
			if (fileptr->flags & COMPFS_TEXT)
				(void) dos_text(buf, retcode);
		}

#ifdef COMPFS_OPS_DEBUG
		if (cpfsdebuglevel > 4) {
			printf("compfs_read(): solo dos read returned %d\n",
				retcode);
			if (cpfsdebuglevel & 1)
				goany();
		}
#endif
	}

#ifdef COMPFS_OPS_DEBUG
	if (cpfsdebuglevel > 4) {
		printf("compfs_read(): return code %d\n", retcode);
		if (cpfsdebuglevel & 1)
			goany();
	}
#endif
	return (retcode);
}

static int
boot_compfs_close(int fd)
{
	struct compfsfile *fileptr;
	int ret1 = 0;
	int ret2 = 0;

	fileptr = get_fileptr(fd);
	if (fileptr == NULL) {
		printf("compfs_close(): no such fd %d.\n", fd);
#ifdef COMPFS_OPS_DEBUG
		goany();
#endif
		return (-1);
	}

#ifdef COMPFS_OPS_DEBUG
	if (cpfsdebuglevel > 4) {
		printf("compfs_close(): fd=%d ofd=%d efd=%d\n",
			fd, fileptr->ofd, fileptr->efd);
		if (cpfsdebuglevel & 1)
			goany();
	}
#endif

	if (fileptr->efd >= 0)
		ret1 = (*extendfs_ops->fsw_close)(fileptr->efd);
	if (fileptr->ofd >= 0)
		ret2 = (*origfs_ops->fsw_close)(fileptr->ofd);
	fileptr->fd = 0; /* don't bother to free, make it re-usable */

	if (ret1 < 0 || ret2 < 0)
		return (-1);
	return (0);
}


/*
 * compfs_mountroot() returns 0 on success and -1 on failure.
 */

static int
boot_compfs_mountroot(char *str)
{
	/* extern unsigned short BootDev; */

	/* if (BootDev != BOOTDRV_HD) */
	(void) init_disketteio(); /* can be folded to mountroot */
	if ((*extendfs_ops->fsw_mountroot)("/dev/diskette") == 0)
		cpfs_build_map();

	return ((*origfs_ops->fsw_mountroot)(str));
}

static void
boot_compfs_closeall(int flag)
{	struct map_entry *mlp;
	struct compfsfile *fileptr = compfshead;

	(*extendfs_ops->fsw_closeall)(flag);
	(*origfs_ops->fsw_closeall)(flag);

	while (fileptr != NULL) {
		bkmem_free(fileptr, sizeof (struct compfsfile));
		fileptr = fileptr->forw;
	}
	compfshead = NULL;

	for (mlp = map_listh; mlp; mlp = map_listh) {
		map_listh = mlp->link;
		bkmem_free((caddr_t) mlp, sizeof (struct map_entry) +
		    strlen(mlp->target) + strlen(mlp->source) + 2);
	}
	map_listt = NULL;

#ifdef COMPFS_OPS_DEBUG
	if (cpfsdebuglevel > 2) {
		printf("compfs_closeall()\n");
		if (cpfsdebuglevel & 1)
			goany();
	}
#endif
}
