#ident	"@(#)map_subr.c 1.8 94/08/10"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "defs.h"
#include <sys/mman.h>
#include "dboper.h"

static long pagesize;
static size_t mapsize;

char *
getmapblock(map, recp, firstrec, lastrec, reqrec, recsize,
			fd, prot, syncit, filesize)
	char **map;
	char **recp;
	u_long	*firstrec;
	u_long *lastrec;
	u_long reqrec;
	int recsize;
	int fd;
	int prot;
	int syncit;
	int *filesize;
{
	register int diff, offset;
	register char *retp;
	extern int nmapblocks;
	char msgbuf[MAXMSGLEN];

	if (pagesize == 0)
		pagesize = getpagesize();

	if (mapsize == 0) {
		if (nmapblocks)
			mapsize = nmapblocks*MAPSIZE;
		else
			mapsize = MAPSIZE;
	}

	if (*recp) {
		if (reqrec >= *firstrec && reqrec <= *lastrec) {
			retp = *recp + ((reqrec - *firstrec) * recsize);
			return (retp);
		} else {
			if (syncit) {
				if (msync(*map, mapsize, MS_ASYNC))
					perror("getmapblock/msync");
			}
			if (munmap(*map, mapsize))
				perror("getmapblock/munmap");
		}
	}

	offset = reqrec * recsize;

	if (filesize) {
		if ((offset + mapsize) > *filesize) {
			*filesize = offset + mapsize;
			if (ftruncate(fd, (off_t)*filesize) == -1) {
				perror("ftruncate");
				(void) fprintf(stderr,
				    gettext("%s: cannot extend file\n"),
					"getmapblock");
				exit(1);
			}
		}
	}

	if ((diff = (offset % pagesize)) != 0)
		offset -= diff;

	*firstrec = reqrec;
	*lastrec = reqrec + ((mapsize - diff) / recsize) - 1;

	*map = mmap((caddr_t)0, mapsize, prot, MAP_SHARED, fd, (off_t)offset);
	if (*map == (caddr_t)-1) {
		(void) sprintf(msgbuf, gettext(
			"Errno %d on database mmap, update aborted\n"), errno);
		(void) oper_send(DBOPER_TTL, LOG_NOTICE, DBOPER_FLAGS, msgbuf);
		perror("getmapblock/mmap");
		exit(1);
	}
	retp = *recp = ((char *)*map + diff);
	return (retp);
}

void
release_map(map, syncit)
	char *map;
	int syncit;
{
	if (syncit) {
		if (msync(map, mapsize, 0))
			perror("release_map/msync");
	}
	if (munmap(map, mapsize))
		perror("release_map/munmap");
}

static caddr_t rmname = NULL; /* used by next two functions */

void
#ifdef __STDC__
startupreg(const char *name)
#else
startupreg(name)
	char *name;
#endif
{
	int mapfd;

	if (rmname == NULL) {
		if ((mapfd = open("/dev/zero", O_RDWR)) < 0)
			return; /* no printed error message (yet?) */
		rmname = mmap((caddr_t) 0, MAXPATHLEN,
			PROT_READ | PROT_WRITE, MAP_SHARED, mapfd, (off_t) 0);
		(void) close(mapfd);
		if (rmname == (caddr_t) -1) {
			/* no printed error message (yet?) */
			rmname = NULL;
			return;
		}
	}

	rmname[0] = '\0';

	if (name == NULL)
		return;

	if ((int) strlen(name) < MAXPATHLEN)
		(void) strcpy(rmname, name);
}

int
#ifdef __STDC__
startupunlink(void)
#else
startupunlink()
#endif
{
	int rc = -1;

	if ((rmname != NULL) && (rmname[0] != '\0')) {
		rc = unlink(rmname);
		rmname[0] = '\0';
		if (rc == 0)
			(void) unlink("core");
	}
	return (rc);
}
