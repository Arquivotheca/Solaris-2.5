 /*
  * Copyright (c) 1992 Sun Microsystems, Inc.  All Rights Reserved.
  */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)codes.c	1.24	94/09/17 SMI"	/* SVr4.0 1.14	*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/fault.h>
#include <sys/syscall.h>
#include "pcontrol.h"
#include "ioc.h"

#include <ctype.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/fstyp.h>
#ifdef i386
#	include <sys/sysi86.h>
#endif /* i386 */
#ifdef u3b2
#	include <sys/sys3b.h>
#endif
#include <sys/unistd.h>
#ifdef u3b2
#	include <sys/vtoc.h>
#	include <sys/if.h>
#endif
#include <sys/file.h>
#include <sys/tiuser.h>
#include <sys/timod.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/sockmod.h>
#include <sys/termios.h>
#include <sys/termiox.h>
#include <sys/jioctl.h>
#include <sys/filio.h>
#include <fcntl.h>
#include <sys/termio.h>
#include <sys/stermio.h>
#include <sys/ttold.h>
#ifdef u3b2
#	include <sys/pump.h>
#endif
#include <sys/lock.h>
#include <sys/mount.h>
#include <sys/utssys.h>
#include <sys/sysconfig.h>
#include <sys/statvfs.h>
#include <sys/kstat.h>

#include "ramdata.h"
#include "proto.h"

#define	FCNTLMIN	F_DUPFD
#define	FCNTLMAX	F_RSETLKW
static CONST char * CONST FCNTLname[] = {
	"F_DUPFD",
	"F_GETFD",
	"F_SETFD",
	"F_GETFL",
	"F_SETFL",
	"F_O_GETLK",
	"F_SETLK",
	"F_SETLKW",
	NULL,
	NULL,
	"F_ALLOCSP",
	"F_FREESP",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"F_RSETLK",
	"F_RGETLK",
	"F_RSETLKW"
};

#define	SYSFSMIN	GETFSIND
#define	SYSFSMAX	GETNFSTYP
static CONST char * CONST SYSFSname[] = {
	"GETFSIND",
	"GETFSTYP",
	"GETNFSTYP"
};

#define	PLOCKMIN	UNLOCK
#define	PLOCKMAX	DATLOCK
static CONST char * CONST PLOCKname[] = {
	"UNLOCK",
	"PROCLOCK",
	"TXTLOCK",
	 NULL,
	"DATLOCK"
};

#define	SCONFMIN	_CONFIG_NGROUPS
#define	SCONFMAX	_CONFIG_AVPHYS_PAGES
static CONST char * CONST SCONFname[] = {
	"_CONFIG_NGROUPS",		/*  2 */
	"_CONFIG_CHILD_MAX",		/*  3 */
	"_CONFIG_OPEN_FILES",		/*  4 */
	"_CONFIG_POSIX_VER",		/*  5 */
	"_CONFIG_PAGESIZE",		/*  6 */
	"_CONFIG_CLK_TCK",		/*  7 */
	"_CONFIG_XOPEN_VER",		/*  8 */
	"_CONFIG_HRESCLK_TCK",		/*  9 */
	"_CONFIG_PROF_TCK",		/* 10 */
	"_CONFIG_NPROC_CONF",		/* 11 */
	"_CONFIG_NPROC_ONLN",		/* 12 */
	"_CONFIG_AIO_LISTIO_MAX",	/* 13 */
	"_CONFIG_AIO_MAX",		/* 14 */
	"_CONFIG_AIO_PRIO_DELTA_MAX",	/* 15 */
	"_CONFIG_DELAYTIMER_MAX",	/* 16 */
	"_CONFIG_MQ_OPEN_MAX",		/* 17 */
	"_CONFIG_MQ_PRIO_MAX",		/* 18 */
	"_CONFIG_RTSIG_MAX",		/* 19 */
	"_CONFIG_SEM_NSEMS_MAX",	/* 20 */
	"_CONFIG_SEM_VALUE_MAX",	/* 21 */
	"_CONFIG_SIGQUEUE_MAX",		/* 22 */
	"_CONFIG_SIGRT_MIN",		/* 23 */
	"_CONFIG_SIGRT_MAX",		/* 24 */
	"_CONFIG_TIMER_MAX",		/* 25 */
	"_CONFIG_PHYS_PAGES",		/* 26 */
	"_CONFIG_AVPHYS_PAGES"		/* 27 */
};

#define	PATHCONFMIN	_PC_LINK_MAX
#define	PATHCONFMAX	_PC_CHOWN_RESTRICTED
static CONST char * CONST PATHCONFname[] = {
	"_PC_LINK_MAX",
	"_PC_MAX_CANON",
	"_PC_MAX_INPUT",
	"_PC_NAME_MAX",
	"_PC_PATH_MAX",
	"_PC_PIPE_BUF",
	"_PC_NO_TRUNC",
	"_PC_VDISABLE",
	"_PC_CHOWN_RESTRICTED"
};

