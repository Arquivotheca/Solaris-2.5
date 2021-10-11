/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)getdate.c	1.36	95/08/20 SMI"	/* SVr4.0 1.8	*/

#ifdef __STDC__
#pragma weak getdate = _getdate
#endif

#include "synonyms.h"
#include "shlib.h"

#include <fcntl.h>
#include <ctype.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <locale.h>
#include <stdlib.h>
#include <errno.h>
#include <synch.h>
#include <thread.h>
#include <mtlib.h>
#include <unistd.h>
#include "_strftime.h"

extern char	*getenv();
extern int	read();
extern void	free();
extern int	close();
extern int	fstat();
extern int	stat();
extern int	tolower();
extern char	*_setlocale();
extern void	extract_era_info(struct era_t *, char *);

extern int __lyday_to_month[];
extern int __yday_to_month[];
extern int __mon_lengths[2][12];

extern int	_num_eras;
extern char	**_eras;

/*
 * The following are the possible contents of the getdate_err
 * variable and the corresponding error conditions:
 * 1	The DATEMASK environment variable is null or undefined.
 * 2	Error on open of the template file.
 * 3    Error on stat of the template file.
 * 4    The template file is not a regular file.
 * 5    Error on read of the template file.
 * 6	Malloc failed.
 * 7	There is no line in the template that matches the input.
 * 8	Invalid input specification.
 */

static int search(int, int);
static int search_all_months(void);
static int search_all_days(void);
static int search_alt_digits(void);
static int search_for_era_name(void);
static int getdate_setup(void);
static int settime();
static struct	tm  *calc_date(struct tm *);
static int read_tmpl(char *, struct tm *);
static int parse_fmt(char *, int, struct tm *);
static void getnow(struct tm *);
static void strtolower(char *);
static int verify_getdate(struct tm *);
static int verify_strptime(struct tm *);
static void Day(int);
static void DMY(void);
static int days(int);
static int jan1();
static int yday(struct tm *, int);
static int week_number_to_yday(struct tm *, int);
static void year(int);
static void MON(int);
static void Month(int);
static void DOW(int);
static void adddays(int);
static void DOY(int);
static void getdate_end(void);

static char	*input;
static int	era_year_offset;
static int	era_index;
static int	week_number_u;
static int	week_number_w;
static int	century;
static int	hour;
static int	wrong_input;
static int	meridian;
static int	linenum;
static int	calling_function;
static struct   tm  *ct = 0;
#ifdef _REENTRANT
static thread_key_t gd_key = 0;
static mutex_t parse_fmt_lock = DEFAULTMUTEX;
#endif _REENTRANT

enum {f_getdate, f_strptime};
/*
 * Default values.
 */

static char  *saved_locale = 0;
static char **timeFormats;


#define	HUNKSZ	512		/* enough for "C" locale plus slop */

typedef struct Hunk Hunk;
struct	Hunk
{
	size_t	h_busy;
	size_t	h_bufsz;
	Hunk	*h_next;
	char	h_buf[1];
};
static Hunk	*hunk;
static void	inithunk();
static char	*savestr();
static char *sinput; /* start of input buffer */

/*
 * getdate_setup() aquires the parse_fmt_lock mutex and makes
 * sure ct is allocated in thread-specific data. getdate_setup()
 * is present because both strptime() and getdate() need this
 * functionality.
 */
static int
getdate_setup()
{
	_mutex_lock(&parse_fmt_lock);

#ifdef _REENTRANT
	ct = (struct tm *)_tsdalloc(&gd_key, sizeof (struct tm));
#else
	if (!ct)
		ct = malloc(sizeof (struct tm));
#endif /* _REENTRANT */
	if (settime() == 0) {
		getdate_end();
		return (0);
		}
	era_index = -1;
	week_number_u = -1;
	week_number_w = -1;
	return (1);
}


static void
getdate_end()
{
	if (ct)
		free(ct);
	_mutex_unlock(&parse_fmt_lock);
}



