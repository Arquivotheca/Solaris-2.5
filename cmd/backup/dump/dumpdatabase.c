/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)dumpdatabase.c	1.58	94/08/10 SMI"

#include "dump.h"
#include <config.h>
#include <database/batchfile.h>
#include <database/header.h>
#include <database/dnode.h>
#if defined(USG) && defined(MAXNAMLEN)
#undef	MAXNAMLEN	/* yecch */
#endif
#include <sys/types.h>
#include <sys/param.h>
#include <unistd.h>
#include <dirent.h>

/* "dump." + 5 char PID + "." + 8 char hostid + "." + mmddhhmmss + \0 */
#define	DBFILENAMELEN	31

#ifdef DEBUG
#define	DBFILEMODE	0644
#else
#define	DBFILEMODE	0600
#endif

/*
 * For error processing...
 */
enum dbfop {
	dbf_create = 0,
	dbf_open = 1,
	dbf_write = 2,
	dbf_read = 3,
	dbf_seek = 4,
	dbf_trunc = 5
};

#define	SENDOPER	1
#define	DONTSEND	0

/*
 * This structure contains the information
 * necessary to checkpoint/restart the
 * database data file.
 */
struct db_chkpt {
	ino_t  db_lastinumber;			/* current inode number */
	struct dnode db_lastdnode;		/* current dnode data */
	off_t	db_written;			/* symlink bytes written */
	off_t  db_offset;			/* offset within data file */
	struct bu_header db_hdr;		/* file header */
	struct bu_tape db_tapelist[1];		/* tapes; can be longer */
};
static struct db_chkpt	*dbchkpt;

static struct bu_tape *tapelist[TP_NINDIR];	/* tape descriptor list */
static struct bu_header	filehdr;		/* temp file header record */
static ino_t lastino;				/* last inode number */
static struct dnode dn;				/* current dnode */
static int written;				/* symlink bytes written */
static char *tmpdir;				/* where tmp file goes */

static u_long	dirsiz;		/* number of TP_BSIZE blocks needed for dirs */
static u_long	files;		/* number of files in this dumpset */

#ifdef __STDC__
static void	dbferror(enum dbfop, int);
static int	iscomplete(int);
static int	isdbtmp(char *);
static void	recover_mode(void);
static void	updatedump(int);
static void	writetapes(int);
#ifdef USG
static long	gethostid(void);
#endif
#else
static void	dbferror();
static int	iscomplete();
static int	isdbtmp();
static void	recover_mode();
static void	updatedump();
static void	writetapes();
#endif

static void
#ifdef __STDC__
dbferror(operation, dosend)
#else
dbferror()
#endif
	enum dbfop operation;
	int	dosend;		/* msg goes to operator msg system if == 1 */
{
	char *text;
	int save_errno = errno;
	int badop = 0;

	switch ((int)operation) {
	case dbf_create:
		text = gettext(
		    "Cannot create database data file `%s': %s\n");
		break;
	case dbf_open:
		text = gettext(
		    "Cannot open database data file `%s': %s\n");
		break;
	case dbf_write:
		text = gettext(
		    "Cannot write to database data file `%s': %s\n");
		break;
	case dbf_read:
		text = gettext(
		    "Cannot read from database data file `%s': %s\n");
		break;
	case dbf_seek:
		text = gettext(
		    "Cannot seek in database data file `%s': %s\n");
		break;
	case dbf_trunc:
		text = gettext(
		    "Cannot truncate database data file `%s': %s\n");
		break;
	default:
		text = gettext(
		    "Unknown operation on database data file `%s': `%d'\n");
		badop++;
		break;
	}

	if (badop == 0)
		msg(text, dbtmpfile, strerror(save_errno));
	else
		msg(text, dbtmpfile, operation);
	if (dosend) {
		if (badop == 0)
			(void) opermes(LOG_INFO, text,
				dbtmpfile, strerror(save_errno));
		else
			(void) opermes(LOG_INFO, text, dbtmpfile, operation);
	}
}

