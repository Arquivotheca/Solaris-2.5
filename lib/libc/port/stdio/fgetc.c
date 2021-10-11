/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)fgetc.c	1.7	92/09/05 SMI"	/* SVr4.0 1.8	*/

/*	3.0 SID #	1.2	*/
/*LINTLIBRARY*/

#include "synonyms.h"
#include "shlib.h"
#include <stdio.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

int
fgetc(iop)
	register FILE *iop;
{
	return GETC(iop);
}

