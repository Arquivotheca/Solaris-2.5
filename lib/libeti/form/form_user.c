/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)form_user.c	1.2	92/07/14 SMI"	/* SVr4.0 1.1	*/

#include "utility.h"

	/*********************
	*  set_form_userptr  *
	*********************/

int set_form_userptr (f, userptr)
FORM * f;
char * userptr;
{
	Form (f) -> usrptr = userptr;
	return E_OK;
}

char * form_userptr (f)
FORM * f;
{
	return Form (f) -> usrptr;
}

