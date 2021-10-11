/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)crypt.c	1.6	92/07/14 SMI"
/*	From:	SVr4.0	libcrypt_x:crypt.c	1.2		*/

#ifdef __STDC__
	#pragma weak setkey = _setkey
	#pragma weak encrypt = _encrypt
	#pragma weak crypt = _crypt
#endif
#include "synonyms.h"

void setkey (key)
/*const*/ char *key;
{
	extern void	des_setkey();
	des_setkey(key);
}

void encrypt(block, edflag)
char *block;
int edflag;
{
	extern void	des_encrypt();
	des_encrypt(block, edflag);
}

char *
crypt(pw, salt)
/*const*/ char *pw;
/*const*/ char *salt;
{
	extern char	*des_crypt();
	
	return(des_crypt(pw, salt));
}
