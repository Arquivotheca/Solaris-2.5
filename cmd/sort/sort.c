/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	Portions Copyright (c) 1988, 1991 - 1994, Sun Microsystems, Inc	*/
/*	All Rights Reserved.						*/

#ident	"@(#)sort.c	1.21	95/02/27 SMI"	/* SVr4.0 1.22	*/

#include <stdio.h>
#include <ctype.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <values.h>
#include <stdlib.h>
#include <limits.h>
#include <locale.h>
#include <widec.h>
#include <wctype.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <ulimit.h>
#include <errno.h>

#define	N	16
#define	C	20
#define	NF	10
#define	MTHRESH	 8 /* threshhold for doing median of 3 qksort selection */
#define	TREEZ	32 /* no less than N and best if power of 2 */

/*
 * Memory administration
 *
 * Using a lot of memory is great when sorting a lot of data.
 * Using a megabyte to sort the output of `who' loses big.
 * MAXMEM, MINMEM and DEFMEM define the absolute maximum,
 * minimum and default memory requirements.  Administrators
 * can override any or all of these via defines at compile time.
 * Users can override the amount allocated (within the limits
 * of MAXMEM and MINMEM) on the command line.
 *
 * For PDP-11s, memory is limited by the maximum unsigned number, 2^16-1.
 * Administrators can override this too.
 * Arguments to core getting routines must be unsigned.
 * Unsigned long not supported on 11s.
 */

#ifndef	MAXMEM
#define	MAXMEM	1048576	/* Megabyte maximum */
#endif

#ifndef	MINMEM
#define	MINMEM	  16384	/* 16K minimum */
#endif

#ifndef	DEFMEM
#define	DEFMEM	  32768	/* Same as old sort */
#endif


#define	ASC 	0
#define	NUM	1
#define	MON	2


#define	blank(c) (iswspace(c) && ((c) != L'\n'))
/* #define	blank(c) ((c)==' ' || (c)=='\t') */

static FILE	*os;
static char	*dirtry = NULL;
static char	*file1;
static char	*file;
static char	*filep;
static int	nfiles;
static int	*lspace;
static int	*maxbrk;
static unsigned tryfor;
static unsigned alloc;
static char bufin[BUFSIZ], bufout[BUFSIZ];
					/*
					 * Use setbuf's to avoid malloc calls.
					 * malloc seems to get heartburn
					 * when brk returns storage.
					 */
static int	maxrec;
static int 	mflg;
static int	nway;
static int	cflg;
static int	uflg;
static char	*outfil;
static int	unsafeout;	/* kludge to assure -m -o works */
static wchar_t	tabchar;
static int 	eargc;
static char	**eargv;
static struct btree {
	wchar_t	*rp;
	int	rn;
	int	recsz;
	int	allocflag;
} tree[TREEZ], *treep[TREEZ];
static long	wasfirst = 0, notfirst = 0;
static int	bonus;
static wchar_t	*save;
static wchar_t	*lines[2];
static int	save_alloc;

static struct	field {
	wchar_t (*code)(wchar_t); /* WAS: unsigned char *code; */
	int (*ignore)(wchar_t); /* WAS: unsigned char *ignore; */
	int fcmp;
	int rflg;
	int bflg[2];
	int m[2];
	int n[2];
}	*fields;

static wchar_t nofold(wchar_t);
static void sort(void);
static void msort(wchar_t **, wchar_t **);
static void insert(struct btree **, int);
static void merge(int, int);
static void cline(wchar_t *, wchar_t *);
static int xrline(FILE *, struct btree *);
static int yrline(FILE *, int);
static void wline(wchar_t *);
static void checksort(void);
static void disorder(char *, wchar_t *);
static void newfile(void);
static char *setfil(int);
static void oldfile(void);
static void safeoutfil(void);
static void cant(char *);
static void diag1(const char *, int);
static void diag2(const char *f, const char *, int);
static void term(void);
static int getsign(wchar_t *, wchar_t *);
static int cmp(wchar_t *, wchar_t *);
static int cmpa(wchar_t *, wchar_t *);
static wchar_t *skip(wchar_t *, struct field *, int);
static wchar_t *eol(wchar_t *);
static void initree(void);
static int cmpsave(int);
static int field(char *, int, int);
static int number(char **);
static void qksort(wchar_t **, wchar_t **);
static void month_init(void);
static int month(wchar_t *);
static void rderror(char *);
static void wterror(char *);
static int grow_core(unsigned, unsigned);
static int nonprint(wchar_t);
static int dict(wchar_t);
static wchar_t fold(wchar_t);
static void initdecpnt(void);
static void warning(void);
static void usage(void);
static int zero(wchar_t);
static char *get_subopt(int, char **, char);
static int (*compare)(wchar_t *, wchar_t *) = cmpa;

static struct field proto = {
	nofold,
	zero,
	ASC,
	1,
	0,
	0,
	0,
	-1,
	0,
	0
};
static int	nfields = 0;
static int 	error = 2;

static int	not_c;	/* flag showing if LC_COLLATE is not C locale */
static int	modflg = 0;	/* if -d, -i or -f is set */
static int	collsize = 0;
static wchar_t	*collb1, *collb2;

static wchar_t *months[12];
static wchar_t	decpnt;			/* decimal point */
static wchar_t	mon_decpnt;		/* decimal point for monetary */
static wchar_t	thousands_sep;		/* thousands separator */
static wchar_t	mon_thousands_sep;	/* thousands separator for monetary */

static struct	tm	ct = {
	0, 0, 1, 0, 86, 0, 0};


