
#ident	"@(#)dumpadd.c 1.14 93/06/25"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

/*
 * Add database entries from existing dumps
 */

#include "defs.h"
#include <sys/stat.h>
#include <sys/mkdev.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>
#include <byteorder.h>

static int newvolume, fulltape;
static struct byteorder_ctx *byteorder = NULL;
static daddr_t tapestart;
extern int t_stdio_remote;

#ifdef __STDC__
static void hostprompt(char **);
static void devprompt(int, char **);
static void alloc_tapebuf(int);
static void free_tapebuf(void);
static void open_tape(const char *);
static void close_tape(void);
static int rewind_tape(void);
static void space_tape(void);
static void free_tapedata(void);
static void dumptape(const char *, const char *, int);
static void error_exit(void);
static void nextvol(void);
static void read_header(int, int);
static char *prompt(int);
static void read_data(FILE *);
static void garbage(void);
static void dirdata(struct s_spcl *, FILE *);
static void linkdata(struct s_spcl *, FILE *);
static int addentries(FILE *);
static void taperead(char *);
static int dnodedata(struct s_spcl *, FILE *);
static void dnode_span(FILE *);
static void outputtapes(FILE *);
#else
static void hostprompt();
static void devprompt();
static void alloc_tapebuf();
static void free_tapebuf();
static void open_tape();
static void close_tape();
static int rewind_tape();
static void space_tape();
static void free_tapedata();
static void dumptape();
static void error_exit();
static void nextvol();
static void read_header();
static char *prompt();
static void read_data();
static void garbage();
static void dirdata();
static void linkdata();
static int addentries();
static void taperead();
static int dnodedata();
static void dnode_span();
static void outputtapes();
#endif

/*
 * add an entire tape's worth of dumps to the database.
 */
void
#ifdef __STDC__
tapeadd(char *dbhost,
	char *dumpdev,
	const char *tdir,
	int tpbsize)
#else
tapeadd(dbhost, dumpdev, tdir, tpbsize)
	char *dbhost;
	char *dumpdev;
	char *tdir;
	int tpbsize;
#endif
{
	int num;
	char buf[256];
	char *yes = gettext("yY");

	if (dbhost == NULL) {
		hostprompt(&dbhost);
	}
	devprompt(1, &dumpdev);
	alloc_tapebuf(tpbsize);
	open_tape(dumpdev);
	if (rewind_tape())
		exit(1);
	fulltape = 1;
	num = 1;
	/*CONSTCOND*/
	while (1) {	/* XXX: while dumps on tape */
		(void) printf(gettext("File %d:\n"), num);
		dumptape(dbhost, tdir, num);
		if (newvolume) {
			newvolume = 0;
			num = 1;
			(void) fprintf(stderr, gettext(
				"Continue adding dumps from new volume? "));
			if (gets(buf) == NULL ||
					(buf[0] != yes[0] && buf[0] != yes[1]))
				break;
		}
		space_tape();
		num++;
	}
	close_tape();
	free_tapebuf();
}

void
#ifdef __STDC__
dumpadd(char *dbhost,
	char *dumpdev,
	const char *tdir,
	int tpbsize)
#else
dumpadd(dbhost, dumpdev, tdir, tpbsize)
	char *dbhost;
	char *dumpdev;
	char *tdir;
	int tpbsize;
#endif
{
	if (dbhost == NULL) {
		hostprompt(&dbhost);
	}
	if (dumpdev == NULL) {
		devprompt(0, &dumpdev);
	}
	alloc_tapebuf(tpbsize);
	open_tape(dumpdev);
	dumptape(dbhost, tdir, -1);
	space_tape();
	close_tape();
	free_tapebuf();
}

static void
hostprompt(host)
	char **host;
{
	static char dbhost[256];

	for (;;) {
		(void) fputs(gettext(
		    "Name of database server where dump data will be added: "),
		    stderr);
		if (gets(dbhost) == NULL)
			exit(1);
		if (gethostbyname(dbhost))
			break;
		(void) fprintf(stderr, gettext("Unknown host: `%s'\n"), dbhost);
	}
	*host = dbhost;
}

