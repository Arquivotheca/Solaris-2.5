#ident	"@(#)dostrfmon.c	1.1	93/12/08 SMI"

/*LINTLIBRARY*/
#include "synonyms.h"
#include "shlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include <varargs.h>
#include <values.h>
#include <memory.h>
#include <string.h>
#include "print.h"	/* parameters & macros for doprnt */
#include "stdiom.h"
#include <locale.h>
#include "../gen/_locale.h"
#include <thread.h>
#include <synch.h>
#include <mtlib.h>
#include <errno.h>

#define	PUT(p, n) \
		{ register unsigned char *newbufptr; \
			if ((newbufptr = bufptr + n) > bufferend) { \
				if (!_dowrite(p, n, iop, &bufptr)) \
					return (EOF); \
			} else { \
				(void) memcpy((char *) bufptr, p, n); \
				bufptr = newbufptr; \
			}\
		}
#define	PAD(s, n) \
		{ register int nn;\
		    for (nn = n; nn > 20; nn -= 20) \
			if (!_dowrite(s, 20, iop, &bufptr)) \
				return (EOF); \
			PUT(s, nn); \
		}

#define	PUT_SIGN() \
		if (prefixlength != 0) { \
			PUT(prefix, prefixlength); \
		} else {  \
			if (flagword&FSHARP) { \
				int len; \
				if (flagword&FPAR) \
					len = 1; \
				else { \
					int pos; \
					int neg; \
					pos = strlen(lc->positive_sign); \
					neg = strlen(lc->negative_sign); \
					if (pos > neg) \
						len = pos; \
					else \
						len = neg; \
					if (len == 0) \
						len = 1; \
				} \
				PUT(_blanks, len); \
			} \
		}

#define	PUT_CURRENCY() \
		if (!(flagword & NOCURR)) { \
			if (flagword & ISYMBOL) { \
				if (currlength != 0) \
					PUT(lc->int_curr_symbol, currlength); \
			} else { \
				if (currlength != 0) \
					PUT(lc->currency_symbol, currlength); \
			} \
		}

#define	PUT_VALUE() \
		if (flagword & LZERO) { \
			char _padchar[20]; \
			int i; \
			for (i = 0; i < 20; i++) \
				_padchar[i] = padchar; \
			PAD(_padchar, lzero-adjzero); \
		} \
		if (n > 0) \
			PUT(bp, n); \
		if (flagword & (RZERO | SUFFIX | FMINUS)) { \
			/* Zeroes on the right */ \
			if (flagword & RZERO) \
				PAD(_zeroes, rzero); \
			/* Blanks on the right if required */ \
			if (flagword & FMINUS && width > k) \
				PAD(_blanks, width - k); \
		}

#define	PUT_SUFFIX() \
		if (flagword & SUFFIX) \
			PUT(suffix, suffixlength);

/* bit positions for flags used in dostrfmon */
#define	FPLUS	   0x01	/* + */
#define	FMINUS	  0x02	/* - */
#define	FSHARP	  0x08	/* # */
#define	PADZERO   0x10	/* padding zeroes requested via '0' */
#define	DOTSEEN   0x20	/* dot appeared in format specification */
#define	SUFFIX	  0x40	/* a suffix is to appear in the output */
#define	RZERO	  0x80	/* there will be trailing zeros in output */
#define	LZERO	  0x100	/* there will be leading zeroes in output */
#define	FPAR	  0x200	/* left paren seen */
#define	NOCURR	  0x400	/* supress currency */
#define	NOGROUP	  0x800	/* No grouping */
#define	ISYMBOL   0x1000

extern char *fconvert();
static int _dowrite();

/*
 * stva_list is used to subvert C's restriction that a variable with an
 * array type can not appear on the left hand side of an assignment operator.
 * By putting the array inside a structure, the functionality of assigning to
 * the whole array through a simple assignment is achieved..
 */
typedef struct stva_list {
	va_list	ap;
} stva_list;

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
			return (0);
		}
		*ptrptr = iop->_ptr;
	} else
		*ptrptr = (unsigned char *) memcpy((char *) *ptrptr, p, n) + n;
	return (1);
}

	static char _blanks[] = "                    ";
	static char _zeroes[] = "00000000000000000000";

