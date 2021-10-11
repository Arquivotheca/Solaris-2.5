#ident	"@(#)pr.c	1.19	95/05/10 SMI"	/* SVr4.0 1.19  */

/*
 *	PR command (print files in pages and columns, with headings)
 *	2+head+2+page[56]+5
 */


/*
 *	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
 *	All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 */


/*
 *		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 */


/*
 *		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 * 	(c) 1986, 1987, 1988, 1989  Sun Microsystems, Inc
 * 	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 * 		All rights reserved.
 */

#include <stdio.h>
#include <signal.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#ifdef EUC
#include <euc.h>
#include <getwidth.h>
#endif EUC

#define	ESC		'\033'
#define	LENGTH		66
#define	LINEW		72
#define	NUMW		5
#define	MARGIN		10
#define	DEFTAB		8
#define	NFILES		10
#define	STDINNAME()	nulls
#define	PROMPT()	(void) putc('\7', stderr) /* BEL */
#define	NOFILE		nulls
#define	ETABS		(Inpos % Etabn)
#define	NSEPC		'\t'
#define	HEAD		gettext("%s  %s Page %d\n\n\n"), date, head, Page
#define	cerror(S)	(void) fprintf(stderr, "pr: %s", gettext(S))
#define	done()		if (Ttyout) (void) chmod(Ttyout, Mode)
#define	ALL_NUMS(s)	(strspn(s, "0123456789") == strlen(s))
#define	REMOVE_ARG(argc, argp)					\
			{					\
				char	**p = argp;		\
				while (*p != NULL)		\
				{				\
					*p = *(p + 1);		\
					p++;			\
				}				\
				argc--;				\
			}
#define	SQUEEZE_ARG(argp, ind)					\
			{					\
				int	i;			\
				for (i = ind; argp[i]; i++)	\
					argp[i] = argp[i+1];	\
			}

/*
 *   ---date time format---
 *   b -- abbreviated month name
 *   e -- day of month
 *   H -- Hour (24 hour version)
 *   M -- Minute
 *   Y -- Year in the form ccyy
 */
#define	FORMAT		"%b %e %H:%M %Y"
#ifdef	EUC
#define	OFF	0
#define	ON	1
#define	ROUNDUP	0.9
#endif	EUC

typedef	char	CHAR;
typedef	int	ANY;
typedef	unsigned	int	UNS;
typedef	struct	{ FILE *f_f; char *f_name; int f_nextc; } FILS;
typedef	struct	{int fold; int skip; int eof; } foldinf;
typedef	struct	{ CHAR *c_ptr, *c_ptr0; long c_lno; int c_skip; } *COLP;
typedef struct	err { struct err *e_nextp; char *e_mess; } ERR;

/*
 * Global data.
 */
static	FILS	*Files;
static	mode_t	Mode;
static	int	Multi = 0;
static	int	Nfiles = 0;
static	int	Error = 0;
static	char	nulls[] = "";
static	char	*Ttyout;
static	char	obuf[BUFSIZ];
static	char	time_buf[50];	/* array to hold the time and date */
static	long	Lnumb = 0;
static	FILE	*Ttyin = stdin;
static	int	Dblspace = 1;
static	int	Fpage = 1;
static	int	Formfeed = 0;
static	int	Length = LENGTH;
static	int	Linew = 0;
static	int	Offset = 0;
static	int	Ncols = 0;
static	int	Pause = 0;
static	int	Sepc = 0;
static	int	Colw;
static	int	Plength;
static	int	Margin = MARGIN;
static	int	Numw;
static	int	Nsepc = NSEPC;
static	int	Report = 1;
static	int	Etabn = 0;
static	int	Etabc = '\t';
static	int	Itabn = 0;
static	int	Itabc = '\t';
static	int	fold = 0;
static	int	foldcol = 0;
static	int	alleof = 0;
static	char	*Head = NULL;
static	CHAR	*Buffer = NULL, *Bufend, *Bufptr;
static	UNS	Buflen;
static	COLP	Colpts;
static	foldinf	*Fcol;
static	int	Page;
static	int	C = '\0';
static	int	Nspace;
static	int	Inpos;
static	int	Outpos;
static	int	Lcolpos;
static	int	Pcolpos;
static	int	Line;
static	ERR	*Err = NULL;
static	ERR	*Lasterr = (ERR *)&Err;
#ifdef	EUC
static	double eucbuf = 1, eucbufw = 0;
static	eucwidth_t	wp;
static	int	setswitch = 0;
static	int	eucwcount = 0;
static	int	scrwcount = 0;
static	int	eucstart = OFF;
#endif	EUC


/*
 * Function prototypes.
 */