struct	tm *
getdate(expression)
const char	*expression;
{

	struct tm t;
	struct tm *res = 0;

	if (getdate_setup() == 0)	/* acquires parse_fmt_lock */
		return (NULL);

	calling_function = f_getdate;
	wrong_input = 0;
	linenum = 1;
	if (read_tmpl((char *)expression, &t)) {
		res = calc_date(&t);
		getdate_end();
		return (res);
	} else {
		if (wrong_input)
			getdate_err = 8;
		getdate_end();
		return (NULL);
	}
}


char *
strptime(expression, format, tm)
const char	*expression;
const char	*format;
struct tm	*tm;
{
	int		c;
	int		ret;

	if (getdate_setup() == 0)		/* acquires parse_fmt_lock */
		return (NULL);

	calling_function = f_strptime;
	if ((sinput = strdup(expression)) == NULL) {
		getdate_end();
		return (NULL);
	}

	input = sinput;
	while (c = (unsigned char)*input)
		*input++ = tolower(c);
	input = sinput;

	if ((ret = parse_fmt((char *)format, 0, tm)) == 0) {
		getdate_end();
		free(sinput);
		return (NULL);
	}
		/* *tm = *calc_date(tm); */

	getdate_end();
	free(sinput);
	return (char *) ((int)expression + (ret - (int)sinput));
}



/*
 * Initialize pointers to month and weekday names and to meridian markers.
 */
static char mytzname[2][4] = { "GMT", "   "};

static int
settime()
{
#ifdef _REENTRANT
	extern mutex_t _locale_lock;
#endif _REENTRANT
	register char *p;
	register int j, k;
	char *locale;

	if ((timeFormats == NULL) &&
	    ((timeFormats = malloc(sizeof (char *)*(LAST+1))) == NULL)) {
		errno = ENOMEM;
		goto error;
	}
	tzset();
	(void) strncpy(&mytzname[0][0], tzname[0], 3);
	(void) strncpy(&mytzname[1][0], tzname[1], 3);
	for (j = 0; j < 2; j++)
		for (k = 0; k < 3; k++) {
			mytzname[j][k] = tolower(mytzname[j][k]);
			if (mytzname[j][k] == ' ') {
				mytzname[j][k] = '\0';
				break;
			}
		}
	locale = _setlocale(LC_TIME, NULL);
	if (saved_locale != 0 && strcmp(locale, saved_locale) == 0)
		return (1);
	inithunk();
	if ((locale = savestr(locale)) == 0)
		goto error;
	_mutex_lock(&_locale_lock);
	_settime();
	_mutex_unlock(&_locale_lock);
	for (j = 0; j < (int)LAST; ++j) {
		timeFormats[j] = savestr(__time[j]);
		strtolower(timeFormats[j]);
	}
	saved_locale = locale;
	return (1);
error:
	saved_locale = 0;
	return (0);
}


static void
inithunk()
{
	register Hunk	*hp;

	for (hp = hunk; hp != 0; hp = hp->h_next)
		hp->h_busy = 0;
}


static char *
savestr(str)
	const char	*str;
{
	register Hunk	*hp;
	size_t		len, avail, sz;
	char		*p;

	len = strlen(str) + 1;
	if ((sz = 2 * len) < HUNKSZ)
		sz = HUNKSZ;
	avail = 0;
	if ((hp = hunk) != 0)
		avail = hp->h_bufsz - hp->h_busy;
	if (hp == 0 || avail < len) {
		if ((hp = (Hunk *)malloc(sizeof (*hp) + sz)) == 0) {
			getdate_err = 6;
			return (0);
		}
		hp->h_bufsz = sz;
		hp->h_busy = 0;
		hp->h_next = hunk;
		hunk = hp;
	}
	p = &hp->h_buf[hp->h_busy];
	(void) strcpy(p, str);
	hp->h_busy += len;
	return (p);
}


