/*
 * grep - pattern matching program - combined grep, egrep, and fgrep.
 *	Based on MKS grep command, with XCU & Solaris mods.
 *
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 */

#ident	"@(#)grep.c	1.17	95/06/06 SMI"

/*
 *
 * Copyright 1985, 1992 by Mortice Kern Systems Inc.  All rights reserved.
 *
 * This Software is unpublished, valuable, confidential property of
 * Mortice Kern Systems Inc.  Use is authorized only in accordance
 * with the terms and conditions of the source licence agreement
 * protecting this Software.  Any unauthorized use or disclosure of
 * this Software is strictly prohibited and will result in the
 * termination of the licence agreement.
 *
 */


#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <regex.h>
#include <limits.h>
#include <sys/types.h>
#include <stdio.h>
#include <locale.h>
#include <widec.h>
#include <errno.h>
#include <unistd.h>
#include <widec.h>
#include <wctype.h>

#define	BSIZE		512		/* Size of block for -b */
#define	NLINE		LINE_MAX	/* Input line length  */
#define	BUFSIZE		(4*LINE_MAX) 	/* Input buffer size */
#define	M_CSETSIZE	256		/* XXX Hack! Fix this */

typedef	struct	PATTERN	{
	struct	PATTERN	*next;
	char	*pattern;		/* original pattern */
	wchar_t	*wpattern;		/* wide, lowercased pattern */
	regex_t	re;			/* compiled pattern */
}	PATTERN;
static wchar_t * mbstowcsdup(char *);
static char * wcstombsdup(wchar_t *);
static wchar_t * wcsstr(wchar_t *, wchar_t *);

static	PATTERN	*patterns;
static	char buffer_[NLINE+BUFSIZE+1];	/* line and IO buffer */
#define	buffer	(buffer_+NLINE)		/* just IO buffer */
static	char	inpline[NLINE];		/* input line */
static	int	regflags = 0;		/* regcomp options */
static	uchar_t	fgrep = 0;		/* Invoked as fgrep */
static	uchar_t	egrep = 0;		/* Invoked as egrep */
static	uchar_t	nvflag = 1;		/* Print matching lines */
static	uchar_t	cflag;			/* Count of matches */
static	uchar_t	errors;			/* Set if errors */
static	uchar_t	iflag;			/* Case insensitve matching */
static	uchar_t	hflag;			/* Supress printing of filename */
static	uchar_t	lflag;			/* Print file names of matches */
static	uchar_t	nflag;			/* Precede lines by line number */
static	uchar_t	bflag;			/* Preccede matches by block number */
static	uchar_t	sflag;			/* Suppress file error messages */
static	uchar_t	qflag;			/* Suppress standard output */
static	uchar_t	wflag;			/* Search for expression as a word */
static	uchar_t	Eflag;			/* Egrep or -E flag */
static	uchar_t	Fflag;			/* Fgrep or -F flag */
static	uchar_t	outfn;			/* Put out file name */
static  uchar_t   trunc;			/* Set if lines truncated */
static	char	*cmdname;

static void addfile(char *fn);
static void addpattern(char *s);
static void fixpatterns(void);
static	int grep(FILE *fp, char *fn);
static	void usage(void);
static	void bmgcomp(char *str, int len);
static	char *bmgexec(char *str, char *end);
static	char *memrchr(register char *ptr, register int c, register size_t len);
static	int	grep(FILE *, char *);

/*
 * mainline for grep
 */
