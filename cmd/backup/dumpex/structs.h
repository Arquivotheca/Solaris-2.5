/*	@(#)structs.h 1.37 94/08/10	*/

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include <stdio.h>
#include <locale.h>
#include <sys/time.h>
#include <sys/types.h>

/*
 * Take out the System wrapper function for untrusted remote commands.
 * Remove this define to get a 4 minute timeout termination boilerplate.
 */
#define	System system

#ifdef USG
/*
 * Make BSD look like System V
 */
#define	index		strchr
#define	rindex		strrchr
#define	bzero(s, n)	memset((void *)s, 0, (size_t)n);
#define	setreuid(r, e)	seteuid(e)

#if !__STDC__
extern int seteuid();
#endif

#if !defined(FLOCK) && !defined(USELOCKF)
#define	LOCK_SH		1	/* shared lock */
#define	LOCK_EX		2	/* exclusive lock */
#define	LOCK_NB		4	/* don't block when locking */
#define	LOCK_UN		8	/* unlock */
#endif
#endif	/* USG */

#define	STATCOLUMN 0		/* P-00000 99 */
#define	USEDCOLUMN 1
#define	IDCOLUMN 2
#define	POSCOLUMN 8

/*
 * group allowed to do backups
 */
#ifdef USG
#define	OPERATOR	"sys"
#else
#define	OPERATOR	"operator"
#endif

/*
 * Path data
 */
#define	LOGFILEDIR	"dumplog"	/* in admdir */
#define	CONFDIR		"/etc/dumps"
#define	BINDIR		"/sbin"
#define	LIBDIR		"/lib"
#define	TMPDIR		"/var/tmp"	/* default temp directory */

#define	INBASE 	20		/* screen input line for editor */
#define	LINEWID 	(SCREENWID-5)
#define	MAXCOMMANDLEN 	1024
#define	MAXCURSESLEN 	1024
#define	MAXDEVICEDISPLAY 15
#define	MAXDUMPSETS	100	/* usually expect 1, 2, or 3 dumpsets */
#define	MAXLIBLEN 	10	/* 16 - strlen(":00000") */
#define	MAXLINELEN 	1025
#define	MAXPATHLEN 	1024
#define	SCREENWID 	70
#define	EXTLEN		5
#define	INPUTCOL	5
#define	MAXTAPENUM	99999
#define	TAPENUMLEN	5

#define	RESERVETIME	23*60*60	/* reserve tapes for 23 hours */
#define	TBUFSZ		(sizeof (struct tapedesc_f))
#define	NCACHEDTAPES	1000		/* less than 20KBytes */

/* Tape library statuses: */
#define	TL_NOSTATUS	0	/* must be zero */
#define	TL_SCRATCH	1	/* scratch tape */
#define	TL_USED		2	/* some data present */
#define	TL_TMP_RESERVED	3	/* reserved until t_expdate */
#define	TL_STATMASK	0xF	/* gets these statuses */
#define	TL_ERRORED	0x20	/* OR'd with above, relatively sticky */
#define	TL_LABELED	0x40	/* OR'd with above, relatively sticky */
#define	TL_OFFSITE	0x80	/* OR'd with above */


#define	NORETURNCODE	-2	/* must not be able to be impersonated */
#define	NODUMPDONE	-3	/* no dump was actually done */

#define	HOURSPERDAY	24
#define	MINUTESPERHOUR	60
#define	SECONDSPERMINUTE	60
#define	DAYTOSEC(d)   ((d)*HOURSPERDAY*MINUTESPERHOUR*SECONDSPERMINUTE)

#define	GROW	3		/* increment to grow structures */

#define	LEV_LIKE_SLOT1		0
#define	LEV_FULL_INCR		1
#define	LEV_FULL_INCRx2		2
#define	LEV_FULL_TRUEINCR	3
#define	LEV_ANY			4
#define	LEV_FULL_TRUEINCRx2	5

#define	HELP_MAIN	0
#define	HELP_FS		1
#define	HELP_FS_EDIT	2
#define	HELP_DEVS	3
#define	HELP_MAIL	4
#define	HELP_SCHED	5
#define	HELP_KEEP	6
#define	HELP_MAX	7

struct keep_f {
	char	k_level;
	int	k_multiple;
	int	k_days;
	int	k_minavail;
};

enum day_name {
	Mon = 0,
	Tue = 1,
	Wed = 2,
	Thu = 3,
	Fri = 4,
	Sat = 5,
	Sun = 6
};
typedef enum day_name day_t;