int
main(argc, argv)
char **argv;
{
	int		a;
	int		i;
	int		nf;
	int		oldmaxrec;
	char		*arg;
	char		*tabarg;
	struct field	*p;
	struct field	*q;
	unsigned int	maxalloc;
	unsigned int	newalloc;

	/*
	 * close any file descriptors that may have been
	 * left open -- we may need them all
	 *
	 * the above comment is from the original sort.
	 *
	 * Because sort depends on libmapmalloc which opens
	 * a file descriptor, #3, for /dev/zero,
	 * "for" statement is moved to the position before setlocale()
	 * so that it won't close /dev/zero.
	 *
	 * Perhaps it is not necessary to close these file descriptors.
	 */
	for (i = 4; i < 4 + N; i++)
		(void) close(i);

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);
	not_c = strcmp("C", setlocale(LC_COLLATE, NULL));


	fields = (struct field *)malloc(NF*sizeof (struct field));
	nf = NF;
	fields[nfields] = proto;
	initree();
	eargv = argv;
	tryfor = DEFMEM;
	initdecpnt();
	while (--argc > 0) {
		if (**++argv == '-') {
			arg = *argv;
			switch (*++arg) {
			case '\0':
				if (arg[-1] == '-')
					eargv[eargc++] = "-";
				break;

			case 'o':
				if (*(arg+1) != '\0')
					outfil = arg + 1;
				else {
					outfil = get_subopt(argc, argv, 'o');
					argc--;
					argv++;
				}
				break;

			case 'k':
				if (++nfields >= nf) {
					if ((fields = (struct field *)
					realloc(fields, (nf + NF) *
					sizeof (struct field))) == NULL) {
						(void) fprintf(stderr, gettext(
						    "sort: too many keys\n"));
						return (2);
					}
					nf += NF;
				}
				fields[nfields] = proto;
				if (field(get_subopt(argc, argv, 'k'), 0, 1)
				    < 0)
					fields[nfields--] = proto;
				argc--;
				argv++;
				break;

			case 'T':
				if (--argc > 0)
					dirtry = *++argv;
				break;

			case 't':
				if (*(arg+1) == '\0') {
					tabarg = get_subopt(argc, argv, 't');
					if (tabarg[1] != '\0')
						usage();
					(void) mbtowc(&tabchar, tabarg,
					    MB_CUR_MAX);
					argc--;
					argv++;
				} else
					(void) mbtowc(&tabchar, arg+1,
					    MB_CUR_MAX);
				break;

			default:
				if (field(++*argv, nfields > 0, 0) < 0)
					fields[nfields--] = proto;
				break;
			}
		} else if (**argv == '+') {
			if (++nfields >= nf) {
				if ((fields = (struct field *)
				realloc(fields, (nf + NF) *
				sizeof (struct field))) == NULL) {
					(void) fprintf(stderr,
					gettext("sort: too many keys\n"));
					return (2);
				}
				nf += NF;
			}
			fields[nfields] = proto;
			(void) field(++*argv, 0, 0);
		} else
			eargv[eargc++] = *argv;
	}
	q = &fields[0];
	for (a = 1; a <= nfields; a++) {
		p = &fields[a];
		if (p->code != proto.code) continue;
		if (p->ignore != proto.ignore) continue;
		if (p->fcmp != proto.fcmp) continue;
		if (p->rflg != proto.rflg) continue;
		if (p->bflg[0] != proto.bflg[0]) continue;
		if (p->bflg[1] != proto.bflg[1]) continue;
		p->code = q->code;
		p->ignore = q->ignore;
		p->fcmp = q->fcmp;
		p->rflg = q->rflg;
		p->bflg[0] = p->bflg[1] = q->bflg[0];
	}
	if (eargc == 0)
		eargv[eargc++] = "-";
	if (cflg && eargc > 1) {
		(void) fprintf(stderr,
		    gettext("sort: can check only 1 file\n"));
		return (2);
	}

	safeoutfil();

	lspace = (int *) sbrk(0);
	maxbrk = (int *) ulimit(3, 0L);
	if (!mflg && !cflg)
		if ((alloc = grow_core(tryfor, (unsigned) 0)) == 0) {
			(void) fprintf(stderr, gettext(
				"sort: allocation error before sort\n"));
			return (2);
		}

	a = -1;
	if ((filep = tempnam(dirtry, "stm")) == NULL) {
		(void) fprintf(stderr, gettext(
			"sort: allocation error on temp name\n"));
		return (2);
	}
	/* add the suffix "aa", used to keep count of files	*/
	file1 = (char *) malloc(strlen(filep) + 3); /* 3 = strlen("aa") + 1 */
	(void) strcpy(file1, filep);
	(void) strcat(file1, "aa");
	free(filep);

	/* set filep to point to beginning of suffix	*/
	filep = file1;
	while (*filep)
		filep++;
	filep -= 2;
	file = file1;
	a = creat(file, 0600);

	if (a < 0) {
		diag1(gettext("sort: can't locate temp: "), errno);
		return (2);
	}
	(void) close(a);
	(void) unlink(file);
	if (sigset(SIGHUP, SIG_IGN) != SIG_IGN)
		(void) sigset(SIGHUP, (void (*)(int))term);
	if (sigset(SIGINT, SIG_IGN) != SIG_IGN)
		(void) sigset(SIGINT, (void (*)(int))term);
	(void) sigset(SIGPIPE, (void (*)(int))term);
	if (sigset(SIGTERM, SIG_IGN) != SIG_IGN)
		(void) sigset(SIGTERM, (void (*)(int))term);
	nfiles = eargc;

	if (cflg) {
		checksort();
		return (0);
	}
	/* only executed when -c is not used */
	maxrec = 0;
	if (!mflg) {
		if (not_c && modflg) {
#define	INIT_COLL_LEN	128
			collsize = INIT_COLL_LEN;
			collb1 = (wchar_t *)
				malloc(collsize * sizeof (wchar_t) * 2);
		}

		sort();
		if (ferror(stdin))
			rderror(NULL);
		(void) fclose(stdin);
	}

	if (maxrec == 0) { /* sorting phase is skipped */
#define	INIT_MAXREC	256
		maxrec = INIT_MAXREC;
		if (not_c && modflg) {
		/* collbuf is not allocated because sorting phase is skipped */
		/* otherwise collbuf is allocated */
		/* If LC_COLLATE == C, no collation buffers are needed */
			collb1 = (wchar_t *) malloc(maxrec *
					sizeof (wchar_t) * 2);
			collb2 = collb1 + maxrec;
		}
	}
	alloc = (N + 1) * (maxrec * sizeof (wchar_t)) + N * BUFSIZ;
	maxalloc = (maxbrk - lspace) * sizeof (int *);
	for (nway = N; nway >= 2; --nway) {
		if (alloc < maxalloc)
			break;
		alloc -= maxrec * sizeof (wchar_t) + BUFSIZ;
	}
	if (nway < 2 || brk((char *)lspace + alloc) != 0) {
		(void) fprintf(stderr, gettext(
			"sort: allocation error before merge\n"));
		term();
	}


	wasfirst = notfirst = 0;
	oldmaxrec = maxrec;
	a = mflg ? 0 : eargc;
	if ((i = nfiles - a) > nway) {	/* Do leftovers early */
		if ((i %= (nway - 1)) == 0)
			i = nway - 1;
		if (i != 1)  {
			newfile();
			setbuf(os, bufout);
			merge(a, a+i);
			a += i;
		}
	}
	for (; a+nway < nfiles || unsafeout && (a < eargc); a = i) {
		i = a+nway;
		if (i >= nfiles)
			i = nfiles;
		newfile();
		setbuf(os, bufout);
		if (oldmaxrec < maxrec) {
			newalloc = (nway + 1) * maxrec * sizeof (wchar_t);
			if (newalloc <= maxalloc) {
				alloc = newalloc;
				(void) brk((char *) lspace + alloc);
			}
			oldmaxrec = maxrec;
		}
		merge(a, i);
	}
	if (a != nfiles) {
		oldfile();
		setbuf(os, bufout);
		if (oldmaxrec < maxrec) {
			newalloc = (nway + 1) * maxrec * sizeof (wchar_t);
			if (newalloc <= maxalloc) {
				alloc = newalloc;
				(void) brk((char *) lspace + alloc);
			}
			oldmaxrec = maxrec;
		}
		merge(a, nfiles);
	}
	error = 0;
	term();
	/*NOTREACHED*/
}

