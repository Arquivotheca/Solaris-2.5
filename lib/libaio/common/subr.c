#ident	"@(#)subr.c	1.5	95/01/23 SMI"

#include <sys/types.h>
#include <sys/reg.h>

void _aiopanic();

static void
_halt()
{
	pause();
}

int _halted = 0;

void
_aiopanic(s)
char *s;
{
	char buf[256];

	_halted = 1;
	sprintf(buf, "AIO PANIC (LWP = %d): %s\n", _lwp_self(), s);
	write(1, buf, strlen(buf));
	_halt();
}

assfail(a, f, l)
	char *a;
	char *f;
	int l;
{
	char buf[256];

	sprintf(buf, "assertion failed: %s, file: %s, line:%d", a, f, l);
	_aiopanic(buf);
}
