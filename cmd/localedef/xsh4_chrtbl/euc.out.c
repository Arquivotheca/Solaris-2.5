/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)euc.out.c	1.16	95/03/08 SMI"

/*
 *  XPG4, chrtbl command
 */
#include "chrtbl.h"
#include "extern.h"


extern void	GetMinMaxwctyp(int, int *, int *);
extern int	GetNewAddr(int, int);
extern void	GetMinMaxWconv(int, int *, int *);

static void		create0w();
static void		create2();
static void		create2w();
static void		create1();
static void		create1w();
static void		comment1();
static void		comment2();
static void		comment3();
static void		comment4();
static void		comment5();
static void		createw_empty();
static unsigned char	*hextostr();

char			*tablename1, *tablename2;

int
output(char *name)
{
	FILE	*f1;	/* for ctype table */
	FILE	*f2;	/* for source file */

	if ((tablename1 = (char *)malloc(strlen(name) + 3)) == NULL) {
		(void) fprintf(stderr, gettext(
			"%s: malloc error\n"),
			program);
		exit(4);
	}
	if ((tablename2 = (char *)malloc(strlen(name) + 1)) == NULL) {
		(void) fprintf(stderr, gettext(
			"%s: malloc error\n"),
			program);
		exit(4);
	}

	(void) strcpy(tablename1, name);
	(void) strcpy(tablename2, name);
	(void) strcat(tablename1, ".c");

	f1 = fopen(tablename1, "w");
	if (f1 == NULL) {
		(void) fprintf(stderr, gettext(
			"Could not create %s.\n"), tablename1);
		return (ERROR);
	}
	f2 = fopen(tablename2, "w");
	if (f2 == NULL) {
		(void) fprintf(stderr, gettext(
			"Could not create %s.\n"), tablename1);
		(void) fclose(f1);
		return (ERROR);
	}

	if (codeset1 || codeset2 || codeset3) {
		create0w();
	}
	create1(f1);
	if (codeset1 || codeset2 || codeset3) {
		create1w(f1);
	} else {
		createw_empty(f1);
	}
	create2(f2);
	if (codeset1 || codeset2 || codeset3) {
		create2w(f2);
	}
	(void) fclose(f1);
	(void) fclose(f2);
	return (0);
}

