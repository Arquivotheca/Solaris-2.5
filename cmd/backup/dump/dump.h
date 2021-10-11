#pragma ident	"@(#)dump.h	1.49	94/11/08 SMI"

#define	NI		16
#define	MAXINOPB	(MAXBSIZE / sizeof (struct dinode))
#define	MAXNINDIR	(MAXBSIZE / sizeof (daddr_t))

#include <stdio.h>
#include <locale.h>
#include <sys/types.h>
#include <ctype.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <fcntl.h>
#include <utmp.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <sys/param.h>	/* for MAXBSIZE */
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/vnode.h>	/* needed by inode.h */
#include <setjmp.h>
#ifdef USG
#include "dumpusg.h"
#else
#include <malloc.h>
#include <fstab.h>
#include <mntent.h>
#include <ufs/inode.h>
#include <ufs/fs.h>
#include <ufs/fsdir.h>

#define	SA_RESTART	0	/* for sigvec flags */
#endif
#include <protocols/dumprestore.h>
#include <metamucil.h>

#ifndef roundup
#define	roundup(x, y)	((((x)+((y)-1))/(y))*(y))
#endif
#ifndef MIN
#define	MIN(a, b)	(((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define	MAX(a, b)	(((a) > (b)) ? (a) : (b))
#endif

/*
 * Define an overflow-free version of howmany so that we don't
 * run into trouble with large files.
 */
#undef	howmany
#define	howmany(x, y)	((x) / (y) + ((x) % (y) != 0))

#define	MWORD(m, i)	(m[(unsigned)(i-1)/NBBY])
#define	MBIT(i)		(1<<((unsigned)(i-1)%NBBY))
#define	BIS(i, w)	(MWORD(w, i) |=  MBIT(i))
#define	BIC(i, w)	(MWORD(w, i) &= ~MBIT(i))
#define	BIT(i, w)	(MWORD(w, i) & MBIT(i))

u_int	msiz;
char	*clrmap;
char	*dirmap;
char	*filmap;
char	*nodmap;
char	*shamap;
char	*activemap;

/*
 *	All calculations done in 0.1" units!
 */

char	*disk;		/* name of the disk file */
char	*tape;		/* name of the tape file */
char	*host;		/* name of the remote tape host */
char	*dumpdev;	/* hostname:device for current volume */
char	*sdumpdev;	/* short form of dumpdev (no hostname if local) */
char	*increm;	/* name of file containing incremental information */
char	*filesystem;	/* name of the file system */
char	*labelfile;	/* name of file containing tape label names */
char	*helplist;	/* where to send mail if we need help */
char	*myname;	/* argv[0] without leading path components */
char	lastincno;	/* increment number of previous dump */
char	incno;		/* increment number */
int	uflag;		/* update flag */
int	pflag;		/* check tape file position flag */
int	fi;		/* disk file descriptor */
int	to;		/* tape file descriptor */
int	pipeout;	/* true => output to standard output */
int	tapeout;	/* true => output to a tape drive */
ino_t	ino;		/* current inumber; used globally */
ino_t	realino;	/* inumber for non-shadow data */
off_t	pos;		/* starting offset within ino; used globally */
int	leftover;	/* number of tape recs left over from prev vol */
int	nsubdir;
int	newtape;	/* new tape flag */
int	nadded;		/* number of added sub directories */
int	dadded;		/* directory added flag */
int	density;	/* density in 0.1" units */
long	tsize;		/* tape size in 0.1" units */
long long esize;	/* estimated tape size, blocks */
int	etapes;		/* estimated number of tapes */
int	ntrec;		/* 1K records per tape block */
int	tenthsperirg;	/* 1/10" per tape inter-record gap */
dev_t	device;		/* id of BLOCK device being dumped */
dev_t 	partial_dev;	/* id of BLOCK device used in partial mode */
pid_t	lockpid;	/* process-ID of file system lock holder */
pid_t	dumppid;	/* process-ID of top-level process */
int	ndevices;	/* number of devices we'll use */

int	verify;		/* verify each volume */
int	doingverify;	/* true => doing a verify pass */
int	active;		/* recopy active files */
int	doingactive;	/* true => redumping active files */
int	archive;	/* true => saving a archive in archivefile */
char	*archivefile;	/* name of archivefile */
char	*dbtmpfile;	/* name of database tmp file */
int	notify;		/* notify operator flag */
int	diskette;	/* true if dumping to a diskette */
int	cartridge;	/* true if dumping to a cartridge tape */
int	tracks;		/* number of tracks on a cartridge tape */
int	printsize;	/* just print estimated size and exit */
int	offline;	/* take tape offline after rewinding */
int	autoload;	/* wait for next tape to autoload; implies offline */
int	doposition;	/* move to specified... */
daddr_t	filenum;	/* position of dump on 1st volume */
int	online;		/* peform dump using on-line mode */
int	readonly;	/* file system dumped is completely locked */
int	trueinc;	/* perform true incremental dump */
int	nodump;		/* don't perform dump -- just check dump.conf */
int	recover;	/* true => look for data files and udpate database */
int	database;	/* update database of backed-up files */
int	dumpstate;	/* dump output state (see below) */
int	dumptoarchive;	/* mark records to be archived */
int	dumptodatabase;	/* mark records to be sent to database */
int	verifylabels;	/* verify tape labels */