/*
 * doupdate -- update a dump database server with info
 *	from a dump.  Called after completing the copying
 *	associated with a dump, or in recovery mode (dump -R)
 *	at system boot).  To update with data from a specific
 *	dump, "name" contains the name of the appropriate tmp
 *	file; in recovery mode, "name" is NULL indicating the
 *	appropriate temporary directory should be searched
 *	for dump data files.
 */
void
doupdate(name)
	char	*name;	/* name of tmp file */
{
	int	maxfds = (int)sysconf(_SC_OPEN_MAX);
	register int fd;
#ifndef DEBUG
	int childpid;

	(void) signal(SIGHUP, SIG_IGN);
	/*
	 * Fork process to do the work
	 */
	if ((childpid = fork()) < 0) {
		msg(gettext("Database update fork fails in parent %d\n"),
		    getpid());
		Exit(X_ABORT);
	}
	/*
	 * Parent -- exits immediately, backgrounding child.
	 */
	if (childpid > 0)
		Exit(X_FINOK);
#endif

	/*
	 * Child starts here -- hangs around to do the actual update(s).
	 */

	if (setsid() < 0)
		(void) opermes(LOG_INFO, gettext(
	    "Warning - %s cannot exit until the database update completes\n"),
			myname);
	if (name == NULL)
		recover_mode();		/* recover mode */
	else
		updatedump(0);	/* leave incomplete files alone */
	Exit(X_FINOK);
}

/*
 * Recover mode.  The current temporary directory
 * is searched for valid database tmp files -- any
 * that are found are sent to the appropriate
 * server.
 */
static void
recover_mode()
{
	DIR *dirp;
	struct dirent *dp;
	char *tmpdir;

	(void) gethostname(spcl.c_host, NAMELEN);

	while (tmpdir = gettmpdir()) {
		(void) opermes(LOG_INFO, gettext(
		    "Recovering database files from directory `%s'\n"), tmpdir);
		if ((dirp = opendir(tmpdir)) == NULL) {
			(void) opermes(LOG_INFO, gettext(
			    "Cannot open temporary directory `%s': %s\n"),
			    tmpdir, strerror(errno));
			continue;
		}
		if (chdir(tmpdir) < 0) {
			(void) opermes(LOG_INFO, gettext(
			    "Cannot chdir to temporary directory `%s': %s\n"),
			    tmpdir, strerror(errno));
			(void) closedir(dirp);
			continue;
		}
		while (dp = readdir(dirp)) {
			if (!isdbtmp(dp->d_name))
				continue;
			dbtmpfile = dp->d_name;
			updatedump(1);	/* remove incomplete files */
		}
		(void) closedir(dirp);
	}
	(void) opermes(LOG_INFO, gettext("Database file recovery complete\n"));
}

