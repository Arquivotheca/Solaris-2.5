/*	@(#)defs.h 1.8 93/07/07	*/

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include <stdio.h>
#include <locale.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
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
#include <database/batchfile.h>
#include <database/activetape.h>
#include <database/cache.h>

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
#define	getdomainname(name, len)	\
	    ((sysinfo(SI_SRPC_DOMAIN, (name), (len)) < 0) ? -1 : 0)
#define	getpagesize()		sysconf(_SC_PAGESIZE)
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

/*
 * Function declarations
 */
#ifdef __STDC__
extern int batch_update(const char *, const char *);
extern int delete_tape(const char *);
extern int scratch_tape(const char *, time_t);
extern int tape_sitechange(const char *, int);
extern int host_namechange(const char *, u_long, const char *, u_long);
extern int duplicate_dump(const char *, const char *);
extern char *lctime(time_t *);
extern int getreadlock(void);
extern void releasereadlock(void);
extern int isupdatepid(int);
extern void closefiles(void);
extern struct dumplist *fheader_search(const struct fsheader_readargs *);
extern int get_mntpts(const char *, time_t, struct mntpts **);
extern void startup(void);
extern void startupreg(const char *);
extern int startupunlink(void);
extern void tape_rechain(const char *, const u_long *, int);
extern void cleanup(void);
#else
extern void releasereadlock();
extern void closefiles();
extern void startup();
extern void startupreg();
extern int startupunlink();
extern struct dumplist *fheader_search();
extern int batch_update();
extern int delete_tape();
extern int scratch_tape();
extern int tape_sitechange();
extern int host_namechange();
extern int duplicate_dump();
extern char *lctime();
extern int getreadlock();
extern int isupdatepid();
extern int get_mntpts();
extern void tape_rechain();
extern void cleanup();
#endif
