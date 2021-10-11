/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)fgrep.c	1.10	95/07/23 SMI"	/* SVr4.0 1.13	*/
/*	Copyright (c) 1987, 1988 Microsoft Corporation	*/
/*	  All Rights Reserved	*/

/*	This Module contains Proprietary Information of Microsoft  */
/*	Corporation and should be treated as Confidential.	   */
/*
 * fgrep -- print all lines containing any of a set of keywords
 *
 *	status returns:
 *		0 - ok, and some matches
 *		1 - ok, but no matches
 *		2 - some error
 */

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <locale.h>
#ifdef  EUC
#include <euc.h>
#endif  EUC

#ifdef 	EUC
#include        <getwidth.h>
#define	Wgetwidth()	{getwidth(&WW); WW._eucw2++; WW._eucw3++;}
eucwidth_t WW;
#define WIDTH1  WW._eucw1
#define WIDTH2  WW._eucw2
#define WIDTH3  WW._eucw3
#define MULTI_BYTE      WW._multibyte
#define GETONE(lc, p)   cw = ISASCII(lc = *p++) ? 1 :                   \
                             (ISSET2(lc) ? WIDTH2 :                     \
                             (ISSET3(lc) ? WIDTH3 : WIDTH1 ));          \
                        if(--cw > --ccount) {                           \
                                cw -= ccount;                           \
                                while(ccount--)                         \
                                        lc = (lc << 7) | ((*p++) & 0177); \
                                if (p == &buf[2*BUFSIZ]) p = buf;       \
                                if (p > &buf[BUFSIZ]) {                 \
					p = &buf[BUFSIZ];		\
                                        if ((ccount = fread(p, sizeof (char), \
					    &buf[2*BUFSIZ] - p, fptr)) <= 0) break; \
                                }                                       \
                                else if ((ccount = fread(p, \
				    sizeof (char),  BUFSIZ, fptr)) \
							     <= 0) break; \
                                blkno += (long)ccount;                  \
                        }                                               \
                        ccount -= cw;                                   \
                        while(cw--)                                     \
                                lc = (lc << 7) | ((*p++) & 0177)
#endif	EUC

/* The same() macro and letter() function were inserted to allow for 
   the -i option work for the multi-byte environment.
*/
#ifdef	EUC
wchar_t letter();
#define same(a,b) (a == b || iflag && (!MULTI_BYTE || ISASCII(a)) && (a ^ b) \
		== ' ' && letter(a) == letter(b))
#else 
#define letter(a) (isupper(a) ? tolower(a) : (a))
#define same(pat, src) ((pat == src) || (iflag && ((pat ^ src) == ' ') && \
			pat == letter(src)))
#endif	EUC

#define	MAXSIZ 6000
 
#define QSIZE 400
struct words {
#ifdef EUC
	wchar_t inp;
#else
	char 	inp;
#endif EUC
	char	out;
	struct	words *nst;
	struct	words *link;
	struct	words *fail;
} w[MAXSIZ], *smax, *q;

FILE *fptr;
long	lnum;
int	bflag, cflag, lflag, fflag, nflag, vflag, xflag, eflag, sflag;
int	hflag, iflag;
int	retcode = 0;
int	nfile;
long	blkno;
int	nsucc;
long	tln;
FILE	*wordf;
char	*argptr;
extern	char *optarg;
extern	int optind;
void exit();

main(argc, argv)
int argc;
char **argv;
{
	register c;
	char *usage;
	int errflg = 0;
	usage = "[ -bchilnsvx ] [ -e exp ] [ -f file ] [ strings ] [ file ] ...";

	(void)setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	while(( c = getopt(argc, argv, "hybcie:f:lnvxs")) != EOF)
		switch(c) {

		case 's':
			sflag++;
			continue;
		case 'h':
			hflag++;
			continue;
		case 'b':
			bflag++;
			continue;

		case 'i':
		case 'y':
			iflag++;
			continue;

		case 'c':
			cflag++;
			continue;

		case 'e':
			eflag++;
			argptr = optarg;
			continue;

		case 'f':
			fflag++;
			wordf = fopen(optarg, "r");
			if (wordf==NULL) {
				fprintf(stderr, 
					gettext("fgrep: can't open %s\n"), optarg);
				exit(2);
			}
			continue;

		case 'l':
			lflag++;
			continue;

		case 'n':
			nflag++;
			continue;

		case 'v':
			vflag++;
			continue;

		case 'x':
			xflag++;
			continue;

		case '?':
			errflg++;
	}

	argc -= optind;
	if (errflg || ((argc <= 0) && !fflag && !eflag)) {
		printf(gettext("usage: fgrep %s\n"),gettext(usage));
		exit(2);
	}
	if ( !eflag  && !fflag ) {
		argptr = argv[optind];
		optind++;
		argc--;
	}

#ifdef 	EUC
        Wgetwidth();
#endif	EUC

	cgotofn();
	cfail();
	nfile = argc;
	argv = &argv[optind];
	if (argc<=0) {
		if (lflag) exit(1);
		execute((char *)NULL);
	}
	else
		while ( --argc >= 0 ) {
			execute(*argv);
			argv++;
		}
	exit(retcode != 0 ? retcode : nsucc == 0);
}

