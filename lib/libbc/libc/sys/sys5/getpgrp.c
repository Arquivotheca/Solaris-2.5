#pragma ident	"@(#)getpgrp.c	1.2	92/07/20 SMI" 

/*
	getpgrp -- system call emulation for 4.2BSD

	last edit:	01-Jul-1983	D A Gwyn
*/

extern int	_getpgrp();

int
getpgrp()
	{
	return _getpgrp( 0 );		/* 0 means this process */
	}