static void
updatedump(remove)
	int	remove;		/* =1 if incomplete files are to be removed */
{
	struct	dheader	dh;	/* dump descriptor record */
	struct	stat sbuf;
	int	rc, handle, interval;
	int	success = 0;
	int	fd;

	fd = open(dbtmpfile, O_RDONLY);
	if (fd < 0) {
		dbferror(dbf_open, SENDOPER);
		if (remove)
			(void) opermes(LOG_INFO,
			    gettext("Removing database data file `%s'\n"),
				dbtmpfile);
		else
			(void) opermes(LOG_INFO,
			    gettext("Ignoring database data file `%s'\n"),
				dbtmpfile);
		goto done;
	}
	/*
	 * Check for file completeness
	 */
	if (!iscomplete(fd)) {
		if (remove)
			(void) opermes(LOG_INFO, gettext(
			    "Removing incomplete database data file `%s'\n"),
				dbtmpfile);
		else
			(void) opermes(LOG_INFO, gettext(
			    "Ignoring incomplete database data file `%s'\n"),
				dbtmpfile);
		goto done;
	}
	if (read(fd, (char *)&filehdr, sizeof (filehdr)) != sizeof (filehdr)) {
		dbferror(dbf_read, SENDOPER);
		goto done;
	}
	if (read(fd, (char *)&dh, sizeof (dh)) != sizeof (dh)) {
		dbferror(dbf_read, SENDOPER);
		goto done;
	}
	if (strncmp(dh.dh_host, spcl.c_host, NAMELEN))
		goto done;		/* only do this host's files */
	disk = dh.dh_dev;
	filesystem = dh.dh_mnt;
	if (lseek(fd, 0L, 0) < 0) {
		dbferror(dbf_seek, SENDOPER);
		goto done;
	}
	/*
	 * Check ownership - file must be
	 * owned by someone with read
	 * permission for dumped device
	 */
	if (fstat(fd, &sbuf) < 0 ||
	    isoperator((int)sbuf.st_uid, (int)sbuf.st_gid) <= 0) {
		if (remove)
			(void) opermes(LOG_INFO, gettext(
		    "Removing database data file `%s' owned by non-operator\n"),
				dbtmpfile);
		else
			(void) opermes(LOG_INFO, gettext(
		    "Ignoring database data file `%s' owned by non-operator\n"),
				dbtmpfile);
		goto done;
	}
	interval = 0;
	if (setreuid(-1, 0) < 0) {
		(void) opermes(LOG_INFO,
		    gettext("Cannot become super-user: %s\n"), strerror(errno));
		Exit(X_ABORT);
	}
	while (!success) {
		interval += 60;		/* linear back-off */
		handle = update_start(filehdr.dbhost, spcl.c_host);
		if (handle < 0) {
			/*
			 * wait for server to come up
			 */
			(void) opermes(LOG_INFO, gettext(
		    "Database server `%s' not responding still trying\n"),
				filehdr.dbhost);
			/*
			 * Release our hold on the operator daemon
			 * while we sleep.
			 */
			msgend();
			(void) sleep((unsigned)interval);
			msginit();
			continue;
		}
		(void) opermes(LOG_INFO, gettext(
		    "Transfer of data for `%s:%s' to server `%s' initiated\n"),
			spcl.c_host, filesystem, filehdr.dbhost);
		if ((rc = update_data(handle, dbtmpfile)) ||
		    update_process(handle)) {
			if (rc == -2) {
				(void) opermes(LOG_INFO, gettext(
			"Data for `%s:%s' on server `%s' already processed\n"),
				    spcl.c_host, filesystem, filehdr.dbhost);
				break;
			}
			(void) opermes(LOG_INFO, gettext(
		"Transfer of data for `%s:%s' to server `%s' interrupted\n"),
				spcl.c_host, filesystem, filehdr.dbhost);
			/*
			 * Release our hold on the operator daemon
			 * while we sleep.
			 */
			msgend();
			(void) sleep((unsigned)interval);
			msginit();
			continue;
		}
		(void) opermes(LOG_INFO, gettext(
		    "Transfer of data for `%s:%s' to server `%s' completed\n"),
			spcl.c_host, filesystem, filehdr.dbhost);
		success = 1;
	}
	(void) setreuid(-1, getuid());
	remove = 1;
done:
	(void) close(fd);
	if (remove)
		(void) unlink(dbtmpfile);
}

/*
 * A database tmp file is owned by root and looks like:
 *	bu_header
 *	...data...
 *	bu_header
 * Returns true (non-zero) if the file is owned by root
 * and is complete, false (zero) otherwise.  The file is
 * left positioned at its first byte.
 */
static int
iscomplete(fd)
	int	fd;
{
	struct bu_header trailer;
	off_t offset = -((long) sizeof (trailer));

	if (lseek(fd, (off_t)0, 0) < 0)	/* struct at beginning */
		return (0);
	if (read(fd, (char *)&filehdr, sizeof (filehdr)) != sizeof (filehdr))
		return (0);
	if (lseek(fd, offset, 2) < 0)	/* struct at end */
		return (0);
	if (read(fd, (char *)&trailer, sizeof (trailer)) != sizeof (trailer))
		return (0);
	/*
	 * leave file positioned at beginning of data
	 */
	if (lseek(fd, 0L, 0) < 0) {
		dbferror(dbf_seek, SENDOPER);
		dumpabort();
	}
	if (bcmp((char *)&filehdr, (char *)&trailer, sizeof (filehdr)) == 0)
		return (1);
	return (0);
}

