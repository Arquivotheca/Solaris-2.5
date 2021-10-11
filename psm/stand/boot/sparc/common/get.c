/*
 * Copyright (c) 1985 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)get.c	1.9	94/11/21 SMI"

#include <sys/types.h>
#include <sys/promif.h>

int
getchar()
{
	register int c;

	while ((c = prom_mayget()) == -1)
		;
	if (c == '\r') {
		prom_putchar(c);
		c = '\n';
	}
	if (c == 0177 || c == '\b') {
		prom_putchar('\b');
		prom_putchar(' ');
		c = '\b';
	}
	prom_putchar(c);
	return (c);
}

int
gets(buf)
	char *buf;
{
	register char *lp;
	register c;

	lp = buf;
	for (;;) {
		c = getchar() & 0177;
		switch (c) {
		case '\n':
		case '\r':
			c = '\n';
			*lp++ = '\0';
			return;
		case '\b':
			lp--;
			if (lp < buf)
				lp = buf;
			continue;
		case 'u'&037:			/* ^U */
			lp = buf;
			prom_putchar('\r');
			prom_putchar('\n');
			continue;
		default:
			*lp++ = c;
		}
	}
}
