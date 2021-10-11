#pragma ident	"@(#)mkepoch.c	1.8	92/07/20 SMI" 

/*
 * Copyright (c) 1986 Sun Microsystems Inc.
 */

/*
 * Put out a C declaration which initializes "epoch" to the time ("tv_sec"
 * portion) when this program was run.
 */

#include <stdio.h>
#include <sys/time.h>

int	gettimeofday();
void	perror();
void	exit();

/*ARGSUSED*/
int
main(argc, argv)
	int argc;
	char **argv;
{
	struct timeval now;

	if (gettimeofday(&now, (struct timezone *)NULL) < 0) {
		perror("mkepoch: gettimeofday failed");
		exit(1);
	}

	if (printf("static long epoch = %ld;\n", now.tv_sec) == EOF) {
		perror("mkepoch: can't write output");
		exit(1);
	}

	return (0);
}
