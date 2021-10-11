/*
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)dumptape.c	1.115	95/02/22 SMI"

#include "dump.h"
#include <lfile.h>
#include <config.h>
#include <rmt.h>
#include <setjmp.h>
#include <sys/socket.h>
#include <sys/filio.h>
#include <sys/mman.h>
#ifdef USG
#include <sys/fs/ufs_lockfs.h>
/*
 * XXX This hack for FDKEJECT enables compilation
 * on both jup_alpha3 and jup_alpha5 environments
 */
#include <sys/dkio.h>
#ifndef FDKEJECT
#include <sys/fdio.h>
#define	FDKEJECT	FDEJECT
#endif
#include <sys/mkdev.h>
#else
#include <sys/lockfs.h>
#include <sun/dkio.h>		/* for FDKEJECT */
#endif

#define	SLEEPMS		50

static int writesize;		/* size of malloc()ed buffer for tape */
static long inos[TP_NINOS];	/* starting inodes on each tape */

/*
 * The req structure is used to pass commands from the parent
 * process through the pipes to the slave processes.  It comes
 * in two flavors, depending on which mode dump is operating under:
 * an inode request (on-line mode) and a disk block request ("old" mode).
 */
/*
 * The inode request structure is used during on-line mode.
 * The master passes inode numbers and starting offsets to
 * the slaves.  The tape writer passes out the current inode,
 * offset, and number of tape records written after completing a volume.
 */
struct ireq {
	ino_t	inumber;	/* inode number to open/dump */
	long	igen;		/* inode generation number */
	off_t	offset;		/* starting offset in inode */
	int	count;		/* count for 1st spclrec */
};
/*
 * The block request structure is used in off-line mode to pass
 * commands to dump disk blocks from the parent process through
 * the pipes to the slave processes.
 */
struct breq {
	daddr_t	dblk;		/* disk address to read */
	long	size;		/* number of bytes to read from disk */
	u_long	spclrec[1];	/* actually longer */
};

struct req {
	short	aflag;		/* write data to archive process as well */
	short	tflag;		/* begin new tape */
	union	reqdata {
		struct ireq ino;	/* used for on-line mode */
		struct breq blks;	/* used for off-line mode */
	} data;
};

#define	ir_inumber	data.ino.inumber
#define	ir_igen		data.ino.igen
#define	ir_offset	data.ino.offset
#define	ir_count	data.ino.count

#define	br_dblk		data.blks.dblk
#define	br_size		data.blks.size
#define	br_spcl		data.blks.spclrec

static int reqsiz = sizeof (struct req) + TP_BSIZE - sizeof (long);

#define	SLAVES 3
struct slaves {
	int	sl_slavefd;	/* pipe from master to slave */
	pid_t	sl_slavepid;	/* slave pid; used by killall() */
	ino_t	sl_inos;	/* inos, if this record starts tape */
	int	sl_offset;	/* logical blocks written for object */
	int	sl_count;	/* logical blocks left in spclrec */
	int	sl_tapea;	/* header number, if starting tape */
	int	sl_firstrec;	/* number of first block on tape */
	int	sl_state;	/* dump output state */
	struct	req *sl_req;	/* instruction packet to slave */
};
static struct slaves slaves[SLAVES];	/* one per slave */
static struct slaves *slp;	/* pointer to current slave */
static struct slaves chkpt;	/* checkpointed data */

struct bdesc {
	char	*b_data;	/* pointer to buffer data */
	int	b_flags;	/* flags (see below) */
};

/*
 * The following variables are in shared memory
 * XXX they must be checkpointed and/or reset manually
 */
static caddr_t shared;		/* pointer to block of shared memory */
static char	*buf;		/* output buffers */
static struct bdesc *bufp;	/* buffer descriptors */
static struct bdesc **current;	/* output buffer to fill */
static int *tapea;		/* logical record count */

#ifdef INSTRUMENT
static int	*readmissp;	/* number of times writer was idle */
static int	*idle;		/* number of times slaves were idle */
#endif

/*
 * Buffer flags
 */
#define	BUF_EMPTY	0x0	/* nothing in buffer */
#define	BUF_FULL	0x1	/* data in buffer */
#define	BUF_SPCLREC	0x2	/* contains special record */
#define	BUF_ARCHIVE	0x4	/* dump to archive */
#define	BUF_DBINFO	0x8	/* dump to database */

static int recsout;		/* number of req's sent to slaves */
static int totalrecsout;	/* total number of req's sent to slaves */
static int rotor;		/* next slave to be instructed */
static pid_t master;		/* pid of master, for sending error signals */
static int writer;		/* fd of tape writer */
static pid_t writepid;		/* pid of tape writer */
static int arch;		/* fd of output archiver */
static pid_t archivepid;	/* pid of output archiver */
static int archivefd;		/* fd of archive file (proper) */
static int dbtmpfd;		/* fd of database tmp file */
static off_t archoffset;	/* checkpointed offset into archive file */
static caddr_t dbtmpstate;	/* checkpointed state (magic cookie) */

int caught;			/* caught signal -- imported by mapfile() */

#ifdef DEBUG
extern	int xflag;
#endif

#ifdef __STDC__
static void cmdwrterr(void);
static void cmdrderr(void);
static void freetape(void);
static void bufclear(void);
static pid_t setuparchive(void);
static pid_t setupwriter(void);
static void nextslave(void);
static void tperror(int);
static void rollforward(int);
static void nap(int);
static void alrm(int);
static void just_rewind(void);
static void killall(void);
static void proceed(int);
static void die(int);
static void enslave(void);
static void wait_our_turn(void);
static void dumponline(int, int, int);
static void dumpoffline(int, int, int);
static void onxfsz(int);
static void dowrite(int);
static void checkpoint(struct bdesc *, int);
static int atomic(int (*)(), int, char *, int);
#else
static void cmdwrterr();
static void cmdrderr();
static void freetape();
static void bufclear();
static pid_t setuparchive();
static pid_t setupwriter();
static void nextslave();
static void tperror();
static void rollforward();
static void nap();
static void alrm();
static void just_rewind();
static void killall();
static void proceed();
static void die();
static void enslave();
static void wait_our_turn();
static void dumponline();
static void dumpoffline();
static void onxfsz();
static void dowrite();
static void checkpoint();
static int atomic();
#endif

static off_t tapesize;

/*
 * Allocate buffers and shared memory variables.  Tape buffers are
 * allocated on page boundaries for tape write() efficiency.
 */
void
#ifdef __STDC__
#else
#endif
alloctape(void)
{
	register struct slaves *slp;
	int pgoff = getpagesize() - 1;	    /* pagesize better be power of 2 */
	int	mapfd;
	register int i, j;

	writesize = ntrec * TP_BSIZE;
	if (!printsize)
		msg(gettext("Writing %d Kilobyte records\n"),
			writesize / TP_BSIZE);

	/*
	 * set up shared memory seg for here and child
	 */
	mapfd = open("/dev/zero", O_RDWR);
	if (mapfd == -1) {
		msg(gettext("Cannot open `%s': %s\n"),
			"/dev/zero", strerror(errno));
		dumpabort();
	}
	/*
	 * Allocate space such that buffers are page-aligned and
	 * pointers are aligned on 4-byte boundaries (for SPARC).
	 * This code assumes that (NBUF * writesize) is a multiple
	 * of the page size and that pages are aligned on 4-byte
	 * boundaries.  Space is allocated as follows:
	 *
	 *    (NBUF * writesize) for the actual buffers
	 *    (pagesize - 1) for padding so the buffers are page-aligned
	 *    (NBUF * ntrec * sizeof (struct bdesc)) for each buffer
	 *    (n * sizeof (int)) for [n] debugging variables/pointers
	 *    (n * sizeof (int)) for [n] miscellaneous variables/pointers
	 */
	tapesize =
	    (NBUF * writesize)				/* output buffers */
	    + pgoff					/* page alignment */
	    + (sizeof (struct bdesc) * NBUF * ntrec)	/* buffer descriptors */
#ifdef INSTRUMENT
	    + (2 * sizeof (int))			/* instrumentation */
#endif
	    + (4 * sizeof (int));			/* shared variables */

	shared = mmap((char *)0, tapesize, PROT_READ|PROT_WRITE,
	    MAP_SHARED, mapfd, (off_t)0);
	if (shared == (caddr_t)-1) {
		msg(gettext("Cannot memory map output buffers: %s\n"),
		    strerror(errno));
		dumpabort();
	}
	(void) close(mapfd);
	/*
	 * Buffers and buffer headers
	 */
	buf = (char *)(((u_long)shared + pgoff) & ~pgoff);
	/*LINTED [buf, writesize are aligned]*/
	bufp = (struct bdesc *) (buf + NBUF*writesize);
	/*
	 * Shared memory variables
	 */
	current = (struct bdesc **) &bufp[NBUF*ntrec];
	tapea = (int *) ((int *)current + 1);
	telapsed = (time_t *) ((int *)tapea + 1);
	tstart_writing = (time_t *) ((int *)telapsed + 1);
#ifdef INSTRUMENT
	/*
	 * Debugging and instrumentation variables
	 */
	readmissp = tstart_writing + 1;
	idle = readmissp + 1;
#endif

	for (i = 0, j = 0; i < NBUF * ntrec; i++, j += TP_BSIZE)
		bufp[i].b_data = &buf[j];

	if (online)
		reqsiz = sizeof (struct req);

	for (slp = slaves; slp < &slaves[SLAVES]; slp++)
		/*LINTED [rvalue = malloc() and therefore aligned]*/
		slp->sl_req = (struct req *)xmalloc(reqsiz);

	chkpt.sl_offset = 0;		/* start at offset 0 */
	chkpt.sl_count = 0;
	chkpt.sl_inos = UFSROOTINO;	/* in root inode */
	chkpt.sl_firstrec = 1;
	chkpt.sl_tapea = 0;
}

static void
#ifdef __STDC__
freetape(void)
#else
freetape()
#endif
{
	if (shared == NULL)
		return;
	(void) timeclock(0);
	(void) munmap(shared, tapesize);
	shared = NULL;
}

/*
 * Reset tape state variables -- called
 * before a pass to dump active files.
 */
