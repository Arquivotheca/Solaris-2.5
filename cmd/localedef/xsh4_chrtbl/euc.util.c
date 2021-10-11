/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)euc.util.c	1.22	95/07/14 SMI"
#include <varargs.h>
#include "chrtbl.h"
#include "extern.h"

extern void		execerror(char *, ...);

static int	GetRealAddr(int, int);

#define	_ADDR(__x)  (__x & 0x7f)|((__x & 0x7f00) >> 1)|((__x & 0x7f0000) >> 2)
#define	ON	1
#define	OFF	0
#define TAB	0x9

void
setmem(int code)
{
	int		i;

	if (((wctyp[code - 1] =
		(unsigned *)(malloc(WSIZE * NUM_BANKS))) == NULL) ||
		((wconv[code - 1] =
		(unsigned int *)(malloc(WSIZE * NUM_BANKS))) == NULL)) {
		(void) fprintf(stderr, gettext(
			"%s: malloc error\n"),
			program);
		exit(4);
	}
	for (i = 0; i < (WSIZE / 4) * NUM_BANKS; i++) {
		wctyp[code - 1][i] = 0;
		wconv[code - 1][i] = 0xffffff;
	}
	num_banks[code - 1] = NUM_BANKS;
}

#define	LOW14		0x03fff

struct addr_conv {
	signed char		flg;
	unsigned char 	old;
};

struct addr_conv *addr_conv;

void
init_add_conv()
{
	int		i;

	addr_conv = (struct addr_conv *)
		malloc((sizeof (struct addr_conv)) * NUM_BANKS);
	if (addr_conv == NULL) {
		(void) fprintf(stderr, gettext(
			"%s: malloc error\n"),
			program);
		exit(4);
	}
	for (i = 0; i < NUM_BANKS; i++) {
		addr_conv[i].flg = 0;
		addr_conv[i].old = 0;
	}
	addr_conv[i - 1].flg = -1;
}

/*
 * Convert
 */
int
Convert(unsigned int num)
{
	unsigned char		top8;
	struct addr_conv	*p = addr_conv;
	int		i = 0;
	/*
	 * If it is already 16 bits, return it.
	 */
	if ((num & 0xff0000) == 0) {
		return (num);
	}

	top8 = (num & 0xff0000) >> 16;

	while (p->flg != -1) {
		if (p->old == top8)	/* found, assigned already */
			break;
		if (p->flg == 0) {
			p->old = top8;
			p->flg = 1;
			break;
		}
		p++;
		i++;
	}
	if (p->flg == -1) {
		/*
		 * Illegal 3 byte EUC code specified.
		 */
		(void) fprintf(stderr, gettext(
			"Error in %s: illegal 3byte code specified.\n"),
			input_fname);
	}

	return (((i+1) << 16+2) | (num & 0xffff));
}

/*
 * Get min and max
 */
void
GetMinMaxwctyp(int s, int *ret_min, int *ret_max)
{
	int		min[NUM_BANKS];
	int		max[NUM_BANKS];
	int		Min = -1;
	int		Max = -1;
	int		i, j;

	for (i = 0; i < NUM_BANKS; i++)
		min[i] = max[i] = -1;

	/*
	 * Get minimum and maximum.
	 */
	for (j = 0; j < num_banks[s]; j++) {
		for (i = 0; i < 0xffff && wctyp[s][i + j * 0xffff] == 0; i++);
		if (i != 0xffff) {
			min[j] = GetRealAddr(s, i + j * 0xffff);
			for (i = 0xffff - 1;
				i >= 0 && wctyp[s][i + j * 0xffff] == 0; i--);
				max[j] = GetRealAddr(s, i + j * 0xffff);
		}
	}

	/*
	 * Decide minimum bank
	 */
	for (i = 0; i < num_banks[s]; i++) {
		if (min[i] != -1) {
			if (Min == -1)
				Min = min[i];
			else {
				if (Min > min[i])
					Min = min[i];
			}
		}
		if (max[i] != -1) {
			if (Max == -1)
				Max = max[i];
			else {
				if (max[i] > Max)
					Max = max[i];
			}
		}
	}
	*ret_min = Min;
	*ret_max = Max;
}


