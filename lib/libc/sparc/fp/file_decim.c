/*
 * Copyright (c) 1988, 1991, by Sun Microsystems, Inc.
 */

#ident	"@(#)file_decim.c	1.5	92/09/05 SMI"

#ifdef __STDC__  
	#pragma weak file_to_decimal = _file_to_decimal 
#endif

#include "synonyms.h"
#include <ctype.h>
#include <stdio.h>
#include "base_conversion.h"
#include <locale.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

void
file_to_decimal(ppc, nmax, fortran_conventions, pd, pform, pechar, pf, pnread)
	char          **ppc;
	int             nmax;
	int             fortran_conventions;
	decimal_record *pd;
	enum decimal_string_form *pform;
	char          **pechar;
	FILE           *pf;
	int            *pnread;

{
	register char  *cp = *ppc;
	register int    current;
	register int    nread = 1;	/* Number of characters read so far. */
	char           *good = cp - 1;	/* End of known good token. */
	char           *cp0 = cp;

	current = GETC(pf);	/* Initialize buffer. */
	*cp = current;

#define ATEOF current
#define CURRENT current
#define ISSPACE isspace

/* if the _IOWRT flag is set, this must be a call from sscanf */
#define NEXT \
       if (nread < nmax) \
               { cp++ ; current = ((pf->_flag & _IOWRT) ? \
				((*pf->_ptr == '\0') ? EOF : *pf->_ptr++) : \
				GETC(pf)) ; \
				*cp = current ; nread++ ;} \
       else \
               { current = NULL ; } ;

#include "char_to_decimal.h"
#undef CURRENT
#undef NEXT

	if (nread < nmax) {
		while (cp >= *ppc) {	/* Push back as many excess
					 * characters as possible. */
			if (*cp != EOF) {	/* Can't push back EOF. */
				if (UNGETC(*cp, pf) == EOF)
					break;
			} cp--;
			nread--;
		}
	}
	cp++;
	*cp = 0;		/* Terminating null. */
	*pnread = nread;

}
