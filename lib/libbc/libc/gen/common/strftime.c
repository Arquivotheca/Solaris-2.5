/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)strftime.c	1.3	92/07/14 SMI"	/* SVr4.0 1.13	*/

/* the following structures and #defines are from SVr4, as is this code */
#include <time.h>

#define	NULL	0
#define _ST_FSTYPSZ 16          /* array size for file system type name */
#define LC_TIME         2
#define LC_ALL          6
#define	O_RDONLY	0
#define LC_NAMELEN      255             /* maximum part name length (inc. \0) */
#define SZ_CTYPE        (257 + 257)     /* is* and to{upp,low}er tables */
#define SZ_CODESET      7               /* bytes for codeset information */
#define SZ_NUMERIC      2               /* bytes for numeric editing */
#define SZ_TOTAL        (SZ_CTYPE + SZ_CODESET)
#define NM_UNITS        0               /* index of decimal point character */
#define NM_THOUS        1               /* index of thousand's sep. character */

typedef struct  timestruc {
        long          tv_sec;         /* seconds */
        long            tv_nsec;        /* and nanoseconds */
} timestruc_t;

struct  stat {
        unsigned long   st_dev;
        long    st_pad1[3];     /* reserve for dev expansion, */
                                /* sysid definition */
        unsigned long   st_ino;
        unsigned short  st_mode;
        unsigned long st_nlink;
        long   st_uid;
        long   st_gid;
        unsigned long   st_rdev;
        long    st_pad2[2];
        long   st_size;
        long    st_pad3;        /* reserve pad for future off_t expansion */
        timestruc_t st_atime;
        timestruc_t st_mtime;
        timestruc_t st_ctime;
        long    st_blksize;
        long    st_blocks;
        char    st_fstype[_ST_FSTYPSZ];
        long    st_pad4[8];     /* expansion area */
};        


extern void  _settime();
enum {
        aJan, aFeb, aMar, aApr, aMay, aJun, aJul, aAug, aSep, aOct, aNov, aDec,
        Jan, Feb, Mar, Apr, May, Jun, Jul, Aug, Sep, Oct, Nov, Dec,
        aSun, aMon, aTue, aWed, aThu, aFri, aSat,
        Sun, Mon, Tue, Wed, Thu, Fri, Sat,
        Local_time, Local_date, DFL_FMT,
        AM, PM, DATE_FMT,
        LAST
};

extern char _cur_locale[LC_ALL][LC_NAMELEN];
extern unsigned char _ctype[SZ_TOTAL];
extern unsigned char _numeric[SZ_NUMERIC];

#if defined(__STDC__)
extern const char * __time[];
char *_nativeloc(int);          /* trunc. name for category's "" locale */
char *_fullocale(const char *, const char *);   /* complete path */
int _set_tab(const char *, int);         /* fill _ctype[]  or _numeric[] */
#else
extern char * __time[];
char *_nativeloc();     /* trunc. name for category's "" locale */
char *_fullocale();     /* complete path */
int _set_tab();         /* fill _ctype[]  or _numeric[] */
#endif


extern  char *tzname[];
static char *gettz();
static char *itoa();


