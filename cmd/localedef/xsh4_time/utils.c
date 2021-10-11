/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)utils.c	1.11	95/04/07 SMI"

#include "time.h"
#include "extern.h"
#include <unistd.h>

extern int	key_words[];
extern int	c_flag;

static encoded_val	*t_abmon[12];
static encoded_val	*t_mon[12];
static encoded_val	*t_abday[7];
static encoded_val	*t_day[7];
static encoded_val	*t_t_fmt;
static encoded_val	*t_d_fmt;
static encoded_val	*t_d_t_fmt;
static encoded_val	*t_am_pm[2];
static encoded_val	*t_date_fmt;
static encoded_val	*t_t_fmt_ampm;
static encoded_val	*t_era_d_fmt;
static encoded_val	*t_era_t_fmt;
static encoded_val	*t_era_d_t_fmt;
static encoded_val	*t_alt_digits[MAX_LIST];
static encoded_val	*t_era[MAX_ERA];

static encoded_val	def_date_fmt = {
	18,
	"%a %b %e %T %Z %Y"
};

static struct out_info {
	encoded_val	**en; /* holding data */
	int		max_num;	 /* maximun num of list elements. */
	int		num_list;	 /* actual num defined. */
	int		flag;	 /* 1 if required, 0 if optional */
	char	*keyword;	 /* keyword */
} out_info[] = {
	t_abmon, 12, 0, 1, "abmon",
	t_mon, 12, 0, 1, "mon",
	t_abday, 7, 0, 1, "abday",
	t_day, 7, 0, 1, "day",
	&t_t_fmt, 1, 0, 1, "t_fmt",
	&t_d_fmt, 1, 0, 1, "d_fmt",
	&t_d_t_fmt, 1, 0, 1, "d_t_fmt",
	t_am_pm, 2, 0, 1, "am_pm",
	&t_date_fmt, 1, 0, 0, "date_fmt",
	&t_t_fmt_ampm, 1, 0, 1, "t_fmt_ampm",
	&t_era_t_fmt, 1, 0, 0, "era_t_fmt",
	&t_era_d_fmt, 1, 0, 0, "era_d_fmt",
	&t_era_d_t_fmt, 1, 0, 0, "era_d_t_fmt",
	t_alt_digits, MAX_LIST, 0, 0, "alt_digits",
	t_era, MAX_ERA, 0, 0, "era",
	0, 0, 0, -1, (char *)0
};

static unsigned char	*get_buffer(unsigned char *, unsigned char *);
static int		chk_adj_table();
static int		out_file(int, struct out_info *);
static int		check_era(unsigned char *);

extern int		bytescopy(unsigned char *, unsigned char *, int);
extern int		get_encoded_string(unsigned char *, encoded_val *);
extern int		set_encoded_string(unsigned char *, encoded_val *);
extern int		expand_sym_string(unsigned char *, unsigned char *);
/*
 *  Supporting routines.
 */
int
output(int fd)
{
	int		ret;
	int		i;
	char	buf[128];

#ifdef DDEBUG
	dump_outinfo("DUMPING OUT_INFO table.");
#endif

	ret = chk_adj_table();
	if (ret != 0) {

#ifdef DEBUG
		printf("CHK_ADJ_TABLE returned %d\n", ret);
#endif

		return (ret);
		/* NOTREACHED */
	}

	if (c_flag) {
		(void) strcpy(buf, "#\n# TIME TABLE\n#\n");
		(void) write(fd, buf, strlen(buf));
	}
	for (i = 0; i <= T_ERA; i++) {
		if (c_flag) {
			switch (i) {
			case T_ABMON:
				(void) strcpy(buf, "#\n# ABMON\n#\n");
				break;
			case T_MON:
				(void) strcpy(buf, "#\n# MON\n#\n");
				break;
			case T_ABDAY:
				(void) strcpy(buf, "#\n# ABDAY\n#\n");
				break;
			case T_DAY:
				(void) strcpy(buf, "#\n# DAY\n#\n");
				break;
			case T_T_FMT:
				(void) strcpy(buf, "#\n# T_FMT\n#\n");
				break;
			case T_D_FMT:
				(void) strcpy(buf, "#\n# D_FMT\n#\n");
				break;
			case T_D_T_FMT:
				(void) strcpy(buf, "#\n# D_T_FMT\n#\n");
				break;
			case T_AM_PM:
				(void) strcpy(buf, "#\n# AMPM\n#\n");
				break;
			case T_DATE_FMT:
				(void) strcpy(buf, "#\n# DATE_FMT\n#\n");
				break;
			case T_T_FMT_AMPM:
				(void) strcpy(buf, "#\n# T_FMT_AMPM\n#\n");
				break;
			case T_ERA_D_FMT:
				(void) strcpy(buf, "#\n# ERA_D_FMT\n#\n");
				break;
			case T_ERA_T_FMT:
				(void) strcpy(buf, "#\n# ERA_T_FMT\n#\n");
				break;
			case T_ERA_D_T_FMT:
				(void) strcpy(buf, "#\n# ERA_D_T_FMT\n#\n");
				break;
			default:
				goto out;
				/* NOTREACHED */
			}
			(void) write(fd, buf, strlen(buf));
		}
out:
		if (i == T_ALT_DIGITS) {
			(void) sprintf(buf, "NUM_ALT_DIGITS=%d\n",
				out_info[i].num_list);
			(void) write(fd, buf, strlen(buf));
		} else if (i == T_ERA) {
			(void) sprintf(buf, "NUM_ERA=%d\n",
				out_info[i].num_list);
			(void) write(fd, buf, strlen(buf));
		}
		ret = out_file(fd, &out_info[i]);
		if (ret != 0) {
			return (-1);
			/* NOTREACHED */
		}
	}
	return (0);
}