static void
devprompt(fulltape, dev)
	int fulltape;
	char **dev;
{
	static char dumpfile[256];
	struct stat stbuf;

again:
	if (*dev == NULL) {
		(void) fputs(gettext(
			"Device from which to read dump: "), stderr);
		if (gets(dumpfile) == NULL)
			exit(1);
		*dev = dumpfile;
	}
	if (strchr(*dev, ':')) /* t_stdio_remote not set yet! */
		return;
	else if (fulltape) {
		if (stat(*dev, &stbuf) == -1) {
			perror(*dev);
			*dev = NULL;
			goto again;
		}
		if ((!S_ISBLK(stbuf.st_mode) && !S_ISCHR(stbuf.st_mode)) ||
				(minor(stbuf.st_rdev) & MT_NOREWIND) == 0) {
			(void) fprintf(stderr, gettext(
				"You must specify a no-rewind tape device\n"));
			*dev = NULL;
			goto again;
		}
	}
}

/*
 * rebuild database entries for a dump.  A dump has the following format:
 *
 *    header
 *    maps
 *    directory inode
 *	directory data
 *	directory data
 *		.
 *		.
 *    directory inode
 *	directory data
 *		.
 *		.
 *		.
 *    file inode
 *	file data
 *    file inode
 *		.
 *		.
 *		.
 *
 * We take this data and turn it into a batch_update file as defined
 * in "batchfile.h".  This file is then processed by the database
 * server...
 */

/*
 * XXX: make sure everything gets re-initialized for multiple
 * invocations.
 */
static struct dheader dumphead;
static struct tape_data {
	struct bu_tape bu_tape;
	struct tape_data *nxt;
} *tapes, *curtape;
static int dnodecnt, namecnt, tapecnt;
static int firstinode = -1;
static u_long volnum;

/*
 * stream pointer to the dump or tape file to be
 * added to the database. Reading an Exabyte
 * drive using 5.0 stdio package fail because
 * the read gets done using sub-record size reads.
 * All operations on tfp must be done using
 * stdio routines prepended with t_ found in the
 * file t_stdio.c. This package is a simple
 * buffering package that maintains semantics with
 * stdio, but is by no means complete.
 */
static FILE *tfp;

FILE *t_fopen(const char *, const char *);
int t_setvbuf(FILE *, char *, int, size_t);
int t_fclose(FILE *);
int t_fileno(FILE *);
void t_rewind(FILE *);
int t_feof(FILE *);
size_t t_fread(void *, size_t, size_t, FILE *);

static char *prompt();
static char *tapedev;
static char *outdir = "/tmp";
static char *tfilename = "taperebuild";
static char outfile[MAXPATHLEN];

/*
 * buffer that we read into.  This is allocated by alloc_tapebuf().
 * User may over-ride the 63KB default size.
 * Note that 1/4" cartridge tapes may not behave correctly
 * when a read size of > 63K is specified.
 */
#define	DEFBSIZE	63
static int  bufsize;
static char *mybuf;

static int volchange;

#define	MNTPOINT	1
#define	TAPELABEL	2

static void
alloc_tapebuf(bsiz)
	int bsiz;
{
	if (!bsiz)
		bsiz = DEFBSIZE;
	bufsize = bsiz*TP_BSIZE;
	mybuf = malloc((unsigned)bufsize);
	if (mybuf == NULL) {
		(void) fprintf(stderr,
			gettext("%s: out of memory\n"), "alloc_tapebuf");
		exit(1);
	}
	if ((byteorder == NULL) &&
	    ((byteorder = byteorder_create()) == NULL)) {
		(void) fprintf(stderr,
			gettext("Cannot allocate byte order status\n"));
		exit(1);
	}
}

static void
#ifdef __STDC__
free_tapebuf(void)
#else
free_tapebuf()
#endif
{
	free(mybuf);
	byteorder_destroy(byteorder);
	byteorder = NULL;
}