static void
create0w()
{
	int		i, j, k, l;
	int		s, mask;
	int		min, max;
	unsigned char	*index_addr;
	unsigned int	*type_addr;
	unsigned int	*code_addr;
	unsigned int	sv_type;
	int				cnt_index_save[3];


#ifdef DDEBUG
	dumpconv();
#endif
	if (codeset1) {
		wcptr[0] = &wctbl[0];
	}
	if (codeset2) {
		wcptr[1] = &wctbl[1];
	}
	if (codeset3) {
		wcptr[2] = &wctbl[2];
	}
	index_addr = (unsigned char *)(sizeof (struct _wctype) * 3);
	for (s = 0; s < 3; s++) {
		if (wcptr[s] == 0) {
			continue;
		}
		/*
		 * Search for minimum.
		 */
		GetMinMaxwctyp(s, &min, &max);
		i = min;
		if (i != -1) {
			wctbl[s].tmin = i;
			sv_type = wctyp[s][GetNewAddr(i, ON)];
			cnt_type[s] = 1;
			wctbl[s].tmax = max;
			cnt_index[s] = wctbl[s].tmax - wctbl[s].tmin + 1;

			/* save real number of cnt_index for */
			/* clearing patch below */
			cnt_index_save[s] = cnt_index[s];

			if ((cnt_index[s] % 8) != 0) {
				cnt_index[s] = ((cnt_index[s] / 8) + 1) * 8;
			}
			if ((index[s] = (unsigned char *)
					(malloc((unsigned)cnt_index[s])))
				== NULL ||
				(type[s] = (unsigned *)
				(malloc((unsigned)cnt_index[s] * 4)))
				== NULL) {
#ifdef DDEBUG
				perror("MALLOC #1");
#endif
				(void) fprintf(stderr, gettext(
					"%s: malloc error\n"),
					program);
				exit(4);
			}
			type[s][0] = sv_type;

			/* clear values for patched index */
			for (i = 1, k = wctbl[s].tmax;
				i <= (cnt_index[s]-cnt_index_save[s]); i++) {
				wctyp[s][GetNewAddr(k+i, ON)] = 0;
			}
			for (i = 0, j = 0, k = wctbl[s].tmin;
				i < cnt_index[s]; i++, k++) {
				for (l = j; l >= 0; l--) {
					int val;
					val = GetNewAddr(k, OFF);
					if (val == -1) {
						l = 1;
						break;
					} else if (type[s][l] ==
						wctyp[s][val]) {
						break;
					}
				}
				if (l >= 0) {
					index[s][i] = l;
				} else {
					int val;
					val = GetNewAddr(k, OFF);
					if (val == -1) {
						type[s][++j] = 0;
					} else {
						type[s][++j] = wctyp[s][val];
					}
					index[s][i] = j;
					cnt_type[s]++;
					if (j > 256) {
#ifdef DDEBUG
						(void) fprintf(stderr,
							"CNT_CTYPE OVERFLOW\n");
#endif
						(void) fprintf(stderr, gettext(
						"%s: character class table "
						"\"LC_CTYPE%d\" exhausted.\n"),
							input_fname, ++s);
					}
				}
			}
			if ((cnt_type[s] % 8) != 0) {
				cnt_type[s] = ((cnt_type[s] / 8) + 1) * 8;
			}
		}
		GetMinMaxWconv(s, &min, &max);
		i = min;
		if (i != -1) {
			wctbl[s].cmin = i;
			wctbl[s].cmax = max;
			cnt_code[s] = wctbl[s].cmax - wctbl[s].cmin + 1;
			if ((cnt_code[s] % 8) != 0) {
				cnt_code[s] = ((cnt_code[s] / 8) + 1) * 8;
			}
			if ((code[s] = (unsigned int *)
					(malloc((unsigned)cnt_code[s] * 4)))
				== NULL) {
				(void) fprintf(stderr, gettext(
					"%s: malloc error\n"),
					program);
				exit(4);
			}
			if (s == 0) {
				mask = 0x8080;
			} else if (s == 1) {
				mask = 0x0080;
			} else {
				mask = 0x8000;
			}
			for (i = 0, j = wctbl[s].cmin; i < cnt_code[s]; i++) {
				code[s][i] = ((((j + i) & 0x3f8080) << 2) |
					(((j + i) & 0x3f80) << 1) |
					((j + i) & 0x7f)) | mask;
			}
			for (i = 0, j = wctbl[s].cmin;
				i < cnt_code[s]; i++, j++) {
				int val;
				val = GetNewAddr(j, OFF);
				if (val != -1) {
					if (wconv[s][GetNewAddr(j, OFF)] !=
					    0xffffff) {
						code[s][i] =
						wconv[s][GetNewAddr(j, OFF)];
					}
				}
			}
		}
		if (cnt_index[s] != 0) {
			wctbl[s].index = index_addr;
		}
		type_addr = (unsigned int *)(index_addr + cnt_index[s]);
		if (cnt_type[s] != 0) {
			wctbl[s].type = type_addr;
		}
		code_addr = (unsigned int *)(type_addr + cnt_type[s]);
		if (cnt_code[s] != 0) {
			wctbl[s].code = (wchar_t *)code_addr;
		}
		index_addr = (unsigned char *)(code_addr + cnt_code[s]);
	}
}


/*
 * create2() & create2w() produces a data file containing the ctype array
 * elements. The name of the file is the same as the value
 * of the environment variable LC_CTYPE.
 */

static void
create2(FILE *f)
{
	int		i = 0;
	int		j;
	if (codeset1 || codeset2 || codeset3) {
		j = SIZE;
	} else {
		j = SIZE - CSLEN + 7;
	}
	for (i = 0; i < j; i++) {
		(void) fprintf(f, "%c", ctype[i]);
	}
}

static void
create2w(FILE *f)
{
	int		s;
	if (fwrite(wctbl, (sizeof (struct _wctype)) * 3, 1, f) == NULL) {
		perror(tablename1);
		exit(4);
	}
	for (s = 0; s < 3; s++) {
		if (cnt_index[s] != 0) {
			if (fwrite(&index[s][0], cnt_index[s], 1, f) == NULL) {
				perror(tablename1);
				exit(4);
			}
		}
		if (cnt_type[s] != 0) {
			if (fwrite(&type[s][0], 4, cnt_type[s], f) == NULL) {
				perror(tablename1);
				exit(4);
			}
		}
		if (cnt_code[s] != 0) {
			if (fwrite(&code[s][0], (sizeof ((unsigned int) 0)),
				cnt_code[s], f) == NULL) {
				perror(tablename1);
				exit(4);
			}
		}
	}
}

