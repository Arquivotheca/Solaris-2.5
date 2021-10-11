/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)utils.c	1.40	95/09/22 SMI"

#include "collate.h"
#include "extern.h"
#include <unistd.h>

typedef struct link_coll_elm {
	collating_element		*coll_elm;
	struct link_coll_elm	*next;
} link_coll_elm;

static link_coll_elm	*head_collating_element = NULL;
static int		no_of_coll_elms = 0;

typedef struct link_order {
	order	*order;
	struct link_order	*next;
	struct link_order	*prev;
} link_order;

static link_order	*head_order = NULL;
static int		no_of_orders = 0;

struct order_list {
	link_order	*head;
	link_order	*tail;
};

static struct order_list	order_list = {NULL, NULL};

typedef struct link_otm {
	one_to_many		*otm;
	struct link_otm	*next;
} link_otm;

static link_otm	*head_otm[MAX_WEIGHTS] =
	{NULL, NULL, NULL, NULL, NULL};

static link_otm	*tail_otm[MAX_WEIGHTS] =
	{NULL, NULL, NULL, NULL, NULL};

static int	num_otm[MAX_WEIGHTS] =
	{0, 0, 0, 0, 0};

struct order_with_missing_weight {
	order					*o;
	int					level;
	encoded_val				encoded_val;
	struct order_with_missing_weight	*next;
};

struct order_with_missing_weight *missing_weight_head = NULL;

static link_order	*ins_order(order *);
static collating_element	*get_collating_element(char *);
static collating_symbol		*get_collating_symbol(char *);
static int	set_relative_weight(link_order *);
static int	order_diff(order *, link_order *);
static int	ins_collating_element(char *, encoded_val *);
static int	ins_collating_symbol(char *);
static int	CheckSymbol(char *);
static int	get_weight(encoded_val *, unsigned int *);
static one_to_many	*set_ont_to_many(order *, encoded_val *, int);
static int 	save_missing_weight(order *order, int level,
					encoded_val encoded_val);


extern void		execerror(char *, ...);
extern void		copy_encoded_string(unsigned char *, encoded_val *);
extern void		copy_encoded(encoded_val *, encoded_val *);
extern void		undefined(char *, char *, int, int *);
extern int		bytescmp(unsigned char *, unsigned char *, int);
extern int		bytescopy(unsigned char *, unsigned char *, int);
extern int		cmp_encoded(encoded_val *, encoded_val *);
extern int		get_encoded_value(unsigned int *, encoded_val *);

/*
 * Symbol name checker
 */
static int
CheckSymbol(char *id)
{
	int		ret = 0;
	if (get_charmap_sym(id) != NULL) {
		ret++;
	}
	if (get_collating_element(id) != NULL) {
		ret++;
	}
	if (get_collating_symbol(id) != NULL) {
		ret++;
	}
	return (ret);
}

/*
 * Handling collating_element
 */
int
set_collating_elms(char *id, encoded_val *en)
{
	if (CheckSymbol(id)) {
		execerror(gettext(
			"collating_element: symbol already defined.\n"));
	} else if (en == NULL) {
		execerror(gettext(
			"collating_element: quoted_string error.\n"));
	} else if (ins_collating_element(id, en) == ERROR) {
		execerror(gettext(
			"Can not register collating_element.\n"));
	}

	return (0);
}
/*
 * Look up collating_element list
 */
static collating_element *
get_collating_element(char *id)
{
	link_coll_elm	*p = head_collating_element;

	while (p != NULL) {
		if (strcmp(p->coll_elm->name, id) == 0) {
			return (p->coll_elm);
		}
		p = p->next;
	}
	return (NULL);
}

/*
 * Alloc/Insert collating_element
 */
#define	NEW_LENGTH	en->length
#define	CUR_LENGTH	cur->coll_elm->encoded_val.length
#define	NEW_BYTES 	en->bytes
#define	CUR_BYTES 	cur->coll_elm->encoded_val.bytes
#define	CUR_NAME	cur->coll_elm->name