static void
sort()
{
	register wchar_t *cp;
	register wchar_t **lp;
	FILE *iop;
	wchar_t *keep;
	wchar_t	*ekeep = 0;	/* keep lint quiet */
	wchar_t	**mp;
	wchar_t	**lmp;
	wchar_t	**ep;
	int n;
	int done, i, first;
	char *f;

	/*
	** Records are read in from the front of the buffer area.
	** Pointers to the records are allocated from the back of the buffer.
	** If a partially read record exhausts the buffer, it is saved and
	** then copied to the start of the buffer for processing with the
	** next coreload.
	*/
	first = 1;
	done = 0;
	keep = 0;
	i = 0;
	ep = (wchar_t **) (((char *)lspace) + alloc);
	if ((f = setfil(i++)) == NULL) /* open first file */
		iop = stdin;
	else if ((iop = fopen(f, "r")) == NULL)
		cant(f);
	setbuf(iop, bufin);
	do {
		lp = ep - 1;
		cp = (wchar_t *) lspace;
		*lp-- = cp;
		if (keep != 0) /* move record from previous coreload */
			for (; keep < ekeep; *cp++ = *keep++);
		while ((wchar_t *)lp - cp > 1) {
			if (fgetws(cp, (wchar_t *)lp - cp, iop) == NULL)
				n = 0;
			else
				n = wslen(cp);
			if (n == 0) {
				if (ferror(iop))
					rderror(f);

				if (keep == 0)
					if (i < eargc) {
						(void) fclose(iop);
						if ((f = setfil(i++)) == NULL)
							iop = stdin;
						else if ((iop = fopen(f, "r"))
						    == NULL)
							cant(f);
						setbuf(iop, bufin);
						continue;
					} else {
						done++;
						break;
					}
			}
			cp += n - 1;
			if (*cp == L'\n') {
				cp += 2;
				if (cp - *(lp+1) > maxrec) {
					maxrec = cp - *(lp+1);
					if (collsize != 0 &&
					collsize < maxrec) {
						free(collb1);
		/* the special malloc from libmapmalloc.so always succeed */
						collsize = maxrec +
							INIT_COLL_LEN;
						collb1 = (wchar_t *)
							malloc(collsize *
							sizeof (wchar_t) * 2);
					}
				}
				*lp-- = cp;
				keep = 0;
			} else if (cp + 2 < (wchar_t *) lp) {
				/* the last record of the input */
				/* file is missing a NEWLINE    */
				if (f == NULL)
					warning();
				else
					(void) fprintf(stderr, gettext(
	"sort: warning: missing NEWLINE added at end of input file %s\n"), f);
				*++cp = L'\n';
				*++cp = 0;
				*lp-- = ++cp;
				keep = 0;
			} else {  /* the buffer is full */
				keep = *(lp+1);
				ekeep = ++cp;
			}

			if ((wchar_t *)lp - cp <= 2 && first == 1) {
				/* full buffer */
				tryfor = alloc;
				tryfor = grow_core(tryfor, alloc);
				if (tryfor == 0)
					/* could not grow */
					first = 0;
				else { /* move pointers */
					lmp = ep +
					    (tryfor/sizeof (wchar_t **) - 1);
					mp = ep - 1;
					while (mp > lp)
						*lmp-- = *mp--;
					ep += tryfor/sizeof (wchar_t **);
					lp += tryfor/sizeof (wchar_t **);
					alloc += tryfor;
				}
			}
		}
		if (keep != 0 && *(lp+1) == (wchar_t *) lspace) {
			(void) fprintf(stderr, gettext(
				"sort: fatal: record too large\n"));
			term();
		}
		first = 0;
		lp += 2;
		if (done == 0 || nfiles != eargc)
			newfile();
		else
			oldfile();
		setbuf(os, bufout);
		collb2 = collb1 + collsize;
		msort(lp, ep);
		if (ferror(os))
			wterror(gettext("sort: write error while sorting: "));
		(void) fclose(os);
	} while (done == 0);
}


static void
msort(wchar_t **a, wchar_t **b)
{
	register struct btree **tp;
	register int i, j, n;
	wchar_t *save;
	int	blkcnt[TREEZ];
	wchar_t	**blkcur[TREEZ];

	i = (b - a);
	if (i < 1)
		return;
	else if (i == 1) {
		wline(*a);
		return;
	} else if (i >= TREEZ)
		n = TREEZ; /* number of blocks of records */
	else n = i;

	/* break into n sorted subgroups of approximately equal size */
	tp = &(treep[0]);
	j = 0;
	do {
		(*tp++)->rn = j;
		b = a + (blkcnt[j] = i / n);
		qksort(a, b);
		blkcur[j] = a = b;
		i -= blkcnt[j++];
	} while (--n > 0);
	n = j;

	/* make a sorted binary tree using the first record in each group */
	i = 0;
	while (i < n) {
		(*--tp)->rp = *(--blkcur[--j]);
		insert(tp, ++i);
	}
	wasfirst = notfirst = 0;
	bonus = cmpsave(n);


	j = uflg;
	tp = &(treep[0]);
	while (n > 0)  {
		wline((*tp)->rp);
		if (j) save = (*tp)->rp;

		/* Get another record and insert.  Bypass repeats if uflg */

		do {
			i = (*tp)->rn;
			if (j)
				while ((blkcnt[i] > 1) &&
				    (**(blkcur[i]-1) == '\0')) {
					--blkcnt[i];
					--blkcur[i];
				}
			if (--blkcnt[i] > 0) {
				(*tp)->rp = *(--blkcur[i]);
				insert(tp, n);
			} else {
				if (--n <= 0) break;
				bonus = cmpsave(n);
				tp++;
			}
		} while (j && (*compare)((*tp)->rp, save) == 0);
	}
}


