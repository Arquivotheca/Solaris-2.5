#pragma ident	"@(#)execv.c	1.6	92/07/20 SMI" 
/*
 *	execv(file, argv)
 *
 *	where argv is a vector argv[0] ... argv[x], NULL
 *	last vector element must be NULL
 *	environment passed automatically
 */

execv(file, argv)
	char	*file;
	char	**argv;
{
	extern	char	**environ;

	return(execve(file, argv, environ));
}
