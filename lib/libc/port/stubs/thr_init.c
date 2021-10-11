/*      Copyright (c) 1995 Sun Microsystems Inc */
/*      All Rights Reserved. */

#ident  "@(#)thr_init.c 1.1	95/03/07 SMI"

/*
 * set __threaded variable. perf improvement for stdio.
 * If libthread gets linked in or is dlopened it calls
 * _libc_set_threaded to set __threaded to 1.
 */

/* CSTYLED */
#pragma init (_check_threaded)

void _check_threaded();
int __threaded;

void
_check_threaded()
{
	if (_thr_main() == -1)
		__threaded = 0;
	else
		__threaded = 1;
}

void
_libc_set_threaded()
{
	__threaded = 1;
}