void
#ifdef __STDC__
reset(void)
#else
reset()
#endif
{
	bufclear();

#ifdef INSTRUMENT
	(*readmissp) = 0;
	(*idle) = 0;
#endif

	spcl.c_flags = 0;
	spcl.c_volume = tapeno = 0;

	chkpt.sl_offset = 0;		/* start at offset 0 */
	chkpt.sl_count = 0;
	chkpt.sl_inos = UFSROOTINO;	/* in root inode */
	chkpt.sl_firstrec = 1;
	chkpt.sl_tapea = 0;
}

static void
#ifdef __STDC__
bufclear(void)
#else
bufclear()
#endif
{
	register struct bdesc *bp;
	register int i;

	for (i = 0, bp = bufp; i < NBUF * ntrec; i++, bp++)
		bp->b_flags = BUF_EMPTY;
	*current = bufp;
}

/*
 * Start a process to collect information describing the dump.
 * This data takes two forms:
 *    the bitmap and directory information being written to
 *	the front of the tape (the "archive" file)
 *    information describing each directory and inode (to
 *	be included in the database tmp file)
 * Write the data to the files as it is received so huge file
 * systems don't cause dump to consume large amounts of memory.
 */
static pid_t
#ifdef __STDC__
setuparchive(void)
#else
setuparchive()
#endif
{
	register struct slaves *slp;
	int cmd[2];
	pid_t pid;
	int size;
	char *data;
	int flags;
	int punt = 0;

	/*
	 * Both the archive and database tmp files are
	 * checkpointed by taking their current offsets
	 * (sizes) after completing each volume.  Restoring
	 * from a checkpoint involves truncating to the
	 * checkpointed size.
	 */
	if (archive && !doingactive) {
		archivefd = open(archivefile, O_WRONLY|O_CREAT, 0600);
		if (archivefd < 0) {
			msg(gettext("Cannot open archive file `%s': %s\n"),
			    archivefile, strerror(errno));
			dumpabort();
		}
		if (lseek(archivefd, archoffset, 0) < 0) {
			msg(gettext("Cannot position archive file `%s': %s\n"),
			    archivefile, strerror(errno));
			dumpabort();
		}
		if (ftruncate(archivefd, archoffset) < 0) {
			msg(gettext("Cannot truncate archive file `%s': %s\n"),
			    archivefile, strerror(errno));
			dumpabort();
		}
	}

	if (database)
		dbtmpfd = dbtmpstate ? opendbtmp(dbtmpstate) : initdbtmp();

	if (pipe(cmd) < 0 || (pid = fork()) < 0) {
		msg(gettext("%s: %s error: %s\n"),
		    "setuparchive", "fork", strerror(errno));
		return (0);
	}
	if (pid > 0) {
		/* parent process */
		(void) close(cmd[0]);
		arch = cmd[1];
		return (pid);
	}
	/*
	 * child process
	 */
	(void) signal(SIGINT, SIG_IGN);		/* master handles this */
#ifdef TDEBUG
	(void) sleep(4);	/* allow time for parent's message to get out */
	/* XGETTEXT:  #ifdef TDEBUG only */
	msg(gettext("Archiver has pid = %ld\n"), (long)getpid());
#endif
	freeino();	/* release unneeded resources */
	freetape();
	for (slp = &slaves[0]; slp < &slaves[SLAVES]; slp++)
		(void) close(slp->sl_slavefd);
	(void) close(to);
	(void) close(fi);
	(void) close(cmd[1]);
	data = xmalloc(TP_BSIZE);
	for (;;) {
		size = atomic((int(*)())read, cmd[0], (char *)&flags,
		    sizeof (flags));
		if (size != sizeof (flags))
			break;
		size = atomic((int(*)())read, cmd[0], data, TP_BSIZE);
		if (size == TP_BSIZE) {
			if (archive && flags & BUF_ARCHIVE &&
			    write(archivefd, data, TP_BSIZE) != TP_BSIZE) {
				msg(gettext(
				    "Cannot write archive file `%s': %s\n"),
					archivefile, strerror(errno));
				dumpailing(gettext("archive write error"));
				punt++;
			}
			if (database && flags & BUF_DBINFO)
				writedbtmp(dbtmpfd, data, flags & BUF_SPCLREC);
		} else
			break;
	}
	(void) close(cmd[0]);
	if (archive)
		(void) close(archivefd);
	if (database) {
		if (caught) {
			closedbtmp(dbtmpfd);
			doupdate(dbtmpfile);
		} else
			savedbtmp(dbtmpfd);
	}
	if (punt) {
		(void) unlink(archivefile);
		Exit(X_ABORT);
	}
	Exit(X_FINOK);
	/* NOTREACHED */
}

/*
 * Start a process to read the output buffers and write the data
 * to the output device.
 */
static pid_t
#ifdef __STDC__
setupwriter(void)
#else
setupwriter()
#endif
{
	register struct slaves *slp;
	int cmd[2];
	pid_t pid;

	if (pipe(cmd) < 0 || (pid = fork()) < 0) {
		msg(gettext("%s: %s error: %s\n"),
			"setupwriter", "fork", strerror(errno));
		return (0);
	}
	if (pid > 0) {
		/*
		 * Parent process
		 */
		(void) close(cmd[0]);
		writer = cmd[1];
		return (pid);
	}
	/*
	 * Child (writer) process
	 */
	caught = 0;
	(void) signal(SIGINT, SIG_IGN);		/* master handles this */
#ifdef TDEBUG
	(void) sleep(4);	/* allow time for parent's message to get out */
	/* XGETTEXT:  #ifdef TDEBUG only */
	msg(gettext("Writer has pid = %ld\n"), (long)getpid());
#endif
	freeino();	/* release unneeded resources */
	for (slp = &slaves[0]; slp < &slaves[SLAVES]; slp++)
		(void) close(slp->sl_slavefd);
	(void) close(fi);
	(void) close(cmd[1]);
	dowrite(cmd[0]);
	if (arch >= 0)
		(void) close(arch);
	(void) close(cmd[0]);
	Exit(X_FINOK);
	/* NOTREACHED */
}

void
#ifdef __STDC__
spclrec(void)
#else
spclrec()
#endif
{
	register int s, i, *ip;
	int flags = BUF_SPCLREC|BUF_DBINFO;

	if ((BIT(ino, shamap)) && (spcl.c_type == TS_INODE)) {
		spcl.c_type = TS_ADDR;
		spcl.c_dinode.di_mode &= ~S_IFMT;
		spcl.c_dinode.di_mode |= IFSHAD;
	}
	if (spcl.c_dinode.di_shadow != 0)
		spcl.c_dinode.di_mode &= ~077;

	/*
	 * Only TS_INODEs should have short metadata, if this
	 * isn't such a spclrec, clear the metadata flag and
	 * the c_shadow contents.
	 */
	if (!(spcl.c_type == TS_INODE && (spcl.c_flags & DR_HASMETA))) {
		spcl.c_flags &= ~DR_HASMETA;
		bcopy(c_shadow_save, &(spcl.c_shadow),
		    sizeof (spcl.c_shadow));
	}

	if (spcl.c_type == TS_END) {
		spcl.c_count = 1;
		spcl.c_flags |= DR_INODEINFO;
		(void) bcopy((char *)inos, (char *)spcl.c_inos, sizeof (inos));
	} else if (spcl.c_type == TS_TAPE) {
		spcl.c_flags |= DR_NEWHEADER;
		if (trueinc)
			spcl.c_flags |= DR_TRUEINC;
		if (doingactive)
			spcl.c_flags |= DR_REDUMP;
	} else if (spcl.c_type != TS_INODE)
		flags = BUF_SPCLREC;
	spcl.c_tapea = *tapea;
	spcl.c_inumber = ino;
	spcl.c_magic = NFS_MAGIC;
	spcl.c_checksum = 0;
	ip = (int *)&spcl;
	s = CHECKSUM;
	i = sizeof (union u_spcl) / sizeof (int);
	i /= 8;
	do {
		s -= *ip++; s -= *ip++; s -= *ip++; s -= *ip++;
		s -= *ip++; s -= *ip++; s -= *ip++; s -= *ip++;
	} while (--i > 0);
	spcl.c_checksum = s;
	taprec((char *)&spcl, flags);
	if (spcl.c_type == TS_END)
		spcl.c_flags &= ~DR_INODEINFO;
	else if (spcl.c_type == TS_TAPE)
		spcl.c_flags &= ~(DR_NEWHEADER|DR_REDUMP|DR_TRUEINC);
}

/*
 * Fill appropriate buffer
 */
void
taprec(dp, flags)
	char *dp;
	int flags;
{
	while ((*current)->b_flags & BUF_FULL)
		nap(10);
	(void) bcopy(dp, (*current)->b_data, TP_BSIZE);
	if (dumptoarchive)
		flags |= BUF_ARCHIVE;
	if (dumptodatabase)
		flags |= BUF_DBINFO;
	(*current)->b_flags = (flags | BUF_FULL);
	if (++*current >= &bufp[NBUF*ntrec])
		(*current) = &bufp[0];
	(*tapea)++;
}

void
dmpblk(blkno, size, offset)
	daddr_t blkno;
	long size;
	off_t offset;
{
	daddr_t dblkno;

	dblkno = fsbtodb(sblock, blkno) + (offset >> DEV_BSHIFT);
	size = (size + DEV_BSIZE-1) & ~(DEV_BSIZE-1);
	slp->sl_req->br_dblk = dblkno;
	slp->sl_req->br_size = size;
	if (dumptoarchive)
		slp->sl_req->aflag |= BUF_ARCHIVE;
	if (dumptodatabase)
		slp->sl_req->aflag |= BUF_DBINFO;
	toslave((void(*)())0, ino);
}

