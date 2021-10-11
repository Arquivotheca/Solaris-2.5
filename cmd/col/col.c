/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)col.c	1.10	94/11/17 SMI"	/* SVr4.0 1.7	*/
/*
 *	col - filter reverse carraige motions
 *
 */


#include <stdio.h>
#include <ctype.h>
#include <locale.h>
#include <limits.h>
#include <stdlib.h>
#include <getwidth.h>
#include <wctype.h>

#define	PL 256
#define	ESC '\033'
#define	RLF '\013'
#define	SI '\017'
#define	SO '\016'
#define	GREEK 0200
#define	LINELN 4096
#define	wcscrwidth(c)	(eucsw[wcsetno(c)])

wchar_t *page[PL];
wchar_t lbuff[LINELN], *line;
eucwidth_t eucwidth;
int eucsw[4], euccw1;
wchar_t ws_blank[2] = {' ', 0};
char esc_chars, underline, temp_off, smart;
int bflag, xflag, fflag, pflag;
int greeked;
int half;
int cp, lp;
int ll, llh, mustwr;
int pcp = 0;
char *pgmname;
char *strcpy();
static char *usage = "usage:\tcol [-bfxp]\n";

main(argc, argv)
	int argc; char **argv;
{
	int i, n;
	int opt;
	int greek;
	register int c;
	wchar_t wc;
	char byte;
	static char fbuff[BUFSIZ];

	setbuf(stdout, fbuff);
	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);
	getwidth(&eucwidth);
	eucsw[0] = 1;
	eucsw[1] = eucwidth._scrw1;
	eucsw[2] = eucwidth._scrw2;
	eucsw[3] = eucwidth._scrw3;
	euccw1 = eucwidth._eucw1;
	pgmname = argv[0];

	while ((opt = getopt(argc, argv, "bfxp")) != EOF)
		switch (opt) {
		case 'b':
			bflag++;
			break;
		case 'x':
			xflag++;
			break;
		case 'f':
			fflag++;
			break;
		case 'p':
			pflag++;
			break;
		case '?':
		default:
			(void) fprintf(stderr, gettext(usage));
			exit(2);
		}

	argc -= optind;
	if (argc >= 1) {
		(void) fprintf(stderr, gettext(usage));
		exit(2);
	}

	for (ll = 0; ll < PL; ll++)
		page[ll] = 0;

	smart = temp_off = underline = esc_chars = '\0';
	cp = 0;
	ll = 0;
	greek = 0;
	mustwr = PL;
	line = lbuff;

	while ((c = getwchar()) != EOF) {
				if (underline && temp_off && c > ' ') {
					outc(ESC);
					if (*line) line++;
					*line++ = 'X';
					*line = temp_off = '\0';
				}
		if (c != '\b')
			if (esc_chars) esc_chars = '\0';
		switch (c) {
		case '\n':
			if (underline && !temp_off) {
				if (*line) line++;
				*line++ = ESC;
				*line++ = 'Y';
				*line = '\0';
				temp_off = '1';
			}
			incr();
			incr();
			cp = 0;
			continue;

		case '\0':
			continue;

		case ESC:
			c = getwchar();
			switch (c) {
			case '7':	/* reverse full line feed */
				decr();
				decr();
				break;

			case '8':	/* reverse half line feed */
				if (fflag)
					decr();
				else {
					if (--half < -1) {
						decr();
						decr();
						half += 2;
					}
				}
				break;

			case '9':	/* forward half line feed */
				if (fflag)
					incr();
				else {
					if (++half > 0) {
						incr();
						incr();
						half -= 2;
					}
				}
				break;

			default:
				if (pflag)	{	/* pass through esc */
					outc(ESC);
					line++;
					*line = c;
					line++;
					*line = '\0';
					esc_chars = 1;
					if (c == 'X')  underline = 1;
					if (c == 'Y' && underline) underline =
						temp_off = '\0';
					if (c == ']') smart = 1;
					if (c == '[') smart = '\0';
					}
				break;
			}
			continue;

		case SO:
			greek = GREEK;
			greeked++;
			continue;

		case SI:
			greek = 0;
			continue;

		case RLF:
			decr();
			decr();
			continue;

		case '\r':
			cp = 0;
			continue;

		case '\t':
			cp = (cp + 8) & -8;
			continue;

		case '\b':
			if (esc_chars) {
				*line++ = '\b';
				*line = '\0';
			} else if (cp > 0) cp--;
			continue;

		case ' ':
			cp++;
			continue;

		default:
			if (iswprint(c)) {	/* if printable */
				if (!greek) {
					outc((wchar_t) c);
					cp += wcscrwidth(c);
				}
				/*
				 * EUC (apply SO only when there can
				 * be corresponding character in CS1)
				 */
				else if (iswascii(c)) {
					byte = (c | greek);
					n = mbtowc(&wc, &byte, 1);
					if (!iswcntrl(c) && !iswspace(c) &&
					    euccw1 == 1 && n == 1 &&
					    wcsetno(wc) == 1) {
						outc(wc);
						cp += wcscrwidth(wc);
					} else {
						outc((wchar_t) c);
						cp += wcscrwidth(c);
					}
				} else {
					outc((wchar_t) c);
					cp += wcscrwidth(c);
				}
			}
			continue;
		}
	}

	for (i = 0; i < PL; i++)
		if (page[(mustwr+i)%PL] != 0)
			emit(page[(mustwr+i) % PL], mustwr+i-PL);
	emit(ws_blank, (llh + 1) & -2);
	return (0);
}

