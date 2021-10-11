/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)psfile.c	1.5	94/03/31 SMI"	/* SVr4.0 1.1	*/

#include <fcntl.h>

#define	PSCOM	"%!"
#define	PC_PSCOM	"%!"

#if defined(__STDC__)
psfile(char * fname)
#else
psfile(fname)
char	*fname;
#endif
{
	int		fd;
	register int	ret = 0;
	char		buf[sizeof(PC_PSCOM)-1];

	if ((fd = open(fname, O_RDONLY)) >= 0 &&
    	    read(fd, buf, sizeof(buf)) == sizeof(buf) &&
    	    ((strncmp(buf, PSCOM, sizeof(PSCOM)-1) == 0) ||
    	     (strncmp(buf, PC_PSCOM, sizeof(PC_PSCOM)-1) == 0)))
			ret++;
	(void)close(fd);
	return(ret);
}