/*ARGSUSED*/
static void
tperror(sig)
	int	sig;
{
	char buf[3000];

	if (pipeout) {
		msg(gettext("Write error on %s\n"), tape);
		msg(gettext("Cannot recover\n"));
		dumpabort();
		/* NOTREACHED */
	}
	if (!doingverify) {
		broadcast(gettext("WRITE ERROR!\n"));
		(void) sprintf(buf, gettext("Do you want to restart?: %s "),
			gettext("(\"yes\" or \"no\") "));
		if (!query(buf, gettext("Write error")))
			dumpabort();
		if (tapeout && (isrewind(to) || offline)) {
			msg("%s%s%s",
				gettext("This tape will rewind.  "),
				gettext("After it is rewound, \n\
replace the faulty tape with a new one; \n"),
				gettext(
				    "this dump volume will be rewritten.\n"));
			(void) opermes(LOG_WARNING, gettext(
				"Rewinding faulty tape\n"));
		}
	} else {
		broadcast(gettext("TAPE VERIFICATION ERROR!\n"));
		(void) sprintf(buf, gettext("Do you want to rewrite?: %s "),
			gettext("(\"yes\" or \"no\") "));
		if (!query(buf, gettext("Verification error")))
			dumpabort();
		msg(gettext(
			"This tape will be rewritten and then verified\n"));
		(void) opermes(LOG_WARNING, gettext(
			"Rewinding non-verifiable tape\n"));
	}
	killall();
	trewind();
	Exit(X_REWRITE);
}

/*
 * Called by master from pass() to send a request to dump files/blocks
 * to one of the slaves.  Slaves return whether the file was active
 * when it was being dumped.  The tape writer process sends checkpoint
 * info when it completes a volume.
 */
void
toslave(fn, inumber)
	void	(*fn)();
	ino_t	inumber;
{
	int	wasactive;

	if (recsout >= SLAVES) {
		if (atomic((int(*)())read, slp->sl_slavefd, (char *)&wasactive,
		    sizeof (wasactive)) != sizeof (wasactive)) {
			cmdrderr();
			dumpabort();
		}
		if (wasactive) {
			active++;
			msg(gettext(
		"The file at inode `%lu' was active and will be recopied\n"),
				slp->sl_req->ir_inumber);
			BIS(slp->sl_req->ir_inumber, activemap);
		}
	}
	slp->sl_req->aflag = 0;
	if (dumptoarchive)
		slp->sl_req->aflag |= BUF_ARCHIVE;
	if (dumptodatabase)
		slp->sl_req->aflag |= BUF_DBINFO;
	if (fn)
		(*fn)(inumber);

	if (atomic((int(*)())write, slp->sl_slavefd, (char *)slp->sl_req,
	    reqsiz) != reqsiz) {
		cmdwrterr();
		dumpabort();
	}
	++recsout;
	nextslave();
}

void
doinode(inumber)
	ino_t	inumber;
{
	/*
	 * Send file info command to slave
	 */
	slp->sl_req->ir_inumber = inumber;
	slp->sl_req->ir_igen = getigen(inumber);
	if (pos || leftover) {
		slp->sl_req->ir_offset = pos * TP_BSIZE;
		slp->sl_req->ir_count = leftover;
		leftover = pos = 0;
	} else {
		slp->sl_req->ir_offset = 0;
		slp->sl_req->ir_count = 0;
	}
	if (newtape) {
		slp->sl_req->tflag = 1;
		newtape = 0;
	} else {
		slp->sl_req->tflag = 0;
	}
}

void
dospcl(inumber)
	ino_t	inumber;
{
	spcl.c_inumber = inumber;
	slp->sl_req->br_dblk = 0;
	(void) bcopy((char *)&spcl, (char *)slp->sl_req->br_spcl, TP_BSIZE);
}

static void
#ifdef __STDC__
nextslave(void)
#else
nextslave()
#endif
{
	if (++rotor >= SLAVES) {
		rotor = 0;
	}
	slp = &slaves[rotor];
}

void
#ifdef __STDC__
flushcmds(void)
#else
flushcmds()
#endif
{
	register int i;
	int	wasactive;

	/*
	 * Retrieve all slave status
	 */
	if (recsout < SLAVES) {
		slp = slaves;
		rotor = 0;
	}
	for (i = 0; i < (recsout < SLAVES ? recsout : SLAVES); i++) {
		if (atomic((int(*)())read, slp->sl_slavefd, (char *)&wasactive,
		    sizeof (wasactive)) != sizeof (wasactive)) {
			cmdrderr();
			dumpabort();
		}
		if (wasactive) {
			active++;
			msg(gettext(
			    "inode %d was active and will be recopied\n"),
				slp->sl_req->ir_inumber);
			BIS(slp->sl_req->ir_inumber, activemap);
		}
		nextslave();
	}
}

void
#ifdef __STDC__
flusht(void)
#else
flusht()
#endif
{
#ifdef USG
	sigset_t block_set, oset;	/* hold SIGUSR1 and atomically sleep */

	(void) sigemptyset(&block_set);
	(void) sigaddset(&block_set, SIGUSR1);
	(void) sigprocmask(SIG_BLOCK, &block_set, &oset);
	(void) kill(writepid, SIGUSR1);	/* tell writer to flush */
	(void) sigpause(SIGUSR1);	/* wait for SIGUSR1 from writer */
#else
	(void) kill(writepid, SIGUSR1);	/* tell writer to flush */
	(void) pause();			/* wait for SIGUSR1 from writer */
#endif
	/*NOTREACHED*/
}

jmp_buf	checkpoint_buf;

/*
 * Roll forward to the next volume after receiving
 * an EOT signal from writer.  Get checkpoint data
 * from writer and return if done, otherwise fork
 * a new process and jump back to main state loop
 * to begin the next volume.  Installed as the master's
 * signal handler for SIGUSR1.
 */
/*ARGSUSED*/
static void
rollforward(sig)
	int	sig;
{
	int status;
#ifndef USG
	int oldmask = sigblock(sigmask(SIGUSR1));
#else
	(void) sighold(SIGUSR1);
#endif

	/*
	 * Writer sends us checkpoint information after
	 * each volume.  A returned state of DONE with no
	 * unwritten (left-over) records differentiates a
	 * clean flush from one in which EOT was encountered.
	 */
	if (atomic((int(*)())read, writer, (char *)&chkpt,
	    sizeof (struct slaves)) != sizeof (struct slaves)) {
		cmdrderr();
		dumpabort();
	}
	if (atomic((int(*)())read, writer, (char *)&spcl,
	    TP_BSIZE) != TP_BSIZE) {
		cmdrderr();
		dumpabort();
	}
	ino = chkpt.sl_inos - 1;
	pos = chkpt.sl_offset;
	leftover = chkpt.sl_count;
	dumpstate = chkpt.sl_state;
	blockswritten = ++chkpt.sl_tapea;

	if (dumpstate == DONE) {
		if (leftover && !doingverify)
			/* mark volume full */
			modlfile((u_char)LF_FULL, (u_char)LF_USED,
				(char *)0, 0);
		if (archivepid) {
			/*
			 * If archiving (either archive or
			 * database), signal the archiver
			 * to finish up.  This must happen
			 * before the writer exits in order
			 * to avoid a race.
			 */
			(void) kill(archivepid, SIGUSR1);
		}
		(void) signal(SIGUSR1, SIG_IGN);
#ifdef USG
		(void) sigrelse(SIGUSR1);
#endif
		(void) kill(writepid, SIGUSR1);	/* tell writer to exit */
		dbtmpstate = (caddr_t)0;
		archoffset = 0L;
		longjmp(checkpoint_buf, 1);
		/*NOTREACHED*/
	}

	if (leftover) {
#ifdef USG
		/*
		 * USG memcpy does not handle overlapping
		 * copies correctly, so we must explicitly
		 * call memmove -- the args are reversed
		 * from those of bcopy!
		 */
		(void) memmove(spcl.c_addr,
		    &spcl.c_addr[spcl.c_count-leftover], leftover);
#else
		(void) bcopy(&spcl.c_addr[spcl.c_count-leftover],
		    spcl.c_addr, leftover);
#endif

		(void) bzero(&spcl.c_addr[leftover], TP_NINDIR-leftover);
	}
	if (!doingverify)
		/*
		 * Mark volume full -- we need to lie
		 * to dumpex and tell it we didn't
		 * write on the tape if the tape was
		 * too short.
		 */
		modlfile((u_char)LF_FULL, dumpstate == START ?
			(u_char)LF_NOTUSED : (u_char)LF_USED, (char *)0, 0);

	if (writepid) {
		(void) kill(writepid, SIGUSR1);	/* tell writer to exit */
		(void) close(writer);
	}
	if (archivepid) {
		(void) waitpid(archivepid, &status, 0);	/* wait for archiver */
#ifdef TDEBUG

		/* XGETTEXT:  #ifdef TDEBUG only */
		msg(gettext("Archiver %ld returns with status %d\n"),
		    (long)archivepid, status);
#endif
	}
	/*
	 * Checkpoint archive file
	 */
	if (!doingverify && archive) {
		archoffset = lseek(archivefd, 0L, 2);
		if (archoffset < 0) {
			msg(gettext("Cannot position archive file `%s': %s\n"),
				archivefile, strerror(errno));
			dumpabort();
		}
		(void) close(archivefd);
	}
	/*
	 * Checkpoint database file
	 */
	if (!doingverify && database)
		dbtmpstate = statdbtmp(dbtmpfd);
	resetino(ino);

	if (dumpstate == START) {
		msg(gettext(
			"Tape too short: changing volumes and restarting\n"));
		reset();
	}

	if (!pipeout) {
		if (verify && !doingverify)
			trewind();
		else {
			close_rewind();
			changevol();
		}
	}

#ifndef USG
	(void) sigsetmask(oldmask & ~sigmask(SIGUSR1));	/* unblock signal */
#else
	(void) sigrelse(SIGUSR1);
#endif
	otape(0);
	longjmp(checkpoint_buf, 1);
	/*NOTREACHED*/
}

static void
nap(ms)
	int ms;
{
	struct timeval tv;

	tv.tv_sec = ms / 1000;
	tv.tv_usec = (ms - tv.tv_sec * 1000) * 1000;
	(void) select(0, (fd_set *)0, (fd_set *)0, (fd_set *)0, &tv);
}

static jmp_buf alrm_buf;

/*ARGSUSED*/
static void
alrm(sig)
	int	sig;
{
	longjmp(alrm_buf, 1);
	/*NOTREACHED*/
}

