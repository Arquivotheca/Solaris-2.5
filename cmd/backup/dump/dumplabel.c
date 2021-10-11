/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#ident	"@(#)dumplabel.c 1.36 93/04/28"

#include "dump.h"
#include <config.h>
#include <lfile.h>
#include <rmt.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>
#include <byteorder.h>

/*
 * An array of lfile structures is kept in shared
 * memory so the status info is retained both forward
 * in time (across forks) and backward (across exits)
 * in the case we're unwinding after a tape error.
 * NB: Dump imposes a limit (TP_NINOS == 128) on the
 * number of volumes per dumpset.
 */
struct lfile {
	char	lf_status;		/* N, U, P, F, E */
	char	lf_used;		/* +, - */
	daddr_t	lf_filenum;		/* tape position */
	char	lf_name[LBLSIZE+1];	/* label name */
};
static struct lfile *tapelist;		/* dynamically allocated array */
static struct lfile *lfp;		/* active/current lfile struct */
static struct lfile *endlfp;		/* last+1 in lfile array */

static char	*tapedb;		/* tape library database name */
static size_t	tpdbnamlen;		/* length of database name */
static u_int	maxid;			/* maximum tape id number */

#ifdef __STDC__
static void alloclfile(void);
static void writelfile(void);
static char *getlabelname(void);
#else
static void alloclfile();
static void writelfile();
static char *getlabelname();
#endif

void
#ifdef __STDC__
readlfile(void)
#else
readlfile()
#endif
{
	FILE *fp;
	char line[256];
	int tapeid;
	register int i;

	if (labelfile == NULL)
		return;

	for (i = 0, maxid = 10; i < LF_MAXIDLEN; i++)
		maxid *= 10;
	maxid--;

	(void) setreuid(-1, 0);
	fp = fopen(labelfile, "r");
	(void) setreuid(-1, getuid());
	if (fp == NULL) {
		msg(gettext("Cannot open volume label file `%s': %s\n"),
		    labelfile, strerror(errno));
		dumpabort();
	}
	if (fgets(line, sizeof (line), fp) == NULL || strcmp(line, LF_HEADER)) {
		msg(gettext("Volume label file `%s' missing header\n"),
			labelfile);
		dumpabort();
	}
	if (fgets(line, sizeof (line), fp) == NULL) {
		msg(gettext("Volume label file `%s' missing library name\n"),
			labelfile);
		dumpabort();
	}
	tpdbnamlen = strlen(line)-1;	/* don't count trailing "\n\0" */
	line[tpdbnamlen] = '\0';	/* strip newline */
	if (tpdbnamlen > (LBLSIZE-LF_MAXIDLEN)-1) {
		msg(gettext("Volume library name `%s' too long! (%u > %d)\n"),
		    line, tpdbnamlen, (LBLSIZE-LF_MAXIDLEN)-1);
		dumpabort();
	}
	tapedb = xmalloc(tpdbnamlen+1);
	(void) strcpy(tapedb, line);
	i = 2;
	alloclfile();
	lfp = tapelist;
	while (fgets(line, sizeof (line), fp)) {
		if ((lfp - tapelist) > TP_NINOS) {
			msg(gettext("Too many volumes (max %d)\n"), TP_NINOS);
			dumpabort();
		}
		++i;
		if (sscanf(line, "%c%c%d",
		    &lfp->lf_status, &lfp->lf_used, &tapeid) != 3) {
			msg(gettext(
		    "Malformed entry in volume label file `%s', line %d\n"),
			    labelfile, i);
			dumpabort();
		}
		(void) sprintf(lfp->lf_name, "%s%c%05d", tapedb,
			LF_LIBSEP, tapeid);
		++lfp;
	}
	endlfp = lfp;
	lfp = NULL;	/* indicate not yet used */
}

/*
 * Build a tape label list (fake L-file) from
 * the list we were handed on the command line.
 */
