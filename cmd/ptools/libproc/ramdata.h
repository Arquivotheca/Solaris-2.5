#ident	"@(#)ramdata.h	1.1	94/11/10 SMI"

/* ramdata.h -- read/write data declarations */

/* requires:
	<stdio.h>
	<signal.h>
	<sys/types.h>
	<sys/fault.h>
	<sys/syscall.h>
	<sys/param.h>
	"pcontrol.h"
*/

/* maximum sizes of things */
#define	PRMAXSIG	(32*sizeof(sigset_t)/sizeof(long))
#define	PRMAXFAULT	(32*sizeof(fltset_t)/sizeof(long))
#define	PRMAXSYS	(32*sizeof(sysset_t)/sizeof(long))

extern	char *	command;	/* name of command ("truss") */
extern	int	interrupt;	/* interrupt signal was received */

extern	int	Fflag;		/* option flags from getopt() */
extern	int	qflag;

extern	char *	str_buffer;	/* fetchstring() buffer */
extern	unsigned str_bsize;	/* sizeof(*str_buffer) */

extern	process_t	Proc;	/* the process structure */
extern	process_t *	PR;	/* pointer to same (for abend()) */

extern	char *	procdir;	/* default PROC directory */
extern	sigset_t psigs;		/* pending signals (used by Psyscall()) */

extern	int	timeout;	/* set TRUE by SIGALRM catchers */

extern	int	debugflag;	/* enable debugging printfs */