void
creatdbtmp(time)
	time_t	time;
{
	struct statfs	statb;
	struct tm *tm;
	u_long freesz;
	u_long dbsize =			/* estimate database tmp file size */
	    (dirsiz +
	    howmany((files * sizeof (struct dnode)), TP_BSIZE)) *
	    (TP_BSIZE/DEV_BSIZE);	/* tape blocks to device blocks */

	if (tmpdir == NULL) {
		/*
		 * Walk the list of directories in "tmpdir" and find
		 * the first one suitable for use during this dump.
		 * The directory cannot reside on a file system that
		 * will be locked at any time during the dump and if
		 * database update is to be performed, its free space
		 * must exceed the estimated amount of space required
		 * to store the tmp file.
		 */
		while (tmpdir = gettmpdir()) {
			if (!okwrite(tmpdir, 0)) {
				msg(gettext(
			"Skipping temporary directory `%s': write-locked\n"),
				    tmpdir);
				continue;
			}
			if (statfs(tmpdir, &statb) < 0) {
				msg(gettext(
				    "Skipping temporary directory `%s': %s\n"),
					tmpdir, strerror(errno));
				continue;
			}
			freesz = statb.f_bfree * (statb.f_bsize / DEV_BSIZE);
			if (dbsize > freesz)
				msg(gettext(
				    "Skipping temporary directory `%s': \
not enough space on f/s (%lu Kb < %lu Kb)\n"),
				    tmpdir,
				    (freesz * DEV_BSIZE) / 1024,
				    (dbsize * DEV_BSIZE) / 1024);
			else
				break;
		}
		if (tmpdir == NULL) {
			msg(gettext(
				"Cannot find suitable temporary directory\n"));
			dumpabort();
		}
	}

	dbtmpfile = xmalloc((DBFILENAMELEN+strlen(tmpdir)+1));
	tm = localtime(&time);
	(void) sprintf(dbtmpfile, "%s/dump.%05d.%08lx.%02d%02d%02d%02d%02d",
	    tmpdir,
	    (int)getpid(),
	    (u_long)gethostid(),
	    tm->tm_mon+1,	/* XXX */
	    tm->tm_mday,
	    tm->tm_hour,
	    tm->tm_min,
	    tm->tm_sec);
}

int
#ifdef __STDC__
initdbtmp(void)
#else
initdbtmp()
#endif
{
	struct dheader	dh;	/* dump descriptor record */
	char	path[MAXPATHLEN];
	int	fd;

	fd = open(dbtmpfile, O_RDWR|O_CREAT|O_TRUNC, DBFILEMODE);
	if (fd < 0) {
		dbferror(dbf_create, DONTSEND);
		dumpabort();
	}
	/*
	 * The bu_header record will be used to propagate
	 * name and dnode counts across volumes
	 */
	(void) bzero((char *)&filehdr, sizeof (filehdr));
	(void) getdbserver(filehdr.dbhost, BCHOSTNAMELEN);
	errno = ENOSPC;
	if (write(fd, (char *)&filehdr, sizeof (filehdr)) != sizeof (filehdr)) {
		dbferror(dbf_write, DONTSEND);
		dumpabort();
	}
	/*
	 * Make dump header and write it out
	 */
	(void) bzero((char *)&dh, sizeof (dh));
	(void) strncpy(dh.dh_host, spcl.c_host, NAMELEN);
	dh.dh_netid = 0;
	(void) strcpy(dh.dh_dev, spcl.c_dev);
	if (realpath(filesystem, path))
		(void) strncpy(dh.dh_mnt, path, sizeof (dh.dh_mnt));
	else
		(void) strncpy(dh.dh_mnt, filesystem, sizeof (dh.dh_mnt));
	dh.dh_time = spcl.c_date;
	dh.dh_prvdumptime = spcl.c_ddate;
	dh.dh_level = spcl.c_level;
	dh.dh_flags =
	    (doingactive ? DH_ACTIVE : 0) | (trueinc ? DH_TRUEINC : 0);
#ifdef PARTIAL
	if (strstr(spcl.c_filesys, "partial"))
		dh.dh_flags |= DH_PARTIAL;
#endif
	dh.dh_position = filenum;
	dh.dh_ntapes = 0;
	errno = ENOSPC;
	if (write(fd, (char *)&dh, sizeof (dh)) != sizeof (dh)) {
		dbferror(dbf_write, DONTSEND);
		dumpabort();
	}
	lastino = ROOTINO;
	return (fd);
}