/* Insert the element at tp[0] into its proper place in the array of size n */
/* Pretty much Algorith B from 6.2.1 of Knuth, Sorting and Searching */
/* Special case for data that appears to be in correct order */

static void
insert(tp, n)
struct btree **tp;
int n;
{
	register struct btree **lop, **hip, **midp;
	register int c;
	struct btree *hold;

	midp = lop = tp;
	hip = lop++ + (n - 1);
	if ((wasfirst > notfirst) && (n > 2) &&
	    ((*compare)((*tp)->rp, (*lop)->rp) >= 0)) {
		wasfirst += bonus;
		return;
	}
	while ((c = hip - lop) >= 0) {
	/* leave midp at the one tp is in front of */
		midp = lop + c / 2;
		if ((c = (*compare)((*tp)->rp, (*midp)->rp)) == 0)
			break; /* match */
		if (c < 0)
			lop = ++midp;   /* c < 0 => tp > midp */
		else
			hip = midp - 1; /* c > 0 => tp < midp */
	}
	c = midp - tp;
	if (--c > 0) { /* number of moves to get tp just before midp */
		hip = tp;
		lop = hip++;
		hold = *lop;
		do
			*lop++ = *hip++;
		while (--c > 0);
		*lop = hold;
		notfirst++;
	} else
		wasfirst += bonus;
}




static void
merge(a, b)
{
	FILE *tfile[N];
	wchar_t	*buffer;
	register int nf;		/* number of merge files */
	register struct btree **tp;
	register int i, j;
	char	*f;
	char	*iobuf;
	struct btree	*bptr;
/*
 * Memory allocation policy:
 *
 *	lspace == buffer ---->  +START OF HEAP------------------+
 *	tfile[0] (FILE *)	| IO buffer for file 0		|
 *	tfile[1] (FILE *)	| IO buffer for file 1		|
 *	  ...			.				.
 *	tfile[i] (FILE *) uses->| IO buffer (BUFSIZE bytes)	|
 *	  ...			.				.
 *	tfile[N-1] (FILE *)	|				|
 *			save -->|-------------------------------|
 *		                |<maxrec> characters for temp?  |
 *				|-------------------------------|
 *				|<maxrec> characters for file 0	|
 *				|<maxrec> characters for file 1	|
 *	treep[0]		.                              	.
 *	treep[1]		.				.
 *	  ...	       /------->|<maxrec> characters for file i	|
 *	treep[i].rp --/		.				.
 *	        .rn == i (?)	.				.
 *	  ...			.				.
 *	treep[N-1]		|-------------------------------|
 *
 */

	iobuf = (char *) lspace;
	save = (wchar_t *) ((char *)lspace + nway * BUFSIZ);
	save_alloc = 0;
	buffer = save + maxrec;
	tp = &(treep[0]);
	for (nf = 0, i = a; i < b; i++)  {
		f = setfil(i);
		if (f == 0)
			tfile[nf] = stdin;
		else if ((tfile[nf] = fopen(f, "r")) == NULL)
			cant(f);
		bptr = *tp;
		bptr->rn = nf;
		bptr->recsz = maxrec;
		if ((char *) (buffer + maxrec) > ((char *) lspace + alloc)) {
				if (bptr->allocflag)
					free(bptr->rp);
				bptr->rp = (wchar_t *)	malloc
						(maxrec * sizeof (wchar_t));
				bptr->allocflag = 1;
		} else {
			if (bptr->allocflag)
				free(bptr->rp);
			bptr->rp = buffer;
			bptr->allocflag = 0;
		}
		buffer += maxrec;
		setbuf(tfile[nf], iobuf);
		iobuf += BUFSIZ;
		if (xrline(tfile[nf], (*tp)) == 0) {
			nf++;
			tp++;
		} else {
			if (ferror(tfile[nf]))
				rderror(f);
			(void) fclose(tfile[nf]);
		}
	}


	/* make a sorted btree from the first record of each file */
	--tp;
	i = 1;
	while (i++ < nf)
		insert(--tp, i);

	bonus = cmpsave(nf);
	tp = &(treep[0]);
	j = uflg;
	while (nf > 0) {
		wline((*tp)->rp);
		if (j) cline(save, (*tp)->rp);

		/* Get another record and insert.  Bypass repeats if uflg */

		do {
			i = (*tp)->rn;
			if (xrline(tfile[i], (*tp))) {
				if (ferror(tfile[i]))
					rderror(setfil(i+a));
				(void) fclose(tfile[i]);
				if (--nf <= 0) break;
				++tp;
				bonus = cmpsave(nf);
			} else insert(tp, nf);
		} while (j && (*compare)((*tp)->rp, save) == 0);
	}


	for (i = a; i < b; i++) {
		if (i >= eargc)
			(void) unlink(setfil(i));
	}
	if (ferror(os))
		wterror(gettext("sort: write error while merging: "));
	(void) fclose(os);
}

static void
cline(wchar_t *tp, wchar_t *fp)
{
	while ((*tp++ = *fp++) != L'\0');
}