static void
strtolower(str)
	register char	*str;
{
	register unsigned char	c;

	while ((c = *str) != '\0') {
		if (c == '%' && *(str + 1) != '\0')
			++str;
		else
			*str = tolower(c);
		++str;
	}
}

/*
 * Parse the number given by the specification.
 * Allow at most length digits.
 */
static
number(length)
int	length;
{
	int	val;
	unsigned char c;

	val = 0;
	if (!isdigit((unsigned char)*input))
		return (-1);
	while (length--) {
		if (!isdigit(c = *input))
			return (val);
		val = 10*val + c - '0';
		input++;
	}
	return (val);
}

/*
 * Search for format string in timeFormats array
 */
static int
search(start, end)
int	start;
int	end;
{
	int	i, length;
	char	*s;

	for (i = start; i <= end; i++) {
		s = timeFormats[i];
		while (isspace((unsigned char)*s)) {
			s++;
		}
		length = strlen(s);
		if (strncmp(s, input, length) == 0) {
			input += length;
			return (i);
		}
	}
	return (-1);
}

static int
search_all_months(void)
{
	/* returns [0,11] for either [Jan,Dec] or [aJan,aDec] */
	int	ret;

	if ((ret = search(Jan, Dec)) >= 0)
		ret -= Jan;
	else if ((ret = search(aJan, aDec)) >= 0)
		ret -= aJan;
	return (ret);
}

static int
search_all_days(void)
{
	/* returns [0,6] for either [Sun,Sat] or [aSun,aSat] */
	int	ret;

	if ((ret = search(Sun, Sat)) >= 0)
		ret -= (int)Sun;
	else if ((ret = search(aSun, aSat)) >= 0)
		ret -= (int)aSun;
	return (ret);
}

static int
search_alt_digits()
{
	extern int	_num_alt_digits;
	extern char	**_alt_digits;
	int		i, length;
	int		digit, maxitem;

	digit = 0;
	maxitem = _num_alt_digits;
	for (i = 0; i < _num_alt_digits; i++) {
		length = strlen(_alt_digits[i]);
		if (strncmp(_alt_digits[i], input, length) == 0) {
			if (length > digit) {
				digit = length;
				maxitem = i;
			}
		}
	}
	if (maxitem == _num_alt_digits) {
		return (-1);
	} else {
		input += digit;
		return (maxitem);
	}
}

static int
search_for_era_name()
{
	int		i, length;
	struct	era_t	era;

	for (i = 0; i < _num_eras; i++) {
		extract_era_info(&era, _eras[i]);
		length = strlen(era.era_name);
		if (strncmp(era.era_name, input, length) == 0) {
			input += length;
			return (i);
		}
	}
	return (-1);
}

/*
 * Read the user specified template file by line
 * until a match occurs.
 * The DATEMSK environment variable points to the template file.
 */

static int
read_tmpl(line, t)
char	*line;
struct tm *t;
{
	FILE  *fp;
	char	*file;
	char *bp, *start;
	struct stat sb;
	int	ret = 0, c;

	if (((file = getenv("DATEMSK")) == 0) || file[0] == '\0') {
		getdate_err = 1;
		return (0);
	}
	if ((start = (char *)malloc(512)) == NULL) {
		getdate_err = 6;
		return (0);
	}
	if (access(file, R_OK) != 0 || (fp = fopen(file, "r")) == NULL) {
		getdate_err = 2;
		free(start);
		return (0);
	}
	if (stat(file, &sb) < 0) {
		getdate_err = 3;
		goto end;
	}
	if ((sb.st_mode & S_IFMT) != S_IFREG) {
		getdate_err = 4;
		goto end;
	}