static int
ins_collating_element(char *id, encoded_val *en)
{
	collating_element	*new;
	link_coll_elm	*cur = head_collating_element;
	link_coll_elm	*prev;
	link_coll_elm	*nl;

	if (en == NULL) {
		return (ERROR);
		/* NOTREACHED */
	}
	new = MALLOC_COLLATING_ELEMENT;
	if (new == NULL) {
		return (ERROR);
		/* NOTREACHED */
	} else {
		(void) memset(new, 0, sizeof (collating_element));
	}
	nl = (link_coll_elm *)malloc(sizeof (link_coll_elm));
	if (nl == NULL) {
		free(new);
		return (ERROR);
		/* NOTREACHED */
	} else {
		(void) memset(nl, 0, sizeof (link_coll_elm));
	}
	(void) strcpy(new->name, id);
	copy_encoded(&new->encoded_val, en);
	nl->coll_elm = new;

	/*
	 * Insert in the list.
	 *	Sorted by byte length.
	 */
	if (head_collating_element == NULL) {
		head_collating_element = nl;
		nl->next = NULL;
	} else {
		prev = NULL;
		while (CUR_LENGTH <= NEW_LENGTH) {
			if (CUR_LENGTH == NEW_LENGTH) {
				if (bytescmp(NEW_BYTES, CUR_BYTES,
					CUR_LENGTH) < 0) {
					break;
				}
			}
			prev = cur;
			cur = cur->next;
			if (cur == NULL) {
				break;
			}
		}
		if (prev != NULL) {
			prev->next = nl;
			nl->next = cur;
		} else {
			nl->next = cur;
			head_collating_element = nl;
		}
	}
	++no_of_coll_elms;
	return (0);
}

/*
 * Handling collating_symbol
 */
int
set_collating_syms(char *id)
{
	if (CheckSymbol(id)) {
		execerror(gettext(
			"collating_symbol: symbol already defined.\n"));
	} else if (ins_collating_symbol(id) == ERROR) {
		execerror(gettext(
			"Can not register collating_symbol.\n"));
	}
	return (0);
}

static collating_symbol *
get_collating_symbol(char *id)
{
	collating_symbol	*p = head_collating_symbol;
	int	diff;

	while (p != NULL) {
		if ((diff = strcmp(id, p->name)) < 0) {
			break;
		} else if (diff == 0) {
			return (p);
		}
		p = p->next;
	}
	return (NULL);
}

/*
 * insert/alloc
 */
static int
ins_collating_symbol(char *id)
{
	collating_symbol	*new;
	collating_symbol	*cur = head_collating_symbol;
	collating_symbol 	*prev;

	new = MALLOC_COLLATING_SYMBOL;
	if (new == NULL) {
		return (ERROR);
		/* NOTREACHED */
	} else {
		(void) memset(new, 0, sizeof (collating_symbol));
	}
	(void) strcpy(new->name, id);
	/*
	 * Insert in the list.
	 *	Sorted by name
	 */
	if (head_collating_symbol == NULL) {
		head_collating_symbol = new;
		new->next = NULL;
	} else {
		prev = NULL;
		while (strcmp(cur->name, id) < 0) {
			prev = cur;
			cur = cur->next;
			if (cur == NULL) {
				break;
			}
		}
		if (prev != NULL) {
			prev->next = new;
			new->next = cur;
		} else {
			new->next = cur;
			head_collating_symbol = new;
		}
	}
	return (0);
}


/*
 * Handling order and weight
 */
#define	HAS_WEIGHT\
	(T_CHAR_CHARMAP|T_CHAR_ID|\
	T_CHAR_ENCODED|T_CHAR_COLL_ELM|T_UNDEFINED)

int
adjust_weight(order *o, int level)
{
	if (o == NULL) {
		return (ERROR);
		/* NOTREACHED */
	}
	if ((weight_level > level) && (o->type & HAS_WEIGHT)) {
		while (level < weight_level) {
			o->weights[level].u.weight = o->r_weight;
			o->weights[level].type = WT_RELATIVE;
			level++;
		}
	}
	return (0);
}

