
#include <math.h>
#include "stabs.h"

char	*convert_format();

int	line;

main(int argc, char **argv)
{
	parse_input();
	get_dbgs(argc, argv);
	exit(0);
}

/*
 * This routine will read the .dbg files and build a list of the structures
 * and fields that user is interested in. Any struct specified will get all
 * its fields included. If nested struct needs to be printed - then the
 * field name and name of struct type needs to be included in the next line.
 */
get_dbgs(int argc, char **argv)
{
	FILE *fp;

	for (argc--, argv++; argc != 0; argc--, argv++) {
		if ((fp = fopen(*argv, "r")) == NULL)
			fprintf(stderr, "Cannot open %s\n", *argv);
		/* add all types in this file to our table */
		parse_dbg(fp);
	}
}

char *
namex(char *cp, char **w)
{
	char *new, *orig, c;
	int len;

	for (c = *cp++; isspace(c); c = *cp++)
		;
	orig = --cp;
	c = *cp++;
	if (isalpha(c) || ispunct(c)) {
		for (c = *cp++; isalnum(c) || ispunct(c); c = *cp++)
			;
		len = cp - orig;
		new = (char *)malloc(len);
		while (orig < cp - 1)
			*new++ = *orig++;
		*new = '\0';
		*w = new - (len - 1);
	} else
		fprintf(stderr, "line %d has bad character %c\n", line, c);

	return (cp);
}

/*
 * checks to see if this field in the struct was requested for by user
 * in the .dbg file.
 */
struct child *
find_child(struct node *np, char *w)
{
	struct child *chp;

	for (chp = np->child; chp != NULL; chp = chp->next) {
		if (strcmp(chp->name, w) == 0)
			return (chp);
	}
	return (NULL);
}

struct tdesc *
find_member(struct tdesc *tdp, char *name)
{
	struct mlist *mlp;

	while (tdp->type == TYPEOF)
		tdp = tdp->data.tdesc;
	if (tdp->type != STRUCT && tdp->type != UNION)
		return (NULL);
	for (mlp = tdp->data.members; mlp != NULL; mlp = mlp->next)
		if (strcmp(mlp->name, name) == 0)
			return (mlp->fdesc);
	return (NULL);
}

/*
 * add this field to our table of structs/fields that the user has
 * requested in the .dbg files
 */
addchild(char *cp, struct node *np)
{
	struct child *chp;
	char *w;

	chp = ALLOC(struct child);
	cp = namex(cp, &w);
	chp->name = w;
	cp = namex(cp, &w);
	chp->format = w;
	chp->next = np->child;
	np->child = chp;
}

/*
 * add this struct to our table of structs/fields that the user has
 * requested in the .dbg files
 */
struct node *
getnode(char *cp)
{
	char *w;
	struct node *np;
	int sum = 0;

	cp = namex(cp, &w);
	np = ALLOC(struct node);
	np->name = w;
	np->child = NULL;
	return (np);
}

/*
 * Format for .dbg files should be
 * Ex:
 * seg
 *	as		s_as
 * if you wanted the contents of "s_as" (a pointer) to be printed in
 * the format of a "as"
 */
parse_dbg(FILE *sp)
{
	char *cp;
	struct node *np;
	static char linebuf[MAXLINE];
	int copy_flag = 0;

	/* grab each line and add them to our table */
	for (line = 1; cp = fgets(linebuf, MAXLINE, sp); line++) {
		if (*cp == '\n') {
			if (copy_flag)
				printf("\n");
			continue;
		}
		if (*cp == '\\')
			continue;
		if (strcmp(cp, "forth_start\n") == 0) {
			copy_flag = 1;
			continue;
		}
		if (strcmp(cp, "forth_end\n") == 0) {
			copy_flag = 0;
			continue;
		}
		if (copy_flag) {
			printf("%s", cp);
			continue;
		}
		np = getnode(cp);
		for (line++;
		    (cp = fgets(linebuf, MAXLINE, sp)) && *cp != '\n';
		    line++) {
			/* members of struct, union or enum */
			addchild(cp, np);
		}
		printnode(np);
	}
}

printnode(struct node *np)
{
	struct tdesc *tdp;

	tdp = lookupname(np->name);
	if (tdp == NULL) {
		char *member;
		struct tdesc *ptdp;

		if (member = strchr(np->name, '.')) {
			*member = '\0';
			ptdp = lookupname(np->name);
			if (ptdp != NULL)
				tdp = find_member(ptdp, member + 1);
			*member = '.';
		}
		if (tdp == NULL) {
			fprintf(stderr, "Can't find %s\n", np->name);
			return;
		}
	}
again:
	switch (tdp->type) {
	case STRUCT:
	case UNION:
		do_sou(tdp, np);
		break;
	case ENUM:
		do_enum(tdp, np);
		break;
	case TYPEOF:
		tdp = tdp->data.tdesc;
		goto again;
	default:
		fprintf(stderr, "%s isn't aggregate\n", np->name);
		break;
	}
}

do_sou(struct tdesc *tdp, struct node *np)
{
	struct mlist *mlp;
	struct child *chp;
	char *name, *format;

	printf("\n");
	printf("vocabulary %s-words\n", np->name);
	printf("%x ' %s-words c-struct .%s\n",
		tdp->size, np->name, np->name);
	printf("also %s-words definitions\n\n", np->name);

	/*
	 * Run thru all the fields of a struct and print them out
	 */
	for (mlp = tdp->data.members; mlp != NULL; mlp = mlp->next) {
		/*
		 * If there's a child list, only print those members.
		 */
		if (np->child) {
			chp = find_child(np, mlp->name);
			if (chp == NULL)
				continue;
			format = chp->format;
		} else
			format = NULL;
		switch_on_type(mlp, mlp->fdesc, format, 0);
	}
	printf("\nkdbg-words definitions\n");
	printf("previous\n\n");
	printf("\\ end %s section\n\n", np->name);
}