	if ((sinput = malloc(strlen(line)+1)) == (char *)0) {
		getdate_err = 6;
		goto end;
	}
	input = sinput;
	(void) strcpy(sinput, line);
	while (c = (unsigned char)*input)
		*input++ = tolower(c);
	input = sinput;
	for (;;) {
		bp = start;
		if (!fgets(bp, 512, fp)) {
			if (!feof(fp)) {
				getdate_err = 5;
				ret = 0;
				break;
			}
			getdate_err = 7;
			ret = 0;
			break;
		}
		if (*(bp+strlen(bp)-1) != '\n')  { /* terminating newline? */
			getdate_err = 5;
			ret = 0;
			break;
		}
		*(bp + strlen(bp) - 1) = '\0';
#ifdef DEBUG
printf("line number \"%2d\"---> %s\n", linenum, bp);
#endif
		if (strlen(bp))  /*  anything left?  */
			if (ret = parse_fmt(bp, 0, t))
				break;
		linenum++;
		input = sinput;
	}
end:
	free(start);
	(void) fclose(fp);
	free(sinput);
	return (ret);
}

/*
 * Match lines in the template with input specification.
 */
/*VARARGS2*/
static int
parse_fmt(bp, flag, t)
	char		*bp;
	int		flag;
	struct tm	*t;
{
	int		ret;
	int		eflag	= 0;
	int		oflag	= 0;
	char		*fmt;
	char		*tmpFmt;
	unsigned char	c;
	unsigned char	d;
	struct era_t	era;

