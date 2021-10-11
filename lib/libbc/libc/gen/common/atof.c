#pragma ident	"@(#)atof.c	1.2	92/07/20 SMI" 

/*
 * Copyright (c) 1986 by Sun Microsystems, Inc. 
 */

#include <stdio.h>

extern double 
strtod();

double
atof(cp)
	char           *cp;
{
	return strtod(cp, (char **) NULL);
}