void
GetMinMaxWconv(int s, int *ret_min, int *ret_max)
{
	int		min[NUM_BANKS];
	int		max[NUM_BANKS];
	int		Min = -1;
	int		Max = -1;
	int		i, j;

	for (i = 0; i < num_banks[s]; i++)
		min[i] = max[i] = -1;

	/*
	 * Get minimum and maximum.
	 */
	for (j = 0; j < num_banks[s]; j++) {
		for (i = 0;
			i < 0xffff && wconv[s][i + j * 0xffff] == 0xffffff;
			i++);
		if (i != 0xffff) {
			min[j] = GetRealAddr(s, i + j * 0xffff);
			for (i = 0xffff - 1;
				i >= 0 && wconv[s][i + j * 0xffff] == 0xffffff;
				i--);
			max[j] = GetRealAddr(s, i + j * 0xffff);
		}
	}

	/*
	 * Decide minimum bank
	 */
	for (i = 0; i < num_banks[s]; i++) {
		if (min[i] != -1) {
			if (Min == -1)
				Min = min[i];
			else {
				if (Min > min[i])
					Min = min[i];
			}
		}
		if (max[i] != -1) {
			if (Max == -1)
				Max = max[i];
			else {
				if (max[i] > Max)
					Max = max[i];
			}
		}
	}
	*ret_min = Min;
	*ret_max = Max;
}

/*
 * GetOriAddr
 *	Given the address in wctyp, it returns the real address
 */
static int
GetRealAddr(int s, int addr)
{
	int		top8;
	int		point;
	int		newaddr;

	if (addr < 0xffff)	/* first block */
		return (addr);

	point = (addr & 0x0fffff) >> 16;
	if (point >= num_banks[s]) {
		(void) fprintf(stderr,
		"%s: INTERNAL ERROR in GetRealAddr-illegal address(%x).\n",
		input_fname, addr);
	}
	top8 = addr_conv[point - 1].old;
	newaddr = (addr & 0xffff) | ((top8 & 0x7f) << 14);
	return (newaddr);
}

/*
 * GetNewAddr
 *	Given the address in wctyp, it returns the real address
 */
int
GetNewAddr(int addr, int flag)
{
	struct addr_conv	*p = addr_conv;
	int		top8;
	int		newaddr;
	int		i = 0;

	if ((addr & 0xffff0000) == 0)
		return (addr);

	top8 = ((addr >> 14) & 0xff) | 0x80;
	while (p->flg != -1) {
		if (p->old == top8) {
			break;
		}
		p++;
		i++;
	}

	if (p->flg == -1) {
		if (flag == OFF) {
			return (-1);
		} else {
			(void) fprintf(stderr,
			"%s: INTERNAL ERROR in GetNewAddr-illegal address(%x).",
			input_fname, addr);
		}
	}
	newaddr = ((i+1) << 14+2) | (addr & LOW14);
	return (newaddr);
}

/*
 * Set up table
 */
int
set_ctype(int code, unsigned int mask, unsigned int val,
	int flag, unsigned int low)
{
	unsigned int lower, upper;
#ifdef DDEBUG
(void) printf(
	"set_ctype:codeset = %d,mask = 0x%x,val = 0x%x,flag = %d,low = 0x%x\n",
	code, mask, val, flag, low);
#endif

	/*
	 * HACK - bug 1196745
	 * When we change the notion of what isprint() means then we
	 * can probably remove this.
	 */
	if (val == TAB)
		mask &= ~ _B;		/* turn off blank flag */

	if (code == 0 || (code == 1 && val <= 0x00ff)) {
		/*
		 * get the lower 8 bits
		 */
		low = low & 0x00ff;
		val = val & 0x00ff;
	}
	if (flag == ON) {
		lower = Convert(low);
		upper = Convert(val);
		if (lower > upper) {
			(void) fprintf(stderr, gettext(
				"Lower limit is larger than upper limit.\n"));
			return (-1);
		}
		if (code == 0 || (code == 1 && val <= 0x00ff))
			(ctype+1)[upper] |= mask;
		if (code != 0)
			wctyp[code - 1][_ADDR(upper)] |= mask;
		while (++lower <= upper) {
			if (code == 0 || (code == 1 && val <= 0x00ff))
				(ctype+1)[lower] |= mask;
			if (code != 0)
				wctyp[code - 1][_ADDR(lower)] |= mask;
		}
	} else {
		lower = Convert(val);
		if (code == 0 || (code == 1 && val <= 0x00ff))
			(ctype+1)[lower] |= mask;
		if (code != 0)
			wctyp[code - 1][_ADDR(lower)] |= mask;
	}
	return (0);
}

