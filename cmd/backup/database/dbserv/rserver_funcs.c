#ident	"@(#)rserver_funcs.c 1.19 94/08/10"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "defs.h"
#define	_POSIX_SOURCE   /* hack to avoid redef of MAXNAMLEN */
#define	_POSIX_C_SOURCE
#include <dirent.h>
#undef	_POSIX_C_SOURCE
#undef	_POSIX_SOURCE
#include <sys/stat.h>
#include <database/handles.h>
#include "rpcdefs.h"
#include "dboper.h"

#ifdef __STDC__
static int header_search(struct fsheader_readargs *, struct dheader *, int);
static void lockdb(void);
static void unlockdb(void);
static void addupdatepid(int);
static int getfd(const char *);
#else
static int header_search();
static void lockdb();
static void unlockdb();
static void addupdatepid();
static int getfd();
#endif

int *
start_update_1(host)
	char **host;
{
	int hid;
	static int rc = -1;

	/*
	 * XXX: must ensure that we get a unique timestamp!
	 */
	(void) sleep(1);
	hid = (int)time((time_t *)0);
	if (new_handle(hid, *host) == NULL_HANDLE) {
		(void) fprintf(stderr, gettext("%s error\n"), "new_handle");
		return (&rc);
	}
	rc = hid;
	return (&rc);
}

int *
process_update_1(p)
	struct process *p;
{
	int handle;
	char oldname[256], newname[256];
	struct file_handle *h;
	static int rc = -1;
	int pid;
	extern int updatecnt;

	handle = p->handle;
	if ((h = handle_lookup(handle)) == NULL_HANDLE) {
		(void) fprintf(stderr, "process_update/handle_lookup\n");
		return (&rc);
	}

	/*
	 * XXX: attempt to verify the file here.  We'd like to ensure
	 * that the file contains at least something resembling good
	 * data and that we'll be able to write all the temp files
	 * and junk for the given host before we tell the caller
	 * that everything's OK.
	 */

	(void) sprintf(oldname, "%s/%s%s.%d", h->host,
			TEMP_PREFIX, UPDATE_FILE, handle);
	(void) sprintf(newname, "%s/%s.%d", h->host, UPDATE_FILE, handle);
	if (rename(oldname, newname) == -1) {
		perror("process_update: rename");
		return (&rc);
	}
	if ((pid = fork()) == -1) {
		perror("fork");
		return (&rc);
	} else if (pid == 0) {
		yp_unbind(mydomain);
		closefiles();
		(void) oper_init(opserver, myname, 0);

		/*
		 * lock the database.  Is it important that updates
		 * be processed in the order they're received (i.e.,
		 * must the lock be acquired in request order)?
		 */
		lockdb();
		cleanup();

		if (!duplicate_dump(h->host, newname)) {
			if (batch_update(h->host, newname)) {
				(void) fprintf(stderr, gettext(
					"%s error\n"), "batch_update");
				/* XXX ??? */
			}
		}
		unlockdb();
		(void) unlink(newname);
		oper_end();
		exit(0);
	} else {
		addupdatepid(pid);
		updatecnt++;
		free_handle(h);
		rc = 0;
		return (&rc);
	}
	/*NOTREACHED*/
}

struct readdata *
read_dir_1(p)
	struct blk_readargs *p;
{
	static struct readdata r;
	static struct dir_block d;
	char dirfile[256];
	int fd;
	extern time_t dbupdatetime;