static CONST struct ioc {
	int	code;
	CONST char * name;
} ioc[] = {
	{ TCGETA	, "TCGETA"	},
	{ TCSETA	, "TCSETA"	},
	{ TCSETAW	, "TCSETAW"	},
	{ TCSETAF	, "TCSETAF"	},
	{ TCFLSH	, "TCFLSH"	},
	{ TIOCKBON	, "TIOCKBON"	},
	{ TIOCKBOF	, "TIOCKBOF"	},
	{ KBENABLED	, "KBENABLED"	},
	{ TCGETS	, "TCGETS"	},
	{ TCSETS	, "TCSETS"	},
	{ TCSETSW	, "TCSETSW"	},
	{ TCSETSF	, "TCSETSF"	},
	{ TCDSET	, "TCDSET"	},
	{ RTS_TOG	, "RTS_TOG"	},
	{ TIOCSWINSZ	, "TIOCSWINSZ"	},
	{ TIOCGWINSZ	, "TIOCGWINSZ"	},
	{ TIOCGETD	, "TIOCGETD"	},
	{ TIOCSETD	, "TIOCSETD"	},
	{ TIOCHPCL	, "TIOCHPCL"	},
	{ TIOCGETP	, "TIOCGETP"	},
	{ TIOCSETP	, "TIOCSETP"	},
	{ TIOCSETN	, "TIOCSETN"	},
	{ TIOCEXCL	, "TIOCEXCL"	},
	{ TIOCNXCL	, "TIOCNXCL"	},
	{ TIOCFLUSH	, "TIOCFLUSH"	},
	{ TIOCSETC	, "TIOCSETC"	},
	{ TIOCGETC	, "TIOCGETC"	},
	{ TIOCGPGRP	, "TIOCGPGRP"	},
	{ TIOCSPGRP	, "TIOCSPGRP"	},
	{ TIOCGSID	, "TIOCGSID"	},
	{ TIOCSTI	, "TIOCSTI"	},
	{ TIOCSSID	, "TIOCSSID"	},
	{ TIOCMSET	, "TIOCMSET"	},
	{ TIOCMBIS	, "TIOCMBIS"	},
	{ TIOCMBIC	, "TIOCMBIC"	},
	{ TIOCMGET	, "TIOCMGET"	},
	{ TIOCREMOTE	, "TIOCREMOTE"	},
	{ TIOCSIGNAL	, "TIOCSIGNAL"	},
	{ TIOCSTART	, "TIOCSTART"	},
	{ TIOCSTOP	, "TIOCSTOP"	},
	{ TIOCNOTTY	, "TIOCNOTTY"	},
	{ TIOCOUTQ	, "TIOCOUTQ"	},
	{ TIOCGLTC	, "TIOCGLTC"	},
	{ TIOCSLTC	, "TIOCSLTC"	},
	{ TIOCCDTR	, "TIOCCDTR"	},
	{ TIOCSDTR	, "TIOCSDTR"	},
	{ TIOCCBRK	, "TIOCCBRK"	},
	{ TIOCSBRK	, "TIOCSBRK"	},
	{ TIOCLGET	, "TIOCLGET"	},
	{ TIOCLSET	, "TIOCLSET"	},
	{ TIOCLBIC	, "TIOCLBIC"	},
	{ TIOCLBIS	, "TIOCLBIS"	},
	{ LDOPEN	, "LDOPEN"	},
	{ LDCLOSE	, "LDCLOSE"	},
	{ LDCHG		, "LDCHG"	},
	{ LDGETT	, "LDGETT"	},
	{ LDSETT	, "LDSETT"	},
	{ LDSMAP	, "LDSMAP"	},
	{ LDGMAP	, "LDGMAP"	},
	{ LDNMAP	, "LDNMAP"	},
	{ TCGETX	, "TCGETX"	},
	{ TCSETX	, "TCSETX"	},
	{ TCSETXW	, "TCSETXW"	},
	{ TCSETXF	, "TCSETXF"	},
	{ FIORDCHK	, "FIORDCHK"	},
	{ FIOCLEX	, "FIOCLEX"	},
	{ FIONCLEX	, "FIONCLEX"	},
	{ FIONREAD	, "FIONREAD"	},
	{ FIONBIO	, "FIONBIO"	},
	{ FIOASYNC	, "FIOASYNC"	},
	{ FIOSETOWN	, "FIOSETOWN"	},
	{ FIOGETOWN	, "FIOGETOWN"	},
#ifdef DIOCGETP
	{ DIOCGETP	, "DIOCGETP"	},
	{ DIOCSETP	, "DIOCSETP"	},
#endif
#ifdef DIOCGETC
	{ DIOCGETC	, "DIOCGETC"	},
	{ DIOCGETB	, "DIOCGETB"	},
	{ DIOCSETE	, "DIOCSETE"	},
#endif
#ifdef EI_RESET
	{ EI_RESET	, "EI_RESET"	},
	{ EI_LOAD	, "EI_LOAD"	},
	{ EI_FCF	, "EI_FCF"	},
	{ EI_SYSGEN	, "EI_SYSGEN"	},
	{ EI_SETID	, "EI_SETID"	},
	{ EI_TURNON	, "EI_TURNON"	},
	{ EI_ALLOC	, "EI_ALLOC"	},
	{ EI_TERM	, "EI_TERM"	},
	{ EI_TURNOFF	, "EI_TURNOFF"	},
	{ EI_SETA	, "EI_SETA"	},
	{ EI_GETA	, "EI_GETA"	},
#endif
#ifdef IFFORMAT
	{ IFFORMAT	, "IFFORMAT"	},
	{ IFBCHECK	, "IFBCHECK"	},
	{ IFCONFIRM	, "IFCONFIRM"	},
#endif
#ifdef LIOCGETP
	{ LIOCGETP	, "LIOCGETP"	},
	{ LIOCSETP	, "LIOCSETP"	},
	{ LIOCGETS	, "LIOCGETS"	},
	{ LIOCSETS	, "LIOCSETS"	},
#endif
#ifdef JBOOT
	{ JBOOT		, "JBOOT"	},
	{ JTERM		, "JTERM"	},
	{ JMPX		, "JMPX"	},
#ifdef JTIMO
	{ JTIMO		, "JTIMO"	},
#endif
	{ JWINSIZE	, "JWINSIZE"	},
	{ JTIMOM	, "JTIMOM"	},
	{ JZOMBOOT	, "JZOMBOOT"	},
	{ JAGENT	, "JAGENT"	},
	{ JTRUN		, "JTRUN"	},
	{ JXTPROTO	, "JXTPROTO"	},
#endif
	{ KSTAT_IOC_CHAIN_ID,	"KSTAT_IOC_CHAIN_ID"	},
	{ KSTAT_IOC_READ,	"KSTAT_IOC_READ"	},
	{ KSTAT_IOC_WRITE,	"KSTAT_IOC_WRITE"	},
#ifdef NISETA
	{ NISETA	, "NISETA"	},
	{ NIGETA	, "NIGETA"	},
	{ SUPBUF	, "SUPBUF"	},
	{ RDBUF		, "RDBUF"	},
	{ NIERRNO	, "NIERRNO"	},
	{ STATGET	, "STATGET"	},
	{ NISTATUS	, "NISTATUS"	},
	{ NIPUMP	, "NIPUMP"	},
	{ NIRESET	, "NIRESET"	},
	{ NISELGRP	, "NISELGRP"	},
	{ NISELECT	, "NISELECT"	},
#endif
	{ STGET		, "STGET"	},
	{ STSET		, "STSET"	},
	{ STTHROW	, "STTHROW"	},
	{ STWLINE	, "STWLINE"	},
	{ STTSV		, "STTSV"	},
	{ I_NREAD	, "I_NREAD"	},
	{ I_PUSH	, "I_PUSH"	},
	{ I_POP		, "I_POP"	},
	{ I_LOOK	, "I_LOOK"	},
	{ I_FLUSH	, "I_FLUSH"	},
	{ I_SRDOPT	, "I_SRDOPT"	},
	{ I_GRDOPT	, "I_GRDOPT"	},
	{ I_STR		, "I_STR"	},
	{ I_SETSIG	, "I_SETSIG"	},
	{ I_GETSIG	, "I_GETSIG"	},
	{ I_FIND	, "I_FIND"	},
	{ I_LINK	, "I_LINK"	},
	{ I_UNLINK	, "I_UNLINK"	},
	{ I_PEEK	, "I_PEEK"	},
	{ I_FDINSERT	, "I_FDINSERT"	},
	{ I_SENDFD	, "I_SENDFD"	},
	{ I_RECVFD	, "I_RECVFD"	},
	{ I_SWROPT	, "I_SWROPT"	},
	{ I_GWROPT	, "I_GWROPT"	},
	{ I_LIST	, "I_LIST"	},
	{ I_PLINK	, "I_PLINK"	},
	{ I_PUNLINK	, "I_PUNLINK"	},
	{ I_SETEV	, "I_SETEV"	},
	{ I_GETEV	, "I_GETEV"	},
	{ I_STREV	, "I_STREV"	},
	{ I_UNSTREV	, "I_UNSTREV"	},
	{ I_FLUSHBAND	, "I_FLUSHBAND"	},
	{ I_CKBAND	, "I_CKBAND"	},
	{ I_GETBAND	, "I_GETBAND"	},
	{ I_ATMARK	, "I_ATMARK"	},
	{ I_SETCLTIME	, "I_SETCLTIME"	},
	{ I_GETCLTIME	, "I_GETCLTIME"	},
	{ I_CANPUT	, "I_CANPUT"	},
#ifdef TI_GETINFO
	{ TI_GETINFO	, "TI_GETINFO"	},
	{ TI_OPTMGMT	, "TI_OPTMGMT"	},
	{ TI_BIND	, "TI_BIND"	},
	{ TI_UNBIND	, "TI_UNBIND"	},
#endif
#ifdef TI_GETMYNAME
	{ TI_GETMYNAME	, "TI_GETMYNAME"},
	{ TI_GETPEERNAME, "TI_GETPEERNAME"},
	{ TI_SETMYNAME	, "TI_SETMYNAME"},
	{ TI_SETPEERNAME, "TI_SETPEERNAME"},
#endif
#ifdef SI_GETUDATA
	{ O_SI_GETUDATA	, "O_SI_GETUDATA"},
	{ SI_SHUTDOWN	, "SI_SHUTDOWN"	},
	{ SI_LISTEN	, "SI_LISTEN"	},
	{ SI_SETMYNAME	, "SI_SETMYNAME"},
	{ SI_SETPEERNAME, "SI_SETPEERNAME"},
	{ SI_GETINTRANSIT, "SI_GETINTRANSIT"},
	{ SI_TCL_LINK	, "SI_TCL_LINK"	},
	{ SI_TCL_UNLINK	, "SI_TCL_UNLINK"},
	{ SI_SOCKPARAMS	, "SI_SOCKPARAMS"},
	{ SI_GETUDATA	, "SI_GETUDATA"	},
#endif
#ifdef V_PREAD
	{ V_PREAD	, "V_PREAD"	},
	{ V_PWRITE	, "V_PWRITE"	},
	{ V_PDREAD	, "V_PDREAD"	},
	{ V_PDWRITE	, "V_PDWRITE"	},
#   if !defined(i386)
	{ V_GETSSZ	, "V_GETSSZ"	},
#   endif /* !i386 */
#endif
	{ PIOCSTATUS	, "PIOCSTATUS"	},
	{ PIOCSTOP	, "PIOCSTOP"	},
	{ PIOCWSTOP	, "PIOCWSTOP"	},
	{ PIOCRUN	, "PIOCRUN"	},
	{ PIOCGTRACE	, "PIOCGTRACE"	},
	{ PIOCSTRACE	, "PIOCSTRACE"	},
	{ PIOCSSIG	, "PIOCSSIG"	},
	{ PIOCKILL	, "PIOCKILL"	},
	{ PIOCUNKILL	, "PIOCUNKILL"	},
	{ PIOCGHOLD	, "PIOCGHOLD"	},
	{ PIOCSHOLD	, "PIOCSHOLD"	},
	{ PIOCMAXSIG	, "PIOCMAXSIG"	},
	{ PIOCACTION	, "PIOCACTION"	},
	{ PIOCGFAULT	, "PIOCGFAULT"	},
	{ PIOCSFAULT	, "PIOCSFAULT"	},
	{ PIOCCFAULT	, "PIOCCFAULT"	},
	{ PIOCGENTRY	, "PIOCGENTRY"	},
	{ PIOCSENTRY	, "PIOCSENTRY"	},
	{ PIOCGEXIT	, "PIOCGEXIT"	},
	{ PIOCSEXIT	, "PIOCSEXIT"	},
	{ PIOCSFORK	, "PIOCSFORK"	},
	{ PIOCRFORK	, "PIOCRFORK"	},
	{ PIOCSRLC	, "PIOCSRLC"	},
	{ PIOCRRLC	, "PIOCRRLC"	},
	{ PIOCGREG	, "PIOCGREG"	},
	{ PIOCSREG	, "PIOCSREG"	},
	{ PIOCGFPREG	, "PIOCGFPREG"	},
	{ PIOCSFPREG	, "PIOCSFPREG"	},
	{ PIOCNICE	, "PIOCNICE"	},
	{ PIOCPSINFO	, "PIOCPSINFO"	},
	{ PIOCNMAP	, "PIOCNMAP"	},
	{ PIOCMAP	, "PIOCMAP"	},
	{ PIOCOPENM	, "PIOCOPENM"	},
	{ PIOCCRED	, "PIOCCRED"	},
	{ PIOCGROUPS	, "PIOCGROUPS"	},
	{ PIOCGETPR	, "PIOCGETPR"	},
	{ PIOCGETU	, "PIOCGETU"	},
	{ PIOCSET	, "PIOCSET"	},
	{ PIOCRESET	, "PIOCRESET"	},
#ifdef PIOCNWATCH
	{ PIOCNWATCH	, "PIOCNWATCH"	},
	{ PIOCGWATCH	, "PIOCGWATCH"	},
	{ PIOCSWATCH	, "PIOCSWATCH"	},
#endif
	{ PIOCUSAGE	, "PIOCUSAGE"	},
	{ PIOCOPENPD	, "PIOCOPENPD"	},
	{ PIOCLWPIDS	, "PIOCLWPIDS"	},
	{ PIOCOPENLWP	, "PIOCOPENLWP"	},
	{ PIOCLSTATUS	, "PIOCLSTATUS"	},
	{ PIOCLUSAGE	, "PIOCLUSAGE"	},
	{ PIOCNAUXV	, "PIOCNAUXV"	},
	{ PIOCAUXV	, "PIOCAUXV"	},
#ifdef PIOCGWIN
	{ PIOCGWIN	, "PIOCGWIN"	},
#endif
#ifdef PIOCNLDT
	{ PIOCNLDT	, "PIOCNLDT"	},
	{ PIOCLDT	, "PIOCLDT"	},
#endif
#ifdef PUMP
	{ PUMP		, "PUMP"	},
#endif
#ifdef PIOCGXREGSIZE
	{ PIOCGXREGSIZE	, "PIOCGXREGSIZE" },
	{ PIOCGXREG	, "PIOCGXREG"	},
	{ PIOCSXREG	, "PIOCSXREG"	},
#endif
	{ 0		,  NULL		}
};

