#ident	"@(#)fmt.c	1.16	93/05/20 SMI" /* from UCB 2.1 07/01/81 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <wctype.h>
#include <widec.h>
#include <dlfcn.h>
#include <locale.h>
#include <sys/param.h>

/*
 * fmt -- format the concatenation of input files or standard input
 * onto standard output.  Designed for use with Mail ~|
 *
 * Syntax: fmt [ -width ] [ -c ] [ name ... ]
 * Author: Kurt Shoens (UCB) 12/7/78
 */

#define	NOSTR	((wchar_t *) 0)	/* Null string pointer for lint */

wchar_t	outbuf[BUFSIZ];			/* Sandbagged output line image */
wchar_t	*outp;				/* Pointer in above */
int	filler;				/* Filler amount in outbuf */

int	pfx;			/* Current leading blank count */
int	lineno;			/* Current input line */
int	mark;			/* Last place we saw a head line */
int	width = 72;		/* Width that we will not exceed */
int	nojoin = 0;		/* split lines only, don't join short ones */

enum crown_type	{c_none, c_reset, c_head, c_lead, c_fixup, c_body};
enum crown_type	crown_state;	/* Crown margin state */
int	crown_head;		/* The header offset */
int	crown_body;		/* The body offset */

wchar_t	*headnames[] = {
	L"To", L"Subject", L"Cc", L"cc", L"Bcc", L"bcc", 0};

int (*(split))();

/*
 * Drive the whole formatter by managing input files.  Also,
 * cause initialization of the output stuff and flush it out
 * at the end.
 */

main(int argc, char **argv)
{
	register FILE *fi;
	register int errs = 0;
	char sobuf[BUFSIZ];
	register char *cp;
	int nofile;
	char *locale;
	int csplit(), msplit();
	void _wckind_init();

	outp = NOSTR;
	lineno = 1;
	mark = -10;
	setbuf(stdout, sobuf);
	setlocale(LC_ALL, "");
	locale = setlocale(LC_CTYPE, "");
	if (strcmp(locale, "C") == 0) {
		split = csplit;
	} else {
		split = msplit;
		(void) _wckind_init();
	}
	if (argc < 2) {
single:
		fmt(stdin);
		oflush();
		exit(0);
	}
	nofile = 1;
	while (--argc) {
		cp = *++argv;
		if (setopt(cp))
			continue;
		nofile = 0;
		if ((fi = fopen(cp, "r")) == NULL) {
			perror(cp);
			errs++;
			continue;
		}
		fmt(fi);
		fclose(fi);
	}
	if (nofile)
		goto single;
	oflush();
	exit(errs);
	/* NOTREACHED */
}

/*
 * Read up characters from the passed input file, forming lines,
 * doing ^H processing, expanding tabs, stripping trailing blanks,
 * and sending each line down for analysis.
 */

fmt(FILE *fi)
{
	wchar_t linebuf[BUFSIZ], canonb[BUFSIZ];
	register wchar_t *cp, *cp2;
	register int col;
	wchar_t	c;

	c = getwc(fi);
	while (c != EOF) {
		/*
		 * Collect a line, doing ^H processing.
		 * Leave tabs for now.
		 */

		cp = linebuf;
		while (c != L'\n' && c != EOF && cp-linebuf < BUFSIZ-1) {
			if (c == L'\b') {
				if (cp > linebuf)
					cp--;
				c = getwc(fi);
				continue;
			}
			if (!(iswprint(c)) && c != L'\t') {
				c = getwc(fi);
				continue;
			}
			*cp++ = c;
			c = getwc(fi);
		}
		*cp = L'\0';

		/*
		 * Toss anything remaining on the input line.
		 */

		while (c != L'\n' && c != EOF)
			c = getwc(fi);
		/*
		 * Expand tabs on the way to canonb.
		 */

		col = 0;
		cp = linebuf;
		cp2 = canonb;
		while (c = *cp++) {
			if (c != L'\t') {
				col += scrwidth(c);
				if (cp2-canonb < BUFSIZ-1)
					*cp2++ = c;
				continue;
			}
			do {
				if (cp2-canonb < BUFSIZ-1)
					*cp2++ = L' ';
				col++;
			} while ((col & 07) != 0);
		}

		/*
		 * Swipe trailing blanks from the line.
		 */

		for (cp2--; cp2 >= canonb && *cp2 == L' '; cp2--);
		*++cp2 = '\0';
		prefix(canonb);
		if (c != EOF)
			c = getwc(fi);
	}
}

/*
 * Take a line devoid of tabs and other garbage and determine its
 * blank prefix.  If the indent changes, call for a linebreak.
 * If the input line is blank, echo the blank line on the output.
 * Finally, if the line minus the prefix is a mail header, try to keep
 * it on a line by itself.
 */

