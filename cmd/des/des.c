/*
 * Copyright (c) 1986 by Sun Microsystems, Inc.
 */

#ident	"@(#)des.c	1.4	94/10/14 SMI"

/*
 * DES encrypt/decrypt command
 * Features:
 *	Hardware or software implementation
 *	Cipher Block Chaining (default) or Electronic Code Book (-b) modes
 *  A word about the key:
 * 	The DES standard specifies that the low bit of each of the 8 bytes
 *	of the key is used for odd parity.  We prompt the user for an 8
 *	byte ASCII key and add parity to the high bit and use the result
 *	as the key.  The nature of parity is that given any 7 bits you can
 *	figure out what the missing bit should be, so it doesn't matter which
 *	bit is used for parity; the information (in the theoretical sense) is
 * 	the same.
 */
main(argc, argv)
	char **argv;
{
}



