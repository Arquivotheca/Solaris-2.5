/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)psrinfo.c	1.7	95/08/22 SMI"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <kstat.h>

static char cmdname[8] = "psrinfo";
static void cpu_info(kstat_ctl_t *kc, kstat_t *ksp, int verbosity);

static void
usage(char *msg)
{
	if (msg != NULL)
		fprintf(stderr, "%s: %s\n", cmdname, msg);
	fprintf(stderr, "usage: \n\t%s [-v] [processor_id ...]\n"
		"\t%s -s processor_id\n", cmdname, cmdname);
	exit(2);
}

int
main(int argc, char *argv[])
{
	kstat_ctl_t *kc;
	kstat_t	*ksp;
	int c, cpu;
	int verbosity = 1;	/* 0 = silent, 1 = normal, 3 = verbose */
	int errors = 0;

	while ((c = getopt(argc, argv, "sv")) != EOF) {
		switch (c) {
		    case 'v':
			verbosity |= 2;
			break;
		    case 's':
			verbosity &= ~1;
			break;
		    default:
			usage(NULL);
		}
	}

	argc -= optind;
	argv += optind;

	if (verbosity == 2)
		usage("options -s and -v are mutually exclusive");

	if (verbosity == 0 && argc != 1)
		usage("must specify exactly one processor if -s used");

	if ((kc = kstat_open()) == NULL) {
		fprintf(stderr, "%s: kstat_open() failed: %s\n",
			cmdname, strerror(errno));
		exit(1);
	}

	if (argc == 0) {
		/*
		 * No processors specified.  Report on all of them.
		 */
		int maxcpu = -1;
		for (ksp = kc->kc_chain; ksp != NULL; ksp = ksp->ks_next)
			if (strcmp(ksp->ks_module, "cpu_info") == 0 &&
			    ksp->ks_instance > maxcpu)
				maxcpu = ksp->ks_instance;
		for (cpu = 0; cpu <= maxcpu; cpu++)
			if (ksp = kstat_lookup(kc, "cpu_info", cpu, NULL))
				cpu_info(kc, ksp, verbosity);
	} else {
		/*
		 * Report on specified processors.
		 */
		for (; argc > 0; argv++, argc--) {
			char cpubuf[20];
			sprintf(cpubuf, "cpu_info%.10s", *argv);
			if (ksp = kstat_lookup(kc, "cpu_info", -1, cpubuf))
				cpu_info(kc, ksp, verbosity);
			else {
				fprintf(stderr, "%s: processor %s: %s\n",
					cmdname, *argv, strerror(EINVAL));
				errors = 2;
			}
		}
	}
	return (errors);
}

#define	GETLONG(name)	((kstat_named_t *)kstat_data_lookup(ksp, name))->value.l
#define	GETSTR(name)	((kstat_named_t *)kstat_data_lookup(ksp, name))->value.c

static void
cpu_info(kstat_ctl_t *kc, kstat_t *ksp, int verbosity)
{
	char	curtime[40], start[40];
	int	cpu = ksp->ks_instance;
	time_t	now = time(NULL);

	if (kstat_read(kc, ksp, NULL) == -1) {
		fprintf(stderr, "%s: kstat_read() failed for cpu %d: %s\n",
			cmdname, cpu, strerror(errno));
		exit(1);
	}

	(void) cftime(start, "%D %T", (time_t *)&GETLONG("state_begin"));
	(void) cftime(curtime, "%D %T", &now);

	if (verbosity == 0) {
		printf("%d\n", strcmp(GETSTR("state"), "on-line") == 0);
		return;
	}
	if (verbosity == 1) {
		printf("%d\t%-8s  since %s\n", cpu, GETSTR("state"), start);
		return;
	}

	printf("Status of processor %d as of: %s\n", cpu, curtime);
	printf("  Processor has been %s since %s.\n", GETSTR("state"), start);
	printf("  The %s processor operates at ", GETSTR("cpu_type"));

	printf(GETLONG("clock_MHz") != 0 ? "%d MHz" : "an unknown frequency",
		GETLONG("clock_MHz"));

	printf(",\n\tand has %s%s floating point processor.\n",
		GETSTR("fpu_type")[0] == '\0' ? "no" :
		strchr("aeiouy", GETSTR("fpu_type")[0]) ? "an " : "a ",
		GETSTR("fpu_type"));
}
