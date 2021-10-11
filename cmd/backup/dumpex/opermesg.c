#ident	"@(#)opermesg.c 1.15 92/03/31"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "structs.h"
#include <operator.h>
#include <config.h>
#include <netdb.h>
#include <syslog.h>

/*
 * this file is short because the include files are lengthy and may cause
 * compiles to slow a bit.  So, the include/compile is isolated in this
 * wrapper
 */

#define	MINUTES(x)	(SECONDSPERMINUTE*(x))

/* once upon beginning of program: */
void
display_init(void)
{
	if (nswitch)
		return;
	(void) readconfig((char *)0, (void (*)(const char *, ...))0);
	(void) getopserver(opserver, BCHOSTNAMELEN);
	/* XXX check return code */
	(void) oper_init(opserver, progname, 0);
}

/* puts up message */

void
display(mesg)
	char	*mesg;
{

	if (nswitch)
		return;
	if (debug)
		(void) printf("opermesg>> %s\n", mesg);
	(void) oper_send(MINUTES(10), LOG_NOTICE, MSG_DISPLAY, mesg);
}