static void
#ifdef __STDC__
open_tape(const char *dev)
#else
open_tape(dev)
	char *dev;
#endif
{
	if ((tfp = t_fopen(dev, "r")) == NULL) {
		(void) fprintf(stderr, gettext("cannot open `%s'\n"), dev);
		exit(1);
	}
	tapedev = (char *)dev;

	if (t_setvbuf(tfp, mybuf, _IOFBF, bufsize) != 0) {
		(void) fprintf(stderr, gettext("cannot setvbuf `%s'\n"), dev);
		exit(1);
	}
}

static void
#ifdef __STDC__
close_tape(void)
#else
close_tape()
#endif
{
	if (fulltape)
		(void) rewind_tape();
	(void) t_fclose(tfp);
}

static int
#ifdef __STDC__
rewind_tape(void)
#else
rewind_tape()
#endif
{
	struct mtop tcom;

	if (t_stdio_remote) {
		if (rmtioctl(MTREW, 1) != 1) {
			perror("rewind");
			return (1);
		}
	} else {
		tcom.mt_op = MTREW;
		tcom.mt_count = 0;
		if (ioctl(t_fileno(tfp), (int)MTIOCTOP, (char *)&tcom) < 0) {
			perror("rewind");
			return (1);
		}
	}
	return (0);
}

static void
#ifdef __STDC__
space_tape(void)
#else
space_tape()
#endif
{
	struct mtop tcom;

	if (t_stdio_remote) {
		if (rmtioctl(MTFSF, 1) != 1) {
			perror("ioctl MTFSF");
			exit(1);
		}
	} else {
		tcom.mt_op = MTFSF;
		tcom.mt_count = 1;
		if (ioctl(t_fileno(tfp), (int)MTIOCTOP, (char *)&tcom) < 0) {
			perror("ioctl MTFSF");
			exit(1);
		}
	}
	t_rewind(tfp);
}

static void
#ifdef __STDC__
free_tapedata(void)
#else
free_tapedata()
#endif
{
	register struct tape_data *t, *tt;

	t = tapes;
	while (t) {
		tt = t;
		t = t->nxt;
		free((char *)tt);
	}
	tapes = curtape = (struct tape_data *)0;
}

static void
#ifdef __STDC__
dumptape(const char *dbhost,
	const char *tdir,
	int filenum)
#else
dumptape(dbhost, tdir, filenum)
	char *dbhost;
	char *tdir;
	int filenum;
