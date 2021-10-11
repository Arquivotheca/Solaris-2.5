
#ident	"@(#)listem.c 1.16 93/10/05"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "defs.h"
#define	_POSIX_SOURCE	/* hack to avoid redef of MAXNAMLEN */
#define	_POSIX_C_SOURCE
#include <dirent.h>
#undef	_POSIX_SOURCE
#undef	_POSIX_C_SOURCE
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "rpcdefs.h"

/*
 * Support for listing tapes/dumps the dump database knows about
 */

#define	GROW 3

struct tapef {
	char	*label;
	long	recnum;
};

#define	LISTHASHSIZE 257
static struct htablef {
	long	val;
	struct htablef *next;
} *htable[LISTHASHSIZE];
#define	NULL_HTB (struct htablef *)0

#ifdef __STDC__
static int recseen(int);
static struct active_tape *getrec(long, FILE *);
static struct active_tape *lookupname(const char *, long *, FILE *, long);
static char *my_realloc(char *p, long);
static int tapecmp(const struct tapef *, const struct tapef *);
static bool_t showtape(XDR *, const char *, int);
static int dumpcmp(const struct dump_entry *, const struct dump_entry *);
static bool_t showdumps(XDR *, struct active_tape *, long, FILE *, int);
static bool_t showdump(XDR *, const char *, struct dump_entry *, int);
#else
static int recseen();
static struct active_tape *getrec();
static struct active_tape *lookupname();
static char *my_realloc();
static int tapecmp();
static bool_t showtape();
static int dumpcmp();
static bool_t showdumps();
static bool_t showdump();
#endif

bool_t
xdr_listem(xdrs, p)
	XDR *xdrs;
	struct tapelistargs *p;
{
	bool_t rc;

	if (getreadlock() == 0) {
		return (xdr_unavailable(xdrs));		/* XXX */
	}

	rc = showtape(xdrs, p->label, p->verbose);

	releasereadlock();
	return (rc);
}


static int
recseen(recnum)
	int recnum;
{
	int	hval = recnum % LISTHASHSIZE;
	struct htablef *p;

	for (p = htable[hval]; p != NULL_HTB; p = p->next) {
		if (p->val == recnum)
			break;
	}
	if (p != NULL_HTB)
		return (1);

	p = (struct htablef *)malloc(sizeof (struct htablef));
	if (p == (struct htablef *)0) {
		(void) fprintf(stderr,
			gettext("%s: out of memory\n"), "recseen");
		return (1);
	}
	p->val = recnum;
	p->next = htable[hval];
	htable[hval] = p;
	return (0);
}


static struct active_tape *
getrec(recnum, tfile)
	long recnum;
	FILE *tfile;
{
	static struct active_tape tape;

	if (fseek(tfile, recnum * sizeof (struct active_tape), 0) < 0) {
		(void) fprintf(stderr, gettext(
			"%s: %s error\n"), "getrec", "fseek");
		return (NULL_TREC);
	}
	if (fread((char *)&tape, sizeof (struct active_tape), 1, tfile) == 0) {
		(void) fprintf(stderr, gettext(
			"%s: %s error\n"), "getrec", "fread");
		return (NULL_TREC);
	}
	return (&tape);
}


static struct active_tape *
#ifdef __STDC__
lookupname(const char *name,
	long *recnump,
	FILE *tfile,
	long maxrecs)
#else
lookupname(name, recnump, tfile, maxrecs)
	char *name;
	long *recnump;
	FILE *tfile;
	long maxrecs;
#endif
{
	long	i;
	struct active_tape *tape;

	for (i = TAPE_FIRSTDATA; i < maxrecs; i++) {
		tape = getrec(i, tfile);
		if (tape == NULL_TREC)
			return (NULL_TREC);
		if (strncmp(name, tape->tape_label, LBLSIZE) == 0) {
			*recnump = i;
			return (tape);
		}
	}
	return (NULL_TREC);
}


static char *
my_realloc(p, size)
	char *p;
	long size;
{
	char	*retval;

	if (p == NULL)
		retval = malloc((unsigned)size);
	else
		retval = realloc(p, (unsigned)size);
	return (retval);
}


static int
#ifdef __STDC__
tapecmp(const struct tapef *a,
	const struct tapef *b)
#else
tapecmp(a, b)
	struct tapef *a, *b;
#endif
{
	return (strcoll(a->label, b->label));
}


static bool_t
#ifdef __STDC__
showtape(XDR *xdrs,
	const char *name,
	int verbose)
#else
showtape(xdrs, name, verbose)
	XDR *xdrs;
	char *name;
	int verbose;
