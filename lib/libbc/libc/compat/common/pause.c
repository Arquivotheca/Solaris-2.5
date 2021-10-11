#pragma ident	"@(#)pause.c	1.11	92/07/21 SMI"
	  /* from UCB 4.1 83/06/09 */

/*
 * Backwards compatible pause.
 */
pause()
{

	sigpause(sigblock(0));
	return (-1);
}
