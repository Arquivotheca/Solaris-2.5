/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)time.h	1.5	95/03/09 SMI"
#include "../head/_localedef.h"
#include "../head/charmap.h"

/*
 * This is the order to be appeared in the
 * output file.
 */
#define	T_ABMON			0
#define	T_MON			1
#define	T_ABDAY			2
#define	T_DAY			3
#define	T_T_FMT			4
#define	T_D_FMT			5
#define	T_D_T_FMT		6
#define	T_AM_PM			7
#define	T_DATE_FMT		8
#define	T_T_FMT_AMPM	9
#define	T_ERA_D_FMT		10
#define	T_ERA_T_FMT		11
#define	T_ERA_D_T_FMT	12
#define	T_ALT_DIGITS	13
#define	T_ERA			14

#define	MAX_LIST		101
#define	MAX_ERA			100

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
