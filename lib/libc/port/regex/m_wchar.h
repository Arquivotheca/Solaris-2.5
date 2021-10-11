/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 *
 * This code is MKS code ported to Solaris with minimum modifications so that
 * upgrades from MKS will readily integrate. Avoid modification if possible!
 */
#ident	"@(#)m_wchar.h 1.1	94/10/12 SMI"

/*
 * ISO/IEC 9899: 1990/Add.3: 1993 (E): Wide character header file
 *
 * Copyright 1992, 1993 by Mortice Kern Systems Inc.  All rights reserved.
 *
 * This Software is unpublished, valuable, confidential property of
 * Mortice Kern Systems Inc.  Use is authorized only in accordance
 * with the terms and conditions of the source licence agreement
 * protecting this Software.  Any unauthorized use or disclosure of
 * this Software is strictly prohibited and will result in the
 * termination of the licence agreement.
 *
 * If you have any questions, please consult your supervisor.
 * 
 */


#ifndef __M_M_WCHAR_H__
#define __M_M_WCHAR_H__ 1

/*
 * m_wchar.h:
 *   configuration file for multi-byte vs. single byte enablement
 */

#ifdef FOO
#include <stdio.h>		/* ZZZ */
#include <widec.h>		/* ZZZ */
#endif
#include <wchar.h>
#include <wctype.h>
#include <limits.h>		/* Fetch MB_LEN_MAX */

#ifdef M_I18N_LOCKING_SHIFT
extern char *m_strsanitize (char *);
#else
#define m_strsanitize(str)	(str)
#endif /* M_I18N_LOCKING_SHIFT */


#ifdef	M_I18N_MB

# ifndef MB_LEN_MAX
#  error M_I18N_MB defined; but the local system does not support multibyte
# endif /* MB_LEN_MAX */

#define	MB_BEGIN	if (MB_CUR_MAX > 1) {
#define	MB_ELSE		} else {
#define	MB_END		}

#define	M_MB_L(s)	L##s

#ifndef _WUCHAR_T
#define _WUCHAR_T
/* a typedef to allow single byte distinction between char and uchar 
 * in MKS environment
 */
/* typedef	wchar_t	wuchar_t; ZZZ */
typedef	long	wuchar_t;
/* typedef	long	wchar_t; */
#endif /*_WUCHAR_T*/

extern int	m_fputmbs(FILE* fp, char *mbs, int wid, int prec, int ljust);
extern int	m_fgetws (wchar_t *, 
size_t, 
FILE *);
extern FILE	*m_fwopen (wchar_t *, char *);
extern	wchar_t	*m_wcsdup (wchar_t *);
extern wchar_t	*m_mbstowcsdup (const char *s);
extern char	*m_wcstombsdup (const wchar_t *w);
extern char	*m_mbschr (const char *, int);
extern char	*m_mbsrchr (const char *, int);
extern char	*m_mbspbrk (const char *, const char *);
extern wchar_t	*m_wcsset (wchar_t *, wchar_t, size_t);
extern int	m_mbswidth (const char *, size_t);
extern int	m_mbsrwidth (const char *, size_t, mbstate_t *);
extern int	iswabsname (wchar_t *);

#define m_smalls(s) (s)
#define wctomb_init() wctomb(NULL,0)

#else	/* MB_LEN_MAX == 1   --  No Multibyte chars! */

/* include <stdlib.h> here,
 * We must include the multibyte function prototypes (in <stdlib.h>) before
 * redefining the prototype function names below.
 *
 * AND including <stdlib.h> DOES NOT cause a problem with wchar_t.
 *
 * ANSI says that the typedef of wchar_t should be defined in stdlib.h.
 * Thus, the prototypes in stdlib.h are declared using stdlib's definition
 * of wchar_t.  That's ok because we are now in "singlebyte" mode and these
 * multibype prototypes should NEVER be called from MKS "singlebyte" code
 * Thus, we will never get a conflict even though we redefine 
 * wchar_t below.
 */

