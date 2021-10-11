/*
 * (C) COPYRIGHT International Business Machines Corp. 1985, 1993
 * All Rights Reserved
 *
 * (c) Copyright 1990, 1991, 1992 OPEN SOFTWARE FOUNDATION, INC.
 * ALL RIGHTS RESERVED
 *
 * Copyright (c) 1994, 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 *
 */

#pragma ident	"@(#)printf.c	1.18	95/03/15 SMI"

#include <errno.h>
#include <stdio.h>
#include <locale.h>
#include <nl_types.h>
#include <stdlib.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#define	MAXSTR _POSIX2_LINE_MAX		/* max string to be output */

/*
 * This typedef is used as our internal representation of strings.
 * We need this to avoid problems with real \0 characters being
 * treated as string terminators. Yuck.
 */
typedef struct {
	char	*begin;
	char	*end;
	int	bail;
} String;

static void escwork(char *source, String *Dest);
static int doargs(String *Dest, String *Fmtptr, char *args, char *argv[]);
static void finishline(String *Dest, String *Fmtptr);
static void p_out(String *Dest, const char *format, ...);
static char *find_percent(String *s);
static void old_printf(char *argv[]);

static int error = 0;
static char outstr[MAXSTR + 1];		/* output string */
static String Outstr = { outstr, outstr, 0 };

int
main(int argc, char **argv)
{
	String Fmt;
	char *start;
	int argn;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	if (argc > 1 &&
	    strcmp(argv[1], "--") == 0) {	/* XCU4 "--" handling, sigh */
		argv++;
		argc--;
	}

	if (argv[1]) {
		Fmt.begin = argv[1];
		Fmt.end = argv[1] + strlen(argv[1]);
	} else {
		(void) fprintf(stderr,
		    gettext("Usage: printf format [argument...]\n"));
		exit(1);
	}

	/*
	 * Transform octal numbers and backslash sequences to the correct
	 * character representations and stores the resulting string back
	 * into Fmt.begin.
	 */
	escwork(argv[1], &Fmt);

	/*
	 * If no format specification, simply output the format string
	 */
	if (find_percent(&Fmt) == NULL) {
		(void) fwrite(Fmt.begin, Fmt.end - Fmt.begin, 1, stdout);
		exit(0);
	}

	/*
	 * Escape sequences have been translated.  Now process
	 * format arguments.
	 */

	start = Fmt.begin;
	argn = 2;

	while (argn < argc) {
		int rc;

		errno = 0;

		if ((rc = doargs(&Outstr, &Fmt, argv[argn], argv)) == 1) {
			/* ending format string is a string */
			Fmt.begin = start;
		} else if (rc == 2) {
			/* invalid conversion or containing % char(s) */
			break;
		} else if (rc == 3) {
			/* found a \c, suppress all further */
			break;
		} else
			argn++;
	}

	/*
	 * Check to see if 'format' is done. if not transfer the
	 * rest of the 'format' to output string.
	 */

	if (Fmt.begin != Fmt.end)
		finishline(&Outstr, &Fmt);

	(void) fwrite(Outstr.begin, Outstr.end - Outstr.begin, 1, stdout);
	return (error);
}



/*
 * 	escwork
 *
 * This routine transforms octal numbers and backslash sequences to the
 * correct character representations and stores the resulting string
 * in the String 'Dest'.
 *
 * The returned value is a character pointer to the last available position in
 * destination string, the function itself returns an indication of whether
 * or not it detected the 'bailout' character \c while parsing the string.
 */