void
#ifdef __STDC__
nextdevice(void)
#else
nextdevice()
#endif
{
	char	*cp;

	if (metamucil_mode == METAMUCIL) {
		cp = getdevice();
		tape = xmalloc((strlen(cp)+1));
		(void) strcpy(tape, cp);
	} else {
		if (host)	/* we set the host only once in ufsdump */
			return;
	}
	host = NULL;
	if (strchr(tape, ':')) {
		if (diskette) {
			msg(gettext("Cannot do remote dump to diskette\n"));
			Exit(X_ABORT);
		}
		host = tape;
		tape = strchr(host, ':');
		*tape++ = 0;
		cp = strchr(host, '@');	/* user@host? */
		if (cp != (char *)0)
			cp++;
		else
			cp = host;
	} else
		cp = spcl.c_host;
	/*
	 * dumpdev is provided for use in prompts and is of
	 * the form:
	 *	hostname:device
	 * sdumpdev is of the form:
	 *	hostname:device
	 * for remote devices, and simply:
	 *	device
	 * for local devices.
	 */
	dumpdev = xmalloc((NAMELEN+strlen(tape)+2));
	(void) sprintf(dumpdev, "%.*s:%s", NAMELEN, cp, tape);
	if (cp == spcl.c_host)
		sdumpdev = strchr(dumpdev, ':') + 1;
	else
		sdumpdev = dumpdev;
}

/*
 * Gross hack due to misfeature of mt tape driver that causes
 * the device to rewind if we generate any signals.  Guess
 * whether tape is rewind device or not -- for local devices
 * we can just look at the minor number.  For rmt devices,
 * make an educated guess.
 */
int
isrewind(f)
	int	f;	/* fd, if local device */
{
	struct stat sbuf;
	char    *c;
	int	unit;
	int	rewind;

	if (host) {
		c = strrchr(tape, '/');
		if (c == NULL)
			c = tape;
		else
			c++;
		/*
		 * If the last component begins or ends with an 'n', it is
		 * assumed to be a non-rewind device.
		 */
		if (c[0] == 'n' || c[strlen(c)-1] == 'n')
			rewind = 0;
		else if ((strstr(tape, "mt") || strstr(tape, "st")) &&
		    sscanf(tape, "%*[a-zA-Z/]%d", &unit) == 1 &&
		    (unit & MT_NOREWIND))
			rewind = 0;
		else
			rewind = 1;
	} else {
		if (fstat(f, &sbuf) < 0) {
			msg(gettext(
			    "Cannot obtain status of output device `%s'\n"),
				tape);
			dumpabort();
		}
		rewind = minor(sbuf.st_rdev) & MT_NOREWIND ? 0 : 1;
	}
	return (rewind);
}

static void
#ifdef __STDC__
just_rewind(void)
#else
just_rewind()
#endif
{
	register struct slaves *slp;

	for (slp = &slaves[0]; slp < &slaves[SLAVES]; slp++) {
		if (slp->sl_slavepid > 0)	/* signal normal exit */
			(void) kill(slp->sl_slavepid, SIGTERM);
		if (slp->sl_slavefd >= 0)
			(void) close(slp->sl_slavefd);
	}
	while (waitpid(0, (int *)0, 0) >= 0);
		/* wait for any signals from slaves */
	if (pipeout)
		return;

	if (doingverify) {
		/*
		 * Space to the end of the tape.
		 * Backup first in case we already read the EOF.
		 */
		if (host) {
			(void) rmtioctl(MTBSR, 1);
			if (rmtioctl(MTEOM, 1) < 0)
				(void) rmtioctl(MTFSF, 1);
		} else {
			static struct mtop bsr = { MTBSR, 1 };
			static struct mtop eom = { MTEOM, 1 };
			static struct mtop fsf = { MTFSF, 1 };

			(void) ioctl(to, MTIOCTOP, &bsr);
			if (ioctl(to, MTIOCTOP, &eom) < 0)
				(void) ioctl(to, MTIOCTOP, &fsf);
		}
	}

	/*
	 * Guess whether the tape is rewinding so we can tell
	 * the operator if it's going to take a long time.
	 */
	if (tapeout && isrewind(to)) {
		/* tape is probably rewinding */
		msg(gettext("Tape rewinding\n"));
		(void) opermes(LOG_INFO, gettext("Tape rewinding\n"));
	}
}

void
#ifdef __STDC__
trewind(void)
#else
trewind()
#endif
{
	(void) timeclock(0);
	if (offline) {
		close_rewind();
	} else {
		just_rewind();
		if (host)
			rmtclose();
		else
			(void) close(to);
	}
}

void
#ifdef __STDC__
close_rewind(void)
#else
close_rewind()
#endif
{
	(void) timeclock(0);
	just_rewind();
	/*
	 * The check in just_rewind won't catch the case in
	 * which the current volume is being taken off-line
	 * and is not mounted on a no-rewind device (and is
	 * not the last volume, which is not taken off-line).
	 */
	if (tapeout && !isrewind(to) && offline) {
		/* tape is probably rewinding */
		msg(gettext("Tape rewinding\n"));
		(void) opermes(LOG_INFO, gettext("Tape rewinding\n"));
	}
	if (host) {
		if (offline || autoload)
			(void) rmtioctl(MTOFFL, 0);
		rmtclose();
	} else {
		if (offline || autoload) {
			static struct mtop offl = { MTOFFL, 0 };

			(void) ioctl(to, MTIOCTOP, &offl);
			if (diskette)
				(void) ioctl(to, FDKEJECT, 0);
		}
		(void) close(to);
	}
}

void
#ifdef __STDC__
changevol(void)
#else
changevol()
#endif
{
	char buf1[3000], buf2[3000];
	char volname[LBLSIZE+1];
	int wrapped;

	filenum = 1;
	nextdevice();
	getlabel();
	getdevinfo((dtype_t *)0, &ndevices, &wrapped);
	if (host) {
		char	*cp = strchr(host, '@');
		if (cp == (char *)0)
			cp = host;
		else
			cp++;
		(void) setreuid(-1, 0);
		if (rmthost(host, ntrec) == 0) {
			msg(gettext("Cannot connect to tape host `%s'\n"), cp);
			dumpabort();
		}
		(void) setreuid(-1, getuid());
		host = cp;
	}

	/*
	 * Make volume switching as automatic as possible
	 * while avoiding overwriting volumes.  We will
	 * switch automatically under the following conditions:
	 *    1) The user specified autoloading from the
	 *	command line.
	 *    2) We are using more than one physical device
	 *	(sequence) and any of the following are true:
	 *	  a) we have not yet used all devices once
	 *	  b) label checking is being performed
	 *	  c) volumes are being taken offline as
	 *		they are filled.
	 */
	if (autoload ||
	    (ndevices > 1 && (!wrapped || verifylabels || offline))) {
		int tries;
		char volname[LBLSIZE+1];

		if (labelfile) {
			(void) strncpy(volname, spcl.c_label, LBLSIZE);
			volname[LBLSIZE] = '\0';
		} else
			(void) sprintf(volname, "#%d", tapeno+1);
		(void) sprintf(buf1, gettext(
		    "Mounting volume %s on %s\n"), volname, dumpdev);
		msg(buf1);
		broadcast(buf1);

		/* wait up to 2.5 minutes for the tape to autoload */
		for (tries = 0; tries < 12; tries++) {
			if (host) {
				if (rmtopen(tape, O_RDONLY) >= 0) {
					rmtclose();
					return;
				}
			} else {
				int f, m;

				m = (access(tape, F_OK) == 0) ? 0 : O_CREAT;
				if ((f = doingverify ?
				    open(tape, O_RDONLY) :
				    open(tape, O_RDONLY|m, 0600)) >= 0) {
					(void) close(f);
					return;
				}
			}
			(void) sleep(12);
		}
		/* auto-load timed out, ask the operator to do it */
	}

	if (strcmp(spcl.c_label, "none")) {
		(void) strncpy(volname, spcl.c_label, LBLSIZE);
		volname[LBLSIZE] = '\0';
	} else
		(void) sprintf(volname, "#%d", tapeno+1);

	timeest(1, spcl.c_tapea);
	(void) sprintf(buf1, gettext(
	    "Change Volumes: Mount volume `%s' on `%s'\n"), volname, dumpdev);
	msg(buf1);
	(void) opermes(LOG_CRIT, buf1);
	broadcast(gettext("CHANGE VOLUMES!\7\7\n"));
	(void) sprintf(buf1, gettext(
	    "Is the new volume (%s) mounted on `%s' and ready to go?: %s"),
	    volname, dumpdev, gettext("(\"yes\" or \"no\") "));
	while (!query(buf1, (char *)0)) {
		(void) sprintf(buf2, gettext("Do you want to abort dump?: %s "),
			gettext("(\"yes\" or \"no\") "));
		if (query(buf2, gettext("Volume change"))) {
			dumpabort();
			/*NOTREACHED*/
		}
	}
}

/*
 *	We implement taking and restoring checkpoints on the tape level.
 *	When each tape is opened, a new process is created by forking; this
 *	saves all of the necessary context in the parent.  The child
 *	continues the dump; the parent waits around, saving the context.
 *	If the child returns X_REWRITE, then it had problems writing that tape;
 *	this causes the parent to fork again, duplicating the context, and
 *	everything continues as if nothing had happened.
 */