#include <stdlib.h> 	/* DO NOT MOVE THIS include - THIS must be first */
#undef	m_fgetws
#undef	m_fwopen
#undef	m_wcsdup
#undef	m_mbstowcsdup
#undef	m_wcstombsdup
#undef	m_mbschr
#undef	m_mbsrchr
#undef	m_mbspbrk
#undef	m_wcsset
#undef	m_mbswidth
#undef	m_mbsrwidth
#undef	iswabsname
#undef	m_fputmbs

#define	m_fgetws	m_fgets
#define	m_fwopen	fopen
#define	m_wcsdup	strdup
#define	m_mbstowcsdup	strdup
#define	m_wcstombsdup	strdup
#define	m_mbschr	strchr
#define	m_mbsrchr	strrchr
#define	m_mbspbrk	strpbrk
#define	m_wcsset	memset
#define	m_mbswidth(s, n)	strlen(s)
#define	m_mbsrwidth(s, n, st)	strlen(s)
#define	iswabsname(s)	isabsname(s)

#define	m_fputmbs(fp, str, wid, prec, ljust) \
	fprintf((fp), (ljust) ? "%-*.*s" : "%*.*s", wid, prec, str)


#define	MB_BEGIN	if (0) {
#define	MB_ELSE		} else {
#define	MB_END		}

#define	M_MB_L(s)	s

/*
 * Types and Macros
 */
#undef WEOF
#undef wint_t
#undef wuchar_t
#undef wchar_t

#define	WEOF	EOF
#define	wchar_t	char		/* ensures we never use the wchar_t typedef */
#define	wint_t	int		/* type as large as either wchar_t or WEOF */
#define	wuchar_t unsigned char 		/* Force override of typedef */

/*
 * Must define _WCHAR_T, _WINT_T and _WUCHAR_T to avoid typedefs collisions
 * in other system headers.
 * Most systems do something like this:
 *    #ifndef _WCHAR_T
 *      #define _WCHAR_T
 *      typedef unsigned short wchar_t
 *    #endif
 * in their system headers to avoid multiple declarations of wchar_t
 */
#undef _WCHAR_T
#undef _WINT_T
#undef _WUCHAR_T
#define _WCHAR_T
#define _WINT_T
#define _WUCHAR_T
/*
 * Input/Output
 */
#undef	fgetwc
#undef	getwc
#undef	getwchar
#undef	fputwc
#undef	putwc
#undef	putwchar
#undef	fputws
#undef	puts
#undef	fgetwx
#undef	getws
#undef	ungetwc
#undef	fwprintf
#undef	fwscanf
#undef	wprintf
#undef	wscanf
#undef	swscanf
#undef	vfwprintf
#undef	vwprintf
#undef	vswprintf

#define	fgetwc		fgetc
#define	getwc		getc
#define	getwchar	getchar
#define	fputwc		fputc
#define	putwc		putc
#define	putwchar	putchar
#define	fputws		fputs
#define	puts		puts
#define	fgetws		fgets
#define	getws		gets
#define	ungetwc		ungetc
#define	fwprintf	fprintf
#define	fwscanf		fscanf
#define	wprintf		printf
#define	wscanf		scanf
#define	swscanf		sscanf
#define	vfwprintf	vfprintf
#define	vwprintf	vprintf
#define	vswprintf(w,n,f,v)	vsprintf((char*)w,(const char*)f, v)

#ifndef m_smalls
extern wchar_t *m_smalls (wchar_t *);
#endif

/*
 * General Utilities
 */
#undef wcstod
#undef wcstol
#undef wcstoul
#undef wctomb_init

#define	wcstod		strtod
#define	wcstol		strtol
#define	wcstoul		strtoul
#define wctomb_init()   (0)	 /* No state dependency for nonmultibyte. */

/*
 * Wide string handling
 */
#undef	wcscpy
#undef	wcsncpy
#undef	wcscat
#undef	wcsncat
#undef	wcscoll
#undef	wcscmp
#undef	wcsncmp
#undef	wcsxfrm
#undef	wcschr
#undef	wcscspn
#undef	wcspbrk
#undef	wcsrchr
#undef	wcsspn
#undef	wcsstr
#undef	wcstok
#undef	wcslen
#undef	wcswidth
#undef	wcwidth

