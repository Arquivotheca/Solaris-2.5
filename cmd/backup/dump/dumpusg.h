#pragma ident	"@(#)dumpusg.h	1.8	94/07/18 SMI"

#if !defined(DUMPUSG) && defined(USG)
#define	DUMPUSG
/*
 * Translate from BSD to System V, where possible.
 */
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
#include <sys/vfstab.h>
#include <sys/fs/ufs_inode.h>
#include <sys/fs/ufs_fs.h>
#include <sys/fs/ufs_fsdir.h>
#include <sys/fs/ufs_acl.h>

#include <sys/mnttab.h>
#include <sys/vfstab.h>

/*
 * make mnttab look like mtab
 */
#define	MOUNTED		MNTTAB
#define	mntent		mnttab
#define	mnt_fsname	mnt_special
#define	mnt_dir		mnt_mountp
#define	mnt_type	mnt_fstype
#define	mnt_opts	mnt_mntopts
#define	MNTTYPE_42	"ufs"
#define	MNTINFO_DEV	"dev"

#define	setmntent	fopen
#define	endmntent	fclose

/*
 * Function translations
 */
#define	bcmp(s1, s2, len)	memcmp((s1), (s2), (len))
#define	bcopy(s1, s2, len)	(void) memcpy((s2), (s1), (len))
#define	bzero(s, len)		memset((s), 0, (len))
#define	gethostname(name, len)	\
	    ((sysinfo(SI_HOSTNAME, (name), (len)) < 0) ? -1 : 0)
#define	getpagesize()		sysconf(_SC_PAGESIZE)
#define	getdtablesize()		ulimit(UL_GDESLIM, 0)
#define	signal			nsignal		/* defined in dumpmain.c */
#define	sigvec			sigaction	/* both struct and func */
#define	sv_flags		sa_flags
#define	sv_handler		sa_handler
#define	sv_mask			sa_mask
#define	sigmask(x)		x
#define	setreuid(r, e)		seteuid(e)
#define	statfs			statvfs		/* both struct and func */
#define	setjmp(b)		sigsetjmp((b), 1)
#define	longjmp			siglongjmp
#define	jmp_buf			sigjmp_buf

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
