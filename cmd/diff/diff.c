/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)diff.c	1.20	95/03/22 SMI"	/* SVr4.0 1.17	*/

/*
 *
 *		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 *		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 *	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
 *	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		All rights reserved.
 */

/*
 *	diff - differential file comparison
 *
 *	Uses an algorithm  which finds
 *	a pair of longest identical subsequences in the two
 *	files.
 *
 *	The major goal is to generate the match vector J.
 *	J[i] is the index of the line in file1 corresponding
 *	to line i file0. J[i] = 0 if there is no
 *	such line in file1.
 *
 *	Lines are hashed so as to work in core. All potential
 *	matches are located by sorting the lines of each file
 *	on the hash (called value). In particular, this
 *	collects the equivalence classes in file1 together.
 *	Subroutine equiv  replaces the value of each line in
 *	file0 by the index of the first element of its
 *	matching equivalence in (the reordered) file1.
 *	To save space equiv squeezes file1 into a single
 *	array member in which the equivalence classes
 *	are simply concatenated, except that their first
 *	members are flagged by changing sign.
 *
 *	Next the indices that point into member are unsorted into
 *	array class according to the original order of file0.
 *
 *	The cleverness lies in routine stone. This marches
 *	through the lines of file0, developing a vector klist
 *	of "k-candidates". At step i a k-candidate is a matched
 *	pair of lines x,y (x in file0 y in file1) such that
 *	there is a common subsequence of lenght k
 *	between the first i lines of file0 and the first y
 *	lines of file1, but there is no such subsequence for
 *	any smaller y. x is the earliest possible mate to y
 *	that occurs in such a subsequence.
 *
 *	Whenever any of the members of the equivalence class of
 *	lines in file1 matable to a line in file0 has serial number
 *	less than the y of some k-candidate, that k-candidate
 *	with the smallest such y is replaced. The new
 *	k-candidate is chained (via pred) to the current
 *	k-1 candidate so that the actual subsequence can
 *	be recovered. When a member has serial number greater
 *	that the y of all k-candidates, the klist is extended.
 *	At the end, the longest subsequence is pulled out
 *	and placed in the array J by unravel.
 *
 *	With J in hand, the matches there recorded are
 *	checked against reality to assure that no spurious
 *	matches have crept in due to hashing. If they have,
 *	they are broken, and "jackpot " is recorded--a harmless
 *	matter except that a true match for a spuriously
 *	mated line may now be unnecessarily reported as a change.
 *
 *	Much of the complexity of the program comes simply
 *	from trying to minimize core utilization and
 *	maximize the range of doable problems by dynamically
 *	allocating what is needed and reusing what is not.
 *	The core requirements for problems larger than somewhat
 *	are (in words) 2*length(file0) + length(file1) +
 *	3*(number of k-candidates installed),  typically about
 *	6n words for files of length n.
 */
#include <stdio.h>
#include <widec.h>
#include <ctype.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <dirent.h>
#include <locale.h>
#include <stdarg.h>
#include <errno.h>
#include "diff.h"

#define	CHRTRAN(x)	(iflag ? (iswupper(x) ? towlower(x) : (x)) : (x))

int pref, suff;		/* length of prefix and suffix */
int *class;		/* will be overlaid on file[0] */
int *member;		/* will be overlaid on file[1] */
int *klist;		/* will be overlaid on file[0] after class */
struct cand *clist;	/* merely a free storage pot for candidates */
int clen = 0;
int *J;			/* will be overlaid on class */
long *ixold;		/* will be overlaid on klist */
long *ixnew;		/* will be overlaid on file[1] */
char *mbdummy;		/* multibyte dummy for figuring byte count */

static void error(const char *);

/*
 * error message string constants
 */
#define	BAD_MB_ERR	"invalid multibyte character encountered"
#define	NO_PROCS_ERR	"no more processes"
#define	NO_MEM_ERR	"out of memory"

char *
talloc(n)
{
	register char *p;
	p = (char *)malloc((unsigned)n);
	if (p == NULL)
		noroom();
	return (p);
}

char *
ralloc(p, n)	/* compacting reallocation */
char *p;
{
	register char *q;
#if 0
	free(p);
#endif
	free(dummy);
	dummy = (char *)malloc(1);
	q = (char *)realloc(p, (unsigned)n);
	if (q == NULL)
		noroom();
	return (q);
}

main(argc, argv)
char **argv;
{
	register int k;
	register int c;
	register char *argp;
	int flag;			/* option flag read by getopt() */
	int i, j;
	char buf1[BUFSIZ], buf2[BUFSIZ];
	extern char *optarg;
	extern int optind;


	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)		/* Should be defined by cc -D */
