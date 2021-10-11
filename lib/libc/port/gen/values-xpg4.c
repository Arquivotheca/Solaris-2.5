/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 */

#ident	"@(#)values-xpg4.c	1.1	94/02/04 SMI"

/* Setting thie value to 1 enables XPG4 mode for APIs
 * which have differing runtime behaviour from XPG3 to XPG4.
 * See usr/src/lib/libc/port/gen/xpg4.c for the default value.
 */

int __xpg4 = 1;
