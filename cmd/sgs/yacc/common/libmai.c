/*	Copyright (c) 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)libmai.c	6.5	93/06/07 SMI"

#include <locale.h>

main(){
	setlocale(LC_ALL, "");
	yyparse();
	return 0;
	}
