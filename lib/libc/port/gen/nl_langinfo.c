/*
 * Copyright (c) 1991, Sun Microsystems, Inc.
 */
#pragma ident	"@(#)nl_langinfo.c	1.27	95/09/01 SMI"

#pragma weak nl_langinfo = _nl_langinfo

#include "synonyms.h"
#include <stdlib.h>
#include <limits.h>
#include <nl_types.h>
#include <langinfo.h>
#include <locale.h>
#include <time.h>
#include <string.h>
#include "_strftime.h"
#include "_locale.h"
#include <thread.h>
#include <synch.h>
#include <mtlib.h>
#include <fcntl.h>

#ifdef _REENTRANT
extern mutex_t _locale_lock;
#endif
extern struct lconv *_localeconv_nolock();

extern int	_num_alt_digits;
extern int	_num_eras;
extern char	**_alt_digits;
extern char	**_eras;
static char	*_getcodeset();

#define	MAXANSLENGTH 128
#define	RETURN(x) { _mutex_unlock(&_locale_lock); return (x); }

struct databuf {
	char	locale_name[CHARCLASS_NAME_MAX];
	char	*data;
	int	datalen;
};

static int databuf_check_cache(struct databuf *);
static void databuf_check_size(struct databuf *, int);

char *
nl_langinfo(item)
	nl_item item;
{
	int		i, totalbytes;
	static char	statlanginfobuf[MAXANSLENGTH]; /* Output buffer. */
	static thread_key_t nl_key = 0;
	struct lconv	*lconv; /* For handling CRNCYSTR. */
	char *langinfobuf = (_thr_main() ? statlanginfobuf :
			(char *)_tsdalloc(&nl_key, MAXANSLENGTH));
	static struct databuf *era_data = NULL;
	static struct databuf *digit_data = NULL;

	_mutex_lock(&_locale_lock);
	switch (item) {
	case CODESET:
		RETURN(_getcodeset());

	case DAY_1: case DAY_2: case DAY_3: case DAY_4:
	case DAY_5: case DAY_6: case DAY_7:
		_settime();
		RETURN((char *)__time[Sun+(item-DAY_1)]);

	case ABDAY_1: case ABDAY_2: case ABDAY_3: case ABDAY_4:
	case ABDAY_5: case ABDAY_6: case ABDAY_7:
		_settime();
		RETURN((char *)__time[aSun+(item-ABDAY_1)]);

	case MON_1: case MON_2: case MON_3: case MON_4:
	case MON_5: case MON_6: case MON_7: case MON_8:
	case MON_9: case MON_10: case MON_11: case MON_12:
		_settime();
		RETURN((char *)__time[Jan+(item-MON_1)]);

	case ABMON_1: case ABMON_2: case ABMON_3: case ABMON_4:
	case ABMON_5: case ABMON_6: case ABMON_7: case ABMON_8:
	case ABMON_9: case ABMON_10: case ABMON_11: case ABMON_12:
		_settime();
		RETURN((char *)__time[aJan+(item-ABMON_1)]);

	case ALT_DIGITS:
		_settime();
		if (digit_data == NULL)
			digit_data = (struct databuf *)
			    calloc(1, sizeof (struct databuf));
		if (databuf_check_cache(digit_data) == 0)
			RETURN(digit_data->data);
		if (_num_alt_digits != 0) {
                        totalbytes = 0;
			for (i = 0; i < _num_alt_digits; i++)
				totalbytes += strlen(_alt_digits[i])+1;
			databuf_check_size(digit_data, totalbytes);
			if (digit_data->data == NULL)	/* no mem */
				RETURN("");
			for (i = 0; i < _num_alt_digits; i++) {
				if (i != 0)
					(void) strcat(digit_data->data, ";");
				(void) strcat(digit_data->data, _alt_digits[i]);
			}
			RETURN(digit_data->data);
		}
		RETURN("");

	case AM_STR :
		_settime();
		RETURN((char *)__time[AM]);

	case PM_STR :
		_settime();
		RETURN((char *)__time[PM]);

	case T_FMT_AMPM :
		_settime();
		RETURN((char *)__time[T_fmt_ampm]);

	case ERA:
		_settime();
		if (era_data == NULL)
			era_data = (struct databuf *)
			    calloc(1, sizeof (struct databuf));
		if (databuf_check_cache(era_data) == 0)
			RETURN(era_data->data);
		if (_num_eras != 0) {
                        totalbytes = 0;
			for (i = 0; i < _num_eras; i++)
				totalbytes += strlen(_eras[i])+1;
			databuf_check_size(era_data, totalbytes);
			if (era_data->data == NULL)	/* no mem */
				RETURN("");
			for (i = 0; i < _num_eras; i++) {
				if (i != 0)
					(void) strcat(era_data->data, ";");
				(void) strcat(era_data->data, _eras[i]);
			}
			RETURN(era_data->data);
		}
		RETURN("");

	case ERA_T_FMT :
		_settime();
		RETURN((char *)__time[Era_t_fmt]);

	case T_FMT :
		_settime();
		RETURN((char *)__time[Local_time]);	/* %X */

	case ERA_D_FMT :
		_settime();
		RETURN((char *)__time[Era_d_fmt]);

	case D_FMT :
		_settime();
		RETURN((char *)__time[Local_date]);	/* %x */

	case _DATE_FMT :
		_settime();
		RETURN((char *)__time[DATE_FMT]);	/* %C -- non XPG4 */

	case ERA_D_T_FMT :
		_settime();
		RETURN((char *)__time[Era_d_t_fmt]);

	case D_T_FMT:
		_settime();
		RETURN((char *)__time[DFL_FMT]);	/* %c */

		/*
		 * NOTE: The application must be linked with -lintl
		 * in order for nl_langinfo(YESSTR|NOSTR) to work
		 * properly because it is implemented using gettext().
		 *
		 * NOTE: XPG3 says nl_langinfo(YES/NOSTR) should depend on
		 * LC_ALL category.  But there is no such category as
		 * LC_ALL.  LC_ALL is a collection of categories.
		 * This must be a mistake.  This implementation uses
		 * LC_MESSAGES category instead.  This error in XPG3
		 * has been corrected in XPG4.
		 */
	case YESSTR:
		RETURN((char *)gettxt("SUNW_OST_LINFO:1", "yes"));
	case NOSTR:
		RETURN((char *)gettxt("SUNW_OST_LINFO:2", "no"));
	case YESEXPR:
		RETURN((char *)gettxt("SUNW_OST_LINFO:3", "^[yY]"));
	case NOEXPR:
		RETURN((char *)gettxt("SUNW_OST_LINFO:4", "^[nN]"));

	case CRNCYSTR:
		/*
		 * There is a subtle difference between the semantics of
		 * localeconv() and that of nl_langinfo().
		 * The code here tries to simulate
		 * the nl_langinfo() using the information made for
		 * localeconv().  Thus there are following limitations:
		 *	o nl_langinfo(), by definition, doesn't support
		 *	  a monetary format where the position of currency
		 *	  symbol differes depending on the sign of the value.
		 *	  nl_langinfo() only reflects the currency symbol
		 *	  position for the non-negative values.
		 *	o This implementation of nl_langinfo() cannot
		 *	  represents a monetary format in which the currency
		 *	  symbol comes where the decimal point should be.
		 *	  This is because the database is built for
		 *	  localeconv() which doesn't support such a format.
		 */

		/*
		 * localeconv() call now replaced by _localeconv_nolock() since
		 * localeconv() has its own _mutex_lock() and _mutex_unlock()
		 * calls, that causes hang. _localeconv_nolock() is identical
		 * to localeconv() except there is no _mutex_lock() call.
		 */
		lconv = _localeconv_nolock();
		if (lconv->p_cs_precedes == CHAR_MAX ||
		    *(lconv->currency_symbol) == '\0') {
			RETURN("");
		} else {
			langinfobuf[0] = (lconv->p_cs_precedes == 1)?'-':'+';
			strncpy(langinfobuf+1, lconv->currency_symbol,
				MAXANSLENGTH-1);
			RETURN(langinfobuf);
		}

	case RADIXCHAR:
		langinfobuf[0] = (char)_numeric[0];
		langinfobuf[1] = (char)0;
		RETURN(langinfobuf);

	case THOUSEP:
		langinfobuf[0] = (char)_numeric[1];
		langinfobuf[1] = (char)0;
		RETURN(langinfobuf);

	default:
		RETURN("");
	}
}