int
main(argc, argv)
int argc;
register char *argv[];
{
	register char *ap;
	int matched = 0;
	int c;
	int fflag = 0;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN	"SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	/*
	 * Skip leading slashes
	 */
	cmdname = argv[0];
	if (ap = strrchr(cmdname, '/'))
		cmdname = ap + 1;

	ap = cmdname;
	/*
	 * Detect egrep/fgrep via command name, map to -E and -F options.
	 */
	if (*ap == 'e' || *ap == 'E') {
		regflags |= REG_EXTENDED;
		egrep++;
	} else
		if (*ap == 'f' || *ap == 'F') {
			fgrep++;
		}

	while ((c = getopt(argc, argv, "vwchilnbse:f:qxEFI")) != -1)
		switch (c) {
		case 'v':	/* POSIX: negate matches */
			nvflag = 0;
			break;

		case 'c':	/* POSIX: write count */
			cflag++;
			break;

		case 'i':	/* POSIX: ignore case */
			if (MB_CUR_MAX > 1) {
				iflag++;
			} else {
				regflags |= REG_ICASE;
			}

			break;

		case 'l':	/* POSIX: Write filenames only */
			lflag++;
			break;

		case 'n':	/* POSIX: Write line numbers */
			nflag++;
			break;

		case 'b':	/* Solaris: Write file block numbers */
			bflag++;
			break;

		case 's':	/* POSIX: No error msgs for files */
			sflag++;
			break;

		case 'e':	/* POSIX: pattern list */
			addpattern(optarg);
			break;

		case 'f':	/* POSIX: pattern file */
			fflag = 1;
			addfile(optarg);
			break;
		case 'h':	/* Solaris: supress printing of file name */
			hflag = 1;
			break;

		case 'q':	/* POSIX: quiet: status only */
			qflag++;
			break;

		case 'w':	/* Solaris: treat pattern as word */
			wflag++;
			break;

		case 'x':	/* POSIX: full line matches */
			regflags |= REG_ANCHOR;
			break;

		case 'E':	/* POSIX: Extended RE's */
			regflags |= REG_EXTENDED;
			Eflag++;
			break;

		case 'F':	/* POSIX: strings, not RE's */
			Fflag++;
			break;

#ifdef IFLAG_OPT		/* Should work. Turned off for KISS reasons */
		case 'I':	/* MKS: Use regex icase: slower! */
			/*
			 * Use the ignore case built into regexec.
			 * Normally, this is considerably slower,
			 * because regexec can't do a scan for a
			 * regmust pattern, so the bmg code here in
			 * grep can't function.  However, for a pattern
			 * including a non-alphabetic, the non-alpha
			 * becomes the regmust pattern, our bmg
			 * functions, and we don't have to perform
			 * the case translations at all, as we do
			 * in the -i case.  This is an order of
			 * magnitude faster than -i!
			 */
			regflags |= REG_ICASE;
			break;
#endif 	/* IFLAG_OPT */

		default:
			usage();
		}
	/*
	 * If we're invoked as egrep or fgrep we need to do some checks
	 */

	if (egrep || fgrep) {
		/*
		 * Use of -E or -F with egrep or fgrep is illegal
		 */
		if (Eflag || Fflag)
			usage();
		/*
		 * Don't allow use of wflag with egrep / fgrep
		 */
		if (wflag)
			usage();
		/*
		 *  Don't allow -q flag with egrep / fgrep
		 */
		if (qflag)
			usage();
		/*
		 * For Solaris the -s flag is equivalent to XCU -q
		 */
		if (sflag)
			qflag++;
		/*
		 * done with above checks - set the appropriate flags
		 */
		if (egrep)
			Eflag++;
		else			/* Else fgrep */
			Fflag++;
	}

	/*
	 * -E and -F flags are mutually exclusive - check for this
	 */
	if (Eflag && Fflag)
		usage();

	/*
	 * -c, -l and -q flags are mutually exclusive
	 * We have -c override -l like in Solaris.
	 * -q overrides -l & -c programmatically in grep() function.
	 */
	if (cflag && lflag)
		lflag = 0;

	argv += optind-1;
	argc -= optind-1;

	/*
	 * Convert -I into -i for fgrep
	 * Can't let regexec do it because we don't call regexec!
	 */
	if (Fflag && (regflags & REG_ICASE))
		iflag++;

	/*
	 * No -e or -f?  Make sure there is one more arg, use it as the pattern.
	 */
	if (patterns == NULL && !fflag) {
		if (argc < 2)
			usage();
		addpattern(argv[1]);
		argc--;
		argv++;
	}

	/* Compile Patterns */
	fixpatterns();

	/* Process all files: stdin, or rest of arg list */
	if (argc < 2) {
		matched = grep(stdin, gettext("(standard input)"));
	} else {
		if (argc > 2 && hflag == 0)
			outfn = 1;	/* Print filename on match line */
		for (argv++; *argv != NULL; argv++) {
			register FILE *fp;

			if ((fp = fopen(*argv, "r")) == NULL) {
				errors = 1;
				if (sflag)
					continue;
				(void) fprintf(stderr, gettext(
				    "%s: can't open \"%s\"\n"),
				    cmdname, *argv);
				continue;
			}
			matched |= grep(fp, *argv);
			(void) fclose(fp);
			if (ferror(stdout))
				break;
			if (trunc) {
				/*
				 * MKS printed an error here.
				 * We choose not to - just clear flag for now
				 */
				trunc = 0;
			}
		}
	}
	/*
	 * Return() here is used instead of exit
	 */

	fflush(stdout);

	if (errors)
		return (2);
	return (matched ? 0 : 1);
}