static void
escwork(char *source,			/* pointer to source */
	String *Dest)			/* pointer to destination */
{
	char *destin;
	int j;
	int mbcnt = 0;
	wchar_t wd;

	/*
	 * Preserve the underlying string for the sake of '$' arguments.
	 */
	Dest->begin = strdup(source);
	Dest->bail = 0;		/* set to 1 when we hit the \c character */

	for (destin = Dest->begin; *source; source += (mbcnt > 0) ? mbcnt : 1) {
		mbcnt = mbtowc(&wd, source, MB_CUR_MAX);
		if (mbcnt == 1 && wd == '\\') {
			/*
			 * process escape sequences
			 */
			switch (*++source) {
			case 'a':	/* bell/alert */
				/*LINTED*/
				*destin++ = '\a';
				continue;
			case 'b':
				*destin++ = '\b';
				continue;
			case 'c':	/* no newline, nothing */
				Dest->end = destin;
				Dest->bail = 1;
				*destin = 0;
				return;
			case 'f':
				*destin++ = '\f';
				continue;
			case 'n':
				*destin++ = '\n';
				continue;
			case 'r':
				*destin++ = '\r';
				continue;
			case 't':
				*destin++ = '\t';
				continue;
			case 'v':
				*destin++ = '\v';
				continue;
			case '\\':
				*destin++ = '\\';
				continue;
			case '0': /* 0-prefixed octal chars */
			case '1': /* non-0-prefixed octal chars */
			case '2': case '3': case '4': case '5':
			case '6': case '7':
				/*
				 * the following 2 lines should not be
				 * necessary, but VSC allows for \0ddd
				 */
				if (*source == '0')
					source++;
				j = wd = 0;
				while ((*source >= '0' &&
					*source <= '7') && j++ < 3) {
					wd <<= 3;
					wd |= (*source++ - '0');
				}
				*destin++ = (char)wd;
				--source;
				continue;
			default:
				--source;
			}	/* end of switch */
		}
		mbcnt = wctomb(destin, wd);		/* normal character */
		destin += (mbcnt > 0) ? mbcnt : 1;
	}	/* end of for */

	Dest->end = destin;
	*destin = '\0';
}


/*
 *    doargs
 *
 * This routine does the actual formatting of the input arguments.
 *
 * This routine handles the format of the following form:
 *	%n$pw.df
 *		n:	argument number followed by $
 *		p:	prefix, zero or more of {- + # or blank}.
 *		w:	width specifier. It is optional.
 *		.:	decimal.
 *		d:	precision specifier.
 *                      A null digit string is treated as zero.
 *		f:	format xXioudfeEgGcbs.
 *
 * The minimum set required is "%f".  Note that "%%" prints one "%" in output.
 *
 * RETURN VALUE DESCRIPTION:
 *	0 	forms a valid conversion specification.
 *	1	the ending format string is a string.
 *	2	cannot form a valid conversion; or the string contains
 *		literal % char(s).
 *
 * NOTE: If a character sequence in the format begins with a % character,
 *	 but does not form a valid conversion specification, the doargs()
 *	 will pass the invalid format to the sprintf() and let it handle
 *	 the situation.
 */