#define	TEXT_DOMAIN	"SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	mbdummy = (char *)malloc(MB_CUR_MAX);

	diffargv = argv;
	whichtemp = 0;
	while ((flag = getopt(argc, argv, "bitwcefhnlrsC:D:S:")) != EOF) {
		switch (flag) {
		case 'D':
			opt = D_IFDEF;
			wantelses = 1;
			ifdef1 = "";
			ifdef2 = optarg;
			break;

		case 'b':
			bflag = 1;
			break;

		case 'C':
			opt = D_CONTEXT;
			argp = optarg;
			context = 0;
			while (*argp >= '0' && *argp <= '9')
				context *= 10, context += *argp++ - '0';
			if (*argp)
				error("use -C num");
			break;

		case 'c':
			opt = D_CONTEXT;
			context = 3;
			break;

		case 'e':
			opt = D_EDIT;
			break;

		case 'f':
			opt = D_REVERSE;
			break;

		case 'h':
			hflag++;
			break;

		case 'i':
			iflag = 1;
			break;

		case 'l':
			lflag = 1;
			break;

		case 'n':
			opt = D_NREVERSE;
			break;

		case 'r':
			rflag = 1;
			break;

		case 'S':
			strcpy(start, optarg);
			break;

		case 's':
			sflag = 1;
			break;

		case 't':
			tflag = 1;
			break;

		case 'w':
			wflag = 1;
			break;

		case '?':
			usage();
			break;

		default:
			/* Not sure how it would get here, but just in case */
			fprintf(stderr, "diff: ");
			fprintf(stderr, gettext("invalid option -%c\n"), flag);
			usage();
		}
	}

	argc -= optind;
	argv = &argv[optind];

	if (argc != 2)
		error("two filename arguments required");

	file1 = argv[0];
	file2 = argv[1];

	if (hflag) {
		if (opt) {
			error("-h doesn't support -e, -f, -n, -c, or -I");
		} else {
			diffargv[0] = "diffh";
			execv(diffh, diffargv);
			fprintf(stderr, "diffh: ");
			perror(diffh);
			status = 2;
			done();
		}

	}

	dummy = (char *)malloc(1);

	if (strcmp(file1, "-") == 0)
		stb1.st_mode = S_IFREG;
	else if (stat(file1, &stb1) < 0) {
		fprintf(stderr, "diff: ");
		perror(file1);
		done();
	}
	if (strcmp(file2, "-") == 0) {
		if (strcmp(file1, "-") == 0)
			error("cannot specify - -");
		stb2.st_mode = S_IFREG;
	} else if (stat(file2, &stb2) < 0) {
		fprintf(stderr, "diff: ");
		perror(file2);
		done();
	}

	if ((stb1.st_mode & S_IFMT) == S_IFDIR &&
	    (stb2.st_mode & S_IFMT) == S_IFDIR) {
		diffdir(argv);
		done();
	    }

	filename(&file1, &file2, &stb1);
	filename(&file2, &file1, &stb2);
	if ((input[0] = fopen(file1, "r")) == NULL) {
		fprintf(stderr, "diff: ");
		perror(file1);
		status = 2;
		done();
	}
	if ((input[1] = fopen(file2, "r")) == NULL) {
		fprintf(stderr, "diff: ");
		perror(file2);
		status = 2;
		done();
	}
	if (stb1.st_size != stb2.st_size)
		goto notsame;

	for (;;) {
		i = fread(buf1, 1, BUFSIZ, input[0]);
		j = fread(buf2, 1, BUFSIZ, input[1]);
		if (ferror(input[0]) || ferror(input[1])) {
			fprintf(stderr, "diff: ");
			fprintf(stderr, gettext("Error reading "));
			perror(ferror(input[0])? file1:file2);
			fclose(input[0]);
			fclose(input[1]);
			status = 2;
			done();
		}
		if (i != j)
			goto notsame;
		if (i == 0 && j == 0) {
			fclose(input[0]);
			fclose(input[1]);
			status = 0;
			goto same;		/* files don't differ */
		}
		for (j = 0; j < i; j++)
			if (buf1[j] != buf2[j])
				goto notsame;
	}

notsame:
	status = 1;
	if (filebinary(input[0]) || filebinary(input[1])) {
		if (ferror(input[0]) || ferror(input[1])) {
			fprintf(stderr, "diff: ");
			fprintf(stderr, gettext("Error reading "));
			perror(ferror(input[0])? file1:file2);
			fclose(input[0]);
			fclose(input[1]);
			status = 2;
			done();
		}
		status = 2;
		fprintf(stderr, gettext("Binary files %s and %s differ\n"),
		    file1, file2);
		fclose(input[0]);
		fclose(input[1]);
		done();
	}
	prepare(0, file1);
	prepare(1, file2);
	prune();
	sort(sfile[0], slen[0]);
	sort(sfile[1], slen[1]);

	member = (int *)file[1];
	equiv(sfile[0], slen[0], sfile[1], slen[1], member);
	member = (int *)ralloc((char *)member, (slen[1]+2)*sizeof (int));

	class = (int *)file[0];
	unsort(sfile[0], slen[0], class);
	class = (int *)ralloc((char *)class, (slen[0]+2)*sizeof (int));

	klist = (int *)talloc((slen[0]+2)*sizeof (int));
	clist = (struct cand *)talloc(sizeof (cand));
	k = stone(class, slen[0], member, klist);
	free((char *)member);
	free((char *)class);

	J = (int *)talloc((len[0]+2)*sizeof (int));
	unravel(klist[k]);
	free((char *)clist);
	free((char *)klist);

	ixold = (long *)talloc((len[0]+2)*sizeof (long));
	ixnew = (long *)talloc((len[1]+2)*sizeof (long));
	check();
	output();
	status = anychange;

same:
	if (opt == D_CONTEXT && anychange == 0)
		printf(gettext("No differences encountered\n"));
	done();
	/*NOTREACHED*/
}

stone(a, n, b, c)
register *a, *b, *c;
{
	register int i, k, y;
	int j, l;
	int oldc, tc;
	int oldl;

	k = 0;
	c[0] = newcand(0, 0, 0);
	for (i = 1; i <= n; i++) {
		j = a[i];
		if (j == 0)
			continue;
		y = -b[j];
		oldl = 0;
		oldc = c[0];
		do {
			if (y <= clist[oldc].y)
				continue;
			l = search(c, k, y);
			if (l != oldl+1)
				oldc = c[l-1];
			if (l <= k) {
				if (clist[c[l]].y <= y)
					continue;
				tc = c[l];
				c[l] = newcand(i, y, oldc);
				oldc = tc;
				oldl = l;
			} else {
				c[l] = newcand(i, y, oldc);
				k++;
				break;
			}
		} while ((y = b[++j]) > 0);
	}
	return (k);
}

newcand(x, y, pred)
{
	register struct cand *q;

	clist = (struct cand *)ralloc((char *)clist, ++clen*sizeof (cand));
	q = clist + clen -1;
	q->x = x;
	q->y = y;
	q->pred = pred;
	return (clen-1);
}

search(c, k, y)
register *c;
{
	register int i, j, l;
	int t;

	if (clist[c[k]].y < y)	/* quick look for typical case */
		return (k+1);
	i = 0;
	j = k+1;
	while ((l = (i+j)/2) > i) {
		t = clist[c[l]].y;
		if (t > y)
			j = l;
		else if (t < y)
			i = l;
		else
			return (l);
	}
	return (l+1);
}