#endif
{
	FILE *outfp;
	struct bu_header buh;
	int tapepos = 1;
	int handle;
	char response[256];
	unsigned sleeptime;
	int doprompts = 0;

	volchange = dnodecnt = namecnt = tapecnt = volnum = 0;
	firstinode = -1;
	free_tapedata();

	if (filenum == -1) {
		doprompts = 1;
		(void) fprintf(stderr, gettext(
			"Defaulting to file 1 of the tape. OK? (yes or no) "));
		if (gets(response) == NULL)
			exit(1);
		else if (strcmp(response, gettext("yes"))) {
			for (;;) {
				(void) fprintf(stderr, gettext(
					"Enter desired file number: "));
				if (gets(response) == NULL)
					exit(1);
				if (sscanf(response, "%d", &tapepos) != 1)
					exit(1);
				if (tapepos >= 1 && tapepos < 500) {
					(void) fprintf(stderr, gettext(
						"Using file number %d\n"),
						tapepos);
					break;
				} else {
					(void) fprintf(stderr, gettext(
					    "ridiculous file number %d\n"),
						tapepos);
				}
			}
		}
	} else {
		tapepos = filenum;
	}

	if (tdir)
		outdir = (char *)tdir;
	(void) sprintf(outfile, "%s/%s.%lu",
		outdir, tfilename, (u_long)getpid());
	if ((outfp = fopen(outfile, "w")) == NULL) {
		(void) fprintf(stderr, gettext("cannot open `%s'\n"), outfile);
		exit(1);
	}
	read_header(tapepos, doprompts);
	buh.name_cnt = 0;
	buh.dnode_cnt = 0;
	buh.tape_cnt = 0;
	/*
	 * write dummy batchfile header - we update this later when
	 * we know what the values should be.
	 */
	if (fwrite((char *)&buh, sizeof (struct bu_header), 1, outfp) != 1) {
		(void) fprintf(stderr,
			gettext("cannot write update file header\n"));
		(void) fclose(outfp);
		(void) unlink(outfile);
		exit(1);
	}
	/* write dump header */
	if (fwrite((char *)&dumphead, sizeof (struct dheader), 1, outfp) != 1) {
		(void) fprintf(stderr, gettext("cannot write dump header\n"));
		(void) fclose(outfp);
		(void) unlink(outfile);
		exit(1);
	}
	/* write filename and dnode data */
	read_data(outfp);
	/* write tape data */
	outputtapes(outfp);
	/* write updated batchfile header at the end */
	buh.name_cnt = namecnt;
	buh.dnode_cnt = dnodecnt;
	buh.tape_cnt = tapecnt;

	if (namecnt == 0 || dnodecnt == 0 || tapecnt == 0) {
#if 0
		(void) fprintf(stderr, gettext("no data read?\n"));
		(void) fclose(outfp);
		(void) unlink(outfile);
		exit(1);
#else
		(void) fprintf(stderr, gettext("empty dump\n"));
#endif
	}
	if (fwrite((char *)&buh, sizeof (struct bu_header), 1, outfp) != 1) {
		(void) fprintf(stderr,
			gettext("cannot write update file header\n"));
		(void) fclose(outfp);
		(void) unlink(outfile);
		exit(1);
	}
	/* and rewrite the one at the beginning */
	rewind(outfp);
	if (fwrite((char *)&buh, sizeof (struct bu_header), 1, outfp) != 1) {
		(void) fprintf(stderr,
			gettext("cannot write update file header\n"));
		exit(1);
	}
	(void) fclose(outfp);

	/*
	 * send update file to database.
	 */
	sleeptime = 0;
	/*CONSTCOND*/
	while (1) {
		if (sleeptime) {
			(void) sleep(sleeptime);
			(void) fprintf(stderr,
			    gettext("retrying database update at `%s'\n"),
				dbhost);
		}
		if ((handle = update_start(dbhost, dumphead.dh_host)) == -1) {
			(void) fprintf(stderr, gettext(
			    "cannot start update at database server `%s'\n"),
				dbhost);
		} else if (update_data(handle, outfile)) {
			(void) fprintf(stderr, gettext(
		    "cannot send update file to database server `%s'\n"),
				dbhost);
		} else if (update_process(handle)) {
			(void) fprintf(stderr, gettext(
			    "database server `%s' cannot process update\n"),
				dbhost);
		} else {
			(void) fprintf(stderr, gettext(
		    "update file transmitted to database server `%s'\n"),
				dbhost);
			break;
		}
		sleeptime += 5;
	}
	(void) unlink(outfile);
}

static void
#ifdef __STDC__
error_exit(void)
#else
error_exit()
#endif
{
	(void) unlink(outfile);
	close_tape();
	exit(1);
}

