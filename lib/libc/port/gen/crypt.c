/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)crypt.c	1.18	95/03/14 SMI"	/* SVr4.0 1.11	*/

/*LINTLIBRARY*/
/*
 * This program implements a data encryption algorithm to encrypt passwords.
 */
#ifdef __STDC__
#pragma weak crypt = _crypt
#pragma weak encrypt = _encrypt
#pragma weak setkey = _setkey
#endif
#include "synonyms.h"

char *
crypt(pw, salt)
char	*pw, *salt;
{
	return (0);
}


/*ARGSUSED*/
void
encrypt(block,fake)
char	*block;
int	fake;
{
}


void
setkey(key)
const char	*key;
{
}
