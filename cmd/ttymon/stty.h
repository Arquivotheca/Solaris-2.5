/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)stty.h	1.8	95/04/13 SMI"	/* SVr4.0 1.3	*/

#define ASYNC   1
#define FLOW	2
#define MAX_CC	NCCS-1	/* max number of ctrl char fields printed by stty -g */
#define NUM_MODES 4	/* number of modes printed by stty -g */
#define NUM_FIELDS NUM_MODES+MAX_CC /* num modes + ctrl char fields (stty -g) */
#define WINDOW	4
#define TERMIOS 8
#ifdef EUC
#define EUCW	16
#endif EUC

struct	speeds {
	const char	*string;
	int	speed;
};

struct mds {
	const char	*string;
	long	set;
	long	reset;
};

