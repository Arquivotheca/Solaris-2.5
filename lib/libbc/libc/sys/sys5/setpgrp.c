#pragma ident	"@(#)setpgrp.c	1.2	92/07/20 SMI" 

extern int	setsid();

int
setpgrp()
{

	return (setsid());
}