void
otape(top)
	int top;
{
	static struct mtget mt;
	pid_t parentpid;
	pid_t childpid;
	pid_t waitproc;
	int status;
	int waslocked = 0;
	int lockstat;
	struct sigvec sv, osv;

	sv.sv_flags = SA_RESTART;
#ifdef USG
	(void) sigemptyset(&sv.sa_mask);
#else
	sv.sv_mask = 0;
#endif
	sv.sv_handler = SIG_IGN;
	(void) sigvec(SIGINT, &sv, (struct sigvec *)0);

	parentpid = getpid();

	if (verify) {
		if (doingverify)
			doingverify = 0;
		else
			Exit(X_VERIFY);
	}
restore_check_point:

	sv.sv_handler = interrupt;
	(void) sigvec(SIGINT, &sv, (struct sigvec *)0);
	(void) fflush(stderr);
	if (online) {
		lockstat = lockfs(filesystem, "status");
		if ((lockstat == LOCKFS_WLOCK) || (lockstat == LOCKFS_NLOCK))
			waslocked = 1;
	}
	/*
	 *	All signals are inherited...
	 */
	childpid = fork();
	if (childpid < 0) {
		msg(gettext(
		    "Context save fork fails in parent %ld\n"),
			(long)parentpid);
		Exit(X_ABORT);
	}
	if (childpid != 0) {
		/*
		 *	PARENT:
		 *	save the context by waiting
		 *	until the child doing all of the work returns.
		 *	don't catch the interrupt
		 */
		sv.sv_handler = SIG_IGN;
		(void) sigvec(SIGINT, &sv, (struct sigvec *)0);
#ifdef TDEBUG

		/* XGETTEXT:  #ifdef TDEBUG only */
		msg(gettext(
		    "Volume: %d; parent process: %ld child process %ld\n"),
			tapeno+1, (long)parentpid, (long)childpid);
#endif /* TDEBUG */
		for (;;) {
			waitproc = waitpid(0, &status, 0);
			if (waitproc == childpid)
				break;
			msg(gettext(
	"Parent %ld waiting for child %ld has another child %ld return\n"),
			    (long)parentpid, (long)childpid, (long)waitproc);
		}
		if (WIFSIGNALED(status)) {
			msg(gettext("Process %ld killed by signal %d: %s\n"),
			    (long)childpid, WTERMSIG(status),
			    strsignal(WTERMSIG(status)));
			status = X_ABORT;
		} else
			status = WEXITSTATUS(status);
		if (top && status != X_RESTART)
			sendmail();
#ifdef TDEBUG
		switch (status) {
		case X_FINOK:
			/* XGETTEXT:  #ifdef TDEBUG only */
			msg(gettext(
			    "Child %ld finishes X_FINOK\n"), (long)childpid);
			break;
		case X_ABORT:
			/* XGETTEXT:  #ifdef TDEBUG only */
			msg(gettext(
			    "Child %ld finishes X_ABORT\n"), (long)childpid);
			break;
		case X_REWRITE:
			/* XGETTEXT:  #ifdef TDEBUG only */
			msg(gettext(
			    "Child %ld finishes X_REWRITE\n"), (long)childpid);
			break;
		case X_RESTART:
			/* XGETTEXT:  #ifdef TDEBUG only */
			msg(gettext(
			    "Child %ld finishes X_RESTART\n"), (long)childpid);
			break;
		case X_VERIFY:
			/* XGETTEXT:  #ifdef TDEBUG only */
			msg(gettext(
			    "Child %ld finishes X_VERIFY\n"), (long)childpid);
			break;
		default:
			/* XGETTEXT:  #ifdef TDEBUG only */
			msg(gettext("Child %ld finishes unknown %d\n"),
			    (long)childpid, status);
			break;
		}
#endif /* TDEBUG */
		switch (status) {
		case X_FINOK:
			while (waitpid(0, (int *)0, 0) >= 0);
				/* wait for children */
			Exit(X_FINOK);
			/*NOTREACHED*/
		case X_ABORT:
			Exit(X_ABORT);
			/*NOTREACHED*/
		case X_VERIFY:
			doingverify++;
			goto restore_check_point;
			/*NOTREACHED*/
		case X_REWRITE:
			doingverify = 0;
			if (online) {
				lockstat = lockfs(filesystem, "status");
				/*
				 * waslocked == 1 if the file system
				 * was name-locked when this volume was
				 * started.  We have to restart from
				 * scratch if the lock was relaxed
				 * during the writing of this volume.
				 */
				if (waslocked &&
				    (lockstat != LOCKFS_WLOCK) &&
				    (lockstat != LOCKFS_NLOCK)) {
					if (top) {
						if (!offline && !verifylabels)
							autoload = 0;
						changevol();
						dumpstate = INIT;
						sv.sv_handler = interrupt;
						(void) sigvec(SIGINT, &sv,
							(struct sigvec *)0);
						return;
					}
					/* propagate up */
					Exit(X_RESTART);
				}
			}
			changevol();
			goto restore_check_point;
			/* NOTREACHED */
		case X_RESTART:
			doingverify = 0;
			if (!top) {
				modlfile((u_char)LF_NEWLABELD,
					(u_char)LF_NOTUSED, (char *)0, 0);
				Exit(X_RESTART);
			}
			if (!offline && !verifylabels)
				autoload = 0;
			changevol();
			sv.sv_handler = interrupt;
			(void) sigvec(SIGINT, &sv, (struct sigvec *)0);
			return;
			/* NOTREACHED */
		default:
			msg(gettext("Bad return code from dump: %d\n"), status);
			Exit(X_ABORT);
			/*NOTREACHED*/
		}
		/*NOTREACHED*/
	} else {	/* we are the child; just continue */
#ifdef TDEBUG
		(void) sleep(4); /* time for parent's message to get out */
		/* XGETTEXT:  #ifdef TDEBUG only */
		msg(gettext(
		    "Child on Volume %d has parent %ld, my pid = %ld\n"),
			tapeno+1, (long)parentpid, (long)getpid());
#endif
		(void) sprintf(buf, gettext(
		    "Cannot open `%s'.  Do you want to retry the open?: %s"),
		    dumpdev, gettext("(\"yes\" or \"no\") "));
		if (doingverify) {
			while ((to = host ? rmtopen(tape, O_RDONLY) :
			    pipeout ? 1 : open(tape, O_RDONLY)) < 0) {
				if (autoload) {
					if (!query_once(buf, (char *)0, 1))
						dumpabort();
				} else {
					if (!query(buf, (char *)0))
						dumpabort();
				}
			}

			/*
			 * If we're using the non-rewinding tape device,
			 * the tape will be left positioned after the
			 * EOF mark.  We need to back up to the beginning
			 * of this tape file (cross two tape marks in the
			 * reverse direction and one in the forward
			 * direction) before the verify pass.
			 */
			if (host) {
				if (rmtioctl(MTBSF, 2) >= 0)
					(void) rmtioctl(MTFSF, 1);
				else
					(void) rmtioctl(MTNBSF, 1);
			} else {
				static struct mtop bsf = { MTBSF, 2 };
				static struct mtop fsf = { MTFSF, 1 };
				static struct mtop nbsf = { MTNBSF, 1 };

				if (ioctl(to, MTIOCTOP, &bsf) >= 0)
					(void) ioctl(to, MTIOCTOP, &fsf);
				else
					(void) ioctl(to, MTIOCTOP, &nbsf);
			}
		} else {
			if (!pipeout && doposition && tapeno == 0) {
				if (verifylabels)
					verifylabel();
				positiontape(buf);
				sv.sv_handler = alrm;
				(void) sigvec(SIGALRM, &sv, &osv);
				if (setjmp(alrm_buf)) {
					/*
					 * The tape is rewinding;
					 * we're screwed.
					 */
				    msg(gettext(
			    "Cannot position tape using rewind device!\n"));
				    dumpabort();
				} else
					(void) alarm(15);
				while ((to = host ? rmtopen(tape, O_WRONLY) :
				    open(tape, O_WRONLY)) < 0)
					(void) sleep(10);
				(void) alarm(0);
				(void) sigvec(SIGALRM, &osv,
				    (struct sigvec *)0);
			} else {
				int m;
				m = (access(tape, F_OK) == 0) ? 0 : O_CREAT;
				/*
				 * Only verify the tape label if label
				 * verification is on and we are at BOT
				 */
				if (!pipeout && verifylabels && filenum == 1)
					verifylabel();
				if (pipeout)
					to = 1;
				else while ((to = host ?
				    rmtopen(tape, O_WRONLY) :
				    open(tape, O_WRONLY|m, 0600))
					< 0)
					    if (!query_once(buf, (char *)0, 1))
						dumpabort();
			}
			/* update status */
			modlfile((u_char)LF_PARTIAL, (u_char)LF_USED,
				(char *)0, filenum+1);
		}
		if (!pipeout) {
			tapeout = host ? rmtstatus(&mt) >= 0 :
			    ioctl(to, MTIOCGET, &mt) >= 0;	/* set state */
			/*
			 * Make sure the tape is positioned
			 * where it is supposed to be
			 */
			if (tapeout && (tapeno > 0 || pflag) &&
			    (mt.mt_fileno != (filenum-1))) {
				(void) sprintf(buf, gettext(
				    "Warning - tape positioning error!\n\
\t%s current file %ld, should be %ld\n"),
				    tape, mt.mt_fileno+1, filenum);
				msg(buf);
				(void) opermes(LOG_CRIT, buf);
				dumpailing(gettext("tape positioning error"));
			}
		}
		tapeno++;		/* current tape sequence */
		if (tapeno < TP_NINOS)
			inos[tapeno] = chkpt.sl_inos;
		spcl.c_firstrec = chkpt.sl_firstrec;
		spcl.c_tapea = (*tapea) = chkpt.sl_tapea;
		spcl.c_volume++;

		enslave();	/* Share tape buffers with slaves */

#ifdef DEBUG
		if (xflag) {
			/* XGETTEXT:  #ifdef DEBUG only */
			msg(gettext("Checkpoint state:\n"));
			msg("    blockswritten %u\n", blockswritten);
			msg("    ino %u\n", ino);
			msg("    pos %u\n", pos);
			msg("    left %u\n", leftover);
			msg("    tapea %u\n", (*tapea));
			msg("    state %d\n", dumpstate);
		}
#endif
		spcl.c_type = TS_TAPE;
		if (leftover == 0) {
			spcl.c_count = 0;
			spclrec();
			newtape = 0;
		} else
			newtape++;	/* new volume indication */
		if (doingverify) {
			msg(gettext("Starting verify pass\n"));
			(void) opermes(LOG_INFO, gettext(
				"Starting verify pass\n"));
		} else if (tapeno > 1) {
			msg(gettext(
			    "Volume %d begins with blocks from inode %lu\n"),
				tapeno, chkpt.sl_inos);
			(void) opermes(LOG_INFO, gettext(
			    "Volume %d begins with blocks from inode %lu\n"),
			    tapeno, chkpt.sl_inos);
		}
		timeclock(1);
		(void) time(tstart_writing);
		timeest(0, spcl.c_tapea);
	}
}

