/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)montbl.h	1.5	95/03/03 SMI"
#include "../head/_localedef.h"
#include "../head/charmap.h"

#define	T_INT_CURR_SYMBOL	10
#define	T_CURRENCY_SYMBOL	20
#define	T_MON_DECIMAL_POINT	30
#define	T_MON_THOUSANDS_SEP	40
#define	T_MON_GROUPING		50
#define	T_POSITIVE_SIGN		60
#define	T_NEGATIVE_SIGN		70
#define	T_INT_FRAC_DIGITS	80
#define	T_FRAC_DIGITS		90
#define	T_P_CS_PRECEDES		100
#define	T_P_SEP_BY_SPACE	110
#define	T_N_SEP_BY_SPACE	120
#define	T_N_CS_PRECEDES		130
#define	T_P_SIGN_POSN		140
#define	T_N_SIGN_POSN		150

/*
 * Malloc macros
 */
#define	MALLOC_ENCODED\
	(encoded_val *)malloc(sizeof (encoded_val))

/*
 * function prototypes
 */
extern encoded_val	*alloc_encoded(encoded_val *);
extern int	set_enbuf(char *);
