#pragma ident	"@(#)rand.c	1.6	92/07/20 SMI"  /* from S5R2 1.2 */

/*LINTLIBRARY*/

static long randx=1;

void
srand(x)
unsigned x;
{
	randx = x;
}

int
rand()
{
	return(((randx = randx * 1103515245L + 12345)>>16) & 0x7fff);
}