static void
#ifdef __STDC__
nextvol(void)
#else
nextvol()
#endif
{
	char buffer[TP_BSIZE];
	char response[100];
	struct s_spcl *p;
	struct tape_data *tp;
	char *pp;

	close_tape();
	(void) fprintf(stderr, gettext("Mount next volume (yes or no): "));
	if (gets(response) == NULL)
		error_exit();
	else if (strcmp(response, gettext("yes")))
		error_exit();

	open_tape(tapedev);
	if (t_fread(buffer, TP_BSIZE, 1, tfp) != 1) {
		(void) fprintf(stderr,
			gettext("%s: cannot read header\n"), "nextvol");
		error_exit();
	}
	/*LINTED [buffer properly aligned]*/
	p = (struct s_spcl *)buffer;
	if ((normspcl(byteorder, p, (int *) p, NFS_MAGIC) != 0) ||
	    (p->c_type != TS_TAPE)) {
		(void) fprintf(stderr,
			gettext("%s: not in dump format!\n"), "nextvol");
		error_exit();
	}
	volnum++;
	if (p->c_volume != volnum) {
		(void) fprintf(stderr, gettext(
			"expected volume %lu, got %lu\n"), volnum, p->c_volume);
		error_exit();
	}

	if (p->c_date != dumphead.dh_time) {
		(void) fprintf(stderr, gettext("first volume had dumpdate %s"),
			lctime(&dumphead.dh_time));
		(void) fprintf(stderr, gettext("this volume has dumpdate %s"),
			lctime(&p->c_date));
		error_exit();
	}

	tp = (struct tape_data *)malloc(sizeof (struct tape_data));
	if (tp == (struct tape_data *)0) {
		(void) fprintf(stderr,
			gettext("%s: out of memory\n"), "nextvol");
		error_exit();
	}
	tapestart = p->c_tapea;
	tp->nxt = (struct tape_data *)0;
	tp->bu_tape.first_inode = tp->bu_tape.last_inode = 0;
	tp->bu_tape.filenum = 1; /* XXX */
	curtape->nxt = tp;
	if (strcmp(p->c_label, "none") == 0) {
		pp = prompt(TAPELABEL);
		if (pp)
			(void) bcopy(pp, tp->bu_tape.label, LBLSIZE);
		else
			(void) bcopy(p->c_label, tp->bu_tape.label, LBLSIZE);
	} else {
		(void) bcopy(p->c_label, tp->bu_tape.label, LBLSIZE);
	}
	tapecnt++;
	curtape = tp;
	volchange = 1;
	newvolume = 1;
}

static void
read_header(tapepos, doprompt)
	int tapepos;
	int doprompt;
{
	char buffer[TP_BSIZE];
	struct s_spcl *p;
	struct hostent *hp;
	char *tp;

	if (t_fread(buffer, TP_BSIZE, 1, tfp) != 1) {
		if (t_feof(tfp)) {
			(void) fprintf(stderr,
				gettext("no more dumps on this media\n"));
		} else {
			(void) fprintf(stderr,
				gettext("cannot read dump header\n"));
		}
		error_exit();
	}

	/*LINTED [buffer properly aligned]*/
	p = (struct s_spcl *)buffer;
	if ((normspcl(byteorder, p, (int *) p, NFS_MAGIC) != 0) ||
	    (p->c_type != TS_TAPE)) {
		(void) fprintf(stderr,
			gettext("%s: not in dump format!\n"), "read_header");
		error_exit();
	}
	if (p->c_volume != 1) {
		(void) fprintf(stderr, gettext(
			"%s: not first volume of dump\n"), "read_header");
		error_exit();
	}

	volnum = p->c_volume;

	(void) strcpy(dumphead.dh_host, p->c_host);
	if ((hp = gethostbyname(p->c_host)) == NULL) {
		(void) fprintf(stderr,
			gettext("cannot get host `%s'\n"), p->c_host);
		error_exit();
	}
	dumphead.dh_netid = **(u_long **)(hp->h_addr_list);
	(void) strcpy(dumphead.dh_dev, p->c_dev);
	dumphead.dh_time = p->c_date;
	dumphead.dh_prvdumptime = p->c_ddate;
	dumphead.dh_level = p->c_level;
	dumphead.dh_flags = 0;
	if (p->c_flags & DR_REDUMP)
		dumphead.dh_flags |= DH_ACTIVE;
	if (p->c_flags & DR_TRUEINC)
		dumphead.dh_flags |= DH_TRUEINC;
	if (strstr(p->c_filesys, "a partial"))
		dumphead.dh_flags |= DH_PARTIAL;
	dumphead.dh_position = tapepos;
	dumphead.dh_ntapes = 1;
	(void) bcopy(p->c_label, dumphead.dh_label[0], LBLSIZE);

	if (strstr(p->c_filesys, "file system")) {
		(void) strcpy(dumphead.dh_mnt, "/");
	} else {
		(void) strcpy(dumphead.dh_mnt, p->c_filesys);
	}
	if (doprompt || (strcmp(p->c_label, "none") == 0)) {
		tp = prompt(MNTPOINT);
		if (tp)
			(void) strcpy(dumphead.dh_mnt, tp);
		else
			(void) strcpy(dumphead.dh_mnt, "/");
	}

	/*
	 * first tape record
	 */
	tapes = (struct tape_data *)malloc(sizeof (struct tape_data));
	if (tapes == (struct tape_data *)0) {
		(void) fprintf(stderr,
			gettext("%s: out of memory\n"), "read_header");
		error_exit();
	}
	tapes->nxt = (struct tape_data *)0;
	tapes->bu_tape.first_inode = tapes->bu_tape.last_inode = 0;
	tapes->bu_tape.filenum = tapepos;
	if (strcmp(p->c_label, "none") == 0) {
		tp = prompt(TAPELABEL);
		if (tp)
			(void) bcopy(tp, tapes->bu_tape.label, LBLSIZE);
		else
			(void) bcopy(p->c_label, tapes->bu_tape.label, LBLSIZE);
	} else {
		(void) bcopy(p->c_label, tapes->bu_tape.label, LBLSIZE);
	}
	tapecnt = 1;
	curtape = tapes;
	(void) printf(gettext("level %lu dump of %s:%s on %s"),
		dumphead.dh_level,
		dumphead.dh_host,
		dumphead.dh_mnt,
		lctime(&dumphead.dh_time));
}

