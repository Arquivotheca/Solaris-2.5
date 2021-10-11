/*
 * Copyright (c) 1986 - 1991 by Sun Microsystems, Inc.
 */

#ident	"@(#)des_crypt.c	1.8	94/10/19 SMI"

#if !defined(lint) && defined(SCCSIDS)
static char sccsid[] = "@(#)des_crypt.c 1.2 89/03/10 Copyr 1986 Sun Micro";
#endif

/*
 * des_crypt.c, DES encryption library routines
 */

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <rpc/trace.h>
#include <rpc/des_crypt.h>

/*
 * CBC mode encryption
 */
cbc_crypt(key, buf, len, mode, ivec)
	char *key;
	char *buf;
	unsigned len;
	unsigned mode;
	char *ivec;
{
	return (DESERR_HWERROR);
}


/*
 * ECB mode encryption
 */
ecb_crypt(key, buf, len, mode)
	char *key;
	char *buf;
	unsigned len;
	unsigned mode;
{
	return (DESERR_HWERROR);
}