execute(file)
char *file;
{
	register char *p;
	register struct words *c;
	register ccount;
	char buf[2*BUFSIZ];
	int failed;
	char *nlp;
#ifdef	EUC
        register wchar_t lc;
        int cw;
#endif	EUC

	if (file) {
		if ((fptr = fopen(file, "r")) == NULL) {
			fprintf(stderr, 
				gettext("fgrep: can't open %s\n"), file);
			retcode = 2;
			return;
		}
	}
	else
		fptr = stdin;
	ccount = 0;
	failed = 0;
	lnum = 1;
	tln = 0;
	blkno = 0;
	p = buf;
	nlp = p;
	c = w;
	for (;;) {
#ifndef	EUC
		if (--ccount <= 0) {
#else
                if (ccount <= 0) {
#endif	EUC
			if (p == &buf[2*BUFSIZ]) p = buf;
			if (p > &buf[BUFSIZ]) {
				if ((ccount = fread(p, sizeof(char), 
				       &buf[2*BUFSIZ] - p, fptr)) <= 0) break;
			}
			else if ((ccount = fread(p, sizeof(char), BUFSIZ, fptr)) <= 0) 
				break;
			blkno += (long)ccount;
		}
#ifndef	EUC
		nstate:
			if (same(c->inp, *p)) {
#else
			GETONE(lc, p);
                nstate:
                        if (same(c->inp,lc)) {
#endif	EUC
				c = c->nst;
			}
			else if (c->link != 0) {
				c = c->link;
				goto nstate;
			}
			else {
				c = c->fail;
				failed = 1;
				if (c==0) {
					c = w;
					istate:
#ifndef	EUC
					if (same(c->inp,*p)) { 	
#else
                                        if (same(c->inp,lc)) {
#endif	EUC
						c = c->nst;
					}
					else if (c->link != 0) {
						c = c->link;
						goto istate;
					}
				}
				else goto nstate;
			}
		if (c->out) {
#ifndef	EUC
			while (*p++ != '\n') {
				if (--ccount <= 0) {
#else
                        while (lc != '\n') {
                                if (ccount <= 0) {
#endif	EUC
					if (p == &buf[2*BUFSIZ]) p = buf;
					if (p > &buf[BUFSIZ]) {
						if ((ccount = fread(p, sizeof(char), 
						   &buf[2*BUFSIZ] - p, fptr)) <= 0) break;
					}
					else if ((ccount = fread(p, sizeof(char), BUFSIZ, 
						fptr)) <= 0) break;
					blkno += (long)ccount;
				}
#ifdef	EUC
                                GETONE(lc, p);
#endif	EUC
			}
			if ( (vflag && (failed == 0 || xflag == 0)) || 
			     (vflag == 0 && xflag && failed) )
				goto nomatch;
	succeed:	nsucc = 1;
			if (cflag) tln++;
			else if (sflag)
				;	/* ugh */
			else if (lflag) {
				printf("%s\n", file);
				fclose(fptr);
				return;
			}
			else {
				if (nfile > 1 && !hflag) printf("%s:", file);
				if (bflag) 
					printf("%d:", (blkno-(long)(ccount-1))/BUFSIZ);
				if (nflag) 
					printf("%ld:", lnum);
				if (p <= nlp) {
					while (nlp < &buf[2*BUFSIZ]) putchar(*nlp++);
					nlp = buf;
				}
				while (nlp < p) putchar(*nlp++);
			}
	nomatch:	lnum++;
			nlp = p;
			c = w;
			failed = 0;
			continue;
		}
#ifndef	EUC
                if (*p++ == '\n')                      
#else
                if (lc == '\n')
#endif	EUC
			if (vflag) goto succeed;
			else {
				lnum++;
				nlp = p;
				c = w;
				failed = 0;
			}
	}
	fclose(fptr);
	if (cflag) {
		if ((nfile > 1) && !hflag)
			printf("%s:", file);
		printf("%ld\n", tln);
	}
}


#ifdef	EUC
wchar_t
#endif	EUC
getargc()
{
			/* appends a newline to shell quoted argument list so */
			/* the list looks like it came from an ed style file  */
#ifndef	EUC
        register c;                                    
#else
        register wchar_t c;
        int cw;
	register int b;
#endif	EUC
	static int endflg;


#ifndef	EUC
	if (wordf)
		return(getc(wordf));
	if (endflg) 
		return(EOF);
	if ((c = *argptr++) == '\0') {
		endflg++;
		return('\n');
	}
	return iflag ? letter(c) : c;

#else
	if (wordf) {
		if((b = getc(wordf)) == EOF)
			return(EOF);
                cw = ISASCII(c = (wchar_t) b) ? 
		     1 : (ISSET2(c) ? WIDTH2 : (ISSET3(c) ? WIDTH3 : WIDTH1 ));
                while(--cw) {
			if((b = getc(wordf)) == EOF)
				return(EOF);
                        c = (c << 7) | (b & 0177);
		}
		return (c);
	} 

	if (endflg) 
		return(EOF);

	{
        	cw = ISASCII(c = *argptr++) ? 
		     1 : (ISSET2(c) ? WIDTH2 : (ISSET3(c) ? WIDTH3 : WIDTH1 ));
	
        	while(--cw)
                	c = (c << 7) | ((*argptr++) & 0177);
        	if (c == '\0') {
			endflg++;
			return('\n');
		}
	}
	return iflag ? letter(c) : c;

#endif	EUC

}

cgotofn() {
	register c;
	register struct words *s;

	s = smax = w;
nword:	for(;;) {
		c = getargc();
		if (c==EOF)
			return;
		if (c == '\n') {
			if (xflag) {
				for(;;) {
					if (s->inp == c) {
						s = s->nst;
						break;
					}
					if (s->inp == 0) goto nenter;
					if (s->link == 0) {
						if (smax >= &w[MAXSIZ -1]) overflo();
						s->link = ++smax;
						s = smax;
						goto nenter;
					}
					s = s->link;
				}
			}
			s->out = 1;
			s = w;
		} else {
		loop:	if (s->inp == c) {
				s = s->nst;
				continue;
			}
			if (s->inp == 0) goto enter;
			if (s->link == 0) {
				if (smax >= &w[MAXSIZ - 1]) overflo();
				s->link = ++smax;
				s = smax;
				goto enter;
			}
			s = s->link;
			goto loop;
		}
	}

	enter:
	do {
		s->inp = c;
		if (smax >= &w[MAXSIZ - 1]) overflo();
		s->nst = ++smax;
		s = smax;
	} while ((c = getargc()) != '\n' && c!=EOF);
	if (xflag) {
	nenter:	s->inp = '\n';
		if (smax >= &w[MAXSIZ -1]) overflo();
		s->nst = ++smax;
	}
	smax->out = 1;
	s = w;
	if (c != EOF)
		goto nword;
}

overflo() {
	fprintf(stderr, gettext("wordlist too large\n"));
	exit(2);
}
cfail() {
	struct words *queue[QSIZE];
	struct words **front, **rear;
	struct words *state;
	register char c;
	register struct words *s;
	s = w;
	front = rear = queue;
init:	if ((s->inp) != 0) {
		*rear++ = s->nst;
		if (rear >= &queue[QSIZE - 1]) overflo();
	}
	if ((s = s->link) != 0) {
		goto init;
	}

	while (rear!=front) {
		s = *front;
		if (front == &queue[QSIZE-1])
			front = queue;
		else front++;
	cloop:	if ((c = s->inp) != 0) {
			*rear = (q = s->nst);
			if (front < rear)
				if (rear >= &queue[QSIZE-1])
					if (front == queue) overflo();
					else rear = queue;
				else rear++;
			else
				if (++rear == front) overflo();
			state = s->fail;
		floop:	if (state == 0) state = w;
			if (state->inp == c) {
			qloop:	q->fail = state->nst;
				if ((state->nst)->out == 1) q->out = 1;
				if ((q = q->link) != 0) goto qloop;
			}
			else if ((state = state->link) != 0)
				goto floop;
		}
		if ((s = s->link) != 0)
			goto cloop;
	}
}

#ifdef	EUC
wchar_t letter(c)
	register wchar_t c;
{
        if (c >= 'a' && c <= 'z')
                return (c);
        if (c >= 'A' && c <= 'z')
                return (c + 'a' - 'A');
        return(c);
}
#endif	EUC