static	void	onintr();
static	ANY	*getspace();
static	int	findopt(int, char **);
static	void	fixtty();
static	char 	*GETDATE();
static	char	*ffiler(char *);
static	int	print(char *);
static	void	putpage();
static	void	foldpage();
static	void	nexbuf();
static	void	foldbuf();
static	void	balance(int);
static	int	readbuf(char **, int, COLP);
static	int	get(int);
static	int	put(int);
static	void	putspace();
static	void	unget(int);
static	FILE	*mustopen(char *, FILS *);
static	void	die(char *);
static	void	errprint();
static	void	usage(int);


main(int argc, char **argv)
{
	FILS	fstr[NFILES];
	int	nfdone = 0;


	/* Get locale variables for environment */
	(void) setlocale(LC_ALL, "");

#ifdef EUC
	getwidth(&wp);
	wp._eucw2++;
	wp._eucw3++;
#endif

#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	Files = fstr;
	for (argc = findopt(argc, argv); argc > 0; --argc, ++argv) {
		if (Multi == 'm') {
			if (Nfiles >= NFILES - 1) die("too many files");
			if (mustopen(*argv, &Files[Nfiles++]) == NULL)
				++nfdone;	/* suppress printing */
		} else {
			if (print(*argv))
				(void) fclose(Files->f_f);
			++nfdone;
		}
	}
	if (!nfdone)	/* no files named, use stdin */
		(void) print(NOFILE);	/* on GCOS, use current file, if any */

	if (Report) {
		errprint();	/* print accumulated error reports */
		exit(Error);
	}

	return (Error);
}


/*
 * findopt() returns argc modified to be the number of explicitly supplied
 * filenames, including '-', the explicit request to use stdin.
 * argc == 0 implies that no filenames were supplied and stdin should be used.
 * Options are striped from argv and only file names are returned.
 */