/*
 * Add a file of strings to the pattern list.
 */
static void
addfile(fn)
char *fn;
{
	register FILE *fp;
	register char *cp;
	register char *inbuf;
	register char *buf;
	unsigned bufsiz;

	/*
	 * Open the pattern file
	 */
	if ((fp = fopen(fn, "r")) == NULL) {
		(void) fprintf(stderr, gettext("%s: can't open \"%s\"\n"),
		    cmdname, fn);
		exit(2);
	}
	if ((buf = inbuf = malloc(bufsiz = NLINE)) == NULL) {
		(void) fprintf(stderr,
		    gettext("%s: out of memory\n"), cmdname);
		exit(2);
	}
	/*
	 * Read in the file, reallocing as we need more memory
	 */
	while (fgets(buf, NLINE, fp) != NULL) {
		cp = strchr(buf, 0);
		if (*--cp == '\n')
			*cp = '\0';
		if ((++cp - buf) == NLINE-1) {
			buf = inbuf;
			if ((inbuf = realloc(inbuf, bufsiz += NLINE)) == NULL) {
				(void) fprintf(stderr, gettext(
					"%s: out of memory\n"),
					cmdname);
				exit(2);
			}
			buf = inbuf + (cp - buf);
			continue;
		}
		if ((inbuf = realloc(inbuf, (++cp - inbuf))) == NULL) {
			(void) fprintf(stderr,
			    gettext("%s: out of memory\n"),
			    cmdname);
			exit(2);
		}
		addpattern(inbuf);
		if ((buf = inbuf = malloc(bufsiz = NLINE)) == NULL) {
			(void) fprintf(stderr,
			    gettext("%s: out of memory\n"),
			    cmdname);
			exit(2);
		}
	}
	free(inbuf);
	(void) fclose(fp);
}

/*
 * Add a string to the pattern list.
 */
static void
addpattern(s)
char *s;
{
	register PATTERN *pp;
	register char *np;

	/*
	 * Solaris wflag support: Add '<' '>' to pattern to select it as
	 * a word. Doesn't make sense with -F but we're Libertarian.
	 */
	if (wflag) {
		unsigned int	wordlen;
		char		*wordbuf;

		wordlen = strlen(s) + 5;
		if ((wordbuf = malloc(wordlen)) == NULL) {
			(void) fprintf(stderr,
			    gettext("%s: out of memory for word\n"), cmdname);
			exit(2);
		}
		(void) strcpy(wordbuf, "\\<");
		(void) strcat(wordbuf, s);
		(void) strcat(wordbuf, "\\>");
		s = wordbuf;
	}

	/*
	 * Now add the pattern
	 */
	for (;;) {
		np = strchr(s, '\n');
		if (np != NULL)
			*np = '\0';
		if ((pp = (PATTERN *)malloc(sizeof (PATTERN))) == NULL) {
			(void) fprintf(stderr, gettext(
			    "%s: out of memory for pattern %s\n"), cmdname, s);
			exit(2);
		}
		pp->pattern = s;
		pp->next = patterns;
		patterns = pp;
		if (np == NULL)
			break;
		s = np+1;
	}
}

/*
 * Fix patterns.
 * Must do after all arguments read, in case later -i option.
 */