	if (!flag)
		getnow(t);
	fmt = bp;
	while ((c = *fmt++) != '\0') {
		while (isspace(d = *input))
			input++;
		if (c == '%') {
			c = *fmt++;
			if (c == 'E') {
				eflag++;
				c = *fmt++;
			} else if (c == 'O') {
				oflag++;
				c = *fmt++;
			} else if (c != 't' && c != 'c' && c != 'x' && c != 'X')
				while (isspace(d = *input))
					input++;
			switch (c) {
			case 'a':
				if (calling_function == f_strptime)
					ret = search_all_days();
				else
					ret = search(aSun, aSat) - (int)aSun;
				if (ret < 0)
					return (0);
				ret = ret + 1;
				if (t->tm_wday && t->tm_wday != ret) {
					wrong_input++;
					return (0);
				}
				t->tm_wday = ret;
				continue;
			case 'w':
				ret = (oflag) ? search_alt_digits() : number(1);
				ret++;
				if (ret < 1 || ret > 7)
					return (0);
				if (t->tm_wday && t->tm_wday != ret) {
					wrong_input++;
					return (0);
				}
				t->tm_wday = ret;
				continue;

			case 'd':
			case 'e':
				ret = (oflag) ? search_alt_digits() : number(2);
				if (ret < 1 || ret > 31)
					return (0);
				if (t->tm_mday && t->tm_mday != ret) {
					wrong_input++;
					return (0);
				}
				t->tm_mday = ret;
				continue;

			case 'A':
				if (calling_function == f_strptime)
					ret = search_all_days();
				else
					ret = search(Sun, Sat) - (int)Sun;
				if (ret < 0)
					return (0);
				ret = ret + 1;
				if (t->tm_wday && t->tm_wday != ret) {
					wrong_input++;
					return (0);
				}
				t->tm_wday = ret;
				continue;

			case 'h':
			case 'b':
				if (calling_function == f_strptime)
					ret = search_all_months();
				else
					ret = search(aJan, aDec) - (int)aJan;
				if (ret < 0)
					return (0);
				ret += 1;
				if (t->tm_mon && t->tm_mon != ret) {
					wrong_input++;
					return (0);
				}
				t->tm_mon = ret;
				continue;
			case 'B':
				if (calling_function == f_strptime)
					ret = search_all_months();
				else
					ret = search(Jan, Dec) - (int)Jan;
				if (ret < 0)
					return (0);
				ret += 1;
				if (t->tm_mon && t->tm_mon != ret) {
					wrong_input++;
					return (0);
				}
				t->tm_mon = ret;
				continue;
			case 'C':
				if (eflag) {
					/* check for name of the period */
					if ((ret = search_for_era_name())
					    == -1)
						return (0);
					if ((era_index != -1) &&
					    (era_index != ret)) {
						wrong_input++;
						return (0);
					}
					era_index = ret;
				} else {
					if ((ret = number(2)) < 0 || ret > 99)
						return (0);
					if (century != -1 && century != ret) {
						wrong_input++;
						return (0);
					}
					century = ret;
				}
				continue;
			case 'Y':
				if (eflag) {
					if (era_index == -1)
						return (0);
					extract_era_info(&era, _eras[era_index]);
					if (parse_fmt(era.era_format, 1, t))
						continue;
					return (0);
				} else {
					/*
					 * The last time UNIX can handle is
					 * 1/18/2038; for simplicity stop at
					 * 2038.
					 */
					if (((ret = number(4)) < 1970) ||
					    (ret > 2037))
						return (0);
					else
						ret = ret - 1900;
					if (t->tm_year && t->tm_year != ret) {
						wrong_input++;
						return (0);
					}
					t->tm_year = ret;
				}
				continue;

			case 'y':
				if (eflag) {
					if ((ret = number(4)) < 0)
						return (0);
					if (era_year_offset &&
					    era_year_offset != ret) {
						wrong_input++;
						return (0);
					}
					era_year_offset = ret;
				} else {
					ret = (oflag) ? search_alt_digits() :
					    number(2);
					if (ret >= 70 || ret < 38)
						ret = (ret < 38) ?
						    100 + ret : ret;
					else
						return (0);
					if (t->tm_year && t->tm_year != ret) {
						wrong_input++;
						return (0);
					}
					t->tm_year = ret;
				}
				continue;
			case 'm':
				ret = (oflag) ? search_alt_digits() : number(2);
				if (ret <= 0 || ret > 12)
					return (0);
				if (t->tm_mon && t->tm_mon != ret) {
					wrong_input++;
					return (0);
				}
				t->tm_mon = ret;
				continue;
			case 'I':
				ret = (oflag) ? search_alt_digits() : number(2);
				if (ret < 1 || ret > 12)
					return (0);
				if (t->tm_hour && t->tm_hour != ret) {
					wrong_input++;
					return (0);
				}
				t->tm_hour = ret;
				continue;
			case 'j':
				if ((ret = number(3)) < 1 || ret > 366)
					return (0);
				if (t->tm_yday && t->tm_yday != ret) {
					wrong_input++;
					return (0);
				}
				t->tm_yday = ret;
				continue;
			case 'p':
				if ((ret  = search(AM, PM)) < 0)
					return (0);
				if (meridian && meridian != ret) {
					wrong_input++;
					return (0);
				}
				meridian = ret;
				continue;
			case 'H':
				ret = (oflag) ? search_alt_digits() : number(2);
				if (ret >= 0 && ret <= 23)
					ret = ret + 1;
				else
					return (0);
				if (hour && hour != ret) {
					wrong_input++;
					return (0);
				}
				hour = ret;
				continue;
			case 'M':
				ret = (oflag) ? search_alt_digits() : number(2);
				if (ret >= 0 && ret <= 59)
					ret = ret + 1;
				else
					return (0);
				if (t->tm_min && t->tm_min != ret) {
					wrong_input++;
					return (0);
				}
				t->tm_min = ret;
				continue;
			case 'S':
				ret = (oflag) ? search_alt_digits() : number(2);
				if (ret >= 0 && ret <= 61)
					ret = ret + 1;
				else
					return (0);
				if (t->tm_sec && t->tm_sec != ret) {
					wrong_input++;
					return (0);
				}
				t->tm_sec = ret;
				continue;
			case 'U':
				ret = (oflag) ? search_alt_digits() : number(2);
				if (ret < 0 || ret > 53)
					return (0);
				if (week_number_u != -1 &&
				    week_number_u != ret) {
					wrong_input++;
					return (0);
				}
				week_number_u = ret;
				continue;
			case 'W':
				ret = (oflag) ? search_alt_digits() : number(2);
				if (ret < 0 || ret > 53)
					return (0);
				if (week_number_w != -1 &&
				    week_number_w != ret) {
					wrong_input++;
					return (0);
				}
				week_number_w = ret;
				continue;
			case 'Z':
				if (!mytzname[0][0])
					continue;
				if (strncmp(&mytzname[0][0], input,
				    strlen(&mytzname[0][0])) == 0) {
					input += strlen(&mytzname[0][0]);
					if (t->tm_isdst == 2) {
						wrong_input++;
						return (0);
					}
					t->tm_isdst = 1;
					continue;
				}
				if (strncmp(&mytzname[1][0], input,
				    strlen(&mytzname[1][0])) == 0) {
					input += strlen(&mytzname[1][0]);
					if (t->tm_isdst == 1) {
						wrong_input++;
						return (0);
					}
					t->tm_isdst = 2;
					continue;
				}
				return (0);

			case 't':
				/* ignore white space in format */
				continue;

			case 'n':
				/* ignore white space in format */
				continue;

			/* composite formats */

			case 'c':
				if ((eflag) &&
				    (timeFormats[Era_d_t_fmt] != NULL))
					tmpFmt = timeFormats[Era_d_t_fmt];
				else
					tmpFmt = timeFormats[DFL_FMT];
				if (parse_fmt(tmpFmt, 1, t))
					continue;
				return (0);

			case 'x':
				if ((eflag) && (timeFormats[Era_d_fmt] != NULL))
					tmpFmt = timeFormats[Era_d_fmt];
				else
					tmpFmt = timeFormats[Local_date];
				if (parse_fmt(tmpFmt, 1, t))
					continue;
				return (0);

			case 'X':
				if ((eflag) && (timeFormats[Era_t_fmt] != NULL))
					tmpFmt = timeFormats[Era_t_fmt];
				else
					tmpFmt = timeFormats[Local_time];
				if (parse_fmt(tmpFmt, 1, t))
					continue;
				return (0);

			case 'D':
				if (parse_fmt("%m/%d/%y", 1, t))
					continue;
				return (0);

			case 'r':
				if (parse_fmt(timeFormats[T_fmt_ampm], 1, t))
					continue;
				return (0);

			case 'R':
				if (parse_fmt("%H:%M", 1, t))
					continue;
				return (0);

			case 'T':
				if (parse_fmt("%H:%M:%S", 1, t))
					continue;
				return (0);

			case '%':
				if (*input++ != '%')
					return (0);
				continue;

			default:
				wrong_input++;
				return (0);
			}
		} else {
			while (isspace(c))
				c = *fmt++;
			if (c == '%') {
				fmt--;
				continue;
			}
			d = *input++;
			if (d != tolower(c))
				return (0);
			if (!d) {
				input--;
				break;
			}
		}
	}
	if (flag)
		return (1);
	if (calling_function == f_getdate) {
		while (isspace(d = *input))
			input++;
		if (*input)
			return (0);
		if (verify_getdate(t))
			return (1);
		else
			return (0);
	} else		/* calling_function == f_strptime */
		if (verify_strptime(t))
			return ((int)input);
		else
			return (0);
}