static	int
findopt(int argc, char **argv)
{
	int	eargc = 0;
	int	c;
	int	mflg = 0;
	int	aflg = 0;
	int	optnum;
	int	argv_ind;
	int	end_opt;
	int	i;

	fixtty();

	/* Handle page number option */
	for (optnum = 1, end_opt = 0; optnum < argc && !end_opt; optnum++) {
		switch (*argv[optnum]) {
		case '+':
			/* check for all digits */
			if (strlen(&argv[optnum][1]) !=
			    strspn(&argv[optnum][1], "0123456789")) {
				(void) fprintf(stderr, gettext(
				    "pr: Badly formed number\n"));
				exit(1);
			}

			if ((Fpage = (int) strtol(&argv[optnum][1],
			    (char **)NULL, 10)) < 0) {
				(void) fprintf(stderr, gettext(
				    "pr: Badly formed number\n"));
				exit(1);
			}
			REMOVE_ARG(argc, &argv[optnum]);
			optnum--;
			break;

		case '-':
			/* Check for end of options */
			if (argv[optnum][1] == '-') {
				end_opt++;
				break;
			}
			break;

		default:
			break;
		}
	}

	/*
	 * Handle options with optional arguments.
	 * If optional arguments are present they may not be separated
	 * from the option letter.
	 */

	for (optnum = 1; optnum < argc; optnum++) {
		if (argv[optnum][0] == '-' && argv[optnum][1] == '-')
			/* End of options */
			break;

		if (argv[optnum][0] == '-' && argv[optnum][1] == '\0')
			/* stdin file name */
			continue;

		if (argv[optnum][0] != '-')
			/* not option */
			continue;

		for (argv_ind = 1; argv[optnum][argv_ind] != '\0'; argv_ind++) {
			switch (argv[optnum][argv_ind]) {
			case 'e':
				SQUEEZE_ARG(argv[optnum], argv_ind);
				if ((c = argv[optnum][argv_ind]) != '\0' &&
				    !isdigit(c)) {
					Etabc = c;
					SQUEEZE_ARG(argv[optnum], argv_ind);
				}
				if (isdigit(argv[optnum][argv_ind])) {
					Etabn = (int) strtol(&argv[optnum]
					    [argv_ind], (char **)NULL, 10);
					while (isdigit(argv[optnum][argv_ind]))
					    SQUEEZE_ARG(argv[optnum], argv_ind);
				}
				if (Etabn <= 0)
					Etabn = DEFTAB;
				argv_ind--;
				break;

			case 'i':
				SQUEEZE_ARG(argv[optnum], argv_ind);
				if ((c = argv[optnum][argv_ind]) != '\0' &&
				    !isdigit(c)) {
					Itabc = c;
					SQUEEZE_ARG(argv[optnum], argv_ind);
				}
				if (isdigit(argv[optnum][argv_ind])) {
					Itabn = (int) strtol(&argv[optnum]
					    [argv_ind], (char **)NULL, 10);
					while (isdigit(argv[optnum][argv_ind]))
					    SQUEEZE_ARG(argv[optnum], argv_ind);
				}
				if (Itabn <= 0)
					Itabn = DEFTAB;
				argv_ind--;
				break;


			case 'n':
				++Lnumb;
				SQUEEZE_ARG(argv[optnum], argv_ind);
				if ((c = argv[optnum][argv_ind]) != '\0' &&
				    !isdigit(c)) {
					Nsepc = c;
					SQUEEZE_ARG(argv[optnum], argv_ind);
				}
				if (isdigit(argv[optnum][argv_ind])) {
					Numw = (int) strtol(&argv[optnum]
					    [argv_ind], (char **)NULL, 10);
					while (isdigit(argv[optnum][argv_ind]))
					    SQUEEZE_ARG(argv[optnum], argv_ind);
				}
				argv_ind--;
				if (!Numw)
					Numw = NUMW;
				break;

			case 's':
				SQUEEZE_ARG(argv[optnum], argv_ind);
				if ((Sepc = argv[optnum][argv_ind]) == '\0')
					Sepc = '\t';
				else {
					Sepc = argv[optnum][argv_ind];
					SQUEEZE_ARG(argv[optnum], argv_ind);
				}
				argv_ind--;
				break;

			default:
				break;
			}
		}
		if (argv[optnum][0] == '-' && argv[optnum][1] == '\0') {
			REMOVE_ARG(argc, &argv[optnum]);
			optnum--;
		}
	}

	/* Now get the other options */
	while ((c = getopt(argc, argv, "0123456789adfFh:l:mo:prtw:"))
	    != EOF) {
		switch (c) {
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			Ncols *= 10;
			Ncols += c - '0';
			break;

		case 'a':
			aflg++;
			if (!Multi)
				Multi = c;
			break;

		case 'd':
			Dblspace = 2;
			break;

		case 'f':
			++Formfeed;
			++Pause;
			break;

		case 'h':
			Head = optarg;
			break;

		case 'l':
			if (strlen(optarg) != strspn(optarg, "0123456789"))
				usage(1);
			Length = (int) strtol(optarg, (char **)NULL, 10);
			break;

		case 'm':
			mflg++;
			Multi = c;
			break;

		case 'o':
			if (strlen(optarg) != strspn(optarg, "0123456789"))
				usage(1);
			Offset = (int) strtol(optarg, (char **)NULL, 10);
			break;

		case 'p':
			++Pause;
			break;

		case 'r':
			Report = 0;
			break;

		case 't':
			Margin = 0;
			break;

		case 'w':
			if (strlen(optarg) != strspn(optarg, "0123456789"))
				usage(1);
			Linew = (int) strtol(optarg, (char **)NULL, 10);
			break;

		case 'F':
#ifdef	XPG4
			++Formfeed;
#else
			fold++;
#endif
			break;

		case '?':
			usage(2);
			break;

		default :
			usage(2);
		}
	}

	/* Count the file names and strip options */
	for (i = 1; i < argc; i++) {
		/* Check for explicit stdin */
		if ((argv[i][0] == '-') && (argv[i][1] == '\0')) {
			argv[eargc++][0] = '\0';
			REMOVE_ARG(argc, &argv[i]);
			if (i < optind)
				optind--;
		}
	}
	for (i = eargc; optind < argc; i++, optind++) {
		argv[i] = argv[optind];
		eargc++;
	}

	/* Check options */
	if (Ncols == 0)
		Ncols = 1;

	if (mflg && (Ncols > 1)) {
		(void) fprintf(stderr,
		    gettext("pr: only one of either -m or -column allowed\n"));
		usage(1);
	}

	if (Ncols == 1 && fold)
		Multi = 'm';

	if (Length <= 0)
		Length = LENGTH;

	if (Length <= Margin)
		Margin = 0;

	Plength = Length - Margin/2;

	if (Multi == 'm')
		Ncols = eargc;

	switch (Ncols) {
	case 0:
		Ncols = 1;
		break;

	case 1:
		break;

	default:
		if (Etabn == 0)	/* respect explicit tab specification */
			Etabn = DEFTAB;
		if (Itabn == 0)
			Itabn = DEFTAB;
	}

	if ((Fcol = (foldinf *) malloc(sizeof (foldinf) * Ncols)) == NULL) {
		(void) fprintf(stderr, gettext("pr: malloc failed\n"));
		exit(1);
	}
	for (i = 0; i < Ncols; i++)
		Fcol[i].fold = Fcol[i].skip = 0;

	if (Linew == 0)
		Linew = Ncols != 1 && Sepc == 0 ? LINEW : 512;

	if (Lnumb) {
		int numw;

		if (Nsepc == '\t') {
			if (Itabn == 0)
				numw = Numw + DEFTAB - (Numw % DEFTAB);
			else
				numw = Numw + Itabn - (Numw % Itabn);
		} else {
			numw = Numw + ((isprint(Nsepc)) ? 1 : 0);
		}
		Linew -= (Multi == 'm') ? numw : numw * Ncols;
	}

	if ((Colw = (Linew - Ncols + 1)/Ncols) < 1)
		die("width too small");

	if (Ncols != 1 && Multi == 0) {
		Buflen = ((UNS) (Plength / Dblspace + 1)) *
		    (Linew + 1) * sizeof (CHAR);
#ifdef EUC
		if (wp._multibyte) {
			if (wp._scrw1 > 0)
				if ((eucbufw = (double) wp._eucw1 /
				    (double) wp._scrw1) > 0)
					if (eucbufw > 1)
						eucbuf = eucbufw;
					else
						eucbuf = 1;
			if (wp._scrw2 > 0)
				if ((eucbufw = (double) wp._eucw2 /
				    (double) wp._scrw2) > 0)
					if (eucbufw > eucbuf)
						eucbuf = eucbufw;
			if (wp._scrw3 > 0)
				if ((eucbufw = (double) wp._eucw3 /
				    (double) wp._scrw3) > 0)
					if (eucbufw > eucbuf)
						eucbuf = eucbufw;
			Buflen = (UNS) (Buflen * eucbuf + ROUNDUP);
		}
#endif
		Buffer = (CHAR *) getspace(Buflen);
		Bufptr = Bufend = &Buffer[Buflen];
		Colpts = (COLP) getspace((UNS) ((Ncols + 1) *
		    sizeof (*Colpts)));
		Colpts[0].c_lno = 0;
	}

	/* is stdin not a tty? */
	if (Ttyout && (Pause || Formfeed) && !ttyname(fileno(stdin)))
		Ttyin = fopen("/dev/tty", "r");

	return (eargc);
}