	r.retdata = NULL;
	(void) sprintf(dirfile, "%s/%s", p->host, DIRFILE);
	if ((fd = getfd(dirfile)) == -1) {
		perror(dirfile);
		r.readrc = DBREAD_NOHOST;
		return (&r);
	}
#if 0	/* DEBUG */
{
	struct stat stbuf;
	if (fstat(fd, &stbuf) == -1) {
		perror("fstat/dir");
		r.readrc = DBREAD_NOHOST;
		return (&r);
	}
	if (p->blksize != DIR_BLKSIZE) {
		r.readrc = DBREAD_USERERROR;
		return (&r);
	}
	if (p->recnum*p->blksize > stbuf.st_size) {
		(void) fprintf(stderr,
			gettext("%s: read past EOF\n"), "read_dir");
		r.readrc = DBREAD_USERERROR;
		return (&r);
	}
}
#endif
	if (dbupdatetime > p->cachetime) {
		/*
		 * let caller know his cache is stale
		 */
		r.readrc = DBREAD_NEWDATA;
		return (&r);
	}
	if (lseek(fd, (off_t)(p->recnum*p->blksize), SEEK_SET) == -1) {
		perror("lseek");
		r.readrc = DBREAD_INTERNALERROR;
		return (&r);
	}
	if (read(fd, (char *)&d, sizeof (struct dir_block)) == -1) {
		perror("read");
		r.readrc = DBREAD_INTERNALERROR;
		return (&r);
	}
	r.retdata = (char *)&d;
	r.readrc = DBREAD_SUCCESS;
	return (&r);
}

struct readdata *
read_inst_1(p)
	struct blk_readargs *p;
{
	static struct readdata r;
	/* large enough to hold a 50 entry instance record */
	static char i[COMPUTE_INST_RECSIZE(50)];
	char instfile[256];
	int fd;
	extern time_t dbupdatetime;

	r.retdata = NULL;
	if (p->blksize > sizeof (i)) {
		r.readrc = DBREAD_INTERNALERROR;
		return (&r);
	}
	if (dbupdatetime > p->cachetime) {
		/*
		 * tell caller his cache is outdated
		 */
		r.readrc = DBREAD_NEWDATA;
		return (&r);
	}
	(void) sprintf(instfile, "%s/%s", p->host, INSTANCEFILE);
	if ((fd = getfd(instfile)) == -1) {
		perror(instfile);
		r.readrc = DBREAD_NOHOST;
		return (&r);
	}
#if 0	/* DEBUG */
{
	struct stat stbuf;
	struct instance_record dummy;

	if (fstat(fd, &stbuf) == -1) {
		perror("fstat/instance");
		r.readrc = DBREAD_INTERNALERROR;
		return (&r);
	}
	if (read(fd, &dummy, sizeof (struct instance_record)) !=
				sizeof (struct instance_record)) {
		(void) fprintf(stderr,
			gettext("%s: cannot get blocksize\n"), "read_instance");
		r.readrc = DBREAD_INTERNALERROR;
		return (&r);
	}
	if (dummy.i_entry[0].ie_dnode_index != p->blksize) {
		if (p->blksize != sizeof (struct instance_record) ||
				p->recnum != INSTANCE_FREEREC) {
			(void) fprintf(stderr, gettext(
				"%s: bad user block size\n"), "read_instance");
			(void) fprintf(stderr, "<%d,%d>\n",
				dummy.i_entry[0].ie_dnode_index, p->blksize);
			r.readrc = DBREAD_USERERROR;
			return (&r);
		}
	}
	if (p->recnum*p->blksize > stbuf.st_size) {
		(void) fprintf(stderr, gettext("%s: read past EOF\n"),
			"read_instance");
		r.readrc = DBREAD_USERERROR;
		return (&r);
	}
}
#endif
	if (lseek(fd, (off_t)(p->recnum*p->blksize), SEEK_SET) == -1) {
		perror("lseek");
		r.readrc = DBREAD_INTERNALERROR;
		return (&r);
	}
	if (read(fd, i, p->blksize) == -1) {
		perror("read");
		r.readrc = DBREAD_INTERNALERROR;
		return (&r);
	}
	r.retdata = i;
	r.blksize = p->blksize;
	r.readrc = DBREAD_SUCCESS;
	return (&r);
}

int *
delete_tape_1(label)
	char **label;
{
	static int rc;

	lockdb();
	rc = delete_tape(*label);
	unlockdb();
	return (&rc);
}

struct readdata *
read_dnode_1(p)
	struct dnode_readargs *p;
{
	static struct readdata r;
	static struct dnode d;
	char dnodefile[256];
	int fd;

