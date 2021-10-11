#ident	"@(#)trans_subr.c 1.12 93/07/07"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "defs.h"
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <fcntl.h>
#include <errno.h>
#include <database/backupdb.h>
#include <database/dir.h>
#include <database/instance.h>
#include <database/activetape.h>

static int fd = -1;

extern	char	*dir_dblockmap;
extern	char	*inst_dblockmap;
extern	char	*tape_dblockmap;

extern	int	dir_dblockmapsize;
extern	int	inst_dblockmapsize;
extern	int	tape_dblockmapsize;

#ifdef __STDC__
static int dotrans(const char *, const char *, int, char *, int);
static int trans_open(const char *, const char *, off_t);
static void trans_close(void);
static caddr_t trans_getblock(u_long, int);
static int set_instance_recsize(const char *host);
#else
static int dotrans();
static int trans_open();
static void trans_close();
static caddr_t trans_getblock();
static int set_instance_recsize();
#endif

int
#ifdef __STDC__
dir_trans(const char *host)
#else
dir_trans(host)
	char *host;
#endif
{
	int rc;

	rc = dotrans(host, DIRFILE, DIR_BLKSIZE,
			dir_dblockmap, dir_dblockmapsize);
	dir_dblockmap = (char *)0;
	dir_dblockmapsize = 0;
	return (rc);
}

int
#ifdef __STDC__
instance_trans(const char *host)
#else
instance_trans(host)
	char *host;
#endif
{
	int rc;

	rc = 0;
	if (instance_recsize == 0) {
		rc = set_instance_recsize(host);
	}
	if (rc == 0) {
		rc = dotrans(host, INSTANCEFILE, instance_recsize,
		    inst_dblockmap, inst_dblockmapsize);
	}
	inst_dblockmap = (char *) NULL;
	inst_dblockmapsize = 0;
	return (rc);
}

int
#ifdef __STDC__
tape_trans(const char *host)
#else
tape_trans(host)
	char *host;
#endif
{
	int rc;

	rc = dotrans(host, TAPEFILE, TAPE_RECSIZE,
			tape_dblockmap, tape_dblockmapsize);
	tape_dblockmap = (char *)0;
	tape_dblockmapsize = 0;
	return (rc);
}

static int
#ifdef __STDC__
set_instance_recsize(const char *host)
#else
set_instance_recsize(host)
	char *host;
#endif
{
	struct instance_record ir;
	int fd;
	char fname[MAXPATHLEN];

	(void) sprintf(fname, "%s/%s", host, INSTANCEFILE);
	if ((fd = open(fname, O_RDONLY)) == -1) {
		(void) sprintf(fname, "%s/%s%s", host,
			INSTANCEFILE, TRANS_SUFFIX);
		if ((fd = open(fname, O_RDONLY)) == -1) {
			/*
			 * XXX: send panic message to operator...
			 */
			(void) fprintf(stderr, gettext(
				"%s: cannot get instance recsize\n"), "trans");
			return (-1);
		}
	}
	(void) bzero((char *)&ir, sizeof (struct instance_record));
	if (read(fd, (char *)&ir, sizeof (struct instance_record)) !=
	    sizeof (struct instance_record)) {
		(void) fprintf(stderr, gettext(
			"cannot get instance recsize\n"));
		(void) close(fd);
		return (-1);
	}
	entries_perrec = ir.i_entry[0].ie_dumpid;
	instance_recsize = ir.i_entry[0].ie_dnode_index;
	if (instance_recsize == 0) {
		(void) fprintf(stderr, gettext(
			"cannot get instance_recsize\n"));
		(void) close(fd);
		return (-1);
	}
	(void) close(fd);
	return (0);
}

/*
 * apply transactions to the given file
 */
static int
#ifdef __STDC__
dotrans(const char *host,
	const char *file,
	int recsize,
	char *localblockmap,
	int bmapsize)
#else
dotrans(host, file, recsize, localblockmap, bmapsize)
	char *host;
	char *file;
	int recsize;
	char *localblockmap;
	int bmapsize;