static void
fixpatterns()
{
	register PATTERN *pp;

	for (pp = patterns; pp != NULL; pp = pp->next) {
		int rv;

		if ((pp->wpattern = mbstowcsdup(pp->pattern)) == NULL)
			exit(2);
		/* -i flag: ignore case.  Convert all patterns to lower case */
		if (iflag) {
			wchar_t *wp;

			for (wp = pp->wpattern; *wp != '\0'; wp++)
				*wp = towlower(*(wint_t *)wp);
		}

		/*
		 * fgrep: No regular expressions.  For non-fgrep, compile the
		 * regular expression, give an informative error message, and
		 * exit if it didn't compile.
		 */
		if (Fflag)
			continue;

		if ((rv = regwcomp(&pp->re, pp->wpattern, regflags)) != 0) {
			regerror(rv, &pp->re, inpline, sizeof (inpline));
			(void) fprintf(stderr,
			    gettext("%s: RE error in %ws %s\n"),
				cmdname, pp->wpattern, inpline);
			exit(2);
		}
	}
}

/*
 * find last occurrence of a character in a string
 */
static char *
memrchr(ptr, c, len)
register char *ptr;
register int c;
register size_t len;
{
	ptr += len;
	while (len--)
		if (*--ptr == c)
			return (ptr);
	return (NULL);
}

/*
 * Do grep on a single file.
 * Return true in any lines matched.
 *
 * We have two strategies:
 * The fast one is used when we have a single pattern with
 * a string known to occur in the pattern. We can then
 * do a BMG match on the whole buffer.
 * This is an order of magnitude faster.
 * Otherwise we split the buffer into lines,
 * and check for a match on each line.
 */