CONST char *
ioctlname(code)
register int code;
{
	register CONST struct ioc *ip;
	register CONST char * str = NULL;
	register int c;

	for (ip = &ioc[0]; ip->name; ip++) {
		if (code == ip->code) {
			str = ip->name;
			break;
		}
	}

	if (str == NULL) {
		c = code >> 8;
		if (isascii(c) && isprint(c))
			(void) sprintf(code_buf, "(('%c'<<8)|%d)",
				c, code & 0xff);
		else
			(void) sprintf(code_buf, "0x%.4X", code);
		str = code_buf;
	}

	return str;
}

CONST char *
fcntlname(code)
register int code;
{
	register CONST char * str = NULL;

	if (code >= FCNTLMIN && code <= FCNTLMAX)
		str = FCNTLname[code-FCNTLMIN];
	return str;
}

CONST char *
sfsname(code)
register int code;
{
	register CONST char * str = NULL;

	if (code >= SYSFSMIN && code <= SYSFSMAX)
		str = SYSFSname[code-SYSFSMIN];
	return str;
}

CONST char *
plockname(code)
register int code;
{
	register CONST char * str = NULL;

	if (code >= PLOCKMIN && code <= PLOCKMAX)
		str = PLOCKname[code-PLOCKMIN];
	return str;
}