void
buildlfile(labels)
	char	*labels;
{
	char	*thislabel;
	register char *cp = labels;
	register int i;
	int len;

	for (i = 0, maxid = 10; i < LF_MAXIDLEN; i++)
		maxid *= 10;
	maxid--;

	alloclfile();
	lfp = tapelist;
	while (*cp) {
		if ((lfp - tapelist) > TP_NINOS) {
			msg(gettext("Too many volumes (max %d)\n"), TP_NINOS);
			dumpabort();
		}
		while (*cp && (isspace((u_char)*cp) || *cp == ','))
			cp++;
		if (*cp == '\0')
			break;		/* trailing white-space */
		thislabel = cp;		/* beginning of label name */
		while (*cp && !isspace((u_char)*cp) && *cp != ',')
			cp++;
		len = cp - thislabel;
		if (len == 0) {
			msg(gettext(
			    "Volume label list contains empty label\n"));
			dumpabort();
		}
		if (len > LBLSIZE) {
			msg(gettext(
			    "Volume label (%.*s) longer than %d characters\n"),
				len, thislabel, LBLSIZE);
			dumpabort();
		}
		lfp->lf_status = LF_UNLABELD;
		lfp->lf_used = LF_NOTUSED;
		(void) strncpy(lfp->lf_name, thislabel, len);
		lfp++;
	}
	if (lfp == tapelist) {
		msg(gettext("Volume label list contains no labels!\n"));
		dumpabort();
	}
	endlfp = lfp;
	lfp = NULL;	/* indicate not yet used */
}

/*
 * Allocate the lfile (either real or "fake") in
 * shared memory.  We need the tape status information
 * to span processes both forward and backward (for
 * error recovery) in time.
 */
static void
#ifdef __STDC__
alloclfile(void)
#else
alloclfile()
#endif
{
	int	fd;
	/*
	 * set up shared memory seg
	 */
	fd = open("/dev/zero", O_RDWR);
	if (fd < 0) {
		msg(gettext("Cannot open `%s': %s\n"),
			"/dev/zero", strerror(errno));
		dumpabort();
	}
	tapelist = (struct lfile *)mmap((char *)0,
	    TP_NINOS * sizeof (struct lfile),		/* max number vols */
	    /*LINTED [mmap() returns aligned valued]*/
	    PROT_READ|PROT_WRITE, MAP_SHARED, fd, (off_t)0);
	if ((int)tapelist == -1 || tapelist == NULL) {
		msg(gettext("Cannot mmap volume label list: %s\n"),
		    strerror(errno));
		dumpabort();
	}
	(void) bzero((char *)tapelist, TP_NINOS * sizeof (struct lfile));
	(void) close(fd);
}

/*
 * Write out lfile entries.
 */
static void
#ifdef __STDC__
writelfile(void)
#else
writelfile()
#endif
{
	char *badwrite = gettext("Cannot write volume label file `%s': %s\n");
	FILE *fp;
	char tapepos[20], *idp;
	int firstp = 1;
	register struct lfile *lp;	/* don't disturb global lfp */

	if (labelfile == NULL)
		return;

	(void) setreuid(-1, 0);
	fp = fopen(labelfile, "w");
	(void) setreuid(-1, getuid());
	if (fp == NULL) {
		msg(gettext("Cannot open volume label file `%s': %s\n"),
		    labelfile, strerror(errno));
		dumpabort();
	}
	if (fprintf(fp, LF_HEADER) == EOF) {
		msg(badwrite, labelfile, strerror(errno));
		dumpabort();
	}
	if (fprintf(fp, "%s\n", tapedb) == EOF) {
		msg(badwrite, labelfile, strerror(errno));
		dumpabort();
	}
	for (lp = tapelist; lp < endlfp; lp++) {
		if (lp->lf_status == LF_PARTIAL && firstp) {
			/*
			 * We tell the sequencer where to begin
			 * the next dump if the last volume we
			 * wrote is partially full.  There will
			 * only be one volume in the label file
			 * marked as partially full, but we check
			 * anyway just to be sure.
			 */
			(void) sprintf(tapepos, " %ld\n", lp->lf_filenum);
			firstp = 0;
		} else
			(void) strcpy(tapepos, "\n");
		idp = strchr(lp->lf_name, LF_LIBSEP);
		++idp;
		if (fprintf(fp, "%c%c%s%s", (u_char)lp->lf_status,
		    (u_char)lp->lf_used, idp, tapepos) == EOF) {
			msg(badwrite, labelfile, strerror(errno));
			dumpabort();
		}
	}
	if (fflush(fp) == EOF || fclose(fp) == EOF) {
		msg(gettext("Cannot close volume label file `%s': %s\n"),
			labelfile, strerror(errno));
		dumpabort();
	}
}

