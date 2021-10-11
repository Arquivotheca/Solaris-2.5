/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef	_REGEXPR_H
#define	_REGEXPR_H

#pragma ident	"@(#)myregexpr.h	1.5	94/02/03 SMI"
		/* SVr4.0 1.4	*/

extern char	**braslist;	/* start of \(,\) pair */
extern char	**braelist;	/* end of \(,\) pair */
extern int	nbra;		/* number of \(,\) pairs */
extern int	regerrno;	/* for compile() errors */
extern char	*loc1;
extern char	*loc2;
extern char	*locs;
#ifdef __STDC__
extern int step(const char *string, const char *expbuf);
extern int advance(const char *string, const char *expbuf);
extern char *compile(char *instring, char *expbuf, const char *endbuf);
#else
extern int step();
extern int advance();
extern char *compile();
#endif /* __STDC__ */

#endif /* _REGEXPR_H */
