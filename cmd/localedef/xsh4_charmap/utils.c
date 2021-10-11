/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)utils.c	1.9	95/03/06 SMI"

#include "../head/_localedef.h"
#include "../head/charmap.h"
#include "extern.h"
#include <unistd.h>
#include <ctype.h>

typedef struct slink {
	CharmapSymbol	*sym;
	struct slink	*next;
} slink;

slink	*sym_head = NULL;

extern char		*output_fname;

int
check_id(char *id1, char *id2)
{
	char	*p;
	int		num1;
	int		num2;
#ifdef DDEBUG
	(void) printf("\t<CHECK_ID>\n");
	(void) printf("\tID1 = '%s'\n", id1);
	(void) printf("\tID2 = '%s'\n", id2);
#endif
	while (*id1 == *id2) {
		++id1;
		++id2;
	}
	if (!isdigit(*id1) || !isdigit(*id2)) {
		return (ERROR);
	}
	p = id1;
	while (*p) {
		if (!isdigit(*p++)) {
			return (ERROR);
		}
	}
	p = id2;
	while (*p) {
		if (!isdigit(*p++)) {
			return (ERROR);
		}
	}
	num1 = atoi(id1);
	num2 = atoi(id2);
	if (num1 > num2) {
		return (ERROR);
	}
#ifdef DDEBUG
	(void) printf("\tnum1 = %d, num2 = %d\n", num1, num2);
#endif
	return (num2 - num1);
}

int
install_symbol(int type, char *id, unsigned char *bytes, int length, int range)
{
	extern int	num_of_symbols;
	slink		*l, *p;
	slink		*nl;
	CharmapSymbol	*sym;
	int		i;

	/*
	 * Check duplicate
	 */
	l = sym_head;
	while (l != NULL) {
		if ((i = strcmp(id, l->sym->name)) ==  0) {
			(void) fprintf(stderr, gettext(
				"%s already used.\n"),
				id);
			return (ERROR);
		} else if (i < 0) {
			break;
		}
		l = l->next;
	}

	/*
	 * Set up the Symbol structure
	 */
	sym = (CharmapSymbol *)calloc(1, sizeof (CharmapSymbol));
	if (sym == NULL) {
		(void) fprintf(stderr, gettext(
			"Could not allocate memory for symbol table.\n"));
		return (ERROR);
	}
	sym->type = type;
	(void) strcpy(sym->name, id);
	sym->en_val.length = length;
	for (i = 0; i < MAX_BYTES; i++) {
		sym->en_val.bytes[i] = bytes[i];
	}
	sym->range = range;

	nl = (slink *)calloc(1, sizeof (slink));
	if (nl == NULL) {
		(void) fprintf(stderr, gettext(
			"Could not allocate memory for symbol table.\n"));
		free(sym);
		return (ERROR);
	}
	nl->sym = sym;

	/*
	 * Insert
	 */
	if (sym_head == NULL) {
		sym_head = nl;
		nl->next = NULL;
	} else {
		l = sym_head;
		while (strcmp(id, l->sym->name) > 0) {
			p = l;
			l = l->next;
			if (l == NULL) {
				break;
			}
		}
		if (l == sym_head) {
			nl->next = l;
			sym_head = nl;
		} else {
			p->next = nl;
			nl->next = l;
		}
	}
	num_of_symbols++;
	return (0);
}

void
output(int fd)
{
	CharmapHeader	*Header;
	CharmapSymbol	*sym;
	extern int		num_of_symbols;
	slink	*lp = sym_head;
	int		i;

	Header = calloc(1, sizeof (CharmapHeader));
	if (Header == NULL) {
		(void) fprintf(stderr, gettext("Calloc error.\n"));
		(void) unlink(output_fname);
		exit(4);
	}
	(void) strcpy(Header->code_set_name, code_set_name);
	Header->mb_cur_max = mb_cur_max;
	Header->mb_cur_min = mb_cur_min;
	Header->escape_char = escape_char;
	Header->comment_char = comment_char;
	Header->num_of_elements = num_of_symbols;

	if (write(fd, Header, sizeof (CharmapHeader)) !=
		sizeof (CharmapHeader)) {
		(void) unlink(output_fname);
		(void) fprintf(stderr, gettext(
			"Error in writing \"%s\".\n"), output_fname);
		exit(4);
	}

	if (num_of_symbols != 0) {
		sym = sym_head->sym;
	}

	for (i = 0; i < num_of_symbols; i++) {
		sym = lp->sym;
#ifdef DDEBUG
		(void) printf("\tOUTPUT name = (%s)\n", sym->name);
#endif
		if (write(fd, sym, sizeof (CharmapSymbol)) !=
			sizeof (CharmapSymbol)) {
			(void) unlink(output_fname);
			(void) fprintf(stderr, gettext(
				"Error in writing \"%s\".\n"), output_fname);
			exit(4);
		}
		lp = lp->next;
	}
}