static int
xrline(FILE *iop, struct btree *btp)
{
	register int n;
	int 	sz = btp->recsz;
	wchar_t	*s = btp->rp;
	int	y;


	if (fgetws(s, sz, iop) == NULL)
		n = 0;
	else
		n = wslen(s);
	if (n == 0)
		return (1);
	if (*(s+n-1) == L'\n')
		return (0);
	else if (n < sz - 1) {
		warning();
		s += n - 1;
		*++s = L'\n';
		*++s = L'\0';
		return (0);
	} else {
#define	INC_MAXREC 128
		sz += INC_MAXREC;
		if (btp->allocflag) {
			btp->rp = (wchar_t *)
				realloc(btp->rp, sz * sizeof (wchar_t));
		} else {
			btp->rp = (wchar_t *) malloc(sz * sizeof (wchar_t));
			(void) wscpy(btp->rp, s);
			btp->allocflag = 1;
		}
		s = btp->rp + n;
		for (;;) {
			if (fgetws(s, INC_MAXREC + 1, iop) == NULL)
				n = 0;
			else
				n = wslen(s);
			if (n == 0) {
				y = 1;
				break;
			}
			if (*(s + n - 1) == L'\n') {
				y = 0;
				break;
			} else if (n < INC_MAXREC) {
				warning();
				s += n - 1;
				*++s = L'\n';
				*++s = L'\0';
				y = 0;
				break;
			} else {
				sz += INC_MAXREC;
				btp->rp = (wchar_t *)
					realloc(btp->rp, sz * sizeof (wchar_t));
				s = btp->rp + sz - INC_MAXREC - 1;
				if (btp->rp == NULL) {
				/*
				 * it is impossible in this special malloc
				 * which is from libmapmalloc
				 */
					(void) fprintf(stderr, gettext(
						"out of memory\n"));
					term();
				}
			}
		}
		if (maxrec < (sz - (INC_MAXREC - n))) {
			maxrec = sz - (INC_MAXREC - n);
			if (not_c && modflg) {
				free(collb1);
				collb1 = (wchar_t *) malloc
					(maxrec * sizeof (wchar_t) * 2);
				collb2 = collb1 + maxrec;
			}
			if (uflg) { /* expand save */
				s = save;
				save = (wchar_t *)
					malloc(maxrec * sizeof (wchar_t));
				(void) wscpy(save, s);
				if (save_alloc != 0)
					free(s);
				save_alloc = 1;
			}
		}
		btp->recsz = sz;
		return (y);
	}
}



static int
yrline(FILE *iop, int i)
{
	register int n;
	wchar_t	*s, *t;
	int	sz;
	int	y;

	s = lines[i];
	if (fgetws(s, maxrec, iop) == NULL)
		n = 0;
	else
		n = wslen(s);
	if (n == 0)
		return (1);
	if (*(s + n -1) == L'\n')
		return (0);
	else if (n < maxrec - 1) {
		warning();
		s += n - 1;
		*++s = L'\n';
		*++s = L'\0';
		return (0);
	} else {
		t = (wchar_t *) malloc(maxrec * sizeof (wchar_t));
		(void) wscpy(t, lines[1-i]); /* save the other line */

		if (lines[i] != (wchar_t *) lspace) { /* move around lines[i] */
			(void) wscpy((wchar_t *) lspace, lines[i]);
			s = lines[i] = (wchar_t *) lspace;
		}
		sz = INC_MAXREC + 1;
		s += n;
		for (;;) {
			maxrec += INC_MAXREC;
			alloc += INC_MAXREC * sizeof (wchar_t) * 2;
			if (brk((char *) lspace + alloc) != 0) {
				(void) fprintf(stderr, gettext(
					"sort: fatal: line too long\n"));
				term();
			}
			if (fgetws(s, sz, iop) == NULL)
				n = 0;
			else
				n = wslen(s);
			if (n == 0) {
				y = 1;
				break;
			}
			s += n - 1;
			if (*s == L'\n') {
				y = 0;
				break;
			} else if (n < sz - 1) {
				warning();
				*++s = L'\n';
				*++s = '\0';
				y = 0;
				break;
			} else {
				s++;
			}
		}
		lines[1-i] = (wchar_t *) lspace + maxrec;
		(void) wscpy(lines[1-i], t); /* restore */
		free(t);
		if (not_c && modflg) {
			free(collb1);
			collb1 = (wchar_t *)
				malloc(maxrec * 2 * sizeof (wchar_t));
			collb2 = collb1 + maxrec;
		}
		return (y);
	}
}

static void
wline(wchar_t *s)
{
	(void) fputws(s, os);
	if (ferror(os))
		wterror(gettext("sort: write error while sorting: "));

}

static void
checksort()
{
	char *f; /* Temp file name. */
	register int i, j, r;
	register FILE *iop;

	f = setfil(0);
	if (f == 0)
		iop = stdin;
	else if ((iop = fopen(f, "r")) == NULL)
		cant(f);

	setbuf(iop, bufin);

	maxrec = INIT_MAXREC;
	alloc = maxrec * 2 * sizeof (wchar_t);
	if (alloc > (maxbrk - lspace) * sizeof (int *)) {
		maxrec = (maxbrk - lspace) / 2;
		alloc = maxrec * 2 * sizeof (int *);
	}
	(void) brk((char *) lspace + alloc);

	lines[0] = (wchar_t *) lspace;
	lines[1] = (wchar_t *) lspace + maxrec;

	if (not_c && modflg) {
		collb1 = (wchar_t *) malloc(maxrec * 2 * sizeof (wchar_t));
		collb2 = collb1 + maxrec;
	}

	if (yrline(iop, 0)) {
		if (ferror(iop)) {
			rderror(f);
		}
		(void) fclose(iop);
		exit(0);
	}
	i = 0;   j = 1;
	while (!yrline(iop, j))  {
		r = (*compare)(lines[i], lines[j]);
		if (r < 0)
			disorder(gettext("sort: disorder: %ws\n"),
				lines[j]);
		if (r == 0 && uflg)
			disorder(gettext("sort: non-unique: %ws\n"),
				lines[j]);
		r = i;  i = j; j = r;
	}
	if (ferror(iop))
		rderror(f);
	(void) fclose(iop);
}

static void
disorder(char *format, wchar_t *t)
{
	register wchar_t *u;
	for (u = t; *u != L'\n'; u++);
	*u = 0;
#ifndef XPG4
	(void) fprintf(stderr, format, t);
#endif
	error = 1;
	term();
}

static void
newfile()
{
	register char *f;

	f = setfil(nfiles);
	if ((os = fopen(f, "w")) == NULL) {
		diag2(gettext("sort: can't create %s: "), f, errno);
		term();
	}
	nfiles++;
}

static char *
setfil(i)
register int i;
{
	if (i < eargc)
		if (eargv[i][0] == '-' && eargv[i][1] == '\0')
			return (0);
		else
			return (eargv[i]);
	i -= eargc;
	filep[0] = i/26 + 'a';
	filep[1] = i%26 + 'a';
	return (file);
}

static void
oldfile()
{
	if (outfil) {
		if ((os = fopen(outfil, "w")) == NULL) {
			diag2(gettext("sort: can't create %s: "),
				outfil, errno);
			term();
		}
	} else
		os = stdout;
}

