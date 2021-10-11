#ident	"@(#)delete.c 1.11 93/02/25"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include <config.h>
#include "defs.h"
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifdef __STDC__
static int buildrenamefile(const char *, u_long, const char *, u_long);
static void remove_dump(const char *, u_long);
static int check_dumpdate(const char *, u_long, time_t);
static int mkhost(u_long, char *);
static int tape_update_init(void);
static int tape_update_done(void);
#else
static int buildrenamefile();
static void remove_dump();
static int check_dumpdate();
static int mkhost();
static int tape_update_init();
static int tape_update_done();
#endif

#ifdef __STDC__
delete_tape(const char *label)
#else
delete_tape(label)
	char *label;
#endif
{
	u_long recnum, firstrec, myrec;
	struct active_tape *tp;
	register int i;
	char host[128];
	int fd;
	struct stat stbuf;
	u_long lasthost;

	if (lstat(TAPEFILE, &stbuf) == -1) {
		perror("stat");
		(void) fprintf(stderr, gettext("cannot stat `%s'\n"), TAPEFILE);
		return (-1);
	}

	if ((fd = open(DELETE_TAPE, O_WRONLY|O_CREAT, 0600)) == -1) {
		perror("open");
		(void) fprintf(stderr, gettext("cannot open `%s'\n"),
			DELETE_TAPE);
		return (-1);
	}
	if (write(fd, (char *)&stbuf.st_size, sizeof (int)) != sizeof (int)) {
		perror("write");
		(void) fprintf(stderr,
			gettext("%s: tape size write error\n"), "delete_tape");
		(void) close(fd);
		(void) unlink(DELETE_TAPE);
		return (-1);
	}
	if (write(fd, label, LBLSIZE) != LBLSIZE) {
		perror("write");
		(void) fprintf(stderr, gettext(
		    "cannot write label in %s\n"), DELETE_TAPE);
		(void) close(fd);
		(void) unlink(DELETE_TAPE);
		return (-1);
	}
	(void) fsync(fd);
	(void) close(fd);
	if (tape_open("."))
		return (-1);

	if ((tp = tape_lookup(label, &recnum)) == NULL_TREC) {
		(void) unlink(DELETE_TAPE);
		tape_close(".");
		tape_trans(".");	/* removes trans and map files */
		(void) fprintf(stderr, gettext(
		    "%s: label `%s' not found\n"), "delete_tape", label);
		return (-1);
	}

	firstrec = recnum;
	lasthost = 0;
	do {
		for (i = 0; i < DUMPS_PER_TAPEREC; i++) {
			if (tp->dumps[i].host == 0)
				continue;
			if (lasthost != tp->dumps[i].host) {
				if (mkhost(tp->dumps[i].host, host)) {
					continue;
				}
				lasthost = tp->dumps[i].host;
			}
			remove_dump(host, tp->dumps[i].dump_id);
		}
		myrec = recnum;
		recnum = tp->tape_next;
		(void) tape_freerec(myrec);
		if (recnum != firstrec) {
			tp = tape_nextent(label, recnum);
			if (tp == NULL_TREC) {
				(void) unlink(DELETE_TAPE);
				tape_close(".");
				tape_trans(".");
				return (-1);
			}
		}
	} while (recnum != firstrec);
	tape_close(".");
	(void) unlink(DELETE_TAPE);
	if (tape_trans(".") != 0) {
		(void) fprintf(stderr, gettext("%s failure\n"), "tape_trans");
		return (-1);
	}
	return (0);
}

#ifdef __STDC__
scratch_tape(const char *label,
	time_t	date)
#else
scratch_tape(label, date)
	char *label;
	time_t	date;
#endif
{
	struct active_tape *tp;
	u_long firstrec, thisrec, holdrec;
	int moredumps;
	register int i;
	char host[2*BCHOSTNAMELEN];
	u_long lasthost;
#define	CHAINMAX 1000 /* XXX a tape can't have more than this many dumps */
	u_long chain[CHAINMAX];
	int chaincnt;

	/*
	 * called when the first file of a tape is added to the
	 * database.  We delete DB data for any dumps on this tape
	 * which have an earlier dump date than the one specified
	 * here.
	 */
	if (tape_update_init())
		return (-1);

	if ((tp = tape_lookup(label, &thisrec)) == NULL_TREC) {
		/*
		 * specified tape not currently in database
		 */
		(void) unlink(TAPE_UPDATE);
		tape_close(".");
		tape_trans(".");	/* removes trans and map files */
		return (0);
	}

	chaincnt = 0;
	lasthost = 0;
	firstrec = thisrec;
	do {
		moredumps = 0;
		for (i = 0; i < DUMPS_PER_TAPEREC; i++) {
			if (tp->dumps[i].host == 0)
				continue;
			if (lasthost != tp->dumps[i].host) {
				if (mkhost(tp->dumps[i].host, host))
					continue;
				lasthost = tp->dumps[i].host;
			}
			if (check_dumpdate(host,
			    tp->dumps[i].dump_id, date) == 0)
				moredumps++;
		}
		holdrec = thisrec;
		thisrec = tp->tape_next;
		if (!moredumps) {
			(void) tape_freerec(holdrec);
		} else {
			/*
			 * there are still entries in this record, so we
			 * can't free the record.  Now we must be sure to
			 * adjust the chain of records associated with this
			 * tape since some records in the chain may be freed
			 * while others may not.  Here we keep track of all
			 * records that remain in the chain for this label.
			 */
			if (chaincnt >= CHAINMAX) {
				(void) fprintf(stderr, gettext(
				    "%s: more than %d dumps!\n"), "rechain",
				    CHAINMAX);
			} else {
				chain[chaincnt++] = holdrec;
			}
		}
		if (thisrec != firstrec) {
			tp = tape_nextent(label, thisrec);
			if (tp == NULL_TREC) {
				(void) unlink(TAPE_UPDATE);
				tape_close(".");
				tape_trans(".");
				return (-1);
			}
		}
	} while (thisrec != firstrec);
	if (chaincnt) {
		/*
		 * if we got a list of records that weren't freed, chain
		 * them together now.
		 */
		tape_rechain(label, chain, chaincnt);
	}
	return (tape_update_done());
}