/* ARGSUSED */
CONST char *
#if defined(i386)
si86name(code)
#else /* !i386 */
s3bname(code)
#endif /* !i386 */
register int code;
{
	register CONST char * str = NULL;

#if defined(i386)
	switch (code) {
	case SI86SWPI:		str = "SI86SWPI";	break;
	case SI86SYM:		str = "SI86SYM";	break;
	case SI86CONF:		str = "SI86CONF";	break;
	case SI86BOOT:		str = "SI86BOOT";	break;
	case SI86AUTO:		str = "SI86AUTO";	break;
	case SI86EDT:		str = "SI86EDT";	break;
	case SI86SWAP:		str = "SI86SWAP";	break;
	case SI86FPHW:		str = "SI86FPHW";	break;
	case GRNON:		str = "GRNON";		break;
	case GRNFLASH:		str = "GRNFLASH";	break;
	case STIME:		str = "STIME";		break;
	case SETNAME:		str = "SETNAME";	break;
	case RNVR:		str = "RNVR";		break;
	case WNVR:		str = "WNVR";		break;
	case RTODC:		str = "RTODC";		break;
	case CHKSER:		str = "CHKSER";		break;
	case SI86NVPRT:		str = "SI86NVPRT";	break;
	case SANUPD:		str = "SANUPD";		break;
	case SI86KSTR:		str = "SI86KSTR";	break;
	case SI86MEM:		str = "SI86MEM";	break;
	case SI86TODEMON:	str = "SI86TODEMON";	break;
	case SI86CCDEMON:	str = "SI86CCDEMON";	break;
	case SI86CACHE:		str = "SI86CACHE";	break;
	case SI86DELMEM:	str = "SI86DELMEM";	break;
	case SI86ADDMEM:	str = "SI86ADDMEM";	break;
/* 71 through 74 reserved for VPIX */
	case SI86V86: 		str = "SI86V86";	break;
	case SI86SLTIME:	str = "SI86SLTIME";	break;
	case SI86DSCR:		str = "SI86DSCR";	break;
	case RDUBLK:		str = "RDUBLK";		break;
/* NFA entry point */
	case SI86NFA:		str = "SI86NFA";	break;
	case SI86VM86:		str = "SI86VM86";	break;
	case SI86VMENABLE:	str = "SI86VMENABLE";	break;
	case SI86LIMUSER:	str = "SI86LIMUSER";	break;
	case SI86RDID: 		str = "SI86RDID";	break;
	case SI86RDBOOT:	str = "SI86RDBOOT";	break;
/* Merged Product defines */
	case SI86SHFIL:		str = "SI86SHFIL";	break;
	case SI86PCHRGN:	str = "SI86PCHRGN";	break;
	case SI86BADVISE:	str = "SI86BADVISE";	break;
	case SI86SHRGN:		str = "SI86SHRGN";	break;
	case SI86CHIDT:		str = "SI86CHIDT";	break;
	case SI86EMULRDA: 	str = "SI86EMULRDA";	break;
	}
#elif defined(u3b2)
	switch (code) {
	case S3BSWPI:	str = "S3BSWPI";	break;
	case S3BSYM:	str = "S3BSYM";		break;
	case S3BCONF:	str = "S3BCONF";	break;
	case S3BBOOT:	str = "S3BBOOT";	break;
	case S3BIOP:	str = "S3BIOP";		break;
	case S3BDMM:	str = "S3BDMM";		break;
	case S3BAUTO:	str = "S3BAUTO";	break;
	case S3BEDT:	str = "S3BEDT";		break;
	case S3BSWAP:	str = "S3BSWAP";	break;
	case S3BFPHW:	str = "S3BFPHW";	break;
	case GRNON:	str = "GRNON";		break;
	case GRNFLASH:	str = "GRNFLASH";	break;
	case STIME:	str = "STIME";		break;
	case SETNAME:	str = "SETNAME";	break;
	case RNVR:	str = "RNVR";		break;
	case WNVR:	str = "WNVR";		break;
	case RTODC:	str = "RTODC";		break;
	case CHKSER:	str = "CHKSER";		break;
	case S3BNVPRT:	str = "S3BNVPRT";	break;
	case SANUPD:	str = "SANUPD";		break;
	case S3BKSTR:	str = "S3BKSTR";	break;
	case S3BMEM:	str = "S3BMEM";		break;
	case S3BTODEMON:str = "S3BTODEMON";	break;
	case S3BCCDEMON:str = "S3BCCDEMON";	break;
	case S3BCACHE:	str = "S3BCACHE";	break;
	case S3BDELMEM:	str = "S3BDELMEM";	break;
	case S3BADDMEM:	str = "S3BADDMEM";	break;
	case RDUBLK:	str = "RDUBLK";		break;
	case S3BFPOVR:	str = "S3BFPOVR";	break;
	}
#endif /* u3b2 */

	return str;
}

