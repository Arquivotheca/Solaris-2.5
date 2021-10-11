/*
 * Copyright (c) 1995 Sun Microsystems, Inc.
 */

#ident	"@(#)output.c	1.25	95/06/13 SMI"

/*
 * adb - output routines
 */

#include "adb.h"
#include <stdio.h>
#include <stdarg.h>

int	infile = 0;
int	outfile = 1;
int	maxpos;
int	maxoff;
int	radix = 16;

static void printhex(int);
static void printhex_cmn(u_longlong_t, int);

#define	MAXLIN	255
char	printbuf[MAXLIN];
char	*printptr = printbuf;
char	*digitptr;

int db_printf( ... );

printc(c)
	char c;
{
	char d, *q;
	int posn, tabs, p;

	if (interrupted)
		return;
	if ((*printptr = c) == '\n') {
		tabs = 0;
		posn = 0;
		q = printbuf;
		for (p = 0; p < printptr - printbuf; p++) {
			d = printbuf[p];
			if ((p&7) == 0 && posn) {
				tabs++;
				posn = 0;
			}
			if (d == ' ')
				posn++;
			else {
				while (tabs > 0) {
					*q++= '\t';
					tabs--;
				}
				while (posn > 0) {
					*q++ = ' ';
					posn--;
				}
				*q++ = d;
			}
		}
		*q++ = '\n';
#if defined(KADB)
		trypause();
#endif
		(void) write(outfile, printbuf, q - printbuf);
#if defined(KADB)
		trypause();
#endif
		printptr = printbuf;
		return;
	}
	if (c == '\t') {
		*printptr++ = ' ';
		while ((printptr - printbuf) & 7)
			*printptr++ = ' ';
	} else if (c)
		printptr++;
#if defined(KADB)
	trypause();
#endif
	if (printptr >= &printbuf[sizeof (printbuf)-9]) {
		(void) write(outfile, printbuf, printptr - printbuf);
		printptr = printbuf;
	}
#if defined(KADB)
	trypause();
#endif
}

charpos()
{

	return (printptr - printbuf);
}

flushbuf()
{

	if (printptr != printbuf)
		printc('\n');
}

killbuf()
{

	if (printptr != printbuf) {
		printptr = printbuf;
		printc('\n');
	}
}

/* Cheat a little -- need a back-up macro */
#define va_backup(list, mode) ((mode *)(list = (char *)list - sizeof(mode)))[-1]

/* This printf declaration MUST look just like the prototype definition
 * in stdio.h to propitiate the ANSI gods.
 */
