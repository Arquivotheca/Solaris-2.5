#pragma ident	"@(#)valloc.c	1.3	92/07/21 SMI"
	  /* from UCB 4.3 83/07/01 */

extern	unsigned getpagesize();
extern	char	*memalign();

char *
valloc(size)
	unsigned size;
{
	static unsigned pagesize;
	if (!pagesize)
		pagesize = getpagesize();
	return memalign(pagesize, size);
}