unravel(p)
{
	register int i;
	register struct cand *q;

	for (i = 0; i <= len[0]; i++)
		J[i] = i <= pref ? i :
			i > len[0]-suff ? i+len[1]-len[0]:
			0;
	for (q = clist+p; q->y != 0; q = clist+q->pred)
		J[q->x+pref] = q->y+pref;
}

/*
 * check does double duty:
 * 1. ferret out any fortuitous correspondences due to confounding by
 * hashing (which result in "jackpot")
 * 2. collect random access indexes to the two files
 */

check()
{
	register int c, d, i, j;
	/* int jackpot; */
	int clen;
	long ctold, ctnew;

	input[0] = fopen(file1, "r");
	input[1] = fopen(file2, "r");
	j = 1;
	ixold[0] = ixnew[0] = 0;
	/* jackpot = 0; */

	/*
	 * ctold and ctnew are byte positions within the file (suitable for
	 * lseek()).  After we get a character with getwc(), instead of
	 * just incrementing the byte position by 1, we have to determine
	 * how many bytes the character actually is.  This is the reason for
	 * the wctomb() calls here and in skipline().
	 */
	ctold = ctnew = 0;
	for (i = 1; i <= len[0]; i++) {
		if (J[i] == 0) {
			ixold[i] = ctold += skipline(0);
			continue;
		}
		while (j < J[i]) {
			ixnew[j] = ctnew += skipline(1);
			j++;
		}
		if (bflag || wflag || iflag) {
			for (;;) {
				c = getwc(input[0]);
				d = getwc(input[1]);

				if ((clen = wctomb(mbdummy, c)) < 0)
					error(BAD_MB_ERR);
				else
					ctold += clen;

				if ((clen = wctomb(mbdummy, d)) < 0)
					error(BAD_MB_ERR);
				else
					ctnew += clen;
				if (bflag && iswspace(c) && iswspace(d)) {
					do {
						if (c == '\n' || c == EOF)
							break;

						if ((clen = wctomb(mbdummy,
						    c)) < 0)
							error(BAD_MB_ERR);
						else
							ctold += clen;
					} while (iswspace(c = getwc(input[0])));
					do {
						if (d == '\n' || d == EOF)
							break;

						if ((clen = wctomb(mbdummy,
						    d)) < 0)
							error(BAD_MB_ERR);
						else
							ctnew += clen;
					} while (iswspace(d = getwc(input[1])));
				} else if (wflag) {
					while (iswspace(c) && c != '\n') {
						c = getwc(input[0]);

						if ((clen = wctomb(mbdummy,
						    c)) < 0)
							error(BAD_MB_ERR);
						else
							ctold += clen;
					}
					while (iswspace(d) && d != '\n') {
						d = getwc(input[1]);

						if ((clen = wctomb(mbdummy,
						    d)) < 0)
							error(BAD_MB_ERR);
						else
							ctnew += clen;
					}
				}
				if (c == EOF || d == EOF) {
					if (c != d) {
						/* jackpot++; */
						J[i] = 0;
						if (c != '\n' && c != EOF)
							ctold += skipline(0);
						if (d != '\n' && d != EOF)
							ctnew += skipline(1);
						break;
					}
					break;
				} else {
					if (CHRTRAN(c) != CHRTRAN(d)) {
						/* jackpot++; */
						J[i] = 0;
						if (c != '\n')
							ctold += skipline(0);
						if (d != '\n')
							ctnew += skipline(1);
						break;
					}
					if (c == '\n')
						break;
				}
			}
		} else {
			for (;;) {
				c = getwc(input[0]);
				d = getwc(input[1]);

				if ((clen = wctomb(mbdummy, c)) < 0)
					error(BAD_MB_ERR);
				else
					ctold += clen;

				if ((clen = wctomb(mbdummy, d)) < 0)
					error(BAD_MB_ERR);
				else
					ctnew += clen;

				if (c != d) {
					/* jackpot++; */
					J[i] = 0;
					if (c != '\n' && c != EOF)
						ctold += skipline(0);
					if (d != '\n' && d != EOF)
						ctnew += skipline(1);
					break;
				}
				if (c == '\n' || c == EOF)
					break;
			}
		}
		ixold[i] = ctold;
		ixnew[j] = ctnew;
		j++;
	}
	for (; j <= len[1]; j++) {
		ixnew[j] = ctnew += skipline(1);
	}
	fclose(input[0]);
	fclose(input[1]);

/*	if(jackpot)			*/
/*		fprintf(stderr, "diff: jackpot\n");	*/
}

skipline(f)
{
	register i;
	int c, clen;

	for (i = 1; c = getwc(input[f]); ) {
		if (c == '\n' || c == EOF)
			return (i);

		if ((clen = wctomb(mbdummy, c)) < 0)
			error(BAD_MB_ERR);
		else
			i += clen;
	}
	return (i);
}

output()
{
	int m;
	register int i0, i1, j1;
	int j0;

	input[0] = fopen(file1, "r");
	input[1] = fopen(file2, "r");
	m = len[0];
	J[0] = 0;
	J[m+1] = len[1]+1;
	if (opt != D_EDIT)
		for (i0 = 1; i0 <= m; i0 = i1+1) {
			while (i0 <= m && J[i0] == J[i0-1]+1)
				i0++;
			j0 = J[i0-1]+1;
			i1 = i0-1;
			while (i1 < m && J[i1+1] == 0)
				i1++;
			j1 = J[i1+1]-1;
			J[i1] = j1;
			change(i0, i1, j0, j1);
		} else for (i0 = m; i0 >= 1; i0 = i1 - 1) {
			while (i0 >= 1 && J[i0] == J[i0+1] - 1 && J[i0] != 0)
				i0--;
			j0 = J[i0+1]-1;
			i1 = i0+1;
			while (i1 > 1 && J[i1-1] == 0)
				i1--;
			j1 = J[i1-1]+1;
			J[i1] = j1;
			change(i1, i0, j1, j0);
		}
	if (m == 0)
		change(1, 0, 1, len[1]);
	if (opt == D_IFDEF) {
		for (;;) {
			i0 = getwc(input[0]);
			if (i0 < 0)
				return;
			putwchar(i0);
		}
	}
	if (anychange && opt == D_CONTEXT)
		dump_context_vec();
}