static char *
prompt(msg)
	int msg;
{
	static char buffer[256];

	for (;;) {
		switch (msg) {
		case MNTPOINT:
			(void) fprintf(stderr, gettext(
				"Specify the `real' mount point of this\n\
\tfilesystem -- no symlinks! (default `%s'): "),
				dumphead.dh_mnt);
			break;
		case TAPELABEL:
			(void) bzero(buffer, LBLSIZE);
			(void) fprintf(stderr, gettext(
				"Specify tape label for tape mount prompts: "));
			break;
		}
		if (gets(buffer) == NULL) {
			exit(1);
		} else if (buffer[0] == '\0') {
			if (msg == MNTPOINT)
				return (dumphead.dh_mnt);
		} else if (msg == MNTPOINT && buffer[0] != '/') {
			(void) fprintf(stderr, gettext(
				"mount point must begin with '/'\n\n"));
		} else {
			return (buffer);
		}
	}
}

static void
read_data(fp)
	FILE *fp;
{
	char buffer[TP_BSIZE];
	struct s_spcl *p;
	mode_t mode;
	int doingdir;
	register int i;

	doingdir = 0;
	/*LINTED [buffer properly aligned]*/
	p = (struct s_spcl *)buffer;
	p->c_type = TS_BITS;  /* anything but end */
	while (p->c_type != TS_END) {
		int skipping = 0;

		taperead(buffer);
		if (normspcl(byteorder, p, (int *) p, NFS_MAGIC) != 0) {
			if (! skipping)
				(void) fprintf(stderr,
	    gettext("Dump format error: Attempting to resync: skipping..."));
			(void) fflush(stderr);
			skipping = 1;
			continue;
		} else if (skipping)
			(void) fprintf(stderr, gettext(" done.\n"));
		skipping = 0;

		switch (p->c_type) {
			case TS_INODE:
				volchange = 0;
				if (firstinode == -1) {
					firstinode = p->c_inumber;
					curtape->bu_tape.first_inode =
							firstinode;
				}
				curtape->bu_tape.last_inode = p->c_inumber;
				doingdir = 0;
				mode = p->c_dinode.di_mode;
				if (S_ISDIR(mode)) {
					doingdir = 1;
					dirdata(p, fp);
				} else if (S_ISLNK(mode)) {
					linkdata(p, fp);
				} else {
					(void) dnodedata(p, fp);
					for (i = 0; i < p->c_count; i++)
						if (p->c_addr[i])
							garbage();
					if (volchange)
						dnode_span(fp);
				}
				break;
			case TS_ADDR:
				volchange = 0;
				for (i = 0; i < p->c_count; i++) {
					if (p->c_addr[i]) {
						if (doingdir)
							(void) addentries(fp);
						else
							garbage();
					}
				}
				if (volchange) {
					curtape->bu_tape.first_inode =
						p->c_inumber;
					dnode_span(fp);
				}
				curtape->bu_tape.last_inode = p->c_inumber;
				break;
			case TS_BITS:
			case TS_CLRI:
				doingdir = 0;
				for (i = 0; i < p->c_count; i++)
					/*
					 * XXX: does c_addr map these??
					 */
					garbage();
				break;
			case TS_END:
				doingdir = 0;
				break;
			default:
				(void) fprintf(stderr,
				    gettext("unknown record type %ld\n"),
					p->c_type);
				break;
		}
	}
}