int	blockswritten;	/* number of blocks written on current tape */
int	tapeno;		/* current tape number */
time_t	*telapsed;	/* time spent writing previous tapes */
time_t	*tstart_writing;	/* when we started writing the latest tape */
struct fs *sblock;	/* the file system super block */
int	shortmeta;	/* current file has small amount of metadata */
union u_shadow c_shadow_save[1];

/*
 * Defines for the msec part of
 * inode-based times, since we're
 * not part of the kernel.
 */
#define	di_atspare	di_ic.ic_atspare
#define	di_mtspare	di_ic.ic_mtspare
#define	di_ctspare	di_ic.ic_ctspare

#define	HOUR	(60L*60L)
#define	DAY	(24L*HOUR)
#define	YEAR	(365L*DAY)

/*
 *	Dump output states
 */
#define	INIT		0
#define	START		1
#define	CLRI		2
#define	BITS		3
#define	DIRS		4
#define	FILES		5
#define	END		6
#define	DONE		7

/*
 *	Exit status codes
 */
#define	X_FINOK		0	/* normal exit */
#define	X_REWRITE	2	/* restart writing from the check point */
#define	X_ABORT		3	/* abort all of dump; no checkpoint restart */
#define	X_VERIFY	4	/* verify the reel just written */
#define	X_RESTART	5	/* abort all progress so far; attempt restart */

#define	NINCREM	"/etc/dumpdates"	/* new format incremental info */

#define	TAPE	"/dev/rmt/0b"		/* default tape device */
#define	OPGRENT	"sys"			/* group entry to notify */
#define	DIALUP	"ttyd"			/* prefix for dialups */

#define	DISKETTE	"/dev/rfd0c"

#define	NBUF		64		/* number of output buffers */
#define	MAXNTREC	256		/* max tape blocking factor (in Kb) */

/*
 *	The contents of the file NINCREM are maintained both on
 *	a linked list and then (eventually) arrayified.
 */
struct	idates {
	char	id_name[MAXNAMLEN+3];
	char	id_incno;
	time_t	id_ddate;
};

int	nidates;		/* number of records (might be zero) */
struct	idates	**idatev;	/* the arrayfied version */
#define	ITITERATE(i, ip)	\
	for (i = 0; i < nidates && (ip = idatev[i]) != NULL; i++)

/*
 * Function declarations
 */
#ifdef __STDC__
/*
 * dumpdatabase.c
 */
extern void doupdate(char *);
extern void creatdbtmp(time_t);
extern int initdbtmp(void);
extern int opendbtmp(caddr_t);
extern void savedbtmp(int);
extern caddr_t statdbtmp(int);
extern void closedbtmp(int);
extern void writedbtmp(int, char *, int);
extern void dbtmpest(struct dinode *, long);
/*
 * dumpfstab.c
 */
extern void mnttabread(void);
extern struct mntent *mnttabsearch(char *, int);
extern void setmnttab(void);
extern struct mntent *getmnttab(void);
/*
 * dumpitime.c
 */
extern char *prdate(time_t, int);
extern void inititimes(void);
extern void getitime(void);
extern void putitime(void);
extern void est(struct dinode *);
extern void bmapest(char *);
/*
 * dumplabel.c
 */
extern void readlfile(void);
extern void buildlfile(char *);
extern void getlabel(void);
extern void modlfile(int, int, char *, daddr_t);
extern void verifylabel(void);
/*
 * dumpmain.c
 */
extern void createdefaultfs(int);
extern void sigAbort(int);
extern char *rawname(char *);
extern unsigned long timeclock(int);
#ifdef signal
extern void (*nsignal(int, void (*)(int)))(int);
#endif
/*
 * dumponline.c
 */
extern void allocino(void);
extern void freeino(void);
extern void saveino(ino_t, struct dinode *);
extern void resetino(ino_t);
extern long getigen(ino_t);
extern int ismounted(char *, char *);
extern int checkonline(void);
extern int isoperator(int, int);
extern char *okwrite(char *, int);
extern int lockfs(char *, char *);
extern int openi(ino_t, long, char *);
extern caddr_t mapfile(int, off_t, off_t, int);
extern void unmapfile(void);
extern void stattoi(struct stat *, struct dinode *);
extern void dumpfile(int, caddr_t, off_t, off_t, off_t, int, int);
extern void activepass(void);
/*
 * dumpoptr.c
 */