/*
 * Get the next volume label in sequence that is
 * not marked errored or full.  If no such volume
 * exists, prompt an operator for a new one.
 */
void
#ifdef __STDC__
getlabel(void)
#else
getlabel()
#endif
{
	/*
	 * This routine is used if reading labels
	 * from an L-file or from a list passed
	 * as a command-line arg.
	 */
	if (labelfile == NULL && endlfp == NULL) {
		(void) strcpy(spcl.c_label, "none");
		return;
	}

	for (lfp = (lfp == NULL ? tapelist : lfp+1); lfp < endlfp; lfp++) {
		if (lfp->lf_status != LF_ERRORED && lfp->lf_status != LF_FULL)
			break;
		/*
		 * Use of devices and labels
		 * must be kept in lockstep.
		 */
		nextdevice();
	}
	if (lfp == endlfp) {
		/*
		 * Ran out of volume labels in the
		 * file -- prompt the operator for
		 * a new label.
		 */
		if ((lfp - tapelist) > TP_NINOS) {
			msg(gettext("Too many volumes (max %d)\n"), TP_NINOS);
			dumpabort();
		}
		(void) strcpy(lfp->lf_name, getlabelname());
		lfp->lf_status = '\0';
		endlfp++;
	}
	(void) strncpy(spcl.c_label, lfp->lf_name, LBLSIZE);
	lfp->lf_used = LF_NOTUSED;
	lfp->lf_filenum = filenum;
}

/*
 * Modify status of a tape label
 * and write out the label file.
 */
void
modlfile(status, used, name, pos)
	int	status;
	int	used;
	char	*name;
	daddr_t	pos;
{
	if (lfp == NULL)
		return;
	if (status)
		lfp->lf_status = (char)status;
	if (used)
		lfp->lf_used = (char)used;
	if (name && (off_t)strlen(name) < LF_MAXIDLEN)
		(void) strncpy(lfp->lf_name, name, LF_MAXIDLEN);
	if (pos)
		lfp->lf_filenum = pos;
	if (lfp->lf_status == LF_PARTIAL && used)
		msg(gettext("Writing data to file %ld of volume `%s'.\n"),
		    lfp->lf_filenum-1, lfp->lf_name);
	writelfile();
}


/*
 * Check a tape label against that
 * which is expected.
 */