static void
#ifdef __STDC__
garbage(void)
#else
garbage()
#endif
{
	char buffer[TP_BSIZE];

	taperead(buffer);
}

static void
dirdata(p, fp)
	struct s_spcl *p; /* already passed through normspcl() */
	FILE *fp;
{
	struct bu_name bun;
	register int i;

	bun.inode = p->c_inumber;
	bun.type = DIRECTORY;
	bun.namelen = 0;
	if (fwrite((char *)&bun, sizeof (struct bu_name), 1, fp) != 1) {
		(void) fprintf(stderr,
			gettext("%s: cannot write directory\n"), "dirdata");
		error_exit();
	}

	(void) dnodedata(p, fp);
	namecnt++;
	for (i = 0; i < p->c_count; i++) {
		if (p->c_addr[i])
			(void) addentries(fp);
	}
}

static int linklen;	/* used in linkdata() and dnodedata() */

void
linkdata(p, fp)
	struct s_spcl *p; /* already passed through normspcl() */
	FILE *fp;
{
	char buffer[TP_BSIZE];
	register int i;

	/*
	 * A zero-length symlink does not put any link data into the database
	 * temporary file, for compatibility with the XDR routines and the
	 * "dump" command.  Nor does it bother to read any data from the tape.
	 */
	if (p->c_dinode.di_size <= 0) {
		linklen = 1;
		(void) dnodedata(p, fp);
		return;
	}

	if (p->c_addr[0] == 0) {
		(void) fprintf(stderr,
			gettext("%s: c_addr error?\n"), "linkdata");
	}
	taperead(buffer);
	linklen = strlen(buffer)+1;
	(void) dnodedata(p, fp);
	if (fwrite((char *)buffer, linklen, 1, fp) != 1) {
		(void) fprintf(stderr, gettext("cannot write link data!\n"));
		error_exit();
	}

	for (i = 1; i < p->c_count; i++) {
		if (p->c_addr[i])
			garbage();
	}
}

addentries(fp)
	FILE *fp;
{
	struct bu_name bun;
	struct direct *d, *end;
	char buffer[TP_BSIZE];

	/*
	 * next block read from tape should contain directory data
	 */
	taperead(buffer);
	/*LINTED [buffer properly aligned]*/
	d = (struct direct *)buffer;
	/*LINTED [buffer properly aligned]*/
	end = (struct direct *)(buffer+TP_BSIZE);
	while (d < end) {
		normdirect(byteorder, d);
		if (d->d_reclen == 0)
			break;
		if (d->d_ino && strcmp(d->d_name, ".") &&
				strcmp(d->d_name, "..")) {
			bun.inode = d->d_ino;
			bun.namelen = d->d_namlen+1;
			bun.type = 0;
			if (fwrite((char *)&bun,
					sizeof (struct bu_name), 1, fp) != 1) {
				(void) fprintf(stderr,
				    gettext("%s: %s error\n"),
					"addentries", "fwrite1");
				return (-1);
			}
			if (fwrite((char *)d->d_name, (int)bun.namelen,
							1, fp) != 1) {
				(void) fprintf(stderr,
				    gettext("%s: %s error\n"),
					"addentries", "fwrite2");
				return (-1);
			}
			namecnt++;
		}
		d = (struct direct *)((unsigned)d+d->d_reclen);
	}
	return (0);
}

