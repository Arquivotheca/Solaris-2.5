#ident	"@(#)tapelib.c 1.23 93/10/13"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "structs.h"
#include "tapelib.h"
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/mman.h>

static void tl_setupfile(void);
static int tl_expired(int, int, time_t);
static void tlord_init(int, time_t);
static void tlord_close();
static int tlord_getnext(int, time_t);
static int tlord_lock(void);

static char ord_filename[MAXPATHLEN];
static size_t tapelibmapsize;
static caddr_t tapelibmap;
static struct tapedesc_f *tapearr;
static int tlord_tapelibfid = -1;

/*
 * Tape library routines:
 *	tl_open(tapelibname, configname) ## configname may be NULL
 *	tl_close();
 *	tl_findfree(thiscycle, reservetime)
 *	tl_update(tapenum, expdate, expcycle)
 *	tl_write(tapenum, statbuffer)
 *	tl_read(tapenum, statbuffer)
 *	NO tl_add()
 *	tl_reserve(tapenum, howlong)
 *	tl_setstatus(tapenum, status)	## debug only??
 *	tl_error(tapenum)
 *	tl_markstatus(tapenum, status)  ## bottom bits only...
 *	tl_lock()
 *	tl_unlock()
 *
 * It is understood that this is the cheapest tape library in the world
 */

void
tl_open(char *filename, char *configfilename)
{
	char	checkline[MAXLINELEN];

	if (tapeliblen >= 0) {
		(void) printf(gettext("%s: Tape library already open\n"),
			"tl_open");
		return;
	}
	if (index(filename, '/') != (char *)0)
		die(gettext(
"Specify tape library names without the filename component (i.e., no /)\n"));
	tapelibfid = open(filename, nswitch == 0 ? 2 : 0);
	lentapelibfilesecurity = (int)strlen(tapelibfilesecurity);
	if (tapelibfid == -1) {
		tapelibfid = open(filename, O_RDWR | O_CREAT, 0660);
		if (tapelibfid < 0)
			die(gettext(
			    "%s: Could not create tape library named `%s'\n"),
			    "tl_open", filename);
		(void) printf(gettext(
		    "Creating new tape database `%s' on host `%s'\n"),
			filename, hostname);
		if (write(tapelibfid, tapelibfilesecurity,
		    lentapelibfilesecurity) != lentapelibfilesecurity)
			die(gettext(
			    "Cannot write security string to tape library\n"));
		if (nswitch) {
			if (close(tapelibfid) == -1)
				die(gettext(
			    "Tape library `%s' was not closed cleanly\n"),
					filename);
			tapelibfid = open(filename, 0);
			if (tapelibfid == -1)
				die(gettext(
			    "Cannot re-open tape library `%s' in -n mode\n"),
					filename);
		}
	}
	if (lseek(tapelibfid, (off_t) 0, 0) == -1)
		die(gettext("%s: Cannot seek in tape libarary (%d)\n"),
			"tl_open", 1);
	if (read(tapelibfid, checkline, lentapelibfilesecurity) !=
	    lentapelibfilesecurity || strncmp(checkline, tapelibfilesecurity,
	    (size_t)lentapelibfilesecurity) != 0) {
		int	n = (int)strlen(configfilesecurity);
		if (lseek(tapelibfid, (off_t) 0, 0) == -1)
			die(gettext("%s: Cannot seek in tape libarary (%d)\n"),
				"tl_open", 2);
		if (read(tapelibfid, checkline, n) == n &&
		    strncmp(checkline, configfilesecurity, (size_t)n) == 0)
			die(gettext(
"%s: You specified a dumpex configuration file (`%s') instead\n\
\tof a tape library file\n"),
				"tl_open", filename);
		die(gettext(
"%s: The tape configuration file you specified (`%s')\n\
is not a valid tape configuration file\n"),
			"tl_open", filename);
	}
	tl_setupfile();
	if (configfilename != NULL)
		(void) sprintf(ord_filename, "%s.ord", configfilename);
	else
		ord_filename[0] = '\0';
}

void
tl_close()
{
	if (tapelibfid != -1) {
		(void) close(tapelibfid);
		tapelibfid = -1;
		tapeliblen = -1;
		tlord_close();
	}
}