void
do_order_opt()
{
	if (weight_types[weight_level] == 0) {
		weight_types[weight_level] = T_FORWARD;
	}
	weight_level++;
	/*
	 * over flow check needed
	 */
}

int
set_coll_identifier(order *o)
{
	link_order	*nl;
	if (o == NULL) {
		return (ERROR);
		/* NOTREACHED */
	} else if ((nl = ins_order(o)) == (link_order *)ERROR) {
		return (ERROR);
		/* NOTREACHED */
	} else if (set_relative_weight(nl) == ERROR) {
		return (ERROR);
		/* NOTREACHED */
	}
	if (o->type & T_CHAR_COLL_SYM) {
		int		i;
		unsigned char	name[MAX_ID_LENGTH];
		collating_symbol	*pcs;

		for (i = 0; i < o->encoded_val.length; i++) {
			name[i] = o->encoded_val.bytes[i];
		}
		name[i] = 0;
		pcs = get_collating_symbol((char *) name);
		if (pcs == NULL) {
			return (NULL);
		}
		pcs->r_weight = o->r_weight;
	}
	return (1);
}

link_order *
ins_order(order *new)
{
	link_order	*nl;

	nl = (link_order *)malloc(sizeof (link_order));
	if (nl == NULL) {
		return ((link_order *)ERROR);
		/* NOTREACHED */
	} else {
		(void) memset(nl, 0, sizeof (link_order));
	}
	nl->order = new;

	if (order_list.head == NULL) {
		order_list.head = nl;
		head_order = nl;
		nl->prev = NULL;
	} else {
		order_list.tail->next = nl;
		nl->prev = order_list.tail;
	}
	order_list.tail = nl;
	nl->next = NULL;
	return (nl);
}

order *
alloc_order(int type, union args *args)
{
	order	*new;
	CharmapSymbol		*charsym;
	collating_element	*coll_elm;
	encoded_val	*en;
	int	wtype;
	int	i;

	new = MALLOC_ORDER;
	if (new == NULL) {
		return (NULL);
		/* NOTREACHED */
	} else {
		(void) memset(new, 0, sizeof (order));
	}
	new->type = type;
	for (i = 0; i < MAX_WEIGHTS; i++) {
		new->weights[i].u.index = 0;
	}
	switch (type) {
	case T_CHAR_ENCODED:
		new->encoded_val.length = args->en->length;
		for (i = 0; i < args->en->length; i++) {
			new->encoded_val.bytes[i] = args->en->bytes[i];
		}
		wtype = WT_RELATIVE;
		break;
	case T_CHAR_ID:
		new->encoded_val.length = strlen(args->id);
		(void) strcpy((char *)new->encoded_val.bytes, args->id);
		wtype = WT_RELATIVE;
		break;
	case T_CHAR_CHARMAP:
		if ((charsym = get_charmap_sym(args->id)) != NULL) {
			en = &charsym->en_val;
		} else if (get_collating_symbol(args->id) != NULL) {
			new->type = T_CHAR_COLL_SYM;
			new->encoded_val.length = strlen(args->id);
			(void) strcpy((char *)new->encoded_val.bytes, args->id);
		} else if ((coll_elm = get_collating_element(args->id)) !=
				NULL) {
			new->type = T_CHAR_COLL_ELM;
			en = &coll_elm->encoded_val;
		} else {
			undefined("LC_COLLATE", args->id, lineno, &exec_errors);
			return (NULL);
		}
		if (new->type != T_CHAR_COLL_SYM) {
			copy_encoded(&new->encoded_val, en);
		}
		wtype = WT_RELATIVE;
		break;
	case T_ELLIPSIS:
		/*
		 * get_weight() in libcollate.c uses this.
		 */
		new->encoded_val.length = no_of_orders;
		wtype = WT_ELLIPSIS;
		break;
	case T_UNDEFINED:
		wtype = WT_RELATIVE;
		break;
	default:
		/*
		 * should not happen. Internal error.
		 */
		return (NULL);
	}
	if (new->type != T_CHAR_COLL_SYM) {
		++no_of_orders;
	}
	for (i = 0; i < MAX_WEIGHTS; i++) {
		new->weights[i].type = wtype;
	}
	return (new);
}