size_t
strftime(s, maxsize, format, tm)
char *s;
size_t maxsize;
#ifdef __STDC__
const char *format;
const struct tm *tm;
{
	register const char	*p;
#else
char *format;
struct tm *tm;
{
	register char	*p;
#endif
	register int	i, temp;
	register char	c;
	size_t		size = 0;
	char		nstr[5];
	size_t		n;
	static char	dflcase[] = "%?";
	_settime();

/* envoke mktime, for its side effects */
	{
		struct tm tmp;

		memcpy(&tmp, tm, sizeof (struct tm));
		mktime(&tmp);
	}


	/* Set format string, if not already set */
	if (format == NULL)
		format = __time[DFL_FMT];

	/* Build date string by parsing format string */
	while ((c = *format++) != '\0') {
		if (c != '%') {
			if (++size >= maxsize)
				return (0);
			*s++ = c;
			continue;
		}
		switch (*format++) {
		case '%':	/* Percent sign */
			p = "%";
			break;
		case 'a':	/* Abbreviated weekday name */
			p = __time[aSun + tm->tm_wday];
			break;
		case 'A':	/* Weekday name */
			p = __time[Sun + tm->tm_wday];
			break;
		case 'b':	/* Abbreviated month name */
		case 'h':
			p = __time[aJan + tm->tm_mon];
			break;
		case 'B':	/* Month name */
			p = __time[Jan + tm->tm_mon];
			break;
		case 'c':	/* Localized date & time format */
			p = __time[DFL_FMT];
			goto recur;
		case 'C':	/* Localized date & time format */
			p = __time[DATE_FMT];
			goto recur;
		case 'd':	/* Day number */
			p = itoa(tm->tm_mday, nstr, 2);
			break;
		case 'D':
			p = "%m/%d/%y";
			goto recur;
		case 'e':
			(void) itoa(tm->tm_mday, nstr, 2);
			if (tm->tm_mday < 10)
				nstr[0] = ' ';
#ifdef __STDC__
			p = (const char *)nstr;
#else
			p = (char *)nstr;
#endif
			break;
		case 'k':	/* for bcp */
		case 'H':	/* Hour (24 hour version) */
			p = itoa(tm->tm_hour, nstr, 2);
			break;
		case 'l':	/* for bcp */
		case 'I':	/* Hour (12 hour version) */
			if ((i = tm->tm_hour % 12) == 0)
				i = 12;
			p = itoa(i, nstr, 2);
			break;
		case 'j':	/* Julian date */
			p = itoa(tm->tm_yday + 1, nstr, 3);
			break;
		case 'm':	/* Month number */
			p = itoa(tm->tm_mon + 1, nstr, 2);
			break;
		case 'M':	/* Minute */
			p = itoa(tm->tm_min, nstr, 2);
			break;
		case 'n':	/* Newline */
			p = "\n";
			break;
		case 'p':	/* AM or PM */
			if (tm->tm_hour >= 12)
				p = __time[PM];
			else
				p = __time[AM];
			break;
		case 'r':
			if (tm->tm_hour >= 12)
				p = "%I:%M:%S PM";
			else
				p = "%I:%M:%S AM";
			goto recur;
		case 'R':
			p = "%H:%M";
			goto recur;
		case 'S':	/* Seconds */
			p = itoa(tm->tm_sec, nstr, 2);
			break;
		case 't':	/* Tab */
			p = "\t";
			break;
		case 'T':
			p = "%H:%M:%S";
			goto recur;
		case 'U':	/*
				 * Week number of year, taking Sunday as
				 * the first day of the week
				 */
			p = itoa(1+(tm->tm_yday - tm->tm_wday + 7)/7, nstr, 2);
			break;
		case 'w':	/* Weekday number */
			p = itoa(tm->tm_wday, nstr, 1);
			break;
		case 'W':	/*
				 * Week number of year, taking Monday as
				 * first day of week
				 */
			if ((i = 8 - tm->tm_wday) == 8)
				i = 1;
			p = itoa(1+(tm->tm_yday + i)/7, nstr, 2);
			break;
		case 'x':	/* Localized date format */
			p = __time[Local_date];
			goto recur;
		case 'X':	/* Localized time format */
			p = __time[Local_time];
			goto recur;
		case 'y':	/* Year in the form yy */
			p = itoa(tm->tm_year, nstr, 2);
			break;
		case 'Y':	/* Year in the form ccyy */
			p = itoa(1900 + tm->tm_year, nstr, 4);
			break;
		case 'Z':	/* Timezone */
			p = tm->tm_zone;
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
			if ((n = strftime(s, maxsize-size, p, tm)) == 0)
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
gettz(tm)
struct tm *tm;
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
