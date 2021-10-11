/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)wc.c	1.12	94/10/11 SMI"	/* SVr4.0 1.5.1.3	*/
/*
**	wc -- word and line count
*/

#include	<stdio.h>
#include	<limits.h>
#include	<locale.h>
#include	<wctype.h>

#undef BUFSIZ
#define	BUFSIZ	4096
unsigned char	b[BUFSIZ];

FILE *fptr = stdin;
long	wordct;
long	twordct;
long	linect;
long	tlinect;
long	charct;
long	tcharct;
long	real_charct;
long	real_tcharct;

int cflag = 0, mflag = 0, lflag = 0, wflag = 0;

eucwidth_t width;

main(argc, argv)
char **argv;
{
	register unsigned char *p1, *p2;
	register unsigned int c;
	int	flag;
	int	i, token;
	int	status = 0;
	int	wid[4];
	int	size, wsize;
	wchar_t wc;
	unsigned char	mb[MB_LEN_MAX];
	unsigned char	*mbp;


	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)		/* Should be defined by cc -D */
#define	TEXT_DOMAIN	"SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	getwidth(&width);

	wid[0] = 1;
	wid[1] = width._eucw1;
	wid[2] = width._eucw2 + 1;
	wid[3] = width._eucw3 + 1;

	while ((flag = getopt(argc, argv, "cCmlw")) != EOF) {
		switch (flag) {
		case 'c':
			if (mflag)
				usage();

			cflag++;
			break;

		case 'C':
		case 'm':		/* POSIX.2 */
			if (cflag)
				usage();
			mflag++;
			break;

		case 'l':
			lflag++;
			break;

		case 'w':
			wflag++;
			break;

		default:
			usage();
			break;
		}
	}

	argc -= optind;
	argv = &argv[optind];

	/*
	 * If no flags set, use defaults
	 */
	if (cflag == 0 && mflag == 0 && lflag == 0 && wflag == 0) {
		cflag = 1;
		lflag = 1;
		wflag = 1;
	}

	i = 0;
	do {
		if (argc > 0 && (fptr = fopen(argv[i], "r")) == NULL) {
			fprintf(stderr, gettext(
				"wc: cannot open %s\n"), argv[i]);
			status = 2;
			continue;
		}

		p1 = p2 = b;
		linect = 0;
		wordct = 0;
		charct = 0;
		real_charct = 0;
		token = 0;
		for (;;) {
			if (p1 >= p2) {
				p1 = b;
				c = fread(p1, 1, BUFSIZ, fptr);
				if (c <= 0)
					break;
				charct += c;
				p2 = p1+c;
			}
			c = *p1++;
			real_charct++;
			if (ISASCII(c)) {
				if (isspace(c)) {
					if (c == '\n')
						linect++;
					token = 0;
					continue;
				}

				if (!token) {
					wordct++;
					token++;
				}
			} else {
				mbp = mb;
				*mbp++ = c;
				size = wsize = wid[csetno(c)];
				while (--size) {
					if (p1 >= p2) {
						p1 = b;
						c = fread(p1, 1, BUFSIZ, fptr);
						if (c <= 0)
							goto printwc;
						charct += c;
						p2 = p1+c;
					}
					*mbp++ = *p1++;
				}
				if ((mbtowc(&wc, mb, wsize) > 0) &&
				    iswspace(wc)) {
					token = 0;
					continue;
				}
				if (!token) {
					wordct++;
					token++;
				}
			}

		}
		/* print lines, words, chars */
printwc:
		wcp(charct, wordct, linect, real_charct);
		if (argc > 0) {
			printf(" %s\n", argv[i]);
		}
		else
			printf("\n");
		fclose(fptr);
		tlinect += linect;
		twordct += wordct;
		tcharct += charct;
		real_tcharct += real_charct;
	} while (++i < argc);

	if (argc > 1) {
		wcp(tcharct, twordct, tlinect, real_tcharct);
		printf(" total\n");
	}
	exit(status);
}

wcp(charct, wordct, linect, real_charct)
long charct;
long wordct;
long linect;
long real_charct;
{
	if (lflag)
		printf(" %7ld", linect);

	if (wflag)
		printf(" %7ld", wordct);

	if (cflag)
		printf(" %7ld", charct);
	else if (mflag)
		printf(" %7ld", real_charct);
}

usage()
{
	fprintf(stderr, gettext(
		"usage: wc [-c | -m | -C] [-lw] [file ...]\n"));
	exit(2);
}
