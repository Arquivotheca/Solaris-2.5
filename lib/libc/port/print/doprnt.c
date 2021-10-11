/*	Copyright (c) 1988 AT&T	*/

/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)doprnt.c	1.45	95/09/22 SMI"	/* SVr4.0 3.30	*/

/*LINTLIBRARY*/
/*
 *	_doprnt: common code for printf, fprintf, sprintf
 */

#include "synonyms.h"
#include "shlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include <varargs.h>
#include <values.h>
#include <nan.h>
#include "Qnan.h"
#include <memory.h>
#include <string.h>
#include "print.h"	/* parameters & macros for doprnt */
#include "stdiom.h"
#include <locale.h>
#include "../gen/_locale.h"
#include <widec.h>
#include <mtlib.h>

extern int _cswidth[];
extern void _xgetwidth();
extern int _wssize();

#if defined(i386)
#define	GETQVAL(arg)	(va_arg(arg, long double))
#else /* ! defined(i386) */
#define	GETQVAL(arg)	*(va_arg(arg, long double *))
#endif /* ! defined(i386) */

#define PUT(p, n)     { register unsigned char *newbufptr; \
	              if ((newbufptr = bufptr + n) > bufferend) { \
		              if (!_dowrite(p, n, iop, &bufptr)) \
				return((iop->_flag & _IOREAD) ? iop->_ptr - iop->_base : EOF); \
                      } else { \
			      register char * tmp=(char *)p; \
			      switch (n) {\
			      case 4: \
					*bufptr++=*tmp++; \
			      case 3: \
					*bufptr++=*tmp++; \
			      case 2: \
					*bufptr++=*tmp++; \
			      case 1: \
					*bufptr=*tmp; \
					break; \
			      default: \
		              		(void) memcpy((char *) bufptr, p, n); \
			      } \
			      bufptr = newbufptr; \
                      } \
		    } 
#define PAD(s,n)    { register int nn; \
		    for (nn = n; nn > 20; nn -= 20) \
			       if (!_dowrite(s, 20, iop, &bufptr)) \
				    return((iop->_flag & _IOREAD) ? iop->_ptr - iop->_base : EOF); \
                    PUT(s, nn); \
                   }

#define SNLEN     5    	/* Length of string used when printing a NaN */

/* bit positions for flags used in doprnt */

#define LENGTH 	  1	/* l */
#define FPLUS     2	/* + */
#define FMINUS	  4	/* - */
#define FBLANK	  8	/* blank */
#define FSHARP	 16	/* # */
#define PADZERO  32	/* padding zeroes requested via '0' */
#define DOTSEEN  64	/* dot appeared in format specification */
#define SUFFIX	128	/* a suffix is to appear in the output */
#define RZERO	256	/* there will be trailing zeros in output */
#define LZERO	512	/* there will be leading zeroes in output */
#define SHORT  1024	/* h */
#define	QUAD   2048	/* Q for long double */
#define	XLONG  4096	/* ll for long long */

/*
 *	C-Library routines for floating conversion
 */
extern char *econvert(), *fconvert(), *qeconvert(), *qfconvert();

static char *insert_thousands_sep();
/*
 *	Positional Parameter information
 */
#define MAXARGS	30	/* max. number of args for fast positional paramters */
void _mkarglst();
void _getarg();

static int _dowrite();

/* stva_list is used to subvert C's restriction that a variable with an
 * array type can not appear on the left hand side of an assignment operator.
 * By putting the array inside a structure, the functionality of assigning to
 * the whole array through a simple assignment is achieved..
 */
typedef struct stva_list {
	va_list	ap;
} stva_list;

static int
_lowdigit(valptr)
	long *valptr;
{
	/* This function computes the decimal low-order digit of the number */
	/* pointed to by valptr, and returns this digit after dividing   */
	/* *valptr by ten.  This function is called ONLY to compute the */
	/* low-order digit of a long whose high-order bit is set. */

	int lowbit = *valptr & 1;
	long value = (*valptr >> 1) & ~HIBITL;

	*valptr = value / 5;
	return(value % 5 * 2 + lowbit + '0');
}

#if ! defined(_NO_LONGLONG)
static int
_lowlldigit(valptr)
long long *valptr;
{
	int lowbit = *valptr & 1;
	long long value = (*valptr >> 1) & ~HIBITLL;

	*valptr = value / 5;
	return(value % 5 * 2 + lowbit + '0');
}
#endif /* ! defined(_NO_LONGLONG) */

/* The function _dowrite carries out buffer pointer bookkeeping surrounding */
/* a call to fwrite.  It is called only when the end of the file output */
/* buffer is approached or in other unusual situations. */
static int
_dowrite(p, n, iop, ptrptr)
	register char *p;
	register int	n;
	register FILE	*iop;
	register unsigned char **ptrptr;
{
	if (!(iop->_flag & _IOREAD)) {
		iop->_cnt -= (*ptrptr - iop->_ptr);
		iop->_ptr = *ptrptr;
		_bufsync(iop, _bufend(iop));
		if (_FWRITE(p, 1, n, iop) != n) {
			return 0;
		}
		*ptrptr = iop->_ptr;
	} else {
		if (n > iop->_cnt)
			n = iop->_cnt;
		iop->_cnt -= n;
		*ptrptr = (unsigned char *) memcpy((char *) *ptrptr, p, n) + n;
		iop->_ptr = *ptrptr;
		if (iop->_cnt == 0)
			return 0;
	}
	return 1;
}

	static char _blanks[] = "                    ";
	static char _zeroes[] = "00000000000000000000";
	static char uc_digs[] = "0123456789ABCDEF";
	static char lc_digs[] = "0123456789abcdef";
#ifdef	HANDLE_NaNINF
	static char  lc_nan[] = "nan0x";
	static char  uc_nan[] = "NAN0X";
	static char  lc_inf[] = "inf";
	static char  uc_inf[] = "INF";
#endif	/* defined(HANDLE_NaNINF) */