static int
doargs(
	String *Dest,			/* destination string */
	String *Fmtptr,			/* format string */
	char *args,			/* argument to process */
	char *argv[])			/* full argument list */
{
	char tmpchar, *last;
	char *ptr;
	long lnum;
	double fnum;
	int percent;			/* flag for "%" */
	int width, prec, flag;
	char *fmt;

#define	FPLUS	2
#define	FMINUS	4
#define	FBLANK	8
#define	FSHARP	16
#define	DOTSEEN	64
#define	DIGITSEEN 128

	percent = 0;

	/*
	 *   "%" indicates a conversion is about to happen.  This section
	 *   parses for a "%"
	 */
	for (fmt = last = Fmtptr->begin; last < Fmtptr->end; last++) {
		if (!percent) {
			if (*last == '%') {
				percent++;
				fmt = last;
				flag = width = prec = 0;
			} else
				p_out(Dest, "%c", *last);
			continue;
		}

		/*
		 * '%' has been found check the next character for conversion.
		 */

		switch (*last) {
		case '%':
			p_out(Dest, "%c", *last);
			percent = 0;
			continue;
		case 'x':
		case 'X':
		case 'd':
		case 'o':
		case 'i':
		case 'u':
			if (*last == 'u') {
				if (*args == '\'' || *args == '"') {
					args++;
					(void) strtoul(args, &ptr, 10);
					lnum = (int)args[0];
				} else
					lnum = strtoul(args, &ptr, 0);
			} else {
				if (*args == '\'' || *args == '"') {
					args++;
					(void) strtol(args, &ptr, 10);
					lnum = (int)args[0];
				} else
					lnum = strtol(args, &ptr, 0);
			}
			if (errno) {  /* overflow, underflow or invalid base */
				(void) fprintf(stderr, "printf: %s: %s\n",
				    args, strerror(errno));
				error++;
			} else if (strcmp(args, ptr) == 0) {
				(void) fprintf(stderr, gettext(
				    "printf: %s expected numeric value\n"),
				    args);
				error++;
			} else if (*ptr != NULL) {
				(void) fprintf(stderr, gettext(
				    "printf: %s not completely converted\n"),
				    args);
				error++;
			}
			tmpchar = *(++last);
			*last = '\0';
			p_out(Dest, fmt, lnum);
			*last = tmpchar;
			break;
		case 'f':
		case 'e':
		case 'E':
		case 'g':
		case 'G':
			if (*args == '\'' || *args == '"') {
				args++;
				(void) strtod(args, &ptr);
				fnum = (int)args[0];
			} else
				fnum = strtod(args, &ptr);
			/*
			 * strtod() handles error situations somewhat different
			 * from strtoul() and strtol(), e.g., strtod() will set
			 * errno for incomplete conversion, but strtoul() and
			 * strtol() will not. The following error test order
			 * is used in order to have the same behaviour as for
			 * u, d, etc. conversions
			 */
			if (strcmp(args, ptr) == 0) {
				(void) fprintf(stderr, gettext(
				    "printf: %s expected numeric value\n"),
				    args);
				error++;
			} else if (*ptr != NULL) {
				(void) fprintf(stderr, gettext(
				    "printf: %s not completely converted\n"),
				    args);
				error++;
			} else if (errno) { /* overflow, underflow or EDOM */
				(void) fprintf(stderr, "printf: %s: %s\n",
				    args, strerror(errno));
				error++;
			}
			tmpchar = *(++last);
			*last = '\0';
			p_out(Dest, fmt, fnum);
			*last = tmpchar;
			break;
		case 'c':
			if (*args == '\0') {
				last++;		/* printf %c "" => no output */
				break;
			}
			tmpchar = *(++last);
			*last = '\0';
			p_out(Dest, fmt, *args);
			*last = tmpchar;
			break;
		case 's':
			tmpchar = *(++last);
			*last = '\0';
			p_out(Dest, fmt, args);
			*last = tmpchar;
			break;
		case 'b': {
			int pre = 0, post = 0;
			String Arg, *a = &Arg;
			char *begin, *end;

			/*
			 * XXX	Sigh - %b is -far- too complex.
			 */
			escwork(args, a);
			begin = a->begin;
			end = a->end;

			/*
			 * The 'precision' specifies the minimum number of
			 * chars to be eaten from the string.  We need to
			 * check for multibyte characters in case we truncate
			 * one in the middle.  Oops.
			 */
			if (flag & DOTSEEN) {
				char *p;
				int mbcnt, count;
				wchar_t wd;

				count = 0;
				/*LINTED [bogus used-before-set: 1094364]*/
				for (p = begin; p < end; p += mbcnt) {
					mbcnt = mbtowc(&wd, p, MB_CUR_MAX);
					if (mbcnt <= 0)
						mbcnt = 1;
					if (count + mbcnt > prec)
						end = p;
					count += mbcnt;
				}
			}

			/*
			 * The 'width' specifies the minimum width, padded
			 * with spaces
			 */
			if ((end - begin) < width) {
				if (flag & FMINUS) {
					/* left-justified */
					post = width - (end - begin);
				} else {
					/* right justified */
					pre = width - (end - begin);
				}
			}

			/*
			 * write it all out
			 */
			if (pre)
				p_out(Dest, "%*s", pre, "");
			while (begin < end)
				p_out(Dest, "%c", *begin++);
			if (post)
				p_out(Dest, "%*s", post, "");
			free(a->begin);

			if (a->bail) {
				/*
				 * escwork detected the "give up now"
				 * character, bailing at that point in
				 * the string.
				 */
				Fmtptr->begin = Fmtptr->end;
				return (3);
				/*NOTREACHED*/
			}
			last++;
		}
			break;

		default:	/* 0 flag, width or precision */
			if (isdigit(*last)) {
				int value = strtol(last, &ptr, 0);
				if (errno) {
					(void) fprintf(stderr,
					    "printf: %s: %s\n",
					    last, strerror(errno));
					error++;
				}
				flag |= DIGITSEEN;
				if (flag & DOTSEEN)
					prec = value;
				else
					width = value;
				if (width > MAXSTR) {
					(void) fprintf(stderr,
					    gettext("printf: line too long\n"));
					exit(++error);
				}
				last += ptr - last - 1;
				continue;
			}
			switch (*last) {
			case '-':
				flag |= FMINUS;
				continue;
			case '+':
				flag |= FPLUS;
				continue;
			case ' ':
				flag |= FBLANK;
				continue;
			case '#':
				flag |= FSHARP;
				continue;
			case '.':
				flag |= DOTSEEN;
				continue;
			case '$':
				/*
				 * This is only allowed for compatibility
				 * with the SVR4 base version of printf.
				 *
				 * Once we see that the format specification
				 * contains a '$', we know that it must be
				 * an 'old' usage of printf, so we simply
				 * discard all the work we've done so far,
				 * and behave exactly the same way the old
				 * printf used to do.
				 */
				if (flag == DIGITSEEN) {
					old_printf(argv);
					/*NOTREACHED*/
				}
				(void) fprintf(stderr,
				    gettext("printf: bad '$' argument\n"));
				error++;
				/*FALLTHROUGH*/
			default:
				tmpchar = *(++last);
				*last = '\0';
				p_out(Dest, fmt);
				*last = tmpchar;
				break;
			}
#undef	DIGITSEEN
#undef	DOTSEEN
#undef	FSHARP
#undef	FBLANK
#undef	FMINUS
#undef	FPLUS
		}

		Fmtptr->begin = last;

		return (0);
	} 	/* end of for */

	if (find_percent(Fmtptr) == NULL) {
		/*
		 * Check for the 'bailout' character ..
		 */
		if (Fmtptr->bail)
			return (3);
		/*
		 * the ending format string is a string
		 * fmtptr points to the end of format string
		 */
		Fmtptr->begin = last;
		return (1);
	} else {
		/*
		 * cannot form a valid conversion; or
		 * a string containing literal % char(s)
		 * fmtptr points to the end of format string
		 */
		Fmtptr->begin = last;
		return (2);
	}
}