/*
 * indicate that there is a difference between lines a and b of the from file
 * to get to lines c to d of the to file.
 * If a is greater then b then there are no lines in the from file involved
 * and this means that there were lines appended (beginning at b).
 * If c is greater than d then there are lines missing from the to file.
 */
change(a, b, c, d)
{
	int ch;

	if (opt != D_IFDEF && a > b && c > d)
		return;
	if (anychange == 0) {
		anychange = 1;
		if (opt == D_CONTEXT) {
			printf("*** %s	%s", file1, ctime(&stb1.st_mtime));
			printf("--- %s	%s", file2, ctime(&stb2.st_mtime));

			context_vec_start = (struct context_vec *)
					    malloc(MAX_CONTEXT *
					    sizeof (struct context_vec));
			if (context_vec_start == NULL)
				error(NO_MEM_ERR);

			context_vec_end = context_vec_start + MAX_CONTEXT;
			context_vec_ptr = context_vec_start - 1;
		}
	}
	if (a <= b && c <= d)
		ch = 'c';
	else
		ch = (a <= b) ? 'd' : 'a';
	if (opt == D_CONTEXT) {
		/*
		 * if this new change is within 'context' lines of
		 * the previous change, just add it to the change
		 * record.  If the record is full or if this
		 * change is more than 'context' lines from the previous
		 * change, dump the record, reset it & add the new change.
		 */
		if (context_vec_ptr >= context_vec_end ||
		    (context_vec_ptr >= context_vec_start &&
		    a > (context_vec_ptr->b + 2*context) &&
		    c > (context_vec_ptr->d + 2*context)))
			dump_context_vec();

		context_vec_ptr++;
		context_vec_ptr->a = a;
		context_vec_ptr->b = b;
		context_vec_ptr->c = c;
		context_vec_ptr->d = d;
		return;
	}

	switch (opt) {
	case D_NORMAL:
	case D_EDIT:
		range(a, b, ",");
		putchar(a > b ? 'a' : c > d ? 'd' : 'c');
		if (opt == D_NORMAL) range(c, d, ",");
		printf("\n");
		break;
	case D_REVERSE:
		putchar(a > b ? 'a' : c > d ? 'd' : 'c');
		range(a, b, " ");
		printf("\n");
		break;
	case D_NREVERSE:
		if (a > b)
			printf("a%d %d\n", b, d-c+1);
		else {
			printf("d%d %d\n", a, b-a+1);
			if (!(c > d))
				/* add changed lines */
				printf("a%d %d\n", b, d-c+1);
		}
		break;
	}
	if (opt == D_NORMAL || opt == D_IFDEF) {
		fetch(ixold, a, b, input[0], "< ", 1);
		if (a <= b && c <= d && opt == D_NORMAL)
			prints("---\n");
	}
	fetch(ixnew, c, d, input[1], opt == D_NORMAL?"> ":empty, 0);
	if ((opt == D_EDIT || opt == D_REVERSE) && c <= d)
		prints(".\n");
	if (inifdef) {
		fprintf(stdout, "#endif %s\n", endifname);
		inifdef = 0;
	}
}

range(a, b, separator)
char *separator;
{
	printf("%d", a > b ? b : a);
	if (a < b) {
		printf("%s%d", separator, b);
	}
}

fetch(f, a, b, lb, s, oldfile)
long *f;
FILE *lb;
char *s;
{
	register int i;
	register int col;
	int c;
	register int nc;

	/*
	 * When doing #ifdef's, copy down to current line
	 * if this is the first file, so that stuff makes it to output.
	 */
	if (opt == D_IFDEF && oldfile) {
		long curpos = ftell(lb);
		/* print through if append (a>b), else to (nb: 0 vs 1 orig) */
		nc = f[a > b? b : a-1 ] - curpos;
		for (i = 0; i < nc; i++) {
			c = getwc(lb);
			if (c != EOF)
				putwchar(c);
			else {
				putchar('\n');
				break;
			}
		}
	}
	if (a > b)
		return;
	if (opt == D_IFDEF) {
		int oneflag = (*ifdef1 != '\0') != (*ifdef2 != '\0');
		if (inifdef)
			fprintf(stdout, "#else %s%s\n",
			    oneflag && oldfile == 1 ? "!" : "", ifdef2);
		else {
			if (oneflag) {
				/* There was only one ifdef given */
				endifname = ifdef2;
				if (oldfile)
					fprintf(stdout,
					    "#ifndef %s\n", endifname);
				else
					fprintf(stdout,
					    "#ifdef %s\n", endifname);
			} else {
				endifname = oldfile ? ifdef1 : ifdef2;
				fprintf(stdout, "#ifdef %s\n", endifname);
			}
		}
		inifdef = 1+oldfile;
	}

	for (i = a; i <= b; i++) {
		fseek(lb, f[i-1], 0);
		if (opt != D_IFDEF)
			prints(s);
		col = 0;
		while (c = getwc(lb)) {
			if (c != '\n' && c != EOF)
				if (c == '\t' && tflag)
					do
						putchar(' ');
					while (++col & 7);
				else {
					putwchar(c);
					col++;
				}
			else
				break;
		}
		putchar('\n');
	}
}

/*
 * hashing has the effect of
 * arranging line in 7-bit bytes and then
 * summing 1-s complement in 16-bit hunks
 */