static void
tl_setupfile(void)
{
	struct stat statbuf;

	if (fstat(tapelibfid, &statbuf) == -1)
		die(gettext("%s: Cannot stat tape library file `%s'\n"),
			"tl_open", filename);
	tapeliblen = (statbuf.st_size - lentapelibfilesecurity) / TBUFSZ;
	if (debug) {
		(void) printf(gettext(
	"%s: opened tape library `%s' on host `%s' with %d tapes\n"),
			"tl_open", filename, hostname, tapeliblen);
	}

	/* mmap the entire file */
	tapelibmapsize = statbuf.st_size;
	tapelibmap = mmap((caddr_t)0, tapelibmapsize, nswitch ? PROT_READ :
		PROT_READ|PROT_WRITE, MAP_SHARED, tapelibfid, (off_t)0);
	if (tapelibmap == (caddr_t)-1)
		die(gettext("%s: Cannot mmap tape library file `%s'\n"),
			"tl_open", filename);
	tapearr = (struct tapedesc_f *) (tapelibmap + lentapelibfilesecurity);
}

tl_add(void)
{				/* new TL_SCRATCH entry goes into database */
	struct tapedesc_f tape;
	int	tapenumber;	/* number this tape takes on */

	tape.t_status = TL_SCRATCH;	/* new record */
	tape.t_expdate = 0;
	tape.t_expcycle = 0;

	tapenumber = tapeliblen;
	tl_write(tapenumber, &tape);
	return (tapenumber);
}

void
tl_read(tapenum, statbuffer)
	int	tapenum;		/* which tape to check */
	struct tapedesc_f *statbuffer;	/* output goes here */
{
	if (tapenum >= tapeliblen) {
		statbuffer->t_status = 0;
		statbuffer->t_expdate = 0;
		statbuffer->t_expcycle = 0;
		return;
	}
	*statbuffer = tapearr[tapenum];
}

void
tl_write(tapenum, statbuffer)
	int	tapenum;		/* which tape to check */
	struct tapedesc_f *statbuffer;	/* output goes here */
{
	if (nswitch)
		return;

	if (tapenum < tapeliblen) {
		tapearr[tapenum] = *statbuffer;
		(void) msync(tapelibmap, tapelibmapsize, MS_SYNC);
		return;
	}

	if (lseek(tapelibfid,
	    (off_t) (tapenum * TBUFSZ + lentapelibfilesecurity), 0) == -1)
		die(gettext("%s: Cannot seek in tape libarary (%d)\n"),
			"tl_write", 1);
	if (write(tapelibfid, (char *) statbuffer, TBUFSZ) != TBUFSZ)
		die(gettext("%s: Write failed\n"), "tl_write");

	/* since we extended the file, remove this mapping and get another */
	(void) munmap(tapelibmap, tapelibmapsize);
	tl_setupfile();
}

/* tl_update updates depending on former status */

void
tl_update(tapenum, expdate, expcycle)
	int	tapenum;	/* which tape we're updating */
	int	expdate;	/* new expdate: either set or choose greater */
				/* of this and that date present */
	int	expcycle;	/* new expcycle: either set or choose great */
				/* of this and that date present */
{
	struct tapedesc_f tapedesc;
	tl_lock();
	tl_read(tapenum, &tapedesc);
	if (tapedesc.t_status == TL_TMP_RESERVED ||
	    tapedesc.t_status == TL_SCRATCH) {
		tapedesc.t_status = TL_USED;
		tapedesc.t_expdate = expdate;
		tapedesc.t_expcycle = expcycle;
	} else {
		if (expdate == -1)
			tapedesc.t_expdate = -1;
		else if (tapedesc.t_expdate != -1 &&
		    expdate > tapedesc.t_expdate)
			tapedesc.t_expdate = expdate;
		if (expcycle > tapedesc.t_expcycle)
			tapedesc.t_expcycle = expcycle;
	}
	tl_write(tapenum, &tapedesc);
	tl_unlock();
}