/*
 *   finishline
 *
 *	This routine finishes processing the extra format specifications
 *
 *      If a character sequence in the format begins with a % character,
 *      but does not form a valid conversion specification, nothing will
 *      be written to output string.
 */

static void
finishline(
	String *Dest,	/* destination string */
	String *Fmtptr)	/* format string */
{
	char tmpchar, *last;
	int percent;				/* flag for "%" */
	int width;
	char *ptr;
	char *fmtptr;

	/*
	 * Check remaining format for "%".  If no "%", transfer
	 * line to output.  If found "%" replace with null for %s or
	 * %c, replace with 0 for all others.
	 */

	percent = 0;

	for (last = fmtptr = Fmtptr->begin; last != Fmtptr->end; last++) {
		if (!percent) {
			if (*last == '%') {
				percent++;
				fmtptr = last;
			} else
				p_out(Dest, "%c", *last);
			continue;
		}

		/*
		 * OK. '%' has been found check the next character
		 * for conversion.
		 */
		switch (*last) {
		case '%':
			p_out(Dest, "%%");
			percent = 0;
			continue;
		case 'x':
		case 'X':
		case 'd':
		case 'o':
		case 'i':
		case 'u':
			tmpchar = *(++last);
			*last = '\0';
			p_out(Dest, fmtptr, 0);
			*last = tmpchar;
			fmtptr = last;
			percent = 0;
			last--;
			break;
		case 'f':
		case 'e':
		case 'E':
		case 'g':
		case 'G':
			tmpchar = *(++last);
			*last = '\0';
			p_out(Dest, fmtptr, 0.0);
			*last = tmpchar;
			fmtptr = last;
			percent = 0;
			last--;
			break;
		case 'b':
		case 'c':
		case 's':
			*last = 's';
			tmpchar = *(++last);
			*last = '\0';
			p_out(Dest, fmtptr, "");
			*last = tmpchar;
			fmtptr = last;
			percent = 0;
			last--;
			break;
		default:	/* 0 flag, width or precision */
			if (isdigit(*last)) {
				width = strtol(last, &ptr, 0);
				if (errno) {
					(void) fprintf(stderr,
					    "printf: %s: %s\n",
					    last, strerror(errno));
					error++;
				}
				if (width > MAXSTR) {
					(void) fprintf(stderr,
					    gettext("printf: line too long\n"));
					exit(++error);
				}
				last += ptr - last - 1;
				continue;
			}
			switch (*last) {
			case '-':
			case '+':
			case ' ':
			case '#':
			case '.':
				continue;
			default:
				break;
			}
		}
	}
}

