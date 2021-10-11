/* 
 * Include the SVR4/5.0 errno.h 
 */

#include "s5errno.h"

/*	@(#)errno.h 2.13 89/04/16 SMI; from UCB 4.1 82/12/28	*/

/*
 * XError codes from 4.1
 */

#define XEPERM		1		/* Not owner */
#define XENOENT		2		/* No such file or directory */
#define XESRCH		3		/* No such process */
#define XEINTR		4		/* Interrupted system call */
#define XEIO		5		/* I/O error */
#define XENXIO		6		/* No such device or address */
#define XE2BIG		7		/* Arg list too long */
#define XENOEXEC	8		/* Exec format error */
#define XEBADF		9		/* Bad file number */
#define XECHILD		10		/* No children */
#define XEAGAIN		11		/* No more processes */
#define XENOMEM		12		/* Not enough core */
#define XEACCES		13		/* Permission denied */
#define XEFAULT		14		/* Bad address */
#define XENOTBLK	15		/* Block device required */
#define XEBUSY		16		/* Mount device busy */
#define XEEXIST		17		/* File exists */
#define XEXDEV		18		/* Cross-device link */
#define XENODEV		19		/* No such device */
#define XENOTDIR	20		/* Not a directory*/
#define XEISDIR		21		/* Is a directory */
#define XEINVAL		22		/* Invalid argument */
#define XENFILE		23		/* File table overflow */
#define XEMFILE		24		/* Too many open files */
#define XENOTTY		25		/* Not a typewriter */
#define XETXTBSY	26		/* Text file busy */
#define XEFBIG		27		/* File too large */
#define XENOSPC		28		/* No space left on device */
#define XESPIPE		29		/* Illegal seek */
#define XEROFS		30		/* Read-only file system */
#define XEMLINK		31		/* Too many links */
#define XEPIPE		32		/* Broken pipe */

/* math software */
#define XEDOM		33		/* Argument too large */
#define XERANGE		34		/* Result too large */

/* non-blocking and interrupt i/o */
#define XEWOULDBLOCK	35		/* Operation would block */
#define XEINPROGRESS	36		/* Operation now in progress */
#define XEALREADY	37		/* Operation already in progress */
/* ipc/network software */

	/* argument errors */
#define XENOTSOCK	38		/* Socket operation on non-socket */
#define XEDESTADDRREQ	39		/* Destination address required */
#define XEMSGSIZE	40		/* Message too long */
#define XEPROTOTYPE	41		/* Protocol wrong type for socket */
#define XENOPROTOOPT	42		/* Protocol not available */
#define XEPROTONOSUPPORT	43		/* Protocol not supported */
#define XESOCKTNOSUPPORT	44		/* Socket type not supported */
#define XEOPNOTSUPP	45		/* Operation not supported on socket */
#define XEPFNOSUPPORT	46		/* Protocol family not supported */
#define XEAFNOSUPPORT	47		/* Address family not supported by protocol family */
#define XEADDRINUSE	48		/* Address already in use */
#define XEADDRNOTAVAIL	49		/* Can't assign requested address */

	/* operational errors */
#define XENETDOWN	50		/* Network is down */
#define XENETUNREACH	51		/* Network is unreachable */
#define XENETRESET	52		/* Network dropped connection on reset */
#define XECONNABORTED	53		/* Software caused connection abort */
#define XECONNRESET	54		/* Connection reset by peer */
#define XENOBUFS	55		/* No buffer space available */
#define XEISCONN	56		/* Socket is already connected */
#define XENOTCONN	57		/* Socket is not connected */
#define XESHUTDOWN	58		/* Can't send after socket shutdown */
#define XETOOMANYREFS	59		/* Too many references: can't splice */
#define XETIMEDOUT	60		/* Connection timed out */
#define XECONNREFUSED	61		/* Connection refused */

	/* */
#define XELOOP		62		/* Too many levels of symbolic links */
#define XENAMETOOLONG	63		/* File name too long */

/* should be rearranged */
#define XEHOSTDOWN	64		/* Host is down */
#define XEHOSTUNREACH	65		/* No route to host */
#define XENOTEMPTY	66		/* Directory not empty */

/* quotas & mush */
#define XEPROCLIM	67		/* Too many processes */
#define XEUSERS		68		/* Too many users */
#define XEDQUOT		69		/* Disc quota exceeded */

/* Network File System */
#define XESTALE		70		/* Stale NFS file handle */
#define XEREMOTE	71		/* Too many levels of remote in path */

/* streams */
#define XENOSTR		72		/* Device is not a stream */
#define XETIME		73		/* Timer expired */
#define XENOSR		74		/* Out of streams resources */
#define XENOMSG		75		/* No message of desired type */
#define XEBADMSG	76		/* Trying to read unreadable message */

/* SystemV IPC */
#define XEIDRM		77		/* Identifier removed */

