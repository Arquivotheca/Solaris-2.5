/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright  (c) 1985 AT&T
 *	All Rights Reserved
 */
#ident	"@(#)objform.h	1.4	92/07/14 SMI"       /* SVr4.0 1.4 */

typedef struct {
	int flags;
	int curpage;		/* current form page */
	int lastpage;		/* last page of the form */
	int curfield;		/* current field number */
	int numactive;		/* number of active fields */
	char **holdptrs;	/* array of low-level field structures */
	char **mulvals;		/* field specific variables (F1, F2, etc.) */
	struct fm_mn fm_mn;	/* main structure for form descriptors */
	int *visible;		/* list of active/visible fields */
	int *slks;		/* list of SLKS specific to this form */ 
} forminfo;