do_enum(struct tdesc *tdp, struct node *np)
{
	int nelem = 0;
	struct elist *elp;

	printf("\n");
	for (elp = tdp->data.emem; elp != NULL; elp = elp->next) {
		printf("here ,\" %s\" %x\n", elp->name, elp->number);
		nelem++;
	}
	printf("%x c-enum .%s\n", nelem, np->name);
}

switch_on_type(struct mlist *mlp, struct tdesc *tdp, char *format, int level)
{
	switch (tdp->type) {

	case INTRINSIC:
		print_intrinsic(mlp, tdp, format, level);
		break;
	case POINTER:
		print_pointer(mlp, tdp, format, level);
		break;
	case ARRAY:
		print_array(mlp, tdp, format, level);
		break;
	case FUNCTION:
		print_function(mlp, tdp, format, level);
		break;
	case UNION:
		print_union(mlp, tdp, format, level);
		break;
	case ENUM:
		print_enum(mlp, tdp, format, level);
		break;
	case FORWARD:
		print_forward(mlp, tdp, format, level);
		break;
	case TYPEOF:
		print_typeof(mlp, tdp, format, level);
		break;
	case STRUCT:
		print_struct(mlp, tdp, format, level);
		break;
	default:
		fprintf(stderr, "Switch to Unknown type\n");
		break;
	}
}

print_forward(struct mlist *mlp, struct tdesc *tdp, char *format, int level)
{
	fprintf(stderr, "%s never defined\n", mlp->name);
}

print_typeof(struct mlist *mlp, struct tdesc *tdp, char *format, int level)
{
	switch_on_type(mlp, tdp->data.tdesc, format, level);
}

print_intrinsic(struct mlist *mlp, struct tdesc *tdp, char *format, int level)
{
	format = convert_format(format, ".x");

	if (level != 0)
		switch (tdp->size) {
		case 1:
			printf("' c@ ' %s", format);
			break;
		case 2:
			printf("' w@ ' %s", format);
			break;
		case 4:
			printf("' l@ ' %s", format);
			break;
		case 8:
			printf("' x@ ' %s", format);
			break;
		}
	/*
	 * Check for bit field.
	 */
	else if ((mlp->size % 8) != 0 || (mlp->offset % mlp->size) != 0) {
		int offset, shift, mask;

		offset = (mlp->offset / 32) * 4;
		shift = 32 - ((mlp->offset % 32) + mlp->size);
		mask = ((int)pow(2, mlp->size) - 1) << shift;
		printf("' %s %x %x %x bits-field %s\n",
			format, shift, mask, offset, mlp->name);
	} else {
		switch (tdp->size) {
		case 1:
			printf("' %s %x byte-field %s\n",
				format, mlp->offset / 8, mlp->name);
			break;
		case 2:
			printf("' %s %x short-field %s\n",
				format, mlp->offset / 8, mlp->name);
			break;
		case 4:
			printf("' %s %x long-field %s\n",
				format, mlp->offset / 8, mlp->name);
			break;
		case 8:
			printf("' %s %x ext-field %s\n",
				format, mlp->offset / 8, mlp->name);
			break;
		}
	}
}

print_pointer(struct mlist *mlp, struct tdesc *tdp, char *format, int level)
{
	format = convert_format(format, ".x");
	if (level != 0)
		printf("' l@ ' %s", format);
	else {
		printf("' %s %x ptr-field %s\n",
			format, mlp->offset / 8, mlp->name);
	}
}

print_array(struct mlist *mlp, struct tdesc *tdp, char *format, int level)
{
	struct ardef *ap = tdp->data.ardef;
	int items, inc, limit;

	if (level != 0)
		fprintf(stderr, "");
	else {
		items = ap->indices->range_end - ap->indices->range_start + 1;
		inc = (mlp->size / items) / 8;
		limit = mlp->size / 8;
		switch_on_type(mlp, ap->contents, format, level + 1);
		printf(" %x %x %x", limit, inc, mlp->offset / 8);
		printf(" array-field %s\n", mlp->name);
	}
}

print_function(struct mlist *mlp, struct tdesc *tdp, char *format, int level)
{
	fprintf(stderr, "function in struct %s\n", tdp->name);
}

print_struct(struct mlist *mlp, struct tdesc *tdp, char *format, int level)
{
	format = convert_format(format, ".x");
	if (level != 0)
		printf("' noop ' %s", format);
	else {
		printf("' %s %x struct-field %s\n",
			format, mlp->offset / 8, mlp->name);
	}
}

print_union(struct mlist *mlp, struct tdesc *tdp, char *format, int level)
{
	format = convert_format(format, ".x");
	if (level != 0)
		printf("' noop ' %s", format);
	else {
		printf("' %s %x struct-field %s\n",
			format, mlp->offset / 8, mlp->name);
	}
}

print_enum(struct mlist *mlp, struct tdesc *tdp, char *format, int level)
{
	format = convert_format(format, ".d");

	if (level != 0)
		printf("' l@ ' %s", format);
	else
		printf("' %s %x long-field %s\n",
			format, mlp->offset / 8, mlp->name);
}

char *
convert_format(char *format, char *dfault)
{
	static char dot[3] = ".";

	if (format == NULL)
		return (dfault);
	else if (strlen(format) == 1) {
		dot[1] = *format;
		return (dot);
	} else
		return (format);
}
