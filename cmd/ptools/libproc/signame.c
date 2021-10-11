#ident	"@(#)signame.c	1.1	94/11/10 SMI"

#include <stdio.h>
#include <sys/signal.h>

static char sig_name[20];

char *
rawsigname(sig)		/* return the name of the signal */
int sig;		/* return NULL if unknown signal */
{
	/* belongs in some header file */
	extern int sig2str(int, char *);

	/*
	 * The C library function sig2str() omits the leading "SIG".
	 */
	(void) strcpy(sig_name, "SIG");

	if (sig > 0 && sig2str(sig, sig_name+3) == 0)
		return sig_name;

	return NULL;
}

char *
signame(sig)		/* return the name of the signal */
int sig;		/* manufacture a name for unknown signal */
{
	register char * name = rawsigname(sig);

	if (name == NULL) {			/* manufacture a name */
		(void) sprintf(sig_name, "SIG#%d", sig);
		name = sig_name;
	}

	return name;
}
