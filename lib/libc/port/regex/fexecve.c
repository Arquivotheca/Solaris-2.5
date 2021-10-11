/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 *
 * This code is MKS code ported to Solaris with minimum modifications so that
 * upgrades from MKS will readily integrate. Avoid modification if possible!
 */
#ident	"@(#)fexecve.c 1.1	94/10/12 SMI"

/*
 * MKS C library -- fexecve
 *
 * Copyright 1985, 1992 by Mortice Kern Systems Inc.  All rights reserved.
 *
 * This Software is unpublished, valuable, confidential property of
 * Mortice Kern Systems Inc.  Use is authorized only in accordance
 * with the terms and conditions of the source licence agreement
 * protecting this Software.  Any unauthorized use or disclosure of
 * this Software is strictly prohibited and will result in the
 * termination of the licence agreement.
 *
 * If you have any questions, please consult your supervisor.
 * 
 */
#ifdef M_RCSID
#ifndef lint
static char rcsID[] = "$Header: /u/rd/src/libc/sys/RCS/fexecve.c,v 1.9 1992/06/19 13:30:36 gord Exp $";
#endif
#endif

#include <mks.h>
#include <unistd.h>

/*f
 * Fork and exec (but no wait).
 */
LDEFN pid_t
fexecve(path, argv, envp)
const char *path;
char *const *argv;
char *const *envp;
{
	register pid_t pid;

	if ((pid = fork()) == -1)
		return (-1);
	if (pid != 0)
		return (pid);
	(void)execve(path, argv, envp);
	exit(-1);
}
