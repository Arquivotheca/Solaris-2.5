#pragma ident	"@(#)ftime.c	1.11	92/07/21 SMI"
	/* from UCB 4.1 83/05/01 */

#include <sys/types.h>
#include <sys/time.h>

/*
 * Backwards compatible ftime.
 */
/* these two ints are from libc */
extern int _timezone;
extern int _daylight;


/* from old timeb.h */
struct timeb {
	time_t	time;
	u_short	millitm;
	short	timezone;
	short	dstflag;
};

ftime(tp)
	register struct timeb *tp;
{
	struct timeval t;

	_ltzset(0);
	if (_gettimeofday(&t) < 0)
		return (-1);

	tp->time = t.tv_sec;
	tp->millitm = t.tv_usec / 1000;
	tp->timezone = _timezone / 60;
	tp->dstflag = _daylight;
}