readhash(f, str)
FILE *f;
char *str;
{
	long sum;
	register unsigned shift;
	register space;
	register t;
	sum = 1;
	space = 0;
	if (!bflag && !wflag) {
		if (iflag)
			for (shift = 0; (t = getwc(f)) != '\n'; shift += 7) {
				if (t == EOF) {
					if (shift) {
fprintf(stderr, gettext("Warning: missing newline at end of file %s\n"), str);
						break;
					}
					else
						return (0);
				}
				sum += CHRTRAN(t) << (shift &= HALFMASK);
			}
		else
			for (shift = 0; (t = getwc(f)) != '\n'; shift += 7) {
				if (t == EOF) {
					if (shift) {
fprintf(stderr, gettext("Warning: missing newline at end of file %s\n"), str);
						break;
					} else
						return (0);
				}
					sum += (long)t << (shift &= HALFMASK);
			}
	} else {
		for (shift = 0; ; ) {
			t = getwc(f);

			if (t != '\n' && iswspace(t)) {
				space++;
				continue;
			} else {
				switch (t) {
				case EOF:
					if (shift) {
						fprintf(stderr, gettext(
							"Warning: missing "
							"newline at end of "
							"file %s\n"), str);
						break;
					} else
						return (0);
				default:
					if (space && !wflag) {
						shift += 7;
						space = 0;
					}
					sum += CHRTRAN(t) <<
						(shift &= HALFMASK);
					shift += 7;
					continue;
				case '\n':
					break;
				}
			}
		break;
		}
	}
	return (sum);
}


/* dump accumulated "context" diff changes */
dump_context_vec()
{
	register int	a, b, c, d;
	register char	ch;
	register struct	context_vec *cvp = context_vec_start;
	register int	lowa, upb, lowc, upd;
	register int	do_output;

	if (cvp > context_vec_ptr)
		return;

	lowa = max(1, cvp->a - context);
	upb  = min(len[0], context_vec_ptr->b + context);
	lowc = max(1, cvp->c - context);
	upd  = min(len[1], context_vec_ptr->d + context);

	printf("***************\n*** ");
	range(lowa, upb, ",");
	printf(" ****\n");

	/*
	 * output changes to the "old" file.  The first loop suppresses
	 * output if there were no changes to the "old" file (we'll see
	 * the "old" lines as context in the "new" list).
	 */
	do_output = 0;
	for (; cvp <= context_vec_ptr; cvp++)
		if (cvp->a <= cvp->b) {
			cvp = context_vec_start;
			do_output++;
			break;
		}

	if (do_output) {
		while (cvp <= context_vec_ptr) {
			a = cvp->a; b = cvp->b; c = cvp->c; d = cvp->d;

			if (a <= b && c <= d)
				ch = 'c';
			else
				ch = (a <= b) ? 'd' : 'a';

			if (ch == 'a')
				fetch(ixold, lowa, b, input[0], "  ");
			else {
				fetch(ixold, lowa, a-1, input[0], "  ");
				fetch(ixold, a, b, input[0],
				    ch == 'c' ? "! " : "- ");
			}
			lowa = b + 1;
			cvp++;
		}
		fetch(ixold, b+1, upb, input[0], "  ");
	}

	/* output changes to the "new" file */
	printf("--- ");
	range(lowc, upd, ",");
	printf(" ----\n");

	do_output = 0;
	for (cvp = context_vec_start; cvp <= context_vec_ptr; cvp++)
		if (cvp->c <= cvp->d) {
			cvp = context_vec_start;
			do_output++;
			break;
		}

	if (do_output) {
		while (cvp <= context_vec_ptr) {
			a = cvp->a; b = cvp->b; c = cvp->c; d = cvp->d;

			if (a <= b && c <= d)
				ch = 'c';
			else
				ch = (a <= b) ? 'd' : 'a';

			if (ch == 'd')
				fetch(ixnew, lowc, d, input[1], "  ");
			else {
				fetch(ixnew, lowc, c-1, input[1], "  ");
				fetch(ixnew, c, d, input[1],
				    ch == 'c' ? "! " : "+ ");
			}
			lowc = d + 1;
			cvp++;
		}
		fetch(ixnew, d+1, upd, input[1], "  ");
	}

	context_vec_ptr = context_vec_start - 1;
}



/*
 * diff - directory comparison
 */

struct	dir *setupdir();
int	header;
char	title[2*BUFSIZ], *etitle;

diffdir(argv)
	char **argv;
{
	register struct dir *d1, *d2;
	struct dir *dir1, *dir2;
	register int i;
	int cmp;
	int result, dirstatus;

	if (opt == D_IFDEF)
		error("cannot specify -I with directories");

	if (opt == D_EDIT && (sflag || lflag)) {
		fprintf(stderr, "diff: ");
		fprintf(stderr, gettext(
			"warning: should not give -s or -l with -e\n"));
	}
	dirstatus = 0;
	title[0] = 0;
	strcpy(title, "diff ");
	for (i = 1; diffargv[i+2]; i++) {
		if (strcmp(diffargv[i], "-") == 0) {
			continue;	/* Skip -S and its argument */
		}
		strcat(title, diffargv[i]);
		strcat(title, " ");
	}
	for (etitle = title; *etitle; etitle++)
		;
	setfile(&file1, &efile1, file1);
	setfile(&file2, &efile2, file2);
	argv[0] = file1;
	argv[1] = file2;
	dir1 = setupdir(file1);
	dir2 = setupdir(file2);
	d1 = dir1; d2 = dir2;
	while (d1->d_entry != 0 || d2->d_entry != 0) {
		if (d1->d_entry && useless(d1->d_entry)) {
			d1++;
			continue;
		}
		if (d2->d_entry && useless(d2->d_entry)) {
			d2++;
			continue;
		}
		if (d1->d_entry == 0)
			cmp = 1;
		else if (d2->d_entry == 0)
			cmp = -1;
		else
			cmp = strcmp(d1->d_entry, d2->d_entry);
		if (cmp < 0) {
			if (lflag)
				d1->d_flags |= ONLY;
			else if (opt == 0 || opt == 2)
				only(d1, 1);
			d1++;
			if (dirstatus == 0)
				dirstatus = 1;
		} else if (cmp == 0) {
			result = compare(d1);
			if (result > dirstatus)
				dirstatus = result;
			d1++;
			d2++;
		} else {
			if (lflag)
				d2->d_flags |= ONLY;
			else if (opt == 0 || opt == 2)
				only(d2, 2);
			d2++;
			if (dirstatus == 0)
				dirstatus = 1;
		}
	}
	if (lflag) {
		scanpr(dir1, ONLY, "Only in %.*s", file1, efile1, 0, 0);
		scanpr(dir2, ONLY, "Only in %.*s", file2, efile2, 0, 0);
		scanpr(dir1, SAME,
		    "Common identical files in %.*s and %.*s",
		    file1, efile1, file2, efile2);
		scanpr(dir1, DIFFER,
		    "Binary files which differ in %.*s and %.*s",
		    file1, efile1, file2, efile2);
		scanpr(dir1, DIRECT,
		    "Common subdirectories of %.*s and %.*s",
		    file1, efile1, file2, efile2);
	}
	if (rflag) {
		if (header && lflag)
			printf("\f");
		for (d1 = dir1; d1->d_entry; d1++)  {
			if ((d1->d_flags & DIRECT) == 0)
				continue;
			strcpy(efile1, d1->d_entry);
			strcpy(efile2, d1->d_entry);
			result = calldiff((char *)0);
			if (result > dirstatus)
				dirstatus = result;
		}
	}
	status = dirstatus;
}