char *
_getcodeset()
{
	int fd;
	ssize_t len;
	char *locale;
	static char codesetname[CHARCLASS_NAME_MAX+1];

	locale = _cur_locale[LC_CTYPE];

	if ((fd = open(_fullocale(locale, "LC_CTYPE/charmap"), O_RDONLY)) == -1)
		RETURN("");

	if ((len = read(fd, codesetname, CHARCLASS_NAME_MAX)) != -1) {
		codesetname[len] = 0;
	} else {
		close(fd);
		RETURN("");
	}
	close(fd);
	RETURN(codesetname);
}

static int
databuf_check_cache(databuf)
struct databuf *databuf;
{
	/*
	 * checks to see if the locale information is
	 * cached by comparing current LC_TIME category
	 * with LC_TIME category associated with this saved
	 * data buffer. If the cached data is not good, sets
	 * up the new cached locale name and voids the cache.
	 * returns:
	 * 0 if cached data is good
	 * 1 if cached data is not good
	 */

	if ((databuf != NULL) &&
	    (databuf->locale_name != NULL) &&
	    (strncmp(_cur_locale[LC_TIME], databuf->locale_name,
	    CHARCLASS_NAME_MAX) == 0))
		return (0);
	else {
		strncpy(databuf->locale_name, _cur_locale[LC_TIME],
		    CHARCLASS_NAME_MAX);
		memset(databuf->data, 0, databuf->datalen);
		return (1);
	}
}

static void
databuf_check_size(databuf, totalbytes)
struct databuf *databuf;
int totalbytes;
{

	/*
	 * checks if there are at least totalbytes already
	 * allocated in the data area. If not:
	 *      frees the old data
	 *      allocate totalbytes space
	 *      set datalen to totalbytes
	 */
	if (totalbytes > databuf->datalen) {
		if (databuf->data != NULL)
			free(databuf->data);
		databuf->data = (char *) malloc(totalbytes);
		databuf->datalen = totalbytes;
	}
}
