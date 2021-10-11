/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)strftime.c	1.28	95/04/04 SMI"	/* SVr4.0 1.8	*/

#include	"synonyms.h"
#include	"shlib.h"
#include	<fcntl.h>
#include	<time.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<locale.h>
#include	<limits.h>
#include	<stdio.h>
#include	<string.h>
#include	<stddef.h>
#include	<stdlib.h>
#include	<ctype.h>
#include	<unistd.h>
#include	<synch.h>
#include	<mtlib.h>
#include	"_locale.h"
#include	"_strftime.h"
#define	ALTDIGITSTRING	"NUM_ALT_DIGITS"
#define	ERASTRING	"NUM_ERA"

extern char *_loc_filename[]; /* defined in setlocale.c */
extern int __xpg4; /* defined in _xpg4.c; 0 if not xpg4-compiled program */
extern  char *tzname[];
void _settime();

static char *itoa(int, char *, int);
static char *convert_number(int, int, int, int);
static char *gettz(const struct tm *);
static int compare_simple_date_to_tm(struct simple_date *, const struct tm *);
static int get_era_year(struct era_t *, const struct tm *);
static int get_era_by_date(struct era_t *, const struct tm *);
void extract_era_info(struct era_t *, char *);
static void extract_era_date(struct simple_date *, const char *);
static void xpg4_dfl_fmt_fix(const char *);

int _num_alt_digits = 0;
int _num_eras = 0;
char **_alt_digits = NULL;
char **_eras = NULL;
static size_t convert_fmt(char *, size_t, const char *, const struct tm *,
    const char **);

const char *__time[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
	"January", "February", "March", "April",
	"May", "June", "July", "August",
	"September", "October", "November", "December",
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
	"Sunday", "Monday", "Tuesday", "Wednesday",
	"Thursday", "Friday", "Saturday",
	"%H:%M:%S", "%m/%d/%y", "%a %b %d %H:%M:%S %Y",
	"AM", "PM", "%a %b %e %T %Z %Y",
	"%I:%M:%S %p", "%x", "%X", "%a %b %e %T %Z %Y",
	NULL
};
const char *xpg4_dfl_fmt = "%a %b %e %H:%M:%S %Y";

size_t
strftime(s, maxsize, format, tm)
char *s;
size_t maxsize;
const char *format;
const struct tm *tm;
{
#ifdef	_REENTRANT
	extern mutex_t _locale_lock;
#endif	_REENTRANT
	size_t	retval;


/* envoke mktime, for its side effects */
	{
		struct tm tmp;
		memcpy(&tmp, tm, sizeof (struct tm));
		mktime(&tmp);
	}
	_mutex_lock(&_locale_lock);
	_settime();
	_mutex_unlock(&_locale_lock);
	retval = convert_fmt(s, maxsize, format, tm, __time);
	return (retval);
}