static void
safeoutfil(void)
{
	register int i;
	struct stat ostat, istat;

	if (!mflg || outfil == 0)
		return;
	if (stat(outfil, &ostat) == -1)
		return;
	if ((i = eargc - N) < 0) i = 0;	/* -N is suff., not nec. */
	for (; i < eargc; i++) {
		if (stat(eargv[i], &istat) == -1)
			continue;
		if (ostat.st_dev == istat.st_dev&&
		    ostat.st_ino == istat.st_ino)
			unsafeout++;
	}
}

static void
cant(char *f)
{
	diag2(gettext("sort: can't open %s: "), f, errno);
	term();
}


static void
diag1(const char *format, int errcode)
{
	char *s = strerror(errcode);

	(void) fprintf(stderr, format);

	if (s == NULL)
		(void) fprintf(stderr, gettext("Error %d\n"), errcode);
	else
		(void) fprintf(stderr, "%s\n", s);
}


static void
diag2(const char *format, const char *file, int errcode)
{
	char *s = strerror(errcode);

	(void) fprintf(stderr, format, file);
	if (s == NULL)
		(void) fprintf(stderr, gettext("Error %d\n"), errcode);
	else
		(void) fprintf(stderr, "%s\n", s);
}

static void
term()
{
	register i;

	if (nfiles == eargc)
		nfiles++;
	for (i = eargc; i <= nfiles; i++) {	/* <= in case of interrupt */
		(void) unlink(setfil(i));	/* with nfiles not updated */
	}
	exit(error);
}

static int
getsign(wchar_t *pa, wchar_t *la)
{
	int	i = 1;

	if (pa == la)
		return (0);
	if (*pa == L'-') {
		i = -1;
		pa++;
	}
	while (pa < la && iswdigit(*pa)) {
		if (*pa != L'0')
			return (i);
		pa++;
	}
	if (*pa != decpnt)	/* i is 0 */
		return (0);
	pa++;
	while (pa < la && iswdigit(*pa)) {
		if (*pa != L'0')
			return (i);
		pa++;
	}
	return (0);
}

static int
cmp(wchar_t *i, wchar_t *j)
{
	wchar_t *pa, *pb;
	int (*ignore)(wchar_t);
	int sa;
	int sb;
	wchar_t (*code)(wchar_t);
	int a, b;
	int k;
	wchar_t *la, *lb;
	wchar_t *ipa, *ipb, *jpa, *jpb;
	struct field *fp;
	wchar_t	*p1, wa, wb;
	int	ret;

	for (k = nfields > 0; k <= nfields; k++) {
		fp = &fields[k];
		pa = i;
		pb = j;
		if (k) {
			la = skip(pa, fp, 1);
			pa = skip(pa, fp, 0);
			lb = skip(pb, fp, 1);
			pb = skip(pb, fp, 0);
		} else {
			la = eol(pa);
			lb = eol(pb);
		}
		if (fp->fcmp == NUM) {
			sa = sb = fp->rflg;
			while (iswspace(*pa) && pa < la)
				pa++;
			while (iswspace(*pb) && pb < lb)
				pb++;
			if (pa == la) { /* i is 0 */
				if (b = getsign(pb, lb))
					return (sb * b);
				continue;
			} else if (pb == lb) { /* j is 0 */
				if (a = getsign(pa, la))
					return ((-sa) * a);
				continue;
			}
			if (*pa == '-') {
				pa++;
				sa = -sa;
			}
			if (*pb == '-') {
				pb++;
				sb = -sb;
			}
			for (ipa = pa; ipa < la; ipa++) {
				if (!(iswdigit(*ipa) ||
				    (*ipa == thousands_sep) ||
				    (*ipa == mon_thousands_sep)))
					break;
			}

			for (ipb = pb; ipb < lb; ipb++) {
				if (!(iswdigit(*ipb) ||
				    (*ipb == thousands_sep) ||
				    (*ipb == mon_thousands_sep)))
					break;
			}

			jpa = ipa;
			jpb = ipb;
			a = 0;
			if (sa == sb)
				while (ipa > pa && ipb > pb) {
					ipa--;
					ipb--;
					while ((ipa > pa) &&
					    ((*ipa == thousands_sep) ||
					    (*ipa == mon_thousands_sep)))
						ipa--;
					while ((ipb > pb) &&
					    ((*ipb == thousands_sep) ||
					    (*ipb == mon_thousands_sep)))
						ipb--;
					if ((b = *ipb - *ipa) != 0)
						a = b;
				}

			while (ipa > pa)
				if ((*--ipa != L'0') &&
				    (*ipa != thousands_sep) &&
				    (*ipa != mon_thousands_sep))
					return (-sa);
			while (ipb > pb)
				if ((*--ipb != L'0') &&
				    (*ipb != thousands_sep) &&
				    (*ipb != mon_thousands_sep))
					return (sb);
			if (a)
				return (a*sa);
			if ((*(pa = jpa) == decpnt) ||
			    (*(pa = jpa) == mon_decpnt))
				pa++;
			if ((*(pb = jpb) == decpnt) ||
			    (*(pb = jpb) == mon_decpnt))
				pb++;
			if (sa == sb)
				while (pa < la && iswdigit(*pa) &&
				    pb < lb && iswdigit(*pb))
					if ((a = *pb++ - *pa++) != 0)
						return (a*sa);
			while (pa < la && iswdigit(*pa))
				if (*pa++ != L'0')
					return (-sa);
			while (pb < lb && iswdigit(*pb))
				if (*pb++ != L'0')
					return (sb);
			continue;
		} else if (fp->fcmp == MON)  {
			sa = fp->rflg*(month(pb)-month(pa));
			if (sa)
				return (sa);
			else
				continue;
		}
		code = fp->code;
		ignore = fp->ignore;

		if (!not_c)
			goto loop;

		if (modflg) {
			for (p1 = collb1; pa < la && *pa != L'\n'; pa++) {
				if ((*ignore)(*pa))
					continue;
				*p1++ = (*code)(*pa);
			}
			*p1 = L'\0';
			for (p1 = collb2; pb < lb && *pb != L'\n'; pb++) {
				if ((*ignore)(*pb))
					continue;
				*p1++ = (*code)(*pb);
			}
			*p1 = L'\0';
			ret = wscoll(collb1, collb2);
		} else {	/* no transformation is needed */
			if (pa >= la || *pa == L'\n')
				if (pb < lb && *pb != L'\n')
					return (fp->rflg);
				else continue;
			if (pb >= lb || *pb == L'\n')
				return (-fp->rflg);
			wa = *la;
			*la = L'\0';
			wb = *lb;
			*lb = L'\0';
			ret = wscoll(pa, pb);
			*la = wa;
			*lb = wb;
		}
		if (ret > 0)
			return (- fp->rflg);
		else if (ret < 0)
			return (fp->rflg);
		else
			continue;

loop:		/* executed only when LC_COLLATE == C */
		while ((*ignore)(*pa) && *pa)
			pa++;
		while ((*ignore)(*pb) && *pb)
			pb++;
		if (pa >= la || *pa == L'\n')
			if (pb < lb && *pb != L'\n')
				return (fp->rflg);
			else continue;
		if (pb >= lb || *pb == L'\n')
			return (-fp->rflg);
		if ((sa = (*code)(*pb++)-(*code)(*pa++)) == 0)
			goto loop;
		return (sa*fp->rflg);
	}
	if (uflg)
		return (0);
	return (cmpa(i, j));
}

