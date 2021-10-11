/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)dump_utils.c	1.4	95/06/22 SMI"

#include "collate.h"

void *start_collate;
char *program;

header *h;
one_to_many *h_otm = NULL;
collating_element *h_clm = NULL;
order *h_order = NULL;
int no_otm = 0;

/*
 * Map in collation file
 */
int
map_in_collate(int fd)
{
	struct stat stat;
	int size;
	int i;
	int num_of_otm = 0;

	if (fstat(fd, &stat) == -1) {
		fprintf(stderr, "%s: stat error.\n",
			program);
		return (ERROR);
	}
	size = stat.st_size;
	start_collate =  mmap(NULL, size,
		PROT_READ, MAP_SHARED, fd, 0);
	if (start_collate == (char *)-1) {
		fprintf(stderr, "%s: mmap failed.\n", program);
		return (ERROR);
	}
	/*
	 * setup table information
	 */
	h = (header *)start_collate;
	for (i = 0; i < h->weight_level; i++)
		num_of_otm += h->num_otm[i];
	if (num_of_otm != 0)
		h_otm = (one_to_many *) ((char *)h + sizeof (header));
	if (h->no_of_coll_elms != 0)
		h_clm = (collating_element *) ((char *)h +
			sizeof (header) +
			num_of_otm * sizeof (one_to_many));
	if (h->no_of_orders != 0)
		h_order = (order *) ((char *)h +
			sizeof (header) +
			num_of_otm * sizeof (one_to_many) +
			h->no_of_coll_elms * sizeof (collating_element));
	return (0);
}

#define	SPACES	"   "
#define	STARS	"***"
char buf[128];
/*
 * dump header section
 */
int
dump_header()
{
	int i;

	printf("Dumping Header Section\n");
	printf("%smagic = 0x%x\n", SPACES, h->magic);
	printf("%sflags = 0x%x\n", SPACES, h->flags);
	printf("%sweight level = %d\n", SPACES, h->weight_level);
	printf("%sweight types = (", SPACES);
	for (i = 0; i < h->weight_level; i++) {
		if (h->weight_types[i] & T_FORWARD)
			printf("forward");
		if (h->weight_types[i] & T_BACKWARD)
			printf("backward");
		if (h->weight_types[i] & T_POSITION)
			printf(",position,");
		printf(";");
	}
	printf(")\n");
	printf("%sno. of otm = (", SPACES);
	for (i = 0; i < h->weight_level; i++) {
		no_otm += h->num_otm[i];
		printf("%d,", h->num_otm[i]);
	}
	printf(")\n");
	printf("%sno. of collating element = %d\n", SPACES,
		h->no_of_coll_elms);
	printf("%sno. of orders = %d\n", SPACES,
		h->no_of_orders);
}

/*
 * Dump one to many section
 */
dump_otm_section()
{
	int i;
	int j;
	one_to_many *p = h_otm;

	printf("Dumping One To Many section\n");
	if (no_otm == 0)
		return (0);
	for (i = 0; i < h->weight_level; i++) {
		if (h->num_otm[i] == 0)
			continue;
		printf("%sWeight Level = %d, No. Of. One TO Many = %d.\n",
			SPACES, i, h->num_otm[i]);
		for (j = 0; j < h->num_otm[i]; j++) {
			dump_one_otm(p);
			++p;
		}
	}
}

dump_one_otm(one_to_many *p)
{
	sprintf(buf, "%s%sSOURCE:", SPACES, STARS);
	dump_encoded(buf, &(p->source));
	printf("\n");
	sprintf(buf, "%s%sTARGET:", SPACES, SPACES);
	dump_encoded(buf, &(p->target));
	printf("\n");
}
/*
 * Dump collating elements section
 */
dump_clm_section()
{
	int cnt = h->no_of_coll_elms;
	collating_element *p = h_clm;

	printf("Dumping Collating Element section\n");
	while (cnt > 0) {
		sprintf(buf, "%sCol.Elm. = <%s> ", SPACES, p->name);
		dump_encoded(buf, &(p->encoded_val));
		++p;
		cnt--;
		printf("\n");
	}
}
/*
 * Dump order section
 */
dump_order_section()
{
	order *o = h_order;
	printf("Dumping Order section\n");
	while (o->type != T_NULL) {
		dump_one_order(o);
		o++;
	}
}

/*
 * Dumping encoded
 */
dump_encoded(char *s, encoded_val *en)
{
	int i;
	printf("%s ", s);
	printf("len=%d, bytes=", en->length);
	for (i = 0; i < en->length; i++) {
		if (isprint(en->bytes[i]))
			printf("(%x,'%c'),", en->bytes[i], en->bytes[i]);
		else
			printf("(%x,'*'),", en->bytes[i]);
	}
}

dump_one_order(order *o)
{
	int i;
	int j;

	printf(SPACES);
	if (o->type & T_CHAR_CHARMAP)
		dump_encoded("MAP  ", &o->encoded_val);
	if (o->type &T_CHAR_ID)
		printf("ID   ");
	if (o->type &T_CHAR_ENCODED)
		dump_encoded("ENCOD", &o->encoded_val);
	if (o->type &T_CHAR_COLL_ELM)
		dump_encoded("C_ELM", &o->encoded_val);
	if (o->type &T_CHAR_COLL_SYM)
		dump_encoded("C_SYM", &o->encoded_val);
	if (o->type &T_ELLIPSIS)
		printf("ELLIP");
	if (o->type &T_UNDEFINED)
		printf("UNDEF");
	printf("  (%d) ", o->r_weight);
	printf("[");
	for (i = 0; i < h->weight_level; i++) {
		switch (o->weights[i].type) {
		case WT_RELATIVE:
			printf("%d,", o->weights[i].u.weight);
			break;
		case WT_ONE_TO_MANY:
			/*
			j = 0;
			lp = head_otm[i];
			while (j < o->weights[i].u.index) {
				lp = lp->next;
				j++;
			}
			dump_encoded("\n\t\totm(s)", &lp->otm->source);
			dump_encoded("\n\t\totm(t)", &lp->otm->target);
			 */
			printf("otm(%d),", o->weights[i].u.index);
			break;
		default:
			printf("*, ");
			break;
		}
	}
	printf("]");
	printf("\n");
}
