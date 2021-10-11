/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)charmap.c	1.10	95/03/06 SMI"

#include "../head/_localedef.h"
#include "../head/charmap.h"
#include <ctype.h>

/*
 * Malloc macros
 */
#define	MALLOC_ENCODED\
	(encoded_val *)calloc(1, sizeof (encoded_val))

CharmapHeader	*charmapheader;
CharmapSymbol	*charmapsymbol;
encoded_val		enbuf;

CharmapSymbol	*search_id(char *, CharmapSymbol *);
void		create_fake(CharmapSymbol *, CharmapSymbol *, int, char *);
void		copy_encoded(encoded_val *, encoded_val *);
char		*make_symname(CharmapSymbol *, int);
int			set_enbuf(char *);
int			cat_enbuf(int, unsigned char *);

extern char		*program;

extern int	bytescopy(unsigned char *, unsigned char *, int);

/*
 * Handling Character Map
 */

/*
 * Set Up character mapping information
 */
int
initialize_charmap(int flag, int fd)
{
	struct stat		stat;
	int		size;

	if (flag == 0) {
		/*
		 * No charmap file was specified.
		 * Use default mapping.
		 */
		return (0);
	} else {
		char	*a;

		if (fstat(fd, &stat) == -1) {
			(void) fprintf(stderr, gettext(
				"%s: stat error.\n"), program);
			return (ERROR);
		}
		size = stat.st_size;
		a = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
		if (a == (char *)-1) {
			(void) fprintf(stderr, gettext(
				"%s: mmap failed.\n"), program);
			return (ERROR);
		}
		charmapheader = (CharmapHeader *)a;
		charmapsymbol = (CharmapSymbol *)(a + CHARMAPHEADER);
	}
	return (0);
}

/*
 *	Look up character map table and returns
 */
CharmapSymbol *
get_charmap_sym(char *id)
{
	int			num;
	int			i;
	int			check = 0;
	CharmapSymbol	*symbols = charmapsymbol;
	CharmapSymbol	*prev_sym = (CharmapSymbol *)NULL;

	if (charmapheader == NULL) {
		return (NULL);
	}

	num = charmapheader->num_of_elements;
	while (num-- > 0) {
		if ((i = strcmp(id, symbols->name)) == 0) {
			return (symbols);
		} else if (i < 0) {		/* id < symbols->name */
			if (check == 0) {	/* the previous is SINGLE */
				break;
			} else {		/* the previous is RANGE */
				return (search_id(id, prev_sym));
			}
		} else {
			if (symbols->type == RANGE) {
				check = 1;
				prev_sym = symbols;
			} else {
				check = 0;
				prev_sym = (CharmapSymbol *)NULL;
			}
		}
		symbols++;
	}
	if (check == 1) {
		return (search_id(id, prev_sym));
	}

	return (NULL);
}

CharmapSymbol *
search_id(char *id, CharmapSymbol *symbols)
{
	int		r, i;
	char	*symname;
	static CharmapSymbol	fake_symbol;

	symname = make_symname(symbols, symbols->range);

	if ((i = strcmp(id, symname)) == 0) {
		create_fake(&fake_symbol, symbols, symbols->range, symname);
		free(symname);
		return (&fake_symbol);
	} else if (i > 0) {			/* id > symname */
		free(symname);
		return ((CharmapSymbol *)NULL);
	} else {					/* id < symname */
		free(symname);

		for (r = 1; r < symbols->range; r++) {
			symname = make_symname(symbols, r);
			if ((i = strcmp(id, symname)) == 0) {
				create_fake(&fake_symbol, symbols, r, symname);
				free(symname);
				return (&fake_symbol);
			} else if (i > 0) {	/* id > symname */
				free(symname);
				continue;
			} else {	/* id < symname (impossible case) */
				free(symname);
				break;
			}
		}
		return ((CharmapSymbol *)NULL);
	}
}

/*
 * Handling encoded_val
 */
int
get_encoded_value(unsigned int *i_val, encoded_val *en)
{
	int		length = 0;

	*i_val = 0;
	while (length < en->length) {
		*i_val = (*i_val << 8) | en->bytes[length];
		length++;
	}
#ifdef DDEBUG
(void) printf(
	"get_encoded_value((%d,%c), %d)\n",
	*i_val, *i_val, en->length);
#endif
	return (en->length);
}

/*
 * Expand symbol name
 */
int
expand_sym_string(unsigned char *t, unsigned char *f)
{
	unsigned char	*fp = f;
	unsigned char	sym_id[MAX_BYTES];
	unsigned char	*sym_idp;
	unsigned char	*tmp_fp;
	int		n;		/* tmp use */
	int		len = 0;

	while (*fp != 0) {
		switch (*fp) {
		case '<':
			/*
			 * This is possible symbol which needs to be
			 * looked in the symbol table.
			 */
			sym_idp = sym_id;
			tmp_fp = fp;
			tmp_fp++;

			/*
			 * Get the symbol name in between < and > into
			 * sym_id[].
			 */
			while (*tmp_fp != 0 && *tmp_fp != '>') {
				*sym_idp++ = *tmp_fp++;
			}
			*sym_idp = 0;

			/*
			 * If this is the end of string,
			 * it is done. (Should be an error ?)
			 */
			if (*tmp_fp == 0) {
				tmp_fp = sym_id;
				*t++ = '<'; len++;
				while (*tmp_fp != 0) {
					*t++ = *tmp_fp++;
					len++;
				}
				goto out_while;
			}

			/*
			 * I got an valid symbol to be checked.
			 */
			tmp_fp++;
			fp = tmp_fp;

			/*
			 * Check if this is a symbol defined or not.
			 */
			enbuf.length = 0;	/* initialize enbuf */
			n = set_enbuf((char *)sym_id);
			if (n == ERROR) {
				/*
				 * Symbol was not defined.
				 */
				(void) fprintf(stderr, (char *)gettext(
					"%s: %s is an undefined symbol.\n"),
					program, (char *)sym_id);
				return (ERROR);
			}
			/*
			 * This is a valid symbol.
			 * Expand it into the output buffer.
			 */
			n = 0;
			while (n < enbuf.length) {
				*t++ = enbuf.bytes[n++];
				len++;
			}
			break;
		default:
			*t++ = *fp++;
			len++;
			break;
		}
	}
out_while:
	*t = 0;
	return (len);
}

