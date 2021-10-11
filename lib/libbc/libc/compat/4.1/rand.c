#pragma ident	"@(#)rand.c	1.3	92/07/21 SMI"
	  /* from UCB 4.1 80/12/21 */

static	long	randx = 1;

srand(x)
unsigned x;
{
	randx = x;
}

rand()
{
	return((randx = randx * 1103515245 + 12345) & 0x7fffffff);
}