CONST char *
utscode(code)
register int code;
{
	register CONST char * str = NULL;

	switch (code) {
	case UTS_UNAME:		str = "UNAME";	break;
	case UTS_USTAT:		str = "USTAT";	break;
	case UTS_FUSERS:	str = "FUSERS";	break;
	}

	return str;
}

CONST char *
sconfname(code)
register int code;
{
	register CONST char * str = NULL;

	if (code >= SCONFMIN && code <= SCONFMAX)
		str = SCONFname[code-SCONFMIN];
	return str;
}

CONST char *
pathconfname(code)
register int code;
{
	register CONST char * str = NULL;

	if (code >= PATHCONFMIN && code <= PATHCONFMAX)
		str = PATHCONFname[code-PATHCONFMIN];
	return str;
}

CONST char *
sigarg(arg)
register int arg;
{
	register char * str = NULL;
	register int sig = (arg & SIGNO_MASK);

	str = code_buf;
	arg &= ~SIGNO_MASK;
	if (arg & ~(SIGDEFER|SIGHOLD|SIGRELSE|SIGIGNORE|SIGPAUSE))
		(void) sprintf(str, "%s|0x%X", signame(sig), arg);
	else {
		(void) strcpy(str, signame(sig));
		if (arg & SIGDEFER)
			(void) strcat(str, "|SIGDEFER");
		if (arg & SIGHOLD)
			(void) strcat(str, "|SIGHOLD");
		if (arg & SIGRELSE)
			(void) strcat(str, "|SIGRELSE");
		if (arg & SIGIGNORE)
			(void) strcat(str, "|SIGIGNORE");
		if (arg & SIGPAUSE)
			(void) strcat(str, "|SIGPAUSE");
	}

	return (CONST char *)str;
}