static int
out_file(int fd, struct out_info *o)
{
	char	buf[MAX_BYTES+1];
	int		length;
	int		ret;
	int		i;

	for (i = 0; i < o->num_list; i++) {
		length = o->en[i]->length;
		(void) bytescopy((unsigned char *)buf, o->en[i]->bytes, length);
		buf[length] = '\n';
		ret = write(fd, buf, length+1);
		if (ret != length+1) {
			return (-1);
			/* NOTREACHED */
		}
	}
	return (0);
}

int
set_table(int type, encoded_val **list, int num)
{
	struct out_info	*info;
	int		lim, i;

	if (type < 0 || type > T_ERA) {
		/*
		 * Internal Error
		 */
		return (-1);
		/* NOTREACHED */
	}
	info = &out_info[type];
	/*
	 * Fill in the table.
	 */
	if (num >= info->max_num) {
		lim = info->max_num;
	} else {
		lim = num;
	}
	for (i = 0; i < lim; i++) {
		info->en[i] = list[i];
	}
	info->num_list = lim;
}

static int
chk_adj_table()
{
	int		i;
	int		ret_val = 0;
	unsigned char	buf[MAX_BYTES + 1];
	unsigned char	buf2[MAX_BYTES + 1];
	int	expand_idx[] = { T_T_FMT,
		T_D_FMT, T_D_T_FMT, T_AM_PM,
		T_DATE_FMT, T_T_FMT_AMPM, T_ERA_D_FMT,
		T_ERA_T_FMT, T_ERA_D_T_FMT, -1};

	/*
	 * Are all required keywords set ?
	 */
	for (i = 0; out_info[i].flag != -1; i++) {
		if (out_info[i].flag == 1 &&
		    out_info[i].max_num != out_info[i].num_list) {
			(void) fprintf(stderr, gettext(
				"keyword %s not properly specified.\n"),
				out_info[i].keyword);
			ret_val = 1;
		} else if (out_info[i].flag == 2 &&
			out_info[i].num_list == 0) {
			out_info[i].num_list = 1;
			t_date_fmt = &def_date_fmt;
		}
	}
	if (ret_val != 0) {
		return (ret_val);
		/* NOTREACHED */
	}

	/*
	 * If date_fmt, era_{d, t, d_t}_fmt are not
	 * specified, copy the default.
	 */
	if (out_info[T_DATE_FMT].num_list == 0) {
		out_info[T_DATE_FMT].num_list = 1;
		t_date_fmt = &def_date_fmt;
	}
	if (out_info[T_ERA_D_FMT].num_list == 0) {
		out_info[T_ERA_D_FMT].num_list = 1;
		out_info[T_ERA_D_FMT].en[0] =
			out_info[T_D_FMT].en[0];
	}
	if (out_info[T_ERA_T_FMT].num_list == 0) {
		out_info[T_ERA_T_FMT].num_list = 1;
		out_info[T_ERA_T_FMT].en[0] =
			out_info[T_T_FMT].en[0];
	}
	if (out_info[T_ERA_D_T_FMT].num_list == 0) {
		out_info[T_ERA_D_T_FMT].num_list = 1;
		out_info[T_ERA_D_T_FMT].en[0] =
			out_info[T_D_T_FMT].en[0];
	}

	/*
	 * Expand strings for
	 *	t_fmt, d_fmt, d_t_fmt, am_pm,
	 *	date_fmt, t_fmp_ampm,
	 *	era_{d, t, d_t}_fmt.
	 */
	i = 0;
	while (expand_idx[i] != -1) {
		(void) get_encoded_string(buf, out_info[expand_idx[i]].en[0]);
		if (expand_sym_string(buf2, buf) == ERROR) {
			ret_val = 3;
			goto out;
			/* NOTREACHED */
		}
		(void) set_encoded_string(buf2, out_info[expand_idx[i]].en[0]);
		i++;
	}

	/*
	 * Syntax check for era value
	 */
	for (i = 0; i < out_info[T_ERA].num_list; i++) {
		(void) get_encoded_string(buf, out_info[T_ERA].en[i]);
		if (expand_sym_string(buf2, buf) == ERROR) {
			ret_val = 3;
			break;
		}
		if (check_era(buf2) != 0) {
			ret_val = 2;
			break;
		}
		(void) set_encoded_string(buf2, out_info[T_ERA].en[i]);
	}
out:
	return (ret_val);
}