void
create_fake(CharmapSymbol *fake, CharmapSymbol *symbols, int r, char *name)
{
	unsigned	u_val;
	int			i;

	fake->type = SINGLE;
	(void) strcpy(fake->name, name);
	fake->en_val.length = symbols->en_val.length;
	(void) get_encoded_value(&u_val, &symbols->en_val);

	u_val += r;
	for (i = 0; i < symbols->en_val.length; i++) {
		fake->en_val.bytes[i] = (unsigned char)
			((u_val >> (8 * (symbols->en_val.length - i - 1)))
			& 0x000000ff);
	}
	for (; i < MAX_BYTES; i++) {
		fake->en_val.bytes[i] = '\0';
	}
	fake->range = 0;
}

char *
make_symname(CharmapSymbol *symbols, int r)
{
	char		*n, *p, *q, *t;
	char		name[MAX_ID_LENGTH], num_c[MAX_ID_LENGTH];
	int			num, len, lim, i;

	t = symbols->name;
	n = p = &name[0];

	while (!isdigit(*t)) {
		*p++ = *t++;
	}
	*p = '\0';

	/* Now t must be a decimal number */
	q = t;
	len = 0;
	while (*q ++) {
		len++;
	}

	num = atoi(t);
	num += r;
	for (lim = 1, i = 0; i < len; i++) {
		lim = lim * 10;
	}
	if (num >= lim) {			/* overflow */
		len++;
	}
	(void) sprintf(num_c, "%0*d", len, num);
	(void) strcat(n, num_c);
	return (strdup(n));
}

encoded_val *
alloc_encoded(encoded_val *en)
{
	encoded_val	*p;

	p = MALLOC_ENCODED;
	if (p == NULL) {
		return (NULL);
	}
	copy_encoded(p, en);
	return (p);
}

#define	LENGTH(x)	x->encoded_val.length
#define	BYTES(x)	x->encoded_val.bytes

int
set_enbuf(char *id)
{
	CharmapSymbol	*cs;
	int		ret = 0;

	if ((cs = get_charmap_sym(id)) != NULL) {
		ret = cat_enbuf(cs->en_val.length, cs->en_val.bytes);
	} else {
		ret = ERROR;
	}
	return (ret);
}

int
cat_enbuf(int len, unsigned char *b)
{
	while (len-- > 0) {
		enbuf.bytes[enbuf.length++] = *b++;
		/*
		 * Overflow check needed.
		 */
	}
	return (0);
}

void
free_encoded_symbol(encoded_val *en)
{
	if (en != NULL) {
		free(en);
	}
}

int
cmp_encoded(encoded_val *e1, encoded_val *e2)
{
	int		i = 0;

	if (e1->length != e2->length) {
		return (1);
	}
	while (i < e1->length) {
		if (e1->bytes[i] != e2->bytes[i]) {
			return (1);
		}
		i++;
	}
	return (0);
}

void
copy_encoded(encoded_val *to, encoded_val *from)
{
	to->length = from->length;
	if (to->length > 0) {
		(void) bytescopy(to->bytes, from->bytes, to->length);
	}
}

int
get_encoded_string(unsigned char *o, encoded_val *en)
{
	int len = 0;

	while (len < en->length) {
		*o++ = en->bytes[len];
		len++;
	}
	*o = 0;
	return (len);
}

int
set_encoded_string(unsigned char *o, encoded_val *en)
{
	int len = 0;

	while (*o != 0) {
		en->bytes[len] = *o++;
		len++;
	}
	en->length = len;
	return (len);
}

/*
 * Debugging info
 */
#ifdef DEBUG
dump_charmap()
{
	CharmapSymbol	*p;
	int				num = charmapheader->num_of_elements;

	p = charmapsymbol;
	(void) printf("DUMP_CHARMAP\n");
	(void) printf("num of symbols = %d\n", num);
	while (num-- > 0) {
		(void) printf("name = '%s'\n", p->name);
		++p;
	}
}

dump_encoded(char *s, encoded_val *en)
{
	int		i;
	(void) printf("%s ", s);
	(void) printf("  l=%d,\n\t v=", en->length);
	for (i = 0; i < en->length; i++) {
		if (isprint(en->bytes[i])) {
			(void) printf("(%x,'%c'),", en->bytes[i], en->bytes[i]);
		} else {
			(void) printf("(%x,'*'),", en->bytes[i]);
		}
	}
	(void) printf("\n");
}
#endif
