#pragma ident	"@(#)execl.c	1.7	92/07/20 SMI" 

#include <varargs.h>

/*
 *	execl(name, arg0, arg1, ..., argn, (char *)0)
 *	environment automatically passed.
 */
/*VARARGS1*/
execl(name, va_alist)
	register char *name;
	va_dcl
{
	extern char **environ;
	va_list args;

	va_start(args);
	return (execve(name, (char **)args, environ));
}
