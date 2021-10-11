#pragma ident	"@(#)execle.c	1.7	92/07/20 SMI" 
#include <varargs.h>

/*
 *	execle(name, arg0, arg1, ..., argn, (char *)0, envp)
 */
/*VARARGS1*/
execle(name, va_alist)
	register char *name;
	va_dcl
{
	register char **first;
	va_list args;

	va_start(args);
	first = (char **)args;
	while (va_arg(args, char *))	/* traverse argument list to 0 */
		;
	/* environment is next arg */
	return(execve(name, first, *(char **)args));
}