setfile(fpp, epp, file)
	char **fpp, **epp;
	char *file;
{
	register char *cp;

	*fpp = (char *)malloc(BUFSIZ);
	if (*fpp == 0) {
		fprintf(stderr, "diff: ");
		fprintf(stderr, gettext("out of memory\n"));
		exit(1);
	}
	strcpy(*fpp, file);
	for (cp = *fpp; *cp; cp++)
		continue;
	*cp++ = '/';
	*epp = cp;
}

scanpr(dp, test, title, file1, efile1, file2, efile2)
	register struct dir *dp;
	int test;
	char *title, *file1, *efile1, *file2, *efile2;
{
	int titled = 0;

	for (; dp->d_entry; dp++) {
		if ((dp->d_flags & test) == 0)
			continue;
		if (titled == 0) {
			if (header == 0)
				header = 1;
			else
				printf("\n");
			printf(title,
			    efile1 - file1 - 1, file1,
			    efile2 - file2 - 1, file2);
			printf(":\n");
			titled = 1;
		}
		printf("\t%s\n", dp->d_entry);
	}
}

only(dp, which)
	struct dir *dp;
	int which;
{
	char *file = which == 1 ? file1 : file2;
	char *efile = which == 1 ? efile1 : efile2;

	printf(gettext("Only in %.*s: %s\n"), efile - file - 1, file,
	    dp->d_entry);
}

int	entcmp();

struct dir *
setupdir(cp)
	char *cp;
{
	register struct dir *dp = 0, *ep;
	register struct dirent *rp;
	register int nitems, n;
	register int size;
	DIR *dirp;

	dirp = opendir(cp);
	if (dirp == NULL) {
		fprintf(stderr, "diff: ");
		perror(cp);
		done();
	}
	nitems = 0;
	dp = (struct dir *)malloc(sizeof (struct dir));
	if (dp == 0)
		error(NO_MEM_ERR);

	while (rp = readdir(dirp)) {
		ep = &dp[nitems++];
		ep->d_reclen = rp->d_reclen;
		ep->d_entry = 0;
		ep->d_flags = 0;
		size = strlen(rp->d_name);
		if (size > 0) {
			ep->d_entry = (char *)malloc(size + 1);
			if (ep->d_entry == 0)
				error(NO_MEM_ERR);

			strcpy(ep->d_entry, rp->d_name);
		}
		dp = (struct dir *)realloc((char *)dp,
			(nitems + 1) * sizeof (struct dir));
		if (dp == 0)
			error(NO_MEM_ERR);
	}
	dp[nitems].d_entry = 0;		/* delimiter */
	closedir(dirp);
	qsort(dp, nitems, sizeof (struct dir), entcmp);
	return (dp);
}

entcmp(d1, d2)
	struct dir *d1, *d2;
{
	return (strcmp(d1->d_entry, d2->d_entry));
}

compare(dp)
	register struct dir *dp;
{
	register int i, j;
	int f1, f2;
	mode_t fmt1, fmt2;
	struct stat stb1, stb2;
	int flag = 0;
	char buf1[BUFSIZ], buf2[BUFSIZ];
	int result;

	strcpy(efile1, dp->d_entry);
	strcpy(efile2, dp->d_entry);

	stat(file1, &stb1);
	stat(file2, &stb2);
	fmt1 = stb1.st_mode & S_IFMT;
	fmt2 = stb2.st_mode & S_IFMT;

	if (fmt1 == S_IFREG) {
		f1 = open(file1, O_RDONLY);
		if (f1 < 0) {
			fprintf(stderr, "diff: ");
			perror(file1);
			return (2);
		}
	}

	if (fmt2 == S_IFREG) {
		f2 = open(file2, O_RDONLY);
		if (f2 < 0) {
			fprintf(stderr, "diff: ");
			perror(file2);
			close(f1);
			return (2);
		}
	}

	if (fmt1 != S_IFREG || fmt2 != S_IFREG) {
		if (fmt1 == fmt2) {
			switch (fmt1) {

			case S_IFDIR:
				dp->d_flags = DIRECT;
				if (lflag || opt == D_EDIT)
					goto closem;
				printf(gettext(
				    "Common subdirectories: %s and %s\n"),
				    file1, file2);
				goto closem;

			case S_IFCHR:
			case S_IFBLK:
				if (stb1.st_rdev == stb2.st_rdev)
					goto same;
				printf(gettext(
				    "Special files %s and %s differ\n"),
				    file1, file2);
				break;

			case S_IFLNK:
				if ((i = readlink(file1, buf1, BUFSIZ)) == -1) {
					fprintf(stderr, gettext(
					    "diff: cannot read link\n"));
					return (2);
				}

				if ((j = readlink(file2, buf2, BUFSIZ)) == -1) {
					fprintf(stderr, gettext(
					    "diff: cannot read link\n"));
					return (2);
				}

				if (i == j) {
					if (strncmp(buf1, buf2, i) == 0)
						goto same;
				}

				printf(gettext(
				    "Symbolic links %s and %s differ\n"),
				    file1, file2);
				break;

			case S_IFIFO:
				if (stb1.st_ino == stb2.st_ino)
					goto same;
				printf(gettext(
				    "Named pipes %s and %s differ\n"),
				    file1, file2);
				break;
			}
		} else {
			if (lflag)
				dp->d_flags |= DIFFER;
			else if (opt == D_NORMAL || opt == D_CONTEXT) {
				printf(gettext("File %s is a "), file1);
				pfiletype(fmt1);
				printf(gettext(" while file %s is a "), file2);
				pfiletype(fmt2);
				putchar('\n');
			}
		}
		close(f1); close(f2);
		return (1);
	}
	if (stb1.st_size != stb2.st_size)
		goto notsame;
	for (;;) {
		i = read(f1, buf1, BUFSIZ);
		j = read(f2, buf2, BUFSIZ);
		if (i < 0 || j < 0) {
			fprintf(stderr, "diff: ");
			fprintf(stderr, gettext("Error reading "));
			perror(i < 0 ? file1: file2);
			close(f1); close(f2);
			return (2);
		}
		if (i != j)
			goto notsame;
		if (i == 0 && j == 0)
			goto same;
		for (j = 0; j < i; j++)
			if (buf1[j] != buf2[j])
				goto notsame;
	}
same:
	if (sflag == 0)
		goto closem;
	if (lflag)
		dp->d_flags = SAME;
	else
		printf(gettext("Files %s and %s are identical\n"),
			file1, file2);

closem:
	close(f1); close(f2);
	return (0);

notsame:
	if (binary(f1) || binary(f2)) {
		if (lflag)
			dp->d_flags |= DIFFER;
		else if (opt == D_NORMAL || opt == D_CONTEXT)
			printf(gettext("Binary files %s and %s differ\n"),
			    file1, file2);
		close(f1); close(f2);
		return (1);
	}
	close(f1); close(f2);
	anychange = 1;
	if (lflag) {
		result = calldiff(title);
	} else {
		if (opt == D_EDIT)
			printf("ed - %s << '-*-END-*-'\n", dp->d_entry);
		else
			printf("%s%s %s\n", title, file1, file2);
		result = calldiff((char *)0);
		if (opt == D_EDIT)
			printf("w\nq\n-*-END-*-\n");
	}
	return (result);
}