	r.retdata = NULL;
	(void) sprintf(dnodefile, "%s/%s.%lu", p->host, DNODEFILE, p->dumpid);
	if ((fd = open(dnodefile, O_RDONLY)) == -1) {
		r.readrc = DBREAD_NODUMP;
		perror(dnodefile);
		return (&r);
	}
#if 0	/* DEBUG */
{
	struct stat stbuf;

	if (fstat(fd, &stbuf) == -1) {
		perror("fstat/dir");
		(void) close(fd);
		r.readrc = DBREAD_INTERNALERROR;
		return (&r);
	}
	if (p->recnum*sizeof (struct dnode) > stbuf.st_size) {
		(void) fprintf(stderr, gettext("%s: read past EOF\n"),
			"read_dnode");
		(void) close(fd);
		r.readrc = DBREAD_USERERROR;
		return (&r);
	}
}
#endif
	if (lseek(fd, (off_t)(p->recnum*sizeof (struct dnode)),
						SEEK_SET) == -1) {
		perror("lseek");
		(void) close(fd);
		r.readrc = DBREAD_INTERNALERROR;
		return (&r);
	}
	if (read(fd, (char *)&d, sizeof (struct dnode)) == -1) {
		perror("read");
		(void) close(fd);
		r.readrc = DBREAD_INTERNALERROR;
		return (&r);
	}
	r.retdata = (char *)&d;
	r.readrc = DBREAD_SUCCESS;
	(void) close(fd);
	return (&r);
}

struct readdata *
read_dnodeblk_1(p)
	struct dnode_readargs *p;
{
	static struct readdata r;
	static struct dnode d[DNODE_READBLKSIZE];
	char dnodefile[256];
	int fd;

	r.retdata = NULL;
	(void) sprintf(dnodefile, "%s/%s.%lu", p->host, DNODEFILE, p->dumpid);
	if ((fd = getfd(dnodefile)) == -1) {
		r.readrc = DBREAD_NODUMP;
		perror(dnodefile);
		return (&r);
	}
#if 0	/* DEBUG */
{
	struct stat stbuf;

	if (fstat(fd, &stbuf) == -1) {
		perror("fstat/dir");
		r.readrc = DBREAD_INTERNALERROR;
		return (&r);
	}
	if (p->recnum*sizeof (struct dnode) > stbuf.st_size) {
		(void) fprintf(stderr, gettext("%s: read past EOF\n"),
			"read_dnodeblk");
		r.readrc = DBREAD_USERERROR;
		return (&r);
	}
}
#endif
	if (lseek(fd, (off_t)(p->recnum*sizeof (struct dnode)),
							SEEK_SET) == -1) {
		perror("lseek");
		r.readrc = DBREAD_INTERNALERROR;
		return (&r);
	}
	if (read(fd, (char *)d,
			DNODE_READBLKSIZE*sizeof (struct dnode)) == -1) {
		perror("read");
		r.readrc = DBREAD_INTERNALERROR;
		return (&r);
	}
	r.readrc = DBREAD_SUCCESS;
	r.retdata = (char *)d;
	return (&r);
}

struct readdata *
read_linkval_1(p)
	struct dnode_readargs *p;
{
	static struct readdata r;
	char linkfile[256];
	static char linkval[MAXPATHLEN];
	FILE *fp;
	register char *s;

	r.retdata = NULL;
	(void) sprintf(linkfile, "%s/%s.%lu", p->host, LINKFILE, p->dumpid);
	if ((fp = fopen(linkfile, "r")) == NULL) {
		r.readrc = DBREAD_NODUMP;
		perror(linkfile);
		return (&r);
	}
	if (fseek(fp, (long)p->recnum, 0) == -1) {
		perror("fseek");
		(void) fclose(fp);
		r.readrc = DBREAD_INTERNALERROR;
		return (&r);
	}
	for (s = linkval; *s = getc(fp); s++)
		;
	r.retdata = linkval;
	r.readrc = DBREAD_SUCCESS;
	(void) fclose(fp);
	return (&r);
}

struct readdata *
read_header_1(p)
	struct header_readargs *p;
{
	static struct readdata r;
	static struct dheader dh;
	char filename[256];
	int fd;

