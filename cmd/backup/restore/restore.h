/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)restore.h	1.15	94/07/18 SMI"

#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/vnode.h>
#ifdef USG
#include <locale.h>
#include <stdlib.h>
#include <sys/fs/ufs_inode.h>
#include <sys/fs/ufs_fs.h>
#include <sys/fs/ufs_fsdir.h>
#define	ROOTINO	UFSROOTINO
#else
#include <ufs/inode.h>
#include <ufs/fs.h>
#include <ufs/fsdir.h>
#endif
#include <protocols/dumprestore.h>
#include <metamucil.h>

/*
 * Flags
 */
extern int	cvtflag;	/* convert from old to new tape format */
extern int	bflag;		/* set input block size */
extern int	dflag;		/* print out debugging info */
extern int	hflag;		/* restore heirarchies */
extern int	mflag;		/* restore by name instead of inode number */
extern int	vflag;		/* print out actions taken */
extern int	yflag;		/* always try to recover from tape errors */
/*
 * Global variables
 */
extern int	errno;
extern struct byteorder_ctx *byteorder;
extern char	*progname;	/* our name */
extern char	*dumpmap; 	/* map of inodes on this dump tape */
extern char	*clrimap; 	/* map of inodes to be deleted */
extern ino_t	maxino;		/* highest numbered inode in this file system */
extern long	dumpnum;	/* location of the dump on this tape */
extern long	volno;		/* current volume being read */
extern long	ntrec;		/* number of TP_BSIZE records per tape block */
extern time_t	dumptime;	/* time that this dump begins */
extern time_t	dumpdate;	/* time that this dump was made */
extern char	command;	/* opration being performed */
extern FILE	*terminal;	/* file descriptor for the terminal input */

/*
 * Each file in the file system is described by one of these entries
 */
struct entry {
	char	*e_name;		/* the current name of this entry */
	u_char	e_namlen;		/* length of this name */
	char	e_type;			/* type of this entry, see below */
	short	e_flags;		/* status flags, see below */
	ino_t	e_ino;			/* inode number in previous file sys */
	long	e_index;		/* unique index (for dumpped table) */
	struct	entry *e_parent;	/* pointer to parent directory (..) */
	struct	entry *e_sibling;	/* next element in this directory (.) */
	struct	entry *e_links;		/* hard links to this inode */
	struct	entry *e_entries;	/* for directories, their entries */
	struct	entry *e_next;		/* hash chain list */
};
/* types */
#define	LEAF 1			/* non-directory entry */
#define	NODE 2			/* directory entry */
#define	LINK 4			/* synthesized type, stripped by addentry */
/* flags */
#define	EXTRACT		0x0001	/* entry is to be replaced from the tape */
#define	NEW		0x0002	/* a new entry to be extracted */
#define	KEEP		0x0004	/* entry is not to change */
#define	REMOVED		0x0010	/* entry has been removed */
#define	TMPNAME		0x0020	/* entry has been given a temporary name */
#define	EXISTED		0x0040	/* directory already existed during extract */
/*
 * functions defined on entry structs
 */
#ifdef __STDC__
extern struct entry *lookupino(ino_t);
extern struct entry *lookupname(char *);
extern struct entry *addentry(char *, ino_t, int);
extern void deleteino(ino_t);
extern char *myname(struct entry *);
extern void freeentry(struct entry *);
extern void moveentry(struct entry *, char *);
extern char *savename(char *);
extern void freename(char *);
extern void dumpsymtable(char *, long);
extern void initsymtable(char *);
extern void mktempname(struct entry *);
extern char *gentempname(struct entry *);
extern void newnode(struct entry *);
extern void removenode(struct entry *);
extern void removeleaf(struct entry *);
extern ino_t lowerbnd(ino_t);
extern ino_t upperbnd(ino_t);
extern void badentry(struct entry *, char *);
extern char *flagvalues(struct entry *);
extern ino_t dirlookup(char *);
#else
extern struct entry *lookupino();
extern struct entry *lookupname();
extern struct entry *addentry();
extern void deleteino();
extern char *myname();
extern void freeentry();
extern void moveentry();
extern char *savename();
extern void freename();
extern void dumpsymtable();
extern void initsymtable();
extern void mktempname();
extern char *gentempname();
extern void newnode();
extern void removenode();
extern void removeleaf();
extern ino_t lowerbnd();
extern ino_t upperbnd();
extern void badentry();
extern char *flagvalues();
extern ino_t dirlookup();
#endif
#define	NIL ((struct entry *)(0))

/*
 * Definitions for library routines operating on directories.
 * These definitions used only for reading fake directory
 * entries from restore's temporary file "restoresymtable"
 * These have little to do with real directory entries.
 */
#if !defined(DEV_BSIZE)
#define	DEV_BSIZE	512
#endif
#define	DIRBLKSIZ	DEV_BSIZE
typedef struct _rstdirdesc {
	int	dd_fd;
	long	dd_loc;
	long	dd_size;
	char	dd_buf[DIRBLKSIZ];
} RST_DIR;

/*
 * Constants associated with entry structs
 */
#define	HARDLINK	1
#define	SYMLINK		2
#define	TMPHDR		"RSTTMP"

/*
 * The entry describes the next file available on the tape
 */