static int
cmpa(wchar_t *pa, wchar_t *pb)
{
	int	result;

	result = wscoll(pa, pb);

	if (result == 0)
		return (0);
	else if (result > 0)
		return (-fields[0].rflg);
	else return (fields[0].rflg);
}

static wchar_t *
skip(wchar_t *p, struct field *fp, int j)
{
	register i;
	wchar_t tbc;

	if ((i = fp->m[j]) < 0)
		return (eol(p));
	if ((tbc = tabchar) != 0)
		while (--i >= 0) {
			while (*p != tbc)
				if (*p != L'\n')
					p++;
				else goto ret;
			if (i > 0 || j == 0)
				p++;
		} else
			while (--i >= 0) {
			while (blank(*p))
				p++;
			while (!blank(*p))
				if (*p != L'\n')
					p++;
				else goto ret;
		}
	if (fp->bflg[j]) {
		if (j == 1 && fp->m[j] > 0)
			p++;
		while (blank(*p))
			p++;
	}
	i = fp->n[j];
	while ((i-- > 0) && (*p != L'\n'))
		p++;
ret:
	return (p);
}

static wchar_t *
eol(wchar_t *p)
{
	while (*p != L'\n') p++;
	return (p);
}


static void
initree()
{
	register struct btree **tpp, *tp;
	register int i;

	tp = &(tree[0]);
	tpp = &(treep[0]);
	i = TREEZ;
	while (--i >= 0)
		*tpp++ = tp++;
}

int
cmpsave(int n)
{
	register int award;

	if (n < 2)
		return (0);
	for (n++, award = 0; (n >>= 1) > 0; award++);
	return (award);
}

static int
field(char *s, int k, int kflag)
/*
 * Fill field[nfields] for the current command argument s.
 * k is non-zero iff this is the default and only field so far.
 * kflag is 1 if command line is XCU4 sort key field style:
 *	-k field_start[type][,field_end[type]]
 * kflag is 0 if command line is obsolescent sort key style:
 *	[+pos1 [-pos2]]
 * where pos1 and pos2 are of the form:
 *	field0_number[.first0_character][type]
 *
 * NOTE: the fields and characters in pos1 and pos2 are
 * numbered from 0; XCU4 fields and characters (-k) are
 * numbered from 1. The relation as specified in XCU4.2 is:
 *
 *	The fully specified +pos1 -pos2 form with type
 *	modifiers T and U:
 *		+w.xT -y.zU
 *	is equivalent to:
 *		undefined		(z == 0 & U contains b & -t is present)
 *		-k w+1.x+1T,y.0U	(z == 0 otherwise)
 *		-k w+1.x+1T,y+1.zU	(z > 0)
 */
{
	int (*save_compare)(wchar_t *, wchar_t *) = compare;
	struct field *p;
	int	d;
	int	i;

	p = &fields[nfields];

	for (; *s != 0; s++) {
		d = 0;
		switch (*s) {
		case '\0':
			return (0);

		case ',':
			k = (nfields > 0);
			break;

		case 'b':
			p->bflg[k]++;
			break;

		case 'd':
			p->ignore = dict;
			modflg = 1;
			break;

		case 'f':
			p->code = fold;
			modflg = 1;
			break;

		case 'i':
			p->ignore = nonprint;
			modflg = 1;
			break;

		case 'c':
			cflg = 1;
			continue;

		case 'm':
			mflg = 1;
			continue;

		case 'M':
			month_init();
			p->fcmp = MON;
			p->bflg[0]++;
			break;

		case 'n':
			p->fcmp = NUM;
			p->bflg[0]++;
			break;

		case 't':
			i = mbtowc(&tabchar, s+1, MB_CUR_MAX);
			if (i > 0)
				s += i;
			continue;

		case 'r':
			p->rflg = -1;
			continue;

		case 'u':
			uflg = 1;
			continue;

		case 'y':
			if (*++s) {
				if (isdigit(*s))
					tryfor = number(&s) * 1024;
				else
					usage();
			} else {
				--s;
				tryfor = MAXMEM;
			}
			continue;

		case 'z':	/* depricated by use of libmapmalloc */
#if 0
			/* we don't want NOISE */
			(void) fprintf(stderr, gettext(
			"sort: warning: -z is no longer supported. "
			"sort automatically allocates buffers large enough "
				"to hold the longest lines.\n"));
#endif
			return (0);

		case '.':
			if (p->m[k] == -1)	/* -m.n with m missing */
				p->m[k] = 0;
			d = &fields[0].n[0]-&fields[0].m[0];
			if (*++s == 0) {
				--s;
				p->m[k+d] = 0;
				continue;
			}

		default:
			if (isdigit(*s)) {
				p->m[k+d] = number(&s);
			} else
				usage();
		}
		compare = cmp;
	}
	if (kflag == 1) {
		if (p->m[0] != 0)
			p->m[0]--;
		if (p->n[0] != 0)
			p->n[0]--;
		if (p->n[1] != 0)	/* this is not a bug */
			p->m[1]--;	/* decrement m[1] if n[1] != 0 */
	}				/* see comments above */

	if (k) {
		if ((p->m[1] != 0) && (p->m[0] > p->m[1])) {
			compare = save_compare;
			return (-1);
		}
		if ((p->m[0] == p->m[1]) &&
		    (p->n[0] != 0) &&
		    (p->n[0] > p->n[1])) {
			compare = save_compare;
			return (-1);
		}
	}

	return (0);
}