	r.retdata = NULL;
	(void) sprintf(filename, "%s/%s.%lu", p->host, HEADERFILE, p->dumpid);
	if ((fd = open(filename, O_RDONLY)) == -1) {
		perror(filename);
		r.readrc = DBREAD_NODUMP;
		return (&r);
	}
	if (read(fd, (char *)&dh, sizeof (struct dheader)) == -1) {
		perror("read");
		(void) close(fd);
		r.readrc = DBREAD_INTERNALERROR;
		return (&r);
	}
	r.retdata = (char *)&dh;
	r.readrc = DBREAD_SUCCESS;
	(void) close(fd);
	return (&r);
}

struct readdata *
read_fullheader_1(p)
	struct header_readargs *p;
{

#define	MAXLABELS	20	/* dump never spans more tapes than this! */
	static struct {
		struct dheader d;
		char	labels[MAXLABELS][LBLSIZE];
	} dh;
	static struct readdata r;
	char filename[256];
	int fd;
	struct stat stbuf;

	r.retdata = NULL;
	(void) sprintf(filename, "%s/%s.%lu", p->host, HEADERFILE, p->dumpid);
	if ((fd = open(filename, O_RDONLY)) == -1) {
		perror(filename);
		r.readrc = DBREAD_NODUMP;
		return (&r);
	}
	if (fstat(fd, &stbuf) == -1) {
		perror("fstat");
		(void) close(fd);
		r.readrc = DBREAD_INTERNALERROR;
		return (&r);
	}
	if (stbuf.st_size > sizeof (dh)) {
		(void) fprintf(stderr, gettext(
			"%s: buffer too small\n"), "read_fullheader");
		(void) close(fd);
		r.readrc = DBREAD_INTERNALERROR;
		return (&r);
	}
	if (read(fd, (char *)&dh, (int)stbuf.st_size) == -1) {
		perror("read");
		(void) close(fd);
		r.readrc = DBREAD_INTERNALERROR;
		return (&r);
	}
	r.retdata = (char *)&dh;
	r.readrc = DBREAD_SUCCESS;
	(void) close(fd);
	return (&r);
}

struct readdata *
read_fsheader_1(p)
	struct fsheader_readargs *p;
{
	static struct readdata r;
	static struct dheader dh;

	dh.dh_host[0] = '\0';
	dh.dh_time = 0;
	r.retdata = NULL;
	r.readrc = header_search(p, &dh, sizeof (dh));
	if (r.readrc == DBREAD_SUCCESS) {
		r.retdata = (char *)&dh;
	}
	return (&r);
}

struct readdata *
read_fullfsheader_1(p)
	struct fsheader_readargs *p;
{
	static struct readdata r;
	static struct {
		struct dheader d;
		char	labels[MAXLABELS][LBLSIZE];
	} dh;

	r.retdata = NULL;
	dh.d.dh_host[0] = '\0';
	dh.d.dh_time = 0;
	r.readrc = header_search(p, (struct dheader *)&dh, sizeof (dh));
	if (r.readrc == DBREAD_SUCCESS)
		r.retdata = (char *)&dh;
	return (&r);
}

static int
header_search(p, dhp, size)
	struct fsheader_readargs *p;
	struct dheader *dhp;
	int size;
{
	DIR *dirp;
	struct dheader *hold;
	struct dirent *ep;
	char fullname[1024];
	int fd, gotone, rc;

	gotone = 0;
	rc = DBREAD_NODUMP;
	hold = (struct dheader *)malloc((unsigned)size);
	if (!hold) {
		(void) fprintf(stderr,
			gettext("%s: out of memory\n"), "header_search");
		return (DBREAD_INTERNALERROR);
	}
	if ((dirp = opendir(p->host)) == NULL) {
		(void) fprintf(stderr, gettext(
			"cannot open host directory for %s\n"), p->host);
		free((char *)hold);
		return (DBREAD_NOHOST);
	}
	while (ep = readdir(dirp)) {
		if (strncmp(ep->d_name, HEADERFILE, strlen(HEADERFILE)))
			continue;
		(void) sprintf(fullname, "%s/%s", p->host, ep->d_name);
		if ((fd = open(fullname, O_RDONLY)) == -1) {
			perror(fullname);
			continue;
		}
		if (read(fd, (char *)hold, size) == -1) {
			perror("read");
			(void) close(fd);
			continue;
		}
		if (strcmp(p->mntpt, hold->dh_mnt) == 0) {
			if (hold->dh_time <= p->time) {
				if (hold->dh_time > dhp->dh_time) {
					gotone++;
					bcopy((char *)hold, (char *)dhp, size);
				}
			}
		}
		(void) close(fd);
	}
	(void) closedir(dirp);
	free((char *)hold);
	if (gotone)
		rc = DBREAD_SUCCESS;
	return (rc);
}

