/*
 * Copyright (c) 1986-1991 by Sun Microsystems Inc.
 */

#ident	"@(#)xcrypt.c	1.10	95/04/28 SMI"

#if !defined(lint) && defined(SCCSIDS)
static char sccsid[] = "@(#)xcrypt.c 1.3 89/03/24 Copyr 1986 Sun Micro";
#endif

/*
 * xcrypt.c: Hex encryption/decryption and utility routines
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <rpc/trace.h>
#include <rpc/des_crypt.h>
#include <string.h>

static char hex[];	/* forward */
static char hexval();
static int hex2bin();
static int bin2hex();

int passwd2des();


/*
 * Encrypt a secret key given passwd
 * The secret key is passed and returned in hex notation.
 * Its length must be a multiple of 16 hex digits (64 bits).
 */
xencrypt(secret, passwd)
	char *secret;
	char *passwd;
{
	trace1(TR_xencrypt, 1);
	return (0);
}

/*
 * Decrypt secret key using passwd
 * The secret key is passed and returned in hex notation.
 * Once again, the length is a multiple of 16 hex digits
 */
xdecrypt(secret, passwd)
	char *secret;
	char *passwd;
{
	trace1(TR_xdecrypt, 1);
	return (0);
}

/*
 * Turn password into DES key
 */
passwd2des(pw, key)
	char *pw;
	char *key;
{
	int i;

	trace1(TR_passwd2des, 0);
	memset(key, 0, 8);
	for (i = 0; *pw; i = (i+1) % 8) {
		key[i] ^= *pw++ << 1;
	}
	des_setparity(key);
	trace1(TR_passwd2des, 1);
	return (1);
}

/*
 * Hex to binary conversion
 */
static
hex2bin(len, hexnum, binnum)
	int len;
	char *hexnum;
	char *binnum;
{
	int i;

	trace2(TR_hex2bin, 0, len);
	for (i = 0; i < len; i++) {
		*binnum++ = 16 * hexval(hexnum[2 * i]) +
					hexval(hexnum[2 * i + 1]);
	}
	trace1(TR_hex2bin, 1);
	return (1);
}

/*
 * Binary to hex conversion
 */
static
bin2hex(len, binnum, hexnum)
	int len;
	unsigned char *binnum;
	char *hexnum;
{
	int i;
	unsigned val;

	trace2(TR_bin2hex, 0, len);
	for (i = 0; i < len; i++) {
		val = binnum[i];
		hexnum[i*2] = hex[val >> 4];
		hexnum[i*2+1] = hex[val & 0xf];
	}
	hexnum[len*2] = 0;
	trace1(TR_bin2hex, 1);
	return (1);
}

static char hex[16] = {
	'0', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
};

static char
hexval(c)
	char c;
{
	trace1(TR_hexval, 0);
	if (c >= '0' && c <= '9') {
		trace1(TR_hexval, 1);
		return (c - '0');
	} else if (c >= 'a' && c <= 'z') {
		trace1(TR_hexval, 1);
		return (c - 'a' + 10);
	} else if (c >= 'A' && c <= 'Z') {
		trace1(TR_hexval, 1);
		return (c - 'A' + 10);
	} else {
		trace1(TR_hexval, 1);
		return (-1);
	}
}
