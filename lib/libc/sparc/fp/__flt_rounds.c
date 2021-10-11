/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#ident	"@(#)__flt_rounds.c	1.3	92/07/14 SMI"

#include "synonyms.h"

/*
 * __flt_rounds() returns the prevailing rounding mode per ANSI C spec:
 *	 0:	toward zero
 *	 1:	to nearest			<<< default
 *	 2:	toward positive infinity
 *	 3:	toward negative infinity
 *	-1:	indeterminable			<<< never returned
 */

#include <floatingpoint.h>

extern enum fp_direction_type _QgetRD();

int
__flt_rounds()
{
	register int ansi_rd;

	switch (_QgetRD()) {
	case fp_tozero:		ansi_rd = 0; break;
	case fp_positive:	ansi_rd = 2; break;
	case fp_negative:	ansi_rd = 3; break;
	case fp_nearest:	/* FALLTHRU */
	default:		ansi_rd = 1; break;
	}
	return ansi_rd; 
}