static	int
print(char *name)
{
	static	int	notfirst = 0;
	char	*date = NULL;
	char	*head = NULL;
	int	c;

	if (Multi != 'm' && mustopen(name, &Files[0]) == NULL)
		return (0);
	if (Multi == 'm' && Nfiles == 0 && mustopen(name, &Files[0]) == NULL)
		die("cannot open stdin");
	if (Buffer)
		(void) ungetc(Files->f_nextc, Files->f_f);
	if (Lnumb)
		Lnumb = 1;
	for (Page = 0; ; putpage()) {
		if (C == EOF && !(fold && Buffer))
			break;
		if (Buffer)
			nexbuf();
		Inpos = 0;
		if (get(0) == EOF)
			break;
		(void) fflush(stdout);
		if (++Page >= Fpage) {
			/* Pause if -p and not first page */
			if (Ttyout && Pause && !notfirst++) {
				PROMPT();	/* prompt with bell and pause */
				while ((c = getc(Ttyin)) != EOF && c != '\n')
					;
			}
			if (Margin == 0)
				continue;
			if (date == NULL)
				date = GETDATE();
			if (head == NULL)
				head = Head != NULL ? Head :
				    Nfiles < 2 ? Files->f_name : nulls;
			(void) printf("\n\n");
			Nspace = Offset;
			putspace();
			(void) printf(HEAD);
		}
	}
	C = '\0';
	return (1);
}


static	void
putpage()
{
	register	int	colno;

	if (fold) {
		foldpage();
		return;
	}
	for (Line = Margin / 2; ; (void) get(0)) {
		for (Nspace = Offset, colno = 0, Outpos = 0; C != '\f'; ) {
			if (Lnumb && (C != EOF) &&
			    (((colno == 0) && (Multi == 'm')) ||
			    (Multi != 'm'))) {
				if (Page >= Fpage) {
					putspace();
					(void) printf("%*ld%c", Numw, Buffer ?
					    Colpts[colno].c_lno++ :
					    Lnumb, Nsepc);

					/* Move Outpos for number field */
					Outpos += Numw;
					if (Nsepc == '\t')
						Outpos +=
						    DEFTAB - (Outpos % DEFTAB);
					else
						Outpos++;
				}
				++Lnumb;
			}
			for (Lcolpos = 0, Pcolpos = 0;
			    C != '\n' && C != '\f' && C != EOF;
			    (void) get(colno))
				(void) put(C);

			if ((C == EOF) || (++colno == Ncols) ||
			    ((C == '\n') && (get(colno) == EOF)))
				break;

			if (Sepc)
				(void) put(Sepc);
			else if ((Nspace += Colw - Lcolpos + 1) < 1)
				Nspace = 1;
		}

		if (C == EOF) {
			if (Margin != 0)
				break;
			if (colno != 0)
				(void) put('\n');
			return;
		}
		if (C == '\f')
			break;
		(void) put('\n');
		if (Dblspace == 2 && Line < Plength)
			(void) put('\n');
		if (Line >= Plength)
			break;
	}
	if (Formfeed)
		(void) put('\f');
	else
		while (Line < Length)
			(void) put('\n');
}


