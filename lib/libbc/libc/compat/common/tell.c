#pragma ident	"@(#)tell.c	1.10	92/07/21 SMI"
	  /* from UCB 4.1 80/12/21 */

/*
 * return offset in file.
 */

long	lseek();

long tell(f)
{
	return(lseek(f, 0L, 1));
}
