#ident	"@(#)ramdata.c	1.1	94/11/10 SMI"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/fault.h>
#include <sys/syscall.h>
#include "pcontrol.h"
#include "ramdata.h"

/* ramdata.c -- read/write data definitions for process management */

char *	command = NULL;		/* name of command ("truss") */
int	interrupt = FALSE;	/* interrupt signal was received */

int	Fflag = FALSE;		/* option flags from getopt() */
int	qflag = FALSE;

char *	str_buffer = NULL;	/* fetchstring() buffer */
unsigned str_bsize = 0;		/* sizeof(*str_buffer) */

process_t	Proc;		/* the process structure */
process_t *	PR = NULL;	/* pointer to same (for abend()) */

char *	procdir = "/proc";	/* default PROC directory */
sigset_t psigs;			/* pending signals (used by Psyscall()) */

int	timeout = FALSE;	/* set TRUE by SIGALRM catchers */

int	debugflag = FALSE;	/* enable debugging printfs */
