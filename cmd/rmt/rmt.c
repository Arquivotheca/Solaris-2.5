#ident	"@(#)rmt.c	1.17	93/05/18 SMI"	/* from CalTech */

/*
 *  Multi-process streaming 4.3bsd /etc/rmt server.
 *  Has three locks (for stdin, stdout, and the tape)
 *  that are passed by signals and received by sigpause().
 */

#include <stdio.h>
#include <locale.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>

static sigset_t	cmdmask, maskall, newmask;
static sigset_t	sendmask, tapemask;

static struct mtop mtop;
static struct mtget mtget;
static jmp_buf sjbuf;

#define	RECV	SIGIO
#define	TAPE	SIGURG
#define	SEND	SIGALRM
#define	ERROR	SIGTERM
#define	OPEN	SIGUSR1
#define	CLOSE	SIGUSR2

/*
 * Support for Version 1 of the extended RMT protocol:
 * Placing RMTIVERSION (-1) into the mt_op field of the ioctl ('I')
 * request will return the current version of the RMT protocol that
 * the server supports.  For servers that don't support Version 1,
 * an error is returned and the client knows to knly use Version 0
 * (stock BSD) calls, which include mt_op values in the range of [0-7].
 *
 * Note: The RMTIVERSION request must be made in order for the extended
 * protocol commands to be recognized.
 */
#define	RMTIVERSION	-1
#define	RMT_VERSION	1

/*
 * These requests are made to the extended RMT protocol by specifying the
 * new 'i' command of RMT Protocol Version 1.  They are intended to allow
 * an intelligent client to communicate with both BSD and Solaris RMT
 * servers heterogeneously.  The 'i' command taks an mtop structure as
 * argument, exactly like the 'I' command does.
 */
#define	RMTICACHE	0
#define	RMTINOCACHE	1
#define	RMTIRETEN	2
#define	RMTIERASE	3
#define	RMTIEOM		4
#define	RMTINBSF	5

/*
 * These requests are made to the extended RMT protocol by specifying the
 * new 's' command of RMT Protocol Version 1.  They are intended to allow
 * an intelligent client to obtain "mt status" information with both BSD
 * and Solaris RMT servers heterogeneously.  They return the requested
 * piece of the mtget structure as an ascii integer.  The request is made
 * by sending the required character immediately after the 's' character
 * without any trailing newline.  A single ascii integer is returned, else
 * an error is returned.
 */
#define	MTS_TYPE	'T'		/* mtget.mt_type */
#define	MTS_DSREG	'D'		/* mtget.mt_dsreg */
#define	MTS_ERREG	'E'		/* mtget.mt_erreg */
#define	MTS_RESID	'R'		/* mtget.mt_resid */
#define	MTS_FILENO	'F'		/* mtget.mt_fileno */
#define	MTS_BLKNO	'B'		/* mtget.mt_blkno */
#define	MTS_FLAGS	'f'		/* mtget.mt_flags */
#define	MTS_BF		'b'		/* mtget.mt_bf */

#define	MAXCHILD 1
static pid_t	childpid[MAXCHILD];
static int	children;

static int	tape = -1;
static int	maxrecsize;
static char	*record;

#define	SSIZE	64
static char	device[SSIZE], pos[SSIZE], op[SSIZE], mode[SSIZE], count[SSIZE];

static FILE	*debug;
#define	DEBUG(f)		if (debug) (void) fprintf(debug, (f))
#define	DEBUG1(f, a)		if (debug) (void) fprintf(debug, (f), (a))
#define	DEBUG2(f, a, b)		if (debug) (void) fprintf(debug, (f), (a), (b))
#define	DEBUG3(f, a, b, c)	if (debug) \
				    (void) fprintf(debug, (f), (a), (b), (c))

static char key;

#ifdef __STDC__
static void respond(int, int);
static void getstring(char *);
static void checkbuf(unsigned);
#else
static void respond();
static void getstring();
static void checkbuf();
#endif

static void
catch(sig)
	int sig;
{
	switch (sig) {
	default:    return;
	case OPEN:  key = 'O';	break;
	case CLOSE: key = 'C';	break;
	case ERROR: key = 'E';	break;
	}
	(void) sigprocmask(SIG_SETMASK, &maskall, (sigset_t *)0);
	longjmp(sjbuf, 1);
}