static size_t
convert_fmt(s, maxsize, format, tm, timeFormats)
char *s;
size_t maxsize;
const char *format;
const struct tm *tm;
const char **timeFormats;
{
	register const char	*p;
	register int	i;
	register char	c;
	int		oflag;
	int		eflag;
	size_t		size = 0;
	size_t		n;
	static char	dflcase[] = "%?";
	era_t		era;


	/* Set format string, if not already set */
	if (format == NULL)
		format = timeFormats[DFL_FMT];

	/* Build date string by parsing format string */
	while ((c = *format++) != '\0') {
		if (c != '%') {
			if (++size >= maxsize)
				return (0);
			*s++ = c;
			continue;
		}
		oflag = 0;
		eflag = 0;
		if (*format == 'E') {
			eflag++;
			format++;
		} else if (*format == 'O') {
			oflag++;
			format++;
		}
		switch (*format++) {
		case '%':	/* Percent sign */
			p = "%";
			break;
		case 'a':	/* Abbreviated weekday name */
			p = timeFormats[aSun + tm->tm_wday];
			break;
		case 'A':	/* Weekday name */
			p = timeFormats[Sun + tm->tm_wday];
			break;
		case 'b':	/* Abbreviated month name */
		case 'h':
			p = timeFormats[aJan + tm->tm_mon];
			break;
		case 'B':	/* Month name */
			p = timeFormats[Jan + tm->tm_mon];
			break;
		case 'c':	/* Localized date & time format */
			if ((eflag) && (timeFormats[Era_d_t_fmt] != NULL))
				p = timeFormats[Era_d_t_fmt];
			else
				p = timeFormats[DFL_FMT];
			goto recur;
		case 'C':
			if ((eflag) && ((get_era_by_date(&era, tm)) != -1))
				p = era.era_name;
			else if (__xpg4 == 0)
				/* Localized date & time format */
				p = timeFormats[DATE_FMT];
			else
				/* Century number */
				p = convert_number(((tm->tm_year+1900)/100),
				    NO_ALT_DIGITS, 2, NO_PRECEDE_BY_SPACE);
			goto recur;
			break;
		case 'd':	/* Day number */
			p = convert_number(tm->tm_mday, oflag, 2,
			    NO_PRECEDE_BY_SPACE);
			break;
		case 'D':
			p = "%m/%d/%y";
			goto recur;
		case 'e':
			p = convert_number(tm->tm_mday, oflag, 2,
			    PRECEDE_BY_SPACE);
			break;
		case 'H':	/* Hour (24 hour version) */
			p = convert_number(tm->tm_hour, oflag, 2,
			    NO_PRECEDE_BY_SPACE);
			break;
		case 'I':	/* Hour (12 hour version) */
			if ((i = tm->tm_hour % 12) == 0)
				i = 12;
			p = convert_number(i, oflag, 2, NO_PRECEDE_BY_SPACE);
			break;
		case 'j':	/* Julian date */
			p = convert_number(tm->tm_yday + 1, NO_ALT_DIGITS, 3,
			    NO_PRECEDE_BY_SPACE);
			break;
		case 'k':
			p = convert_number(tm->tm_hour, oflag, 2,
			    PRECEDE_BY_SPACE);
			break;
		case 'l':
			if ((i = tm->tm_hour % 12) == 0)
				i = 12;
			p = convert_number(i, oflag, 2, PRECEDE_BY_SPACE);
			break;
		case 'm':	/* Month number */
			p = convert_number(tm->tm_mon + 1, oflag, 2,
			    NO_PRECEDE_BY_SPACE);
			break;
		case 'M':	/* Minute */
			p = convert_number(tm->tm_min, oflag, 2,
			    NO_PRECEDE_BY_SPACE);
			break;
		case 'n':	/* Newline */
			p = "\n";
			break;
		case 'p':	/* AM or PM */
			if (tm->tm_hour >= 12)
				p = timeFormats[PM];
			else
				p = timeFormats[AM];
			break;
		case 'r':
			p = __time[T_fmt_ampm];
			goto recur;
		case 'R':
			p = "%H:%M";
			goto recur;
		case 'S':	/* Seconds */
			p = convert_number(tm->tm_sec, oflag, 2,
			    NO_PRECEDE_BY_SPACE);
			break;
		case 't':	/* Tab */
			p = "\t";
			break;
		case 'T':
			p = "%H:%M:%S";
			goto recur;
		case 'u':
			p = convert_number(tm->tm_wday + 1, oflag, 1,
			    NO_PRECEDE_BY_SPACE);
			break;
		case 'U':
				/*
				 * Week number of year, taking Sunday as
				 * the first day of the week
				 */
			p = convert_number((tm->tm_yday - tm->tm_wday + 7)/7,
			    oflag, 2, NO_PRECEDE_BY_SPACE);
			break;
		case 'V':
				/*
				 * Week number of year, taking Monday as
				 * the first day of the week
				 */
			p = convert_number((tm->tm_yday - tm->tm_wday + 6)/7,
			    oflag, 2, NO_PRECEDE_BY_SPACE);
			break;
		case 'w':	/* Weekday number */
			p = convert_number(tm->tm_wday, oflag, 1,
			    NO_PRECEDE_BY_SPACE);
			break;
		case 'W':
				/*
				 * Week number of year, taking Monday as
				 * first day of week
				 */
			if ((i = 8 - tm->tm_wday) == 8)
				i = 1;
			p = convert_number((tm->tm_yday + i)/7, oflag, 2,
			    NO_PRECEDE_BY_SPACE);
			break;
		case 'x':	/* Localized date format */
			if ((eflag) && (timeFormats[Era_d_fmt] != NULL))
				p = timeFormats[Era_d_fmt];
			else
				p = timeFormats[Local_date];
			goto recur;
		case 'X':	/* Localized time format */
			if ((eflag) && (timeFormats[Era_t_fmt] != NULL))
				p = timeFormats[Era_t_fmt];
			else
				p = timeFormats[Local_time];
			goto recur;
		case 'y':	/* Offset in current era or... */
			if ((eflag || oflag) &&
			    ((get_era_by_date(&era, tm)) != -1)) {
				if (oflag)
					p = convert_number(
					    get_era_year(&era, tm),
					    oflag, 4,
					    NO_PRECEDE_BY_SPACE);
				else
					p = convert_number(
					    get_era_year(&era, tm),
					    NO_ALT_DIGITS, 4,
					    NO_PRECEDE_BY_SPACE);
				while (*p == '0')
					p++;
			} else	/* Year in the form yy */
				p = convert_number(tm->tm_year, oflag, 2,
				    NO_PRECEDE_BY_SPACE);
			break;
		case 'Y':	/* Era alternative year or... */
			if ((eflag) && ((get_era_by_date(&era, tm)) != -1)) {
				p = era.era_format;
				goto recur;
			} else	/* Year in the form ccyy */
				p = convert_number(1900 + tm->tm_year,
				    NO_ALT_DIGITS, 4, NO_PRECEDE_BY_SPACE);
			break;
		case 'Z':	/* Timezone */
			p = gettz(tm);
			break;
		default:
			dflcase[1] = *(format - 1);
#ifdef __STDC__
			p = (const char *)dflcase;
#else
			p = (char *)dflcase;
#endif
			break;
		recur:;
			if ((n = convert_fmt(s, maxsize-size, p, tm,
			    timeFormats)) == 0)
				return (0);
			s += n;
			size += n;
			continue;
		}
		n = strlen(p);
		if ((size += n) >= maxsize)
			return (0);
		(void) strcpy(s, p);
		s += n;
	}
	*s = '\0';
	return (size);
}