CONST char *
openarg(arg)
register int arg;
{
	register char * str = code_buf;

	switch (arg &
	  ~(O_NDELAY|O_APPEND|O_SYNC|O_DSYNC|O_NONBLOCK|O_CREAT|O_TRUNC|O_EXCL))
	{
	default:
		return (char *)NULL;
	case O_RDONLY:
		(void) strcpy(str, "O_RDONLY");
		break;
	case O_WRONLY:
		(void) strcpy(str, "O_WRONLY");
		break;
	case O_RDWR:
		(void) strcpy(str, "O_RDWR");
		break;
	}

	if (arg & O_NDELAY)
		(void) strcat(str, "|O_NDELAY");
	if (arg & O_APPEND)
		(void) strcat(str, "|O_APPEND");
	if (arg & O_SYNC)
		(void) strcat(str, "|O_SYNC");
	if (arg & O_DSYNC)
		(void) strcat(str, "|O_DSYNC");
	if (arg & O_NONBLOCK)
		(void) strcat(str, "|O_NONBLOCK");
	if (arg & O_CREAT)
		(void) strcat(str, "|O_CREAT");
	if (arg & O_TRUNC)
		(void) strcat(str, "|O_TRUNC");
	if (arg & O_EXCL)
		(void) strcat(str, "|O_EXCL");

	return (CONST char *)str;
}