printf( const char *fmat, ... )
{
	char *fmt = (char *) fmat;
	char *s;
	int width, prec;
	char c, adj;
	int n, val, sygned;
	u_longlong_t llval;
	double d;
	char digits[1024 /* temporary kluge for sprintf bug */];
#if !defined(sparc) || !defined(i386)
	void doubletos (double d, char *s);
#endif /* !defined(sparc) || !defined(i386) */

	va_list vptr;
	va_start( vptr, fmt );

	while (c = *fmt++) {
		if (c != '%') {
			printc(c);
			continue;
		}
		if (*fmt == '-') {
			adj = 'l';
			fmt++;
		} else
			adj='r';
		width = convert(&fmt);
		if (*fmt=='.') {
			fmt++;
			prec = convert(&fmt);
		} else
			prec = -1;
		if (*fmt=='+') {
			fmt++;
			sygned = 1;
		} else {
			sygned = 0;
		}
		digitptr = digits;

		val = va_arg( vptr, int );

		s = 0;
		switch (c = *fmt++) {
		case 'g':	/* signed octal */
		case 'G':	/* unsigned octal */
		case 'e':	/* signed decimal */
		case 'E':	/* unsigned decimal */
		case 'J':	/* hexadecimal */
			va_backup(vptr, int);
			llval = va_arg(vptr, u_longlong_t);
			print_ll(llval, c);
			break;

		case 'o':
			printoct((unsigned short)val, 0); break;
		case 'q':
			printoct((short)val, -1); break;
		case 'x':
			printhex((unsigned short)val); break;
		case 'Y':
			printdate(val); break;
		case 'r':
			/*
			 * "%+r" is printed in the current radix
			 * with a minus sign if the value is negative
			 */
			if (sygned) {
				printnum((short)val, '+', radix);
			} else {
				printnum((unsigned short)val, c, radix);
			}
			break;
		case 'R':
			printnum(val, (sygned? '+': c), radix); break;
		case 'd':
			printnum((short)val, c, 10); break;
		case 'u':
			printnum((unsigned short)val, c, 10); break;
		case 'D':
		case 'U':
			printnum(val, c, 10); break;
		case 'O':
			printoct(val, 0); break;
		case 'Q':
			printoct(val, -1); break;
		case 'X':
			printhex(val); break;
		case 'c':
			printc(val); break;
		case 's':
			s = (char *)val; break;
		case 'z':
			{
			/* form for disassembled 16 bit immediate constants. */
			if ((-9 <= val) && (val <= 9)) {
				/* Let's not use 0x for unambiguous numbers. */
				printnum(val, 'd', 10);
			} else {
				/* 0xhex for big numbers. */
				if (sygned && (val < 0))
					printc('-');
				printc('0');
				printc('x');
				if (sygned && (val < 0))
					printhex(-val);
				else
					printhex(val);
			}
			break;
			}
		case 'Z':
			{
			/* form for disassembled 32 bit immediate constants. */
			if ((-9 <= val) && (val <= 9)) {
				/* Let's not use 0x for unambiguous numbers. */
				printnum( val, 'D', 10);
			} else {
				/* 0xhex for big numbers. */
				if (sygned && (val < 0))
					printc('-');
				printc('0');
				printc('x');
				if (sygned && (val < 0)) 
					printhex(-val);
				else 
					printhex(val);
			}
			break;
			}
#ifndef KADB
		case 'f':
		case 'F':
			s = digits;

			va_backup( vptr, int );
			d =  va_arg( vptr, double );

			prec = -1;
			if (c == 'f') {
				(void) sprintf(s, "%+.7e", d);
			} else {
#if sparc || i386
				(void) sprintf(s, "%+.16e", d);
#else
				doubletos(d, s) ;
#endif
			}
			break;
#endif !KADB
		case 'm':
			va_backup( vptr, int ); break;
		case 'M':
			width = val; break;
		case 'T':
		case 't':
			if (c == 'T')
				width = val;
			else
				va_backup( vptr, int );
			if (width)
				width -= charpos() % width;
			break;
		default:
			printc(c);
			va_backup( vptr, int );
		}
		if (s == 0) {
			*digitptr = 0;
			s = digits;
		}
		n = strlen(s);
		if (prec < n && prec >= 0)
			n = prec;
		width -= n;
		if (adj == 'r')
			while (width-- > 0)
				printc(' ');
		while (n--)
			printc(*s++);
		while (width-- > 0)
			printc(' ');
		digitptr = digits;
	}
} /* end printf */

static
printdate(tvec)
	int tvec;
{
	register int i;
	char *ctime();
	register char *timeptr = ctime((time_t *)&tvec);

	for (i = 20; i < 24; i++)
		*digitptr++ = timeptr[i];
	for (i = 3; i < 19; i++)
		*digitptr++ = timeptr[i];
}

prints(s)
	char *s;
{

	printf("%s", s);
}

newline()
{

	printc('\n');
}

convert(cp)
	register char **cp;
{
	register char c;
	int n;

	n = 0;
	while ((c = *(*cp)++) >= '0' && c <= '9')
		n = n * 10 + c - '0';
	(*cp)--;
	return (n);
}