static char *
itoa(i, ptr, dig)
register int i;
register char *ptr;
register int dig;
{
	ptr += dig;
	*ptr = '\0';
	while (--dig >= 0) {
		*(--ptr) = i % 10 + '0';
		i /= 10;
	}
	return (ptr);
}

static char *
convert_number(number, alternate_flag, num_digits, precede_by_space)
int number;
int alternate_flag;
int num_digits;
int precede_by_space;
{
	static char	nstr[5];
	if ((alternate_flag == 0) || (number > _num_alt_digits) ||
	    (number < 0)) {
		itoa(number, nstr, num_digits);
		if ((precede_by_space == PRECEDE_BY_SPACE) && (number < 10))
			nstr[0] = ' ';
		return (nstr);
	} else
		return (_alt_digits[number]);
}

static char saved_locale[LC_NAMELEN] = "C";

void
_settime()
{
	/* NOTE: acquire _locale_lock before calling _settime() */
	register char *p;
	register int  j;
	char *locale;
	char *my_time[LAST];
	static char *ostr = (char *)0;
	char *str;
	char *alt_digits_ptr;
	char *era_ptr;
	int  fd;
	struct stat buf;

	locale = _cur_locale[LC_TIME];

	if (strcmp(locale, saved_locale) == 0) {
		xpg4_dfl_fmt_fix(locale);
		return;
	}

	if ((fd = open(_fullocale(locale,
	    _loc_filename[LC_TIME]), O_RDONLY)) == -1)
		goto err1;

	if ((fstat(fd, &buf)) != 0 || (str = malloc(buf.st_size + 2)) == NULL)
		goto err2;

	if ((read(fd, str, buf.st_size)) != buf.st_size)
		goto err3;

	/* Set last character of str to '\0' */
	p = &str[buf.st_size];
	p[0] = '\n';
	p[1] = '\0';

	alt_digits_ptr = strstr(str, ALTDIGITSTRING);
	era_ptr = strstr(str, ERASTRING);

	/* p will "walk thru" str */
	p = str;

	j = -1;
	while (*p != '\0') {
		/*
		 * "Look for a newline, i.e. end of sub-string
		 * and  change it to a '\0'. If LAST pointers
		 * have been set in my_time, but the newline hasn't been seen
		 * yet, keep going thru the string leaving my_time alone.
		 */
		if (++j < LAST)
			my_time[j] = p;
		p = strchr(p, '\n');
		*p++ = '\0';
	}
	if (j == DATE_FMT + 1) {
		/*
		 * In case these optional era formats are not
		 * included in the LC_TIME file, initialize them
		 * to the non-era formats.
		 */
		my_time[T_fmt_ampm] = DEFAULT_T_FMT_AMPM;
		my_time[Era_d_fmt] = my_time[Local_date];
		my_time[Era_t_fmt] = my_time[Local_time];
		my_time[Era_d_t_fmt] = my_time[DFL_FMT];
	}
	if (j < DATE_FMT + 1)
		/* all formats up to and including DATE_FMT are required */
		goto err3;

	if (alt_digits_ptr != NULL) {
		alt_digits_ptr = strchr(alt_digits_ptr, '=');
		_num_alt_digits = atoi(alt_digits_ptr + 1);
		_alt_digits = calloc(_num_alt_digits, sizeof (char *));
		for (j = 0; j < _num_alt_digits; j++) {
			alt_digits_ptr = strchr(alt_digits_ptr, '\0');
			_alt_digits[j] = ++alt_digits_ptr;
		}
	} else {
		_alt_digits = NULL;
		_num_alt_digits = 0;
	}

	if (era_ptr != NULL) {
		era_ptr = strchr(era_ptr, '=');
		_num_eras = atoi(era_ptr + 1);
		_eras = calloc(_num_eras, sizeof (char *));
		era_ptr = strchr(era_ptr, '\0');
		for (j = 0; j < _num_eras; j++) {
			_eras[j] = ++era_ptr;
			era_ptr = strchr(era_ptr, '\0');
			p = strrchr(_eras[j], ':');
			*p = '\0';
		}
	} else {
		_eras = NULL;
		_num_eras = 0;
	}

	xpg4_dfl_fmt_fix(locale);
	memcpy(__time, my_time, sizeof (my_time));
	strcpy(saved_locale, locale);
	if (ostr != 0)	 /* free the previoulsy allocated local array */
		free(ostr);
	ostr = str;
	(void) close(fd);

	return;

err3:	free(str);
err2:	(void)close(fd);
err1:	(void)strcpy(_cur_locale[LC_TIME], saved_locale);
}