static void
getnow(t)	/*  get current date */
struct tm *t;
{
	time_t now;

	now = time((time_t *)NULL);
	ct = localtime_r(&now, ct);
	ct->tm_yday += 1;
	t->tm_year = t->tm_mon = t->tm_mday = t->tm_wday = t->tm_hour = 0;
	t->tm_min = t->tm_sec = t->tm_isdst = t->tm_yday = hour = meridian = 0;
	century = -1;
}

/*
 * Check validity of input for strptime
 */
static int
verify_strptime(t)
struct tm *t;
{
	int leap;
	int temp = t->tm_year;

	leap = (days(temp) == 366);
	if (week_number_u != -1 || week_number_w != -1)
		if (week_number_to_yday(t, t->tm_year) == -1)
			return (0);
	if (t->tm_yday)
		if (yday(t, leap) == -1)
			return (0);


	if (t->tm_hour) {
		switch (meridian) {
			case PM:
				t->tm_hour %= 12;
				t->tm_hour += 12;
				break;
			case AM:
				t->tm_hour %= 12;
				break;
		}
	}
	if (hour)
		t->tm_hour = hour - 1;
	if (t->tm_min)
		t->tm_min--;
	if (t->tm_sec)
		t->tm_sec--;
	if (t->tm_wday)
		t->tm_wday--;
	if (t->tm_mon)
		t->tm_mon--;
	if (century != -1)
		t->tm_year += 100 * (century - 1);

