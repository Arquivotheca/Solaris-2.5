/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 * Copyright 1989, 1994 by Mortice Kern Systems Inc.  All rights reserved.
 */

#ifndef	_REGEX_H
#define	_REGEX_H

#pragma ident	"@(#)regex.h	1.7	95/03/13 SMI"

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif


#ifndef _WCHAR_T
#define	_WCHAR_T
typedef long    wchar_t;
#endif

typedef off_t regoff_t;

/* compiled RE */
typedef struct {
	size_t re_nsub;			/* number of subexpressions in RE */

	/*
	 * Internal
	 */
	int re_flags;			/* copy of regcomp flags */
	struct _regblock *re_block;	/* list of regnode's */
	struct _regnode	*re_node;	/* compiled NFA  */
	wchar_t		*re_regmust;	/* must substring */
	unsigned char	*re_parse;	/* parser position */
} regex_t;

/* subexpression positions */
typedef struct {
	const char *rm_sp, *rm_ep;	/* Start pointer, end pointer */
	regoff_t rm_so, rm_eo;		/* Start offset, end offset */
	int	rm_ss, rm_es; 		/* Used internally */
} regmatch_t;

/* regcomp flags */
#define	REG_EXTENDED	0x01		/* Use Extended Regular Expressions */
#define	REG_NEWLINE	0x08		/* Treat \n as regular character */
#define	REG_ICASE	0x04		/* Ignore case in match */
#define	REG_NOSUB	0x02		/* Don't set subexpression */

/* non-standard flags */
#define	REG_DELIM	0x10		/* string[0] is delimiter */
#define	REG_DEBUG	0x20		/* Debug recomp and regexec */
#define	REG_ANCHOR	0x40		/* Implicit ^ and $ */
#define	REG_WORDS	0x80		/* \< and \> match word boundries */

/* internal flags */
#define	REG_MUST	0x100		/* check for regmust substring */

/* regexec flags */
#define	REG_NOTBOL	0x200		/* string is not BOL */
#define	REG_NOTEOL	0x400		/* string has no EOL */
#define	REG_NOOPT	0x800		/* don't do regmust optimization */

/* regcomp and regexec return codes */
#define	REG_OK		0		/* success (non-standard) */
#define	REG_NOMATCH	1		/* regexec failed to match */
#define	REG_ECOLLATE	2		/* invalid collation element ref. */
#define	REG_EESCAPE	3		/* trailing \ in pattern */
#define	REG_ENEWLINE	4		/* \n found before end of pattern */
#define	REG_ENSUB	5		/* more than 9 \( \) pairs (OBS) */
#define	REG_ESUBREG	6		/* number in \[0-9] invalid */
#define	REG_EBRACK	7		/* [ ] inbalance */
#define	REG_EPAREN	8		/* ( ) inbalance */
#define	REG_EBRACE	9		/* \{ \} inbalance */
#define	REG_ERANGE	10		/* invalid endpoint in range */
#define	REG_ESPACE	11		/* no memory for compiled pattern */
#define	REG_BADRPT	12		/* invalid repetition */
#define	REG_ECTYPE	13		/* invalid char-class type */
#define	REG_BADPAT	14		/* syntax error */
#define	REG_BADBR	15		/* \{ \} contents bad */
#define	REG_EFATAL	16		/* internal error, not POSIX.2 */
#define	REG_ECHAR	17		/* invalid mulitbyte character */
#define	REG_STACK	18		/* backtrack stack overflow */
#define	REG_ENOSYS	19		/* function not supported (XPG4) */
#define	REG__LAST	20		/* first unused code */

/*
 * Additional API and structs to support regular expression manipulations
 * on wide characters.
 */

/* subexpression positions */
typedef struct {
	const wchar_t *rm_sp, *rm_ep;
	regoff_t rm_so, rm_eo;
	int	rm_ss, rm_es;  		/* For internal use */
} regwmatch_t;

#if defined(__STDC__)
extern int regcomp(regex_t *, const char *, int);
extern int regexec(const regex_t *, const char *, size_t, regmatch_t [], int);
extern size_t regerror(int, const regex_t *, char *, size_t);
extern void regfree(regex_t *);

#if (!defined(_XOPEN_SOURCE) && !defined(_POSIX_C_SOURCE)) || \
	defined(__EXTENSIONS__)
/*
 * The following are not POSIX.2 routines
 */
extern int regwcomp(regex_t *, const wchar_t *, int);
extern int regwexec(const regex_t *, const wchar_t *, size_t,
		    regwmatch_t *, int);
extern int regdosub(regex_t *, const char *, const char *, char *, int, int *);
extern int regdosuba(regex_t *, const char *,
			const char *, char **, int, int *);
extern int regwdosub(regex_t *, const wchar_t *,
			const wchar_t *, wchar_t *, int, int *);
extern int regwdosuba(regex_t *, const wchar_t *,
			const wchar_t *, wchar_t **, int, int *);
#endif /* (!defined(_XOPEN_SOURCE) && !defined(_POSIX_C_SOURCE)) ... */

#else  /* defined(__STDC__) */

extern int regcomp();
extern int regexec();
extern size_t regerror();
extern void regfree();

#if (!defined(_XOPEN_SOURCE) && !defined(_POSIX_C_SOURCE)) || \
	defined(__EXTENSIONS__)

extern int regwcomp();
extern int regwexec();
extern int regdosub();
extern int regdosuba();
extern int regwdosub();
extern int regwdosuba();

#endif /*  (!defined(_XOPEN_SOURCE) && !defined(_POSIX_C_SOURCE)) ... */

#endif  /* defined(__STDC__) */

#ifdef	__cplusplus
}
#endif

#endif	/* _REGEX_H */