static void
xpg4_dfl_fmt_fix(const char *locale)
{
	if (__xpg4  != 0)
		if ((strcmp(locale, "C") == 0) ||
		    (strcmp(locale, "POSIX") == 0) ||
		    (strcmp(locale, "en_US") == 0))
			__time[DFL_FMT] = xpg4_dfl_fmt;
}

static void
extract_era_date(date, era_str)
struct simple_date *date;
const char *era_str;
{
	char *p = (char *)era_str;

	if (p[1] == '*') {
		if (p[0] == '-') {	/* dawn of time */
			date->day = 1;
			date->month = 0;
			date->year = INT_MIN;
		} else {		/* end of time */
			date->day = 31;
			date->month = 11;
			date->year = INT_MAX;
		}
		return;
	}

	date->year = atoi(p) - 1900;
	if (strchr(p, ':') < strchr(p, '/')) {	/* date is year only */
		date->month = 0;
		date->day = 1;
		return;
	}
	p = strchr(p, '/') + 1;
	date->month = atoi(p) - 1;
	p = strchr(p, '/') + 1;
	date->day = atoi(p);
}


#define	ADVANCE_ERASTRING(p) (p = strchr(p, ':') + 1)

void
extract_era_info(era, era_str)
struct era_t *era;
char *era_str;
{
	era->direction = (era_str[0] == '-') ? ERA_MINUS : ERA_PLUS;
	ADVANCE_ERASTRING(era_str);
	era->year_offset = atoi(era_str);
	ADVANCE_ERASTRING(era_str);
	if (era->direction == ERA_PLUS) {
		extract_era_date(&(era->era_start), era_str);
		ADVANCE_ERASTRING(era_str);
		extract_era_date(&(era->era_end), era_str);
	} else {
		extract_era_date(&(era->era_end), era_str);
		ADVANCE_ERASTRING(era_str);
		extract_era_date(&(era->era_start), era_str);
	}
	ADVANCE_ERASTRING(era_str);
	era->era_name = era_str;
	era->era_format = era_str + strlen(era->era_name) + 1;
}

static int
compare_simple_date_to_tm(date, tm)
struct simple_date *date;
const struct tm *tm;
{
	if (date->year < tm->tm_year)
		return (-1);
	else if (date->year > tm->tm_year)
		return (1);
	else if (date->month < tm->tm_mon)
		return (-1);
	else if (date->month > tm->tm_mon)
		return (1);
	else if (date->day < tm->tm_mday)
		return (-1);
	else if (date->day > tm->tm_mday)
		return (1);
	else
		return (0);
}

static int
get_era_year(era, tm)
struct era_t *era;
const struct tm *tm;
{
	if (era->direction == ERA_PLUS)
		return (tm->tm_year - era->era_start.year + era->year_offset);
	else
		return (era->era_start.year - tm->tm_year + era->year_offset);
}

static int
get_era_by_date(era, tm)
struct era_t *era;
const struct tm *tm;
{
	int j = 0;
	char *p;

	if (_eras == NULL)
		return (-1);
	while (j < _num_eras) {
		extract_era_info(era, _eras[j]);
		if ((compare_simple_date_to_tm(&(era->era_start), tm) <= 0) &&
		    (compare_simple_date_to_tm(&(era->era_end), tm) >= 0))
			return (0);
		j++;
	}
	return (-1);
}

#define	MAXTZNAME	3

static char *
gettz(tm)
const struct tm *tm;
{
	register char	*p;

	if (tm->tm_isdst)
		p = tzname[1];
	else
		p = tzname[0];
	if (strcmp(p, "   ") == 0)
		return ("");
	else
		return (p);
}