int
set_conv(int code, unsigned int lower, unsigned int upper)
{
	int l, u;
	if (code == 0 || (code == 1 && lower <= 0x00ff && upper <= 0x00ff)) {
		l = lower & 0x00ff;
		u = upper & 0x00ff;
		l = Convert(l);
		u = Convert(u);
		(ctype+1)[l+257] = u;
		(ctype+1)[u+257] = l;
	} if (code != 0) {
		l = lower;
		u = upper;
		wconv[code-1][_ADDR(u)] = l;
		wconv[code-1][_ADDR(l)] = u;
	}
#ifdef DDEBUG
(void) printf("set_conv called. code = %d, upper = %d, lower = %d\n",
	code, u, l);
#endif
	return (0);
}

void
euc_width_info(int cs, int w, int s)
{
	if (cs < 3) {
		ctype[START_CSWIDTH + cs] = w;
		ctype[START_CSWIDTH + cs + 3] = s;
	} else {
		if (ctype[START_CSWIDTH + 1] > ctype[START_CSWIDTH + 2]) {
			ctype[START_CSWIDTH + 6] = ctype[START_CSWIDTH + 1];
		} else {
			ctype[START_CSWIDTH + 6] = ctype[START_CSWIDTH + 2];
		}
#ifdef SJIS
		if (!m_flag) {
#endif
			if (ctype[START_CSWIDTH + 6] > 1) {
				++ctype[START_CSWIDTH + 6];
			}

#ifdef SJIS
		}
#endif
		if (ctype[START_CSWIDTH] > ctype[START_CSWIDTH + 6]) {
			ctype[START_CSWIDTH + 6] = ctype[START_CSWIDTH];
		}
	}
}

/*
 * Setting mask
 */
static struct char_mask {
	int				k_type;
	unsigned int	mask;
} char_mask[] = {
	T_UPPER,	ISUPPER,
	T_LOWER,	ISLOWER,
	T_ALPHA,	ISALPHA,
	T_DIGIT,	ISDIGIT,
	T_SPACE,	ISSPACE,
	T_CNTRL,	ISCNTRL,
	T_PUNCT,	ISPUNCT,
	T_GRAPH,	ISGRAPH,
	T_PRINT,	ISPRNT,
	T_XDIGIT,	ISXDIGIT,
	T_BLANK,	ISBLANK,
	T_ISWCHAR1,	ISWCHAR1,
	T_ISWCHAR1,	ISWCHAR1,
	T_ISWCHAR2,	ISWCHAR2,
	T_ISWCHAR2,	ISWCHAR2,
	T_ISWCHAR3,	ISWCHAR3,
	T_ISWCHAR3,	ISWCHAR3,
	T_ISWCHAR4,	ISWCHAR4,
	T_ISWCHAR4,	ISWCHAR4,
	T_ISWCHAR5,	ISWCHAR5,
	T_ISWCHAR5,	ISWCHAR5,
	T_ISWCHAR6,	ISWCHAR6,
	T_ISWCHAR7,	ISWCHAR7,
	T_ISWCHAR8,	ISWCHAR8,
	T_ISWCHAR9,	ISWCHAR9,
	T_ISWCHAR10,	ISWCHAR10,
	T_ISWCHAR11,	ISWCHAR11,
	T_ISWCHAR12,	ISWCHAR12,
	T_ISWCHAR13,	ISWCHAR13,
	T_ISWCHAR14,	ISWCHAR14,
	T_ISWCHAR15,	ISWCHAR15,
	T_ISWCHAR16,	ISWCHAR16,
	T_ISWCHAR17,	ISWCHAR17,
	T_ISWCHAR18,	ISWCHAR18,
	T_ISWCHAR19,	ISWCHAR19,
	T_ISWCHAR20,	ISWCHAR20,
	T_ISWCHAR21,	ISWCHAR21,
	T_ISWCHAR22,	ISWCHAR22,
	T_ISWCHAR23,	ISWCHAR23,
	T_ISWCHAR24,	ISWCHAR24,
	-1, 0
};

unsigned int
get_mask(int type)
{
	int		i = 0;
	while (char_mask[i].k_type != -1) {
		if (char_mask[i].k_type == type) {
			return (char_mask[i].mask);
		}
		++i;
	}
	/*
	 * Should never come here.
	 */
	(void) printf("GET_MASK: INTERNAL ERROR.\n");
	return (0);
}

#ifdef DDEBUG
/*
 * debugging
 */
dumpconv()
{
	int		i;

	(void) fprintf(stderr, "\n DUMPING addr_conv.\n");
	for (i = 0; i < NUM_BANKS; ++i) {
		(void) fprintf(stderr, "\taddr_conv[%x].flg = %x, old = %x\n",
			i, addr_conv[i].flg, addr_conv[i].old);
	}
}
#endif
