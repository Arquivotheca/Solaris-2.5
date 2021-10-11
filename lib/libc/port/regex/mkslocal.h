/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 *
 * This code is MKS code ported to Solaris with minimum modifications so that
 * upgrades from MKS will readily integrate. Avoid modification if possible!
 */
#ident	"@(#)mkslocal.h 1.1	94/10/12 SMI"

/*
 * <mkslocal.h>, Solaris 2.x Version - system configuration file
 *
 * Copyright 1985, 1993 by Mortice Kern Systems Inc.  All rights reserved.
 *
 * This Software is unpublished, valuable, confidential property of
 * Mortice Kern Systems Inc.  Use is authorized only in accordance
 * with the terms and conditions of the source licence agreement
 * protecting this Software.  Any unauthorized use or disclosure of
 * this Software is strictly prohibited and will result in the
 * termination of the licence agreement.
 *
 * If you have any questions, please consult your supervisor.
 * 
 * $Header: /u/rd/h/sol2/RCS/mkslocal.h,v 1.26 1994/02/25 09:15:37 mark Exp $
 */


/*
 * DO NOT define _POSIX_SOURCE on Solaris 2.1 or 2.3
 * We want to be able to access some of Sun's system extensions
 * (e.g some non-POSIX stuff) and there are no feature test macros to enable
 * it individually
 */
#undef	_POSIX_SOURCE
#define _XOPEN_SOURCE	1		/* want XPG definitions */

#define M_RCSID		1		/* enable rcsid's in MKS code */

/* Define _ALL_SOURCE to ensure we get all POSIX.2 prototypes in MKS
 * dot2 header files
 */
#define _ALL_SOURCE 1

#include <sys/types.h>

#ifndef VERSION
/* Used for in sh, vi ... */
#define VERSION "MKS InterOpen Posix Shell and Utilities v4.0(ra1.0)"
#endif

/*
 * MKS makes use of types that may or may not already be defined in the
 * system <sys/types.h> file.  If not defined, then they must be defined
 * here.  (The problem is a lack of #if directive to determine an existing
 * typedef.
 */
typedef unsigned char   uchar;

#undef	BSD			/* Mostly BSD compatible */
#define	SYSV	1		/* Somewhat System V compatible */

/*
 * M_DEVELOPMENT - indicates if in a development environment 
 *		   e.g) not a production environment
 *		   Used only here and by the function rootname()
 */
#if M_DEVELOPMENT==0
#ifndef M_PRODUCT_DIR
#  define M_PRODUCT_DIR	"/usr/posix.2"	/* default installation directory */
#endif /*M_PRODUCT_DIR*/
#else
#ifndef M_PRODUCT_DIR
#  define M_PRODUCT_DIR	"/usr/rd/project/ra"
#endif /*M_PRODUCT_DIR*/
#endif /*M_DEVELOPMENT*/

LEXTERN char*	rootname(const char *filename);

/* don't use M_PRODUCT_DIR in the following macros
 * Assume that rootname() is called to prepend the proper path prefix
 */
#define	M_CS_PATH	"/bin"
#define	M_CS_SHELL	"/bin/sh"

#define M_CS_BINDIR     "/bin"
#define M_CS_ETCDIR     "/etc"
#define M_CS_LIBDIR     "/lib"
#define M_CS_NLSDIR     "/lib/locale"
#define M_CS_SPOOLDIR   "/spool"
#define M_CS_TMPDIR     "/tmp"
#define M_CS_MANPATH    "/man"

#define	M_BINDIR(path)		M_CS_BINDIR"/" #path
#define	M_ETCDIR(path)		M_CS_ETCDIR"/" #path
#define	M_LIBDIR(path)		M_CS_LIBDIR"/" #path
#define	M_SPOOLDIR(path)	M_CS_SPOOLDIR"/" #path
#define	M_NLSDIR(path)		M_CS_NLSDIR"/" #path

/*
 * M_MANPATH - list of pathnames to be used by man utility
 * M_TMPDIR - pathname of temporary
 */
#define	M_MANPATH	M_CS_MANPATH"/"
#define M_TMPDIR	M_CS_TMPDIR"/"


#define M_MALLOC        1
#define M_REALLOC	1		/* want to use m_realloc() */
#define M_WANT_ANSI_REALLOC	1	/* m_realloc() will be ANSI compliant */

/*
 * M_NL_DOM - domain name used by m_text*() when calling catopen()
 */
#define	M_NL_DOM	"mks"

/*
 * Varying limits for scheduling queues - cannot hardcode constants.
 */
#define MIN_NICE (m_minnice())
#define MAX_NICE (m_maxnice())

/* On POSIX and UNIX there is no special file copying routine */
#define m_cp(src, dest, ssb, flags)     (M_CP_NOOP)