#define	wcscpy		strcpy
#define	wcsncpy		strncpy
#define	wcscat		strcat
#define	wcsncat		strncat
#define	wcscoll		strcoll
#define	wcscmp		strcmp
#define	wcsncmp		strncmp
#define	wcsxfrm		strxfrm
#define	wcschr		strchr
#define	wcscspn		strcspn
#define	wcspbrk		strpbrk
#define	wcsrchr		strrchr
#define	wcsspn		strspn
#define	wcsstr		strstr
#define	wcstok(x, y, z)	strtok(x, y)
#define	wcslen		strlen
#define	wcswidth(s1, n)		strlen(s1)	/* Need a strnlen? */
#define	wcwidth(c)		1

/*
 * Date and time
 */
#undef wcsftime
#define	wcsftime	strftime

/*
 * Extended Multibyte functions
 */
#undef	wctob
#undef	sisinit
#undef	mbrlen
#undef	wcrtomb
#undef	wctomb
#undef	mbrtowc
#undef	mbtowc
#undef	mbstowcs
#undef	wcstombs
#undef	mbsrtowcs
#undef	wcsrtombs

#define	wctob(c)		(c)
#define	sisinit(p)		(1)	/* Always in initial state */
#define	mbrlen(s, n, ps)	((s) == NULL ? 0 : ((n)==0||*(s)=='\0') ? 0 : 1)
#define	wcrtomb(mb, wc, st)	(*(char *)(mb) = (wchar_t)(wc), 1)
#define wctomb(mb, wc)		(*(char *)(mb) = (wchar_t)(wc), 1)
#define	mbrtowc(wc, mb, n, st)	((n)>0 ? ((*(wc) = *(uchar *)(mb)) != '\0') : 0)
#define	mbtowc(wc, mb, n)	((n)>0 ? ((*(wc) = *(uchar *)(mb)) != '\0') : 0)

#define mbstowcs(wc, mb, n)	((n)>0 ? (strncpy(wc, mb, n), strlen(mb)) : 0)
#define wcstombs(mb, wc, n)	((n)>0 ? (strncpy(mb, wc, n), strlen(wc)) : 0)

#define mbsrtowcs(wc, mbp, n, ps)	((wc)!=NULL ? strcpy(wc, *mbp) : 0, strlen(*mbp))
#define wcsrtombs(mb, wcp, n, ps) 	((mb)!=NULL ? strcpy(mb, *wcp) : 0, strlen(*wcp))

/*
 * convert definitions from <wctype.h>
 */
#undef	iswalnum
#undef	iswalpha
#undef	iswcntrl
#undef	iswdigit
#undef	iswgraph
#undef	iswlower
#undef	iswprint
#undef	iswpunct
#undef	iswspace
#undef	iswupper
#undef	iswxdigit
#undef	iswblank
#undef	towlower
#undef	towupper

#define	iswalnum(c)	isalnum(c)
#define	iswalpha(c)	isalpha(c)
#define	iswcntrl(c)	iscntrl(c)
#define	iswdigit(c)	isdigit(c)
#define	iswgraph(c)	isgraph(c)
#define	iswlower(c)	islower(c)
#define	iswprint(c)	isprint(c)
#define	iswpunct(c)	ispunct(c)
#define	iswspace(c)	isspace(c)
#define	iswupper(c)	isupper(c)
#define	iswxdigit(c)	isxdigit(c)
#define	iswblank(c)	isblank(c)
#define	towlower(c)	tolower(c)
#define	towupper(c)	toupper(c)

/* ? */
extern wctype_t	wctype(const char *__property);
extern int	iswctype(wint_t, wctype_t __wc_property);


/*
 * .2 Functions
 */
#include <fnmatch.h>
#undef fnwwmatch
#undef fnwnmatch
#define	fnwwmatch	fnmatch
#define	fnwnmatch	fnmatch

#include <regex.h>
#undef regwcomp
#undef regwexec
#undef regwdosub
#undef regwdosuba
#undef regwmatch_t

#define regwcomp	regcomp
#define regwexec	regexec
#define regwdosub	regdosub
#define regwdosuba	regdosuba
#define regwmatch_t	regmatch_t

#endif	/* MB_LEN_MAX was 1 */

#endif /*__M_M_WCHAR_H__*/ 