static int
number(char **ppa)
/*
 * Parse an integer at *ppa of a command argument, advance ppa past
 * the number, return the integer value.
 */
{
	register int n;
	register char *pa;

	pa = *ppa;
	n = 0;
	while (isdigit(*pa)) {
		n = n*10 + *pa - '0';
		*ppa = pa++;
	}
	return (n);
}

#define	qsexc(p, q) t = *p; *p = *q; *q = t
#define	qstexc(p, q, r) t = *p; *p = *r; *r = *q; *q = t

static void
qksort(wchar_t **a, wchar_t **l)
{
	register wchar_t **i, **j;
	register wchar_t **lp, **hp;
	wchar_t **k;
	int c, delta;
	wchar_t *t;
	unsigned n;


start:
	if ((n = l-a) <= 1)
		return;

	n /= 2;
	if (n >= MTHRESH) {
		lp = a + n;
		i = lp - 1;
		j = lp + 1;
		delta = 0;
		c = (*compare)(*lp, *i);
		if (c < 0) --delta;
		else if (c > 0) ++delta;
		c = (*compare)(*lp, *j);
		if (c < 0) --delta;
		else if (c > 0) ++delta;
		if ((delta /= 2) && (c = (*compare)(*i, *j)))
		    if (c > 0)
			n -= delta;
		    else
			n += delta;
	}
	hp = lp = a+n;
	i = a;
	j = l-1;


	for (;;) {
		if (i < lp) {
			if ((c = (*compare)(*i, *lp)) == 0) {
				--lp;
				qsexc(i, lp);
				continue;
			}
			if (c < 0) {
				++i;
				continue;
			}
		}

loop:
		if (j > hp) {
			if ((c = (*compare)(*hp, *j)) == 0) {
				++hp;
				qsexc(hp, j);
				goto loop;
			}
			if (c > 0) {
				if (i == lp) {
					++hp;
					qstexc(i, hp, j);
					i = ++lp;
					goto loop;
				}
				qsexc(i, j);
				--j;
				++i;
				continue;
			}
			--j;
			goto loop;
		}


		if (i == lp) {
			if (uflg) {
				k = lp;
				while (k < hp)
					**k++ = 0;
			}
			if (lp-a >= l-hp) {
				qksort(hp+1, l);
				l = lp;
			} else {
				qksort(a, lp);
				a = hp+1;
			}
			goto start;
		}


		--lp;
		qstexc(j, lp, i);
		j = --hp;
	}
}


static void
month_init()
{
#define	MAX_MON_LEN	20	/* Max. # of chars of month names. */
	char	time_buf[MAX_MON_LEN*MB_LEN_MAX];
	wchar_t	time_wbuf[MAX_MON_LEN];
	int	i;

	for (i = 0; i < 12; i++) {
		ct.tm_mon = i;
		(void) ascftime(time_buf, "%b", &ct);
		(void) mbstowcs(time_wbuf, time_buf, MAX_MON_LEN);
		months[i] = wsdup(time_wbuf);
	}
}


static int
month(s)
wchar_t *s;
{
	register wchar_t *t, *u;
	register i;

	for (i = 0; i < 12; i++) {
		for (t = s, u = months[i]; fold(*t++) == fold(*u++); )
			if (*u == 0)
				return (i);
	}
	return (-1);
}

static void
rderror(s)
char *s;
{
	if (s == 0)
		diag1(gettext("sort: read error on stdin: "), errno);
	else
		diag2(gettext("sort: read error on %s: "), s, errno);
	term();
}

static void
wterror(format)
char *format;
{
	/* gettext has already been applied to format when wterror is invoked */
	diag1(format, errno);
	term();
}

static int
grow_core(size, cursize)
	unsigned size, cursize;
{
	unsigned newsize;
	/*
	 * The variable below and its associated code was written so
	 * this would work on pdp11s.  It works on the vax & 3b20 also.
	 */
	u_long longnewsize;

	longnewsize = (u_long) size + (u_long) cursize;
	if (longnewsize < MINMEM)
		longnewsize = MINMEM;
	else if (longnewsize > MAXMEM)
		longnewsize = MAXMEM;
	newsize = (unsigned) longnewsize;
	for (; ((char *)lspace+newsize) <= (char *)lspace; newsize >>= 1);
	if (longnewsize > (u_long) (maxbrk - lspace) * (u_long) sizeof (int *))
		newsize = (maxbrk - lspace) * sizeof (int *);
	if (newsize <= cursize)
		return (0);
	if (brk((char *) lspace + newsize) != 0)
		return (0);
	return (newsize - cursize);
}

/* One of the three functions is used as an "ignore" function. */
static int
/*LINTED*/
zero(wchar_t w)
{
	return (0);
}

static int
nonprint(wchar_t w)
{
	return (!iswprint(w));
}

static int
dict(wchar_t w)
{
	return (!(iswalnum(w)||iswspace(w)));
}

/* Either function is used as "code" function. */
static wchar_t
fold(wchar_t w)
{
	return (iswlower(w)?towupper(w):w);
}
static wchar_t
nofold(wchar_t w)
{
	return (w);
}

static void
initdecpnt()
/* Load the decimal points and thousands separators for this locale. */
{
	struct lconv *l = localeconv();

	(void) mbtowc(&decpnt, l->decimal_point, MB_CUR_MAX);
	(void) mbtowc(&mon_decpnt, l->mon_decimal_point, MB_CUR_MAX);
	(void) mbtowc(&thousands_sep, l->thousands_sep, MB_CUR_MAX);
	(void) mbtowc(&mon_thousands_sep, l->mon_thousands_sep, MB_CUR_MAX);
}

static void
warning(void)
{
	(void) fprintf(stderr, gettext(
		"sort: warning: missing NEWLINE added at EOF\n"));
}

static void
usage(void)
{
	(void) fprintf(stderr, gettext(
	    "sort [-bcdfiMmnru] [-o output] [-T directory] [-ykmem] [-t char]\n"
	    "     [+pos1 [-pos2]] [-k field_start[type][,field_end[type]] "
	    "[file...]\n"));
	exit(2);
}

static char *
get_subopt(int argc, char **argv, char option)
{
	if ((--argc <= 0) || (**++argv == '-')) {
		(void) fprintf(stderr, gettext(
		    "sort: option requires an argument -- %c\n"), option);
		usage();
	}
	return (*argv);
}