static int
grep(fp, fn)
FILE *fp;
char *fn;
{
	int fd = fileno(fp);			/* read file desc. */
	off_t offset = 0;			/* current read position */
	int fast;				/* BMG search whole buffer */
	ulong lineno;
	register int len;
	register char *ptr = buffer;		/* buffer pointer */
	char *end = buffer;			/* end of buffer */
	char *enl = buffer;			/* last newline +1 */
	char *bline, *eline;			/* begin and end of line */
	register PATTERN *pp;
	ulong matches = 0;			/* Number of matching lines */
	int needmore = 0;
	int bufsize;				/* buffer remaining to read */
	int rlen;				/* size of last read */
	int eofflag = 0;			/* read EOF */
	static wchar_t	*outline;		/* Widened line buffer */

	if (patterns == NULL)
		return (0);	/* no patterns to match -- just return */

	/*
	 * Do we need to copy the string to another buffer, and widen it
	 * and possibly lowercase it?
	 * Yes, if multibyte fgrep without -x; Yes if ignoring case.
	 */
	if (outline == NULL)
	if ((MB_CUR_MAX > 1 && Fflag && (regflags&REG_ANCHOR) == 0) || iflag)
		if ((outline =
		    (wchar_t *) malloc(sizeof (wchar_t)*NLINE)) == NULL) {
			(void) fprintf(stderr,
			    gettext("%s: out of memory for buffer\n"), cmdname);
			exit(2);
		}
	/*
	 * Decide if we are able to run the Boyer-Moore algorithm: set
	 * the fast flag if so.  We can't work fast for a bunch of reasons:
	 *  - ignoring case locally, rather than in regexec
	 *  - printing line numbers [need to find all line boundaries]
	 *  - more than one pattern
	 *  - negating the output, i.e. printing lines which don't match
	 *  - multibyte characters
	 *  - zero length patterns
	 *  - regular expression with no must match string.
	 */
	pp = patterns;
	fast = !iflag && !nflag && nvflag && pp->next == NULL;
	if (MB_CUR_MAX > 1) {
		fast = 0;
	}

	if (fast)
		if (Fflag)
			if ((len = strlen(pp->pattern)) == 0)
				fast = 0;
			else
				bmgcomp(pp->pattern, len);
		else if (pp->re.re_regmust != NULL) {
			char *must = wcstombsdup(pp->re.re_regmust);

			bmgcomp(must, strlen(must));
			/* Do not free must; saved in bmgcomp */
		} else
			fast = 0;

	lineno = 0;
	for (;;) {
		/*
		 * If at end of buffer, read a new one.
		 * Return position of first line in buffer_,
		 * position after last newline in enl.
		 */
		if (ptr >= enl || needmore) {
			needmore = 0;
			/* copy partial line to before IO buffer */
			len = end - ptr;
			if (len > NLINE) {
				len = 0;
				trunc++;
			}
			(void) memcpy(buffer - len, ptr, len);
			ptr = buffer - len;

			/* read next buffer, appending to partial line */
			for (rlen = 1, len = 0, bufsize = BUFSIZE;
			    bufsize > 0 && rlen > 0; bufsize -= rlen) {
				len += (rlen = eofflag ? 0 :
				    read(fd, buffer+len, bufsize));
				if (rlen < 0) {
					/*
					 * Removed fprintf for Solaris Comp XXX
					 */
					if (cflag) {
						if (outfn)
							(void) printf(
							    "%s:", fn);
						if (!qflag)
							(void) printf("%lu\n",
							    matches);
					}
					return (0);
				} else if (rlen == 0) {
					eofflag = 1;
					if (ptr == buffer || len != 0)
						break;
					/* partial last line */
					*(buffer+len) = '\n';
					len += 1;
				}
			}
			if (len == 0)
				break;
			else if (len < bufsize && *(buffer + len - 1) != '\n') {
				/*
				 * If we have reached the end of file
				 * and there is no trailing newline,
				 * add one so that later on we won't
				 * think the line was truncated.
				 */
				*(buffer + len) = '\n';
				len++;
			}
			offset += len;
			end = buffer + len;
			enl = memrchr(buffer, '\n', len);
			if (enl == NULL) {
				trunc++;
				enl = end;		/* truncate line */
			} else
				enl++;
		}

		/*
		 * Either find the next line boundary in the buffer, or
		 * do BMG search of whole buffer for pattern.  If found,
		 * then find the line boundaries for the matching part,
		 * and continue, as in the slow case.
		 */
		if (!fast) {
			/* retrieve next line from buffer */
			bline = ptr;
			eline = memchr(ptr, '\n', enl-ptr);
			if (eline == NULL) {
				needmore++;
				continue;
			}
			lineno++;
		} else {
			/* fast algorithm does BMG search on whole buffer */
			/* TODO: does this return spurious empty lines? */
			if ((bline = bmgexec(ptr, enl)) == NULL) {
				ptr = enl;
				continue;
			}
			if ((bline = memrchr(ptr, '\n', bline-ptr)) == NULL)
				bline = ptr;
			if ((eline = memchr(bline, '\n', enl-bline)) == NULL)
				eline = enl-1;
		}

		ptr = ++eline;
		len = eline - bline;	/* length of line */
		if (len > LINE_MAX && !Fflag) {
			len = LINE_MAX;	/* truncate if grep or egrep */
			trunc++;
		}
		eline = bline + len - 1; /* point at \n */
		*eline-- = '\0';
#if _F_TEXT
		if (*eline == '\r')
			*eline-- = '\0';
#endif
		/*
		 * The heart of the work -- this is where the matching occurs
		 * Use regexec for grep, grep -E
		 * Use strcmp for fgrep -x
		 * Use strstr or wcsstr for fgrep
		 * All these come in two cases:
		 *	- must ignore case; must do a character match
		 *	- byte match ok on raw input.
		 */
		if (outline != NULL) {
			register wchar_t *cp;
			int len;

			/*
			 * Ignore case, or multibyte where we care about
			 * characters, rather than bytes.
			 * Widen, and lowercase as required.
			 */

			/* Never match a line with invalid sequence */
			if ((len = mbstowcs(outline, bline,
					    sizeof (wchar_t)*NLINE)) == -1) {
				(void) fprintf(stderr, gettext(
		"%s: input file %s: line %d: invalid multibyte character\n"),
				    cmdname, fn, lineno);
				continue;
			}
			outline[len] = '\0';
			if (iflag)
				for (cp = outline; *cp != '\0'; cp++)
					*cp = towlower(*(wint_t *)cp);
			if (!Fflag) {			/* grep, egrep */
				for (pp = patterns; pp != NULL; pp = pp->next) {
					int rv;

					if ((rv = regwexec(&pp->re, outline,
					    0, (regwmatch_t *) NULL, 0)) ==
					    REG_OK)
						break;
					if (rv != REG_NOMATCH) {
						regerror(rv, &pp->re, inpline,
						    sizeof (inpline));
						(void) fprintf(stderr, gettext(
					"%s: input file \"%s\": line %d: %s\n"),
						cmdname, fn, lineno, inpline);
						exit(2);
					}
				}
			} else if (regflags&REG_ANCHOR) { /* fgrep -x */
				for (pp = patterns; pp != NULL; pp = pp->next)
					if (outline[0] == pp->wpattern[0] &&
					    wscmp(outline, pp->wpattern) == 0)
						break;
			} else {			/* fgrep */
				for (pp = patterns; pp != NULL;
				    pp = pp->next)
					if (wcsstr(outline, pp->wpattern)
					    != NULL)
						break;
			}
		} else

		if (!Fflag) {			/* grep, egrep */
			for (pp = patterns; pp != NULL; pp = pp->next) {
				int rv;

				if ((rv = regexec(&pp->re, bline,
				    0, (regmatch_t *)NULL, 0)) == REG_OK)
					break;
				switch (rv) {
				case REG_NOMATCH:
					break;
				case REG_ECHAR:
					(void) fprintf(stderr, gettext(
	    "%s: input file \"%s\": line %d: invalid multibyte character\n"),
					    cmdname, fn, lineno);
					break;
				default:
					regerror(rv, &pp->re, inpline,
					    sizeof (inpline));
					(void) fprintf(stderr, gettext(
				    "%s: input file \"%s\": line %d: %s\n"),
					    cmdname, fn, lineno, inpline);
					exit(2);
				}
			}
		} else if (regflags&REG_ANCHOR) { /* fgrep -x */
			for (pp = patterns; pp != NULL; pp = pp->next)
				if (bline[0] == pp->pattern[0] &&
				    strcmp(bline, pp->pattern) == 0)
					break;
		} else {			/* fgrep */
			for (pp = patterns; pp != NULL; pp = pp->next)
				if (strstr(bline, pp->pattern) != NULL)
					break;
		}
		if ((pp != NULL) == nvflag) {
			matches++;
			/*
			 * Handle q, l and c flags.
			 */
			if (qflag) {
				/* Position to end of matched line */
				/* eline points at last char of line */
				(void) lseek(fd, -(end-(eline+2)), SEEK_CUR);
				exit(0);	/* no need to continue */
			}
			if (lflag) {
				(void) printf("%s\n", fn);
				break;
			}
			if (!cflag) {
				if (outfn)
					(void) printf("%s:", fn);
				if (bflag)
					(void) printf("%lu:",
						(offset-(end-bline))/BSIZE);
				if (nflag)
					(void) printf("%lu:", lineno);
				(void) printf("%s\n", bline);
			}
			if (ferror(stdout))
				return (0);
		}
	}

	if (cflag) {
		if (outfn)
			(void) printf("%s:", fn);
		if (!qflag)
			(void) printf("%lu\n", matches);
	}
	return (matches != 0);
}

