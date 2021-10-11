/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)utils.c	1.5	95/09/22 SMI"

#include "numeric.h"
#include <unistd.h>

#define	B_SIZE	128

static char	decimal_point[B_SIZE];
static char	thousands_sep[B_SIZE];
static char	grouping[B_SIZE];
static int	group_size = 0;

/*
 *  Supporting routines.
 */

/*
 * The current doprnt(), doscan() and localeconv()
 * expects decimal_point and thousands_sep to be
 * a byte character. Let me just just a byte for
 * here too. Later, when we decide to correct this
 * limitation, fix some of the following codes.
 * (seizo:May/10/1994)
 */

int
output(int fd)
{
	if (decimal_point[0] == 0) {
		decimal_point[0] = '.';
	}
	if (thousands_sep[0] == 0) {
		thousands_sep[0] = ',';
	}
	if (write(fd, decimal_point, 1) != 1) {
		return (-1);
	}
	if (write(fd, thousands_sep, 1) != 1) {
		return (-1);
	}
	if (group_size != 0) {
		if (write(fd, grouping, group_size) != group_size) {
			return (-1);
		}
	}
	return (0);
}

int
set_string(int type, struct encoded_val *en)
{
	char	*str;

	switch (type) {
	case T_DECIMAL_POINT:
		str = decimal_point;
		break;
	case T_THOUSANDS_SEP:
		str = thousands_sep;
		break;
	default:
		(void) fprintf(stderr, gettext(
			"Internal error\n"));
		return (-1);
		/* NOTREACHED */
	}
	*str = en->bytes[0];
	return (0);
}

int
set_group(int val, int *lval)
{
	char	*str;
	int		i;

	str = grouping;
	group_size = 0;
	if (str == NULL) {
		(void) fprintf(stderr, gettext(
			"Could not allocate memory.\n"));
		return (-1);
		/* NOTREACHED */
	}
	for (i = 0; i < val; i++) {
		*str++ = *lval++;
		group_size ++;
	}
	*str = '\0';
	return (0);
}