static int
set_relative_weight(link_order *ln)
{
	unsigned int	begin;
	order	*o;
	int		diff;

	o = ln->order;

	if (ln->prev == NULL) {
		o->r_weight = MIN_WEIGHT;
	} else if ((ln->prev->order->type & T_ELLIPSIS) == 0) {
		o->r_weight = ln->prev->order->r_weight + 1;
	} else {
		/*
		 * The previous one is "...".
		 */
		diff = order_diff(o, ln->prev->prev);
		if (diff == ERROR || diff < 0) {
			return (ERROR);
			/* NOTREACHED */
		}
		if (ln->prev->prev == NULL) {
			begin = 0;
		} else {
			begin = ln->prev->prev->order->r_weight;
		}
		o->r_weight = begin + diff;
	}
	return (0);
}

/*
 *	<b>
 *	...
 *	<a>
 */
static int
order_diff(order *a, link_order *lb)
{
	unsigned int	a_val;
	unsigned int	b_val;
	encoded_val	*en;
	int		diff;
	order	*b;

	if ((a->type & T_CHAR_COLL_ELM) ||
	    (a->type & T_CHAR_COLL_SYM) ||
	    (a->type & T_ELLIPSIS) ||
	    (a->type & T_UNDEFINED)) {
		return (ERROR);
		/* NOTREACHED */
	}
	if (lb != NULL) {
		b = lb->order;
		if ((b->type & T_CHAR_COLL_ELM) ||
		    (b->type & T_CHAR_COLL_SYM) ||
		    (b->type & T_ELLIPSIS) ||
		    (b->type & T_UNDEFINED)) {
			return (ERROR);
			/* NOTREACHED */
		}
	}

	/*
	 * Get the value of 'b'.
	 */
	if (lb == NULL) {
		b_val = 0;
		goto next;
	}
	switch (b->type) {
	case T_CHAR_CHARMAP:
		en = &b->encoded_val;
		break;
	case T_CHAR_ID:
	case T_CHAR_ENCODED:
		en = &b->encoded_val;
		break;
	default:
		return (ERROR);
		/* NOTREACHED */
	}
	(void) get_encoded_value(&b_val, en);
next:
	/*
	 * Get the value of 'a'.
	 */
	switch (a->type) {
	case T_CHAR_CHARMAP:
		en = &a->encoded_val;
		break;
	case T_CHAR_ID:
	case T_CHAR_ENCODED:
		en = &a->encoded_val;
		break;
	default:
		return (ERROR);
		/* NOTREACHED */
	}
	(void) get_encoded_value(&a_val, en);

	diff = a_val - b_val;
#ifdef DEBUG
printf("order_diff:(diff,a,b)=(%d,%d,%d)\n", diff, a_val, b_val);
#endif
	return (diff);
}