struct context {
	char	*name;		/* name of file */
	ino_t	ino;		/* inumber of file */
	struct	dinode *dip;	/* pointer to inode */
	int	action;		/* action being taken on this file */
	int	ts;		/* TS_* type of tape record */
} curfile;
/* actions */
#define	USING	1	/* extracting from the tape */
#define	SKIP	2	/* skipping */
#define	UNKNOWN 3	/* disposition or starting point is unknown */

/*
 * Other exported routines
 */
#ifdef __STDC__
extern ino_t psearch(char *);
extern void metaset(char *);
extern void metaproc(char *, char *, long);
extern long listfile(char *, ino_t, int);
extern long addfile(char *, ino_t, int);
extern long deletefile(char *, ino_t, int);
extern long nodeupdates(char *, ino_t, int);
extern long verifyfile(char *, ino_t, int);
extern void extractdirs(int genmode);
extern void skipdirs(void);
extern void treescan(char *, ino_t, long (*)(char *, ino_t, int));
extern RST_DIR *rst_opendir(char *);
extern struct direct *rst_readdir(RST_DIR *);
extern void setdirmodes(void);
extern int genliteraldir(char *, ino_t);
extern int inodetype(ino_t);
extern void done(int);
extern void runcmdshell(void);
extern void canon(char *, char *);
extern void onintr(int);
extern void removeoldleaves(void);
extern void findunreflinks(void);
extern void removeoldnodes(void);
extern void createleaves(char *);
extern void createfiles(void);
extern void createlinks(void);
extern void checkrestore(void);
extern void setinput(char *, char *);
extern void newtapebuf(long);
extern void setup(void);
extern void setupR(void);
extern void getvol(long);
extern void printdumpinfo(void);
extern int extractfile(char *);
extern void skipmaps(void);
extern void skipfile(void);
extern void getfile(void (*)(char *, long), void (*)(char *, long));
extern void null(char *, long);
extern void findtapeblksize(int);
extern void flsht(void);
extern void closemt(void);
extern int readhdr(struct s_spcl *);
extern int gethead(struct s_spcl *);
extern int volnumber(ino_t);
extern void findinode(struct s_spcl *);
extern void pathcheck(char *);
extern void renameit(char *, char *);
extern int linkit(char *, char *, int);
extern int reply(char *);
/*PRINTFLIKE1*/
extern void panic(const char *, ...);
extern char *lctime(time_t *);
extern void metamucil(char *);
extern void metamucil_extract_msg(void);
extern void metamucil_getvol(struct s_spcl *);
extern void metamucil_setinput(char *, char *);
extern int metamucil_seek(daddr_t);
extern void reset_dump(void);
extern void get_next_device(void);
#else	/* !STDC */
extern ino_t psearch();
extern void metaset();
extern void metaproc();
extern long listfile();
extern long addfile();
extern long deletefile();
extern long nodeupdates();
extern long verifyfile();
extern void extractdirs();
extern void skipdirs();
extern void treescan();
extern RST_DIR *rst_opendir();
extern struct direct *rst_readdir();
extern void setdirmodes();
extern int genliteraldir();
extern int inodetype();
extern void done();
extern void runcmdshell();
extern void canon();
extern void onintr();
extern void removeoldleaves();
extern void findunreflinks();
extern void removeoldnodes();
extern void createleaves();
extern void createfiles();
extern void createlinks();
extern void checkrestore();
extern void setinput();
extern void newtapebuf();
extern void setup();
extern void setupR();
extern void getvol();
extern void printdumpinfo();
extern int extractfile();
extern void skipmaps();
extern void skipfile();
extern void getfile();
extern void null();
extern void findtapeblksize();
extern void flsht();
extern void closemt();
extern int readhdr();
extern int gethead();
extern int volnumber();
extern void findinode();
extern void pathcheck();
extern void renameit();
extern int linkit();
extern int reply();
extern void panic();
extern char *lctime();
extern void metamucil();
extern void metamucil_extract_msg();
extern void metamucil_getvol();
extern void metamucil_setinput();
extern int metamucil_seek();
extern void reset_dump();
extern void get_next_device();
#endif	/* STDC */

/*
 * Useful macros
 */
#define	MWORD(m, i)	((m)[(unsigned)((i)-1)/NBBY])
#define	MBIT(i)		(1<<((unsigned)((i)-1)%NBBY))
#define	BIS(i, w)	(MWORD((w), (i)) |=  MBIT(i))
#define	BIC(i, w)	(MWORD((w), (i)) &= ~MBIT(i))
#define	BIT(i, w)	(MWORD((w), (i)) & MBIT(i))

/*
 * Defines used by findtapeblksize()
 */
#define	TAPE_FILE	0
#define	ARCHIVE_FILE	1

#ifdef USG
#define	bcopy(s1, s2, len)	(void) memcpy((s2), (s1), (len))
#define	bzero(s, len)		memset((s), 0, (len))
#define	bcmp(s1, s2, len)	memcmp((s1), (s2), (len))
#define	setjmp(b)		sigsetjmp((b), 1)
#define	longjmp			siglongjmp
#define	jmp_buf			sigjmp_buf
#define	chown			lchown
#endif

/*
 * Defaults
 */
#define	TAPE	"/dev/rmt/0b"		/* default tape device */

#define	dprintf		if (dflag) (void) fprintf
#define	vprintf		if (vflag) (void) fprintf

#define	GOOD 1
#define	FAIL 0