static int
check_era(unsigned char *buf)
{
	unsigned char	buffer[MAX_BYTES+1];
	int		ret;
	int		s_year, s_month, s_day;
	int		e_year, e_month, e_day;

	/*
	 * check direction
	 */
	buf = get_buffer(buffer, buf);
	if (buf == NULL) {
		return (-10);
		/* NOTREACHED */
	}
	if ((buffer[0] != '+') && (buffer[0] != '-')) {
		return (-20);
		/* NOTREACHED */
	}
	/*
	 * Check offset
	 */
	buf = get_buffer(buffer, buf);
	if (buf == NULL) {
		return (-30);
		/* NOTREACHED */
	}

	/*
	 * Check start_date
	 */
	buf = get_buffer(buffer, buf);
	if (buf == NULL) {
		return (-40);
		/* NOTREACHED */
	}
	ret = sscanf((char *)buffer, "%d/%d/%d", &s_year, &s_month, &s_day);
	if (ret != 3) {
		return (-51);
		/* NOTREACHED */
	}
	if (s_month < 0 || s_month > 12) {
		return (-53);
		/* NOTREACHED */
	}
	if (s_day < 0 || s_day > 31) {
		return (-54);
		/* NOTREACHED */
	}

#ifdef DEBUG
printf("start (%d/%d/%d)\n", s_year, s_month, s_day);
#endif

	/*
	 * Check end_date
	 */
	buf = get_buffer(buffer, buf);
	if (buf == NULL) {
		return (-60);
		/* NOTREACHED */
	}
	ret = sscanf((char *)buffer, "%d/%d/%d", &e_year, &e_month, &e_day);
	if (ret == 0) {
		goto alternate;
		/* NOTREACHED */
	}
	if (ret != 3) {
		return (-61);
		/* NOTREACHED */
	}
	if (e_month < 0 || e_month > 12) {
		return (-63);
		/* NOTREACHED */
	}
	if (e_day < 0 || e_day > 31) {
		return (-64);
		/* NOTREACHED */
	}
	if (e_year < s_year) {
		return (-65);
		/* NOTREACHED */
	}
	if (e_year == s_year) {
		if (e_month < s_month) {
			return (-66);
			/* NOTREACHED */
		}
		if (e_month == s_month) {
			if (e_day < s_day) {
				return (-67);
				/* NOTREACHED */
			}
		}
	}

#ifdef DEBUG
printf("end (%d/%d/%d)\n", e_year, e_month, e_day);
#endif

	goto check_era_name;

alternate:

	if (strcmp((char *)buffer, "-*") != 0 &&
	    strcmp((char *)buffer, "+*") != 0) {
		return (-68);
		/* NOTREACHED */
	}

#ifdef DEBUG
printf("end (%s)\n", buffer);
#endif

	/*
	 * Check era_name & era_format exist
	 */
check_era_name:
	buf = get_buffer(buffer, buf);
	if (buf == NULL) {
		return (-70);
		/* NOTREACHED */
	}

#ifdef DEBUG
printf("era_name = %s, era_format = %s\n", buffer, buf);
#endif

	return (0);
}

static unsigned char *
get_buffer(unsigned char *to, unsigned char *from)
{
	while (*from != ':' && *from != 0) {
		*to++ = *from++;
	}
	if (*from == ':') {
		from++;
	} else {
		from = NULL;
	}
	*to = 0;
	return (from);
}

#ifdef DEBUG
/*
 * Debugging info.
 */
dump_outinfo(char *s)
{
	int i = 0;

	printf("%s\n", s);
	for (i = 0; out_info[i].keyword != (char *)0; i++) {
		dump_info(&out_info[i]);
	}
}

dump_info(struct out_info *o)
{
	int i;
	printf("KEYWORD = '%s', num_list = %d\n",
		o->keyword,
		o->num_list);
	for (i = 0; i < o->num_list; i++)
		dump_encoded("dumping list", o->en[i]);
}
#endif