int
set_weight(char wt_type, int my_type, union args *arg,
		order *o, int level)
{
	int		ret = 0;
	unsigned int	weight;
	encoded_val		en;
	encoded_val		*ep;
	one_to_many		*o_t_m;
	CharmapSymbol	*charsym;
	collating_symbol	*coll_sym;
	collating_element	*coll_elm;

	if (o == NULL) {
		return (ERROR);
		/* NOTREACHED */
	}
	o->weights[level].type = wt_type;
	switch (wt_type) {
	case WT_RELATIVE:
		switch (my_type) {
		case T_CHAR_ENCODED:
			if ((ret = get_weight(arg->en, &weight)) == ERROR)
				if (save_missing_weight(o, level, *arg->en) ==
									ERROR)
					ret = ERROR;
			break;
		case T_CHAR_ID:
			en.length = strlen(arg->id);
			(void) bytescopy(en.bytes, (unsigned char *)arg->id,
				en.length);
			if ((ret = get_weight(&en, &weight)) == ERROR)
				if (save_missing_weight(o, level, en) == ERROR)
					ret = ERROR;
			break;
		case T_CHAR_CHARMAP:
			if ((charsym = get_charmap_sym(arg->id)) != NULL) {
				ep = &charsym->en_val;
			} else if ((coll_elm = get_collating_element(arg->id))
				!= NULL) {
				ep = &coll_elm->encoded_val;
			} else if ((coll_sym = get_collating_symbol(arg->id)) !=
					NULL) {
				weight = coll_sym->r_weight;
				break;
			} else {
				return (ERROR);
				/* NOTREACHED */
			}
			if ((ret = get_weight(ep, &weight)) == ERROR)
				if (save_missing_weight(o, level, *ep) == ERROR)
					ret = ERROR;
			break;
		default:
			/*
			 * internal error
			 */
			(void) fprintf(stderr, gettext(
				"Internal error in set_weight().\n"));
			ret = ERROR;
		}
		if (ret != ERROR) {
			o->weights[level].u.weight = weight;
		}
		break;
	case WT_ONE_TO_MANY:
		o_t_m = set_ont_to_many(o, arg->en, level);
		if (o_t_m == (one_to_many *)ERROR) {
			ret = ERROR;
		} else {
			o->weights[level].u.index = num_otm[level] - 1;
		}
		break;
	case WT_IGNORE:
		break;
	case WT_ELLIPSIS:
		if (o->type != T_ELLIPSIS) {
			execerror(gettext(
				"ELLIPSIS illegally specified.\n"));
			ret = ERROR;
		}
		break;
	default:
		/*
		 * internal error
		 */
		(void) fprintf(stderr, "Internal error in set_weight().\n");
		ret = ERROR;
		break;
	}
	return (ret);
}

static int
get_weight(encoded_val *en, unsigned int *w)
{
	link_order	*lp = head_order;
	order		*op;

	while (lp != NULL) {
		op = lp->order;
		if (op == NULL) {
			return (ERROR);
			/* NOTREACHED */
		}
		if (cmp_encoded(en, &op->encoded_val) == 0) {
			*w = op->r_weight;
			return (0);
			/* NOTREACHED */
		}
		lp = lp->next;
	}
	/*
	 * could not find a matching one.
	 */
	return (ERROR);
}

static one_to_many *
set_ont_to_many(order *o, encoded_val *en, int level)
{
	one_to_many	*otm;
	link_otm	*l;

	otm = MALLOC_OTM;
	if (otm == NULL) {
		return (NULL);
		/* NOTREACHED */
	} else {
		(void) memset(otm, 0, sizeof (one_to_many));
	}
	copy_encoded(&otm->target, en);
	copy_encoded(&otm->source, &o->encoded_val);

	l = (link_otm *)malloc(sizeof (link_otm));
	if (l == NULL) {
		free(otm);
		return ((one_to_many *)ERROR);
		/* NOTREACHED */
	} else {
		(void) memset(l, 0, sizeof (link_otm));
	}
	l->otm = otm;
	/*
	 * insert in the linked list.
	 *	unique-ness checking not needed, because o is checked.
	 */
	l->next = NULL;
	if (head_otm[level] == NULL) {
		head_otm[level] = l;
	} else {
		tail_otm[level]->next = l;
	}
	tail_otm[level] = l;
	++num_otm[level];
	return (otm);
}


/*
 * Output processiong
 */

/*
 * output structure
 *	char weight_level		# weight levels
 *	char weight_types[MAX_WEIGHTS];	# forward/backward/position
 *	int num_otm[MAX_WEIGHTS]	# no. of one-to-many
 *	int no_of_coll_elms		# no. of collation elements
 *	int no_of_orders		# no. of orders

 *	one_to_many's			# one-to-many's
 *		lev.1			# level 1
 *		 |			#
 *		lev. MAX_WEIGHTS	# level max_weights
 *	element's			# collating elements
 *	order's				# orders
 */

