/*
 *	nisgrep.c
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)nisgrep.c	1.10	94/10/14 SMI"

/*
 * nisgrep.c
 *
 * nis+ table grep utility
 */

#include <stdio.h>
#include <stdlib.h>
#include <rpc/rpc.h>
#include <rpcsvc/nis.h>

extern int 	optind;
extern char	*optarg;

extern char	*nisname_index();

extern char	*re_comperr;
extern char	*re_comp();


#define	BINARY_STR "*BINARY*"
#define	TABLE_COLS(tres) tres->objects.objects_val[0].TA_data.ta_cols


struct pl_data {
	unsigned flags;
	char ta_sep;
	u_long nmatch;
	char **re_dfa;
};

#define	PL_BINARY 1
#define	PL_COUNT 2
#define	PL_OBJECT 4

int
print_line(tab, ent, udata)
	char *tab;
	nis_object *ent;
	void *udata;
{
	register entry_col *ec = ent->EN_data.en_cols.en_cols_val;
	register int ncol = ent->EN_data.en_cols.en_cols_len;
	register struct pl_data *d = (struct pl_data*)udata;
	register int i;

	/*
	 * check for matches with all patterns
	 */
	for (i = 0; i < ncol; i++)
		if (d->re_dfa[i]) {
			if (ec[i].ec_value.ec_value_len == 0)
				return (0);
			switch (re_exec(d->re_dfa[i],
					ec[i].ec_value.ec_value_val)) {
			case -1:
				return (-1);
			case 0:
				return (0);
			}
		}

	d->nmatch++;
	if (d->flags & PL_COUNT)
		return (0);

	if (d->flags & PL_OBJECT) {
		nis_print_object(ent);
		return (0);
	}

	for (i = 0; i < ncol; i++) {
		if (i > 0)
			printf("%c", d->ta_sep);
		if (ec[i].ec_value.ec_value_len) {
			if ((ec[i].ec_flags & EN_BINARY) &&
			    !(d->flags & PL_BINARY))
				printf(BINARY_STR);
			else
				printf("%s", ec[i].ec_value.ec_value_val);
		}
	}
	printf("\n");

	return (0);
}


#define	EXIT_MATCH 0
#define	EXIT_NOMATCH 1
#define	EXIT_ERROR 2

#define	F_HEADER 1

void
usage()
{
	fprintf(stderr, "usage: nisgrep [-AMchvo] keypat tablename\n");
	fprintf(stderr,
		"       nisgrep [-AMchvo] colname=keypat ... tablename\n");
	exit(EXIT_ERROR);
}

main(argc, argv)
	int argc;
	char *argv[];
{
	int c;
	u_long allres = 0, master = 0;
	unsigned flags = 0;
	char *p;
	int npat, ncol, i, j;
	char **patstr;
	char *name;
	nis_result *tres, *eres;
	char tname[NIS_MAXNAMELEN];
	struct pl_data pld;

	/*
	 * By default, don't print binary data to ttys.
	 */
	pld.flags = (isatty(1))?0:PL_BINARY;

	while ((c = getopt(argc, argv, "AMchvo")) != -1) {
		switch (c) {
		case 'A':
			allres = ALL_RESULTS;
			break;
		case 'M':
			master = MASTER_ONLY;
			break;
		case 'c':
			pld.flags |= PL_COUNT;
			break;
		case 'h':
			flags |= F_HEADER;
			break;
		case 'v' :
			pld.flags &= ~PL_BINARY;
			break;
		case 'o' :
			pld.flags |= PL_OBJECT;
			break;
		default:
			usage();
		}
	}

	if ((npat = argc - optind - 1) < 1)
		usage();
	if ((patstr = (char **)malloc(npat * sizeof (char *))) == 0) {
		fprintf(stderr, "No memory!\n");
		exit(EXIT_ERROR);
	}
	for (i = 0; i < npat; i++)
		patstr[i] = argv[optind++];
	name = argv[optind++];

	/*
	 * Get the table object using expand name magic.
	 */
	tres = nis_lookup(name, master|FOLLOW_LINKS|EXPAND_NAME);
	if (tres->status != NIS_SUCCESS) {
		nis_perror(tres->status, name);
		exit(EXIT_ERROR);
	}

	/*
	 * Construct the name for the table that we found.
	 */
	sprintf(tname, "%s.", tres->objects.objects_val[0].zo_name);
	if (*(tres->objects.objects_val[0].zo_domain) != '.')
		strcat(tname, tres->objects.objects_val[0].zo_domain);

	/*
	 * Make sure it's a table object.
	 */
	if (tres->objects.objects_val[0].zo_data.zo_type != TABLE_OBJ) {
		fprintf(stderr, "%s is not a table!\n", tname);
		exit(EXIT_ERROR);
	}

	/*
	 * Compile the regular expressions.
	 */
	ncol = TABLE_COLS(tres).ta_cols_len;

	if ((pld.re_dfa = (char **)malloc(ncol * sizeof (char *))) == 0) {
		fprintf(stderr, "No memory!\n");
		exit(EXIT_ERROR);
	}
	memset(pld.re_dfa, 0, ncol * sizeof (char *));

	/* XXX  pat could contain '=' */
	if ((npat == 1) && (nisname_index(patstr[0], '=') == 0)) {
		if ((pld.re_dfa[0] = re_comp(patstr[0])) == 0) {
			fprintf(stderr,
				"can't compile regular expression \"%s\": %s\n",
				patstr[0], re_comperr);
			exit(EXIT_ERROR);
		}
	} else {
		for (i = 0; i < npat; i++) {
			if ((p = nisname_index(patstr[i], '=')) == 0)
				usage();
			*(p++) = 0;
			for (j = 0; j < ncol; j++)
				if (TABLE_COLS(tres).ta_cols_val[j].tc_name &&
				    (strcmp(
					TABLE_COLS(tres).ta_cols_val[j].tc_name,
					patstr[i]) == 0))
					break;
			if (j == ncol) {
				fprintf(stderr, "column not found: %s\n",
					patstr[i]);
				exit(EXIT_ERROR);
			}
			if ((pld.re_dfa[j] = re_comp(p)) == 0) {
				fprintf(stderr,
				"can't compile regular expression \"%s\": %s\n",
					p, re_comperr);
				exit(EXIT_ERROR);
			}
		}
	}

	/*
	 * Use the table's separator character when printing entries.
	 */
	pld.ta_sep = tres->objects.objects_val[0].TA_data.ta_sep;

	/*
	 * Print column names
	 */
	if ((flags & F_HEADER) && !(pld.flags & (PL_COUNT|PL_OBJECT))) {
		ncol = TABLE_COLS(tres).ta_cols_len;
		c = pld.ta_sep;
		printf("# ");
		for (i = 0; i < ncol; i++) {
			if (i > 0)
				printf("%c", c);
			printf("%s",
			    TABLE_COLS(tres).ta_cols_val[i].tc_name);
		}
		printf("\n");
	}

	/*
	 * Cat matching entries from the table using a callback function.
	 */
	pld.nmatch = 0;
	eres = nis_list(tname, allres|master, print_line, (void *)&(pld));
	if (eres->status != NIS_CBRESULTS &&
	    eres->status != NIS_NOTFOUND) {
		nis_perror(eres->status, "can't list table");
		exit(EXIT_ERROR);
	}
	if (pld.flags & PL_COUNT)
		printf("%d\n", pld.nmatch);

	if (pld.nmatch)
		exit(EXIT_MATCH);
	else
		exit(EXIT_NOMATCH);
}
