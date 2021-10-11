/*
 * Copyright (c) 1989, 1991, by Sun Microsystems, Inc.
 */

#ident	"@(#)func_decim.c	1.5	92/07/14 SMI"

#ifdef __STDC__  
	#pragma weak func_to_decimal = _func_to_decimal 
#endif

#include "synonyms.h"
#include <ctype.h>
#include <stdio.h>
#include "base_conversion.h"
#include <locale.h>

void
func_to_decimal(ppc, nmax, fortran_conventions, pd, pform, pechar, pget, pnread, punget)
	char          **ppc;
	int             nmax;
	int             fortran_conventions;
	decimal_record *pd;
	enum decimal_string_form *pform;
	char          **pechar;
	int             (*pget) ();
int            *pnread;
int             (*punget) ();

{
	register char  *cp = *ppc;
	register int    current;
	register int    nread = 1;	/* Number of characters read so far. */
	char           *good = cp - 1;	/* End of known good token. */
	char           *cp0 = cp;

	current = (*pget) ();	/* Initialize buffer. */
	*cp = current;

#define ATEOF current
#define CURRENT current
#define ISSPACE isspace
#define NEXT \
	if (nread < nmax) \
		{ cp++ ; current = (*pget)() ; *cp = current ; nread++ ;} \
	else \
		{ current = NULL ; } ;

#include "char_to_decimal.h"
#undef CURRENT
#undef NEXT

	if ((nread < nmax) && (punget != NULL)) {
		while (cp >= *ppc) {	/* Push back as many excess
					 * characters as possible. */
			if (*cp != EOF) {	/* Can't push back EOF. */
				if ((*punget) (*cp) == EOF)
					break;
			}
			cp--;
			nread--;
		}
	}
	cp++;
	*cp = 0;		/* Terminating null. */
	*pnread = nread;
}
