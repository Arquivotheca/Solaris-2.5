/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)dump_charmap.c	1.1	94/06/06 SMI"

#include "../head/_localedef.h"
#include "../head/charmap.h"

main(int argc, char **argv)
{
	int fd;
	CharmapHeader header;
	CharmapSymbol charmapsymbol;
	int i;

	if (argc == 1) {
		fprintf (stderr, "usage: %s charmap_file\n");
		exit (1);
	}

	fd = open (argv[1], O_RDONLY);
	if (fd == -1) {
		fprintf (stderr, "Could not open %s\n", argv[1]);
		exit (1);
	}
	if (read(fd, (char *)&header, sizeof (header)) != sizeof (header)) {
		fprintf (stderr, "Read error, reading header.\n");
		exit (1);
	}
	printf ("DUMP CHARMAP FILE (%s)\n", argv[1]);
	dump_header (&header);
	printf ("Dumping symbols\n");
	for (i = 0; i < header.num_of_elements; i++) {
		if (read(fd, (char *)&charmapsymbol, sizeof(charmapsymbol)) !=
			sizeof (charmapsymbol)) {
			fprintf (stderr, "Symbol read error.\n");
			exit (1);
		}
		dump_symbol(&charmapsymbol);
	}
	close (fd);
	exit (0);
}

/*
 * 
 */
dump_header (CharmapHeader *h)
{
	printf ("Dumping header\n");
	printf ("	mb_cur_max = %d\n", h->mb_cur_max);
	printf ("	mb_cur_min = %d\n", h->mb_cur_min);
	printf ("	escape_char= '%c'\n", h->escape_char);
	printf ("	com. char  = '%c'\n", h->comment_char);
	printf ("	No. of sym = %d\n", h->num_of_elements);
}

dump_symbol(CharmapSymbol *s)
{
	printf ("	type = %s,", s->type == 1 ? "RANGE": "SINGLE");
	printf (" name = %s,", s->name);
	printf (" range= %d\n", s->range);
	dump_encoded(&(s->en_val));
}

dump_encoded(encoded_val *en)
{
	int i;
	printf ("	encoded_val(%d) = ", en->length);
	for (i = 0; i < en->length; i++) {
		dump_val(en->bytes[i]);
	}
	printf("\n\n");
}

dump_val(unsigned char uc)
{
	if (isspace((int)uc)) {
		switch (uc) {
		case '\n':
			printf("(0x%x:'\\n'),", uc);
			break;
		case '\t':
			printf("(0x%x:'\\t'),", uc);
			break;
		case ' ':
			printf("(0x%x:' '),", uc);
			break;
		default:
			printf("(0x%x:''),", uc);
		}
	} else if (isprint ((int)uc))
		printf("(0x%x:'%c'),", uc, uc);
	else
		printf("(0x%x:''),", uc);
}