struct readdata *
read_tape_1(p)
	struct tape_readargs *p;
{
	int fd;
	static struct active_tape t;
	static struct readdata r;

	r.retdata = NULL;
	if ((fd = open(TAPEFILE, O_RDONLY)) == -1) {
		perror(TAPEFILE);
		r.readrc = DBREAD_NOTAPE;
		return (&r);
	}

	if (lseek(fd, (off_t)(TAPE_FIRSTDATA*sizeof (struct active_tape)),
				SEEK_SET) == -1) {
		perror("lseek");
		(void) close(fd);
		r.readrc = DBREAD_INTERNALERROR;
		return (&r);
	}

	while (read(fd, (char *)&t, sizeof (struct active_tape)) ==
				sizeof (struct active_tape)) {
		if (bcmp(t.tape_label, p->label, LBLSIZE) == 0) {
			r.readrc = DBREAD_SUCCESS;
			r.retdata = (char *)&t;
			(void) close(fd);
			return (&r);
		}
	}

	r.readrc = DBREAD_NOTAPE;
	(void) close(fd);
	return (&r);
}

#include <sys/file.h>

#ifndef LOCK_SH
#define	LOCK_SH		1	/* shared lock */
#define	LOCK_EX		2	/* exclusive lock */
#define	LOCK_NB		4	/* don't block when locking */
#define	LOCK_UN		8	/* unlock */
#endif

#if defined(USG) && !defined(FLOCK)
/*
 * Trump up a version of flock based on fcntl.
 * You don't need this if your implementation
 * has flock -- just compile with -DFLOCK.
 */

static int
flock(fd, operation)
	int	fd;
	int	operation;
{
	struct flock fl;
	int block = 0;
	int status;

	if (operation & LOCK_SH)
		fl.l_type = F_RDLCK;
	else if (operation & LOCK_EX)
		fl.l_type = F_WRLCK;
	else if (operation & LOCK_UN)
		fl.l_type = F_UNLCK;
	if ((operation & LOCK_NB) == 0)
		block = 1;
	fl.l_whence = SEEK_SET;	/* XXX ? */
	fl.l_start = (off_t)0;
	fl.l_len = (off_t)0;	/* till EOF */
	while ((status = fcntl(fd, F_SETLK, (char *)&fl)) < 0 &&
	    (errno == EACCES || errno == EAGAIN) && block) {
		(void) sleep(1);
	}
	return (status);
}
#endif

static int lockfd = -1;

static void
#ifdef __STDC__
lockdb(void)
#else
lockdb()
#endif
{
	if (lockfd != -1) {
		(void) fprintf(stderr,
			gettext("%s: already locked?\n"), "lockdb");
	}
	if ((lockfd = open(DBSERV_LOCKFILE, O_RDWR|O_CREAT, 0600)) == -1) {
		perror("lockdb/open");
		exit(1);
	}
	if (flock(lockfd, LOCK_EX) == -1) {
		perror("lockdb/flock");
	}
}

static void
#ifdef __STDC__
unlockdb(void)
#else
unlockdb()
#endif
{
	if (flock(lockfd, LOCK_UN) == -1) {
		perror("unlockdb/flock");
		exit(1);
	}
	(void) close(lockfd);
	lockfd = -1;
}

static int readlk_fd = -1;

#ifdef __STDC__
getreadlock(void)
#else
getreadlock()
#endif
{
	if (readlk_fd != -1) {
		(void) fprintf(stderr, gettext(
		    "process %ld readlock?\n"), (long)getpid());
	}
	if ((readlk_fd = open(DBSERV_LOCKFILE, O_RDWR|O_CREAT, 0600)) == -1) {
		perror("getreadlock/open");
		return (0);
	}
	if (flock(readlk_fd, LOCK_SH|LOCK_NB) == -1) {
		if (errno != EACCES && errno != EAGAIN) {
			perror("getreadlock/flock");
		}
		(void) close(readlk_fd);
		readlk_fd = -1;
		return (0);
	}
	return (1);
}

