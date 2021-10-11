/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 * Copyright (c) 1988 by Nihon Sun Microsystems K.K.
 */

#ident	"@(#)csetlen.c	1.4	92/07/14 SMI"   /* Nihon Sun Micro JLE */

#include	"synonyms.h"
#include	<ctype.h>
#include	<euc.h>

int
csetlen(cset)
int cset;	/* Code set number. 0, 1, 2, or 3. */
{
	switch (cset) {
        case 0: return 1;
        case 1: return eucw1;
        case 2: return eucw2;
        case 3: return eucw3;
	default: return 0;
        }
}


int
csetcol(cset)
int cset;	/* Code set number. 0, 1, 2, or 3. */
{
	switch (cset) {
        case 0: return 1;
        case 1: return scrw1;
        case 2: return scrw2;
        case 3: return scrw3;
	default: return 0;
        }
}