/*
 * create1() produces a C source file based on the definitions
 * read from the input file. For example for the current ASCII
 * character classification where LC_CTYPE = ascii it produces a C source
 * file named wctype.c.
 */

static void
create1(FILE *f)
{
	struct  field {
		unsigned char	ch[20];
	} out[8];

	struct classname	*cnp;
	unsigned char	outbuf[256];
	int		cond = 0;
	int		flag = 0;
	int		i, j, index1, index2;
	int		line_cnt = 0;
	int		num;
	int		k;

	comment1(f);
	(void) sprintf((char *)outbuf,
		"unsigned char\t_ctype_[] =  { 0,");
	(void) fprintf(f, "%s\n", outbuf);

	index1 = 0;
	index2 = 7;
	while (flag <= 1) {
		for (i = 0; i <= 7; i++) {
			(void) strcpy((char *)out[i].ch, "\0");
		}
		for (i = index1; i <= index2; i++) {
			if (!((ctype + 1)[i])) {
				(void) strcpy((char *)out[i - index1].ch, "0");
				continue;
			}
			num = (ctype + 1)[i];
			if (flag) {
				(void) strcpy((char *)out[i - index1].ch, "0x");
				(void) strcat((char *)out[i-index1].ch,
					(char *)hextostr(num));
				continue;
			}
			while (num)  {
				for (cnp = cln; cnp->num != UL; cnp++) {
					if (!(num & cnp->num)) {
						continue;
					}
					if ((strlen((char *)out[i - index1].ch))
						== NULL) {
						(void) strcat((char *)
							out[i-index1].ch,
							cnp->repres);
					} else  {
						(void) strcat((char *)
							out[i-index1].ch,
							"|");
						(void) strcat((char *)
							out[i-index1].ch,
							cnp->repres);
					}
					num = num & ~cnp->num;
					if (!num) {
						break;
					}
				}		/* end inner for */
			}			/* end while */
		}				/* end outer for loop */
		(void) sprintf((char *)outbuf,
			"\t%s,\t%s,\t%s,\t%s,\t%s,\t%s,\t%s,\t%s,",
			out[0].ch, out[1].ch, out[2].ch, out[3].ch,
			out[4].ch, out[5].ch, out[6].ch, out[7].ch);
		if (++line_cnt == 32) {
			line_cnt = 0;
			flag++;
			cond = flag;
		}
		switch (cond) {
		case 1:
			(void) fprintf(f, "%s\n", outbuf);
			comment2(f);
			(void) fprintf(f, "\t0,\n");
			index1++;
			index2++;
			cond = 0;
			break;
		case 2:
			(void) fprintf(f, "%s\n", outbuf);
			(void) fprintf(f,
				"\n\t/* multiple byte character "
				"width information */\n\n");
			k = 6;
			for (j = 0; j < k; j++) {
				if ((j % 8 == 0) && (j != 0)) {
					(void) fprintf(f, "\n");
				}
				(void) fprintf(f, "\t%d,",
					ctype[START_CSWIDTH + j]);
			}
			(void) fprintf(f, "\t%d\n", ctype[START_CSWIDTH + k]);
			(void) fprintf(f, "};\n");
			break;
		default:
			(void) fprintf(f, "%s\n", outbuf);
			break;
		}
		index1 += 8;
		index2 += 8;
	}			/* end while loop */
	if (width) {
		comment3(f);
	}
	/* print the numeric array here. */
	(void) fprintf(f, "\n\nunsigned char\t_numeric[SZ_NUMERIC] = \n");
	(void) fprintf(f, "{\n");
	(void) fprintf(f, "\t%d,\t%d,\n",
		ctype[START_NUMERIC], ctype[START_NUMERIC +1]);
	(void) fprintf(f, "};\n");
}

/* create1w() produces a C source program for supplementary code sets */