CONST char *
whencearg(arg)
register int arg;
{
	register CONST char * str = NULL;

	switch (arg) {
	case SEEK_SET:	str = "SEEK_SET";	break;
	case SEEK_CUR:	str = "SEEK_CUR";	break;
	case SEEK_END:	str = "SEEK_END";	break;
	}

	return str;
}

#define	IPC_FLAGS	(IPC_ALLOC|IPC_CREAT|IPC_EXCL|IPC_NOWAIT)

static char *
ipcflags(arg)
register int arg;
{
	register char * str = code_buf;

	if (arg&0777)
		(void) sprintf(str, "0%.3o", arg&0777);
	else
		*str = '\0';

	if (arg & IPC_ALLOC)
		(void) strcat(str, "|IPC_ALLOC");
	if (arg & IPC_CREAT)
		(void) strcat(str, "|IPC_CREAT");
	if (arg & IPC_EXCL)
		(void) strcat(str, "|IPC_EXCL");
	if (arg & IPC_NOWAIT)
		(void) strcat(str, "|IPC_NOWAIT");

	return str;
}

CONST char *
msgflags(arg)
register int arg;
{
	register char * str;

	if (arg == 0 || (arg & ~(IPC_FLAGS|MSG_NOERROR|0777)) != 0)
		return (char *)NULL;

	str = ipcflags(arg);

	if (arg & MSG_NOERROR)
		(void) strcat(str, "|MSG_NOERROR");

	if (*str == '|')
		str++;
	return (CONST char *)str;
}

CONST char *
semflags(arg)
register int arg;
{
	register char * str;

	if (arg == 0 || (arg & ~(IPC_FLAGS|SEM_UNDO|0777)) != 0)
		return (char *)NULL;

	str = ipcflags(arg);

	if (arg & SEM_UNDO)
		(void) strcat(str, "|SEM_UNDO");

	if (*str == '|')
		str++;
	return (CONST char *)str;
}

CONST char *
shmflags(arg)
register int arg;
{
	register char * str;

	if (arg == 0 || (arg & ~(IPC_FLAGS|SHM_RDONLY|SHM_RND|0777)) != 0)
		return (char *)NULL;

	str = ipcflags(arg);

	if (arg & SHM_RDONLY)
		(void) strcat(str, "|SHM_RDONLY");
	if (arg & SHM_RND)
		(void) strcat(str, "|SHM_RND");

	if (*str == '|')
		str++;
	return (CONST char *)str;
}

#define	MSGCMDMIN	IPC_RMID
#define	MSGCMDMAX	IPC_STAT
static CONST char * CONST MSGCMDname[MSGCMDMAX+1] = {
	"IPC_RMID",
	"IPC_SET",
	"IPC_STAT",
};

#define	SEMCMDMIN	IPC_RMID
#define	SEMCMDMAX	SETALL
static CONST char * CONST SEMCMDname[SEMCMDMAX+1] = {
	"IPC_RMID",
	"IPC_SET",
	"IPC_STAT",
	"GETNCNT",
	"GETPID",
	"GETVAL",
	"GETALL",
	"GETZCNT",
	"SETVAL",
	"SETALL",
};

#define	SHMCMDMIN	IPC_RMID
#ifdef	SHM_UNLOCK
#	define	SHMCMDMAX	SHM_UNLOCK
#else
#	define	SHMCMDMAX	IPC_STAT
#endif
static CONST char * CONST SHMCMDname[SHMCMDMAX+1] = {
	"IPC_RMID",
	"IPC_SET",
	"IPC_STAT",
#ifdef	SHM_UNLOCK
	"SHM_LOCK",
	"SHM_UNLOCK",
#endif
};

CONST char *
msgcmd(arg)
register int arg;
{
	register CONST char * str = NULL;

	if (arg >= MSGCMDMIN && arg <= MSGCMDMAX)
		str = MSGCMDname[arg-MSGCMDMIN];
	return str;
}

CONST char *
semcmd(arg)
register int arg;
{
	register CONST char * str = NULL;

	if (arg >= SEMCMDMIN && arg <= SEMCMDMAX)
		str = SEMCMDname[arg-SEMCMDMIN];
	return str;
}

CONST char *
shmcmd(arg)
register int arg;
{
	register CONST char * str = NULL;

	if (arg >= SHMCMDMIN && arg <= SHMCMDMAX)
		str = SHMCMDname[arg-SHMCMDMIN];
	return str;
}

CONST char *
strrdopt(arg)		/* streams read option (I_SRDOPT I_GRDOPT) */
register int arg;
{
	register CONST char * str = NULL;

	switch (arg) {
	case RNORM:	str = "RNORM";		break;
	case RMSGD:	str = "RMSGD";		break;
	case RMSGN:	str = "RMSGN";		break;
	}

	return str;
}