int
_doprnt(format, in_args, iop)
register char	*format;
va_list	in_args;
register FILE	*iop;
{
                
	/* bufptr is used inside of doprnt instead of iop->_ptr; */
	/* bufferend is a copy of _bufend(iop), if it exists.  For */
	/* dummy file descriptors (iop->_flag & _IOREAD), bufferend */
	/* may be meaningless. Dummy file descriptors are used so that */
        /* sprintf and vsprintf may share the _doprnt routine with the */
	/* rest of the printf family. */

	unsigned char *bufptr, *bufferend;

	/* This variable counts output characters. */
	int	count = 0;

	/* Starting and ending points for value to be printed */
	register char	*bp;
	char *p;

	/* Field width and precision */
	int	width = 0;
	int	prec = 0;
	int sec_display;
	char wflag = 0;
	wchar_t *wp;
	int preco;
	int wcount = 0;
	char tmpbuf[10];
	int quote;		/* ' */

	/* Format code */
	register int	fcode;

	/* Number of padding zeroes required on the left and right */
	int	lzero, rzero;

	/* Flags - bit positions defined by LENGTH, FPLUS, FMINUS, FBLANK, */
	/* and FSHARP are set if corresponding character is in format */
	/* Bit position defined by PADZERO means extra space in the field */
	/* should be padded with leading zeroes rather than with blanks */
	register int	flagword;

	/* Values are developed in this buffer */
	char	buf[max(MAXLLDIGS, 1034)];
	char	cvtbuf[DECIMAL_STRING_LENGTH];
	/* Pointer to sign, "0x", "0X", or empty */
	char	*prefix;

	/* Exponent or empty */
	char	*suffix;

	/* Buffer to create exponent */
	char	expbuf[MAXESIZ + 1];

	/* Length of prefix and of suffix */
	int	prefixlength, suffixlength;

	/* Combined length of leading zeroes, trailing zeroes, and suffix */
	int 	otherlength;

	/* The value being converted, if integer */
	long	val;

#if ! defined(_NO_LONGLONG)
	/* The value being converted, if long long */
	long long ll;
#endif /* ! defined(_NO_LONGLONG) */

	/* The value being converted, if real */
	double	dval;

	/* The value being converted, if long double */
	long double quadval;

	/* Output values from fcvt and ecvt */
	int	decpt, sign;

	/* Pointer to a translate table for digits of whatever radix */
	char	*tab;

	/* Work variables */
	int	k, lradix, mradix;

#ifdef	HANDLE_INFNaN
	/* Variables used to flag an infinities and nans, resp. */
	/* Nan_flg is used with two purposes: to flag a NaN and */
	/* as the length of the string ``NAN0X'' (``nan0x'') */
	 int	 inf_nan = 0, NaN_flg = 0 ;

	/* Pointer to string "NAN0X" or "nan0x" */
	 char	 *SNAN ;

        /* Flag for negative infinity or NaN */
	 int neg_in = 0;
#else	/* defined(HANDLE_INFNaN) */
	 int	 inf_nan = 0;
#endif	/* defined(HANDLE_INFNaN) */

	/* variables for positional parameters */
	char	*sformat = format;	/* save the beginning of the format */
	int	fpos = 1;		/* 1 if first positional parameter */
	stva_list	args,	/* used to step through the argument list */
			sargs;	/* used to save the start of the argument list */
	stva_list	bargs;	/* used to restore args if positional width
				 * or precision */
	stva_list	arglst[MAXARGS];/* array giving the appropriate values
					 * for va_arg() to retrieve the
					 * corresponding argument:	
					 * arglst[0] is the first argument
					 * arglst[1] is the second argument, etc.
					 */
	int	starflg = 0;		/* set to 1 if * format specifier seen */
	/* Initialize args and sargs to the start of the argument list.
	 * Note that ANSI guarantees that the address of the first member of
	 * a structure will be the same as the address of the structure. */
	args = sargs = *(struct stva_list *)&in_args;

	/* if first I/O to the stream get a buffer */
	/* Note that iop->_base should not equal 0 for sprintf and vsprintf */
	if (iop->_base == 0 && _findbuf(iop) == 0)
		return(EOF);

	/* initialize buffer pointer and buffer end pointer */
	/* _IOREAD && _cnt == MAXINT implies [v]sprintf (no boundschecking) */
	bufptr = iop->_ptr;
	bufferend = (iop->_flag & _IOREAD) && iop->_cnt == MAXINT ? 
			(unsigned char *)((long) bufptr | (-1L & ~HIBITL))
				 : _bufend(iop);

	if (!*_cswidth)
		_xgetwidth();
	/*
	 *	The main loop -- this loop goes through one iteration
	 *	for each string of ordinary characters or format specification.
	 */
	for ( ; ; ) {
		register int n;

		if ((fcode = *format) != '\0' && fcode != '%') {
			bp = format;
			do {
				format++;
			} while ((fcode = *format) != '\0' && fcode != '%');
		
			count += (n = format - bp); /* n = no. of non-% chars */
			PUT(bp, n);
		}
		if (fcode == '\0') {  /* end of format; return */
			register int nn = bufptr - iop->_ptr;
			iop->_cnt -= nn;
			iop->_ptr = bufptr;
			if (bufptr + iop->_cnt > bufferend && /* in case of */
					!(iop->_flag & _IOREAD)) /* interrupt */
				_bufsync(iop, bufferend); /* during last several lines */
			if (iop->_flag & (_IONBF | _IOLBF) &&
				    (iop->_flag & _IONBF ||
				     memchr((char *)(bufptr+iop->_cnt), '\n', -iop->_cnt) != NULL))
				(void) _xflsbuf(iop);
			return(FERROR(iop) ? EOF : count);
		}

		/*
		 *	% has been found.
		 *	The following switch is used to parse the format
		 *	specification and to perform the operation specified
		 *	by the format letter.  The program repeatedly goes
		 *	back to this switch until the format letter is
		 *	encountered.
		 */
		width = prefixlength = otherlength = flagword = suffixlength = 0;
		format++;
		wflag = 0;
		sec_display = 0;
		quote = 0;

	charswitch:

		switch (fcode = *format++) {

		case '+':
			flagword |= FPLUS;
			goto charswitch;
		case '-':
			flagword |= FMINUS;
			flagword &= ~PADZERO; /* ignore 0 flag */
			goto charswitch;
		case ' ':
			flagword |= FBLANK;
			goto charswitch;
		case '\'':	/* XSH4 */
			quote++;
			goto charswitch;
		case '#':
			flagword |= FSHARP;
			goto charswitch;

		/* Scan the field width and precision */
		case '.':
			flagword |= DOTSEEN;
			prec = 0;
			goto charswitch;

		case '*':
			if (isdigit(*format)) {
				starflg = 1;
				bargs = args;
				goto charswitch;
			}
			if (!(flagword & DOTSEEN)) {
				width = va_arg(args.ap, int);
				if (width < 0) {
					width = -width;
					flagword |= FMINUS;
				}
			} else {
				prec = va_arg(args.ap, int);
				if (prec < 0) {
					prec = 0;
					flagword ^= DOTSEEN; /* ANSI sez so */
				}
			}
			goto charswitch;

		case '$':
			{
			int		position;
			stva_list	targs;
			if (fpos) {
				_mkarglst(sformat, sargs, arglst);
				fpos = 0;
			}
			if (flagword & DOTSEEN) {
				position = prec;
				prec = 0;
			} else {
				position = width;
				width = 0;
			}
			if (position <= 0) {
				/* illegal position */
				format--;
				continue;
			}
			if (position <= MAXARGS) {
				targs = arglst[position - 1];
			} else {
				targs = arglst[MAXARGS - 1];
				_getarg(sformat, &targs, position);
			}
			if (!starflg)
				args = targs;
			else {
				starflg = 0;
				args = bargs;
				if (flagword & DOTSEEN) {
					prec = va_arg(targs.ap, int);
					if (prec < 0) {
						prec = 0;
						flagword ^= DOTSEEN; /* XSH */
					}
				} else {
					width = va_arg(targs.ap, int);
					if (width < 0) {
						width = -width;
						flagword |= FMINUS;
					}
				}
			}
			goto charswitch;
			}

		case '0':	/* obsolescent spec:  leading zero in width */
				/* means pad with leading zeros */
			if (!(flagword & (DOTSEEN | FMINUS)))
				flagword |= PADZERO;
			/* FALLTHROUGH */
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
		      { register num = fcode - '0';
			while (isdigit(fcode = *format)) {
				num = num * 10 + fcode - '0';
				format++;
			}
			if (flagword & DOTSEEN)
				prec = num;
			else
				width = num;
			goto charswitch;
		      }

		/* Scan the length modifier */
		case 'l':
			if (! (flagword & XLONG)) {
			  if (flagword & LENGTH) {	/* long long */
			    flagword &= ~LENGTH;
			    flagword |= XLONG;
			  }
			  else				/* long */
			    flagword |= LENGTH;
			}
			goto charswitch; 
		case 'L':			/* long double */
			flagword |= QUAD;
			goto charswitch; 
		case 'h':
			flagword |= SHORT;
			goto charswitch; 

		/*
		 *	The character addressed by format must be
		 *	the format letter -- there is nothing
		 *	left for it to be.
		 *
		 *	The status of the +, -, #, and blank
		 *	flags are reflected in the variable
		 *	"flagword".  "width" and "prec" contain
		 *	numbers corresponding to the digit
		 *	strings before and after the decimal
		 *	point, respectively. If there was no
		 *	decimal point, then flagword & DOTSEEN
		 *	is false and the value of prec is meaningless.
		 *
		 *	The following switch cases set things up
		 *	for printing.  What ultimately gets
		 *	printed will be padding blanks, a
		 *	prefix, left padding zeroes, a value,
		 *	right padding zeroes, a suffix, and
		 *	more padding blanks.  Padding blanks
		 *	will not appear simultaneously on both
		 *	the left and the right.  Each case in
		 *	this switch will compute the value, and
		 *	leave in several variables the informa-
		 *	tion necessary to construct what is to
		 *	be printed.  
		 *
		 *	The prefix is a sign, a blank, "0x",
		 *	"0X", or null, and is addressed by
		 *	"prefix".
		 *
		 *	The suffix is either null or an
		 *	exponent, and is addressed by "suffix".
		 *	If there is a suffix, the flagword bit
		 *	SUFFIX will be set.
		 *
		 *	The value to be printed starts at "bp"
		 *	and continues up to and not including
		 *	"p".
		 *
		 *	"lzero" and "rzero" will contain the
		 *	number of padding zeroes required on
		 *	the left and right, respectively.
		 *	The flagword bits LZERO and RZERO tell
		 *	whether padding zeros are required.
		 *
		 *	The number of padding blanks, and
		 *	whether they go on the left or the
		 *	right, will be computed on exit from
		 *	the switch.
		 */



		
		/*
		 *	decimal fixed point representations
		 *
		 *	HIBITL is 100...000
		 *	binary, and is equal to	the maximum
		 *	negative number.
		 *	We assume a 2's complement machine
		 */

		case 'i':
		case 'd':
			if ((flagword & PADZERO) && (flagword & DOTSEEN))
				flagword &= ~PADZERO; /* ignore 0 flag */

			/* Fetch the argument to be printed */
			if (flagword & XLONG) {		/* long long */
#if ! defined(_NO_LONGLONG)
			  ll = va_arg(args.ap, long long);

			  /* Set buffer pointer to last digit */
	                  p = bp = buf + MAXLLDIGS;

			  /* If signed conversion, make sign */
			  if (ll < 0) {
				prefix = "-";
				prefixlength = 1;
				/*
				 * Negate, checking in
				 * advance for possible
				 * overflow.
				 */
				if (ll != HIBITLL)
					ll = -ll;
				else     /* number is -HIBITLL; convert last */
					 /* digit now and get positive number */
					*--bp = _lowlldigit(&ll);
			  } else if (flagword & FPLUS) {
				prefix = "+";
				prefixlength = 1;
			  } else if (flagword & FBLANK) {
				prefix = " ";
				prefixlength = 1;
			  }
#else /* defined(_NO_LONGLONG) */
			  ;
#endif /* defined(_NO_LONGLONG) */
			}
			else {				/* not long long */
			  if (flagword & LENGTH)
				val = va_arg(args.ap, long);
			  else
				val = va_arg(args.ap, int);

			  if (flagword & SHORT)
				val = (short)val;

			  /* Set buffer pointer to last digit */
	                  p = bp = buf + MAXDIGS;

			  /* If signed conversion, make sign */
			  if (val < 0) {
				prefix = "-";
				prefixlength = 1;
				/*
				 * Negate, checking in
				 * advance for possible
				 * overflow.
				 */
				if (val != HIBITL)
					val = -val;
				else     /* number is -HIBITL; convert last */
					 /* digit now and get positive number */
					*--bp = _lowdigit(&val);
			  } else if (flagword & FPLUS) {
				prefix = "+";
				prefixlength = 1;
			  } else if (flagword & FBLANK) {
				prefix = " ";
				prefixlength = 1;
			  }
			}

		decimal:
			{ register long qval = val;
#if ! defined(_NO_LONGLONG)
			  long long lll = ll;
			  long long tll;
#endif /* ! defined(_NO_LONGLONG) */

			  if (flagword & XLONG) {
#if ! defined(_NO_LONGLONG)
				if (lll <= 9LL) {
					if (lll != 0LL || !(flagword & DOTSEEN))
						*--bp = lll + '0';
				} else {
					do {
						tll = lll;
						lll /= 10;
						*--bp = tll - lll * 10 + '0';
					} while (lll > 9);
					*--bp = lll + '0';
				}
#else /* defined(_NO_LONGLONG) */
				;
#endif /* defined(_NO_LONGLONG) */
			  } else {
				if (qval <= 9) {
					if (qval != 0 || !(flagword & DOTSEEN))
						*--bp = qval + '0';
				} else {
					do {
						n = qval;
						qval /= 10;
						*--bp = n - qval * 10 + '0';
					} while (qval > 9);
					*--bp = qval + '0';
				}
			  }
			}
			/* Handle the ' flag */
			if (quote) {
				switch (fcode) {
					case 'd':
					case 'i':
					case 'u':
						p = insert_thousands_sep(bp, p);
						break;
				}
			}

			/* Calculate minimum padding zero requirement */
			if (flagword & DOTSEEN) {
				register leadzeroes = prec - (p - bp);
				if (leadzeroes > 0) {
					otherlength = lzero = leadzeroes;
					flagword |= LZERO;
				}
			}
			break;

		case 'u':
			if ((flagword & PADZERO) && (flagword & DOTSEEN))
				flagword &= ~PADZERO; /* ignore 0 flag */

			/* Fetch the argument to be printed */
			if (flagword & XLONG) {
#if ! defined(_NO_LONGLONG)
			  ll = va_arg(args.ap, long long);

			  p = bp = buf + MAXLLDIGS;

			  if (ll & HIBITLL)
				*--bp = _lowlldigit(&ll);
#else /* defined(_NO_LONGLONG) */
			  ;
#endif /* defined(_NO_LONGLONG) */
			} else {
			  if (flagword & LENGTH)
				val = va_arg(args.ap, long);
			  else
				val = va_arg(args.ap, unsigned);

			  if (flagword & SHORT)
				val = (unsigned short)val;

			  p = bp = buf + MAXDIGS;

			  if (val & HIBITL)
				*--bp = _lowdigit(&val);

			}

			goto decimal;

		/*
		 *	non-decimal fixed point representations
		 *	for radix equal to a power of two
		 *
		 *	"mradix" is one less than the radix for the conversion.
		 *	"lradix" is one less than the base 2 log
		 *	of the radix for the conversion. Conversion is unsigned.
		 *	HIBITL is 100...000
		 *	binary, and is equal to	the maximum
		 *	negative number.
		 *	We assume a 2's complement machine
		 */

		case 'o':
			mradix = 7;
			lradix = 2;
			goto fixed;

		case 'X':
		case 'x':
		case 'p':
			mradix = 15;
			lradix = 3;

		fixed:
			if ((flagword & PADZERO) && (flagword & DOTSEEN))
				flagword &= ~PADZERO; /* ignore 0 flag */

			/* Set translate table for digits */
			  tab = (fcode == 'X') ? uc_digs : lc_digs;
			
			/* Fetch the argument to be printed */
			if (flagword & XLONG) {
#if ! defined(_NO_LONGLONG)
			  ll = va_arg(args.ap, long long);
#else /* defined(_NO_LONGLONG) */
			  ;
#endif /* defined(_NO_LONGLONG) */
			} else {
			  if (flagword & LENGTH)
				val = va_arg(args.ap, long);
			  else
				val = va_arg(args.ap, unsigned);

			  if (flagword & SHORT)
				val = (unsigned short)val;
			}

#ifdef	HANDLE_INFNaN
			/* Entry point when printing a double which is a NaN */
		put_pc:
#endif	/* defined(HANDLE_INFNaN) */

			/* Develop the digits of the value */
			{ register long qval = val;
#if ! defined(_NO_LONGLONG)
			  long long lll = ll;
#endif /* ! defined(_NO_LONGLONG) */

			  if (flagword & XLONG)
			  {
#if ! defined(_NO_LONGLONG)
				p = bp = buf + MAXLLDIGS;
				if (lll == 0LL) {
					if (!(flagword & DOTSEEN)) {
						otherlength = lzero = 1;
						flagword |= LZERO;
					}
				} else
					do {
						*--bp=tab[(int)(lll & mradix)];
						lll = ((lll >> 1) & ~HIBITLL)
								 >> lradix;
					} while (lll != 0LL);
#else /* defined(_NO_LONGLONG) */
				;
#endif /* defined(_NO_LONGLONG) */
			  } 
			  else {
				p = bp = buf + MAXDIGS;
				if (qval == 0) {
					if (!(flagword & DOTSEEN)) {
						otherlength = lzero = 1;
						flagword |= LZERO;
					}
				} else
					do {
						*--bp = tab[qval & mradix];
						qval = ((qval >> 1) & ~HIBITL)
								 >> lradix;
					} while (qval != 0);
			  }
			}

			/* Calculate minimum padding zero requirement */
			if (flagword & DOTSEEN) {
				register leadzeroes = prec - (p - bp);
				if (leadzeroes > 0) {
					otherlength = lzero = leadzeroes;
					flagword |= LZERO;
				}
			}

			/* Handle the # flag, (val != 0) for int and long */
#if ! defined(_NO_LONGLONG)
			/* (ll!= 0) handles long long case */
			if (flagword & FSHARP && ((val != 0) || (ll != 0)))
#else /* defined(_NO_LONGLONG) */
			if ((flagword & FSHARP) && (val != 0))
#endif /* defined(_NO_LONGLONG) */
				switch (fcode) {
				case 'o':
					if (!(flagword & LZERO)) {
						otherlength = lzero = 1;
						flagword |= LZERO;
					}
					break;
				case 'x':
					prefix = "0x";
					prefixlength = 2;
					break;
				case 'X':
					prefix = "0X";
					prefixlength = 2;
					break;
				}

			break;

		case 'E':
		case 'e':
			/*
			 * E-format.  The general strategy
			 * here is fairly easy: we take
			 * what ecvt gives us and re-format it.
			 * (qecvt for long double)
			 */

			/* Establish default precision */
			if (!(flagword & DOTSEEN))
				prec = 6;

			if (flagword & QUAD) {	/* long double */
			  /* Fetch the value */
	                  quadval = GETQVAL(args.ap);

#ifdef	HANDLE_INFNaN
                          /* Check for NaNs and Infinities */
			  if (QIsNANorINF(quadval))  {
			     if (QIsINF(quadval)) {
			        if (QIsNegNAN(quadval)) 
				  neg_in = 1;
			        inf_nan = 1;
			        bp = (fcode == 'E')? uc_inf: lc_inf;
			        p = bp + 3;
			        break;
                             }
			     else {
				  if (QIsNegNAN(quadval)) 
					  neg_in = 1;
				  inf_nan = 1;
			          val  = QGETNaNPC(quadval); 
				  NaN_flg = SNLEN;
				  mradix = 15;
				  lradix = 3;
				  if (fcode == 'E') {
					  SNAN = uc_nan;
					  tab =  uc_digs;
				  }
				  else {
					  SNAN =  lc_nan;
					  tab =  lc_digs;
				  }
				  goto put_pc;
                             }
			  }
#endif	/* defined(HANDLE_INFNaN) */

			  /* Develop the mantissa */
			  bp = qeconvert(&quadval,min(prec+1,MAXECVT),
					&decpt,&sign,cvtbuf);
#ifndef	HANDLE_INFNaN
			  if (*bp > '9') {
				inf_nan = 1;
				break;
			  }
#endif	/* !defined(HANDLE_INFNaN) */
			} else {			/* double */
			  /* Fetch the value */
	                  dval = va_arg(args.ap, double);

#ifdef	HANDLE_INFNaN
                          /* Check for NaNs and Infinities */
			  if (IsNANorINF(dval))  {
			     if (IsINF(dval)) {
			        if (IsNegNAN(dval)) 
				  neg_in = 1;
			        inf_nan = 1;
			        bp = (fcode == 'E')? uc_inf: lc_inf;
			        p = bp + 3;
			        break;
                             }
			     else {
				  if (IsNegNAN(dval)) 
					  neg_in = 1;
				  inf_nan = 1;
			          val  = GETNaNPC(dval); 
				  NaN_flg = SNLEN;
				  mradix = 15;
				  lradix = 3;
				  if (fcode == 'E') {
					  SNAN = uc_nan;
					  tab =  uc_digs;
				  }
				  else {
					  SNAN =  lc_nan;
					  tab =  lc_digs;
				  }
				  goto put_pc;
                             }
			  }
#endif	/* defined(HANDLE_INFNaN) */

			  /* Develop the mantissa */
                          bp = econvert(dval, min(prec+1, MAXECVT),
                                        &decpt, &sign, cvtbuf);
#ifndef	HANDLE_INFNaN
			  if (*bp > '9') {
				inf_nan = 1;
				break;
			  }
#endif	/* !defined(HANDLE_INFNaN) */
			}

			/* Determine the prefix */
		e_merge:
			if (sign) {
				prefix = "-";
				prefixlength = 1;
			} else if (flagword & FPLUS) {
				prefix = "+";
				prefixlength = 1;
			} else if (flagword & FBLANK) {
				prefix = " ";
				prefixlength = 1;
			}

			/* Place the first digit in the buffer*/
			p = &buf[0];
			*p++ = (*bp != '\0') ? *bp++ : '0';

			/* Put in a decimal point if needed */
			if (prec != 0 || (flagword & FSHARP))
				*p++ = _numeric[0];

			/* Create the rest of the mantissa */
			{ register rz = prec;
				for ( ; rz > 0 && *bp != '\0'; --rz)
					*p++ = *bp++;
				if (rz > 0) {
					otherlength = rzero = rz;
					flagword |= RZERO;
				}
			}

			{
			  long	is_zero;
			  if (flagword & QUAD)
			    is_zero = (quadval == 0.0) ? 1 : 0;
			  else
			    is_zero = (dval == 0.0) ? 1 : 0;

			  bp = &buf[0];

			  /* Create the exponent */
			  *(suffix = &expbuf[MAXESIZ]) = '\0';
			  if (! is_zero) {
				register int nn = decpt - 1;
				if (nn < 0)
				    nn = -nn;
				for ( ; nn > 9; nn /= 10)
					*--suffix = todigit(nn % 10);
				*--suffix = todigit(nn);
			  }

			  /* Prepend leading zeroes to the exponent */
			  while (suffix > &expbuf[MAXESIZ - 2])
				*--suffix = '0';

			  /* Put in the exponent sign */
			  *--suffix = (decpt > 0 || is_zero) ? '+' : '-';

			  /* Put in the e */
			  *--suffix = isupper(fcode) ? 'E'  : 'e';

			  /* compute size of suffix */
			  otherlength += (suffixlength = &expbuf[MAXESIZ]
								 - suffix);
			  flagword |= SUFFIX;
			}

			break;

		case 'f':
			/*
			 * F-format floating point.  This is a
			 * good deal less simple than E-format.
			 * The overall strategy will be to call
			 * fcvt, reformat its result into buf,
			 * and calculate how many trailing
			 * zeroes will be required.  There will
			 * never be any leading zeroes needed.
			 * (gcvt for long double)
			 */

			/* Establish default precision */
			if (!(flagword & DOTSEEN))
				prec = 6;

			if (flagword & QUAD) {	/* long double */
			  /* Fetch the value */
			  quadval = GETQVAL(args.ap);
  
#ifdef	HANDLE_INFNaN
                          /* Check for NaNs and Infinities  */
			  if (QIsNANorINF(quadval)) {
			     if (QIsINF(quadval)) {
			        if (QIsNegNAN(quadval))
				  neg_in = 1;
                                inf_nan = 1;
			        bp = lc_inf;
			        p = bp + 3;
			        break;
                             }
                             else {
				  if (QIsNegNAN(quadval)) 
					  neg_in = 1;
				  inf_nan = 1;
			          val  = QGETNaNPC(quadval);
				  NaN_flg = SNLEN;
				  mradix = 15;
				  lradix = 3;
				  tab =  lc_digs;
				  SNAN = lc_nan;
				  goto put_pc;
                             }
                          } 
#endif	/* defined HANDLE_INFNaN */

			  /* Do the conversion */
                          bp = qfconvert(&quadval, min(prec,MAXFCVT), &decpt,
                                        &sign, cvtbuf);
#ifndef	HANDLE_INFNaN
			  if (*bp > '9') {
				inf_nan = 1;
				break;
			  }
#endif	/* !defined(HANDLE_INFNaN) */

			  if (bp[0] == 0) {
			    /* qeconvert overflow internal decimal record */
                            bp=qeconvert(&quadval,min(prec+1,MAXECVT),&decpt,
                                        &sign, cvtbuf);
			    goto e_merge;
			  }
			} else {			/* double */
			  /* Fetch the value */
			  dval = va_arg(args.ap, double);
  
#ifdef	HANDLE_INFNaN
                          /* Check for NaNs and Infinities  */
			  if (IsNANorINF(dval)) {
			     if (IsINF(dval)) {
			        if (IsNegNAN(dval))
				  neg_in = 1;
                                inf_nan = 1;
			        bp = lc_inf;
			        p = bp + 3;
			        break;
                             }
                             else {
				  if (IsNegNAN(dval)) 
					  neg_in = 1;
				  inf_nan = 1;
			          val  = GETNaNPC(dval);
				  NaN_flg = SNLEN;
				  mradix = 15;
				  lradix = 3;
				  tab =  lc_digs;
				  SNAN = lc_nan;
				  goto put_pc;
                             }
                          } 
#endif	/* defined(HANDLE_INFNaN) */

			  /* Do the conversion */
                          bp = fconvert(dval, min(prec, MAXFCVT), &decpt,
                                        &sign, cvtbuf);
#ifndef	HANDLE_INFNaN
			  if (*bp > '9') {
				inf_nan = 1;
				break;
			  }
#endif	/* !defined(HANDLE_INFNaN) */
			}

			/* Determine the prefix */
		f_merge:
			if (sign) {
				prefix = "-";
				prefixlength = 1;
			} else if (flagword & FPLUS) {
				prefix = "+";
				prefixlength = 1;
			} else if (flagword & FBLANK) {
				prefix = " ";
				prefixlength = 1;
			}

			/* Initialize buffer pointer */
			p = &buf[0];

			{ register int nn = decpt;

				/* Emit the digits before the decimal point */
				k = 0;
				do {
					*p++ = (nn <= 0 || *bp == '\0' 
						|| k >= MAXFSIG) ?
				    		'0' : (k++, *bp++);
				} while (--nn > 0);

				if (quote)
					p = insert_thousands_sep(buf, p);

				/* Decide whether we need a decimal point */
				if ((flagword & FSHARP) || prec > 0)
					*p++ = _numeric[0];

				/* Digits (if any) after the decimal point */
				nn = min(prec, MAXFCVT);
				if (prec > nn) {
					flagword |= RZERO;
					otherlength = rzero = prec - nn;
				}
				while (--nn >= 0)
					*p++ = (++decpt <= 0 || *bp == '\0' ||
				   	    k >= MAXFSIG) ? '0' : (k++, *bp++);
			}

			bp = &buf[0];

			break;

		case 'G':
		case 'g':
			/*
			 * g-format.  We play around a bit
			 * and then jump into e or f, as needed.
			 */
		
			/* Establish default precision */
			if (!(flagword & DOTSEEN))
				prec = 6;
			else if (prec == 0)
				prec = 1;

			if (flagword & QUAD) {	/* long double */
			  /* Fetch the value */
			  quadval = GETQVAL(args.ap);

#ifdef	HANDLE_INFNaN
			  /* Check for NaN and Infinities  */
			  if (QIsNANorINF(quadval)) {
			     if (QIsINF(quadval)) {
			        if (QIsNegNAN(quadval)) 
				  neg_in = 1;
			     bp = (fcode == 'G') ? uc_inf : lc_inf;
			     p = bp + 3;
                             inf_nan = 1;
			     break;
                             }
                             else {
				  if (QIsNegNAN(quadval)) 
					  neg_in = 1;
				  inf_nan = 1;
			          val  = QGETNaNPC(quadval);
				  NaN_flg = SNLEN;
				  mradix = 15;
				  lradix = 3;
				  if ( fcode == 'G') {
					SNAN = uc_nan;
					tab = uc_digs;
				  }
				  else {
					SNAN = lc_nan;
					tab =  lc_digs;
				  }
				  goto put_pc;
                             }
                          }
#endif	/* defined(HANDLE_INFNaN) */

			  /* Do the conversion */
                          bp = qeconvert(&quadval, min(prec,MAXECVT), &decpt,
                                        &sign, cvtbuf);
#ifndef	HANDLE_INFNaN
			  if (*bp > '9') {
				inf_nan = 1;
				break;
			  }
#endif	/* !defined(HANDLE_INFNaN) */
			  if (quadval == 0)
				  decpt = 1;
			} else {			/* double */
			  /* Fetch the value */
			  dval = va_arg(args.ap, double);

#ifdef	HANDLE_INFNaN
			  /* Check for NaN and Infinities  */
			  if (IsNANorINF(dval)) {
			     if (IsINF(dval)) {
			        if (IsNegNAN(dval)) 
				  neg_in = 1;
			     bp = (fcode == 'G') ? uc_inf : lc_inf;
			     p = bp + 3;
                             inf_nan = 1;
			     break;
                             }
                             else {
				  if (IsNegNAN(dval)) 
					  neg_in = 1;
				  inf_nan = 1;
			          val  = GETNaNPC(dval);
				  NaN_flg = SNLEN;
				  mradix = 15;
				  lradix = 3;
				  if ( fcode == 'G') {
					SNAN = uc_nan;
					tab = uc_digs;
				  }
				  else {
					SNAN = lc_nan;
					tab =  lc_digs;
				  }
				  goto put_pc;
                             }
                          }
#endif	/* defined(HANDLE_INFNaN) */

			  /* Do the conversion */
                          bp = econvert(dval, min(prec, MAXECVT), &decpt,
                                        &sign, cvtbuf);
#ifndef	HANDLE_INFNaN
			  if (*bp > '9') {
				inf_nan = 1;
				break;
			  }
#endif	/* !defined(HANDLE_INFNaN) */
			  if (dval == 0)
				  decpt = 1;
			}

			{ register int kk = prec;
				if (!(flagword & FSHARP)) {
					n = strlen(bp);
					if (n < kk)
						kk = n;
					while (kk >= 1 && bp[kk-1] == '0')
						--kk;
				}
				
				if (decpt < -3 || decpt > prec) {
					prec = kk - 1;
					goto e_merge;
				}
				prec = kk - decpt;
				goto f_merge;
			}

		case '%':
			buf[0] = (char)fcode;
			goto c_merge;
		
		case 'w':
			wflag = 1;
			goto charswitch;

		case 'C': /* xpg xsh4 extention */
			if (!wflag)
				/* force %wC, since %C is a nop */
				wflag++;
		case 'c':
			if (wflag) {
				wchar_t temp[2];
				temp[0] = va_arg(args.ap, int);
				if (temp[0]) {
					temp[1] = 0;
					wcstombs(buf, temp, sizeof(buf)-1);
					p = (bp = buf) + strlen(buf);
				}
				else { /* NULL character */
					buf[0] = 0;
					p = (bp = buf) + 1;
				}
				wcount = (int)p -(int)bp;
			}
			else {
#if ! defined(_NO_LONGLONG)
				if (flagword & XLONG) 
					buf[0] = va_arg(args.ap, long long);
				else
#endif /* ! defined(_NO_LONGLONG) */
					buf[0] = va_arg(args.ap, int);
			c_merge:
				p = (bp = &buf[0]) + 1;
			}
			break;

		case 'S': /* xpg xsh4 extention */
			if (!wflag)
				/* force %wS, since %S is a nop */
				wflag++;
			bp = va_arg(args.ap, char *);
			if (!(flagword & DOTSEEN)) {
				if (wflag) {
					/* wide character handling */
					prec = MAXINT;
					goto wide_hand_1;
				} else {
					p = bp + strlen(bp);
					sec_display 
					    = eucscol((unsigned char *)bp);
				}
			} else { /* a strnlen function would  be useful here! */
				if (wflag) {
					/* wide character handling */

				wide_hand_1:
					wp = (wchar_t *)bp;
					wcount = 0;
					while ((*wp) && (prec - wcount > 1)) {
						wcount += wctomb(tmpbuf, *wp);
						sec_display += scrwidth(*wp);
						wp++;
					}
					p = (char *)wp;
				} else {
					register char *qp = bp;
					int dhl;

					preco = prec;
					while(*qp) {
						dhl = 
						    euccol((unsigned char *)qp);
						/* if we're at the end, and
						 * the next char is special,
						 * then stop.
						 */
						if ((prec == 0) &&
                                                    (dhl == 0)) {
                                                        break;
                                                }
						if ((prec -= dhl) >= 0) {
						qp += mblen(qp, MB_LEN_MAX);
							if (prec == 0)
								break;
						} else {
							break;
						}
					}
					if (prec < 0)
						prec += dhl;
					p = qp;
					sec_display = preco - prec;
				}
			}
			break;

		case 's':
			bp = va_arg(args.ap, char *);
			if (!(flagword & DOTSEEN)) {
				if (wflag) {
					/* wide character handling */
					prec = MAXINT;
					goto wide_hand;
				} else {
					p = bp + strlen(bp);
					sec_display 
					    = eucscol((unsigned char *)bp);
				}
			}
			else { /* a strnlen function would  be useful here! */
				if (wflag) {
					/* wide character handling */

				wide_hand:
					wp = (wchar_t *)bp;
					preco = prec;
					wcount = 0;
					while (*wp &&
					(prec -= scrwidth(*wp)) >= 0) {
						wcount += wctomb(tmpbuf, *wp);
						wp++;
					}
					if (*wp)
						prec += scrwidth(*wp);
					p = (char *)wp;
					sec_display = preco - prec;
				} else {
					register char *qp = bp;
					int ncol;

					preco = prec;
					while(*qp) {
						ncol =
						    _priv_euccol((unsigned char *)qp);
						if ((prec -= ncol) >= 0) {
							qp += mblen(qp, MB_LEN_MAX);
							if (prec == 0)
								break;
						} else {
							break;
						}
					}
					if (prec < 0)
						prec += ncol;
					p = qp;
					sec_display = preco - prec;
				}
			}
			break;

		case 'n':
		      {
			/*
			if (flagword & XLONG) {
				long long *svcount;
				svcount = va_arg(args.ap, long long *);
				*svcount = (long long) count;
			} else 
			*/
			if (flagword & LENGTH) {
				long *svcount;
				svcount = va_arg(args.ap, long *);
				*svcount = count;
			} else if (flagword & SHORT) {
				short *svcount;
				svcount = va_arg(args.ap, short *);
				*svcount = (short)count;
			} else {
				int *svcount;
				svcount = va_arg(args.ap, int *);
				*svcount = count;
			}
			continue;
		      }

		default: /* this is technically an error; what we do is to */
			/* back up the format pointer to the offending char */
			/* and continue with the format scan */
			format--;
			continue;

		}
       
		if (inf_nan) {
#ifndef	HANDLE_INFNaN
		   for (p = bp+1; *p != '\0'; p++)
			;
		   if (sign)
#else	/* !defined(HANDLE_INFNaN) */
		   if (neg_in)
#endif	/* !defined(HANDLE_INFNaN) */
		   {
			prefix = "-";
			prefixlength = 1;
#ifdef	HANDLE_INFNaN
			neg_in = 0;
#endif	/* defined(HANDLE_INFNaN) */
                   }
		   else if (flagword & FPLUS) {
			prefix = "+";
			prefixlength = 1;
			}
		   else if (flagword & FBLANK) {
			prefix = " ";
			prefixlength = 1;
		   }
		   inf_nan = 0;
		}
		 
		/* Calculate number of padding blanks */
		n = p - bp; /* n == size of the converted value (in bytes) */

		if (sec_display) /* when format is %s or %ws or %S */
			if (fcode == 'S')
				k = max(sec_display, wcount);
			else
				k = sec_display;

		else
			k = n;
		/* k is the (screen) width of the converted value */
		k += prefixlength + otherlength
#ifdef	HANDLE_INFNaN
			+ NaN_flg
#endif	/* defined(HANDLE_INFNaN) */
			;

		/* update count which is the overall size of the output data
		 * and passed to memchr()
		 */
		if (wflag)
		/*
		 * when wflag != 0 (i.e. %ws or %wc), the size of the converted
		 * value is wcount bytes
		 */
			count += wcount;
		else
		/*
		 * when wflag == 0, the size of the converted
		 * value is n (= p-bp) bytes
		 */
			count += n;
		count += prefixlength + otherlength
#ifdef	HANDLE_INFNaN
			+ NaN_flg
#endif	/* defined(HANDLE_INFNaN) */
			;

		if (width > k) {
			count += (width - k);
			/*
			 * Set up for padding zeroes if requested
			 * Otherwise emit padding blanks unless output is
			 * to be left-justified.
			 */

			if (flagword & PADZERO) {
				if (!(flagword & LZERO)) {
					flagword |= LZERO;
					lzero = width - k;
				} else
					lzero += width - k;
				k = width; /* cancel padding blanks */
			} else
				/* Blanks on left if required */
				if (!(flagword & FMINUS))
					PAD(_blanks, width - k);
		}

		/* Prefix, if any */
		if (prefixlength != 0)
			PUT(prefix, prefixlength);

#ifdef	HANDLE_INFNaN
		/* If value is NaN, put string NaN*/
		if (NaN_flg) {
			PUT(SNAN,SNLEN);
			NaN_flg = 0;
                }
#endif	/* defined(HANDLE_INFNaN) */

		/* Zeroes on the left */
		if ((flagword & LZERO) /* &&
			(!(flagword & SHORT) || !(flagword & FMINUS)) */ )
			PAD(_zeroes, lzero);
		
		/* The value itself */
		if ((fcode == 's' || fcode == 'S') && wflag) {
			/* wide character handling */
			wchar_t *wp = (wchar_t *)bp;
			int cnt;
			char *bufp;
			int printn;

			printn = (wchar_t *)p - (wchar_t *)bp;
			bufp = buf;
			while (printn > 0) {
				cnt = wctomb(buf, *wp);
				PUT (bufp, cnt);
				wp++;
				printn--;
			}
		}
		else {	/* non wide character value */
			if (n > 0)
				PUT(bp, n);
		}

		if (flagword & (RZERO | SUFFIX | FMINUS)) {
			/* Zeroes on the right */
			if (flagword & RZERO)
				PAD(_zeroes, rzero);

			/* The suffix */
			if (flagword & SUFFIX)
				PUT(suffix, suffixlength);

			/* Blanks on the right if required */
			if (flagword & FMINUS && width > k)
				PAD(_blanks, width - k);
		}
	}
}