char	*prargs[] = { "pr", "-h", 0, 0, 0 };

calldiff(wantpr)
	char *wantpr;
{
	pid_t pid;
	int diffstatus, prstatus, pv[2];

	prargs[2] = wantpr;
	fflush(stdout);
	if (wantpr) {
		sprintf(etitle, "%s %s", file1, file2);
		pipe(pv);
		pid = fork();
		if (pid == (pid_t)-1)
			error(NO_PROCS_ERR);

		if (pid == 0) {
			close(0);
			dup(pv[0]);
			close(pv[0]);
			close(pv[1]);
			execv(pr+5, prargs);
			execv(pr, prargs);
			perror(pr);
			done();
		}
	}
	pid = fork();
	if (pid == (pid_t)-1)
		error(NO_PROCS_ERR);

	if (pid == 0) {
		if (wantpr) {
			close(1);
			dup(pv[1]);
			close(pv[0]);
			close(pv[1]);
		}
		execv(diff+5, diffargv);
		execv(diff, diffargv);
		perror(diff);
		done();
	}
	if (wantpr)
	{
		close(pv[0]);
		close(pv[1]);
	}
	while (wait(&diffstatus) != pid)
		continue;
	while (wait((int *)0) != (pid_t)-1)
		continue;
	if ((diffstatus&0177) != 0)
		return (2);
	else
		return ((diffstatus>>8) & 0377);
}

pfiletype(fmt)
	mode_t fmt;
{
	switch (fmt) {

	case S_IFDIR:
		printf(gettext("directory"));
		break;

	case S_IFCHR:
		printf(gettext("character special file"));
		break;

	case S_IFBLK:
		printf(gettext("block special file"));
		break;

	case S_IFREG:
		printf(gettext("plain file"));
		break;

	case S_IFIFO:
		printf(gettext("named pipe"));
		break;

	default:
		printf(gettext("unknown type"));
		break;
	}
}

binary(f)
	int f;
{
	char buf[BUFSIZ];
	int cnt;

	lseek(f, (long)0, 0);
	cnt = read(f, buf, BUFSIZ);
	if (cnt < 0)
		return (1);
	return (isbinary(buf, cnt));
}

filebinary(f)
	FILE *f;
{
	char buf[BUFSIZ];
	int cnt;

	fseek(f, (long)0, 0);
	cnt = fread(buf, 1, BUFSIZ, f);
	if (ferror(f))
		return (1);
	return (isbinary(buf, cnt));
}


/*
 * We consider a "binary" file to be one that:
 * contains a null character ("diff" doesn't handle them correctly, and
 *    neither do many other UNIX text-processing commands).
 * Characters with their 8th bit set do NOT make a file binary; they may be
 * legitimate text characters, or parts of same.
 */
isbinary(buf, cnt)
	char *buf;
	register int cnt;
{
	register char *cp;

	cp = buf;
	while (--cnt >= 0)
		if (*cp++ == '\0')
			return (1);
	return (0);
}


/*
 * THIS IS CRUDE.
 */
useless(cp)
register char *cp;
{

	if (cp[0] == '.') {
		if (cp[1] == '\0')
			return (1);	/* directory "." */
		if (cp[1] == '.' && cp[2] == '\0')
			return (1);	/* directory ".." */
	}
	if (start && strcmp(start, cp) > 0)
		return (1);
	return (0);
}


sort(a, n)	/* shellsort CACM #201 */
register struct line *a;
{
	struct line w;
	register int j, m;
	register struct line *ai;
	register struct line *aim;
	register k;

	for (j = 1, m = 0; j <= n; j *= 2)
		m = 2*j - 1;
	for (m /= 2; m != 0; m /= 2) {
		k = n-m;
		for (j = 1; j <= k; j++) {
			for (ai = &a[j]; ai > a; ai -= m) {
				aim = &ai[m];
				if (aim < ai)
					break;	/* wraparound */
				if (aim->value > ai[0].value ||
				    aim->value == ai[0].value &&
				    aim->serial > ai[0].serial)
					break;
				w.value = ai[0].value;
				ai[0].value = aim->value;
				aim->value = w.value;
				w.serial = ai[0].serial;
				ai[0].serial = aim->serial;
				aim->serial = w.serial;
			}
		}
	}
}