#endif
{
	FILE *fp;
	char transfile[256], mapfile[256];
	struct stat stbuf;
	register int i;
	char *trec, *fb;

	(void) sprintf(transfile, "%s/%s%s", host, file, TRANS_SUFFIX);
	(void) sprintf(mapfile, "%s/%s%s", host, file, MAP_SUFFIX);
	if ((fp = fopen(transfile, "r")) == (FILE *)0) {
		(void) fprintf(stderr, gettext("%s: cannot open '%s'\n"),
			"dotrans", transfile);
		return (-1);
	}
	if (fstat(fileno(fp), &stbuf) == -1) {
		perror("stat");
		(void) fprintf(stderr, gettext("%s: cannot stat `%s'\n"),
			"dotrans", transfile);
		(void) fclose(fp);
		return (-1);
	}
	if (stbuf.st_size == 0) {
		(void) fclose(fp);
		(void) unlink(transfile);
		(void) unlink(mapfile);
		return (0);
	}
	if (trans_open(host, file, stbuf.st_size)) {
		(void) fclose(fp);
		return (-1);
	}
	if (localblockmap == (char *)0) {
		int mfd;

		/*
		 * when called from startup() we don't have a map
		 * of dirty blocks.
		 */
		if ((mfd = open(mapfile, O_RDONLY)) == -1) {
			perror("dotrans: mapfile open");
			(void) fclose(fp);
			trans_close();
			return (-1);
		}
		if (fstat(mfd, &stbuf) == -1) {
			perror("dotrans: mapfile stat");
			(void) close(mfd);
			trans_close();
			(void) fclose(fp);
			return (-1);
		}
		bmapsize = stbuf.st_size;
		if (bmapsize) {
			localblockmap = (char *)malloc((unsigned)bmapsize);
			if (localblockmap == (char *)0) {
				(void) fprintf(stderr,
				    gettext("%s: cannot allocate map\n"),
					"dotrans");
				(void) close(mfd);
				trans_close();
				(void) fclose(fp);
				return (-1);
			}
			if (read(mfd, localblockmap, bmapsize) != bmapsize) {
				perror("dotrans: mapfile read");
				(void) close(mfd);
				trans_close();
				(void) fclose(fp);
				free(localblockmap);
				return (-1);
			}
		}
		(void) close(mfd);
	}
	if ((trec = (char *)malloc((unsigned)recsize)) == (char *)0) {
		(void) fprintf(stderr,
			gettext("%s: cannot allocate buffer\n"), "dotrans");
		trans_close();
		(void) fclose(fp);
		if (bmapsize)
			free(localblockmap);
		return (-1);
	}
	for (i = 0; i < bmapsize; i++) {
		if (localblockmap[i] == 0)
			continue;
		if (fseek(fp, (long)(i*recsize), 0)) {
			(void) fprintf(stderr,
				gettext("cannot seek in transaction file\n"));
			trans_close();
			(void) fclose(fp);
			free(trec);
			free(localblockmap);
			return (-1);
		}
		if (fread(trec, recsize, 1, fp) != 1) {
			(void) fprintf(stderr,
				gettext("cannot read transaction block\n"));
			trans_close();
			(void) fclose(fp);
			free(trec);
			free(localblockmap);
			return (-1);
		}
		fb = trans_getblock((u_long)i, recsize);
		bcopy(trec, fb, recsize);
	}
	if (bmapsize)
		free(localblockmap);
	free(trec);
	(void) fclose(fp);
	trans_close();
	(void) unlink(mapfile);
	(void) unlink(transfile);
	return (0);
}

static int
#ifdef __STDC__
trans_open(const char *host,
	const char *file,
	off_t size)
#else
trans_open(host, file, size)
	char *host;
	char *file;
	off_t size;
#endif
{
	char file_name[256];

	if (strcmp(file, TAPEFILE) == 0)
		host = ".";
	(void) sprintf(file_name, "%s/%s", host, file);
	if ((fd = open(file_name, O_RDWR)) == -1) {
		if (errno == ENOENT) {
			if ((fd = open(file_name,
					O_RDWR|O_CREAT, 0600)) == -1) {
				perror(file_name);
				return (-1);
			}
		} else {
			return (-1);
		}
	}
	if (ftruncate(fd, size) == -1) {
		perror("trans_open/ftruncate");
		(void) close(fd);
		fd = -1;
		return (-1);
	}
	return (0);
}

static caddr_t map;
static caddr_t fmb;
static u_long map_firstblock, map_maxblock;

static void
#ifdef __STDC__
trans_close(void)
#else
trans_close()
#endif
{
	if (map) {
		release_map(map, 1);
		map = (caddr_t)0;
		fmb = (caddr_t)0;
		map_firstblock = map_maxblock = 0;
	}
	if (fsync(fd) == -1)
		perror("trans_close/fsync");
	if (close(fd) == -1)
		perror("trans_close/close");
	fd = -1;
}

static caddr_t
#ifdef __STDC__
trans_getblock(u_long num, int recsize)
#else
trans_getblock(num, recsize)
	u_long num;
	int recsize;
#endif
{
	register caddr_t p;

	p = (caddr_t)getmapblock(&map, &fmb, &map_firstblock,
			&map_maxblock, num, recsize, fd,
			PROT_READ|PROT_WRITE, 1, (int *)0);
	return (p);
}