#define	M_DEVIO	1	/* Use devio routines */

/*
 * M_LKSUFFIX is used by the mailx locking code which attempts to
 * lock mailboxes with lockfiles.  This setting is correct for
 * SunOS/sendmail
 */
#define M_LKSUFFIX	".lock"

/*
 * group owner of ALL tty's - used by m_wallow() (by write, talk, mesg)
 */
#define M_TTYGROUP	"tty"

#define	M_LDATA		1

#define	halloc(n,s)	malloc((size_t)((n)*(s)))
#define	hfree(ptr)	free(ptr)

#define	M_FSDELIM(c)	((c) == '/')

/* M_FSMOUNT: pathname to the list of mounted filesystems 
 * Used by df.c and m_fs.c
 */
#define M_FSMOUNT 	"/etc/mnttab"


#define	__CLK_TCK	60		/* times() units */

#define         M_MAKEOS        	"OS:=unix"
#define         M_MAKEFILES     	".MAKEFILES:makefile Makefile"
#define         M_MAKEDIRSEPSTR 	"/"
#define         M_MAKE_BUFFER_SIZE      32768
#define         M_MAKE_STRING_SIZE      32768
#define         M_MAKE_PATSUB_SIZE      8096
#define         M_GETSWITCHAR   '-'


#define M_ENDPWENT	1	/* set to 1 if system provides a getpwent()
				   routine */

/*
 * These macros are hardwired to avoid having to include sysmacros.h
 */
#define M_DEVMAJOR(x)	((uint)((int)((unsigned)(((x)->st_rdev)>>8)&0x7F)))
#define M_DEVMINOR(x)	((uint)((int)(((x)->st_rdev)&0xFF)))
#define	M_DEVMAKE(mjr,mnr)	((dev_t)(((mjr)<<8)|(mnr)))

#define M_INODIRENT(name, x)	((ino_t)((x)->d_ino))

#define	M_ST_BLOCKS(sbp)	((sbp)->st_blocks)
#define	M_ST_BLKSIZE(sbp)	512	/* M_ST_BLOCKS is in units of 512 */
#define M_ST_RDEV(sb)		((sb).st_rdev)

#define M_SVFS_INO		/* statvfs provides relevant info for df(1) */

#ifndef PATH_MAX
#define M_PATH_MAX      2047    /* For systems which can return -1 and errno
                                 * NOT set from pathconf(file,_PC_PATH_MAX)
                                 * (which means that PATH_MAX for 'file'
                                 *  is unlimited),
                                 * we provide a suitable LARGE value
                                 * that can be returned by m_pathmax().
                                 * This number should be sufficiently large
                                 * to handle most (if not all) reasonable
                                 * pathnames for a particular system.
                                 * m_pathmax() is usually used to determine
                                 * how large a buffer must be allocated to store
                                 * pathnames.
                                 */
#endif /* PATH_MAX */

#define M_MATHERR	1	/* math library supports matherr() */

/*
 * In order to get full Posix.2 i18n, then you must either:
 *
 * i) Use the full mks ansi-c library; mks localedef, mks locale.h file...
 * ii) Extend your own ansi-c library to contain the mks specified functions
 * as described in the mks Porting and Tuning Guide.
 *
 * Otherwise, it is not possible to conform to posix .2.
 *
 * You may still turn on I18N, and get as much internationalization as is
 * possible using a standard ANSI-C compiler.
 *
 * Your options are:
 * i)   Full posix conformance. You must have i or ii above, and must define
 *      I18N and M_I18N_MKS_{FULL,XPG}.
 * ii)  I18N at ANSI-C level.  You must define I18N, do not
 *      define M_I18N_MKS_{FULL,XPG}.
 * iii) No I18N.  Do not define I18N, do not define M_I18N_MKS_{FULL,XPG}.
 */
#define I18N	1	/* OBSOLESCENT version of M_I18N */
#define M_I18N    1             /* Do we want internationalized code?  To build
				 * a system where everything gets deleted at
                                 * compile time via #define's when possible,
                                 * this flag should be set.  <mks.h> does not
                                 * define I18N, but it is normal to set it.
                                 */

#define  M_I18N_MKS_FULL   1     /* Define this to be 1 - This means that we
				    want MKS's i18n extensions
				    but NOT the MKS implementation of locale
				    data files - we are still using Sun's
				  */

#define  M_I18N_MKS_XPG  1       /* Define this if you system has nl_langinfo().
				    Some routines can call this routine to 
				    get appropriate locale information.
				  */