prefix(wchar_t line[])
{
	register wchar_t *cp, **hp;
	register int np, h;
	register int i;
	char	cbuf[BUFSIZ];

	if (line[0] == L'\0') {
		oflush();
		putchar('\n');
		if (crown_state != c_none)
			crown_state = c_reset;
		return;
	}
	for (cp = line; *cp == L' '; cp++);
	np = cp - line;

	/*
	 * The following horrible expression attempts to avoid linebreaks
	 * when the indent changes due to a paragraph.
	 */

	if (crown_state == c_none && np != pfx && (np > pfx || abs(pfx-np) > 8))
		oflush();
	/*
	 * since we only want to make sure cp points to a "From" line of the
	 * email, we don't have to alloc BUFSIZ * MB_LEN_MAX to cbuf
	 */
	wcstombs(cbuf, cp, (BUFSIZ - 1));
	if (h = ishead(cbuf))
		oflush(), mark = lineno;
	if (lineno - mark < 7 && lineno - mark > 0)
		/*
		 * make this check for 7 lines because some users like to put
		 * cc's on multiple lines
		 */
		for (hp = &headnames[0]; *hp != (wchar_t *) 0; hp++)
			if (ispref(*hp, cp)) {
				h = 1;
				oflush();
				break;
			}
	if (nojoin) {
		h = 1;
		oflush();
	}
	if (!h && (h = (*cp == L'.')))
		oflush();
	pfx = np;
	switch (crown_state) {
	case c_reset:
		crown_head = pfx;
		crown_state = c_head;
		break;
	case c_lead:
		crown_body = pfx;
		crown_state = c_body;
		break;
	case c_fixup:
		crown_body = pfx;
		crown_state = c_body;
		if (outp) {
			wchar_t s[BUFSIZ];

			*outp = L'\0';
			wscpy(s, &outbuf[crown_head]);
			outp = NOSTR;
			split(s);
		}
		break;
	}
	split(cp);
	if (h)
		oflush();
	lineno++;
}

/*
 * Split up the passed line into output "words" which are
 * maximal strings of non-blanks with the blank separation
 * attached at the end.  Pass these words along to the output
 * line packer.
 */

csplit(wchar_t line[])
{
	register wchar_t *cp, *cp2;
	wchar_t word[BUFSIZ];
	static const wchar_t *srchlist = (const wchar_t *) L".:!?";

	cp = line;
	while (*cp) {
		cp2 = word;

		/*
		 * Collect a 'word,' allowing it to contain escaped
		 * white space.
		 */

		while (*cp && !(iswspace(*cp))) {
			if (*cp == '\\' && iswspace(cp[1]))
				*cp2++ = *cp++;
			*cp2++ = *cp++;
		}

		/*
		 * Guarantee a space at end of line.
		 * Two spaces after end of sentence punctuation.
		 */

		if (*cp == L'\0') {
			*cp2++ = L' ';
			if (wschr(srchlist, cp[-1]) != NULL)
				*cp2++ = L' ';
		}
		while (iswspace(*cp))
			*cp2++ = *cp++;
		*cp2 = L'\0';
		pack(word);
	}
}

msplit(wchar_t line[])
{
	register wchar_t *cp, *cp2, prev;
	wchar_t word[BUFSIZ];
	static const wchar_t *srchlist = (const wchar_t *) L".:!?";

	cp = line;
	while (*cp) {
		cp2 = word;
		prev = *cp;

		/*
		 * Collect a 'word,' allowing it to contain escaped
		 * white space.
		 */

		while (*cp) {
			if (iswspace(*cp))
				break;
			if (_wckind(*cp) != _wckind(prev))
				if (wcsetno(*cp) != 0 || wcsetno(prev) != 0)
					break;
			if (*cp == '\\' && iswspace(cp[1]))
				*cp2++ = *cp++;
			prev = *cp;
			*cp2++ = *cp++;
		}

		/*
		 * Guarantee a space at end of line.
		 * Two spaces after end of sentence punctuation.
		 */

		if (*cp == L'\0') {
			*cp2++ = L' ';
			if (wschr(srchlist, cp[-1]) != NULL)
				*cp2++ = L' ';
		}
		while (iswspace(*cp))
			*cp2++ = *cp++;
		*cp2 = L'\0';
		pack(word);
	}
}

/*
 * Output section.
 * Build up line images from the words passed in.  Prefix
 * each line with correct number of blanks.  The buffer "outbuf"
 * contains the current partial line image, including prefixed blanks.
 * "outp" points to the next available space therein.  When outp is NOSTR,
 * there ain't nothing in there yet.  At the bottom of this whole mess,
 * leading tabs are reinserted.
 */

/*
 * Pack a word onto the output line.  If this is the beginning of
 * the line, push on the appropriately-sized string of blanks first.
 * If the word won't fit on the current line, flush and begin a new
 * line.  If the word is too long to fit all by itself on a line,
 * just give it its own and hope for the best.
 */

