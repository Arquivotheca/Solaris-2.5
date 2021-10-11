/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)chrtbl.h	1.10	95/03/08 SMI"

#include "../head/_localedef.h"
#include "../head/charmap.h"
#include "../head/_collate.h"
#include <wctype.h>

/*
 * character classfication
 */
#define	T_UPPER		1
#define	T_LOWER		2
#define	T_ALPHA		3
#define	T_DIGIT		4
#define	T_SPACE		5
#define	T_CNTRL		6
#define	T_PUNCT		7
#define	T_GRAPH		8
#define	T_PRINT		9
#define	T_XDIGIT	10
#define	T_BLANK		11
#define	T_TOUPPER	12
#define	T_TOLOWER	13
#define	T_CODE1		14
#define	T_CODE2		15
#define	T_CODE3		16
#define	T_ISWCHAR1	17
#define	T_ISWCHAR2	18
#define	T_ISWCHAR3	19
#define	T_ISWCHAR4	20
#define	T_ISWCHAR5	21
#define	T_ISWCHAR6	22
#define	T_ISWCHAR7	23
#define	T_ISWCHAR8	24
#define	T_ISWCHAR9	25
#define	T_ISWCHAR10	26
#define	T_ISWCHAR11	27
#define	T_ISWCHAR12	28
#define	T_ISWCHAR13	29
#define	T_ISWCHAR14	30
#define	T_ISWCHAR15	31
#define	T_ISWCHAR16	32
#define	T_ISWCHAR17	33
#define	T_ISWCHAR18	34
#define	T_ISWCHAR19	35
#define	T_ISWCHAR20	36
#define	T_ISWCHAR21	37
#define	T_ISWCHAR22	38
#define	T_ISWCHAR23	39
#define	T_ISWCHAR24	40

/*
 * From chrtbl.c, Solaris.2.3
 */
#define	HEX			1
#define	OCTAL		2
#define	RANGE		1
#define	UL_CONV		2
#define	CSLEN		10
#define	SIZE		(2 * 257 + CSLEN) 	/* SIZE must be multiple of 4 */
#define	START_CSWIDTH	(2 * 257)
#define	START_NUMERIC	((2 * 257) + 7)
#define	ISUPPER		0x00000001
#define	ISLOWER		0x00000002
#define	ISDIGIT		0x00000004
#define	ISSPACE		0x00000008
#define	ISPUNCT		0x00000010
#define	ISCNTRL		0x00000020
#define	ISBLANK		0x00000040
#define	ISXDIGIT	0x00000080

#define	UL			0xff
#define	ISWCHAR1	0x00000100	/* phonogram (international use) */
#define	ISWCHAR2	0x00000200	/* ideogram (international use) */
#define	ISWCHAR3	0x00000400	/* English (international use) */
#define	ISWCHAR4	0x00000800	/* number (international use) */
#define	ISWCHAR5	0x00001000	/* special (international use) */
#define	ISWCHAR6	0x00002000	/* reserved (international use) */
#define	ISGRAPH		0x00002000	/* NOW USED */
#define	ISWCHAR7	0x00004000	/* reserved (international use) */
#define	ISALPHA		0x00004000	/* NOW USED */
#define	ISWCHAR8	0x00008000	/* reserved (international use) */
#define	ISPRNT		0x00008000	/* NOW USED */
#define	ISWCHAR9	0x00010000
#define	ISWCHAR10	0x00020000
#define	ISWCHAR11	0x00040000
#define	ISWCHAR12	0x00080000
#define	ISWCHAR13	0x00100000
#define	ISWCHAR14	0x00200000
#define	ISWCHAR15	0x00400000
#define	ISWCHAR16	0x00800000
#define	ISWCHAR17	0x01000000
#define	ISWCHAR18	0x02000000
#define	ISWCHAR19	0x04000000
#define	ISWCHAR20	0x08000000
#define	ISWCHAR21	0x10000000
#define	ISWCHAR22	0x20000000
#define	ISWCHAR23	0x40000000
#define	ISWCHAR24	0x80000000

#define	CSWIDTH			11
#define	DECIMAL_POINT	14
#define	THOUSANDS_SEP	15

#define	LC_CTYPE1	21
#define	LC_CTYPE2	22
#define	LC_CTYPE3	23
#define	WSIZE		(0xffff * 4)
#define	NUM_BANKS	20

#define	ON	1
#define	OFF	0

struct	classname {
	char	*name;
	int		num;
	char	*repres;
};