/*
 * usage message for grep
 */
static void
usage()
{
	(void) fprintf(stderr, gettext("Usage:\t%s"), cmdname);
	(void) fprintf(stderr, gettext(
	" %s[-c|-l|-q] [-bhinsv%sx] [file ...]\n"),
	(egrep || fgrep) ? "" : "[-E|-F] ",
	(egrep || fgrep) ? "" : "w");

	(void) fprintf(stderr, gettext("%s"), cmdname);
	(void) fprintf(stderr, gettext(
" %s[-c|-l|-q] [-bhinsv%sx] -e pattern... [-f pattern_file]...[file...]\n"),
	(egrep || fgrep) ? "" : "[-E|-F] ",
	(egrep || fgrep) ? "" : "w");

	(void) fprintf(stderr, gettext("%s"), cmdname);
	(void) fprintf(stderr, gettext(
" %s[-c|-l|-q] [-bhinsv%sx] [-e pattern]... -f pattern_file [file...]\n"),
	(egrep || fgrep) ? "" : "[-E|-F] ",
	(egrep || fgrep) ? "" : "w");
	exit(2);
	/* NOTREACHED */
}

static	int bmglen;		/* length of BMG pattern */
static	char *bmgpat;		/* BMG pattern */
static	short bmgtab[M_CSETSIZE];	/* BMG delta1 table */