/*
 * Shared parent/child routine:  Open database file, initialize
 * state variables from the checkpointed state data.  The state
 * data is handed in as a caddr_t because the master treats it
 * as opaque data, i.e., a "magic cookie".
 */
int
opendbtmp(state)
	caddr_t	state;
{
	int	fd;
	off_t	offset;
	register int i;

	fd = open(dbtmpfile, O_RDWR);
	if (fd < 0) {
		dbferror(dbf_open, DONTSEND);
		dumpabort();
	}
	/*LINTED [state = malloc() and therefore aligned]*/
	dbchkpt = (struct db_chkpt *)state;
	lastino = dbchkpt->db_lastinumber;
	dn = dbchkpt->db_lastdnode;
	written = dbchkpt->db_written;
	offset = dbchkpt->db_offset;
	filehdr = dbchkpt->db_hdr;
	for (i = 0; i < filehdr.tape_cnt; i++)
		tapelist[i] = &dbchkpt->db_tapelist[i];
	if (lseek(fd, offset, 0) < 0) {
		dbferror(dbf_seek, DONTSEND);
		dumpabort();
	}
	if (ftruncate(fd, offset) < 0) {
		dbferror(dbf_trunc, DONTSEND);
		dumpabort();
	}
	return (fd);
}

/*
 * Child routine:  Checkpoint database file state
 * and place this information at the end of the
 * data file, followed by the size of the info.
 * Called by archiver, just before exiting.
 */
void
savedbtmp(fd)
	int	fd;
{
	register int i;
	size_t nbytes;

	nbytes = sizeof (struct bu_tape) * (filehdr.tape_cnt - 1);
	nbytes += sizeof (struct db_chkpt);
	/*LINTED [rvalue = malloc() and therefore aligned]*/
	dbchkpt = (struct db_chkpt *)xmalloc(nbytes);
	dbchkpt->db_lastinumber = lastino;
	dbchkpt->db_lastdnode = dn;
	dbchkpt->db_written = written;
	dbchkpt->db_offset = lseek(fd, 0L, 2);
	if (dbchkpt->db_offset < 0) {
		dbferror(dbf_seek, DONTSEND);
		dumpabort();
	}
	dbchkpt->db_hdr = filehdr;
	for (i = 0; i < filehdr.tape_cnt; i++)
		(void) bcopy((char *)tapelist[i],
		    (char *)&dbchkpt->db_tapelist[i], sizeof (struct bu_tape));
	errno = ENOSPC;
	if (write(fd, (char *)dbchkpt, nbytes) != nbytes) {
		dbferror(dbf_write, DONTSEND);
		dumpabort();
	}
	errno = ENOSPC;
	if (write(fd, (char *)&nbytes, sizeof (nbytes)) != sizeof (nbytes)) {
		dbferror(dbf_write, DONTSEND);
		dumpabort();
	}
	free(dbchkpt);
	dbchkpt = (struct db_chkpt *)0;
	(void) close(fd);
}

/*
 * Parent routine:  retrieve and hold on to
 * the state info at the end of the data file.
 * This becomes the checkpointed state.  Called
 * by rollforward, just before forking the next
 * volume master.
 */
caddr_t
statdbtmp(fd)
	int	fd;
{
	size_t nbytes;

	if (lseek(fd, -sizeof (nbytes), 2) < 0) {
		dbferror(dbf_seek, DONTSEND);
		dumpabort();
	}
	if (read(fd, (char *)&nbytes, sizeof (nbytes)) != sizeof (nbytes)) {
		dbferror(dbf_read, DONTSEND);
		dumpabort();
	}
	if (lseek(fd, -(nbytes+sizeof (nbytes)), 2) < 0) {
		dbferror(dbf_seek, DONTSEND);
		dumpabort();
	}
	/*LINTED [rvalue = malloc() and therefore aligned]*/
	dbchkpt = (struct db_chkpt *)xmalloc(nbytes);
	if (read(fd, (char *)dbchkpt, nbytes) != nbytes) {
		dbferror(dbf_read, DONTSEND);
		dumpabort();
	}
	(void) close(fd);
	return ((caddr_t)dbchkpt);
}