#endif
{
	long	recnum;
	long	maxrecs;
	long	numtapes = 0;
	long	maxtapes = 0;
	struct tapef *tapes = NULL;
	long	i;
	FILE *tfile;
	int	size;
	char	retbuf[MAXPATHLEN], *rp;
	struct stat st;
	struct active_tape *tape;

	if ((tfile = fopen(TAPEFILE, "r")) == NULL) {
		(void) fprintf(stderr,
			gettext("cannot open active tapes file\n"));
		return (FALSE);
	}
	if (fstat(fileno(tfile), &st) < 0) {
		(void) fprintf(stderr,
			gettext("cannot stat active tapes file\n"));
		(void) fclose(tfile);
		return (FALSE);
	}
	if (st.st_size % sizeof (struct active_tape)) {
		(void) fprintf(stderr,
			gettext("active tapes blocksize mismatch\n"));
		(void) fclose(tfile);
		return (FALSE);
	}
	maxrecs = st.st_size / sizeof (struct active_tape);

	if (name[0] != '\0') {
		tape = lookupname(name, &recnum, tfile, maxrecs);
		if (tape == NULL_TREC) {
			(void) sprintf(retbuf, gettext("%s not found\n"), name);
			size = strlen(retbuf) + 1;
			rp = retbuf;
			if (!xdr_bytes(xdrs, &rp, (u_int *)&size, MAXPATHLEN)) {
				(void) fclose(tfile);
				return (FALSE);
			}
		} else {
			if (verbose < 2) {
				(void) sprintf(retbuf, "%s\n", name);
				size = strlen(retbuf) + 1;
				rp = retbuf;
				if (!xdr_bytes(xdrs, &rp,
				    (u_int *)&size, MAXPATHLEN)) {
					(void) fclose(tfile);
					return (FALSE);
				}
			}
			if (verbose)
				(void) showdumps(xdrs, tape, recnum,
				    tfile, verbose);
		}
	} else {
		/*
		 * since we are going to do all tapes,
		 * mark the ones on the freelist as
		 * having been seen already
		 */
		tape = getrec((long)TAPE_FREELIST, tfile);
		if (tape == NULL_TREC) {
			(void) fclose(tfile);
			return (FALSE);
		}
		while (tape->tape_next != TAPE_FREELIST) {
			(void) recseen((int)tape->tape_next);
			tape = getrec((long)tape->tape_next, tfile);
			if (tape == NULL_TREC) {
				(void) fclose(tfile);
				return (FALSE);
			}
		}
		(void) recseen((int)TAPE_FREELIST);

		/*
		 * Walk remaining records, saving tape
		 * names and first record num
		 */
		recnum = TAPE_FIRSTDATA;
		while (recnum < maxrecs) {
			tape = getrec(recnum, tfile);
			if (tape == NULL_TREC) {
				(void) fclose(tfile);
				return (FALSE);
			}

			/* already seen it? */
			if (recseen((int)recnum) != 0) {
				recnum++;
				continue;
			}

			/* save the name and recnum */
			if (numtapes >= maxtapes) {
				maxtapes += GROW;
				tapes = (struct tapef *)
				    my_realloc((char *)tapes,
				    /*LINTED [tapes realloc'ed]*/
				    maxtapes * sizeof (struct tapef));
				if (tapes == NULL) {
					(void) fprintf(stderr, gettext(
					    "%s: out of memory\n"), "showtape");
					(void) fclose(tfile);
					return (FALSE);
				}
			}
			tapes[numtapes].label = malloc(LBLSIZE + 1);
			if (tapes[numtapes].label == NULL) {
				(void) fprintf(stderr,
				    gettext("%s: out of memory\n"), "showtape");
				(void) fclose(tfile);
				return (FALSE);
			}
			(void) sprintf(tapes[numtapes].label, "%.*s", LBLSIZE,
			    tape->tape_label);
			tapes[numtapes].recnum = recnum;
			numtapes++;

			/* mark all other records for this tape seen */
			for (i = tape->tape_next;
			    i != recnum;
			    i = tape->tape_next) {
				tape = getrec(i, tfile);
				if (tape == NULL_TREC) {
					(void) fclose(tfile);
					return (FALSE);
				}
				(void) recseen((int)i);
			}
			recnum++;
		}

		if (numtapes) {
			/* sort the tape names */
#ifdef __STDC__
			qsort((char *)tapes, (int)numtapes,
				sizeof (struct tapef),
				(int (*)(const void *, const void *))tapecmp);
#else
			qsort((char *)tapes, (int)numtapes,
				sizeof (struct tapef), (int (*)())tapecmp);
#endif

			/* print out the requested info */
			for (i = 0; i < numtapes; i++) {
				if (verbose < 2) {
					(void) sprintf(retbuf, "%s\n",
						tapes[i].label);
					size = strlen(retbuf) + 1;
					rp = retbuf;
					if (!xdr_bytes(xdrs, &rp,
					    (u_int *)&size, MAXPATHLEN)) {
						(void) fclose(tfile);
						return (FALSE);
					}
				}

				if (verbose) {
					tape = getrec(tapes[i].recnum, tfile);
					if (tape == NULL_TREC) {
						(void) fclose(tfile);
						return (FALSE);
					}
					if (showdumps(xdrs, tape,
					    tapes[i].recnum,
					    tfile, verbose) == FALSE) {
						(void) fclose(tfile);
						return (FALSE);
					}
				}
			}
		}
	}
	(void) fclose(tfile);
	return (TRUE);
}


