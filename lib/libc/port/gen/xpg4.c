/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 */

#ident	"@(#)xpg4.c	1.1	94/02/03 SMI"

/*
 * __xpg4 == 0 by default. The xpg4 cc driver will add an object
 * file that contains int __xpg4 = 1". The symbol interposition
 * provided by the linker will allow libc to find that symbol
 * instead.
 */

int __xpg4 = 0;