void
tl_reserve(tapenum, howlong)
	int	tapenum;	/* which tape to reserve */
	int	howlong;	/* number of seconds to reserve it */
{
	struct tapedesc_f tapedesc;
	time_t  tloc;

	if (time(&tloc) == -1)
		die(gettext("%s: Cannot determine current time\n"),
			"tl_reserve");

	tl_read(tapenum, &tapedesc);
	tapedesc.t_status =
		(tapedesc.t_status & ~TL_STATMASK) | TL_TMP_RESERVED;
	tapedesc.t_expdate = tloc + howlong;
	tapedesc.t_expcycle = 0;
	tl_write(tapenum, &tapedesc);
}

void
tl_setstatus(tapenum, status)
	int	tapenum;	/* which tape to reserve */
	int	status;		/* new status */
{
	struct tapedesc_f tapedesc;

	tl_lock();
	tl_read(tapenum, &tapedesc);
	tapedesc.t_status = status;
	tl_write(tapenum, &tapedesc);
	tl_unlock();
}

#define	HOURS(t)  ((t) * MINUTESPERHOUR * SECONDSPERMINUTE)

void
tl_setdate(tapenum, date)
	int	tapenum;	/* which tape to reserve */
	int	date;		/* new incremental date in hours */
{
	struct tapedesc_f tapedesc;
	time_t  tloc;

	if (time(&tloc) == -1)
		die(gettext("%s: Cannot determine current time\n"),
			"tl_setdate");

	tl_lock();
	tl_read(tapenum, &tapedesc);
	tapedesc.t_expdate = tloc + HOURS(date);
	tl_write(tapenum, &tapedesc);
	tl_unlock();
}

void
tl_error(tapenum)		/* sets TL_ERRORED on tapenum */
	int	tapenum;
{
	struct tapedesc_f tapedesc;

	tl_lock();
	tl_read(tapenum, &tapedesc);
	tapedesc.t_status |= TL_ERRORED;
	tl_write(tapenum, &tapedesc);
	tl_unlock();
}

void
tl_markstatus(tapenum, status)	/* sets bottom status bits on tapenum */
	int	tapenum;
	int	status;
{
	struct tapedesc_f tapedesc;

	tl_lock();
	tl_read(tapenum, &tapedesc);
	tapedesc.t_status = (tapedesc.t_status & ~TL_STATMASK) | status;
	if (status == TL_USED)
		tapedesc.t_status |= TL_LABELED;
	tl_write(tapenum, &tapedesc);
	tl_unlock();
}

/*
 * tl_findfree uses a locking protocol
 *  -- only one entity can concurrently search the database for free tapes
 *  -- only free tape searching is protected against concurrency
 *  -- use the RESERVE feature to protect against other concurrency problems
 *
 * returns reserved applicable free tape or -1
 *
 * update this routine carefully, preserving the lock protocol
 */

tl_findfree(thiscycle, reservetime)
	int	thiscycle;
	int	reservetime;
{
	static int firsttime = 1;
	time_t  tloc;
	int	readlen;	/* probably will get short read at end of DB */
	int	tapesread;	/* how many tapes in the short read */
	int	i;

	if (time(&tloc) == -1)
		die(gettext("%s: Cannot determine current time\n"),
			"tl_findfree");

	tl_lock();

	/*
	 * First time through, we initialize the ordered tape list.
	 * We also clear any old TMP_RESERVED tapes that have expired.
	 */
	if (firsttime) {
		firsttime = 0;
		for (i = 0; i < tapeliblen; i++) {
			int stat = tapearr[i].t_status & TL_STATMASK;

			if (stat == TL_TMP_RESERVED &&
			    tapearr[i].t_expdate != -1 &&
			    tapearr[i].t_expdate <= tloc)
				tl_markstatus(i, TL_SCRATCH);
		}
		tlord_init(thiscycle, tloc);
	}

	/* first, exhaust the ordered list */
	i = tlord_getnext(thiscycle, tloc);
	if (i != -1) {
		if (reservetime)
			tl_reserve(i, reservetime);
		tl_unlock();
		return (i);
	}

	/* then, look at all the tapes for candidates */
	for (i = 0; i < tapeliblen; i++) {
		if (tl_expired(i, thiscycle, tloc)) {
			if (reservetime)
				tl_reserve(i, reservetime);
			tl_unlock();
			return (i);
		}
	}
	tl_unlock();
	return (-1);		/* no tapes were available */
}