	return (1);
}

/*
 * Check validity of input for getdate
 */
static int
verify_getdate(t)
struct tm *t;
{
	int min = 0;
	int sec = 0;
	int hr = 0;
	int leap;

	if (t->tm_year)
		year(t->tm_year);
	leap = (days(ct->tm_year) == 366);
	if (week_number_u != -1 || week_number_w != -1)
		if (week_number_to_yday(t, ct->tm_year) == -1) {
			wrong_input++;
			return (0);
		}
	if (t->tm_yday)
		if (yday(t, leap) == -1) {
			wrong_input++;
			return (0);
		} else
			t->tm_yday = 0;
	if (t->tm_mon)
		MON(t->tm_mon - 1);
	if (t->tm_mday)
		Day(t->tm_mday);
	if (t->tm_wday)
		DOW(t->tm_wday - 1);
	if (((t->tm_mday)&&((t->tm_mday != ct->tm_mday) ||
	    (t->tm_mday > __mon_lengths[leap][ct->tm_mon]))) ||
	    ((t->tm_wday)&&((t->tm_wday-1) != ct->tm_wday)) ||
	    ((t->tm_hour&&hour)||(t->tm_hour&&!meridian) ||
	    (!t->tm_hour&&meridian)||(hour&&meridian))) {
		wrong_input++;
		return (0);
	}
	if (t->tm_hour) {
		switch (meridian) {
			case PM:
				t->tm_hour %= 12;
				t->tm_hour += 12;
				break;
			case AM:
				t->tm_hour %= 12;
				if (t->tm_hour != 0)
					hr++;
				break;
			default:
				return (0);
		}
	}
	if (hour)
		t->tm_hour = hour - 1;
	if (t->tm_min) {
		min++;
		t->tm_min -= 1;
	}
	if (t->tm_sec) {
		sec++;
		t->tm_sec -= 1;
	}
	if ((! t->tm_year && ! t->tm_mon && ! t->tm_mday && ! t->tm_wday) &&
	    ((t->tm_hour < ct->tm_hour) || ((t->tm_hour == ct->tm_hour) &&
	    (t->tm_min < ct->tm_min)) || ((t->tm_hour == ct->tm_hour) &&
	    (t->tm_min == ct->tm_min) && (t->tm_sec < ct->tm_sec))))
		t->tm_hour += 24;
	if (t->tm_hour || hour || hr || min || sec) {
		ct->tm_hour = t->tm_hour;
		ct->tm_min = t->tm_min;
		ct->tm_sec = t->tm_sec;
	}
	if (t->tm_isdst)
		ct->tm_isdst = t->tm_isdst - 1;
	else
		ct->tm_isdst = 0;
	return (1);
}


static void
Day(day)
int day;
{
	if (day < ct->tm_mday)
		if (++ct->tm_mon == 12)  ++ct->tm_year;
	ct->tm_mday = day;
	DMY();
}


static void
DMY()
{
	int doy;
	if (days(ct->tm_year) == 366)
		doy = __lyday_to_month[ct->tm_mon];
	else
		doy = __yday_to_month[ct->tm_mon];
	ct->tm_yday = doy + ct->tm_mday;
	ct->tm_wday = (jan1(ct->tm_year) + ct->tm_yday - 1) % 7;
}


