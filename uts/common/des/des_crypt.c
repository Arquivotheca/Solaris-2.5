/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)des_crypt.c	1.10	94/03/10 SMI"	/* SVr4.0 1.5	*/

/*
 *  		PROPRIETARY NOTICE (Combined)
 *  
 *  This source code is unpublished proprietary information
 *  constituting, or derived under license from AT&T's Unix(r) System V.
 *  In addition, portions of such source code were derived from Berkeley
 *  4.3 BSD under license from the Regents of the University of
 *  California.
 *  
 *  
 *  
 *  		Copyright Notice 
 *  
 *  Notice of copyright on this source code product does not indicate 
 *  publication.
 *  
 *  	(c) 1986,1987,1988,1989  Sun Microsystems, Inc.
 *  	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *  	          All rights reserved.
 */

/*
 * des_crypt.c, DES encryption library routines
 */



/*   
 * This is the loadable module wrapper.    
 */    
#include <sys/errno.h>
#include <sys/modctl.h>

extern struct mod_ops mod_miscops;

/*
 * Module linkage information for the kernel.
 */
static struct modlmisc modlmisc = {
	&mod_miscops,
	"des encryption",
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlmisc, 0
};

_init()
{     
	return (mod_install(&modlinkage));
} 
 
_info(modinfop)
	struct modinfo *modinfop;
{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * Copy 8 bytes
 */
#define COPY8(src, dst) { \
	register char *a = (char *) dst; \
	register char *b = (char *) src; \
	*a++ = *b++; *a++ = *b++; *a++ = *b++; *a++ = *b++; \
	*a++ = *b++; *a++ = *b++; *a++ = *b++; *a++ = *b++; \
}
 
/*
 * Copy multiple of 8 bytes
 */
#define DESCOPY(src, dst, len) { \
	register char *a = (char *) dst; \
	register char *b = (char *) src; \
	register int i; \
	for (i = (int) len; i > 0; i -= 8) { \
		*a++ = *b++; *a++ = *b++; *a++ = *b++; *a++ = *b++; \
		*a++ = *b++; *a++ = *b++; *a++ = *b++; *a++ = *b++; \
	} \
}

/*
 * CBC mode encryption
 */
cbc_crypt(key, buf, len, mode, ivec)
	char *key;
	char *buf;
	unsigned len;
	unsigned mode;
	char *ivec;	
{
}


/*
 * ECB mode encryption
 */
ecb_crypt(key, buf, len, mode)
	char *key;
	char *buf;
	unsigned len;
	unsigned mode;
{
}