void
taperead(buffer)
	char *buffer;
{
	if (t_fread(buffer, TP_BSIZE, 1, tfp) != 1) {
		if (t_feof(tfp)) {
			nextvol();
			if (t_fread(buffer, TP_BSIZE, 1, tfp) != 1) {
				(void) fprintf(stderr, gettext(
				    "%s: %s error\n"), "taperead", "fread");
				error_exit();
			}
			firstinode = -1;
		} else {
			(void) fprintf(stderr, gettext("%s: %s error\n"),
				"taperead", "fread");
			error_exit();
		}
	}
}

static struct dnode lastdnode;

dnodedata(p, fp)
	struct s_spcl *p; /* already passed through normspcl() */
	FILE *fp;
{
	struct dinode *ip;
	mode_t mode;

	ip = &p->c_dinode;
	lastdnode.dn_mode = ip->di_mode;
	lastdnode.dn_uid = ip->di_uid;
	lastdnode.dn_gid = ip->di_gid;
	mode = p->c_dinode.di_mode;
	if (S_ISBLK(mode) || S_ISCHR(mode)) {
		lastdnode.dn_size = p->c_dinode.di_rdev;
	} else {
		lastdnode.dn_size = p->c_dinode.di_size;
	}
	lastdnode.dn_atime = ip->di_atime;
	lastdnode.dn_mtime = ip->di_mtime;
	lastdnode.dn_ctime = ip->di_ctime;
	if (S_ISLNK(mode))
		lastdnode.dn_symlink = linklen;
	else
		lastdnode.dn_blocks = ip->di_blocks;
	/* XXX: no way to get active file data back from here? */
	lastdnode.dn_flags = DN_OFFSET;
	lastdnode.dn_vol_position = p->c_tapea - tapestart;
	lastdnode.dn_inode = p->c_inumber;
	if (fwrite((char *)&lastdnode, sizeof (struct dnode), 1, fp) != 1) {
		(void) fprintf(stderr, gettext("%s: %s error\n"),
			"dnodedata", "write");
		return (-1);
	}
	dnodecnt++;
	return (0);
}

void
dnode_span(fp)
	FILE *fp;
{
	long curpos;
	/*
	 * re-write the last dnode to indicate that its file
	 * spans tapes.
	 */
	lastdnode.dn_flags |= DN_MULTITAPE;
	curpos = ftell(fp);
	if (fseek(fp, curpos - sizeof (struct dnode), 0) == -1) {
		(void) fprintf(stderr, gettext("%s: %s error\n"),
			"dnode_span", "fseek");
		return;
	}
	if (fwrite((char *)&lastdnode, sizeof (struct dnode), 1, fp) != 1) {
		(void) fprintf(stderr,
			gettext("%s: cannot rewrite dnode\n"), "dnode_span");
		return;
	}
}

void
outputtapes(fp)
	FILE *fp;
{
	struct tape_data *tp;
	int cnt;

	for (cnt = 0, tp = tapes; tp; cnt++, tp = tp->nxt) {
		if (cnt > tapecnt) {
			(void) fprintf(stderr, gettext("tape count error\n"));
			exit(1);
		}
		if (fwrite((char *)&tp->bu_tape,
				sizeof (struct bu_tape), 1, fp) != 1) {
			(void) fprintf(stderr, gettext("tape write error\n"));
			exit(1);
		}
	}
}
