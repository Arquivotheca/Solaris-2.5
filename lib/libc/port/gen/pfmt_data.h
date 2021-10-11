/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)pfmt_data.h	1.3	93/11/08 SMI"

#include <synch.h>
#include "mtlib.h"

/* Severity */
struct sev_tab {
	int severity;
	char *string;
};

extern char __pfmt_label[MAXLABEL];
extern struct sev_tab *__pfmt_sev_tab;
extern int __pfmt_nsev;

extern rwlock_t _rw_pfmt_label;
extern rwlock_t _rw_pfmt_sev_tab;