static long	maxdumps = 0;
static struct dump_entry *dumps = NULL;

static int
#ifdef __STDC__
dumpcmp(const struct dump_entry *a,
	const struct dump_entry *b)
#else
dumpcmp(a, b)
	struct dump_entry *a, *b;
#endif
{
	return (a->tapepos - b->tapepos);
}

static bool_t
showdumps(xdrs, tape, tape_first, tfile, verbose)
	XDR *xdrs;
	struct active_tape *tape;
	long tape_first;
	FILE *tfile;
	int verbose;
{
	int	i;
	long	numdumps = 0;
	struct active_tape save_tape = *tape;

	do {
		for (i = 0; i < DUMPS_PER_TAPEREC; i++) {
			if (tape->dumps[i].host == 0)
				continue;
			if (numdumps >= maxdumps) {
				maxdumps += GROW;
				dumps = (struct dump_entry *)
				    my_realloc((char *)dumps,
				    /*LINTED [dumps realloc'ed]*/
				    maxdumps * sizeof (struct dump_entry));
				if (dumps == NULL) {
					(void) fprintf(stderr,
					    gettext("%s: out of memory\n"),
						"showdumps");
					return (FALSE);
				}
			}
			bcopy((char *)&tape->dumps[i], (char *)&dumps[numdumps],
			    sizeof (struct dump_entry));
			numdumps++;
		}

		if (tape->tape_next != tape_first) {
			tape = getrec((long)tape->tape_next, tfile);
			if (tape == NULL_TREC)
				return (FALSE);
		} else
			tape = NULL_TREC;
	} while (tape != NULL_TREC);

	if (numdumps) {
#ifdef __STDC__
		qsort((char *)dumps, (int)numdumps, sizeof (struct dump_entry),
			(int (*)(const void *, const void *))dumpcmp);
#else
		qsort((char *)dumps, (int)numdumps, sizeof (struct dump_entry),
			(int (*)())dumpcmp);
#endif

		/*
		 * we used to exit when one showdump() failed, but now we
		 * just skip that line.
		 */
		for (i = 0; i < numdumps; i++)
			(void) showdump(xdrs, save_tape.tape_label,
			    &dumps[i], verbose);
	}

	return (TRUE);
}

static bool_t
#ifdef __STDC__
showdump(XDR *xdrs,
	const char *label,
	struct dump_entry *dump,
	int verbose)
#else
showdump(xdrs, label, dump, verbose)
	XDR *xdrs;
	char *label;
	struct dump_entry *dump;
	int verbose;