outc(c)
	register wchar_t c;
{
	register int n, i;
	register int width, widthl, widthc;
	register wchar_t *p1;
	wchar_t c1;
	char esc_chars = '\0';
	if (lp > cp) {
		line = lbuff;
		lp = 0;
	}

	while (lp < cp) {
		if (*line != '\b')
			if (esc_chars) esc_chars = '\0';
			switch (*line)	{
			case ESC:
				line++;
				esc_chars = 1;
				break;
			case '\0':
				*line = ' ';
				lp++;
				break;
			case '\b':
/*				if ( ! esc_chars ) */
					lp--;
				break;
			default:
				lp += wcscrwidth(*line);
			}
		line++;
	}
	while (*line == '\b') {
		/*
		 * EUC (For a multi-column character, backspace characters
		 * are assumed to be used like "__^H^HXX", where "XX"
		 * represents a two-column character, and a backspace
		 * always goes back by one column.)
		 */
		for (n = 0; *line == '\b'; line++) {
			n++;
			lp--;
		}
		while (n > 0 && lp < cp) {
			i = *line++;
			i = wcscrwidth(i);
			n -= i;
			lp += i;
		}
	}
	while (*line == ESC) line += 6;
	widthc = wcscrwidth(c);
	widthl = wcscrwidth(*line);
	if (bflag || (*line == '\0') || *line == ' ') {
		if (*line == '\0' || widthl == widthc) {
			*line = c;
		} else if (widthl > widthc) {
			n = widthl - widthc;
			wsinsert(line, n);
			*line++ = c;
			for (i = 0; i < n; i++)
				*line++ = ' ';
			line = lbuff;
			lp = 0;
		} else {
			n = widthc - widthl;
			for (p1 = line+1; n > 0; n -= wcscrwidth(i))
				i = *p1++;
			*line = c;
			wscpy(line+1, p1);

		}
	} else {
		if (smart && (widthl == 1) && (widthc == 1)) {
			register wchar_t c1, c2, c3, c4, c5, c6, c7;
			c1 = *++line;
			*line++ = ESC;
			c2 = *line;
			*line++ = '[';
			c3 = *line;
			*line++ = '\b';
			c4 = *line;
			*line++ = ESC;
			c5 = *line;
			*line++ = ']';
			c6 = *line;
			*line++ = c;
			while (c1) {
				c7 = *line;
				*line++ = c1;
				c1 = c2;
				c2 = c3;
				c3 = c4;
				c4 = c5;
				c5 = c6;
				c6 = c7;
			}
		} else	{
			if ((widthl == 1) && (widthc == 1)) {
				register wchar_t c1, c2, c3;
				c1 = *++line;
				*line++ = '\b';
				c2 = *line;
				*line++ = c;
				while (c1) {
					c3 = *line;
					*line++ = c1;
					c1 = c2;
					c2 = c3;
				}
			} else {
				(width = widthc) > widthl ? widthc : widthl;
				for (i = 0; i < width; i += wcscrwidth(c1))
					c1 = *line++;
				wsinsert(line, width + (width - widthc + 1));
				for (i = 0; i < width; i++)
					*line++ = '\b';
				*line++ = c;
				for (i = widthc; i < width; i++)
					*line++ = ' ';
			}
		}
		lp = 0;
		line = lbuff;
	}
}

