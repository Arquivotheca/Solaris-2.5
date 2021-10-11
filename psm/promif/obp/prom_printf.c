/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)prom_printf.c	1.8	94/06/10 SMI"

#include <sys/promif.h>
#include <sys/promimpl.h>
#include <sys/varargs.h>

static void _doprint(char *, va_list, void (*)(char, char **), char **);
static void _printn(u_long, int, int, void (*)(char, char **), char **);

/*
 * Emit character functions...
 */

/*ARGSUSED*/
static void
_pput(char c, char **p)
{
	(void) prom_putchar(c);
}

static void
_sput(char c, char **p)
{
	**p = c;
	*p += 1;
}

/*VARARGS1*/
void
prom_printf(char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	(void) _doprint(fmt, adx, _pput, (char **)0);
	va_end(adx);
}

void
prom_vprintf(char *fmt, va_list adx)
{
	(void) _doprint(fmt, adx, _pput, (char **)0);
}

/*VARARGS2*/
char *
prom_sprintf(char *s, char *fmt, ...)
{
	char *bp = s;
	va_list adx;

	va_start(adx, fmt);
	(void) _doprint(fmt, adx, _sput, &bp);
	*bp++ = (char)0;
	va_end(adx);
	return (s);
}

char *
prom_vsprintf(char *s, char *fmt, va_list adx)
{
	char *bp = s;

	(void) _doprint(fmt, adx, _sput, &bp);
	*bp++ = (char)0;
	return (s);
}

static void
_doprint(char *fmt, va_list adx, void (*emit)(char, char **), char **bp)
{
	register int b, c, i, width;
	register char *s;

loop:
	width = 0;
	while ((c = *fmt++) != '%') {
		if (c == '\0')
			goto out;
		if (c == '\n')
			(*emit)('\r', bp);
		(*emit)(c, bp);
	}
again:
	c = *fmt++;
	if (c >= '2' && c <= '9') {
		width = c - '0';
		c = *fmt++;
	}
	switch (c) {

	case 'l':
		goto again;
	case 'x':
	case 'X':
		b = 16;
		goto number;
	case 'd':
	case 'D':
	case 'u':
		b = 10;
		goto number;
	case 'o':
	case 'O':
		b = 8;
number:
		_printn(va_arg(adx, u_long), b, width, emit, bp);
		break;
	case 'c':
		b = va_arg(adx, int);
		for (i = 24; i >= 0; i -= 8)
			if ((c = ((b >> i) & 0x7f)) != 0) {
				if (c == '\n')
					(*emit)('\r', bp);
				(*emit)(c, bp);
			}
		break;
	case 's':
		s = va_arg(adx, char *);
		while ((c = *s++) != 0) {
			if (c == '\n')
				(*emit)('\r', bp);
			(*emit)(c, bp);
		}
		break;

	case '%':
		(*emit)('%', bp);
		break;
	}
	goto loop;
out:
	va_end(x1);
}

/*
 * Printn prints a number n in base b.
 * We don't use recursion to avoid deep kernel stacks.
 */
static void
_printn(u_long n, int b, int width, void (*emit)(char, char **), char **bp)
{
	char prbuf[40];
	register char *cp;

	if (b == 10 && (int)n < 0) {
		(*emit)('-', bp);
		n = (unsigned)(-(int)n);
	}
	cp = prbuf;
	do {
		*cp++ = "0123456789abcdef"[n%b];
		n /= b;
		width--;
	} while (n);
	while (width-- > 0)
		*cp++ = '0';
	do {
		(*emit)(*--cp, bp);
	} while (cp > prbuf);
}