int
output(char *f_name, unsigned int sflags, char *loc)
{
	int	fd;
	int	i;
	link_otm	*l_otm;
	link_coll_elm	*l_elm;
	link_order	*l_o;
	header	header;
	order	dummy;
	unsigned int flags = USE_FULL_XPG4;
	int hash_ret;

	flags |= sflags;
	(void) memset(&dummy, 0, sizeof (order));
	(void) memset(&header, 0, sizeof (header));
	header.magic = PLAIN;
	header.flags = flags;
	header.weight_level = weight_level;
	for (i = 0; i < MAX_WEIGHTS; i++) {
		header.weight_types[i] = weight_types[i];
		header.num_otm[i] = num_otm[i];
	}
	header.no_of_coll_elms = no_of_coll_elms;
	header.no_of_orders = no_of_orders + 1;

	/*
	 * create output file.
	 */
	fd = creat(f_name, 0777);
	if (fd == -1) {
		(void) fprintf(stderr, gettext(
			"%s: could not create output file '%s'.\n"),
			program, f_name);
		return (ERROR);
		/* NOTREACHED */
	}

	/*
	 * write header
	 */
	if (write(fd, (char *)&header, sizeof (header)) != sizeof (header)) {
		(void) fprintf(stderr, gettext(
			"Error in writing \"%s\".\n"), program);
		return (ERROR);
		/* NOTREACHED */
	}

	/*
	 * write one_to_many information
	 */
	for (i = 0; i < MAX_WEIGHTS; i++) {
		l_otm = head_otm[i];
		while (l_otm != NULL) {
			if (write(fd, (char *)l_otm->otm,
					sizeof (one_to_many)) !=
					sizeof (one_to_many)) {
				(void) fprintf(stderr, gettext(
					"Error in writing \"%s\".\n"), program);
				return (ERROR);
				/* NOTREACHED */
			}
			l_otm = l_otm->next;
		}
	}

	/*
	 * write collating elements
	 */
	l_elm = head_collating_element;
	while (l_elm != NULL) {
		if (write(fd, (char *)l_elm->coll_elm,
				sizeof (collating_element)) !=
				sizeof (collating_element)) {
			(void) fprintf(stderr, gettext(
				"Error in writing \"%s\".\n"), program);
			return (ERROR);
			/* NOTREACHED */
		}
		l_elm = l_elm->next;
	}

	/*
	 * write order information
	 */
	hash_ret = hash_init(loc);
	if (hash_ret == ERROR) {
		(void) fprintf(stderr, gettext(
			"localedef: Could not generate the hash table.\n"));
		return (ERROR);
	}
	l_o = head_order;
	while (l_o != NULL) {
		if (l_o->order->type != T_CHAR_COLL_SYM) {
			if (write(fd, (char *)l_o->order, sizeof (order)) !=
				sizeof (order)) {
				(void) fprintf(stderr, gettext(
					"Error in writing \"%s\".\n"),
					program);
				return (ERROR);
				/* NOTREACHED */
			}
		}
		if (hash_ret != ERROR) {
			int ret;
			ret = hash_order(l_o->order);
		}
		l_o = l_o->next;
	}
	if (hash_ret != ERROR)
		hash_fin();
	dummy.type = T_NULL;
	if (write(fd, (char *)&dummy, sizeof (order)) != sizeof (order)) {
		(void) fprintf(stderr, gettext(
			"Error in writing \"%s\".\n"), program);
		return (ERROR);
		/* NOTREACHED */
	}
	return (0);
}


#ifdef DEBUG
/*
 * Debugging functions
 */