static void
#ifdef __STDC__
remove_dump(const char *host,
	u_long dumpid)
#else
remove_dump(host, dumpid)
	char *host;
	u_long dumpid;
#endif
{
	char filename[256];

	(void) sprintf(filename, "%s/%s.%lu", host, HEADERFILE, dumpid);
	if (unlink(filename) == -1) {
		perror("unlink");
		(void) fprintf(stderr, gettext("%s: cannot unlink `%s'\n"),
			"remove_dump", filename);
	}
	(void) sprintf(filename, "%s/%s.%lu", host, DNODEFILE, dumpid);
	if (unlink(filename) == -1) {
		perror("unlink");
		(void) fprintf(stderr, gettext("%s: cannot unlink `%s'\n"),
			"remove_dump", filename);
	}
	(void) sprintf(filename, "%s/%s.%lu", host, PATHFILE, dumpid);
	if (unlink(filename) == -1) {
		perror("unlink");
		(void) fprintf(stderr, gettext("%s: cannot unlink `%s'\n"),
			"remove_dump", filename);
	}
	(void) sprintf(filename, "%s/%s.%lu", host, LINKFILE, dumpid);
	if (unlink(filename) == -1) {
		perror("unlink");
		(void) fprintf(stderr, gettext("%s: cannot unlink `%s'\n"),
			"remove_dump", filename);
	}
	tape_remdump(dumpid);
}

static int
#ifdef __STDC__
check_dumpdate(const char *host,
	u_long dumpid,
	time_t date)
#else
check_dumpdate(host, dumpid, date)
	char *host;
	u_long dumpid;
	time_t date;
#endif
{
	char filename[256];
	int fd;
	struct dheader dh;

	(void) sprintf(filename, "%s/%s.%lu", host, HEADERFILE, dumpid);
	if ((fd = open(filename, O_RDONLY)) == -1) {
		return (0);
	}
	if (read(fd, (char *)&dh,
			sizeof (struct dheader)) != sizeof (struct dheader)) {
		(void) close(fd);
		return (0);
	}
	(void) close(fd);
	if (dh.dh_time > date) {
		return (0);
	}
	remove_dump(host, dumpid);
	return (1);
}

/*
 * build a full hostname for the given internet address.
 */
static int
mkhost(hostid, fullhost)
	u_long hostid;
	char *fullhost;
{
	struct hostent *h;
	char *name, *hid;
	struct in_addr inaddr;

	if ((h = gethostbyaddr((char *)&hostid,
			sizeof (u_long), AF_INET)) == NULL) {
		(void) fprintf(stderr, gettext(
		    "%s: cannot get host data for %lx\n"), "mkhost", hostid);
		return (-1);
	}
	if (name = strchr(h->h_name, '.'))
		*name = '\0';
	inaddr.s_addr = hostid;
	hid = inet_ntoa(inaddr);
	(void) sprintf(fullhost, "%s.%s", h->h_name, hid);
	return (0);
}

static int
#ifdef __STDC__
tape_update_init(void)
#else
tape_update_init()
#endif
{
	struct stat stbuf;
	int fd;

	if (lstat(TAPEFILE, &stbuf) == -1) {
		perror("stat");
		(void) fprintf(stderr, gettext("cannot stat `%s'\n"), TAPEFILE);
	}

	if ((fd = open(TAPE_UPDATE, O_WRONLY|O_CREAT, 0600)) == -1) {
		perror("open");
		(void) fprintf(stderr,
			gettext("cannot open `%s'\n"), TAPE_UPDATE);
		return (-1);
	}
	if (write(fd, (char *)&stbuf.st_size, sizeof (int)) != sizeof (int)) {
		perror("write");
		(void) fprintf(stderr, gettext("%s: tape size write\n"),
			"delete_tape");
		(void) close(fd);
		(void) unlink(TAPE_UPDATE);
		return (-1);
	}
	(void) fsync(fd);
	(void) close(fd);
	if (tape_open("."))
		return (-1);
	return (0);
}

static int
#ifdef __STDC__
tape_update_done(void)
#else
tape_update_done()
#endif
{
	tape_close(".");
	if (rename(TAPE_UPDATE, TAPE_UPDATEDONE) == -1) {
		perror("delete_tape/rename");
		return (-1);
	}
	if (tape_trans(".") != 0) {
		(void) fprintf(stderr, gettext("%s failure\n"), "tape_trans");
		return (-1);
	}
	(void) unlink(TAPE_UPDATEDONE);
	return (0);
}
