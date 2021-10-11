/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ident	"@(#)_hidden_dgettext.c	1.3	92/07/14 SMI"

#pragma weak	_dgettext = __well_hidden_dgettext

#include "synonyms.h"

/* ARGUSED */
char *
__well_hidden_dgettext(const char *domain, const char *msg_id)
{
	return((char *)msg_id);
}