static	void
foldpage()
{
	register	int	colno;
	int	keep;
	int	i;
	static	int	sl;

	for (Line = Margin / 2; ; (void) get(0)) {
		for (Nspace = Offset, colno = 0, Outpos = 0; C != '\f'; ) {
			if (Lnumb && Multi == 'm' && foldcol) {
				if (!Fcol[colno].skip) {
					unget(colno);
					putspace();
					if (!colno) {
						for (i = 0; i <= Numw; i++)
							(void) printf(" ");
						(void) printf("%c", Nsepc);
					}
					for (i = 0; i <= Colw; i++)
						(void) printf(" ");
					(void) put(Sepc);
					if (++colno == Ncols)
						break;
					(void) get(colno);
					continue;
				} else if (!colno)
					Lnumb = sl;
			}

			if (Lnumb && (C != EOF) &&
			    ((colno == 0 && Multi == 'm') || (Multi != 'm'))) {
				if (Page >= Fpage) {
					putspace();
					if ((foldcol &&
					    Fcol[colno].skip && Multi != 'a') ||
					    (Fcol[0].fold && Multi == 'a') ||
					    (Buffer && Colpts[colno].c_skip)) {
						for (i = 0; i < Numw; i++)
							(void) printf(" ");
						(void) printf("%c", Nsepc);
						if (Buffer) {
							Colpts[colno].c_lno++;
							Colpts[colno].c_skip =
							    0;
						}
					}
					else
					(void) printf("%*ld%c", Numw, Buffer ?
					    Colpts[colno].c_lno++ :
					    Lnumb, Nsepc);
				}
				sl = Lnumb++;
			}
			for (Lcolpos = 0, Pcolpos = 0;
			    C != '\n' && C != '\f' && C != EOF;
			    (void) get(colno)) {
				if (put(C)) {
					unget(colno);
					Fcol[(Multi == 'a') ? 0 : colno].fold
					    = 1;
					break;
				} else if (Multi == 'a') {
					Fcol[0].fold = 0;
				}
			}
			if (Buffer) {
				alleof = 1;
				for (i = 0; i < Ncols; i++)
					if (!Fcol[i].eof)
						alleof = 0;
				if (alleof || ++colno == Ncols)
					break;
			} else if (C == EOF || ++colno == Ncols)
				break;
			keep = C;
			(void) get(colno);
			if (keep == '\n' && C == EOF)
				break;
			if (Sepc)
				(void) put(Sepc);
			else if ((Nspace += Colw - Lcolpos + 1) < 1)
				Nspace = 1;
		}
		foldcol = 0;
		if (Lnumb && Multi != 'a') {
			for (i = 0; i < Ncols; i++) {
				Fcol[i].skip = Fcol[i].fold;
				foldcol +=  Fcol[i].fold;
				Fcol[i].fold = 0;
			}
		}
		if (C == EOF) {
			if (Margin != 0)
				break;
			if (colno != 0)
				(void) put('\n');
			return;
		}
		if (C == '\f')
			break;
		(void) put('\n');
		(void) fflush(stdout);
		if (Dblspace == 2 && Line < Plength)
			(void) put('\n');
		if (Line >= Plength)
			break;
	}
	if (Formfeed)
		(void) put('\f');
	else while (Line < Length)
		(void) put('\n');
}