int
_dostrfmon_unlocked(limit_cnt, format, in_args, iop)
int limit_cnt;
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
	int padded_blank = 0;
	int adjzero;

	/* This variable counts output characters. */
	int	count = 0;

	/* Starting and ending points for value to be printed */
	register char	*bp;
	char *p;

	/* Field width and precision */
	int	width = 0;
	int 	prec = 0;
	int	lprec = 0;

	/* Format code */
	register int	fcode;

	/* Number of padding zeroes required on the left and right */
	int	lzero, rzero;
	char padchar = ' ';

	/* Flags - bit positions defined by LENGTH, FPLUS, FMINUS, FBLANK, */
	/* and FSHARP are set if corresponding character is in format */
	/* Bit position defined by PADZERO means extra space in the field */
	/* should be padded with leading zeroes rather than with blanks */
	register int	flagword;

	/*
	 * Other monetary information
	 */
	char p_cs_precedes;
	char n_cs_precedes;
	char cs_precedes;
	char p_sign_posn;
	char n_sign_posn;
	char sign_posn;
	char p_sep_by_space;
	char n_sep_by_space;
	char sep_by_space;

	/* Values are developed in this buffer */
	char	buf[max(MAXLLDIGS, 1034)];
	char	cvtbuf[DECIMAL_STRING_LENGTH];
	char	*prefix;
	char	*suffix;

	/* Length of prefix and of suffix */
	int	prefixlength, suffixlength, currlength;

	/* Combined length of leading zeroes, trailing zeroes, and suffix */
	int 	otherlength;

	/* number of thousand separactors used */
	int num_1000_sep = 0;

	/* The value being converted, if real */
	double	dval;

	/* Output values from fcvt and ecvt */
	int	decpt, sign;

	/* Work variables */
	int	k;

	/* localeconv */
	struct lconv *lc;
	int mon_grouping;
	char mon_sep;


	/* variables for positional parameters */
	stva_list	args;	/* used to step through the argument list */
	stva_list	bargs;	/* used to restore args if positional width */
				/* or precision */
	int	starflg = 0;	/* set to 1 if * format specifier seen */
	/*
	 * Initialize args and sargs to the start of the argument list.
	 * Note that ANSI guarantees that the address of the first member of
	 * a structure will be the same as the address of the structure.
	 */
	args = *(struct stva_list *)&in_args;

	/*
	 * Get currentcy information
	 */
	lc = localeconv();
	if (lc == NULL)
		return (-1);
	mon_grouping = *lc->mon_grouping;
	if (strlen(lc->mon_thousands_sep) == 0)
		mon_sep = ',';
	else {
		mon_sep = *lc->mon_thousands_sep;
		if (mon_grouping == 0)
			mon_grouping = 3;
	}
	p_cs_precedes = lc->p_cs_precedes;
	if (p_cs_precedes != 1 && p_cs_precedes != 0)
		p_cs_precedes = 1;
	n_cs_precedes = lc->n_cs_precedes;
	if (n_cs_precedes != 1 && n_cs_precedes != 0)
		n_cs_precedes = 1;
	p_sign_posn = lc->p_sign_posn;
	if (p_sign_posn > 4 || p_sign_posn < 0)
		p_sign_posn = 1;
	n_sign_posn = lc->n_sign_posn;
	if (n_sign_posn > 4 || n_sign_posn < 0)
		n_sign_posn = 1;
	p_sep_by_space = lc->p_sep_by_space;
	if (p_sep_by_space > 3 || p_sep_by_space < 0)
		p_sep_by_space = 0;
	n_sep_by_space = lc->n_sep_by_space;
	if (n_sep_by_space > 3 || n_sep_by_space < 0)
		n_sep_by_space = 0;

	/* if first I/O to the stream get a buffer */
	/* Note that iop->_base should not equal 0 for sprintf and vsprintf */
	if (iop->_base == 0 && _findbuf(iop) == 0)
		return (EOF);

	/* initialize buffer pointer and buffer end pointer */
	bufptr = iop->_ptr;
	bufferend = (iop->_flag & _IOREAD) ?
			(unsigned char *)((long) bufptr | (-1L & ~HIBITL))
			: _bufend(iop);
	/*
	 *	The main loop -- this loop goes through one iteration
	 *	for each string of ordinary characters or format specification.
	 */
	for (; ; ) {
		register int n;

		if ((fcode = *format) != '\0' && fcode != '%') {
			bp = format;
			do {
				format++;
			} while ((fcode = *format) != '\0' && fcode != '%');

			count += (n = format - bp); /* n = no. of non-% chars */
			if (count > limit_cnt) {
				errno = E2BIG;
				return (-1);
			}
			PUT(bp, n);
		}
		if (fcode == '\0') {  /* end of format; return */
			register int nn = bufptr - iop->_ptr;
			iop->_cnt -= nn;
			iop->_ptr = bufptr;
			if (bufptr + iop->_cnt > bufferend &&
					!(iop->_flag & _IOREAD))
			_bufsync(iop, bufferend);
			if (iop->_flag & (_IONBF | _IOLBF) &&
			    (iop->_flag & _IONBF ||
			memchr((char *)(bufptr-count), '\n', count) != NULL))
				(void) _xflsbuf(iop);
			return (FERROR(iop) ? EOF : count);
		}

		/*
		 *	% has been found.
		 *	The following switch is used to parse the format
		 *	specification and to perform the operation specified
		 *	by the format letter.  The program repeatedly goes
		 *	back to this switch until the format letter is
		 *	encountered.
		 */
		width = prefixlength = otherlength = flagword = 0;
		currlength = suffixlength = 0;
		format++;

	charswitch:
		switch (fcode = *format++) {
		case '+':
			flagword |= FPLUS;
			flagword &= ~FPAR;
			goto charswitch;
		case '(':
			flagword |= FPAR;
			flagword &= ~FPLUS;
			goto charswitch;
		case '-':
			flagword |= FMINUS;
			flagword &= ~PADZERO; /* ignore 0 flag */
			goto charswitch;
		case '!':
			flagword |= NOCURR;
			goto charswitch;
		case '^':
			flagword |= NOGROUP;
			goto charswitch;
		case '#':
			{
			int num = 0;
			flagword |= FSHARP;
			while (isdigit(fcode = *format)) {
				num = num * 10 + fcode - '0';
				format++;
			}
			lprec = num;
			goto charswitch;
			}
		/* Scan the field width and precision */
		case '.':
			flagword |= DOTSEEN;
			prec = 0;
			goto charswitch;

		case '=':
			padchar = *format++;
			if (!(flagword & (DOTSEEN | FMINUS)))
				flagword |= PADZERO;
			goto charswitch;
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
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

		case 'i':
			flagword |= ISYMBOL;
		case 'n':
			if (!(flagword & DOTSEEN)) {
				if (fcode == 'i')
					prec = lc->int_frac_digits;
				else
					prec = lc->frac_digits;
			}
			if (prec > 20)
				prec = 2;

			dval = va_arg(args.ap, double);
			bp = fconvert(dval, min(prec, MAXFCVT), &decpt,
					&sign, cvtbuf);

			/* Determine the prefix */
			if (sign) {
				if (flagword & FPAR) {
					flagword |= SUFFIX;
					prefix = "(";
					suffix = ")";
					suffixlength = 1;
				} else {
					prefix = lc->negative_sign;
					if (strlen(prefix) == 0)
						prefix = "-";
				}
				prefixlength = strlen(prefix);
			} else if (flagword & FPLUS) {
				prefix = lc->positive_sign;
				prefixlength = strlen(prefix);
			}

			/* Initialize buffer pointer */
			p = &buf[0];
/*
 * INDENTED
 */
{ register int nn = decpt;
	int mod;
	int cntr = 0;
	int i;
	if (mon_grouping != 0 && !(flagword & NOGROUP)) {
		mod = decpt -
		(decpt/mon_grouping)*mon_grouping;
		i = num_1000_sep = (decpt-mod)/mon_grouping;
		if (mod == 0) {
			num_1000_sep -= 1;
			i -= 1;
		}
	}

	/* Emit the digits before the decimal point */
	k = 0;
	/*
	 * If left precision is specified, fill in
	 * filler character.
	 */
	if (flagword & FSHARP) {
		if (mon_grouping != 0)
			lprec += lprec/mon_grouping;
		if (lprec > decpt + num_1000_sep) {
			int i = lprec - decpt - num_1000_sep;
			while (i-- > 0) {
				*p++ = padchar;
			}
		}
	}
	do {
		if ((flagword & NOGROUP) || mon_grouping <= 0)
			*p++ = (nn <= 0 || *bp == '\0' ||
			k >= MAXFSIG) ? '0' : (k++, *bp++);
		else {
			/*
			 * Grouping
			 */
			if (nn <= 0 || *bp == '\0' || k >= MAXFSIG)
				*p++ = '0';
			else {
				if (mod > 0 && i != 0) {
					*p++ = *bp++;
					k++;
					mod--;
					if (mod == 0) {
						*p++ = mon_sep;
						--i;
					}
				} else {
					*p++ = *bp++;
					k++;
					++cntr;
					if (cntr == mon_grouping &&
					    i != 0) {
						*p++ = mon_sep;
						--i;
						cntr = 0;
					}
				}
			}
		}
	} while (--nn > 0);

	/* Decide whether we need a decimal point */
	if ((flagword & FSHARP) || prec > 0) {
		if (strcmp(lc->mon_decimal_point, ""))
			*p++ = *lc->mon_decimal_point;
		else
			*p++ = '.';
	}

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
/*
 * END INDENTED
 */
			bp = &buf[0];
			break;

		case '%':
			buf[0] = (char)fcode;
			p = (bp = &buf[0]) + 1;
			break;
		default: /* this is technically an error; what we do is to */
			/* back up the format pointer to the offending char */
			/* and continue with the format scan */
			format--;
			continue;
		}
		/*
		 * calculate currency symbol length
		 */
		if (!(flagword & NOCURR)) {
			if (flagword & ISYMBOL) {
				currlength = strlen(lc->int_curr_symbol);
			} else {
				currlength = strlen(lc->currency_symbol);
			}
		}

		/* Calculate number of padding blanks */
		n = p - bp; /* n == size of the converted value (in bytes) */
		k = n;
		/* k is the (screen) width of the converted value */
		k += prefixlength + otherlength + currlength;

		/* update count which is the overall size of the output data */
		count += n;
		count += prefixlength + otherlength + currlength;
		if (count > limit_cnt) {
			errno = E2BIG;
			return (-1);
		}

		if (width > k) {
			count += (width - k);
			if (count > limit_cnt) {
				errno = E2BIG;
				return (-1);
			}
			/*
			 * Set up for padding zeroes if requested
			 * Otherwise emit padding blanks unless output is
			 * to be left-justified.
			 */

			if (!(flagword & FMINUS)) {
				if (flagword & PADZERO) {
					padded_blank = width - k - 1;
				} else {
					padded_blank = width - k;
				}
				adjzero = width - k;
				PAD(_blanks, padded_blank);
			}
			if (flagword & PADZERO) {
				if (!(flagword & LZERO)) {
					flagword |= LZERO;
					lzero = width - k;
				} else
					lzero += width - k;
				k = width; /* cancel padding blanks */
			}
		}

		if (sign) {
			cs_precedes = n_cs_precedes;
			sign_posn = n_sign_posn;
			sep_by_space = n_sep_by_space;
		} else {
			cs_precedes = p_cs_precedes;
			sign_posn = p_sign_posn;
			sep_by_space = p_sep_by_space;
		}

		if (flagword & FPAR) {
			PUT_SIGN()
			if (cs_precedes == 1) {
				PUT_CURRENCY()
				if (sep_by_space == 1)
					PUT(_blanks, 1);
				PUT_VALUE()
			} else {
				PUT_VALUE()
				if (sep_by_space == 1)
					PUT(_blanks, 1);
				PUT_CURRENCY()
			}
			PUT_SUFFIX()
		} else {
			if (cs_precedes == 1) {
				switch (sign_posn) {
				case 0:
					switch (sep_by_space) {
					case 0:
						PUT_CURRENCY()
						PUT_VALUE()
						break;
					case 1:
						PUT_CURRENCY()
						PUT(_blanks, 1);
						PUT_VALUE()
						break;
					case 2:
						PUT_CURRENCY()
						PUT_VALUE()
						break;
					}
					break;
				case 1:
					switch (sep_by_space) {
					case 0:
						PUT_SIGN()
						PUT_CURRENCY()
						PUT_VALUE()
						break;
					case 1:
						PUT_SIGN()
						PUT_CURRENCY()
						PUT(_blanks, 1);
						PUT_VALUE()
						break;
					case 2:
						PUT_SIGN()
						PUT_CURRENCY()
						PUT_VALUE()
						break;
					}
					break;
				case 2:
					switch (sep_by_space) {
					case 0:
						PUT_CURRENCY()
						PUT_VALUE()
						PUT_SIGN()
						break;
					case 1:
						PUT_CURRENCY()
						PUT(_blanks, 1);
						PUT_VALUE()
						PUT_SIGN()
						break;
					case 2:
						PUT_CURRENCY()
						PUT_VALUE()
						PUT_SIGN()
						break;
					}
					break;
				case 3:
					switch (sep_by_space) {
					case 0:
						PUT_SIGN()
						PUT_CURRENCY()
						PUT_VALUE()
						break;
					case 1:
						PUT_SIGN()
						PUT_CURRENCY()
						PUT(_blanks, 1);
						PUT_VALUE()
						break;
					case 2:
						PUT_SIGN()
						PUT_CURRENCY()
						PUT_VALUE()
						break;
					}
					break;
				case 4:
					switch (sep_by_space) {
					case 0:
						PUT_CURRENCY()
						PUT_SIGN()
						PUT_VALUE()
						break;
					case 1:
						PUT_CURRENCY()
						PUT_SIGN()
						PUT_VALUE()
						break;
					case 2:
						PUT_CURRENCY()
						PUT(_blanks, 1);
						PUT_SIGN()
						PUT_VALUE()
						break;
					}
					break;
				}
			} else {
				switch (sign_posn) {
				case 0:
					switch (sep_by_space) {
					case 0:
						PUT_VALUE()
						PUT_CURRENCY()
						break;
					case 1:
						PUT_VALUE()
						PUT(_blanks, 1);
						PUT_CURRENCY()
						break;
					case 2:
						PUT_VALUE()
						PUT_CURRENCY()
						break;
					}
					break;
				case 1:
					switch (sep_by_space) {
					case 0:
						PUT_SIGN()
						PUT_VALUE()
						PUT_CURRENCY()
						break;
					case 1:
						PUT_SIGN()
						PUT_VALUE()
						PUT(_blanks, 1);
						PUT_CURRENCY()
						break;
					case 2:
						PUT_SIGN()
						PUT_VALUE()
						PUT_CURRENCY()
						break;
					}
					break;
				case 2:
					switch (sep_by_space) {
					case 0:
						PUT_VALUE()
						PUT_CURRENCY()
						PUT_SIGN()
						break;
					case 1:
						PUT_VALUE()
						PUT(_blanks, 1);
						PUT_CURRENCY()
						PUT_SIGN()
						break;
					case 2:
						PUT_VALUE()
						PUT_CURRENCY()
						PUT(_blanks, 1);
						PUT_SIGN()
						break;
					}
					break;
				case 3:
					switch (sep_by_space) {
					case 0:
						PUT_VALUE()
						PUT_SIGN()
						PUT_CURRENCY()
						break;
					case 1:
						PUT_VALUE()
						PUT_SIGN()
						PUT(_blanks, 1);
						PUT_CURRENCY()
						break;
					case 2:
						PUT_VALUE()
						PUT_SIGN()
						PUT_CURRENCY()
						break;
					}
					break;
				case 4:
					switch (sep_by_space) {
					case 0:
						PUT_VALUE()
						PUT_CURRENCY()
						PUT_SIGN()
						break;
					case 1:
						PUT_VALUE()
						PUT(_blanks, 1);
						PUT_CURRENCY()
						PUT_SIGN()
						break;
					case 2:
						PUT_VALUE()
						PUT_CURRENCY()
						PUT(_blanks, 1);
						PUT_SIGN()
						break;
					}
					break;
				}
			}
		}
	}
}