/* This function initializes arglst, to contain the appropriate va_list values
 * for the first MAXARGS arguments. */
void
_mkarglst(fmt, args, arglst)
char	*fmt;
stva_list args;
stva_list arglst[];
{
	static char digits[] = "01234567890", skips[] = "# +-.'0123456789h$";

	enum types {INT = 1, LONG, CHAR_PTR, DOUBLE, LONG_DOUBLE, VOID_PTR,
		LONG_PTR, INT_PTR, LONG_LONG /*, LONG_LONG_PTR*/};
	enum types typelst[MAXARGS], curtype;
	int maxnum, n, curargno, flags;

	/*
	* Algorithm	1. set all argument types to zero.
	*		2. walk through fmt putting arg types in typelst[].
	*		3. walk through args using va_arg(args.ap, typelst[n])
	*		   and set arglst[] to the appropriate values.
	* Assumptions:	Cannot use %*$... to specify variable position.
	*/

	(void)memset((VOID *)typelst, 0, sizeof(typelst));
	maxnum = -1;
	curargno = 0;
	while ((fmt = strchr(fmt, '%')) != 0)
	{
		fmt++;	/* skip % */
		if (fmt[n = strspn(fmt, digits)] == '$')
		{
			curargno = atoi(fmt) - 1;	/* convert to zero base */
			if (curargno < 0)
				continue;
			fmt += n + 1;
		}
		/*
		   flags = 0x01		for long
			 = 0x02		for int
			 = 0x04		for long long
			 = 0x08		for long double
		 */
		flags = 0;
	again:;
		fmt += strspn(fmt, skips);
		switch (*fmt++)
		{
		case '%':	/*there is no argument! */
			continue;
		case 'l':
			if (flags & 0x5) {
			  flags |= 0x4;
			  flags &= ~0x1;
			} else {
			  flags |= 0x1;
			}
			goto again;
		case '*':	/* int argument used for value */
			/* check if there is a positional parameter */
			if (isdigit(*fmt)) {
				int	targno;
				targno = atoi(fmt) - 1;
				fmt += strspn(fmt, digits);
				if (*fmt == '$')
					fmt++; /* skip '$' */
				if (targno >= 0 && targno < MAXARGS) {
					typelst[targno] = INT;
					if (maxnum < targno)
						maxnum = targno;
				}
				goto again;
			}
			flags |= 0x2;
			curtype = INT;
			break;
		case 'L':
			flags |= 0x8;
			goto again;
		case 'e':
		case 'E':
		case 'f':
		case 'g':
		case 'G':
			if (flags & 0x8)
			  curtype = LONG_DOUBLE;
			else
			  curtype = DOUBLE;
			break;
		case 's':
			curtype = CHAR_PTR;
			break;
		case 'p':
			curtype = VOID_PTR;
			break;
		case 'n':
			/*
			if (flags & 0x4)
				curtype = LONG_LONG_PTR;
			else 
			*/
			if (flags & 0x1)
				curtype = LONG_PTR;
			else
				curtype = INT_PTR;
			break;
		default:
			if (flags & 0x4)
				curtype = LONG_LONG;
			else if (flags & 0x1)
				curtype = LONG;
			else
				curtype = INT;
			break;
		}
		if (curargno >= 0 && curargno < MAXARGS)
		{
			typelst[curargno] = curtype;
			if (maxnum < curargno)
				maxnum = curargno;
		}
		curargno++;	/* default to next in list */
		if (flags & 0x2)	/* took care of *, keep going */
		{
			flags ^= 0x2;
			goto again;
		}
	}
	for (n = 0 ; n <= maxnum; n++)
	{
		arglst[n] = args;
		if (typelst[n] == 0)
			typelst[n] = INT;
		
		switch (typelst[n])
		{
		case INT:
			(void) va_arg(args.ap, int);
			break;
		case LONG:
			(void) va_arg(args.ap, long);
			break;
		case CHAR_PTR:
			(void) va_arg(args.ap, char *);
			break;
		case DOUBLE:
			(void) va_arg(args.ap, double);
			break;
		case LONG_DOUBLE:
			(void) GETQVAL(args.ap);
			break;
		case VOID_PTR:
			(void) va_arg(args.ap, VOID *);
			break;
		case LONG_PTR:
			(void) va_arg(args.ap, long *);
			break;
		case INT_PTR:
			(void) va_arg(args.ap, int *);
			break;
#if ! defined(_NO_LONGLONG)
		case LONG_LONG:
			(void) va_arg(args.ap, long long);
			break;
#endif /* ! defined(_NO_LONGLONG) */
		/*
		case LONG_LONG_PTR:
			(void) va_arg(args.ap, long long *);
			break;
		*/
		}
	}
}

