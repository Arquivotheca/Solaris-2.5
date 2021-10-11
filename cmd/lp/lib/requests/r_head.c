/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)r_head.c	1.6	93/10/15 SMI"	/* SVr4.0 1.5	*/
/* EMACS_MODES: !fill, lnumb, !overwrite, !nodelete, !picture */

#include "sys/types.h"

#include "lp.h"
#include "requests.h"

struct {
	char			*v;
	short			len;
}			reqheadings[RQ_MAX] = {

#define	ENTRY(X)	X, sizeof(X)-1

	ENTRY("C "),	/* RQ_COPIES */
	ENTRY("D "),	/* RQ_DEST */
	ENTRY("F "),	/* RQ_FILE */
	ENTRY("f "),	/* RQ_FORM */
	ENTRY("H "),	/* RQ_HANDL */
	ENTRY("N "),	/* RQ_NOTIFY */
	ENTRY("O "),	/* RQ_OPTS */
	ENTRY("P "),	/* RQ_PRIOR */
	ENTRY("p "),	/* RQ_PGES */
	ENTRY("S "),	/* RQ_CHARS */
	ENTRY("T "),	/* RQ_TITLE */
	ENTRY("Y "),	/* RQ_MODES */
	ENTRY("t "),	/* RQ_TYPE */
	ENTRY("U "),	/* RQ_USER */
	ENTRY("r "),	/* RQ_RAW */
	ENTRY("a "),	/* RQ_FAST */
	ENTRY("s "),	/* RQ_STAT */
	ENTRY("v "),	/* RQ_VERSION */
/*	ENTRY("x "), */	/* reserved (slow filter) */
/*	ENTRY("y "), */	/* reserved (fast filter) */
/*	ENTRY("z "), */	/* reserved (printer name) */

#undef	ENTRY

};
