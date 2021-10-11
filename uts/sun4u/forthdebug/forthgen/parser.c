
#include "stabs.h"

jmp_buf	resetbuf;

char	*whitesp(), *name(), *id(), *decl(), *number(), *offsize();
char	*tdefdecl(), *intrinsic(), *arraydef();
void	addhash();


parse_input()
{
	char *cp;
	int i = 0;
	static char linebuf[MAXLINE];

	while (i++ < BUCKETS) {
		hash_table[i] = NULL;
		name_table[i] = NULL;
	}

	/*
	 * get a line at a time from the .s stabs file and parse.
	 */
	while (cp = fgets(linebuf, MAXLINE, stdin))
		parseline(cp);
}

/*
 * Parse each line of the .s file (stabs entry) gather meaningful information
 * like name of type, size, offsets of fields etc.
 */
parseline(cp)
	char *cp;
{
	struct tdesc *tdp;
	char c, *w;
	int h, tagdef;
	int debug;

	/*
	 * setup for reset()
	 */
	if (setjmp(resetbuf))
		return;

	/*
	 * Look for lines of the form
	 *	.stabs	"str",n,n,n,n
	 * The part in '"' is then parsed.
	 */
	cp = whitesp(cp);
#define	STLEN	6
	if (strncmp(cp, ".stabs", STLEN) != 0)
		reset();
	cp += STLEN;
#undef STLEN
	cp = whitesp(cp);
	if (*cp++ != '"')
		reset();

	/*
	 * name:type		variable (ignored)
	 * name:ttype		typedef
	 * name:Ttype		struct tag define
	 */
	cp = name(cp, &w);
	switch (c = *cp++) {
	case 't': /* type */
		tagdef = 0;
		break;
	case 'T': /* struct, union, enum */
		tagdef = 1;
		break;
	default:
		reset();
	}

	/*
	 * The type id and definition follow.
	 */
	cp = id(cp, &h);
	if (*cp++ != '=')
		reset();
	if (tagdef) {
		tagdecl(cp, &tdp, h, w);
	} else {
		tdefdecl(cp, &tdp);
		tagadd(w, h, tdp);
	}
}

/*
 * Check if we have this node in the hash table already
 */
struct tdesc *
lookup(int h)
{
	int hash = HASH(h);
	struct tdesc *tdp = hash_table[hash];

	while (tdp != NULL) {
		if (tdp->id == h)
			return (tdp);
		tdp = tdp->hash;
	}
	return (NULL);
}

char *
whitesp(cp)
	char *cp;
{
	char *orig, c;

	orig = cp;
	for (c = *cp++; isspace(c); c = *cp++)
		;
	if (--cp == orig)
		reset();
	return (cp);
}

char *
name(cp, w)
	char *cp, **w;
{
	char *new, *orig, c;
	int len;

	orig = cp;
	c = *cp++;
	if (c == ':')
		*w = NULL;
	else if (isalpha(c) || c == '_') {
		for (c = *cp++; isalnum(c) || c == ' ' || c == '_'; c = *cp++)
			;
		if (c != ':')
			reset();
		len = cp - orig;
		new = (char *)malloc(len);
		while (orig < cp - 1)
			*new++ = *orig++;
		*new = '\0';
		*w = new - (len - 1);
	} else
		reset();
	return (cp);
}

char *
number(cp, n)
	char *cp;
	long *n;
{
	char *next;

	*n = strtol(cp, &next, 10);
	if (next == cp)
		reset();
	return (next);
}

char *
id(cp, h)
	char *cp;
	int *h;
{
	long n1, n2;

	if (*cp++ != '(')
		reset();
	cp = number(cp, &n1);
	if (*cp++ != ',')
		reset();
	cp = number(cp, &n2);
	if (*cp++ != ')')
		reset();
	*h = n1 * 1000 + n2;
	return (cp);
}

