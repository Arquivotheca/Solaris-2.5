/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)utils.c	1.6	95/09/22 SMI"

/*
 *  Supporting routines.
 */
#include "montbl.h"
#include <unistd.h>

#define	B_SIZE	1024

static struct lconv	mon;
static char	*null = "";

int
output(int fd)
{
	int		offset = 0;
	int		length;
	char	buf[B_SIZE];

	if (mon.int_curr_symbol == NULL) {
		mon.int_curr_symbol = null;
	}
	if (mon.currency_symbol == NULL) {
		mon.currency_symbol = null;
	}
	if (mon.mon_decimal_point == NULL) {
		mon.mon_decimal_point = null;
	}
	if (mon.mon_thousands_sep == NULL) {
		mon.mon_thousands_sep = null;
	}
	if (mon.mon_grouping == NULL) {
		mon.mon_grouping = null;
	}
	if (mon.positive_sign == NULL) {
		mon.positive_sign = null;
	}
	if (mon.negative_sign == NULL) {
		mon.negative_sign = null;
	}

#ifdef DDEBUG
	dump_lconv(&mon);
#endif

	(void) strcpy(buf, mon.int_curr_symbol);
	length = strlen(mon.int_curr_symbol) + 1;
	mon.int_curr_symbol = 0;
	offset += length;

	(void) strcpy(buf+offset, mon.currency_symbol);
	length = strlen(mon.currency_symbol) + 1;
	mon.currency_symbol = (char *)offset;
	offset += length;

	(void) strcpy(buf+offset, mon.mon_decimal_point);
	length = strlen(mon.mon_decimal_point) + 1;
	mon.mon_decimal_point = (char *)offset;
	offset += length;

	(void) strcpy(buf+offset, mon.mon_thousands_sep);
	length = strlen(mon.mon_thousands_sep) + 1;
	mon.mon_thousands_sep = (char *)offset;
	offset += length;

	(void) strcpy(buf+offset, mon.mon_grouping);
	length = strlen(mon.mon_grouping) + 1;
	mon.mon_grouping = (char *)offset;
	offset += length;

	(void) strcpy(buf+offset, mon.positive_sign);
	length = strlen(mon.positive_sign) + 1;
	mon.positive_sign = (char *)offset;
	offset += length;

	(void) strcpy(buf+offset, mon.negative_sign);
	length = strlen(mon.negative_sign) + 1;
	mon.negative_sign = (char *)offset;
	offset += length;

	if (write(fd, (char *)&mon, sizeof (struct lconv)) !=
	    sizeof (struct lconv)) {
		(void) fprintf(stderr, "WRITE error\n");
		return (-1);
		/* NOTREACHED */
	}

	if (write(fd, buf, offset) != offset) {
		(void) fprintf(stderr, "WRITE error\n");
		return (-1);
		/* NOTREACHED */
	}
	return (0);
}

int
set_string(int type, struct encoded_val *en)
{
	char	*str;

	if (en->length != 0) {
		int	i;

		str = malloc(en->length + 1);
		if (str == NULL) {
			(void) fprintf(stderr, gettext(
				"Could not allocate memory.\n"));
			return (-1);
		}
		for (i = 0; i < en->length; i++) {
			str[i] = en->bytes[i];
		}
		str[i] = 0;
	}

	switch (type) {
	case T_INT_CURR_SYMBOL:
		if (mon.int_curr_symbol != NULL) {
			free(mon.int_curr_symbol);
		}
		mon.int_curr_symbol = str;
		break;
	case T_CURRENCY_SYMBOL:
		if (mon.currency_symbol != NULL) {
			free(mon.currency_symbol);
		}
		mon.currency_symbol = str;
		break;
	case T_MON_DECIMAL_POINT:
		if (mon.mon_decimal_point != NULL) {
			free(mon.mon_decimal_point);
		}
		mon.mon_decimal_point = str;
		break;
	case T_MON_THOUSANDS_SEP:
		if (mon.mon_thousands_sep != NULL) {
			free(mon.mon_thousands_sep);
		}
		mon.mon_thousands_sep = str;
		break;
	case T_POSITIVE_SIGN:
		if (mon.positive_sign != NULL) {
			free(mon.positive_sign);
		}
		mon.positive_sign = str;
		break;
	case T_NEGATIVE_SIGN:
		if (mon.negative_sign != NULL) {
			free(mon.negative_sign);
		}
		mon.negative_sign = str;
		break;
	default:
		/* Should never happen */
		return (-2);
		/* NOTREACHED */
		break;
	}
	return (0);
}

int
set_char(int type, int val)
{
	char	c_val;

	c_val = val;
	switch (type) {
	case T_INT_FRAC_DIGITS:
		mon.int_frac_digits = c_val;
		break;
	case T_FRAC_DIGITS:
		mon.frac_digits = c_val;
		break;
	case T_P_CS_PRECEDES:
		mon.p_cs_precedes = c_val;
		break;
	case T_P_SEP_BY_SPACE:
		mon.p_sep_by_space = c_val;
		break;
	case T_N_CS_PRECEDES:
		mon.n_cs_precedes = c_val;
		break;
	case T_N_SEP_BY_SPACE:
		mon.n_sep_by_space = c_val;
		break;
	case T_P_SIGN_POSN:
		mon.p_sign_posn = c_val;
		break;
	case T_N_SIGN_POSN:
		mon.n_sign_posn = c_val;
		break;
	default:
		/* Should never happen */
		return (-2);
		/* NOTREACHED */
		break;
	}
	return (0);
}

int
set_group(int val, int *lval)
{
	char	*str;
	int		i;
	if (mon.mon_grouping != NULL) {
		free(mon.mon_grouping);
	}
	mon.mon_grouping = str = malloc(2 * val);
	if (str == NULL) {
		(void) fprintf(stderr, gettext(
			"Could not allocate memory.\n"));
		return (-1);
		/* NOTREACHED */
	}
	for (i = 0; i < val; i++) {
		*str++ = *lval++;
	}
	*str = 0;
	return (0);
}

#ifdef DEBUG
/*
 * DEBUG
 */
int
dump_lconv(struct lconv *l)
{
	printf(" *int_curr_symbol = %s\n", l->int_curr_symbol);
	printf(" *currency_symbol = %s\n", l->currency_symbol);
	printf(" *mon_decimal_point = %s\n", l->mon_decimal_point);
	printf(" *mon_thousands_sep = %s\n", l->mon_thousands_sep);
	printf(" *mon_grouping = %s\n", l->mon_grouping);
	printf(" *positive_sign = %s\n", l->positive_sign);
	printf(" *negative_sign = %s\n", l->negative_sign);
	printf(" int_frac_digits = %d\n", l->int_frac_digits);
	printf(" frac_digits = %d\n", l->frac_digits);
	printf(" p_cs_precedes = %d\n", l->p_cs_precedes);
	printf(" p_sep_by_space = %d\n", l->p_sep_by_space);
	printf(" n_cs_precedes = %d\n", l->p_cs_precedes);
	printf(" n_sep_by_space = %d\n", l->n_sep_by_space);
	printf(" p_sign_posn = %d\n", l->p_sign_posn);
	printf(" n_sign_posn = %d\n", l->n_sign_posn);
}
#endif
