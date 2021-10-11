#pragma ident	"@(#)psignal.c	1.2	92/07/20 SMI" 

/*
 * Print the name of the signal indicated
 * along with the supplied message.
 */
#include <stdio.h>

extern	int fflush();
extern	void _psignal();

void
psignal(sig, s)
	unsigned sig;
	char *s;
{

	(void)fflush(stderr);
	_psignal(sig, s);
}