static	void
nexbuf()
{
	register	CHAR	*s = Buffer;
	register	COLP	p = Colpts;
	int	j;
	int	c;
	int	bline = 0;
#ifdef EUC
	int	bufcolw;

	if (wp._multibyte)
		bufcolw = (int)(Colw * eucbuf + ROUNDUP);
	else
		bufcolw = Colw;
#endif EUC

	if (fold) {
		foldbuf();
		return;
	}
	for (; ; ) {
		p->c_ptr0 = p->c_ptr = s;
		if (p == &Colpts[Ncols])
			return;
		(p++)->c_lno = Lnumb + bline;
		for (j = (Length - Margin)/Dblspace; --j >= 0; ++bline) {
			for (Inpos = 0; ; ) {
				if ((c = getc(Files->f_f)) == EOF) {
					for (*s = EOF; p <= &Colpts[Ncols]; ++p)
						p->c_ptr0 = p->c_ptr = s;
					balance(bline);
					return;
				}
#ifdef EUC
				if (ISPRINT(c, wp))
					++Inpos;
				if (Inpos <= bufcolw || c == '\n') {
#else
				if (isprint(c))
					++Inpos;
				if (Inpos <= Colw || c == '\n') {
#endif EUC
					*s = c;
					if (++s >= Bufend)
						die("page-buffer overflow");
				}
				if (c == '\n')
					break;
				switch (c) {
				case '\b':
					if (Inpos == 0)
						--s;

					/*FALLTHROUGH*/

				case ESC:
					if (Inpos > 0)
						--Inpos;
				}
			}
		}
	}
}


static	void
foldbuf()
{
	int	num;
	int	i;
	int	colno = 0;
	int	size = Buflen;
	char	*s;
	char	*d;
	register COLP	p = Colpts;

	for (i = 0; i < Ncols; i++)
		Fcol[i].eof = 0;
	d = Buffer;
	if (Bufptr != Bufend) {
		s = Bufptr;
		while (s < Bufend)
			*d++ = *s++;
		size -= (Bufend - Bufptr);
	}
	Bufptr = Buffer;
	p->c_ptr0 = p->c_ptr = Buffer;
	if (p->c_lno == 0) {
		p->c_lno = Lnumb;
		p->c_skip = 0;
	} else {
		p->c_lno = Colpts[Ncols-1].c_lno;
		p->c_skip = Colpts[Ncols].c_skip;
		if (p->c_skip)
			p->c_lno--;
	}
	if ((num = fread(d, 1, size, Files->f_f)) != size) {
		for (*(d+num) = EOF; (++p) <= &Colpts[Ncols]; ) {
			p->c_ptr0 = p->c_ptr = (d+num);
		}
		balance(0);
		return;
	}
	i = (Length - Margin) / Dblspace;
	do {
		(void) readbuf(&Bufptr, i, p++);
	} while (++colno < Ncols);
}


static	void
balance(int bline)	/* line balancing for last page */
{
	CHAR	*s = Buffer;
	register	COLP	p = Colpts;
	int	colno = 0;
	int	j;
	int	c;
	int	l;
	int	lines;

	if (!fold) {
		c = bline % Ncols;
		l = (bline + Ncols - 1)/Ncols;
		bline = 0;
		do {
			for (j = 0; j < l; ++j)
				while (*s++ != '\n')
					;
			(++p)->c_lno = Lnumb + (bline += l);
			p->c_ptr0 = p->c_ptr = s;
			if (++colno == c)
				--l;
		} while (colno < Ncols - 1);
	} else {
		lines = readbuf(&s, 0, 0);
		l = (lines + Ncols - 1)/Ncols;
		if (l > ((Length - Margin) / Dblspace)) {
			l = (Length - Margin) / Dblspace;
			c = Ncols;
		} else {
			c = lines % Ncols;
		}
		s = Buffer;
		do {
			(void) readbuf(&s, l, p++);
			if (++colno == c)
				--l;
		} while (colno < Ncols);
		Bufptr = s;
	}
}


static	int
readbuf(char **s, int lincol, COLP p)
{
	int	lines = 0;
	int	chars = 0;
	int	width;
	int	nls = 0;
	int	move;
	int	skip = 0;
	int	decr = 0;

	width = (Ncols == 1) ? Linew : Colw;
	while (**s != (char) EOF) {
		switch (**s) {
			case '\n':
				lines++; nls++; chars = 0; skip = 0;
				break;

			case '\b':
			case ESC:
				if (chars) chars--;
				break;

			case '\t':
				move = Itabn - ((chars + Itabn) % Itabn);
				move = (move < width-chars) ? move :
				    width-chars;
				chars += move;

			default:
				if (isprint(**s))
					chars++;
		}
		if (chars > width) {
			lines++;
			skip++;
			decr++;
			chars = 0;
		}
		if (lincol && lines == lincol) {
			(p+1)->c_lno = p->c_lno + nls;
			(++p)->c_skip = skip;
			if (**s == '\n') (*s)++;
			p->c_ptr0 = p->c_ptr = (CHAR *) *s;
			return (0);
		}
		if (decr)
			decr = 0;
		else
			(*s)++;
	}
	return (lines);
}


static	int
get(int colno)
{
	static	int	peekc = 0;
	register	COLP	p;
	register	FILS	*q;
	register	int	c;

	if (peekc) {
		peekc = 0;
		c = Etabc;
	} else if (Buffer) {
		p = &Colpts[colno];
		if (p->c_ptr >= (p+1)->c_ptr0)
			c = EOF;
		else if ((c = *p->c_ptr) != EOF)
			++p->c_ptr;
		if (fold && c == (char)EOF)
			Fcol[colno].eof = 1;
	} else if ((c =
		(q = &Files[Multi == 'a' ? 0 : colno])->f_nextc) == (char)EOF) {
		for (q = &Files[Nfiles]; --q >= Files && q->f_nextc == EOF; )
			;
		if (q >= Files)
			c = '\n';
	} else
		q->f_nextc = getc(q->f_f);
	if (Etabn != 0 && c == Etabc) {
		++Inpos;
		peekc = ETABS;
		c = ' ';
#ifdef  EUC
	} else if (isprint(c) && ISASCII(c))
#else
	} else if (isprint(c))
#endif EUC
		++Inpos;
	else
		switch (c) {
		case '\b':
		case ESC:
			if (Inpos > 0)
				--Inpos;
			break;

		case '\f':
			if (Ncols == 1)
				break;
			c = '\n';

			/*FALLTHROUGH*/

		case '\n':
		case '\r':
			Inpos = 0;
#ifdef EUC
			break;

		default  :
			if (NOTASCII(c)) {
				if (ISSET2(c)) {
					setswitch = 2;
					eucwcount = wp._eucw2;
					scrwcount = wp._scrw2;
					eucstart = ON;
				} else if (ISSET3(c)) {
					setswitch = 3;
					eucwcount = wp._eucw3;
					scrwcount = wp._scrw3;
					eucstart = ON;
				} else if (!setswitch) {
					setswitch = 1;
					eucwcount = wp._eucw1;
					scrwcount = wp._scrw1;
					eucstart = ON;
				}
				if (eucstart == ON)
					Inpos += scrwcount;
			}
#endif EUC
		}
	return (C = c);
}