CONST char *
strevents(arg)		/* bit map of streams events (I_SETSIG & I_GETSIG) */
register int arg;
{
	register char * str = code_buf;

	if (arg & ~(S_INPUT|S_HIPRI|S_OUTPUT|S_MSG|S_ERROR|S_HANGUP))
		return (char *)NULL;

	*str = '\0';
	if (arg & S_INPUT)
		(void) strcat(str, "|S_INPUT");
	if (arg & S_HIPRI)
		(void) strcat(str, "|S_HIPRI");
	if (arg & S_OUTPUT)
		(void) strcat(str, "|S_OUTPUT");
	if (arg & S_MSG)
		(void) strcat(str, "|S_MSG");
	if (arg & S_ERROR)
		(void) strcat(str, "|S_ERROR");
	if (arg & S_HANGUP)
		(void) strcat(str, "|S_HANGUP");

	return (CONST char *)(str+1);
}

CONST char *
tiocflush(arg)		/* bit map passsed by TIOCFLUSH */
register int arg;
{
	register char * str = code_buf;

	if (arg & ~(FREAD|FWRITE))
		return (char *)NULL;

	*str = '\0';
	if (arg & FREAD)
		(void) strcat(str, "|FREAD");
	if (arg & FWRITE)
		(void) strcat(str, "|FWRITE");

	return (CONST char *)(str+1);
}

CONST char *
strflush(arg)		/* streams flush option (I_FLUSH) */
register int arg;
{
	register CONST char * str = NULL;

	switch (arg) {
	case FLUSHR:	str = "FLUSHR";		break;
	case FLUSHW:	str = "FLUSHW";		break;
	case FLUSHRW:	str = "FLUSHRW";	break;
	}

	return str;
}

#define	ALL_MOUNT_FLAGS	\
	(MS_RDONLY|MS_FSS|MS_DATA|MS_NOSUID|MS_REMOUNT|MS_NOTRUNC|MS_OVERLAY)

CONST char *
mountflags(arg)		/* bit map of mount syscall flags */
register int arg;
{
	register char * str = code_buf;

	if (arg & ~ALL_MOUNT_FLAGS)
		return (char *)NULL;

	*str = '\0';
	if (arg & MS_RDONLY)
		(void) strcat(str, "|MS_RDONLY");
	if (arg & MS_FSS)
		(void) strcat(str, "|MS_FSS");
	if (arg & MS_DATA)
		(void) strcat(str, "|MS_DATA");
	if (arg & MS_NOSUID)
		(void) strcat(str, "|MS_NOSUID");
	if (arg & MS_REMOUNT)
		(void) strcat(str, "|MS_REMOUNT");
	if (arg & MS_NOTRUNC)
		(void) strcat(str, "|MS_NOTRUNC");
	if (arg & MS_OVERLAY)
		(void) strcat(str, "|MS_OVERLAY");
	return *str? (CONST char *)(str+1) : "0";
}

CONST char *
svfsflags(arg)		/* bit map of statvfs syscall flags */
register int arg;
{
	register char * str = code_buf;

	if (arg & ~(ST_RDONLY|ST_NOSUID|ST_NOTRUNC)) {
		(void) sprintf(str, "0x%x", arg);
		return str;
	}
	*str = '\0';
	if (arg & ST_RDONLY)
		(void) strcat(str, "|ST_RDONLY");
	if (arg & ST_NOSUID)
		(void) strcat(str, "|ST_NOSUID");
	if (arg & ST_NOTRUNC)
		(void) strcat(str, "|ST_NOTRUNC");
	return *str? (CONST char *)(str+1) : "0";
}

CONST char *
fuiname(arg)		/* fusers() input argument */
register int arg;
{
	register CONST char * str = NULL;

	switch (arg) {
	case F_FILE_ONLY:	str = "F_FILE_ONLY";		break;
	case F_CONTAINED:	str = "F_CONTAINED";		break;
	}

	return str;
}

CONST char *
fuflags(arg)		/* fusers() output flags */
register int arg;
{
	register char * str = code_buf;

	if (arg & ~(F_CDIR|F_RDIR|F_TEXT|F_MAP|F_OPEN|F_TRACE|F_TTY)) {
		(void) sprintf(str, "0x%x", arg);
		return str;
	}
	*str = '\0';
	if (arg & F_CDIR)
		(void) strcat(str, "|F_CDIR");
	if (arg & F_RDIR)
		(void) strcat(str, "|F_RDIR");
	if (arg & F_TEXT)
		(void) strcat(str, "|F_TEXT");
	if (arg & F_MAP)
		(void) strcat(str, "|F_MAP");
	if (arg & F_OPEN)
		(void) strcat(str, "|F_OPEN");
	if (arg & F_TRACE)
		(void) strcat(str, "|F_TRACE");
	if (arg & F_TTY)
		(void) strcat(str, "|F_TTY");
	return *str? (CONST char *)(str+1) : "0";
}