/* SystemV Record Locking */
#define XEDEADLK	78		/* Deadlock condition. */
#define XENOLCK		79		/* No record locks available. */

/* POSIX */
#define XENOSYS		90		/* function not implemented */


void	maperror()
{
	extern int errno;

	if (errno == 0)
	return;

	switch (errno) {
	case ENOMSG:
		errno = XENOMSG;	/* No message of desired type */
		break;
	case EIDRM:
		errno = XEIDRM;		/* Identifier removed */
		break;
	case EDEADLK:
		errno = XEDEADLK;	/* Deadlock condition. */
		break;
	case ENOLCK:
		errno = XENOLCK;	/* No record locks available. */
		break;
	case ENOSTR:
		errno = XENOSTR;	/* Device not a stream */
		break;
	case ETIME:
		errno = XETIME;		/* timer expired */
		break;
	case ENOSR:
		errno = XENOSR;		/* out of streams resources */
		break;
	case EBADMSG:
		errno = XEBADMSG;	/* trying to read unreadable message */
		break;
	case ENOSYS:
		errno = XENOSYS;	/* Unsupported file system operation */
		break;
	case ELOOP:
		errno = XELOOP;		/* Symbolic link loop */
		break;
	case ERESTART:
		errno = XEINTR;		/* Convert ERESTART to EINTR for 
					   interrupted system calls */
		break;
	case ENAMETOOLONG:
		errno = XENAMETOOLONG; /* File name too long */
		break;
	case ENOTEMPTY:
		errno = XENOTEMPTY;	/* directory not empty */
		break;
	case EUSERS:
		errno = XEUSERS;	/* Too many users (for UFS) */
		break;
	case ENOTSOCK:
		errno = XENOTSOCK;	/* Socket operation on non-socket */
		break;
	case EDESTADDRREQ:
		errno = XEDESTADDRREQ;	/* Destination address required */
		break;
	case EMSGSIZE:
		errno = XEMSGSIZE;	/* Message too long */
		break;
	case EPROTOTYPE:
		errno = XEPROTOTYPE;	/* Protocol wrong type for socket */
		break;
	case ENOPROTOOPT:
		errno = XENOPROTOOPT;	/* Protocol not available */
		break;
	case EPROTONOSUPPORT:
		errno = XEPROTONOSUPPORT;	/* Protocol not supported */
		break;
	case ESOCKTNOSUPPORT:
		errno = XESOCKTNOSUPPORT;	/* Socket type not supported */
		break;
	case EOPNOTSUPP:
		errno = XEOPNOTSUPP;	/* Operation not supported on socket */
		break;
	case EPFNOSUPPORT:
		errno = XEPFNOSUPPORT;	/* Protocol family not supported */
		break;
	case EAFNOSUPPORT:
		errno = XEAFNOSUPPORT;	/* Address family not supported by */
		break;
	case EADDRINUSE:
		errno = XEADDRINUSE;	/* Address already in use */
		break;
	case EADDRNOTAVAIL:
		errno = XEADDRNOTAVAIL; /* Can't assign requested address */
		break;
	case ENETDOWN:
		errno = XENETDOWN;	/* Network is down */
		break;
	case ENETUNREACH:
		errno = XENETUNREACH;	/* Network is unreachable */
		break;
	case ENETRESET:
		errno = XENETRESET;	/* Dropped connection due to reset */
		break;
	case ECONNABORTED:
		errno = XECONNABORTED;	/* Software caused connection abort */
		break;
	case ECONNRESET:
		errno = XECONNRESET;	/* Connection reset by peer */
		break;
	case ENOBUFS:
		errno = XENOBUFS;	/* No buffer space available */
		break;
	case EISCONN:
		errno = XEISCONN;	/* Socket is already connected */
		break;
	case ENOTCONN:
		errno = XENOTCONN;	/* Socket is not connected */
		break;
	case ESHUTDOWN:
		errno = XESHUTDOWN;	/* Can't send after socket shutdown */
		break;
	case ETOOMANYREFS:
		errno = XETOOMANYREFS;	/* Too many references: can't splice */
		break;
	case ETIMEDOUT:
		errno = XETIMEDOUT;	/* Connection timed out */
		break;
	case ECONNREFUSED:
		errno = XECONNREFUSED;	/* Connection refused */
		break;
	case EHOSTDOWN:
		errno = XEHOSTDOWN;	/* Host is down */
		break;
	case EHOSTUNREACH:
		errno = XEHOSTUNREACH;	/* No route to host */
		break;
	case EALREADY:
		errno = XEALREADY;
		break;
	case EINPROGRESS:
		errno = XEINPROGRESS;
		break;
	case ESTALE:
		errno = XESTALE;	/* Stale NFS file handle */
		break;
	case EDQUOT:
		errno = XEDQUOT;	/* Disc quota exceeded */
		break;
	default:
		break;
	}

	return;

}