unsort(f, l, b)
register struct line *f;
register *b;
{
	register int *a;
	register int i;

	a = (int *)talloc((l+1)*sizeof (int));
	for (i = 1; i <= l; i++)
		a[f[i].serial] = f[i].value;
	for (i = 1; i <= l; i++)
		b[i] = a[i];
	free((char *)a);
}

filename(pa1, pa2, st)
char **pa1, **pa2;
struct stat *st;
{
	char *copytemp();
	register char *a1, *b1, *a2;

	a1 = *pa1;
	a2 = *pa2;
	if ((st->st_mode & S_IFMT) == S_IFDIR) {
		b1 = *pa1 = (char *)malloc(PATH_MAX);
		while (*b1++ = *a1++)
			;
		b1[-1] = '/';
		a1 = b1;
		while (*a1++ = *a2++)
			if (*a2 && *a2 != '/' && a2[-1] == '/')
				a1 = b1;
		if (stat(*pa1, st) < 0) {
			fprintf(stderr, "diff: ");
			perror(*pa1);
			done();
		}
	} else if ((st->st_mode & S_IFMT) == S_IFCHR)
		*pa1 = copytemp(a1);
	else if (a1[0] == '-' && a1[1] == 0 && whichtemp == 0) {
		*pa1 = copytemp(a1);	/* hack! */
	}
}

char *
copytemp(fn)
char *fn;
{
	char *mktemp();
	register ifd, ofd;	/* input and output file descriptors */
	int i;
	char *temp, template[13];	/* template for temp file name */
	char buf[BUFSIZ];

	/*
	 * a "-" file is interpreted as fd 0 for pre-/dev/fd systems
	 * ... let's hope this goes away soon!
	 */
	if ((ifd = (strcmp(fn, "-") ? open(fn, 0) : 0)) < 0) {
		fprintf(stderr, "diff: ");
		fprintf(stderr, gettext("cannot open %s\n"), fn);
		done();
	}
	signal(SIGHUP, done);
	signal(SIGINT, done);
	signal(SIGPIPE, done);
	signal(SIGTERM, done);
	strcpy(tempfile[whichtemp++],
		temp = mktemp(strcpy(template, "/tmp/dXXXXXX")));
	if ((ofd = creat(temp, S_IRUSR|S_IWUSR)) < 0) {
		fprintf(stderr, "diff: ");
		fprintf(stderr, gettext("cannot create %s\n"), temp);
		done();
	}
	while ((i = read(ifd, buf, BUFSIZ)) > 0)
		if (write(ofd, buf, i) != i) {
			fprintf(stderr, "diff: ");
			fprintf(stderr,
				gettext("write failed %s\n"), temp);
			done();
		}
	close(ifd); close(ofd);
	return (tempfile[whichtemp-1]);
}

prepare(i, arg)
char *arg;
{
	register struct line *p;
	register j, h;

	fseek(input[i], (long)0, 0);
	p = (struct line *)talloc(3 * sizeof (line));
	for (j = 0; h = readhash(input[i], arg); ) {
		p = (struct line *)ralloc((char *)p, (++j+3)*sizeof (line));
		p[j].value = h;
	}
	len[i] = j;
	file[i] = p;
	fclose(input[i]);
}

prune()
{
	register i, j;

	for (pref = 0; pref < len[0] && pref < len[1] &&
			file[0][pref+1].value == file[1][pref+1].value;
	    pref++)
		;
	for (suff = 0; (suff < len[0] - pref) &&
			(suff < len[1] - pref) &&
			(file[0][len[0]-suff].value ==
			file[1][len[1]-suff].value);
	    suff++)
		;

	/* decremnt suff by 2 iff suff >= 2, ensure that suff is never < 0 */
	if (suff >= 2)
		suff -= 2;

	for (j = 0; j < 2; j++) {
		sfile[j] = file[j]+pref;
		slen[j] = len[j]-pref-suff;
		for (i = 0; i <= slen[j]; i++)
			sfile[j][i].serial = i;
	}
}

equiv(a, n, b, m, c)
register struct line *a, *b;
register *c;
{
	register int i, j;
	i = j = 1;
	while (i <= n && j <= m) {
		if (a[i].value < b[j].value)
			a[i++].value = 0;
		else if (a[i].value == b[j].value)
			a[i++].value = j;
		else
			j++;
	}
	while (i <= n)
		a[i++].value = 0;
	b[m+1].value = 0;	j = 0;
	while (++j <= m) {
		c[j] = -b[j].serial;
		while (b[j+1].value == b[j].value) {
			j++;
			c[j] = b[j].serial;
		}
	}
	c[j] = -1;
}


min(a, b)
int a, b;
{
	return (a < b ? a : b);
}

max(a, b)
int a, b;
{
	return (a > b ? a : b);
}


void
done()
{
	if (whichtemp) unlink(tempfile[0]);
	if (whichtemp == 2) unlink(tempfile[1]);
	exit(status);
}

noroom()
{
	fprintf(stderr, "diff: ");
	fprintf(stderr, gettext("files too big, try -h\n"));
	done();
}

static void
error(const char *s)
{
	fprintf(stderr, "diff: ");
	fprintf(stderr, gettext(s));
	fprintf(stderr, "\n");
	done();
}

usage()
{
	fprintf(stderr, gettext(
		"usage: diff [-bitw] [-c | -e | -f | -h | -n] file1 file2\n"
		"       diff [-bitw] [-C number] file1 file2\n"
		"       diff [-bitw] [-D string] file1 file2\n"
		"       diff [-bitw] [-c | -e | -f | -h | -n] [-l] [-r] "
			"[-s] [-S name] directory1 directory2\n"));
	status = 2;
	done();
}
