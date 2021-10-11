/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/
/* (The above copyright notice came from strftime.c) */
/*
 * Copyright (c) 1991, Sun Microsystems, Inc.
 */
#pragma ident	"@(#)_strftime.h	1.2	94/02/04 SMI"

/*
 * _strftime.h ... semi-private definition of the stuff used by
 * strftime.c, to be shared with nl_langinfo.c.
 */
extern void  _settime();
enum {
	aJan, aFeb, aMar, aApr, aMay, aJun, aJul, aAug, aSep, aOct, aNov, aDec,
	Jan, Feb, Mar, Apr, May, Jun, Jul, Aug, Sep, Oct, Nov, Dec,
	aSun, aMon, aTue, aWed, aThu, aFri, aSat,
	Sun, Mon, Tue, Wed, Thu, Fri, Sat,
	Local_time, Local_date, DFL_FMT,
	AM, PM, DATE_FMT,
	T_fmt_ampm, Era_d_fmt, Era_t_fmt, Era_d_t_fmt,
	LAST
};
#ifdef __STDC__
extern const char * __time[];
#else
extern char * __time[];
#endif

typedef struct simple_date {
	int	day;
	int	month;
	int	year;
} simple_date;

typedef struct era_t {
	int			direction;
	int			year_offset;
	struct simple_date	era_start;
	struct simple_date	era_end;
	char			*era_name;
	char			*era_format;
} era_t;
#define	ERA_MINUS	0
#define	ERA_PLUS	1
#define	NO_ALT_DIGITS		0
#define	NO_PRECEDE_BY_SPACE	0
#define	PRECEDE_BY_SPACE	1
#define	DEFAULT_T_FMT_AMPM	"%I:%M:%S %p"