struct cron_f {
	int	c_enable;		/* global enable/disable */
	int	c_dtime;		/* dumpex time */
	int	c_ttime;		/* tape reminder time */
	int	c_ena[7];		/* per-day enable/disable */
	int	c_new[7];		/* per-day new tape? */
};

struct tapeset_f {
	struct devcycle_f *ts_devlist;
};

struct devcycle_f {
	int	dc_fullcycle;
	char	*dc_filesys;
	char	*dc_dumplevels;
	int	dc_linenumber;
	struct string_f *dc_log;
	struct devcycle_f *dc_next;
};

struct inline_f {
	int	if_fileoffset;
	char	*if_text;
};

struct tapedesc_f {		/* array of these on disk */
	long	t_status;	/* see TL_* defines; one byte is sufficient */
	long	t_expdate;	/* seconds since 1970; -1 == infinity */
	long	t_expcycle;	/* fulldump cycle number */
};

struct tapes_f {			/* tapes we are using/going to use */
	int	ta_number;		/* tape id number */
	char	*ta_mount;		/* tape mount location */
	char	ta_status;		/* N for new, etc. */
	struct tapes_f *ta_next;	/* double linked list */
	struct tapes_f *ta_prev;	/* double linked list */
};

struct range_f {
	int	r_val;
	struct range_f *r_next;
};

struct string_f {
	char	*s_string;	/* allocated */
	char	*s_max;		/* one past end -- when grow is necessary */
	char	*s_last;	/* for fast appending */
};

struct remote_handle {
	int	rh_fd;
	int	rh_err;
};
typedef struct remote_handle *rhp_t;

struct oob_mail {
	struct string_f		*om_fs;
	int			om_tapeid;
	int			om_file;
	struct oob_mail		*om_continue;
	struct oob_mail		*om_next;
};

extern struct inline_f *inlines;
extern int ninlines;		/* how many lines total */
extern int maxinlines;		/* how many lines max */
extern FILE *infid;		/* config file */
extern char *cf_filename;

extern char *cf_dumplib;
extern char *cf_tapelib;
extern char **cf_dumpdevs;
extern char *tmpdir;
extern int ncf_dumpdevs;
extern int maxcf_dumpdevs;
extern int cf_blockfac;
extern int cf_tapesup;
extern struct keep_f *cf_keep;
extern int ncf_keep;
extern int maxcf_keep;
extern struct cron_f cf_cron;
extern struct tapeset_f *cf_tapeset[];	/* xxx Bad form */
extern char **cf_notifypeople;
extern int maxcf_notifypeople;
extern int ncf_notifypeople;
extern char *cf_rdevuser;	/* for rmt protocol */
extern int cf_mastercycle;	/* how many cycles through all dumpsets */
extern int cf_mastercycleline;
extern int cf_maxset;		/* maximum dump set number */
extern int cf_longplay;		/* leave tape in drive overnight */
				/* DOES NOT WORK ON ALL DEVICES */

extern int tapelibfid;		/* for reading/writing library db */
extern int tapeliblen;		/* # bytes currently in tapelib */
extern int debug;		/* debug level */

extern char filename[];
extern char newfilename[];

extern struct tapes_f tapes_head;	/* two headed for easy insertion */
extern char hostname[];
extern char opserver[];
extern char lfilename[];	/* name of the temporary lfile */
extern char rlfilename[];	/* name of the (remote) temporary lfile */
extern FILE *lfilefid;		/* I/O on lfile */

extern char *securitystring;	/* header string to avoid /etc/passwd mods */

extern struct string_f *dumpcommand;	/* dump command text */
extern char remote[];		/* remote machine name for this dump */
extern int usehsmroot;		/* uses /opt/hsm rather than /usr/etc */
extern int keepdays;		/* how long to keep this tape */
extern int keepminavail;	/* how many to keep of this tape type */
extern int keeptil;		/* keep this tape through this cycle */
extern int nswitch;		/* 1 -> -n -> report but do not execute */
extern int Nswitch;		/* 1 -> -N -> report briefly do not execute */
extern FILE *logfile;		/* logfile fid */
				/* dumplog/91.03.23 is typical dumpfilename */