static	int
put(int c)
{
	int	move = 0;
	int	width = Colw;
	int	sp = Lcolpos;

	if (fold && Ncols == 1)
		width = Linew;

	switch (c) {
	case ' ':
		/* If column not full or this is separator char */
		if ((!fold && Ncols < 2) || (Lcolpos < width) ||
		    ((Sepc == c) && (Lcolpos == width))) {
			++Nspace;
			++Lcolpos;
		}
		if (fold && sp == Lcolpos)
			if (Lcolpos >= width)
				return (1);

		return (0);

	case '\t':
		if (Itabn == 0)
			break;

		/* If column not full or this is separator char */
		if ((Lcolpos < width) ||
		    ((Sepc == c) && (Lcolpos == width))) {
			move = Itabn - ((Lcolpos + Itabn) % Itabn);
			move = (move < width-Lcolpos) ? move : width-Lcolpos;
			Nspace += move;
			Lcolpos += move;
		}
		if (fold && sp == Lcolpos)
			if (Lcolpos >= width)
				return (1);
		return (0);

	case '\b':
		if (Lcolpos == 0)
			return (0);
		if (Nspace > 0) {
			--Nspace;
			--Lcolpos;
			return (0);
		}
		if (Lcolpos > Pcolpos) {
			--Lcolpos;
			return (0);
		}

		/*FALLTHROUGH*/

	case ESC:
		move = -1;
		break;

	case '\n':
		++Line;

		/*FALLTHROUGH*/

	case '\r':
	case '\f':
		Pcolpos = 0;
		Lcolpos = 0;
		Nspace = 0;
		Outpos = 0;
#ifdef EUC
		setswitch = 0;
		eucstart = OFF;
#endif EUC

	default:
		move = (isprint(c) != 0);
#ifdef EUC
		if (NOTASCII(c)) {
			if (eucstart == ON)
				move = scrwcount;
			else
				move = 0;
			if (--eucwcount <= 0)
				setswitch = 0;
		}
#endif EUC
	}
	if (Page < Fpage)
		return (0);
	if (Lcolpos > 0 || move > 0)
		Lcolpos += move;

	putspace();

	/* If column not full or this is separator char */
	if ((!fold && Ncols < 2) || (Lcolpos <= width) ||
	    ((Sepc == c) && (Lcolpos > width))) {
		(void) putchar(c);
		Outpos += move;
		Pcolpos = Lcolpos;
#ifdef EUC
		if (eucstart == ON)
			eucstart = OFF;
#endif EUC
	}
#ifdef EUC
	else if (eucstart == ON) {
		eucstart = OFF;
		setswitch = 0;
		if (Lcolpos - scrwcount < Colw) {
			Nspace += (Colw + scrwcount - Lcolpos);
			putspace();
		}
	}
#endif EUC

	if (fold && Lcolpos > width)
		return (1);

	return (0);
}