#define M_I18N_M_       1       /* Define if your mks extentions start with m_
                                 * It is the mks intention that if these
                                 * extentions get approved all code will have
                                 * m_ removed. Since it is not yet approved,
                                 * we are maintaining the mks conventions of
                                 * prefixing our private libraries with m_.
                                 * If you have chosen to implement these
                                 * routines without the m_ do not define
                                 * M_I18N_M_
                                 */


#define	M_SH_ULIMIT	1	/* SVR4 ulimit built-in */



#undef	M_FCLOSE_NOT_POSIX_1		/* Not POSIX.1 section 8.2 */
#undef	M_FFLUSH_NOT_POSIX_1		/* Not POSIX.1 section 8.2 */
#undef	M_BSD_SPRINTF		/* sprintf on this system has BSD
					semantics - does not return length */

#undef M_I18N_LOCKING_SHIFT     /* No locking-shift character sets. */


/* 
 * Using MKS logger - with Solaris 2.x specific portions.
 * Default options to use username, as Solaris's syslog facility already
 * prepends a timestamp and hostname.
 */
#undef	M_LOGGER_CONSOLE	/* "/dev/console" */
#define M_LOGGER_OPTIONS	log_user
#define M_LOGGER_SYSLOGD	1


/* Cron configuration options:
 * M_CRON_USESFIFO      define this (to 1) if your cron is implemented
 *                      using a FIFO (normally found in /usr/lib/cron/FIFO)
 *                      to accept communication from the at/batch/crontab
 *                      utilities when notifying cron of changes to the
 *                      at/batch queues or the user crontabs.
 *                      If this is not defined, then cron will expect
 *                      a signal (SIGUSR) from at/batch/crontab to indicate
 *                      a change in the at/batch queues or the crontabs
 *
 * M_CRONVARS_DEFINED   define this if you define the pathnames below.
 *                      If you don't define this, then the pathnames that cron
 *                      uses is defined in src/cron/cronvars.c.
 *                      (e.g it uses the rootname() and the M_SPOOLDIR,
 *                           M_LIBDIR macros )
 *
 *                      This can be used to override cronvars.c definitions
 *                      This is useful on systems that you don't want to
 *                      use MKS's cron daemon and thus, you have to define
 *                      the directories/files where the system cron expects
 *                      things.
 */

#define M_CRON_USESFIFO 1	/* we are using SUN native cron */
 
#define M_CRONVARS_DEFINED 1
 
/* the following M_CRON_* macros necessary only
 * if M_CRONVARS_DEFINED is defined above
 */

/* the following are used by cron, crontabs, at and batch */
#define M_CRON_SPOOLDIR          "/usr/spool/cron"
#define M_CRON_CRONTABSDIR       "/usr/spool/cron/crontabs"
#define M_CRON_ATJOBSDIR         "/usr/spool/cron/atjobs"
#define M_CRON_LIBDIR            "/usr/lib/cron"
#define M_CRON_QUEUEDEFSFILE     "/usr/lib/cron/queuedefs"
#define M_CRON_FIFOFILE          "/usr/lib/cron/FIFO"
/* the following are used by cron only 
 * since we are NOT using MKS's cron, these don't matter (for now)
 */
#define M_CRON_LOGFILE           "/var/cron/log"
#define M_CRON_PIDFILE           "/usr/lib/cron/pid"

/* ps utility configuration */
#define	M_PS_TTY_WIDTH	8		/* e.g pts/24, term/a, console */
/* 
 * M_PS_TTY_PREFIX_TOSTRIP is a prefix string  that must be removed from
 * the name that ttyname() returns in order to match the name returned
 * by m_psread().
 */
#define M_PS_TTY_PREFIX_TOSTRIP	"/dev/"	


/*
 * The following probably not useful for RA 
 *  - not using MKS curses
 */

/*
 * Interopen Curses for Sunos
 */
#undef	M_CURSES_MEMMAPPED
#define M_TERM_NAME		"dumb"
#undef M_TERMINFO_DIR		/* "/usr/lib/terminfo" */


#if defined(_POSIX_SOURCE) || defined(_XOPEN_SOURCE)

 /* Some Solaris2.3's header files explicitly do NOT define some
  * function prototypes when either _POSIX_SOURCE or _XOPEN_SOURCE are defined.
  *  So, we must explicitly define it ourselves to eliminate some
  *  warning messages.
  */

/* from <string.h> */
extern char *strdup(const char *);

/* from <stdlib.h> */
extern char	*getpass(const char *);
extern void     swab(const char *, char *, int);	/* Solaris2.3 defn */
/* SHOULD BE:
extern void    swab (const void *, void *, ssize_t);
 */

/* from <crypt.h> */
extern char     *crypt (const char *, const char *);
extern void     encrypt (char *, int);


#endif /* defined(_POSIX_SOURCE) || defined(_XOPEN_SOURCE) */