main(argc, argv)
	int argc;
	char *argv[];
{
	struct sigaction sa;
	pid_t parent = getpid(), next = parent;
	register int n, i, cc, rval, saverr;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	if (argc > 1) {
		if ((debug = fopen(argv[1], "w")) == NULL)
			exit(1);
		setbuf(debug, NULL);
	}
	(void) sigemptyset(&maskall);
	(void) sigaddset(&maskall, RECV);
	(void) sigaddset(&maskall, OPEN);
	(void) sigaddset(&maskall, CLOSE);
	(void) sigaddset(&maskall, ERROR);
	(void) sigaddset(&maskall, TAPE);
	(void) sigaddset(&maskall, SEND);

	tapemask = maskall;
	(void) sigdelset(&tapemask, TAPE);

	sendmask = maskall;
	(void) sigdelset(&sendmask, SEND);

	(void) sigemptyset(&cmdmask);
	(void) sigaddset(&cmdmask, TAPE);
	(void) sigaddset(&cmdmask, SEND);

	(void) sigemptyset(&sa.sa_mask);

	sa.sa_handler = catch;
	sa.sa_flags = SA_RESTART;
	(void) sigaction(RECV, &sa, (struct sigaction *)0);
	(void) sigaction(SEND, &sa, (struct sigaction *)0);
	(void) sigaction(TAPE, &sa, (struct sigaction *)0);
	(void) sigaction(OPEN, &sa, (struct sigaction *)0);
	(void) sigaction(CLOSE, &sa, (struct sigaction *)0);
	(void) sigaction(ERROR, &sa, (struct sigaction *)0);

	(void) sigprocmask(SIG_SETMASK, &maskall, (sigset_t *)0);

	(void) kill(parent, TAPE);
	(void) kill(parent, SEND);

	while (read(0, &key, 1) == 1) {
		switch (key) {
		case 'L':		/* lseek */
			getstring(count);
			getstring(pos);
			DEBUG2("rmtd: L %s %s\n", count, pos);
			(void) kill(next, RECV);
			(void) sigsuspend(&tapemask);
			rval = (int)lseek(tape, atol(count), atoi(pos));
			saverr = errno;
			(void) kill(next, TAPE);
			(void) sigsuspend(&sendmask);
			respond(rval, saverr);
			break;

		case 'I':		/* ioctl */
		case 'i': {		/* extended version ioctl */
			int bad = 0;

			getstring(op);
			getstring(count);
			DEBUG3("rmtd: %c %s %s\n", key, op, count);
			mtop.mt_op = atoi(op);
			mtop.mt_count = atoi(count);
			if (key == 'i') {
				/*
				 * Map the supported compatibility defines
				 * into real ioctl values.
				 */
				switch (mtop.mt_op) {
				case RMTICACHE:
				case RMTINOCACHE:	/* not support on Sun */
					bad = 1;
					break;
				case RMTIRETEN:
					mtop.mt_op = MTRETEN;
					break;
				case RMTIERASE:
					mtop.mt_op = MTERASE;
					break;
				case RMTIEOM:
					mtop.mt_op = MTEOM;
					break;
				case RMTINBSF:
					mtop.mt_op = MTNBSF;
					break;
				default:
					bad = 1;
					break;
				}
			}
			if (bad) {
				respond(-1, EINVAL);
			} else {
				(void) kill(next, RECV);
				(void) sigsuspend(&tapemask);
				if (mtop.mt_op == RMTIVERSION) {
					rval = mtop.mt_count = RMT_VERSION;
				} else {
					rval = ioctl(tape, MTIOCTOP,
					    (char *)&mtop);
				}
				saverr = errno;
				(void) kill(next, TAPE);
				(void) sigsuspend(&sendmask);
				respond(rval < 0 ? rval : mtop.mt_count,
				    saverr);
			}
			break;
		}

		case 'S':		/* status */
		case 's': {		/* extended status */
			char skey;

			DEBUG1("rmtd: %c\n", key);
			if (key == 's') {
				if (read(0, &skey, 1) != 1)
					continue;
			}
			(void) kill(next, RECV);
			(void) sigsuspend(&tapemask);
			errno = 0;
			rval = ioctl(tape, MTIOCGET, (char *)&mtget);
			saverr = errno;
			(void) kill(next, TAPE);
			(void) sigsuspend(&sendmask);
			if (rval < 0)
				respond(rval, saverr);
			else {
				if (key == 's') {	/* extended status */
					DEBUG1("rmtd: s%c\n", key);
					switch (skey) {
					case MTS_TYPE:
						respond(mtget.mt_type, saverr);
						break;
					case MTS_DSREG:
						respond(mtget.mt_dsreg, saverr);
						break;
					case MTS_ERREG:
						respond(mtget.mt_erreg, saverr);
						break;
					case MTS_RESID:
						respond(mtget.mt_resid, saverr);
						break;
					case MTS_FILENO:
						respond(mtget.mt_fileno,
						    saverr);
						break;
					case MTS_BLKNO:
						respond(mtget.mt_blkno, saverr);
						break;
					case MTS_FLAGS:
						respond(mtget.mt_flags, saverr);
						break;
					case MTS_BF:
						respond(mtget.mt_bf, saverr);
						break;
					default:
						respond(-1, EINVAL);
						break;
					}
				} else {
					respond(sizeof (mtget), saverr);
					(void) write(1, (char *)&mtget,
					    sizeof (mtget));
				}
			}
			break;
		}

		case 'W':
			getstring(count);
			n = atoi(count);
			checkbuf(n);
			DEBUG1("rmtd: W %s\n", count);
#ifdef lint
			cc = 0;
#endif
			for (i = 0; i < n; i += cc) {
				cc = read(0, &record[i], n - i);
				if (cc <= 0) {
					DEBUG1(gettext("%s: premature eof\n"),
						"rmtd");
					exit(2);
				}
			}
			(void) kill(next, RECV);
			(void) sigsuspend(&tapemask);
			rval = write(tape, record, n);
			saverr = errno;
			(void) kill(next, TAPE);
			(void) sigsuspend(&sendmask);
			respond(rval, saverr);
			break;

		case 'R':
			getstring(count);
			n = atoi(count);
			checkbuf(n);
			DEBUG1("rmtd: R %s\n", count);
			(void) kill(next, RECV);
			(void) sigsuspend(&tapemask);
			rval = read(tape, record, n);
			saverr = errno;
			(void) kill(next, TAPE);
			(void) sigsuspend(&sendmask);
			respond(rval, saverr);
			(void) write(1, record, rval);
			break;

		default:
			DEBUG2(gettext("%s: garbage command '%c'\n"),
				"rmtd", key);
			/*FALLTHROUGH*/

		case 'C':
		case 'O':
			/* rendezvous back into a single process */
			if (setjmp(sjbuf) == 0 || getpid() != parent) {
				(void) sigsuspend(&tapemask);
				(void) sigsuspend(&sendmask);
				(void) kill(parent, key == 'O' ? OPEN :
					key == 'C' ? CLOSE : ERROR);
				(void) sigemptyset(&newmask);
				(void) sigsuspend(&newmask);
			}
			while (children > 0) {
				(void) kill(childpid[--children], SIGKILL);
				while (wait(NULL) != childpid[children])
					;
			}
			next = parent;
			if (key == 'C') {
				getstring(device);
				DEBUG1("rmtd: C %s\n", device);
				rval = close(tape);
				respond(rval, errno);
				(void) kill(parent, TAPE);
				(void) kill(parent, SEND);
				continue;
			}
			if (key != 'O') 		/* garbage command */
				exit(3);
			(void) close(tape);
			getstring(device);
			getstring(mode);
			DEBUG2("rmtd: O %s %s\n", device, mode);
			/*
			 * XXX [shumway]
			 * Due to incompatibilities in the
			 * assignment of mode bits between
			 * BSD and System V, we strip all
			 * but the read/write bits
			 */
			tape = open(device,
			    atoi(mode) & (O_RDONLY|O_WRONLY|O_RDWR));
			respond(tape, errno);
			if (tape >= 0)			/* fork off */
				while (children < MAXCHILD &&
					(childpid[children] = fork()) > 0)
						next = childpid[children++];
			if (next == parent) {
				(void) kill(parent, RECV);
				(void) kill(parent, TAPE);
				(void) kill(parent, SEND);
			}
			(void) sigsuspend(&cmdmask);
			continue;
		}
		(void) kill(next, SEND);
		(void) sigsuspend(&cmdmask);
	}
	(void) kill(next, RECV);
	exit(0);
#ifdef lint
	return (0);
#endif
}

static void
respond(rval, errno)
	register int rval, errno;
{
	char resp[SSIZE];
	char *errstr = strerror(errno);

	if (rval < 0) {
		(void) sprintf(resp, "E%d\n%s\n", errno, errstr);
		DEBUG2("rmtd: E %d (%s)\n", errno, errstr);
	} else {
		(void) sprintf(resp, "A%d\n", rval);
		DEBUG1("rmtd: A %d\n", rval);
	}
	(void) write(1, resp, (int)strlen(resp));
}

static void
getstring(cp)
	register char *cp;
{
	do {
		if (read(0, cp, 1) != 1)
			exit(0);
	} while (*cp++ != '\n');
	*--cp = '\0';
}

static void
checkbuf(size)
	unsigned size;
{
	if (size <= maxrecsize)
		return;
	if (record != 0)
		free(record);
	if ((record = malloc(size)) == NULL) {
		DEBUG1(gettext("%s: cannot allocate buffer space\n"), "rmtd");
		exit(4);
	}
	maxrecsize = size;
}
