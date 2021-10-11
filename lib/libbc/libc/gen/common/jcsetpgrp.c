#pragma ident	"@(#)jcsetpgrp.c	1.3	92/07/20 SMI" 

#include <sys/syscall.h>

/*
 * POSIX call to set job control process group of current process.
 * Use 4BSD "setpgrp" call, but don't call "setpgrp" since that may refer
 * to SVID "setpgrp" call in System V environment.
 */
int
jcsetpgrp(pgrp)
	int pgrp;
{
	return (setpgid(0,pgrp));
}