extern int query(char *, char *);
extern int query_once(char *, char *, int);
extern void interrupt(int);
extern void set_operators(void);
extern void broadcast(char *);
extern void timeest(int, int);
/*PRINTFLIKE1*/
extern void msg(const char *, ...);
/*PRINTFLIKE1*/
extern void msgtail(const char *, ...);
/*PRINTFLIKE2*/
extern u_long opermes(const int, const char *, ...);
extern char *strerror(int);
extern void lastdump(int);
extern char *getinput(char *, char *);
extern void msginit(void);
extern void msgend(void);
extern void setupmail(void);
extern void sendmail(void);
/*
 * dumptape.c
 */
extern void alloctape(void);
extern void reset(void);
extern void spclrec(void);
extern void taprec(char *, int);
extern void dmpblk(daddr_t, long, off_t);
extern void toslave(void (*)(ino_t), ino_t);
extern void doinode(ino_t);
extern void dospcl(ino_t);
extern void flushcmds(void);
extern void flusht(void);
extern void nextdevice(void);
extern int isrewind(int);
extern void trewind(void);
extern void close_rewind(void);
extern void changevol(void);
extern void otape(int);
extern void dumpabort(void);
extern void dumpailing(char *);
extern void Exit(int);
extern char *xmalloc(size_t);
extern void positiontape(char *);
/*
 * dumptraverse.c
 */
extern void pass(void (*)(struct dinode *), char *);
extern void mark(struct dinode *);
extern void active_mark(struct dinode *);
extern void markshad(struct dinode *);
extern void estshad(struct dinode *);
extern void freeshad();
extern void add(struct dinode *);
extern void dirdump(struct dinode *);
extern void dump(struct dinode *);
extern void dumpblocks(ino_t);
extern void bitmap(char *, int);
extern struct dinode *getino(ino_t);
extern void bread(daddr_t, char *, long);
/*
 * lftw.c
 */
extern int lftw(const char *,
	int (*)(const char *, const struct stat *, int), int);
/*
 * partial.c
 */
extern void partial_check(void);
extern int partial_mark(int, char **);
/*
 * unctime.c
 */
extern time_t unctime(char *);
/*
 * XXX -- bugs in header files
 */
extern int munmap(caddr_t, size_t);
extern caddr_t mmap(caddr_t, size_t, int, int, int, off_t);
extern int msync(caddr_t, size_t, int);
extern select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
#else	/* !STDC */
/*
 * dumpdatabase.c
 */
extern void doupdate();
extern void creatdbtmp();
extern int initdbtmp();
extern int opendbtmp();
extern void savedbtmp();
extern caddr_t statdbtmp();
extern void closedbtmp();
extern void writedbtmp();
extern void dbtmpest();
/*
 * dumpfstab.c
 */
extern void mnttabread();
extern struct mntent *mnttabsearch();
extern void setmnttab();
extern struct mntent *getmnttab();
/*
 * dumpitime.c
 */
extern char *prdate();
extern void inititimes();
extern void getitime();
extern void putitime();
extern void est();
extern void bmapest();
/*
 * dumplabel.c
 */
extern void readlfile();
extern void buildlfile();
extern void getlabel();
extern void modlfile();
extern void verifylabel();
/*
 * dumpmain.c
 */
extern void sigAbort();
extern char *rawname();
extern unsigned long timeclock();
#ifdef signal
extern void nsignal();
#endif
/*
 * dumponline.c
 */
extern void allocino();
extern void freeino();
extern void saveino();
extern void resetino();
extern long getigen();
extern int ismounted();
extern int checkonline();
extern int isoperator();
extern char *okwrite();
extern int lockfs();
extern int openi();
extern caddr_t mapfile();
extern void unmapfile();
extern void stattoi();
extern void dumpfile();
extern void activepass();
/*
 * dumpoptr.c
 */
extern int query();
extern int query_once();
extern void interrupt();
extern void set_operators();
extern void broadcast();
extern void timeest();
extern void msg();
extern void msgtail();
extern u_long opermes();
extern char *strerror();
extern void lastdump();
extern char *getinput();
extern void msginit();
extern void msgend();
extern void setupmail();
extern void sendmail();
/*
 * dumptape.c
 */
extern void alloctape();
extern void reset();
extern void spclrec();
extern void taprec();
extern void dmpblk();
extern void toslave();
extern void doinode();
extern void dospcl();
extern void flushcmds();
extern void flusht();
extern void nextdevice();
extern int isrewind();
extern void trewind();
extern void close_rewind();
extern void changevol();
extern void otape();
extern void dumpabort();
extern void dumpailing();
extern void Exit();
extern char *xmalloc();
extern void positiontape();
/*
 * dumptraverse.c
 */
extern void pass();
extern void mark();
extern void active_mark();
extern void markshad();
extern void estshad();
extern void freeshad();
extern void add();
extern void dirdump();
extern void dump();
extern void dumpblocks();
extern void bitmap();
extern struct dinode *getino();
extern void bread();
/*
 * lftw.c
 */
extern int lftw();
/*
 * partial.c
 */
extern void partial_check();
extern int partial_mark();
/*
 * unctime.c
 */
extern time_t unctime();
/*
 * XXX -- bugs in header files
 */
extern int munmap();
extern caddr_t mmap();
extern int msync();
extern select();
#endif
