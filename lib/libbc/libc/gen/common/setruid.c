#pragma ident	"@(#)setruid.c	1.3	92/07/21 SMI"
	  /* from UCB 4.1 83/06/30 */

setruid(ruid)
	int ruid;
{

	return (setreuid(ruid, -1));
}