#endif
{
	char	*addr, *cp;
	char	*ptr;
	FILE	*hfile;
	DIR	*dp;
	int	i;
	char	hfilename[MAXPATHLEN];
	char	name[MAXNAMLEN];
	int	size;
	char	retbuf[2*MAXPATHLEN], *rp;
	char	tmpbuf[MAXPATHLEN];
	struct hostent *hent;
	struct dheader header;
	struct in_addr inaddr;
	struct dirent *de;

	inaddr.s_addr = dump->host;
	addr = inet_ntoa(inaddr);

	/* get the current working directory, the database lives here */
	if ((dp = opendir(".")) == NULL) {
		(void) fprintf(stderr, gettext(
			"cannot open database directory\n"));
		return (FALSE);
	}

	/* read each directory entry until we match the IP address */
	while (de = readdir(dp)) {
		if ((ptr = strchr(de->d_name, (int)'.')) != NULL)
			if (strcmp(addr, ptr+1) == 0)
				break;	/* we have an address match */
	}

	(void) closedir(dp);	/* clean up */

	if (de == NULL) {
		(void) fprintf(stderr, gettext(
			"cannot find directory for %s\n"), addr);
		return (FALSE);
	}

	/* make sure we have a valid host name for the IP address */
	*ptr = '\0';
	/* shouldn't have to do this, but doens't work properly on Intel box */
	(void) strcpy(name, de->d_name);	/* save the name for later */
	if ((hent = gethostbyname(name)) == NULL) {
		(void) fprintf(stderr, gettext(
			"cannot resolve host %s for addr %s\n"),
			name, addr);
		return (FALSE);
	}

	/* double check to make sure that the host matches the IP address */
	for (i = 0; ((u_long *)hent->h_addr_list[i]) != NULL; i++)
		if (*((u_long *)hent->h_addr_list[i]) == dump->host)
			break;
	if (((u_long *)hent->h_addr_list[i]) == NULL) {
		(void) fprintf(stderr, gettext(
			"host %s does not have addr %s\n"), name, addr);
		return (FALSE);
	}

	(void) sprintf(hfilename, "%s.%s/%s.%lu", name, addr,
			HEADERFILE, dump->dump_id);
	if ((hfile = fopen(hfilename, "r")) == NULL) {
		(void) fprintf(stderr, gettext("cannot open header file %s\n"),
			hfilename);
		return (FALSE);
	}
	if (fread((char *) & header, sizeof (header), 1, hfile) == 0) {
		(void) fprintf(stderr, gettext(
		    "error reading header from file %s\n"), hfilename);
		(void) fclose(hfile);
		return (FALSE);
	}
	(void) fclose(hfile);

	if (verbose == 2) {
		(void) sprintf(retbuf, "%.*s ", LBLSIZE, label);
		(void) sprintf(tmpbuf, "%lu ", dump->tapepos);
		(void) strcat(retbuf, tmpbuf);
		if (header.dh_flags & DH_ACTIVE)
			(void) strcat(retbuf, "A");
		else if (header.dh_flags & DH_PARTIAL)
			(void) strcat(retbuf, "P");
		else if (header.dh_flags & DH_TRUEINC)
			(void) strcat(retbuf, "x");
		else {
			(void) sprintf(tmpbuf, "%.1lu", header.dh_level);
			(void) strcat(retbuf, tmpbuf);
		}
		if (strncmp(label, header.dh_label[0], LBLSIZE) != 0)
			(void) strcat(retbuf, "C");
		if (header.dh_flags & DH_EMPTY)
			(void) strcat(retbuf, "E");
		(void) strcat(retbuf, " ");
		(void) sprintf(tmpbuf, "%s:%s ", header.dh_host, header.dh_mnt);
		(void) strcat(retbuf, tmpbuf);
		if (header.dh_dev[0] != '\0')
			(void) sprintf(tmpbuf, "%s ", header.dh_dev);
		else
			(void) sprintf(tmpbuf, "- ");
		(void) strcat(retbuf, tmpbuf);
		/*
		 * This listing is designed for machine-
		 * readability (for use in scripts) and
		 * thus we must use ctime().
		 */
		(void) sprintf(tmpbuf, "%lu %s",
		    header.dh_time, ctime(&header.dh_time));	/* XXX */
		(void) strcat(retbuf, tmpbuf);
		size = strlen(retbuf) + 1;
		rp = retbuf;
		if (!xdr_bytes(xdrs, &rp, (u_int *) & size, MAXPATHLEN))
			return (FALSE);
	} else {
		(void) sprintf(retbuf,
			gettext("    File: %lu - "), dump->tapepos);
		if (strncmp(label, header.dh_label[0], LBLSIZE) != 0)
			(void) strcat(retbuf, gettext("Continued "));
		if (header.dh_flags & DH_EMPTY)
			(void) strcat(retbuf, gettext("Empty "));
		if (header.dh_flags & DH_ACTIVE)
			(void) strcat(retbuf,
				gettext("Active file re-dump of "));
		else if (header.dh_flags & DH_PARTIAL)
			(void) strcat(retbuf, gettext("Partial mode dump of "));
		else if (header.dh_flags & DH_TRUEINC)
			(void) strcat(retbuf,
				gettext("True incremental dump of "));
		else {
			(void) sprintf(tmpbuf, gettext("Level %.1lu dump of "),
			    header.dh_level);
			(void) strcat(retbuf, tmpbuf);
		}
		(void) sprintf(tmpbuf, "%s:%s", header.dh_host, header.dh_mnt);
		(void) strcat(retbuf, tmpbuf);
		if (header.dh_dev[0] != '\0') {
			(void) sprintf(tmpbuf, " (%s)", header.dh_dev);
			(void) strcat(retbuf, tmpbuf);
		}
		(void) sprintf(tmpbuf, gettext("\n        dumped on %s"),
		    lctime(&header.dh_time));
		(void) strcat(retbuf, tmpbuf);
		size = strlen(retbuf) + 1;
		rp = retbuf;
		if (!xdr_bytes(xdrs, &rp, (u_int *) & size, MAXPATHLEN))
			return (FALSE);
	}
	return (TRUE);
}