/*ARGSUSED*/
void
#ifdef __STDC__
dumpabort(void)
#else
dumpabort()
#endif
{

	if (master && master != getpid())
		/*
		 * signal master to call dumpabort
		 */
		(void) kill(master, SIGTERM);
	else {
		killall();
		/*
		 * Unlock file system, before trying to remove database
		 * temporary files.
		 */
		if (lockpid == getpid())
			(void) lockfs(filesystem, "unlock");
		/*
		 * may have entered this routine
		 * as root -- unlink db and arch
		 * files as user
		 */
		(void) setreuid(-1, getuid());
		if (dbtmpfile)
			(void) unlink(dbtmpfile);
		if (archivefile)
			(void) unlink(archivefile);
		msg(gettext("The ENTIRE dump is aborted.\n"));
		if (disk)
			(void) opermes(LOG_INFO,
			    gettext("The ENTIRE dump of `%s' is aborted.\n"),
			    spcl.c_filesys[0] ? spcl.c_filesys : disk);
		else
			(void) opermes(LOG_INFO,
			    gettext("The ENTIRE dump is aborted.\n"));
	}
	Exit(X_ABORT);
}

void
dumpailing(context)
	char	*context;
{

	broadcast(gettext("DUMP IS AILING!\n"));
	if (!query(gettext(
	    "Do you want to attempt to continue? (\"yes\" or \"no\") "),
	    context))
		dumpabort();
}

void
Exit(status)
{
	pid_t mypid = getpid();

	if (lockpid == mypid)
		/*
		 * Unlock file system
		 */
		(void) lockfs(filesystem, "unlock");
	/*
	 * Clean up message system
	 */
	msgend();
#ifdef TDEBUG

	/* XGETTEXT:  #ifdef TDEBUG only */
	msg(gettext("pid = %ld exits with status %d\n"),
		(long)getpid(), status);
#endif /* TDEBUG */
	exit(status);
}

static void
#ifdef __STDC__
killall(void)
#else
killall()
#endif
{
	register struct slaves *slp;

	for (slp = &slaves[0]; slp < &slaves[SLAVES]; slp++)
		if (slp->sl_slavepid > 0) {
			(void) kill(slp->sl_slavepid, SIGKILL);
#ifdef TDEBUG

			/* XGETTEXT:  #ifdef TDEBUG only */
			msg(gettext("Slave child %ld killed\n"),
				(long)slp->sl_slavepid);
#endif
		}
	if (writepid) {
		(void) kill(writepid, SIGKILL);
#ifdef TDEBUG

		/* XGETTEXT:  #ifdef TDEBUG only */
		msg(gettext("Writer child %ld killed\n"), (long)writepid);
#endif
	}
	if (archivepid) {
		(void) kill(archivepid, SIGKILL);
#ifdef TDEBUG

		/* XGETTEXT:  #ifdef TDEBUG only */
		msg(gettext("Archiver child %ld killed\n"), (long)archivepid);
#endif
	}
}

/*ARGSUSED*/
static void
proceed(sig)
	int	sig;
{
	caught++;
}

/*ARGSUSED*/
static void
die(sig)
	int	sig;
{
	Exit(X_FINOK);
}

static void
#ifdef __STDC__
enslave(void)
#else
enslave()
#endif
{
	int cmd[2];			/* file descriptors */
	register int i;
	struct sigvec sv;

	sv.sv_flags = SA_RESTART;
#ifdef USG
	(void) sigemptyset(&sv.sa_mask);
#else
	sv.sv_mask = 0;
#endif
	master = getpid();
	/*
	 * slave sends SIGTERM on dumpabort
	 */
	sv.sv_handler = (void(*)(int))dumpabort;
	(void) sigvec(SIGTERM, &sv, (struct sigvec *)0);
	sv.sv_handler = tperror;
	(void) sigvec(SIGUSR2, &sv, (struct sigvec *)0);
	sv.sv_handler = proceed;
	(void) sigvec(SIGUSR1, &sv, (struct sigvec *)0);
	totalrecsout += recsout;
	caught = 0;
	recsout = 0;
	rotor = 0;
	bufclear();
	for (slp = &slaves[0]; slp < &slaves[SLAVES]; slp++)
		slp->sl_slavefd = -1;
	archivefd = dbtmpfd = arch = writer = -1;
	for (i = 0; i < SLAVES; i++) {
		if (pipe(cmd) < 0 || (slaves[i].sl_slavepid = fork()) < 0) {
			msg(gettext("Cannot create slave child: %s\n"),
			    strerror(errno));
			dumpabort();
		}
		slaves[i].sl_slavefd = cmd[1];
		if (slaves[i].sl_slavepid == 0) {   /* Slave starts up here */
			pid_t next;		    /* pid of neighbor */

			sv.sv_handler = SIG_DFL;
			(void) sigvec(SIGUSR2, &sv, (struct sigvec *)0);
			sv.sv_handler = SIG_IGN;	/* master handler INT */
			(void) sigvec(SIGINT, &sv, (struct sigvec *)0);
			sv.sv_handler = die;		/* normal slave exit */
			(void) sigvec(SIGTERM, &sv, (struct sigvec *)0);

			freeino();	/* release unneeded resources */
#ifdef TDEBUG
		(void) sleep(4); /* time for parent's message to get out */
		/* XGETTEXT:  #ifdef TDEBUG only */
		msg(gettext("Neighbor has pid = %ld\n"), (long)getpid());
#endif
			for (slp = &slaves[0]; slp < &slaves[SLAVES]; slp++)
				if (slp->sl_slavefd >= 0) {
					(void) close(slp->sl_slavefd);
					slp->sl_slavefd = -1;
				}
			(void) close(to);
			(void) close(fi);	    /* Need our own seek ptr */
			if ((fi = open(disk, O_RDONLY)) < 0) {
				msg(gettext(
				    "Cannot open dump device `%s': %s\n"),
					disk, strerror(errno));
				dumpabort();
			}
			if (atomic((int(*)())read, cmd[0], (char *)&next,
			    sizeof (next)) != sizeof (next)) {
				cmdrderr();
				dumpabort();
			}
			if (online)
				dumponline(cmd[0], next, i);
			else
				dumpoffline(cmd[0], next, i);
			Exit(X_FINOK);
		}
		(void) close(cmd[0]);
	}

	if (archive || database && !doingverify) {
		archivepid = setuparchive();
		if (!archivepid)
			dumpabort();
	}

	writepid = setupwriter();
	if (!writepid)
		dumpabort();

	if (arch >= 0)
		(void) close(arch);		/* only writer has this open */

	for (i = 0; i < SLAVES; i++) {
		if (atomic((int(*)())write, slaves[i].sl_slavefd,
		    (char *)&(slaves[(i + 1) % SLAVES].sl_slavepid),
		    sizeof (int)) != sizeof (int)) {
			cmdwrterr();
			dumpabort();
		}
	}
	sv.sv_handler = rollforward;		/* rcvd from writer on EOT */
	(void) sigvec(SIGUSR1, &sv, (struct sigvec *)0);
	slp = slaves;
	(void) kill(slp->sl_slavepid, SIGUSR1);
	master = 0;
}

static void
#ifdef __STDC__
wait_our_turn(void)
#else
wait_our_turn()
#endif
{
#ifndef USG
	int oldmask = sigblock(sigmask(SIGUSR1));
#else
	(void) sighold(SIGUSR1);
#endif

	if (!caught) {
#ifdef INSTRUMENT
		(*idle)++;
#endif
#ifndef USG
		(void) sigpause(0);
#else
		(void) sigpause(SIGUSR1);
#endif
	}
	caught = 0;
#ifndef USG
	(void) sigsetmask(oldmask & ~sigmask(SIGUSR1));
#else
	(void) sigrelse(SIGUSR1);
#endif
}

#ifndef TMCONV
#define	TMCONV	1
#endif