void
#ifdef __STDC__
releasereadlock(void)
#else
releasereadlock()
#endif
{
	if (flock(readlk_fd, LOCK_UN) == -1) {
		perror("releasereadlock/flock");
	}
	(void) close(readlk_fd);
	readlk_fd = -1;
}

static struct pidlist {
	int pid;
	struct pidlist *nxt;
} *pid_upd_list, *pid_free_list;

isupdatepid(pid)
	int pid;
{
	register struct pidlist *t, *prv;

	prv = (struct pidlist *)0;
	for (t = pid_upd_list; t; t = t->nxt) {
		if (t->pid == pid) {
			if (prv) {
				prv->nxt = t->nxt;
			} else {
				pid_upd_list = t->nxt;
			}
			t->nxt = pid_free_list;
			pid_free_list = t;
			return (1);
		}
		prv = t;
	}
	return (0);
}

static void
addupdatepid(pid)
	int pid;
{
	register struct pidlist *t;

	t = pid_free_list;
	if (t) {
		pid_free_list = t->nxt;
	} else {
		t = (struct pidlist *)malloc(sizeof (struct pidlist));
		if (t == (struct pidlist *)0) {
			(void) fprintf(stderr,
			    gettext("%s: out of memory\n"), "addupdatepid");
			exit(0);
		}
	}
	t->nxt = pid_upd_list;
	pid_upd_list = t;
	t->pid = pid;
}

/*
 * keep a cache of open file descriptors.
 * Sub-processes call `closefiles()' as part of their startup
 * activity.
 */
#define	MAXFDS	15	/* for the cache - much smaller than NOFILE */
static int nfds;
static struct holdfd {
	char *name;
	int  fd;
	struct holdfd *nxt;
} *allfds;

static int
#ifdef __STDC__
getfd(const char *name)
#else
getfd(name)
	char *name;
#endif
{
	int newfd;
	register struct holdfd *p, *last, *prv;

	prv = last = (struct holdfd *)0;
	for (p = allfds; p; p = p->nxt) {
		if (strcmp(p->name, name) == 0) {
			return (p->fd);
		}
		prv = last;
		last = p;
	}

	if ((newfd = open(name, O_RDONLY)) == -1)
	    return (-1);
	if (nfds < MAXFDS) {
		nfds++;
		p = (struct holdfd *)malloc(sizeof (struct holdfd));
		if (!p) {
			(void) fprintf(stderr,
				gettext("%s: out of memory\n"), "getfd");
			(void) close(newfd);
			return (-1);
		}
		p->name = malloc(strlen(name) + 1);
		if (!p->name) {
			(void) fprintf(stderr,
				gettext("%s: out of memory\n"), "getfd");
			(void) close(newfd);
			free((char *)p);
			return (-1);
		}
		(void) strcpy(p->name, name);
		p->nxt = allfds;
		allfds = p;
		p->fd = newfd;
		return (p->fd);
	} else {
		/*
		 * re-use the file that was least recently opened
		 * (least recently used would be even better...)
		 */
		if (!(last && prv)) {
			(void) fprintf(stderr, gettext(
				"%s: %s error!\n"), "getfd", "last/prv");
			(void) close(newfd);
			return (-1);
		}
		free(last->name);
		last->name = malloc(strlen(name) + 1);
		if (!last->name) {
			(void) fprintf(stderr,
				gettext("%s: out of memory\n"), "getfd");
			(void) close(newfd);
			return (-1);
		}
		(void) close(last->fd);
		(void) strcpy(last->name, name);
		prv->nxt = (struct holdfd *)0;
		last->nxt = allfds;
		allfds = last;
		last->fd = newfd;
		return (last->fd);
	}
}

void
#ifdef __STDC__
closefiles(void)
#else
closefiles()
#endif
{
	register struct holdfd *p, *t;

	p = allfds;
	while (p) {
		(void) close(p->fd);
		free(p->name);
		t = p;
		p = p->nxt;
		free((char *)t);
	}
	allfds = (struct holdfd *)0;
	nfds = 0;
}
