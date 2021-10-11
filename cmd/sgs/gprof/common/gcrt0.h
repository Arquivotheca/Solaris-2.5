/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)gcrt0.h	1.2	93/06/07 SMI"

struct phdr {
    char	*lpc;
    char	*hpc;
    int		ncnt;
};

struct tostruct {
    char		*selfpc;
    long		count;
    unsigned short	link;
};

    /*
     *	a raw arc,
     *	    with pointers to the calling site and the called site
     *	    and a count.
     */
struct rawarc {
    unsigned long	raw_frompc;
    unsigned long	raw_selfpc;
    long		raw_count;
};
