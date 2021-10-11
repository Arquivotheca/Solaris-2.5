/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)addsev.c	1.3	93/12/01 SMI"

#include "synonyms.h"
#include <stdlib.h>
#include <pfmt.h>
#include <thread.h>
#include "pfmt_data.h"

addsev(severity, string)
int severity;
const char *string;
{
	register int i, firstfree;
	/* Cannot redefine standard severity */
	if ((severity <= 4) || (severity > 255))
		return (-1);

	/* Locate severity in table */
	rw_wrlock(&_rw_pfmt_sev_tab);
	for (i = 0, firstfree = -1; i < __pfmt_nsev; i++) {
		if (__pfmt_sev_tab[i].severity == 0 && firstfree == -1)
			firstfree = i;
		if (__pfmt_sev_tab[i].severity == severity)
			break;
	}

	if (i == __pfmt_nsev) {
		/* Removing non-existing severity */
		if (!string)
			return (0);
		/* Re-use old entry */
		if (firstfree != -1)
			i = firstfree;
		else {
			/* Allocate new entry */
			if (__pfmt_nsev++ == 0) {
				if ((__pfmt_sev_tab =
					malloc(sizeof (struct sev_tab)))
					== NULL) {
					rw_unlock(&_rw_pfmt_sev_tab);
					return (-1);
				}
			} else {
				if ((__pfmt_sev_tab = realloc(__pfmt_sev_tab,
					sizeof (struct sev_tab) * __pfmt_nsev))
					== NULL) {
					rw_unlock(&_rw_pfmt_sev_tab);
					return (-1);
				}
			}
			__pfmt_sev_tab[i].severity = severity;
			__pfmt_sev_tab[i].string = NULL;
		}
	}
	if (!string) {
		if (__pfmt_sev_tab[i].string)
			free(__pfmt_sev_tab[i].string);
		__pfmt_sev_tab[i].severity = 0;
		rw_unlock(&_rw_pfmt_sev_tab);
		return (0);
	}
	if (__pfmt_sev_tab[i].string) {
		if ((__pfmt_sev_tab[i].string = realloc(__pfmt_sev_tab[i].string,
			strlen(string) + 1)) == NULL) {
			rw_unlock(&_rw_pfmt_sev_tab);
			return (-1);
		}
	}
	else
		if ((__pfmt_sev_tab[i].string = malloc(strlen(string) + 1)) == NULL) {
			rw_unlock(&_rw_pfmt_sev_tab);
			return (-1);
		}
	strcpy(__pfmt_sev_tab[i].string, string);
	rw_unlock(&_rw_pfmt_sev_tab);
	return (0);
}