static
printnum(n, fmat, base)
	int n;
	char fmat;
	int base;
{
	register int k;
	register unsigned un;
	char digs[15];
	register char *dptr = digs;

	/*
	 * if signs are wanted, put 'em out
	 */
	switch (fmat) {
	case 'r':
	case 'R':
		if (base != 10) break;
	case '+':
	case 'd':
	case 'D':
	case 'q':
	case 'Q':
		if (n < 0) {
			n = -n;
			*digitptr++ = '-';
		}
		break;
	}
	/*
	 * put out radix
	 */
	switch (base) {
	default:
		break;
	case 010:
		*digitptr++ = '0';
		break;
	case 0x10:
		*digitptr++ = '0';
		*digitptr++ = 'x';
		break;
	}
	un = n;
	while (un) {
		*dptr++ = un % base;
		un /= base;
	}
	if (dptr == digs)
		*dptr++ = 0;
	while (dptr != digs) {
		k = *--dptr;
		*digitptr++ = k + (k <= 9 ? '0' : 'a'-10);
	}
}


static
printoct(o, s)
	int o, s;
{
	int i, po = o;
	char digs[12];

	if (s) {
		if (po < 0) {
			po = -po;
			*digitptr++ = '-';
		} else
			if (s > 0)
				*digitptr++ = '+';
	}
	for (i = 0; i<=11; i++) {
		digs[i] = po & 7;
		po >>= 3;
	}
	digs[10] &= 03; digs[11]=0;
	for (i = 11; i >= 0; i--)
		if (digs[i])
			break;
	for (i++; i >= 0; i--)
		*digitptr++ = digs[i] + '0';
}

print_ll(x, fmt)
	u_longlong_t x;
	char fmt;
{
	switch (fmt) {
	case 'g':
	case 'G':
	case 'e':
	case 'E':
		break;
	case 'J':
		printhex_cmn(x, 16);
		break;
	}
}

static void
printhex(x)
	int x;
{
	printhex_cmn((u_longlong_t)x, 8);
}

static void
printhex_cmn(x, ndigs)
	u_longlong_t x;
	int ndigs;
{
	int i;
	char digs[16];
	static char hexe[]="0123456789abcdef";

	for (i = 0; i < ndigs; i++) {
		digs[i] = x & 0xf;
		x >>= 4;
	}
	for (i = ndigs-1; i > 0; i--)
		if (digs[i])
			break;
	for (; i >= 0; i--)
		*digitptr++ = hexe[digs[i]];
}

oclose()
{
	db_printf(7, "oclose, outfile=%D", outfile);
	if (outfile != 1) {
		flushbuf();
		(void) close(outfile);
		outfile = 1;
	}
}

endline()
{

	if (maxpos <= charpos())
		printf("\n");
}



/*
 * adb-debugging printout routine
 */

int adb_debug = 0;	/* public, set via "+d" adb arg */

#ifdef	__STDC__
db_printf( ... )
#else	/* __STDC__ */
db_printf( va_alist )
	va_dcl
#endif	/* __STDC__ */
{

	if(adb_debug) {
		va_list vptr;
		short level;

#ifdef	__STDC__
	va_start( vptr, );
#else	/* __STDC__ */
	va_start( vptr );
#endif	/* __STDC__ */

		/*
		 * Set the first field (level) in db_printf() to the following:
		 *
		 * level=0	does not print the message
		 * level=1	very important message, used rarely
		 * level=2	important message
		 * level=3	less important message
		 * level=4	print args when entering the function or
		 *			print the return values
		 * level=5	same as 4, but for less important functions
		 * level=6	same as 5, but for less important functions
		 * level=7	detailed
		 * level=8	very detailed
		 * ...
		 * level=ADB_DEBUG_MAX	prints ALL the messages
		 */
		level = va_arg(vptr, short);
		if (level && adb_debug >= level) {
			long a, b, c, d, e;
			char fmt[256];

			(void) sprintf(fmt, "  %d==>\t%s\n",
						level, va_arg(vptr, char *));
			a = va_arg(vptr, long);
			b = va_arg(vptr, long);
			c = va_arg(vptr, long);
			d = va_arg(vptr, long);
			e = va_arg(vptr, long);
			printf(fmt, a, b, c, d, e);
			fflush(stdout);
			va_end(vptr);
		}
	}
	return 0;
}