store(lno)
{
	void *malloc();

	lno %= PL;
	if (page[lno] != 0)
		free((char *)page[lno]);
	page[lno] = (wchar_t *)malloc((unsigned)(wslen(lbuff) + 2)
							* sizeof (wchar_t));
	if (page[lno] == 0) {
/*		fprintf(stderr, "%s: no storage\n", pgmname); */
		exit(2);
	}
	wscpy(page[lno], lbuff);
}

fetch(lno)
{
	register wchar_t *p;

	lno %= PL;
	p = lbuff;
	while (*p)
		*p++ = '\0';
	line = lbuff;
	lp = 0;
	if (page[lno])
		wscpy(line, page[lno]);
}

emit(s, lineno)
	wchar_t *s;
	int lineno;
{
	char esc_chars = '\0';
	static int cline = 0;
	register int ncp;
	register wchar_t *p;
	register char cshifted;
	char	chr[MB_LEN_MAX + 1];

	register int c, n;
	static int gflag = 0;

	if (*s) {
		if (gflag) {
			putchar(SI);
			gflag = 0;
		}
		while (cline < lineno - 1) {
			putchar('\n');
			pcp = 0;
			cline += 2;
		}
		if (cline != lineno) {
			putchar(ESC);
			putchar('9');
			cline++;
		}
		if (pcp)
			putchar('\r');
		pcp = 0;
		p = s;
		while (*p) {
			ncp = pcp;
			while (*p++ == ' ') {
				if ((++ncp & 7) == 0 && !xflag) {
					pcp = ncp;
					putchar('\t');
				}
			}
			if (!*--p)
				break;
			while (pcp < ncp) {
				putchar(' ');
				pcp++;
			}
			if (greeked) {
				if (wcsetno(*p) == 1 && euccw1 == 1) {
					n = wctomb(chr, *p);
					if (gflag != (*chr & GREEK) &&
					    *p != '\b' && n == 1 &&
					    isascii(*chr ^ (gflag ^ GREEK)) &&
					    !iscntrl(*chr ^ (gflag ^ GREEK)) &&
					    !isspace(*chr ^ (gflag ^ GREEK))) {
						if (gflag)
							putchar(SI);
						else
							putchar(SO);
						gflag ^= GREEK;
					}
				}
			}
			c = *p;
			if (greeked) {
				if (wcsetno((wchar_t) c) == 1 && euccw1 == 1) {
					n = wctomb(chr, (wchar_t) c);
					cshifted = (*chr ^ GREEK);
					if (isascii(cshifted) &&
					    !iscntrl(cshifted) &&
					    !isspace(cshifted))
						putchar (*chr & ~GREEK);
				} else
					putwchar(c);
			} else putwchar(c);
			if (c == '\b') {
				if (*(p-2) && *(p-2) == ESC)
				{
					pcp++;
				}
				else
					pcp--;
				}
			else
				pcp += wcscrwidth(c);
			p++;
		}
	}
}

incr()
{
	store(ll++);
	if (ll > llh)
		llh = ll;
	if (ll >= mustwr && page[ll%PL]) {
		emit(page[ll%PL], ll - PL);
		mustwr++;
		free((char *)page[ll%PL]);
		page[ll%PL] = 0;
	}
	fetch(ll);
}

decr()
{
	if (ll > mustwr - PL) {
		store(ll--);
		fetch(ll);
	}
}
wsinsert(s, n)
register wchar_t *s;
int n;
{
	register wchar_t *p1, *p2;


	p1 = s + wslen(s);
	p2 = p1 + n;
	while (p1 >= s)
		*p2-- = *p1--;
}