static void
dumponline(cmd, next, mynum)
	int cmd, next, mynum;
{
	register struct req *p = slaves[mynum].sl_req;
	struct stat statbuf;		/* stat structure for current file */
	struct dinode *ip;		/* inode structure for current file */
	struct timeval omtime, oatime;
	off_t	csize, osize;
	int	fd;
	caddr_t	fmap;
	long	gen;
	char	buf[256];

	(void) setreuid(-1, 0);
	while (atomic((int(*)())read, cmd, (char *)p, reqsiz) == reqsiz) {
		off_t	offset = p->ir_offset;
		int	count = p->ir_count;
		int	mode;

		active = 0;
		csize = osize = (off_t)0;
		(void) bzero((char *)&omtime, sizeof (omtime));
		(void) bzero((char *)&oatime, sizeof (oatime));
		dumptoarchive = p->aflag & BUF_ARCHIVE;
		dumptodatabase = p->aflag & BUF_DBINFO;
		if (p->tflag) {
			ip = &spcl.c_dinode;
			osize = ip->di_size;
			omtime.tv_sec = ip->di_mtime;
			omtime.tv_usec = ip->di_mtspare;
			oatime.tv_sec = ip->di_atime;
			oatime.tv_usec = ip->di_atspare;
			spcl.c_type = TS_TAPE;
		} else if (offset == 0) {
			spcl.c_type = TS_INODE;
			/*
			 * For backwards compatibility -- old dump mapped
			 * only the direct blocks in the first group
			 */
			count = howmany(NDADDR * sblock->fs_bsize, TP_BSIZE);
		} else
			spcl.c_type = TS_ADDR;
		ino = p->ir_inumber;
		gen = p->ir_igen;
		fmap = (caddr_t)0;
		fd = openi(ino, gen, (char *)0);
		if (fd >= 0) {
			if (fstat(fd, &statbuf) < 0) {
				(void) sprintf(buf, gettext(
			"Cannot obtain status of file at inode `%lu': %s\n"),
				    ino, strerror(errno));
				msg(buf);
				(void) opermes(LOG_CRIT, buf);
				dumpailing(gettext("stat error"));
			} else {
				struct dinode *realip;

				csize = statbuf.st_size;
				if (!p->tflag) {
					/*
					 * save new file state
					 */
					oatime.tv_sec = statbuf.st_atime;
					oatime.tv_usec =
					    statbuf.st_spare1 / TMCONV;
					omtime.tv_sec = statbuf.st_mtime;
					omtime.tv_usec =
					    statbuf.st_spare2 / TMCONV;
					osize = statbuf.st_size;
				} else {
					/*
					 * retain old file state
					 */
					statbuf.st_atime = oatime.tv_sec;
					statbuf.st_spare1 =
					    oatime.tv_usec * TMCONV;
					statbuf.st_mtime = omtime.tv_sec;
					statbuf.st_spare2 =
					    omtime.tv_usec * TMCONV;
					statbuf.st_size = osize;
				}
				ip = &spcl.c_dinode;
				stattoi(&statbuf, ip);
				realip = getino(ino);
				if ((statbuf.st_mode & IFMT) == 0)
					bcopy(realip, &spcl.c_dinode,
					    sizeof (spcl.c_dinode));
				else
					spcl.c_dinode.di_shadow =
						realip->di_shadow;
				if (hasshortmeta(&realip)) {
					struct dinode *ship;

					spcl.c_flags |= DR_HASMETA;
					ship = getino(realip->di_shadow);
					bread(fsbtodb(sblock, ship->di_db[0]),
					    spcl.c_shadow.c_shadow,
					    sizeof (spcl.c_shadow));
					realip = getino(ino);
				} else
				    spcl.c_flags &= ~DR_HASMETA;

				spcl.c_count = 0;
				mode = (int) (statbuf.st_mode & IFMT);
				if (mode == 0)
					mode = (int) (realip->di_mode & IFMT);
				if (statbuf.st_nlink == 0) /* unallocated? */
					mode = 0;
				if (mode == IFREG)
					fmap = mapfile(fd, offset, csize, 1);
			}
		} else if (errno == EOPNOTSUPP) {
			/*
			 * fcntl(F_CNVT) won't work on sockets
			 */
			ip = getino(ino);
			if (ip->di_gen == gen) {
				spcl.c_dinode = *ip;
				spcl.c_count = 0;
				mode = (int) (ip->di_mode & IFMT);
			} else
				mode = 0;	/* changed */
		} else {
			mode = 0;
		}
		wait_our_turn();
		switch (mode) {
		case IFDIR:
		case IFLNK:
			dumptodatabase++;
			/*FALLTHROUGH*/
		case IFREG:
		case IFSHAD:
			dumpfile(fd, fmap, offset, csize, osize, count, mode);
			break;
		case 0:
			if (p->tflag)
				/*
				 * The file was removed out from under us
				 * and we are supposed to write the TS_TAPE
				 * record.  This shouldn't happen, but could
				 * if locking has been released.  dumpfile()
				 * will do what we want.
				 */
				dumpfile(-1, (caddr_t)0, offset, csize,
				    -1, count, mode);
			break;
		default:
			spclrec();
		}
		(void) kill(next, SIGUSR1);	/* Next slave's turn */
		if (!readonly && (mode == IFREG || mode == IFDIR)) {
			/*
			 * Check for revised mod time (file activity)
			 */
			if (fstat(fd, &statbuf) < 0) {
				(void) sprintf(buf, gettext(
			"Cannot obtain status of file at inode `%lu': %s\n"),
				    ino, strerror(errno));
				msg(buf);
				(void) opermes(LOG_CRIT, buf);
				dumpailing(gettext("stat error"));
			} else {
				omtime.tv_sec ^= statbuf.st_mtime;
				omtime.tv_usec ^= (statbuf.st_spare2 / TMCONV);
				if (omtime.tv_sec || omtime.tv_usec)
					active = 1;
				/*
				 * Reset access and mod times if
				 * the file was inactive.
				 */
				ip = &spcl.c_dinode;
				if (getfsreset() && active == 0 &&
				    ioctl(fd, _FIOSATIME,
				    (struct timeval *)&ip->di_atime) < 0) {
					(void) sprintf(buf, gettext(
		    "Cannot reset access time of file at inode `%lu': %s\n"),
					    ino, strerror(errno));
					msg(buf);
					(void) opermes(LOG_WARNING, buf);
				}
				if (active) {
					/*
					 * The file was active; find
					 * out what to do: report it,
					 * redump it, or ignore it.
					 */
					action_t action =
					    getfsaction(statbuf.st_size,
						doingactive);
					if (action == report ||
					    action == reportandretry)
						msg(gettext(
				"Warning - file at inode `%lu' was active\n"),
							ino);
					/* ignore it if not retrying */
					if (action != retry &&
					    action != reportandretry)
						active = 0;
				}
			}
		}
		/*
		 * Send back file activity info
		 * if supposed to re-dump file.
		 */
		if (atomic((int(*)())write, cmd, (char *)&active,
		    sizeof (active)) != sizeof (active)) {
			cmdwrterr();
			dumpabort();
		}
		unmapfile();
		if (fd >= 0)
			(void) close(fd);
	}
}

static void
dumpoffline(cmd, next, mynum)
	int cmd, next, mynum;
{
	register struct req *p = (struct req *) slaves[mynum].sl_req;
	register int i;
	register char *cp;
	char	*blkbuf;
	int	notactive = 0;

	blkbuf = xmalloc(sblock->fs_bsize);

	while (atomic((int(*)())read, cmd, (char *)p, reqsiz) == reqsiz) {
		if (p->br_dblk) {
			bread(p->br_dblk, blkbuf, p->br_size);
		} else {
			(void) bcopy((char *)p->br_spcl, (char *)&spcl,
			    TP_BSIZE);
			ino = spcl.c_inumber;
		}
		dumptoarchive = p->aflag & BUF_ARCHIVE;
		dumptodatabase = p->aflag & BUF_DBINFO;
		wait_our_turn();
		if (p->br_dblk)
			for (i = p->br_size, cp = blkbuf;
			    i > 0; i -= TP_BSIZE, cp += TP_BSIZE)
				taprec(cp, 0);
		else
			spclrec();
		(void) kill(next, SIGUSR1);	/* Next slave's turn */
		if (atomic((int(*)())write, cmd, (char *)&notactive,
		    sizeof (notactive)) != sizeof (notactive)) {
			cmdwrterr();
			dumpabort();
		}
	}
}

static int count;		/* tape blocks written since last spclrec */

/*ARGSUSED*/
static void
onxfsz(sig)
	int	sig;
{
	msg(gettext("File size limit exceeded writing output volume %d\n"),
	    tapeno);
	(void) kill(master, SIGUSR2);
	Exit(X_REWRITE);
}

static long	lastnonaddr;			/* last INODE, CLRI, BITS rec */
static long	lastnonaddrm;
/*
 * dowrite -- the main body of the output writer process
 */
static void
dowrite(cmd)
	int	cmd;
{
	struct bdesc *last =
	    &bufp[(NBUF*ntrec)-1];		/* last buffer in pool */
	struct bdesc *bp = bufp;		/* current buf in tape block */
	struct bdesc *begin = bufp;		/* first buf of tape block */
	struct bdesc *end = bufp + (ntrec-1);	/* last buf of tape block */
	int siz;				/* bytes written (block) */
	int trecs;				/* records written (block)  */
	long asize = 0;				/* number of 0.1" units... */
						/* ...written on current tape */
	char *tp, *rbuf;
	char *recmap = spcl.c_addr;		/* current tape record map */
	char *endmp;				/* end of valid map data */
	register char *mp;			/* current map entry */
	union u_spcl *sp;
#ifndef USG
	int oldmask;
#endif

	(void) signal(SIGXFSZ, onxfsz);

	(void) bzero((char *)&spcl, sizeof (union u_spcl));
	count = 0;

	if (doingverify) {
		rbuf = (char *)malloc((u_int)writesize);
		if (rbuf == 0) {
			/* Restart from checkpoint */
			(void) kill(master, SIGUSR2);
			Exit(X_REWRITE);
		}
	}

	for (;;) {
		if ((bp->b_flags & BUF_FULL) == 0) {
			if (caught) {		/* master signalled flush */
#ifndef USG
				(void) sigblock(sigmask(SIGUSR1));
#else
				(void) sighold(SIGUSR1);
#endif
				caught = 0;
				/* signal ready */
				(void) kill(master, SIGUSR1);
				chkpt.sl_count = 0;	/* signal not at EOT */
				checkpoint(--bp, cmd);	/* send data */
#ifndef USG
				(void) sigpause(0);
#else
				(void) sigpause(SIGUSR1);
#endif
				break;
			}
#ifdef INSTRUMENT
			(*readmissp)++;
#endif
			nap(50);
			continue;
		}
		if (bp < end) {
			bp++;
			continue;
		}
		tp = begin->b_data;
#ifndef USG
		oldmask = sigblock(sigmask(SIGUSR1));
#else
		(void) sighold(SIGUSR1);
#endif
		if (host) {
			if (!doingverify)
				siz = rmtwrite(tp, writesize);
			else if ((siz = rmtread(rbuf, writesize)) ==
			    writesize && bcmp(rbuf, tp, writesize))
				siz = -1;
		} else {
			if (!doingverify)
				siz = write(to, tp, writesize);
			else if ((siz = read(to, rbuf, writesize)) ==
			    writesize && bcmp(rbuf, tp, writesize))
				siz = -1;
			if (siz < 0 && diskette && errno == ENOSPC)
				siz = 0;	/* really EOF */
		}
#ifndef USG
		(void) sigsetmask(oldmask & ~sigmask(SIGUSR1));
#else
		(void) sigrelse(SIGUSR1);
#endif
		if (siz < 0 ||
		    (pipeout && siz != writesize)) {
			char buf[3000];

			/*
			 * Isn't i18n wonderful?
			 */
			if (doingverify) {
				if (diskette)
					(void) sprintf(buf, gettext(
		    "Verification error %ld blocks into diskette %d\n"),
						asize * 2, tapeno);
				else if (tapeout)
					(void) sprintf(buf, gettext(
		    "Verification error %ld feet into tape %d\n"),
					    (cartridge ? asize/tracks :
						asize)/120L,
					    tapeno);
				else
					(void) sprintf(buf, gettext(
		    "Verification error %ld blocks into volume %d\n"),
						asize * 2, tapeno);

			} else {
				if (diskette)
					(void) sprintf(buf, gettext(
			"Write error %ld blocks into diskette %d\n"),
						asize * 2, tapeno);
				else if (tapeout)
					(void) sprintf(buf, gettext(
			"Write error %ld feet into tape %d\n"),
					    (cartridge ? asize/tracks :
						asize)/120L, tapeno);
				else
					(void) sprintf(buf, gettext(
			"Write error %ld blocks into volume %d\n"),
						asize * 2, tapeno);
			}

			msg(buf);
			(void) opermes(LOG_CRIT, buf);
			modlfile((u_char)LF_ERRORED, (u_char)LF_USED,
				(char *)0, 0);
			/* Restart from checkpoint */
#ifdef TDEBUG

			/* XGETTEXT:  #ifdef TDEBUG only */
			msg(gettext("sending SIGUSR2 to pid %ld\n"), master);
#endif
			(void) kill(master, SIGUSR2);
			Exit(X_REWRITE);
		}
		trecs = siz / TP_BSIZE;
		if (diskette)
			asize += trecs;	/* asize == blocks written */
		else
			asize += (siz/density + tenthsperirg);
		if (trecs)
			chkpt.sl_firstrec++;
		for (bp = begin; bp < begin + trecs; bp++) {
			if (arch >= 0 &&
			    bp->b_flags & (BUF_ARCHIVE|BUF_DBINFO)) {
				if (atomic((int(*)())write, arch,
				    (char *)&bp->b_flags, sizeof (int))
				    != sizeof (int)) {
					cmdwrterr();
					dumpabort();
				}
				if (atomic((int(*)())write, arch, bp->b_data,
				    TP_BSIZE) != TP_BSIZE) {
					cmdwrterr();
					dumpabort();
				}
			}
			if (bp->b_flags & BUF_SPCLREC) {
				/*LINTED [bp->b_data is aligned]*/
				sp = (union u_spcl *) bp->b_data;
				if (sp->s_spcl.c_type != TS_ADDR) {
					lastnonaddr = sp->s_spcl.c_type;
					lastnonaddrm =
						sp->s_spcl.c_dinode.di_mode;
					if (sp->s_spcl.c_type != TS_TAPE)
						chkpt.sl_offset = 0;
				}
				chkpt.sl_count = sp->s_spcl.c_count;
				(void) bcopy((char *)sp,
					(char *)&spcl, TP_BSIZE);
				mp = recmap;
				endmp = &recmap[spcl.c_count];
				count = 0;
			} else {
				chkpt.sl_offset++;
				chkpt.sl_count--;
				count++;
				mp++;
			}
			/*
			 * Adjust for contiguous hole
			 */
			for (; mp < endmp; mp++) {
				if (*mp)
					break;
				chkpt.sl_offset++;
				chkpt.sl_count--;
			}
		}
		/*
		 * Check for end of tape
		 */
		if (trecs < ntrec ||
		    (!pipeout && tsize > 0 && asize > tsize)) {
			if (tapeout)
				msg(gettext("End-of-tape detected\n"));
			else
				msg(gettext("End-of-file detected\n"));
#ifndef USG
			(void) sigblock(sigmask(SIGUSR1));
#else
			(void) sighold(SIGUSR1);
#endif
			caught = 0;
			(void) kill(master, SIGUSR1);	/* signal EOT */
			checkpoint(--bp, cmd);	/* send checkpoint data */
#ifndef USG
			(void) sigpause(0);
#else
			(void) sigpause(SIGUSR1);
#endif
			break;
		}
		for (bp = begin; bp <= end; bp++)
			bp->b_flags = BUF_EMPTY;
		if (end + ntrec > last) {
			bp = begin = bufp;
			timeest(0, spcl.c_tapea);
		} else
			bp = begin = end+1;
		end = begin + (ntrec-1);
	}
}