tagadd(char *w, int h, struct tdesc *tdp)
{
	struct tdesc *otdp, *hash;

	tdp->name = w;
	if (!(otdp = lookup(h)))
		addhash(tdp, h);
	else if (otdp != tdp) {
		fprintf(stderr, "duplicate entry\n");
		fprintf(stderr, "old: %s %d %d %d\n",
			otdp->name ? otdp->name : "NULL",
			otdp->type, otdp->id / 1000, otdp->id % 1000);
		fprintf(stderr, "new: %s %d %d %d\n",
			tdp->name ? tdp->name : "NULL",
			tdp->type, tdp->id / 1000, tdp->id % 1000);
	}
}

tagdecl(cp, rtdp, h, w)
	char *cp;
	struct tdesc **rtdp;
	int h;
	char *w;
{
	if (*rtdp = lookup(h)) {
		if ((*rtdp)->type != FORWARD)
			fprintf(stderr, "found but not forward: %s \n", cp);
	} else {
		*rtdp = ALLOC(struct tdesc);
		(*rtdp)->name = w;
		addhash(*rtdp, h);
	}

	switch (*cp++) {
	case 's':
		soudef(cp, STRUCT, rtdp);
		break;
	case 'u':
		soudef(cp, UNION, rtdp);
		break;
	case 'e':
		enumdef(cp, rtdp);
		break;
	default:
		reset();
	}
}

char *
tdefdecl(cp, rtdp)
	char *cp;
	struct tdesc **rtdp;
{
	struct tdesc *tdp, *ntdp;
	char *w;
	int c, h;

	/* Type codes */
	switch (*cp) {
	case 'b': /* integer */
		c = *++cp;
		if (c != 's' && c != 'u')
			reset();
		c = *++cp;
		if (c == 'c')
			cp++;
		cp = intrinsic(cp, rtdp);
		break;
	case 'R': /* fp */
		cp += 3;
		cp = intrinsic(cp, rtdp);
		break;
	case '(': /* equiv to another type */
		cp = id(cp, &h);
		ntdp = lookup(h);
		if (ntdp == NULL) {
			if (*cp++ != '=')
				reset();
			cp = tdefdecl(cp, rtdp);
			addhash(*rtdp, h); /* for *(x,y) types */
		} else {
			*rtdp = ALLOC(struct tdesc);
			(*rtdp)->type = TYPEOF;
			(*rtdp)->data.tdesc = ntdp;
		}
		break;
	case '*':
		cp = tdefdecl(cp + 1, &ntdp);
		*rtdp = ALLOC(struct tdesc);
		(*rtdp)->type = POINTER;
		(*rtdp)->size = sizeof (void *);
		(*rtdp)->name = "pointer";
		(*rtdp)->data.tdesc = ntdp;
		break;
	case 'f':
		cp = tdefdecl(cp + 1, &ntdp);
		*rtdp = ALLOC(struct tdesc);
		(*rtdp)->type = FUNCTION;
		(*rtdp)->size = sizeof (void *);
		(*rtdp)->name = "function";
		(*rtdp)->data.tdesc = ntdp;
		break;
	case 'a':
		cp++;
		if (*cp++ != 'r')
			reset();
		*rtdp = ALLOC(struct tdesc);
		(*rtdp)->type = ARRAY;
		(*rtdp)->name = "array";
		cp = arraydef(cp, rtdp);
		break;
	case 'x':
		c = *++cp;
		if (c != 's' && c != 'u' && c != 'e')
			reset();
		cp = name(cp + 1, &w);
		*rtdp = ALLOC(struct tdesc);
		(*rtdp)->type = FORWARD;
		(*rtdp)->name = w;
		break;
	default:
		reset();
	}
	return (cp);
}

char *
intrinsic(cp, rtdp)
	char *cp;
	struct tdesc **rtdp;
{
	struct tdesc *tdp;
	long size;

	cp = number(cp, &size);
	tdp = ALLOC(struct tdesc);
	tdp->type = INTRINSIC;
	tdp->size = size;
	tdp->name = NULL;
	*rtdp = tdp;
	return (cp);
}

soudef(cp, type, rtdp)
	char *cp;
	enum type type;
	struct tdesc **rtdp;
{
	struct mlist *mlp, **prev;
	char *w;
	int h, i = 0;
	long size;
	struct tdesc *tdp;
	char linebuf[MAXLINE];