#define	MAX_RETRIES	10		/* # times to re-try stacker */
void
#ifdef __STDC__
verifylabel(void)
#else
verifylabel()
#endif
{
	static struct mtop rew = { MTREW, 1 };
	static struct mtop offl = { MTOFFL, 1 };
	struct s_spcl *firstspcl;
	struct byteorder_ctx *byteorder;
	char	*labelbuf;
	char	*yesorno = gettext("(\"yes\" or \"no\") ");
	int	tapefd;
	int	m;
	char	buf1[3000], buf2[3000];
	int	unlabeled, readfail, wrapped, timesthrough = 0;
	int	ntrec = CARTRIDGETREC > HIGHDENSITYTREC ?
	    (NTREC > CARTRIDGETREC ? NTREC : CARTRIDGETREC) :
	    (NTREC > HIGHDENSITYTREC ? NTREC : HIGHDENSITYTREC);

	if (lfp == NULL)
		return;

	if ((byteorder = byteorder_create()) == NULL) {
		(void) fprintf(stderr,
		    gettext("Cannot allocate memory for byteorder library\n"));
		dumpabort();
	}

	/* we set m even if `tape' represents a remote tape, but who cares */
	m = (access(tape, F_OK) == 0) ? 0 : O_CREAT;
#ifdef DEBUG
	/* XGETTEXT:  #ifdef DEBUG only */
	msg(gettext("Verifying volume `%.*s' mounted on drive `%s'\n"),
	    LBLSIZE, spcl.c_label, dumpdev);
#endif

	labelbuf = xmalloc((ntrec * TP_BSIZE));
	(void) bzero(labelbuf, TP_BSIZE);
	/*LINTED [labelbuf = malloc() and therefore aligned]*/
	firstspcl = (struct s_spcl *)labelbuf;
again:
	/*
	 * If we've beaten this stacker or drive
	 * (1/4" drives don't offline) to death, then
	 * move on to the next device, but only if
	 * we haven't wrapped through all the
	 * available devices.
	 */
	getdevinfo((dtype_t *)0, &ndevices, &wrapped);
	if (timesthrough >= MAX_RETRIES && ndevices > 1 && !wrapped) {
		nextdevice();
		timesthrough = 0;
	}
	(void) sprintf(buf1, gettext(
	    "Cannot open `%s'.  Do you want to retry the open?: %s "),
		dumpdev, yesorno);
	for (;;) {
		tapefd = host ? rmtopen(tape, O_RDONLY) :
		    open(tape, O_RDONLY|m, 0600);
		if (tapefd < 0) {
			if (autoload) {
				if (!query_once(buf1, (char *)0, 1))
					dumpabort();
			} else {
				if (!query(buf1, (char *)0))
					dumpabort();
			}
		} else
			break;
	}

	unlabeled = (!lfp->lf_status) || lfp->lf_status == LF_UNLABELD;
	if (unlabeled) {
		modlfile((u_char)LF_UNLABELD, 0, (char *)0, 0);
	} else {
		/*
		 * Now that we know the volume has
		 * a label, update its status
		 */
		modlfile((u_char)LF_NEWLABELD, 0, (char *)0, 0);
	}

	if (host)
		(void) rmtioctl(MTREW, 1);
	else
		(void) ioctl(tapefd, MTIOCTOP, &rew);

	readfail = (host ? rmtread(labelbuf, ntrec * TP_BSIZE) :
		read(tapefd, labelbuf, ntrec * TP_BSIZE)) <= 0;
	if (readfail && !unlabeled) {
		msg(gettext(
		    "Cannot read volume #%d on %s to verify its label\n"),
			tapeno+1, dumpdev);
		dumpabort();
	}
	/*
	 * rewind and close the device
	 */
	if (host) {
		(void) rmtioctl(MTREW, 1);
		rmtclose();
	} else {
		(void) ioctl(tapefd, MTIOCTOP, &rew);
		(void) close(tapefd);
	}
	/*
	 * We assume that a failed read for an unlabeled tape meant that the
	 * tape had no valid data.  Continue the dump on this volume.
	 */
	if (readfail && unlabeled)
		goto done;
	if (strncmp(firstspcl->c_label, spcl.c_label, LBLSIZE)) {
		char *context = gettext("Wrong volume");

		if (normspcl(byteorder,
			firstspcl, (int *)firstspcl, NFS_MAGIC) != 0) {
			/*
			 * If we believe this to be an unlabeled tape and
			 * it doesn't look like a dump image, go ahead and
			 * claim the tape as ours.
			 */
			if (unlabeled)
				goto done;
			(void) sprintf(buf1, gettext(
		    "volume on %s is not a dump volume, should be `%.*s'\n"),
				dumpdev, LBLSIZE, spcl.c_label);
		} else {
			(void) sprintf(buf1, gettext(
			    "volume on %s is `%.*s', should be `%.*s'\n"),
				dumpdev,
				LBLSIZE, firstspcl->c_label[0] != '\0' ?
				    firstspcl->c_label : gettext("<none>"),
				LBLSIZE, spcl.c_label);
			/*
			 * XXX - we should see if this is a valid scratch
			 * tape and, if so, just use it anyway.
			 */
		}
		/*
		 * If autoload is on, offline the wrong volume and
		 * try the next one.
		 */
		if (autoload &&
		    (tapefd = (host) ? rmtopen(tape, O_RDONLY) :
		    open(tape, O_RDONLY|m, 0600)) != -1) {
			if (host) {
				(void) rmtioctl(MTOFFL, 1);
				(void) rmtclose();
			} else {
				(void) ioctl(tapefd, MTIOCTOP, &offl);
				(void) close(tapefd);
			}
			timesthrough++;
		}
		msg(buf1);
		(void) opermes(LOG_CRIT, buf1);
		broadcast(gettext("TAPE LABEL VERIFICATION ERROR!\7\7\n"));
		(void) sprintf(buf1, gettext(
		    "Mount volume `%.*s' on drive `%s'.\n\
\tIs it mounted and ready to go?: %s "),
			LBLSIZE, spcl.c_label, dumpdev, yesorno);
		for (;;) {
			if (autoload && timesthrough < MAX_RETRIES) {
				if (query_once(buf1, context, 1)) {
					(void) query_once(NULL, NULL, 0);
					break;
				}
			} else {
				if (query(buf1, context))
					break;
			}
			(void) sprintf(buf2, gettext(
			    "Do you want to enter a new label name?: %s "),
				yesorno);
			if (!lfp->lf_status &&
			    query(buf2, context)) {
				(void) strcpy(spcl.c_label, getlabelname());
				modlfile(0, 0, spcl.c_label, 0);
			} else {
				(void) sprintf(buf2, gettext(
				    "Do you want to abort dump?: %s "),
					yesorno);
				if (query(buf2, context))
					dumpabort();
			}
		}
		goto again;
	}
done:
	free(labelbuf);
	byteorder_destroy(byteorder);
}