void
closedbtmp(fd)
	int	fd;
{
	writetapes(fd);
	/*
	 * Now that we know how many
	 * records of each type there are,
	 * re-write the bu_header record
	 * at the front of the file.
	 */
	if (lseek(fd, 0L, 0) < 0) {		/* struct at beginning */
		dbferror(dbf_seek, DONTSEND);
		dumpabort();
	}
	errno = ENOSPC;
	if (write(fd, (char *)&filehdr, sizeof (filehdr)) != sizeof (filehdr)) {
		dbferror(dbf_write, DONTSEND);
		dumpabort();
	}
	/*
	 * Now write out trailer
	 * record to mark file complete
	 */
	if (lseek(fd, 0L, 2) < 0) {	/* end of file */
		dbferror(dbf_seek, DONTSEND);
		dumpabort();
	}
	errno = ENOSPC;
	if (write(fd, (char *)&filehdr, sizeof (filehdr)) != sizeof (filehdr)) {
		dbferror(dbf_write, DONTSEND);
		dumpabort();
	}
	(void) close(fd);
}

/*
 * The bulk of the work involved in translating archive data
 * into the form required by the database is done here.
 */
void
writedbtmp(fd, data, isspcl)
	int	fd;
	char	*data;
	int	isspcl;
{
#undef	d_ino		/* <dirent.h> does this to us */

	register int loc;
	register struct direct *dp, *ndp;
	static long dirsize;
	/*LINTED [data = malloc() and therefore aligned]*/
	struct s_spcl *spclrec = (struct s_spcl *)data;
	struct dinode *ip = &spclrec->c_dinode;
	static struct bu_tape *tp;
	struct bu_name name;

	if (isspcl) {
		/*
		 * special record -- make a tape entry
		 * if beginning of new volume
		 */
		if (spclrec->c_type == TS_TAPE) {
			filehdr.tape_cnt = spclrec->c_volume - 1;
			tapelist[filehdr.tape_cnt] = (struct bu_tape *)
			    /*LINTED [lvalue = malloc() and therefore aligned]*/
			    xmalloc(sizeof (struct bu_tape));
			tp = tapelist[filehdr.tape_cnt];
			(void) bzero((char *)tp, sizeof (struct bu_tape));
			(void) strncpy(tp->label, spclrec->c_label, LBLSIZE);
			if (spclrec->c_volume == 1)
				tp->filenum = filenum;
			else
				tp->filenum = 1;
			filehdr.tape_cnt++;
			/*
			 * multi-volume check
			 */
			if (spclrec->c_inumber == lastino)
				dn.dn_flags |= DN_MULTITAPE;
			tp->first_inode = tp->last_inode = lastino;
		} else if (spclrec->c_type != TS_ADDR) {
			if (lastino > ROOTINO &&
			    !S_ISLNK(dn.dn_mode) &&
			    !S_ISDIR(dn.dn_mode) &&
			    (spclrec->c_inumber != lastino ||
			    spclrec->c_type == TS_END)) {
				/*
				 * Different inode -- check for file activity,
				 * write out the dnode, initialize w/new inode
				 * XXX doesn't work cuz activemap is local
				 */
				dn.dn_flags |=
				    BIT(dn.dn_inode, activemap) ? DN_ACTIVE : 0;
				errno = ENOSPC;
				if (write(fd, (char *)&dn, sizeof (dn))
				    != sizeof (dn)) {
					dbferror(dbf_write, DONTSEND);
					dumpabort();
				}
				filehdr.dnode_cnt++;
			}
			if (spclrec->c_type == TS_END) {
				/*
				 * stop after 1st TS_END
				 */
				lastino = ROOTINO;
				return;
			}
			if (!tp->first_inode)
				tp->first_inode = spclrec->c_inumber;
			/*
			 * initialize a dnode
			 */
			dn.dn_inode = lastino = spclrec->c_inumber;
			dn.dn_mode = ip->di_mode;
			dn.dn_uid = ip->di_suid == UID_LONG ?
				ip->di_uid : ip->di_suid;
			dn.dn_gid = ip->di_sgid == GID_LONG ?
				ip->di_gid : ip->di_sgid;
			if ((ip->di_mode & IFMT) == IFCHR ||
			    (ip->di_mode & IFMT) == IFBLK)
				dn.dn_size = ip->di_rdev;
			else
				dirsize = dn.dn_size = ip->di_size;
			if ((ip->di_mode & IFMT) == IFLNK) {
				dn.dn_symlink = dn.dn_size+1;
				/*
				 * Symbolic link data -- although dump
				 * will dump an arbitrarily large link,
				 * the database can only handle one
				 * MAXPATHLEN bytes long.
				 */
				if (dn.dn_symlink > TP_BSIZE)
					dn.dn_symlink = TP_BSIZE;
				written = 0;
			} else
				dn.dn_blocks = ip->di_blocks;
			dn.dn_atime = ip->di_atime;
			dn.dn_mtime = ip->di_mtime;
			dn.dn_ctime = ip->di_ctime;
			dn.dn_filename = NULL;
			dn.dn_parent = NULL;
			dn.dn_volid = NULL;
			/*
			 * dn_vol_position is in TP_BSIZE units.
			 * We set DN_OFFSET to tell recover that the value
			 * in dn_vol_position is valid.
			 */
			dn.dn_vol_position =
				((u_long) spclrec->c_tapea - blockswritten);
			dn.dn_flags = DN_OFFSET;
			if (ip->di_shadow != 0)
				dn.dn_flags |= DN_ACL;
			tp->last_inode = spclrec->c_inumber;
			/*
			 * The dnode for a directory immediately follows
			 * its name record, so the data file can be created
			 * in one pass and in one piece.  The dnode is
			 * therefore written before the directory data.
			 * This has the side effect of disabling the checks
			 * for multi-volume or active directories.  We
			 * don't care about active directories since we
			 * are locking the operations we really care about
			 * (rename, unlink) and it's *highly* unlikely
			 * that directories will span volumes.
			 */
			if (S_ISDIR(ip->di_mode) &&
			    spclrec->c_type != TS_ADDR) {
				name.inode = dn.dn_inode;
				name.type = DIRECTORY;
				name.namelen = 0;
				errno = ENOSPC;
				if (write(fd, (char *)&name, sizeof (name))
				    != sizeof (name)) {
					dbferror(dbf_write, DONTSEND);
					dumpabort();
				}
				errno = ENOSPC;
				if (write(fd, (char *)&dn, sizeof (dn))
				    != sizeof (dn)) {
					dbferror(dbf_write, DONTSEND);
					dumpabort();
				}
				filehdr.name_cnt++;
				filehdr.dnode_cnt++;
			} else if (S_ISLNK(ip->di_mode) &&
			    spclrec->c_type != TS_ADDR) {
				errno = ENOSPC;
				if (write(fd, (char *)&dn, sizeof (dn))
				    != sizeof (dn)) {
					dbferror(dbf_write, DONTSEND);
					dumpabort();
				}
				filehdr.dnode_cnt++;
			}
		}
	} else if ((dn.dn_mode & IFMT) == IFLNK && written <= MAXPATHLEN) {
		int nbytes = dn.dn_symlink;

		/*
		 * XXX - a bug in UFS allows the creation of a symlink
		 * with null bytes, but a length > 0.  Work-around this
		 * by checking here and re-writing the dnode if necessary.
		 */
		if (data[0] == '\0') {
			dn.dn_symlink = nbytes = 1;
			errno = ENOSPC;
			if (lseek(fd, (off_t)0 - sizeof (dn), SEEK_CUR) < 0 ||
			    write(fd, (char *)&dn, sizeof (dn))
			    != sizeof (dn)) {
				dbferror(dbf_write, DONTSEND);
				dumpabort();
			}
		}
		data[nbytes-1] = '\0';
		errno = ENOSPC;
		if (write(fd, data, nbytes) != nbytes) {
			dbferror(dbf_write, DONTSEND);
			dumpabort();
		}
		written += nbytes;
	} else {
		/*
		 * Directory data
		 */
		/*LINTED [data = malloc() and therefore aligned]*/
		dp = ndp = (struct direct *)data;
		for (loc = 0; loc < TP_BSIZE && dirsize > 0; dp = ndp) {
			if (dp->d_reclen == 0) {
				break;
			}
			loc += dp->d_reclen;
			dirsize -= dp->d_reclen;
			ndp = (struct direct *)((u_long)ndp + dp->d_reclen);
			if (dp->d_ino == 0 ||
			    strcmp(dp->d_name, ".") == 0 ||
			    strcmp(dp->d_name, "..") == 0)
				continue;
			name.inode = dp->d_ino;
			name.type = 0;
			name.namelen = dp->d_namlen+1;
			errno = ENOSPC;
			if (write(fd, (char *)&name, sizeof (name))
			    != sizeof (name)) {
				dbferror(dbf_write, DONTSEND);
				dumpabort();
			}
			errno = ENOSPC;
			if (write(fd, (char *)dp->d_name, (int)name.namelen)
			    != name.namelen) {
				dbferror(dbf_write, DONTSEND);
				dumpabort();
			}
			filehdr.name_cnt++;
		}
	}
}