/*
 * Send checkpoint info back to master.  This information
 * consists of the current inode number, number of logical
 * blocks written for that inode (or bitmap), the last logical
 * block number written, the number of logical blocks written
 * to this volume, the current dump state, and the current
 * special record map.
 */
static void
checkpoint(bp, cmd)
	struct bdesc *bp;
	int	cmd;
{
	int	state, type;
	ino_t	ino;

	if (++bp >= &bufp[NBUF*ntrec])
		bp = bufp;

	/*
	 * If we are dumping files and the record following
	 * the last written to tape is a special record, use
	 * it to get an accurate indication of current state.
	 */
	if ((bp->b_flags & BUF_SPCLREC) && (bp->b_flags & BUF_FULL) &&
	    lastnonaddr == TS_INODE) {
		/*LINTED [bp->b_data is aligned]*/
		union u_spcl *nextspcl = (union u_spcl *) bp->b_data;

		if (nextspcl->s_spcl.c_type == TS_INODE) {
			chkpt.sl_offset = 0;
			chkpt.sl_count = 0;
		} else if (nextspcl->s_spcl.c_type == TS_END) {
			chkpt.sl_offset = 0;
			chkpt.sl_count = 1;	/* EOT indicator */
		}
		ino = nextspcl->s_spcl.c_inumber;
		type = nextspcl->s_spcl.c_type;
	} else {
		/*
		 * If not, use what we have.
		 */
		ino = spcl.c_inumber;
		type = spcl.c_type;
	}

	switch (type) {		/* set output state */
	case TS_ADDR:
		switch (lastnonaddr) {
		case TS_INODE:
		case TS_TAPE:
			if ((lastnonaddrm & IFMT) == IFDIR)
				state = DIRS;
			else
				state = FILES;
			break;
		case TS_CLRI:
			state = CLRI;
			break;
		case TS_BITS:
			state = BITS;
			break;
		}
		break;
	case TS_INODE:
		if ((spcl.c_dinode.di_mode & IFMT) == IFDIR)
			state = DIRS;
		else
			state = FILES;
		break;
	case 0:			/* EOT on 1st record */
	case TS_TAPE:
		state = START;
		ino = UFSROOTINO;
		break;
	case TS_CLRI:
		state = CLRI;
		break;
	case TS_BITS:
		state = BITS;
		break;
	case TS_END:
		if (spcl.c_type == TS_END)
			state = DONE;
		else
			state = END;
		break;
	}

	/*
	 * Checkpoint info to be processed by rollforward():
	 *	The inode with which the next volume should begin
	 *	The last inode number on this volume
	 *	The last logical block number on this volume
	 *	The current output state
	 *	The offset within the current inode (already in sl_offset)
	 *	The number of records left from last spclrec (in sl_count)
	 *	The physical block the next vol begins with (in sl_firstrec)
	 */
	chkpt.sl_inos = ino;
	chkpt.sl_tapea = spcl.c_tapea + count;
	chkpt.sl_state = state;

	if (atomic((int(*)())write, cmd, (char *)&chkpt,
	    sizeof (struct slaves)) != sizeof (struct slaves)) {
		cmdwrterr();
		dumpabort();
	}
	if (atomic((int(*)())write, cmd, (char *)&spcl, TP_BSIZE) != TP_BSIZE) {
		cmdwrterr();
		dumpabort();
	}
#ifdef DEBUG
	if (xflag) {
		/* XGETTEXT:  #ifdef DEBUG only */
		msg(gettext("sent chkpt to master:\n"));
		msg("    ino %u\n", chkpt.sl_inos);
		msg("    1strec %u\n", chkpt.sl_firstrec);
		msg("    lastrec %u\n", chkpt.sl_tapea);
		msg("    written %u\n", chkpt.sl_offset);
		msg("    left %u\n", chkpt.sl_count);
		msg("    state %d\n", chkpt.sl_state);
	}
#endif
}

/*
 * Since a read from a pipe may not return all we asked for,
 * or a write may not write all we ask if we get a signal,
 * loop until the count is satisfied (or error).
 */
static int
atomic(func, fd, buf, count)
	int (*func)(), fd, count;
	char *buf;
{
	int got = 0, need = count;
	extern int errno;

	while (need > 0) {
		got = (*func)(fd, buf, MIN(need, 4096));
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0)
			break;
		buf += got;
		need -= got;
	}
	return ((count -= need) == 0 ? got : count);
}

char *
xmalloc(bytes)
	size_t bytes;
{
	char *cp;

	cp = malloc(bytes);
	if (cp == NULL) {
		msg(gettext("Cannot allocate memory: %s\n"), strerror(errno));
		dumpabort();
	}
	return (cp);
}

void
#ifdef __STDC__
positiontape(char *msgbuf)
#else
positiontape(msgbuf)
	char *msgbuf;
#endif
{
	/* Why static? */
	static struct mtget mt;
	static struct mtop rew = { MTREW, 1 };
	static struct mtop fsf = { MTFSF, 1 };
	char *info = gettext("Positioning `%s' to file %ld\n");
	char *fail = gettext("Cannot position tape to file %d\n");
	int m;

	m = (access(tape, F_OK) == 0) ? 0 : O_CREAT;

	/*
	 * To avoid writing tape marks at inappropriate places, we open the
	 * device read-only, position it, close it, and reopen it for writing.
	 */
	while ((to = host ? rmtopen(tape, O_RDONLY) :
	    open(tape, O_RDONLY|m, 0600)) < 0) {
		if (autoload) {
			if (!query_once(msgbuf, (char *)0, 1))
				dumpabort();
		} else {
			if (!query(msgbuf, (char *)0))
				dumpabort();
		}
	}

	if (host) {
		if (rmtstatus(&mt) >= 0 &&
		    rmtioctl(MTREW, 1) >= 0 &&
		    filenum > 1) {
			msg(info, dumpdev, filenum);
			(void) opermes(LOG_INFO, info, dumpdev, filenum);
			if (rmtioctl(MTFSF, filenum-1) < 0) {
				msg(fail, filenum);
				dumpabort();
			}
		}
		rmtclose();
	} else {
		if (ioctl(to, MTIOCGET, &mt) >= 0 &&
		    ioctl(to, MTIOCTOP, &rew) >= 0 &&
		    filenum > 1) {
			msg(info, dumpdev, filenum);
			(void) opermes(LOG_INFO, info, dumpdev, filenum);
			fsf.mt_count = (daddr_t) filenum-1;
			if (ioctl(to, MTIOCTOP, &fsf) < 0) {
				msg(fail, filenum);
				dumpabort();
			}
		}
		(void) close(to);
	}
}

static void
#ifdef __STDC__
cmdwrterr(void)
#else
cmdwrterr()
#endif
{
	msg(gettext("Error writing command pipe: %s\n"), strerror(errno));
}

static void
#ifdef __STDC__
cmdrderr(void)
#else
cmdrderr()
#endif
{
	msg(gettext("Error reading command pipe: %s\n"), strerror(errno));
}