static void
create1w(FILE *f)
{
	struct  field {
		unsigned char  ch[100];
	} out[8];

	struct classname	*cnp;
	unsigned char	outbuf[256];
	unsigned char	*cp;
	int		cond = 0;
	int		flag = 0;
	int		i, index1, index2;
	int		line_cnt = 0;
	int 	num;
	int		s;

	comment4(f);
	(void) sprintf((char *)outbuf, "struct _wctype _wcptr[3] = {\n");
	(void) fprintf(f, "%s", outbuf);
	for (s = 0; s < 3; s++) {
		(void) fprintf(f, "\t{");
		if (wctbl[s].tmin == 0) {
			(void) fprintf(f, "0,\t");
		} else {
			(void) fprintf(f, "0x%s,\t",
				hextostr((int)wctbl[s].tmin));
		}
		if (wctbl[s].tmin == 0) {
			(void) fprintf(f, "0,\t");
		} else {
			(void) fprintf(f, "0x%s,\t",
				hextostr((int)wctbl[s].tmax));
		}
		if (wctbl[s].index == 0) {
			(void) fprintf(f, "0,\t");
		} else {
			(void) fprintf(f, "(unsigned char *)0x%s,\t",
				hextostr((int)wctbl[s].index));
		}
		if (wctbl[s].type == 0) {
			(void) fprintf(f, "0,\t");
		} else {
			(void) fprintf(f, "(unsigned *)0x%s,\t",
				hextostr((int)wctbl[s].type));
		}
		if (wctbl[s].cmin == 0) {
			(void) fprintf(f, "0,\t");
		} else {
			(void) fprintf(f, "0x%s,\t",
				hextostr((int)wctbl[s].cmin));
		}
		if (wctbl[s].cmax == 0) {
			(void) fprintf(f, "0,\t");
		} else {
			(void) fprintf(f, "0x%s,\t",
				hextostr((int)wctbl[s].cmax));
		}
		if (wctbl[s].code == 0) {
			(void) fprintf(f, "0");
		} else {
			(void) fprintf(f, "(unsigned int *)0x%s",
				hextostr((int)wctbl[s].code));
		}
		if (s < 2) {
			(void) fprintf(f, "},\n");
		} else {
			(void) fprintf(f, "}\n");
		}
	}
	(void) fprintf(f, "};\n");

	comment5(f);
	for (s = 0; s < 3; s++) {
		index1 = 0;
		index2 = 7;
		flag = 0;
		while (flag <= 2) {
			if (line_cnt == 0) {
				if (flag == 0 && cnt_index[s]) {
					(void) fprintf(f,
						"unsigned char index%d[] = {\n",
						s+1);
				} else if ((flag == 0 || flag == 1) &&
					cnt_type[s]) {
					(void) fprintf(f,
						"unsigned type%d[] = {\n",
						s+1);
					flag = 1;
				} else if (cnt_code[s]) {
					(void) fprintf(f,
						"unsigned int code%d[] = {\n",
						s+1);
					flag = 2;
				} else {
					break;
				}
			}
			for (i = 0; i <= 7; i++) {
				(void) strcpy((char *)out[i].ch, "\0");
			}
			for (i = index1; i <= index2; i++) {
				if ((flag == 0 && !index[s][i]) ||
					(flag == 1 && !type[s][i]) ||
					(flag == 2 && !code[s][i])) {
					(void) strcpy((char *)
						out[i - index1].ch, "0");
					continue;
				}
				if (flag == 0) {
					num = index[s][i];
				} else if (flag == 1) {
					num = type[s][i];
				} else {
					num = code[s][i];
				}
				if (flag == 0 || flag == 2) {
					(void) strcpy((char *)
						out[i - index1].ch, "0x");
					(void) strcat((char *)out[i-index1].ch,
						(char *) hextostr(num));
					continue;
				}
				while (num)  {
					for (cnp = cln; cnp->num != UL; cnp++) {
						if (!(num & cnp->num)) {
							continue;
						}
						if ((strlen((char *)
							out[i-index1].ch)) ==
							NULL) {
							(void) strcat((char *)
							out[i-index1].ch,
							cnp->repres);
						} else {
							(void) strcat((char *)
							out[i - index1].ch,
							"|");
							(void) strcat((char *)
							out[i-index1].ch,
							cnp->repres);
						}
						num = num & ~cnp->num;
						if (!num) {
							break;
						}
					}		/* end inner for */
				}			/* end while */
			}				/* end outer for loop */
			(void) sprintf((char *)outbuf,
				"\t%s,\t%s,\t%s,\t%s,\t%s,\t%s,\t%s,\t%s,",
				out[0].ch, out[1].ch, out[2].ch, out[3].ch,
				out[4].ch, out[5].ch, out[6].ch, out[7].ch);
			line_cnt++;
			if ((flag == 0 && line_cnt == cnt_index[s]/8) ||
				(flag == 1 && line_cnt == cnt_type[s]/8) ||
				(flag == 2 && line_cnt == cnt_code[s]/8)) {
				line_cnt = 0;
				flag++;
				cond = flag;
			}
			switch (cond) {
			case 1:
			case 2:
			case 3:
				cp = outbuf + strlen((char *)outbuf);
				*--cp = ' ';
				*++cp = '\0';
				(void) fprintf(f, "%s\n", outbuf);
				(void) fprintf(f, "};\n");
				index1 = 0;
				index2 = 7;
				cond = 0;
				break;
			default:
				(void) fprintf(f, "%s\n", outbuf);
				index1 += 8;
				index2 += 8;
				break;
			}
		}			/* end while loop */
	}
}

