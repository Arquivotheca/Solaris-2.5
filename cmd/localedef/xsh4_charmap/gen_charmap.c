/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)gen_charmap.c	1.1	94/06/06 SMI"

#include "../head/_localedef.h"
#include "../head/charmap.h"

main(int argc, char **argv)
{
	int fd;
	CharmapHeader header;
	CharmapSymbol charmapsymbol;
	int i;

	if (argc == 1) {
		fprintf(stderr, "usage: %s charmap_file\n");
		exit(1);
	}

	fd = open (argv[1], O_RDONLY);
	if (fd == -1) {
		fprintf(stderr, "Could not open %s\n", argv[1]);
		exit(1);
	}
	if (read(fd, (char *)&header, sizeof (header)) != sizeof (header)) {
		fprintf(stderr, "Read error, reading header.\n");
		exit(1);
	}
	dump_header (&header);
	for (i = 0; i < header.num_of_elements; i++) {
		if (read(fd, (char *)&charmapsymbol, sizeof(charmapsymbol)) !=
			sizeof (charmapsymbol)) {
			fprintf(stderr, "Symbol read error.\n");
			exit (1);
		}
		dump_symbol(&charmapsymbol);
	}
	printf("END CHARMAP\n");
	close (fd);
	exit (0);
}

/*
 * 
 */
dump_header (CharmapHeader *h)
{
	printf("CHARMAP\n");
	if (h->code_set_name != NULL)
		printf("<code_set_name> %s\n", h->code_set_name);
	printf("<mb_cur_max> %d\n", h->mb_cur_max);
	printf("<mb_cur_min> %d\n", h->mb_cur_min);
	printf("<escape_char> %c\n", h->escape_char);
	printf("<comment_char> %c\n", h->comment_char);
}

dump_symbol(CharmapSymbol *s)
{
	if (s->type == SINGLE)
		dump_single(s);
	else
		dump_range(s);
}

dump_range(CharmapSymbol *s)
{
	printf("<%s>...<%s + %d> ", s->name, s->name, s->range);
	dump_encoded(&(s->en_val));
}

dump_single(CharmapSymbol *s)
{
	printf("<%s> ", s->name);
	dump_encoded(&(s->en_val));
}

dump_encoded(encoded_val *en)
{
	int i;
	for (i = 0; i < en->length; i++) {
		dump_val(en->bytes[i]);
	}
	printf("\n");
}

dump_val(unsigned char uc)
{
	printf("\\x%x", uc);
}
