/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)getkval.c	1.4	92/07/14 SMI"	/* SVr4.0 1.2 */

#include <fcntl.h>
#include <nlist.h>
#include <kvm.h>
#include <stdio.h>

/*
 * Get a value for a named kernel variable
 */
int
getkval(char *name, unsigned size, char *result)
{
	static kvm_t *kd;
	struct nlist kvalue[2];

	if (!kd && !(kd = kvm_open(NULL, NULL, NULL, O_RDONLY, NULL)))
		return (0);

	kvalue[0].n_name = name;
	kvalue[1].n_name = (char *)0;

	if (kvm_nlist(kd, kvalue) == -1)
		return (0);

	if (kvm_read(kd, kvalue[0].n_value, result, size) != size)
		return (0);

	return (1);
}
