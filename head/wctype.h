/*	wctype.h	1.13 89/11/02 SMI; JLE	*/
/*	from AT&T JAE 2.1			*/
/*	definitions for international functions	*/

/*
 * Copyright (c) 1991 Sun Microsystems Inc.
 */

#ifndef	_WCTYPE_H
#define	_WCTYPE_H

#pragma ident	"@(#)wctype.h	1.10	95/01/28 SMI"

#include <ctype.h>
#include <widec.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef __STDC__
extern	unsigned _iswctype(wchar_t, int);
extern	wchar_t _trwctype(wchar_t, int);
#else
extern  unsigned _iswctype();
extern  wchar_t _trwctype();
#endif

/* bit definition for character class */

#define	_E1	0x00000100	/* phonogram (international use) */
#define	_E2	0x00000200	/* ideogram (international use) */
#define	_E3	0x00000400	/* English (international use) */
#define	_E4	0x00000800	/* number (international use) */
#define	_E5	0x00001000	/* special (international use) */
#define	_E6	0x00002000	/* other characters (international use) */
#define	_E7	0x00004000	/* reserved (international use) */
#define	_E8	0x00008000	/* reserved (international use) */

#define	_E9	0x00010000
#define	_E10	0x00020000
#define	_E11	0x00040000
#define	_E12	0x00080000
#define	_E13	0x00100000
#define	_E14	0x00200000
#define	_E15	0x00400000
#define	_E16	0x00800000
#define	_E17	0x01000000
#define	_E18	0x02000000
#define	_E19	0x04000000
#define	_E20	0x08000000
#define	_E21	0x10000000
#define	_E22	0x20000000
#define	_E23	0x40000000
#define	_E24	0x80000000

/*
 * data structure for supplementary code set
 * for character class and conversion
 */
struct	_wctype {
	wchar_t	tmin;		/* minimum code for wctype */
	wchar_t	tmax;		/* maximum code for wctype */
	unsigned char  *index;	/* class index */
	unsigned int   *type;	/* class type */
	wchar_t	cmin;		/* minimum code for conversion */
	wchar_t	cmax;		/* maximum code for conversion */
	wchar_t *code;		/* conversion code */
};

/* character classification functions */

/* isw*, except iswascii(), are not macros any more.  They become functions */
#ifdef __STDC__
extern	int iswalpha(wint_t c);
extern	int iswupper(wint_t c);
extern	int iswlower(wint_t c);
extern	int iswdigit(wint_t c);
extern	int iswxdigit(wint_t c);
extern	int iswalnum(wint_t c);
extern	int iswspace(wint_t c);
extern	int iswpunct(wint_t c);
extern	int iswprint(wint_t c);
extern	int iswgraph(wint_t c);
extern	int iswcntrl(wint_t c);

/* iswascii is still a macro */
#define	iswascii(c)	isascii(c)

/* is* also become functions */
extern	int isphonogram(wint_t c);
extern	int isideogram(wint_t c);
extern	int isenglish(wint_t c);
extern	int isnumber(wint_t c);
extern	int isspecial(wint_t c);

/* tow* also become functions */
extern	wint_t towlower(wint_t c);
extern	wint_t towupper(wint_t c);
#else
extern  int iswalpha();
extern  int iswupper();
extern  int iswlower();
extern  int iswdigit();
extern  int iswxdigit();
extern  int iswalnum();
extern  int iswspace();
extern  int iswpunct();
extern  int iswprint();
extern  int iswgraph();
extern  int iswcntrl();

/* iswascii is still a macro */
#define	iswascii(c)	isascii(c)

/* is* also become functions */
extern  int isphonogram();
extern  int isideogram();
extern  int isenglish();
extern  int isnumber();
extern  int isspecial();

/* tow* also become functions */
extern  wint_t towlower();
extern  wint_t towupper();

#endif

#define	iscodeset0(c)	isascii(c)
#define	iscodeset1(c)	(((c) & WCHAR_CSMASK) == WCHAR_CS1)
#define	iscodeset2(c)	(((c) & WCHAR_CSMASK) == WCHAR_CS2)
#define	iscodeset3(c)	(((c) & WCHAR_CSMASK) == WCHAR_CS3)

#ifdef	__cplusplus
}
#endif

#endif	/* _WCTYPE_H */