extern int fswitch;		/* force: retry bad dumps */
extern int thisdumpset;		/* the parallel set  we're doing */
extern char **splitfields;
extern int nsplitfields;
extern int maxsplitfields;
extern int reservetime;		/* how long to hold a tape */
extern int reposition;
extern int tapeposofnextfile;	/* tape position of this file */
extern char auxstatusfile[];	/* xxx Bad form */
extern int ndumpsets;		/* editor */
extern int nfilesystems;
extern struct devcycle_f *fs[];	/* xxx Bad form */
extern int changedbottom;	/* modified dump info in dumped */
extern int changedtop;		/* modified dump info in dumped */
extern int changedcron;		/* modified dump info in dumped */
extern int addedfs;		/* a filesystem was added in dumped */
extern char *progname;		/* die() prints this upon termination */
extern int dontoffline;		/* flag for easier debugging */
extern int cachestart;
extern struct tapedesc_f tapecache[];
extern int thisisedit;		/* flag says dumped is running */
extern int curseson;		/* curses is in use */
extern struct string_f *sectapes;  /* which tapes have security info */
extern int lockfid;		/* fd of current lock on config file */

extern char confdir[];

extern char *configfilesecurity;
extern char *tapelibfilesecurity;
extern int lentapelibfilesecurity;

#ifdef __STDC__
extern void split(char *, char *);
/*PRINTFLIKE1*/
extern void die(const char *, ...);
/*PRINTFLIKE1*/
extern void warn(const char *, ...);
extern char *checkalloc(unsigned);
extern char *checkrealloc(char *, unsigned);
extern char *checkcalloc(unsigned);
extern char *strappend(char *, char *);
extern void initdumpargs(void);
extern void adddumpflag(char *);
extern void adddumpflagc(int);
extern void adddumparg(char *);
extern void printlfile(char *);
extern void makedumpcommand(void);
extern void figurekeep(int, int, char *);
/*PRINTFLIKE1*/
extern void log(const char *, ...);
extern void logmail(struct string_f *, char *);
extern int writelfile(void);
extern int filenamecheck(char *, int);
extern char *genlevelstring(int, int, int, int);
extern void exunlock(void);
extern int exlock(char *, char *);
#if defined(USG) && !defined(FLOCK)
extern int flock(int, int);
#endif
extern struct string_f *newstring(void);
extern void freestring(struct string_f *s);
extern void stringapp(struct string_f *, char *);
extern void chop(char *);
extern void checkroot(int);
extern char *gatherline(int, int, int *);
extern void nocurses(void);
extern void openconfig(char *);
extern void readit(void);
extern void checkget(int, int, int, char *);
extern void newtapelibraryname(void);
extern void newrdevuser(void);
extern void newdumplibraryname(void);
extern void newtapesup(void);
extern void newblockfac(void);
extern void newdevices(void);
extern void newkeeps(void);
extern void newcrons(void);
extern void newmailees(void);
extern void dumped_quit(void);
extern void cleanup(void);
extern int zgetch(void);
extern void setup(void);
extern void newdumpsets(void);
extern void newtmpdir(void);
extern void newfilesys(void);
extern void inputborder(void);
/*PRINTFLIKE2*/
extern void inputshow(int, char *, ...);
extern void inputclear(int);
extern void replotit(void);
extern void addfs(char *);
extern void addfilefs(char *);
extern void probefs(char *, char *, void (*)());
extern char *getfs(int);
extern struct remote_handle *remote_setup(char *, char *, char *, int);
extern void remote_shutdown(rhp_t);
#else	/* !STDC */
extern void split();
extern void die();
extern void warn();
extern char *checkalloc();
extern char *checkrealloc();
extern char *checkcalloc();
extern char *strappend();
extern void initdumpargs();
extern void adddumpflag();
extern void adddumpflagc();
extern void adddumparg();
extern void printlfile();
extern void makedumpcommand();
extern void figurekeep();
extern void log();
extern void logmail();
extern int writelfile();
extern int filenamecheck();
extern char *genlevelstring();
extern void exunlock();
extern int exlock();
extern int flock();
extern struct string_f *newstring();
extern void freestring();
extern void stringapp();
extern void chop();
extern void checkroot();
extern char *gatherline();
extern void nocurses();
extern void openconfig();
extern void readit();
extern void checkget();
extern void newtapelibraryname();
extern void newrdevuser();
extern void newdumplibraryname();
extern void newtapesup();
extern void newblockfac();
extern void newdevices();
extern void newkeeps();
extern void newcrons();
extern void newmailees();
extern void dumped_quit();
extern void cleanup();
extern int zgetch();
extern void setup();
extern void newdumpsets();
extern void newtmpdir();
extern void newfilesys();
extern void inputborder();
extern void inputshow();
extern void inputclear();
extern void replotit();
extern void addfs();
extern void addfilefs();
extern void probefs();
extern char *getfs();
extern struct remote_handle *remote_setup();
extern void remote_shutdown();
/*
 * XXX broken header files
 */
extern int select();
extern char *strdup();
extern int strcasecmp();
extern int strncasecmp();
#endif