pack(wchar_t word[])
{
	register wchar_t *cp;
	register int s, t;

	if (outp == NOSTR)
		leadin();
	t = wscol(word);
	*outp = L'\0';
	s = wscol(outbuf);
	if (t+s <= width) {
		for (cp = word; *cp; *outp++ = *cp++);
		return;
	}
	if (s > filler) {
		oflush();
		leadin();
	}
	for (cp = word; *cp; *outp++ = *cp++);
}

/*
 * If there is anything on the current output line, send it on
 * its way.  Set outp to NOSTR to indicate the absence of the current
 * line prefix.
 */

oflush(void)
{
	if (outp == NOSTR)
		return;
	*outp = L'\0';
	tabulate(outbuf);
	outp = NOSTR;
}

/*
 * Take the passed line buffer, insert leading tabs where possible, and
 * output on standard output (finally).
 */

tabulate(wchar_t line[])
{
	register wchar_t *cp, *cp2;
	register int b, t;


	/* Toss trailing blanks in the output line */
	cp = line + wslen(line) - 1;
	while (cp >= line && *cp == L' ')
		cp--;
	*++cp = L'\0';
	/* Count the leading blank space and tabulate */
	for (cp = line; *cp == L' '; cp++);
	b = cp - line;
	t = b >> 3;
	b &= 07;
	if (t > 0)
		do
			putc('\t', stdout);
		while (--t);
	if (b > 0)
		do
			putc(' ', stdout);
		while (--b);
	while (*cp)
		putwc(*cp++, stdout);
	putc('\n', stdout);
}

/*
 * Initialize the output line with the appropriate number of
 * leading blanks.
 */

leadin()
{
	register int b;
	register wchar_t *cp;
	register int l;

	switch (crown_state) {
	case c_head:
		l = crown_head;
		crown_state = c_lead;
		break;

	case c_lead:
	case c_fixup:
		l = crown_head;
		crown_state = c_fixup;
		break;

	case c_body:
		l = crown_body;
		break;

	default:
		l = pfx;
		break;
	}
	filler = l;
	for (b = 0, cp = outbuf; b < l; b++)
		*cp++ = L' ';
	outp = cp;
}

/*
 * Is s1 a prefix of s2??
 */

ispref(wchar_t *s1, wchar_t *s2)
{

	while (*s1 != L'\0' && *s2 != L'\0')
		if (*s1++ != *s2++)
			return (0);
	return (1);
}

/*
 * Set an input option
 */

setopt(cp)
	register char *cp;
{
	static int ws = 0;

	if (*cp == '-') {
		if (cp[1] == 'c' && cp[2] == '\0') {
			crown_state = c_reset;
			return (1);
		}
		if (cp[1] == 's' && cp[2] == '\0') {
			nojoin = 1;
			return (1);
		}
		if (cp[1] == 'w' && cp[2] == '\0') {
			ws++;
			return (1);
		}
		width = atoi(cp+1);
	} else if (ws) {
		width = atoi(cp);
		ws = 0;
	} else
		return (0);
	if (width <= 0 || width >= BUFSIZ-2) {
		fprintf(stderr, "fmt:  bad width: %d\n", width);
		exit(1);
	}
	return (1);
}


#define	LIB_WDRESOLVE	"/usr/lib/locale/%s/LC_CTYPE/wdresolve.so"
#define	WCHKIND		"_wdchkind_"

static int	_wckind_c_locale();

static int	(*__wckind)() = _wckind_c_locale;
static void	*dlhandle = NULL;


void
_wckind_init()
{
	char	*locale;
	char	path[MAXPATHLEN + 1];


	if (dlhandle != NULL) {
		(void) dlclose(dlhandle);
		dlhandle = NULL;
	}

	locale = setlocale(LC_CTYPE, NULL);
	if (strcmp(locale, "C") == 0)
		goto c_locale;

	(void) sprintf(path, LIB_WDRESOLVE, locale);

	if ((dlhandle = dlopen(path, RTLD_LAZY)) != NULL) {
		__wckind = (int (*)(int))dlsym(dlhandle, WCHKIND);
		if (__wckind != NULL)
			return;
		(void) dlclose(dlhandle);
		dlhandle = NULL;
	}

c_locale:
	__wckind = _wckind_c_locale;
}


int
_wckind(wc)
wchar_t	wc;
{
	return (*__wckind) (wc);
}


static int
_wckind_c_locale(wc)
wchar_t	wc;
{
	int	ret;

	/*
	 * DEPEND_ON_ANSIC: L notion for the character is new in
	 * ANSI-C, k&r compiler won't work.
	 */
	if (iswascii(wc))
		ret = (iswalnum(wc) || wc == L'_') ? 0 : 1;
	else
		ret = wcsetno(wc) + 1;

	return (ret);
}