static int
tl_expired(int tapenum, int thiscycle, time_t thistime)
{
	int stat;
	struct tapes_f *t;

	/* not a valid tape if out of our range */
	if (tapenum >= tapeliblen)
		return (0);

	/* blow off if already reserved by me: */
	for (t = tapes_head.ta_next; t != &tapes_head; t = t->ta_next)
		if (t->ta_number == tapenum)
			return (0);

	stat = tapearr[tapenum].t_status & TL_STATMASK;
	/* SCRATCH: fair game */
	if (stat == TL_SCRATCH)
		return (1);

	/* Not a known tape */
	if (stat == TL_NOSTATUS)
		return (0);

	/*
	 * by here tapestatus must be USED, FULL, or
	 * TMP_RESERVED
	 */
	if (tapearr[tapenum].t_status & TL_ERRORED ||
	    tapearr[tapenum].t_status & TL_OFFSITE)
		return (0);
	if (tapearr[tapenum].t_expcycle > 0 &&
	    tapearr[tapenum].t_expcycle >= thiscycle)
		return (0);
	/*
	 * If we are not holding the lock on the .ord (ordered tape)
	 * file and this is a TMP_RESERVED tap, bump the test time by
	 * the default reserve time.  Thus, tapes that are to expire
	 * "soon" are counted in -n mode.
	 */
	if (tlord_tapelibfid == -1 && stat == TL_TMP_RESERVED)
		thistime += RESERVETIME;
	if (tapearr[tapenum].t_expdate == -1 ||
	    tapearr[tapenum].t_expdate > thistime)
		return (0);
	return (1);
}

#define	MAXLOCKFAIL 5
#ifdef USELOCKF

void
tl_unlock(void)
{
	if (lseek(tapelibfid, (off_t) 0, 0) == -1) /* lockf uses offset... */
		die(gettext("%s: Cannot seek in tape library\n"), "tl_unlock");
	if (lockf(tapelibfid, F_ULOCK, (long) 0) == -1)
		die(gettext("%s: lockf(unlock) failed, errno=%d\n"),
			"tl_unlock", errno);
}

void
tl_lock(void)
{
	int	nfails;

	if (lseek(tapelibfid, (off_t) 0, 0) == -1) /* lockf uses offset... */
		die(gettext("%s: Cannot seek in tape library\n"), "tl_lock");

	for (nfails = 0; nfails < MAXLOCKFAIL; nfails++) {
		if (lockf(tapelibfid, F_TLOCK, 0L) == 0)
			return;
		if (errno == EACCES || errno == EAGAIN) {
			if (debug)
				(void) printf(gettext(
	    "%s[pid=%lu]: sleeping 1 second while waiting for lock\n"),
					"tl_lock", (u_long)getpid());
			(void) sleep(1);
		} else
			die(gettext("%s: lockf(lock) failed, errno=%d\n"),
				"tl_lock", errno);
	}
	die(gettext("%s: tape library file is locked.  Try again later.\n"),
		"tl_lock");
}

#else

void
tl_unlock(void)
{
	if (flock(tapelibfid, LOCK_UN) == -1)
		die(gettext("%s: flock(unlock) failed, errno=%d\n"),
			"tl_unlock", errno);
}

void
tl_lock(void)
{
	int	nfails;
	for (nfails = 0; nfails < MAXLOCKFAIL; nfails++) {
		if (flock(tapelibfid,
		    (nswitch ? LOCK_SH : LOCK_EX) | LOCK_NB) == 0)
			return;
		if (errno == EWOULDBLOCK) {
			if (debug)
				(void) printf(gettext(
	    "%s[pid=%lu]: sleeping 1 second while waiting for lock\n"),
					"tl_lock", (u_long)getpid());
			(void) sleep(1);
		} else
			die(gettext("%s: flock(lock) failed, errno=%d\n"),
				"tl_lock", errno);
	}
	die(gettext("%s: tape library file is locked.  Try again later.\n"),
		"tl_lock");
}

/*
 * Locking the ordered tape library file is special, since we use a
 * different algorithm if we could not lock the file.  This is so
 * "dumpex -n" while another dumpex is in progress (with reserved tapes)
 * does not change the order of the tapes behind our backs.
 *
 * Returns 0 if we got the lock, -1 if we did not.
 */
static int
tlord_lock(void)
{
	if (tlord_tapelibfid == -1)
		return (-1);

	return (flock(tlord_tapelibfid, LOCK_EX | LOCK_NB));
}
#endif