/*
 * Write the tape descriptor records to the end of
 * the database tmp file.
 */
static void
writetapes(fd)
	int	fd;
{
	register int i;

	for (i = 0; i < filehdr.tape_cnt; i++) {
		errno = ENOSPC;
		if (write(fd, (char *)tapelist[i], (int)sizeof (struct bu_tape))
		    != sizeof (struct bu_tape)) {
			dbferror(dbf_write, DONTSEND);
			dumpabort();
		}
	}
}

/*
 * isdbtmp -- if the argument file name is a valid database
 * temporary file name return true (1), false (0) otherwise.
 * Database temp files have names in the form:
 *	"dump.xxxxx.yyyyyyyy.mmddhhmmss"
 * where the x's are the PID and the y's are the hex digits of the hostid.
 */
static int
isdbtmp(name)
	char    *name;
{
	struct tm tm;
	register int i;
	int pid;
	long id;
	register char *cp;

	if (strncmp("dump.", name, 5))
		return (0);
	cp = &name[5];
	for (i = 0; i < 5; i++)
		if (!isdigit((u_char)*cp++))
			return (0);
	if (*cp++ != '.')
		return (0);
	for (i = 0; i < 8; i++)
		if (!isxdigit((u_char)*cp++))
			return (0);
	if (*cp++ != '.')
		return (0);
	if (sscanf(&name[5], "%5d", &pid) != 1 || pid <= 3 || pid > MAXPID)
		return (0);
	if (sscanf(&name[11], "%8lx", (u_long *)&id) != 1 || id != gethostid())
		return (0);
	i = sscanf(cp, "%2d%2d%2d%2d%2d",
	    &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
	if (i != 5 ||
	    (tm.tm_mon < 1 || tm.tm_mon) > 12 ||	/* XXX */
	    (tm.tm_mday < 1 || tm.tm_mday) > 31 ||
	    (tm.tm_hour < 0 || tm.tm_hour) > 24 ||
	    (tm.tm_min < 0 || tm.tm_min) > 59 ||
	    (tm.tm_sec < 0 || tm.tm_sec) > 59)
		return (0);
	return (1);
}

/*
 * Compute a running estimate on the size of the
 * database tmp file.  Called from est().
 */
void
dbtmpest(ip, tpblks)
	struct dinode *ip;
	long tpblks;		/* from est() -- number of tape blocks */
{
	if (S_ISDIR(ip->di_mode))
		dirsiz += tpblks;
	files++;
}

#ifdef USG
#define	HOSTIDLEN	40

static long
#ifdef __STDC__
gethostid(void)
#else
gethostid()
#endif
{
	char	name[HOSTIDLEN], *end;
	long	hostid;
	int	error;

	error = sysinfo(SI_HW_SERIAL, name, HOSTIDLEN);
	/*
	 * error > 0 ==> number of bytes to hold name
	 * and is discarded since gethostid only
	 * cares if it succeeded or failed
	 */
	if (error == -1)
		return (-1);
	else {
		hostid = strtoul(name, &end, 10);
		if (hostid == 0 && end == name) {
			return (-1);
		}
		return (hostid);
	}
}
#endif
