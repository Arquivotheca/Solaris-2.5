/*	@(#)defs.h 1.7 93/05/12	*/

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include <stdio.h>
#include <locale.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/vnode.h>
#define	_POSIX_SOURCE	/* hack to avoid redef of MAXNAMLEN */
#define	_POSIX_C_SOURCE
#include <dirent.h>
#undef	_POSIX_C_SOURCE
#undef	_POSIX_SOURCE

#ifdef USG
/*
 * System-V specific header files
 */
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

#include <protocols/dumprestore.h>

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
#define	getpagesize()		sysconf(_SC_PAGESIZE)
#define	sigvec			sigaction		/* struct and func */
#define	sv_handler		sa_handler
#define	sv_mask			sa_mask
#define	sv_flags		sa_flags
#define	setreuid(r, e)		seteuid(e)
#define	statfs			statvfs			/* struct and func */

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
extern void tapeadd(char *, char *, const char *, int);
extern void dumpadd(char *, char *, const char *, int);
extern void pokeserver(u_long, char *);
extern char *getdbhost(char *);
extern void maint_lock(void);
extern void maint_unlock(void);
extern char *lctime(time_t *);
extern void reclaim(char *, char *);
extern void rebuilddir(char *, char *, int);
extern void rebuildone(char *, DIR *);
extern void rebuildtape(char *);
#else	/* !__STDC__ */
extern void tapeadd();
extern void dumpadd();
extern void pokeserver();
extern char *getdbhost();
extern void maint_lock();
extern void maint_unlock();
extern char *lctime();
extern void reclaim();
extern void rebuilddir();
extern void rebuildone();
extern void rebuildtape();
#endif	/* __STDC__ */

/*
 * Structures
 */

struct labelstruct {
    char *name;
    int width, height, spine;
};
