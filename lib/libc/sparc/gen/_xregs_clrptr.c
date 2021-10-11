/*
 *	Copyright (c) 1994 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)_xregs_clrptr.c	1.2	94/09/17 SMI"

#include "synonyms.h"
#include <ucontext.h>

/*
 * clear the struct ucontext extra register state pointer
 */
void
_xregs_clrptr(uc)
	ucontext_t *uc;
{
	uc->uc_mcontext.xrs.xrs_id = 0;
	uc->uc_mcontext.xrs.xrs_ptr = (caddr_t)NULL;
}
