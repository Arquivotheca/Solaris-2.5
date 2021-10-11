/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)setlabel.c	1.2	93/12/01 SMI"

#include "synonyms.h"
#include <pfmt.h>
#include <thread.h>
#include "pfmt_data.h"
#include <string.h>

int
setlabel(label)
const char *label;
{
	rw_wrlock(&_rw_pfmt_label);
	if (!label)
		__pfmt_label[0] = '\0';
	else {
		strncpy(__pfmt_label, label, sizeof (__pfmt_label) - 1);
		__pfmt_label[sizeof (__pfmt_label) - 1] = '\0';
	}
	rw_unlock(&_rw_pfmt_label);
	return (0);
}