/*
 *   p_out
 *
 *	This routine checks if the current output line is longer than
 * 	LINE_MAX bytes. If so, outputs the current line and exits with
 *	non-zero value; otherwise, do the conversion. If the result
 *	of the conversion has no error, copy the result to the output
 *	buffer. If the result of the conversion plus the current output
 *	line is longer than 2048 bytes, the output will be truncated.
 *
 */
static void
p_out(String *s, const char *format, ...)
{
	char tmpstr[MAXSTR + 1];  /* temp. output string for one conversion */
	char *tmpptr;
	int rc = 0;
	int line;
	va_list ap;

	if ((line = s->end - s->begin) >= MAXSTR) {
		(void) fprintf(stderr, gettext("printf: line too long\n"));
		(void) printf("%s", outstr);
		exit(++error);
	}
	tmpptr = tmpstr;
	va_start(ap, format);
	rc = vsprintf(tmpptr, format, ap);
	if (rc < 0) {
		(void) fprintf(stderr, gettext("printf: bad conversion\n"));
		rc = 0;
		error++;
	} else if (errno != 0 &&
		errno != ERANGE && errno != EINVAL && errno != EDOM) {
		/*
		 * strtol(), strtoul() or strtod() should've reported
		 * the error if errno is ERANGE, EINVAL or EDOM
		 */
		perror("printf");
		error++;
	} else if ((rc + line) > MAXSTR)
		rc = MAXSTR - line;
	va_end(ap);
	s->end = (char *)memcpy(s->end, tmpptr, rc) + rc;
}

static char *
find_percent(String *s)
{
	int mbcnt;
	wchar_t wd;
	char *p;

	/*LINTED [bogus used-before-set: 1094364]*/
	for (p = s->begin; p != s->end; p += (mbcnt > 0) ? mbcnt : 1) {
		mbcnt = mbtowc(&wd, p, MB_CUR_MAX);
		if (mbcnt == 1 && wd == '%')
			return (p);
	}
	return (NULL);
}

#include <libgen.h>

/*
 * This is the printf from 5.0 -> 5.4
 * This version is only used when '$' format specifiers are detected
 * (in which case all bets are off w.r.t. the rest of format functionality)
 */
static void
old_printf(char *argv[])
{
	char *fmt;

	if ((fmt = strdup(argv[1])) == (char *)0) {
		(void) fprintf(stderr, gettext("malloc failed\n"));
		exit(1);
	}
	(void) strccpy(fmt, argv[1]);
	(void) printf(fmt, argv[2], argv[3], argv[4], argv[5],
		argv[6], argv[7], argv[8], argv[9],
		argv[10], argv[11], argv[12], argv[13],
		argv[14], argv[15], argv[16], argv[17],
		argv[18], argv[19], argv[20]);
	exit(0);
}
