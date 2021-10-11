/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)pfmt_data.c	1.2	93/11/08 SMI"

#include <pfmt.h>
#include <thread.h>
#include "mtlib.h"

char __pfmt_label[MAXLABEL];
struct sev_tab *__pfmt_sev_tab;
int __pfmt_nsev;

rwlock_t _rw_pfmt_label = DEFAULTRWLOCK;
rwlock_t _rw_pfmt_sev_tab = DEFAULTRWLOCK;