	cp = number(cp, &size);
	(*rtdp)->size = size;
	(*rtdp)->type = type; /* s or u */

	prev = &((*rtdp)->data.members);
	/* now fill up the fields */
	while ((*cp != '"') && (*cp != ';')) { /* signifies end of fields */
		mlp = ALLOC(struct mlist);
		*prev = mlp;
		cp = name(cp, &w);
		mlp->name = w;
		cp = id(cp, &h);
		/*
		 * find the tdesc struct in the hash table for this type
		 * and stick a ptr in here
		 */
		tdp = lookup(h);
		if (tdp == NULL) { /* not in hash list */
			if (*cp++ != '=')
				reset();
			cp = tdefdecl(cp, &tdp);
			addhash(tdp, h);
		}

		mlp->fdesc = tdp;
		cp = offsize(cp, mlp);
		/* cp is now pointing to next field */
		prev = &mlp->next;
		/* could be a continuation */
		if (*cp == '\\') {
			/* get next line */
			cp = fgets(linebuf, MAXLINE, stdin);
			while (*cp++ != '"')
				;
		}
	}
}

char *
offsize(cp, mlp)
	char *cp;
	struct mlist *mlp;
{
	long offset, size;

	if (*cp++ != ',')
		reset();
	cp = number(cp, &offset);
	if (*cp++ != ',')
		reset();
	cp = number(cp, &size);
	if (*cp++ != ';')
		reset();
	mlp->offset = offset;
	mlp->size = size;
	return (cp);
}

char *
arraydef(char *cp, struct tdesc **rtdp)
{
	int h;
	long start, end;

	cp = id(cp, &h);
	if (*cp++ != ';')
		reset();

	(*rtdp)->data.ardef = ALLOC(struct ardef);
	(*rtdp)->data.ardef->indices = ALLOC(struct element);
	(*rtdp)->data.ardef->indices->index_type = lookup(h);

	cp = number(cp, &start);
	if (*cp++ != ';')
		reset();
	cp = number(cp, &end);
	if (*cp++ != ';')
		reset();
	(*rtdp)->data.ardef->indices->range_start = start;
	(*rtdp)->data.ardef->indices->range_end = end;
	cp = tdefdecl(cp, &((*rtdp)->data.ardef->contents));
	return (cp);
}

enumdef(char *cp, struct tdesc **rtdp)
{
	char *next;
	struct elist *elp, **prev;
	char *w;
	char linebuf[MAXLINE];

	(*rtdp)->type = ENUM;

	prev = &((*rtdp)->data.emem);
	while (*cp != ';') {
		elp = ALLOC(struct elist);
		*prev = elp;
		cp = name(cp, &w);
		elp->name = w;
		cp = number(cp, &elp->number);
		prev = &elp->next;
		if (*cp++ != ',')
			reset();
		if (*cp == '\\') {
			cp = fgets(linebuf, MAXLINE, stdin);
			while (*cp++ != '"')
				;
		}
	}
}

/*
 * Add a node to the hash queues.
 */
void
addhash(tdp, num)
	struct tdesc *tdp;
	int num;
{
	int hash = HASH(num);

	tdp->id = num;
	tdp->hash = hash_table[hash];
	hash_table[hash] = tdp;

	if (tdp->name) {
		hash = compute_sum(tdp->name);
		tdp->next = name_table[hash];
		name_table[hash] = tdp;
	}
}

struct tdesc *
lookupname(name)
	char *name;
{
	int hash = compute_sum(name);
	struct tdesc *tdp, *ttdp = NULL;

	for (tdp = name_table[hash]; tdp != NULL; tdp = tdp->next) {
		if (tdp->name != NULL && strcmp(tdp->name, name) == 0) {
			if (tdp->type == STRUCT || tdp->type == UNION ||
			    tdp->type == ENUM)
				return (tdp);
			if (tdp->type == TYPEOF)
				ttdp = tdp;
		}
	}
	return (ttdp);
}

int
compute_sum(char *w)
{
	char c;
	int sum;

	for (sum = 0; c = *w; sum += c, w++)
		;
	return (HASH(sum));
}

reset()
{
	longjmp(resetbuf, 1);
	/* NOTREACHED */
}