/*
 * Prompt an operator for a tape label.  Only the
 * numeric volume identifier may be entered.  It
 * must be a string of digits no more than LF_MAXIDLEN
 * in length.  If neccessary, '0's will be prepended
 * to pad the string to LF_MAXIDLEN.  Any input error
 * will result in the operator being prompted to abort.
 * Returns the device id and fills in the full name.
 */
static char *
#ifdef __STDC__
getlabelname(void)
#else
getlabelname()
#endif
{
	register char *cp;
	char	buf[3000], *str;
	static char label[LBLSIZE+1];

	for (;;) {
		(void) sprintf(buf, gettext(
		    "Enter the identification number of the\n\
\tnext volume to mount on drive %s: "),
			dumpdev);
		str = getinput(buf, (char *)0);
		for (cp = str; *cp && cp < &str[LF_MAXIDLEN+1]; cp++)
			if (!isdigit((u_char)*cp) && labelfile)
				break;
		if (cp != str && *cp == '\0')
			break;			/* good name */
		if (labelfile)
			(void) sprintf(buf, gettext(
		"Volume identifiers must be between 0 and %d, inclusive\n"),
				maxid);
		else
			(void) sprintf(buf, gettext(
		"Volume identifiers must be less than %d characters\n"),
			LBLSIZE);
		msg(buf);
		(void) opermes(LOG_CRIT, buf);
		(void) sprintf(buf, gettext("Do you want to abort dump?: %s "),
			gettext("(\"yes\" or \"no\") "));
		if (query(buf, gettext("Bad label format")))
			dumpabort();
			/*NOTREACHED*/
	}
	if (labelfile)
		(void) sprintf(label, "%s%c%05d", tapedb, LF_LIBSEP,
			atoi(str));
	else
		(void) strcpy(label, str);
	return (label);
}