/*
 * Support for ordered tape expiration
 */
#define	MAXORD	100			/* at most this many ordered tapes */
static int ord[MAXORD];			/* saved here */
static int curord;			/* current pointer into the array */
static int nord;			/* total number in the array */
static FILE *tlord_fp;			/* open ordered tape library file */

/*
 * Initialize the ordered list of tapes.
 * If the file does not already exist, create and initialize it.
 */
static void
tlord_init(int thiscycle, time_t thistime)
{
	FILE *tlord_fp;
	int i, j, dowrite;

	curord = nord = 0;
	tlord_fp = (FILE *) NULL;

	/*
	 * if the ord_filename isn't set, return right away -- all other
	 * tlord_* funtions will noop.  this may happen if tl_open() was
	 * called with a NULL pointer in the dumpconfig file slot, which
	 * would happen in `dumptm -L <lib> ...', etc.  no harm done, since
	 * only dumpex cares about this anyway, and it always passes the
	 * config file to tl_open().
	 */

	if (ord_filename[0] == '\0')
		return;

	if ((tlord_fp = fopen(ord_filename, "r+")) == NULL) {
		if ((tlord_fp = fopen(ord_filename, "w+")) == NULL)
			return;
		/* lock it; check for race */
		tlord_tapelibfid = fileno(tlord_fp);
		if (tlord_lock() == -1)
			tlord_tapelibfid = -1;
		/* only register labeled tapes */
		for (i = 0; i < tapeliblen; i++) {
			if ((tapearr[i].t_status & TL_LABELED) == 0)
				continue;
			if (tl_expired(i, thiscycle, thistime)) {
				/* only write if we have the lock */
				if (tlord_tapelibfid != -1)
					(void) fprintf(tlord_fp, "%d\n", i);
				ord[nord++] = i;
				if (nord >= MAXORD)
					break;
			}
		}
		(void) fsync(fileno(tlord_fp));
		/* we don't close the file here, so that the lock is held */
		return;
	}
	tlord_tapelibfid = fileno(tlord_fp);
	if (tlord_lock() == -1)
		tlord_tapelibfid = -1;
	dowrite = 0;
	while (fscanf(tlord_fp, "%d", &i) != EOF) {
		if (i < 0 || i >= tapeliblen ||
		    (tapearr[i].t_status & TL_LABELED) == 0) {
			dowrite = 1;
			continue;
		}
		if (tl_expired(i, thiscycle, thistime)) {
			ord[nord++] = i;
			if (nord >= MAXORD)
				break;
		} else
			dowrite = 1;
	}
	/* again, don't close the file, so that the lock is held */

	/* final pass through the current tapes to find newly expired ones */
	/* only register labeled tapes */
	for (i = 0; i < tapeliblen; i++) {
		for (j = 0; j < nord; j++)
			if (i == ord[j])
				break;
		if (j != nord)
			continue;
		if ((tapearr[i].t_status & TL_LABELED) == 0)
			continue;
		if (tl_expired(i, thiscycle, thistime)) {
			dowrite = 1;
			ord[nord++] = i;
			if (nord >= MAXORD)
				break;
		}
	}

	/* finally, write out the new ordered list, if necessary */
	if (dowrite && tlord_tapelibfid != -1) {
		FILE *outfp;

		if ((outfp = fopen(ord_filename, "w+")) == NULL)
			return;
		for (i = 0; i < nord; i++)
			(void) fprintf(outfp, "%d\n", ord[i]);
		(void) fsync(fileno(outfp));
		(void) fclose(outfp);
	}
}

static void
tlord_close()
{
	tlord_tapelibfid = -1;
	if (tlord_fp != (FILE *) NULL) {
		(void) fclose(tlord_fp);
		tlord_fp = (FILE *) NULL;
	}
	curord = nord = 0;
}

/*
 * Grab the next entry from the ordered list of tapes and return it,
 * only if it is indeed an expired or available tape
 */
static int
tlord_getnext(int thiscycle, time_t thistime)
{
	while (curord < nord) {
		if (tl_expired(ord[curord], thiscycle, thistime))
			return (ord[curord++]);
		curord++;
	}
	return (-1);
}