static int
days(y)
int	y;
{
	y += 1900;
	return (y%4 == 0 && y%100 != 0 || y%400 == 0 ? 366 : 365);
}


/*
 *	return day of the week
 *	of jan 1 of given year
 */

static int
jan1(yr)
{
	register y, d;

/*
 *	normal gregorian calendar
 *	one extra day per four years
 */

	y = yr + 1900;
	d = 4+y+(y+3)/4;

/*
 *	julian calendar
 *	regular gregorian
 *	less three days per 400
 */

	if (y > 1800) {
		d -= (y-1701)/100;
		d += (y-1601)/400;
	}

/*
 *	great calendar changeover instant
 */

	if (y > 1752)
		d += 3;

	return (d%7);
}

static void
year(yr)
int	yr;
{
	ct->tm_mon = 0;
	ct->tm_mday = 1;
	ct->tm_year = yr;
	DMY();
}

static void
MON(month)
int month;
{
	ct->tm_mday = 1;
	Month(month);
}

static void
Month(month)
int month;
{
	if (month < ct->tm_mon)  ct->tm_year++;
	ct->tm_mon = month;
	DMY();
}

static void
DOW(dow)
int	dow;
{
	adddays((dow+7-ct->tm_wday)%7);
}

static void
adddays(n)
int	n;
{
	DOY(ct->tm_yday+n);
}

static void
DOY(doy)
int	doy;
{
	int i, leap;

	if (doy > days(ct->tm_year)) {
		doy -= days(ct->tm_year);
		ct->tm_year++;
	}
	ct->tm_yday = doy;

	leap = (days(ct->tm_year) == 366);
	for (i = 0; doy > __mon_lengths[leap][i]; i++)
		doy -= __mon_lengths[leap][i];
	ct->tm_mday = doy;
	ct->tm_mon = i;
	ct->tm_wday = (jan1(ct->tm_year)+ct->tm_yday-1) % 7;
}

static int
yday(struct tm *t, int leap)
{
	int	month;
	int	day_of_month;
	int	*days_to_months;

	days_to_months = (int *) (leap ? __lyday_to_month : __yday_to_month);
	t->tm_yday--;
	if (!t->tm_year) {
		t->tm_year = ct->tm_year;
		year(t->tm_year);
	}

	for (month = 1; month < 12; month++)
		if (t->tm_yday <= days_to_months[month])
			break;

	if (t->tm_mon && t->tm_mon != month - 1)
		return (-1);

	t->tm_mon = month;
	day_of_month = t->tm_yday - days_to_months[month - 1] + 1;
	if (t->tm_mday && t->tm_mday != day_of_month)
		return (-1);

	t->tm_mday = day_of_month;
	return (0);
}

static int
week_number_to_yday(struct tm *t, int year)
{
	int	yday;

	if (week_number_u != -1) {
		yday = 7 * week_number_u + t->tm_wday - jan1(year);
		if (t->tm_yday && t->tm_yday != yday)
			return (-1);
		t->tm_yday = yday;
	}
	if (week_number_w != -1) {
		yday = (8 - jan1(year) % 7) + 7 * (week_number_w - 1) +
		    t->tm_wday - 1;
		if (t->tm_wday == 1)
			yday += 7;

		if (t->tm_yday && t->tm_yday != yday)
			return (-1);
		t->tm_yday = yday;
	}
	return (0);
}

/*
 * return time from time structure
 */
static struct  tm *
calc_date(t)
struct tm *t;
{
	long	tv;
	struct  tm nct;

	nct = *ct;
	tv = mktime(ct);
	if (!t->tm_isdst && ct->tm_isdst != nct.tm_isdst) {
		nct.tm_isdst = ct->tm_isdst;
		tv = mktime(&nct);
	}
	ct = localtime_r(&tv, ct);
	return (ct);
}
