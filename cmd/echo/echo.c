/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)echo.c	1.8	94/09/08 SMI"	/* SVr4.0 1.3	*/

#include <stdio.h>

int
main(argc, argv)
int argc;
char **argv;
{

	register char	*cp;
	register int	i, wd;
	int	j;

	if (--argc == 0) {
		(void) putchar('\n');
		return (0);
	}
	for (i = 1; i <= argc; i++) {
		for (cp = argv[i]; *cp; cp++) {
			if (*cp == '\\')
			switch (*++cp) {
				case 'a':		/* alert - XCU4	*/
					(void) putchar('\a');
					continue;

				case 'b':
					(void) putchar('\b');
					continue;

				case 'c':
					return (0);

				case 'f':
					(void) putchar('\f');
					continue;

				case 'n':
					(void) putchar('\n');
					continue;

				case 'r':
					(void) putchar('\r');
					continue;

				case 't':
					(void) putchar('\t');
					continue;

				case 'v':
					(void) putchar('\v');
					continue;

				case '\\':
					(void) putchar('\\');
					continue;
				case '0':
					j = wd = 0;
					while ((*++cp >= '0' && *cp <= '7') &&
						j++ < 3) {
						wd <<= 3;
						wd |= (*cp - '0');
					}
					(void) putchar(wd);
					--cp;
					continue;

				default:
					cp--;
			}
			(void) putchar(*cp);
		}
		(void) putchar(i == argc? '\n': ' ');
	}
	return (0);
}