/*
 * This function is used to find the va_list value for arguments whose
 * position is greater than MAXARGS.  This function is slow, so hopefully
 * MAXARGS will be big enough so that this function need only be called in
 * unusual circumstances.
 * pargs is assumed to contain the value of arglst[MAXARGS - 1].
 */
void
_getarg(fmt, pargs, argno)
char	*fmt;
stva_list *pargs;
int	argno;
{
	/*
		flags & 0x1		for 'l'
		flags & 0x2		for '*'
		flags & 0x4		for 'll'
		flags & 0x8		for 'L'
	*/
	static char digits[] = "01234567890", skips[] = "# +-.'0123456789h$";
	int i, n, curargno, flags;
	char	*sfmt = fmt;
	int	found = 1;

	i = MAXARGS;
	curargno = 1;
	while (found)
	{
		fmt = sfmt;
		found = 0;
		while ((i != argno) && (fmt = strchr(fmt, '%')) != 0)
		{
			fmt++;	/* skip % */
			if (fmt[n = strspn(fmt, digits)] == '$')
			{
				curargno = atoi(fmt);
				if (curargno <= 0)
					continue;
				fmt += n + 1;
			}

			/* find conversion specifier for next argument */
			if (i != curargno)
			{
				curargno++;
				continue;
			} else
				found = 1;
			flags = 0;
		again:;
			fmt += strspn(fmt, skips);
			switch (*fmt++)
			{
			case '%':	/*there is no argument! */
				continue;
			case 'l':
				if (flags & 0x5) {
				  flags |= 0x4;
				  flags &= ~0x1;
				} else {
				  flags |= 0x1;
				}
				goto again;
			case 'L':
				flags |= 0x8;
				goto again;
			case '*':	/* int argument used for value */
				/* check if there is a positional parameter;
				 * if so, just skip it; its size will be
				 * correctly determined by default */
				if (isdigit(*fmt)) {
					fmt += strspn(fmt, digits);
					if (*fmt == '$')
						fmt++; /* skip '$' */
					goto again;
				}
				flags |= 0x2;
				(void)va_arg((*pargs).ap, int);
				break;
			case 'e':
			case 'E':
			case 'f':
			case 'g':
			case 'G':
				if (flags & 0x8)
					(void) GETQVAL((*pargs).ap);
				else
					(void)va_arg((*pargs).ap, double);
				break;
			case 's':
				(void)va_arg((*pargs).ap, char *);
				break;
			case 'p':
				(void)va_arg((*pargs).ap, void *);
				break;
			case 'n':
				/*
				if (flags & 0x4)
					(void)va_arg((*pargs).ap, long long *);
				else 
				*/
				if (flags & 0x1)
					(void)va_arg((*pargs).ap, long *);
				else
					(void)va_arg((*pargs).ap, int *);
				break;
			default:
#if ! defined(_NO_LONGLONG)
				if (flags & 0x4)
					(void)va_arg((*pargs).ap, long long);
				else 
#endif /* ! defined(_NO_LONGLONG) */
				if (flags & 0x1)
					(void)va_arg((*pargs).ap, long int);
				else
					(void)va_arg((*pargs).ap, int);
				break;
			}
			i++;
			curargno++;	/* default to next in list */
			if (flags & 0x2)	/* took care of *, keep going */
			{
				flags ^= 0x2;
				goto again;
			}
		}

		/* missing specifier for parameter, assume parameter is an int */
		if (!found && i != argno) {
			(void)va_arg((*pargs).ap, int);
			i++;
			curargno = i;
			found = 1;
		}
	}
}

