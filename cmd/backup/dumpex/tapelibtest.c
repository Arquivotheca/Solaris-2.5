#ident	"@(#)tapelibtest.c 1.13 93/10/13"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "structs.h"
#include "tapelib.h"
#include <config.h>
#include <string.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/systeminfo.h>

char	hostname[BCHOSTNAMELEN];

/*
 * .    -l -> list
 * .    -f cycle reserve -> findfree (cyclenumber, reservetime)
 * .    -a -> tl_add()
 * .    -r -> tl_reserve(tapenum, howlong)
 * .	-u -> tl_update(operation, tapenum, expdate, expcycle)
 *	-s -> setstatus (tapenum, status)
 *	-D -> setdate (tapenum, now+a1)
 */

#define	NEED(nn)    if (argc < (nn)+1) { \
	(void) printf(gettext("I need %d arguments for the %c option\n"), \
		(nn), argv[0][1]); \
	exit(1); \
}

static void
tl_list(void)
{
	int	i;
	struct tapedesc_f t;
	static char **statmessages;

#define	TMAX 3			/* max subscript */

	if (statmessages == (char **)0) {
		/*LINTED [alignment ok]*/
		statmessages = (char **)checkalloc(sizeof (char *) * (TMAX+1));
		statmessages[0] = gettext("No_status");
		statmessages[1] = gettext("Scratch");
		statmessages[2] = gettext("Used");
		statmessages[3] = gettext("Tmp_reserved");
	}

	(void) printf(gettext("list -> Total of %d tapes in library\n"),
		tapeliblen);
	for (i = 0; i < tapeliblen; i++) {
		tl_read(i, &t);
		if ((t.t_status & TL_STATMASK) > TMAX)
			(void) printf(gettext("list -> %05.5d  %-15.15s"),
				i, gettext("UNKNOWN<----"));
		else
			(void) printf(gettext("list -> %05.5d  %-15.15s"),
				i, statmessages[t.t_status & TL_STATMASK]);
		(void) printf(" %12ld %4ld", t.t_expdate, t.t_expcycle);
		if (t.t_status & TL_ERRORED)
			(void) printf(gettext(" ERRORED"));
		if (t.t_status & TL_LABELED)
			(void) printf(gettext(" LABELED"));
		if (t.t_status & TL_OFFSITE)
			(void) printf(gettext(" OFFSITE"));
		(void) printf("\n");
	}
}

main(argc, argv)
	char	*argv[];
{
	int	a1;
	int	a2;
	int	a3;
	int	res;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	progname = strrchr(argv[0], '/');
	if (progname == (char *)0)
		progname = argv[0];
	else
		progname++;

	if (sysinfo(SI_HOSTNAME, hostname, BCHOSTNAMELEN) == -1)
		die(gettext("Cannot determine this host's name\n"));

	(void) sprintf(confdir, "%s/dumps", gethsmpath(etcdir));

	for (argc--, argv++; argc; argc--, argv++) {
		if (argv[0][0] == '-') {
			switch (argv[0][1]) {
			case 'n':
				NEED(1);
				argv++, argc--;
				if (chdir(confdir) == -1)
					die(gettext(
			"Cannot chdir to %s; run this program as root\n"),
						confdir);
				tl_open(argv[0], NULL);
				break;
			case 'a':
				res = tl_add();
				(void) printf("tl_add -> %d\n", res);
				break;
			case 'l':
				tl_list();
				break;
			case 'f':
				NEED(2);
				argv++, argc--;
				a1 = atoi(argv[0]);
				argv++, argc--;
				a2 = atoi(argv[0]);
				res = tl_findfree(a1, a2);
				(void) printf("tl_findfree(%d, %d) -> %d\n",
					a1, a2, res);
				break;
			case 'r':
				NEED(2);
				argv++, argc--;
				a1 = atoi(argv[0]);
				argv++, argc--;
				a2 = atoi(argv[0]);
				tl_reserve(a1, a2);
				(void) printf("tl_reserve(%d, %d) -> ()\n",
					a1, a2);
				break;
			case 's':
				NEED(2);
				argv++, argc--;
				a1 = atoi(argv[0]);
				argv++, argc--;
				a2 = atoi(argv[0]);
				tl_setstatus(a1, a2);
				(void) printf("tl_setstatus(%d, %d) -> ()\n",
					a1, a2);
				break;
			case 'D':
				NEED(2);
				argv++, argc--;
				a1 = atoi(argv[0]);
				argv++, argc--;
				a2 = atoi(argv[0]);
				tl_setdate(a1, a2);
				(void) printf("tl_setdate(%d, %d) -> ()\n",
					a1, a2);
				break;
			case 'u':
				NEED(4);
				argv++, argc--;
				a1 = atoi(argv[0]);
				argv++, argc--;
				a2 = atoi(argv[0]);
				argv++, argc--;
				a3 = atoi(argv[0]) + time((time_t *) 0);
				tl_update(a1, a2, a3);
				(void) printf("tl_update(%d, %d, %d) -> ()\n",
					a1, a2, a3);
				break;
			case 'd':
				debug = 1;
				break;
			}
		} else {
			(void) printf(gettext("Cannot parse switch `%s'\n"),
				argv[0]);
			exit(1);
		}
	}
	exit(0);
#ifdef lint
	return (0);
#endif
}
