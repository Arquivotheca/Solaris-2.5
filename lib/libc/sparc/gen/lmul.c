/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)lmul.c	1.6	92/07/14 SMI"	/* SVr4.0 1.2	*/
#ifdef __STDC__
	#pragma weak lmul = _lmul
#endif
#include	"synonyms.h"
#include	"sys/types.h"
#include	"sys/dl.h"

#ifdef __STDC__
const dl_t	_dl_zero;
#else
dl_t	_dl_zero;
#endif

dl_t
lmul(lop, rop)
dl_t	lop;
dl_t	rop;
{
	dl_t		ans;
	dl_t		tmp;
	register int	ii;
	register int	jj;

	ans = _dl_zero;

	for(jj = 0 ; jj <= 63  ; jj++){
		if((lshiftl(rop, -jj).dl_lop & 1) == 0)
			continue;
		tmp = lshiftl(lop, jj);
		tmp.dl_hop &= 0x7fffffff;
		ans = ladd(ans, tmp);
	};

	return(ans);
}