/*
 * Compile literal pattern into BMG tables
 */
static void
bmgcomp(pat, len)
register char *pat;
int len;
{
	register int i;
	register short *tab = bmgtab;	/* delta1 table */

	bmglen = len;
	bmgpat = pat;

	for (i = 0; i < M_CSETSIZE; i++)
		tab[i] = len;
	len--;
	for (i = 0; i <= len; i++)
		tab[(uchar_t)*pat++] = len-i;
}

/*
 * BMG search.
 */
static char *
bmgexec(str, end)
char *str;			/* start of string */
char *end;			/* end+1 of string */
{
	register int t;
	register char *k, *s, *p;
	register short *delta;
	char *pat;
	int plen;

	pat = bmgpat;
	plen = bmglen;
	delta = bmgtab;
	k = str + plen - 1;
	if (plen <= 1)
		return (memchr(str, pat[0], end-str));
	for (;;) {
		/* inner loop, should be most optimized */
		while (k < end && (t = delta[(uchar_t)*k]) != 0)
			k += t;
		if (k >= end)
			return (NULL);
		for (s = k, p = pat+plen-1; *--s == *--p; )
			if (p == pat)
				return (s);
		k++;
	}
	/*NOTREACHED*/
}

/*
 * Solaris port - following functions are typical MKS functions written
 * to work for Solaris.
 */

static wchar_t *
mbstowcsdup(s)
char *s;
{
	int n;
	wchar_t *w;

	n = strlen(s) + 1;
	if ((w = (wchar_t *)malloc(n * sizeof (wchar_t))) == NULL)
		return (NULL);

	if (mbstowcs(w, s, n) == -1)
		return (NULL);
	return (w);

}

extern void *_loaded_coll_;		/* Defined in libc regex code */

static char *
wcstombsdup(wchar_t *w)
{
	int len, n = 0;
	char *mb, *mbp;
	wchar_t *wp = w;

	/*
	 * This routine has been modified for the wide char optimization in
	 * regwcomp.
	 */
	while (*wp++ != 0)		/* Determine length - can't use wslen */
		n++;

	n++;				/* Room for null */
	len = n;

	/* Fetch memory for worst case string length */
	n *= MB_CUR_MAX;
	if ((mb = (char *)malloc(n)) == NULL) {
		return (NULL);
	}

	/*
	 * Convert the string
	 * If no collation table than w is not true wide char string,
	 * just copy characters directly to mb.
	 */
	if (_loaded_coll_ == NULL) {		/* No collation table loaded */
		mbp = mb;
		while (len--)
			*mbp++ = *w++;
	} else if ((n = wcstombs(mb, w, n)) == -1) {
		int saverr = errno;

		free(mb);
		errno = saverr;
		return (0);
	}

	/* Shrink the string down */
	if ((mb = (char *)realloc(mb, strlen(mb)+1)) == NULL)  {
		return (NULL);
	}
	return (mb);
}

/*
 *	wcstr() locates the first occurrence in the string as1 of
 *	the sequence of characters, excluding the terminating null
 *	character, in the string as2. wcstr() returns a pointer
 *	to the located string, or a null pointer if the string is
 *	not found. If as2 is "", the function returns as1.
 */
static wchar_t *
wcsstr(as1, as2)
	wchar_t *as1, *as2;
{
	register const wchar_t *s1, *s2;
	register const wchar_t *tptr;
	register wchar_t c;

	s1 = as1;
	s2 = as2;

	if (s2 == NULL || *s2 == '\0')
		return ((wchar_t *)s1);
	c = *s2;

	while (*s1)
		if (*s1++ == c) {
			tptr = s1;
			while ((c = *++s2) == *s1++ && c);
			if (c == 0)
				return ((wchar_t *)tptr - 1);
			s1 = tptr;
			s2 = as2;
			c = *s2;
		}
	    return (NULL);
}