static unsigned char *
hextostr(int num)
{
	unsigned char			*idx;
	static unsigned char	buf[64];

	idx = buf + sizeof (buf);
	*--idx = '\0';
	do {
		*--idx = "0123456789abcdef"[num % 16];
		num /= 16;
	} while (num);
	return (idx);
}

static void
comment1(FILE *f)
{
	(void) fprintf(f,
		"#include <ctype.h>\n");
	(void) fprintf(f,
		"#include <widec.h>\n");
	(void) fprintf(f,
		"#include <wctype.h>\n\n\n");
	(void) fprintf(f,
		"\t/*\n");
	(void) fprintf(f,
		"\t ************************************************\n");
	(void) fprintf(f,
		"\t *\t\t%s  CHARACTER  SET                \n", tablename1);
	(void) fprintf(f,
		"\t ************************************************\n");
	(void) fprintf(f,
		"\t */\n\n");
	(void) fprintf(f,
		"\t/* The first 257 characters are used to determine\n");
	(void) fprintf(f,
		"\t * the character class */\n\n");
}

static void
comment2(FILE *f)
{
	(void) fprintf(f,
		"\n\n\t/* The next 257 characters are used for \n");
	(void) fprintf(f,
		"\t * upper-to-lower and lower-to-upper conversion */\n\n");
}

static void
comment3(FILE *f)
{
	(void) fprintf(f,
		"\n\n\t/*  CSWIDTH INFORMATION                           */\n");
	(void) fprintf(f,
		"\t/*_____________________________________________   */\n");
	(void) fprintf(f,
		"\t/*                    byte width <> screen width  */\n");
	(void) fprintf(f,
		"\t/* SUP1\t\t\t     %d    |     %d         */\n",
		ctype[START_CSWIDTH], ctype[START_CSWIDTH + 3]);
	(void) fprintf(f,
		"\t/* SUP2\t\t\t     %d    |     %d         */\n",
		ctype[START_CSWIDTH + 1], ctype[START_CSWIDTH + 4]);
	(void) fprintf(f,
		"\t/* SUP3\t\t\t     %d    |     %d         */\n",
		ctype[START_CSWIDTH + 2], ctype[START_CSWIDTH + 5]);
	(void) fprintf(f,
		"\n"
		"\t/* MAXIMUM CHARACTER WIDTH        %d               */\n",
		ctype[START_CSWIDTH + 6]);
}

static void
comment4(FILE *f)
{
	(void) fprintf(f,
		"\n\n\t/* The next entries point to wctype tables */\n");
	(void) fprintf(f,
		"\t/*          and have upper and lower limit */\n\n");
}

static void
comment5(FILE *f)
{
	(void) fprintf(f,
		"\n\n\t/* The folowing table is used to determine\n");
	(void) fprintf(f,
		"\t * the character class for supplementary code sets */\n\n");
}

static void
createw_empty(FILE *f)
{
	(void) fprintf(f,
		"\n\nstruct _wctype _wcptr[3] = {\n");
	(void) fprintf(f,
		"\t{0,	0,	0,	0,	0,	0,	0},\n");
	(void) fprintf(f,
		"\t{0,	0,	0,	0,	0,	0,	0},\n");
	(void) fprintf(f,
		"\t{0,	0,	0,	0,	0,	0,	0}\n");
	(void) fprintf(f,
		"};\n\n");

}