static	void
putspace()
{
	int nc = 0;

	for (; Nspace > 0; Outpos += nc, Nspace -= nc) {
#ifdef XPG4
		/* XPG4:  -i:  replace multiple SPACE chars with tab chars */
		if ((Nspace >= 2 && Itabn > 0 &&
			Nspace >= (nc = Itabn - Outpos % Itabn)) && !fold) {
#else
		/* Solaris:  -i:  replace white space with tab chars */
		if ((Itabn > 0 && Nspace >= (nc = Itabn - Outpos % Itabn)) &&
			!fold) {
#endif
			(void) putchar(Itabc);
		} else {
			nc = 1;
			(void) putchar(' ');
		}
	}
}


static	void
unget(int colno)
{
	if (Buffer) {
		if (*(Colpts[colno].c_ptr-1) != '\t')
			--(Colpts[colno].c_ptr);
		if (Colpts[colno].c_lno)
			Colpts[colno].c_lno--;
	} else {
		if ((Multi == 'm' && colno == 0) || Multi != 'm')
			if (Lnumb && !foldcol)
				Lnumb--;
		colno = (Multi == 'a') ? 0 : colno;
		(void) ungetc(Files[colno].f_nextc, Files[colno].f_f);
		Files[colno].f_nextc = C;
	}
}


/*
 * Defer message about failure to open file to prevent messing up
 * alignment of page with tear perforations or form markers.
 * Treat empty file as special case and report as diagnostic.
*/

static	FILE *
mustopen(char *s, register FILS *f)
{
	char	*empty_file_msg = gettext("%s -- empty file");

	if (*s == '\0') {
		f->f_name = STDINNAME();
		f->f_f = stdin;
	} else if ((f->f_f = fopen(f->f_name = s, "r")) == NULL) {
		s = ffiler(f->f_name);
		s = strcpy((char *) getspace((UNS) strlen(s) + 1), s);
	}
	if (f->f_f != NULL) {
		if ((f->f_nextc = getc(f->f_f)) != EOF || Multi == 'm')
			return (f->f_f);
		(void) sprintf(s = (char *) getspace((UNS) strlen(f->f_name)
		    + 1 + (UNS) strlen(empty_file_msg)),
		    empty_file_msg, f->f_name);
		(void) fclose(f->f_f);
	}
	Error = 1;
	if (Report)
		if (Ttyout) {	/* accumulate error reports */
			Lasterr = Lasterr->e_nextp =
			    (ERR *) getspace((UNS) sizeof (ERR));
			Lasterr->e_nextp = NULL;
			Lasterr->e_mess = s;
		} else {	/* ok to print error report now */
			cerror(s);
			(void) putc('\n', stderr);
		}
	return ((FILE *) NULL);
}


static	ANY *
getspace(UNS n)
{
	ANY *t;

	if ((t = (ANY *) malloc(n)) == NULL)
		die("out of space");
	return (t);
}


static	void
die(char *s)
{
	++Error;
	errprint();
	cerror(s);
	(void) putc('\n', stderr);
	exit(1);

	/*NOTREACHED*/
}


static	void
errprint()	/* print accumulated error reports */
{
	(void) fflush(stdout);
	for (; Err != NULL; Err = Err->e_nextp) {
		cerror(Err->e_mess);
		(void) putc('\n', stderr);
	}
	done();
}


static	void
fixtty()
{
	struct stat sbuf;

	setbuf(stdout, obuf);
	if (signal(SIGINT, SIG_IGN) != SIG_IGN)
		(void) signal(SIGINT, onintr);
	if (Ttyout = ttyname(fileno(stdout))) {	/* is stdout a tty? */
		(void) stat(Ttyout, &sbuf);
		Mode = sbuf.st_mode;		/* save permissions */
		(void) chmod(Ttyout, (S_IREAD|S_IWRITE));
	}
}


static	void
onintr()
{
	++Error;
	errprint();
	_exit(1);
}


static	char *
GETDATE()	/* return date file was last modified */
{
	static	char	*now = NULL;
	static	struct	stat	sbuf;
	static	struct	stat	nbuf;

	if (Nfiles > 1 || Files->f_name == nulls) {
		if (now == NULL) {
			(void) time(&nbuf.st_mtime);
			(void) cftime(time_buf, gettext(FORMAT),
			    &nbuf.st_mtime);
			now = time_buf;
		}
		return (now);
	} else {
		(void) stat(Files->f_name, &sbuf);
		(void) cftime(time_buf, gettext(FORMAT), &sbuf.st_mtime);
		return (time_buf);
	}
}


static	char *
ffiler(char *s)
{
	static char buf[100];

	(void) sprintf(buf, gettext("can't open %s"), s);
	return (buf);
}


static	void
usage(int rc)
{
	(void) fprintf(stderr, gettext(
"usage: pr [-# [-w #] [-a]] [-e[c][#]] [-i[c][#]] [-drtfp] [-n[c][#]]  \\\n"
"          [-o #] [-l #] [-s[char]] [-h header] [-F] [+#] [file ...]\n\n"
"       pr [-m [-w #]] [-e[c][#]] [-i[c][#]] [-drtfp] [-n[c][#]] [-0 #] \\\n"
"          [-l #] [-s[char]] [-h header] [-F] [+#] file1 file2 ...\n"
));
	exit(rc);
}
