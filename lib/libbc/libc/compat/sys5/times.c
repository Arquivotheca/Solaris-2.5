#include <sys/types.h>
#include <sys/time.h>
#include <sys/times.h>

clock_t
times(tmsp)
	register struct tms *tmsp;
{
	int ret;

	ret = _times(tmsp);

	if (ret == -1)
		return(ret * _sysconf(_SC_CLK_TCK) / 60);
	else
		return(0);
}