static 
char *insert_thousands_sep(bp, ep)
	char *bp;
	char *ep;
{
	char thousep;
	struct lconv *locptr;
	int buf_index, i;
	char *obp = bp;
	char buf[371];
	char *bufptr = buf;
	char *grp_ptr;

	/* get the thousands sep. from the current locale */
	locptr = localeconv();
	thousep	= *locptr->thousands_sep;
	grp_ptr = locptr->grouping;

	/* thousands sep. not use in this locale or no grouping required */
	if (!thousep || (*grp_ptr == '\0'))
		return (ep);

	buf_index = ep - bp;
	while (1) {
		if (*grp_ptr == -1) {
			for (i=0; i < buf_index--; i++)
				*bufptr++ = *(bp + buf_index);
			break;
		}
		for (i=0; i < *grp_ptr && buf_index-- > 0; i++)
			*bufptr++ = *(bp + buf_index);

		if (buf_index > 0) {
			*bufptr++ = thousep;
			ep++;
		}
		else
			break;
		if (*(grp_ptr + 1) != '\0')
			++grp_ptr;
	}

	/* put the string in the caller's buffer in reverse order */
	--bufptr;
	while (buf <= bufptr)
		*obp++ = *bufptr--;
	return (ep);
}

/*
 * _priv_euccol(s) returns the screen column width of the EUC char.
 *  This private version of euccol considers SS2 or SS3 as members of
 *  codeset 1 if codesets 2 or 3 are not in use.
 */
static int
_priv_euccol(const unsigned char *s)
{
	if (ISASCII(*s))
		return (1);
	else
		switch (*s) {
		case SS2:
			if (scrw2 == 0)
				goto lab1;
			return (scrw2);
		case SS3:
			if (scrw3 == 0)
				goto lab1;
			return (scrw3);
lab1:
		default: /* code set 1 */
			return (scrw1);
		}
}