dump_collating_element()
{
	link_coll_elm *p = head_collating_element;

	printf("DUMPing collating elements.\n");
	while (p) {
		int i;
		int max;
		int length;

		length = p->coll_elm->encoded_val.length;
		printf("	DUMPING collating_element('%s',%d)",
				p->coll_elm->name, length);
		if (length > 5)
			max = 5;
		else
			max = length;
		for (i = 0; i < max; i++)
			printf("0x%x,",
			p->coll_elm->encoded_val.bytes[i]);
		printf("\n");
		p = p->next;
	}
}

dump_collating_symbol()
{
	collating_symbol *p = head_collating_symbol;

	while (p) {
		printf("	DUMPING collating_symbol('%s')\n", p->name);
		p = p->next;
	}
}

dump_one_order(order *o)
{
	int i;
	int j;
	link_otm *lp;

	printf("  ");
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
	printf("	(%d) ", o->r_weight);
	printf("[");
	for (i = 0; i < weight_level; i++) {
		switch (o->weights[i].type) {
		case WT_RELATIVE:
			printf("%d,", o->weights[i].u.weight);
			break;
		case WT_ONE_TO_MANY:
			j = 0;
			lp = head_otm[i];
			while (j < o->weights[i].u.index) {
				lp = lp->next;
				j++;
			}
			dump_encoded("\n\t\totm(s)", &lp->otm->source);
			dump_encoded("\n\t\totm(t)", &lp->otm->target);
			break;
		default:
			printf("*, ");
			break;
		}
	}
	printf("]");
	printf("\n");
}

dump_order()
{
	link_order *o = head_order;
	printf("DUMPING ORDER\n");
	while (o != NULL) {
		dump_one_order(o->order);
		o = o->next;
	}
}

dump_weights()
{
	int i;
	printf(" WEIGHT INFORMATION\n");
	printf("	weight_level = %d, (", weight_level);
	for (i = 0; i < weight_level; i++) {
		if (weight_types[i] & T_FORWARD)
			printf("forward");
		if (weight_types[i] & T_BACKWARD)
			printf("backward");
		if (weight_types[i] & T_POSITION)
			printf(" position");
		printf(";");
	}
	printf(")\n");
}
#endif


int
fillin_missing_weights()
{
	unsigned int				weight;
	struct order_with_missing_weight	*m_w;


	for (m_w = missing_weight_head; m_w != NULL; m_w = m_w->next) {

		/* lookup the missing weight */

		if (get_weight(&m_w->encoded_val, &weight)
								== ERROR) {
			/*
			 ***after 2.5 ships then uncomment this
			(void) fprintf(stderr, gettext(
				"%wc missing on left hand side\n"),
				m_w->encoded_val.bytes);
			 */
			exec_errors++;
			return (ERROR);
		}

		/* fill in the missing weight */

		m_w->o->weights[m_w->level].u.weight = weight;
#ifdef DEBUG
		(void) fprintf(stderr,
				"missing weight for %c is %c with weight %d\n",
				m_w->o->encoded_val.bytes[0],
				m_w->encoded_val.bytes[0],
				weight);
#endif
	}

	return (0);
}


int
save_missing_weight(order *order, int level, encoded_val encoded_val)
{
	struct order_with_missing_weight	*ptr;


	if ((ptr = (struct order_with_missing_weight *)
		malloc(sizeof (struct order_with_missing_weight))) == NULL) {
		/*
		 ****uncomment after Solaris 2.5 ships
		fprintf(stderr, gettext(
			"Can't allocate memory for missing weight list\n"));
		*/
		exec_errors++;
		return (ERROR);
	}

	/*
	 * add missing weight order to the list
	 */

	ptr->next = missing_weight_head;
	missing_weight_head = ptr;

	/*
	 * fill in the entries
	 */

	ptr->o = order;
	ptr->level = level;
	ptr->encoded_val = encoded_val;

#ifdef DEBUG
	fprintf(stderr, "%c has missing weight for %c\n",
		order->encoded_val.bytes[0], encoded_val.bytes[0]);
#endif

	return (0);
}
