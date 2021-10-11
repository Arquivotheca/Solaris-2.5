/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)des_crypt.c	1.13	95/05/03 SMI"
/*	From:	SVr4.0	libcrypt_x:des_crypt.c	1.6		*/

#pragma weak des_crypt = _des_crypt
#pragma weak des_encrypt = _des_encrypt
#pragma weak des_setkey = _des_setkey

#include "synonyms.h"

#if INTERNATIONAL
#include 	<errno.h>
#endif

#ifdef _REENTRANT
#include <stdlib.h>
#include "mtlib.h"
#endif /* _REENTRANT */

/*LINTLIBRARY*/


static void des_setkey_nolock(key)
/* const */ char	*key;
{
}

void
des_setkey(key)
/* const */ char	*key;
{
}




static void des_encrypt_nolock(block, edflag)
char	*block;
int	edflag;
{
}

void
des_encrypt(block, edflag)
char	*block;
int	edflag;
{
}



#define	IOBUF_SIZE	16

#ifdef _REENTRANT
static char *
_get_iobuf(thread_key_t *key, unsigned size)
{
	char *iobuf = NULL;

	if (_thr_getspecific(*key, &iobuf) != 0) {
		if (_thr_keycreate(key, free) != 0) {
			return (NULL);
		}
	}

	if (!iobuf) {
		if (_thr_setspecific(*key, (void *)(iobuf = malloc(size)))
			!= 0) {
			if (iobuf)
				(void) free(iobuf);
			return (NULL);
		}
	}
	return (iobuf);
}
#endif /* _REENTRANT */

char *
des_crypt(pw, salt)
/* const */ char	*pw, *salt;
{
	return (0);
}
