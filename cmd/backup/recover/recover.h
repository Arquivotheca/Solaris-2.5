/*	@(#)recover.h 1.4 92/04/20	*/

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include <stdio.h>
#include <locale.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <setjmp.h>
#include <sys/timeb.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/vnode.h>

#ifdef USG
/*
 * System-V specific header files
 */
#include <netdb.h>
#include <stdlib.h>
#include <ulimit.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/statvfs.h>
#include <sys/systeminfo.h>
#include <sys/fs/ufs_inode.h>
#include <sys/fs/ufs_fs.h>
#include <sys/fs/ufs_fsdir.h>
#else
#include <ufs/fsdir.h>
#endif

/*
 * database header files
 */
#include <database/dbserv.h>
#include <database/backupdb.h>
#include <database/dir.h>
#include <database/dnode.h>
#include <database/instance.h>
#include <database/header.h>

#ifdef USG
/*
 * BSD to USG translations needed to build rpc.dbserv
 */

/*
 * Function translations
 */
#define	bcmp(s1, s2, len)	memcmp((s1), (s2), (len))
#define	bcopy(s1, s2, len)	(void) memcpy((s2), (s1), (len))
#define	bzero(s, len)		memset((s), 0, (len))
#define	gethostname(name, len)	\
	    ((sysinfo(SI_HOSTNAME, (name), (len)) < 0) ? -1 : 0)
#define	getpagesize()		sysconf(_SC_PAGESIZE)
#define	getwd(d)		getcwd((d), MAXPATHLEN)
#define	sigvec			sigaction		/* struct and func */
#define	sv_handler		sa_handler
#define	sv_mask			sa_mask
#define	sv_flags		sa_flags
#define	setreuid(r, e)		seteuid(e)
#define	statfs			statvfs			/* struct and func */
#define	setjmp(b)		sigsetjmp((b), 1)
#define	longjmp			siglongjmp
#define	jmp_buf			sigjmp_buf

/*
 * XXX seteuid is apparently not a part of the
 * SVID.  If your implementation does not have
 * this function, you're screwed.
 */
#if !__STDC__
extern int seteuid();
#endif

/*
 * Inode related translations
 */
#define	ROOTINO		UFSROOTINO
#define	di_rdev		di_ordev

/*
 * For stat-inode translation.
 * XXX don't forget the translation from
 * nanosecs to usecs (or vica versa)
 */
#define	st_spare1	st_atim.tv_nsec
#define	st_spare2	st_mtim.tv_nsec
#define	st_spare3	st_ctim.tv_nsec

#define	TMCONV	1000
#endif

extern FILE	*outfp;
extern char	*dbserv;
extern int	attempt_seek;

/*
 * Function declarations
 */
#ifdef __STDC__
extern int mkuserdir(char *);
extern struct dir_entry *pathcheck(char *, char *, char *, time_t,
	int, int, struct dir_block **, u_long *);
extern int setdate(char *);
extern void db_homedir(char *, char *, char *, time_t);
extern void smashpath(char *);
extern struct cmdinfo *parsecmd(char *);
extern void usage(void);
extern void printhelp(char *);
extern int dir_read(char *, char *, u_long, struct dir_block *);
extern int instance_read(char *, char *, u_long, int, struct instance_record *);
extern int dnode_blockread(char *, char *, u_long, u_long, int, struct dnode *);
extern char *db_readlink(char *, char *, u_long, long);
extern int header_read(char *, char *, u_long, struct dheader **);
extern void free_dumplist(struct dumplist *);
extern int read_dumps(char *, char *, char *, time_t, struct dumplist **);
extern void free_mntpts(struct mntpts *);
extern int db_getmntpts(char *, char *, char *, time_t, struct mntpts **);
extern int db_find(char *, char *, char *, char *, time_t);
extern void invalidate_handle(void);
extern int dir_ropen(char *, char *);
extern void dir_rclose(void);
extern struct dir_entry *dir_path_lookup(u_long *, struct dir_block **, char *);
extern int getpathcomponent(char **, char *);
extern struct dir_entry *dir_name_lookup(u_long *, struct dir_block **, char *);
extern void dir_initcache(void);
extern struct dir_block *dir_getblock(u_long);
extern struct dnode *dnode_get(char *, char *, u_long, u_long);
extern void dnode_initcache(void);
extern int dnode_flushcache(char *, u_long);
extern int unknown_dump(u_long);
extern int onextractlist(u_long, u_long);
extern int check_extractlist(void);
extern void print_extractlist(void);
extern void remove_extractlist(u_long);
extern void extract(int);
extern int yesorno(char *);
extern void find(char *, char *, char *, time_t);
extern void descend(char *, char *, u_long, time_t, char *);
extern void delay_io(char *);
extern void setdelays(int);
extern int getopaquemode(void);
extern void set_lookupmode(char *);
extern int getdnode(char *, struct dnode *, struct dir_entry *,
	int, time_t, int, char *);
extern void flush_mntpts(void);
extern int header_get(char *, char *, u_long, struct dheader **);
extern int listget(char *, char *, char *, time_t, struct dumplist **);
extern void uncache_header(u_long);
extern void purge_dumplists(void);
extern int instance_ropen(char *, char *);
extern void instance_rclose(void);
extern struct instance_record *instance_getrec(u_long);
extern int instance_initcache(void);
extern void panic(char *);
extern char *lctime(time_t *);
extern int permchk(struct dnode *, int, char *);
extern int getperminfo(struct db_findargs *);
extern int get_termwidth(void);
extern void term_init(void);
extern void term_start_output(void);
extern void term_finish_output(void);
extern void term_putc(int);
extern void term_putline(char *);
extern void close_output(void);
#else	/* !STDC */
extern int mkuserdir();
extern struct dir_entry *pathcheck();
extern int setdate();
extern void db_homedir();
extern void smashpath();
extern struct cmdinfo *parsecmd();
extern void usage();
extern void printhelp();
extern int dir_read();
extern int instance_read();
extern int dnode_blockread();
extern char *db_readlink();
extern int header_read();
extern void free_dumplist();
extern int read_dumps();
extern void free_mntpts();
extern int db_getmntpts();
extern int db_find();
extern void invalidate_handle();
extern int dir_ropen();
extern void dir_rclose();
extern struct dir_entry *dir_path_lookup();
extern int getpathcomponent();
extern struct dir_entry *dir_name_lookup();
extern void dir_initcache();
extern struct dir_block *dir_getblock();
extern struct dnode *dnode_get();
extern void dnode_initcache();
extern int dnode_flushcache();
extern int unknown_dump();
extern int onextractlist();
extern int check_extractlist();
extern void print_extractlist();
extern void remove_extractlist();
extern void extract();
extern int yesorno();
extern void find();
extern void descend();
extern void delay_io();
extern void setdelays();
extern int getopaquemode();
extern void set_lookupmode();
extern int getdnode();
extern void flush_mntpts();
extern int header_get();
extern int listget();
extern void uncache_header();
extern void purge_dumplists();
extern int instance_ropen();
extern void instance_rclose();
extern struct instance_record *instance_getrec();
extern int instance_initcache();
extern void panic();
extern char *lctime();
extern int permchk();
extern int getperminfo();
extern int get_termwidth();
extern void term_init();
extern void term_start_output();
extern void term_finish_output();
extern void term_putc();
extern void term_putline();
extern void close_output();
#endif
