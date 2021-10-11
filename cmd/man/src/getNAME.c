/*      Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T */
/*        All Rights Reserved   */
 
/*      THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T     */
/*      The copyright notice above does not evidence any        */
/*      actual or intended publication of such source code.     */
/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#ident	"@(#)getNAME.c	1.3	90/07/25 SMI"	/* SVr4.0 1.1	*/

/*
 * Get name sections from manual pages.
 *	-t	for building toc
 *	-i	for building intro entries
 *	other	apropos database
 */
#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#define PLEN	3	/* prefix length "man" */

char path[MAXPATHLEN+1];
int tocrc;
int intro;

main(argc, argv)
	int argc;
	char *argv[];
{

	argc--, argv++;
	if (!strcmp(argv[0], "-t"))
		argc--, argv++, tocrc++;
	if (!strcmp(argv[0], "-i"))
		argc--, argv++, intro++;
	if (getcwd(path, sizeof(path)) == NULL) {
		fprintf(stderr, "getcwd: %s\n", path);
		exit(1);
	}
	while (argc > 0)
		getfrom(*argv++), argc--;
	exit(0);
}

getfrom(name)
	char *name;
{
	char headbuf[BUFSIZ];
	char linbuf[BUFSIZ];
	register char *cp;
	int i = 0;

	if (freopen(name, "r", stdin) == 0) {
		perror(name);
		return;
	}
	for (;;) {
		if (fgets(headbuf, sizeof headbuf, stdin) == NULL)
			return;
		if (headbuf[0] != '.')
			continue;
		if (headbuf[1] == 'T' && headbuf[2] == 'H')
			break;
		if (headbuf[1] == 't' && headbuf[2] == 'h')
			break;
	}
	for (;;) {
		if (fgets(linbuf, sizeof linbuf, stdin) == NULL)
			return;
		if (linbuf[0] != '.')
			continue;
		if (linbuf[1] == 'S' && linbuf[2] == 'H')
			break;
		if (linbuf[1] == 's' && linbuf[2] == 'h')
			break;
	}
	trimln(headbuf);
	if (tocrc)
		doname(name);
	if (!intro)
		section(name, headbuf);
	for (;;) {
		if (fgets(linbuf, sizeof linbuf, stdin) == NULL)
			break;
		if (linbuf[0] == '.') {
			if (linbuf[1] == 'S' && linbuf[2] == 'H')
				break;
			if (linbuf[1] == 's' && linbuf[2] == 'h')
				break;
		}
		trimln(linbuf);
		if (intro) {
			split(linbuf, name);
			continue;
		}
		if (i != 0)
			printf(" ");
		i++;
		printf("%s", linbuf);
	}
	printf("\n");
}


/*
 * Substitute section defined in page with new section spec
 * of the form xx/yy where xx is the section suffix of the
 * directory and yy is the filename extension (unless xx 
 * and yy are equal, in which case xx is the section).
 * Pages should be placed in their proper directory with the
 * proper name to simplify things.
 *
 * For example take the following names:
 *    man1/ar.1v	(1/1V)
 *    man1/find.1	(1)
 *    man1/loco		(1/)
 *
 */

section(name, buf)
char *name;
char *buf;
{
	char scratch[MAXPATHLEN+1];
	char *p = buf;
	char *dir, *fname;
	char *dp, *np;
	int i;

	/*
	 * split dirname and filename
	 */
	strcpy(scratch, name);
	if ((fname = strrchr(scratch, '/')) == NULL) {
		fname = name;
		dir = path;
	} else {
		dir = scratch;
		*fname = 0;
		fname++;
	}
	dp = strrchr(dir, '/');
	dp = dp ? dp+PLEN+1 : dir+PLEN;
	np = strrchr(fname, '.');
	np = np ? ++np : "";
	for(i=0; i<2; i++) {
		while(*p && *p != ' ' && *p != '\t')
			p++;
		if (!*p)
			break;
		while(*p && (*p == ' ' || *p == '\t'))
			p++;
		if (!*p)
			break;
	}
	*p++ = 0;
	printf("%s", buf);
	if (strcmp(np, dp) == 0)
		printf("%s", dp);
	else
		printf("%s/%s", dp, np);
	while(*p && *p != ' ' && *p != '\t')
		p++;
	printf("%s\t", p);
}


trimln(cp)
	register char *cp;
{

	while (*cp)
		cp++;
	if (*--cp == '\n')
		*cp = 0;
}

doname(name)
	char *name;
{
	register char *dp = name, *ep;

again:
	while (*dp && *dp != '.')
		putchar(*dp++);
	if (*dp)
		for (ep = dp+1; *ep; ep++)
			if (*ep == '.') {
				putchar(*dp++);
				goto again;
			}
	putchar('(');
	if (*dp)
		dp++;
	while (*dp)
		putchar (*dp++);
	putchar(')');
	putchar(' ');
}

split(line, name)
	char *line, *name;
{
	register char *cp, *dp;
	char *sp, *sep;

	cp = strchr(line, '-');
	if (cp == 0)
		return;
	sp = cp + 1;
	for (--cp; *cp == ' ' || *cp == '\t' || *cp == '\\'; cp--)
		;
	*++cp = '\0';
	while (*sp && (*sp == ' ' || *sp == '\t'))
		sp++;
	for (sep = "", dp = line; dp && *dp; dp = cp, sep = "\n") {
		cp = strchr(dp, ',');
		if (cp) {
			register char *tp;

			for (tp = cp - 1; *tp == ' ' || *tp == '\t'; tp--)
				;
			*++tp = '\0';
			for (++cp; *cp == ' ' || *cp == '\t'; cp++)
				;
		}
		printf("%s%s\t", sep, dp);
		dorefname(name);
		printf("\t%s", sp);
	}
}

dorefname(name)
	char *name;
{
	register char *dp = name, *ep;

again:
	while (*dp && *dp != '.')
		putchar(*dp++);
	if (*dp)
		for (ep = dp+1; *ep; ep++)
			if (*ep == '.') {
				putchar(*dp++);
				goto again;
			}
	putchar('.');
	if (*dp)
		dp++;
	while (*dp)
		putchar (*dp++);
}
