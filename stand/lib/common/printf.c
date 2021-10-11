/* Copyright (c) 1991 Sun Microsystems, Inc. */
#ident	"@(#)printf.c	1.2	92/07/14 SMI"

/*
 * Copyright (c) 1982, 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 */

#include <sys/types.h>
#include <sys/varargs.h>
#include <sys/promif.h>

/*VARARGS1*/
void
printf(char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	prom_vprintf(fmt, adx);
	va_end(adx);
}
