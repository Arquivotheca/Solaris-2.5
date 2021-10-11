/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)mpstat.c	1.1	93/04/01 SMI"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <memory.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <kstat.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/sysinfo.h>

static	kstat_ctl_t	*kc;		/* libkstat cookie */
static	int		ncpus;

typedef struct cpuinfo {
	kstat_t		*cs_kstat;
	cpu_stat_t	cs_old;
	cpu_stat_t	cs_new;
} cpuinfo_t;

static	cpuinfo_t	*cpulist = NULL;

#define	DELTA(i, x)	(cpulist[i].cs_new.x - cpulist[i].cs_old.x)

static	char	*cmdname = "mpstat";
static	int	hz, iter = 0, interval = 0, poll_interval = 0;
static	int	lines_until_reprint = 0;

#define	REPRINT	20

static	void	print_header(void);
static	void	show_cpu_usage(void);
static	void	usage(void);
static	void	cpu_stat_init(void);
static	int	cpu_stat_load(void);
static	void	fail(int, char *, ...);
static	void	safe_zalloc(void **, int, int);

int
main(int argc, char **argv)
{
	if ((kc = kstat_open()) == NULL)
		fail(1, "kstat_open(): can't open /dev/kstat");
	cpu_stat_init();
	hz = sysconf(_SC_CLK_TCK);

	if (argc > 1) {
		interval = atoi(argv[1]);
		poll_interval = 1000 * interval;
		if (interval <= 0)
			usage();
		iter = (1 << 30);
		if (argc > 2) {
			iter = atoi(argv[2]);
			if (iter <= 0)
				usage();
		}
	}

	show_cpu_usage();
	while (--iter > 0) {
		poll(NULL, 0, poll_interval);
		show_cpu_usage();
	}
}

static void
print_header(void)
{
	printf("CPU minf mjf xcal  intr ithr  csw icsw migr "
	    "smtx  srw syscl  usr sys  wt idl\n");
}

static void
show_cpu_usage(void)
{
	int i, c, ticks;
	double etime, percent;

	while (kstat_chain_update(kc) || cpu_stat_load()) {
		printf("<<State change>>\n");
		cpu_stat_init();
	}

	if (lines_until_reprint == 0 || ncpus > 1) {
		print_header();
		lines_until_reprint = REPRINT;
	}
	lines_until_reprint--;

	for (c = 0; c < ncpus; c++) {
		ticks = 0;
		for (i = 0; i < CPU_STATES; i++)
			ticks += DELTA(c, cpu_sysinfo.cpu[i]);
		etime = (double)ticks / hz;
		if (etime == 0.0)
			etime = 1.0;
		percent = 100.0 / etime / hz;
		printf("%3d %4d %3d %4d %5d %4d %4d %4d %4d %4d %4d %5d  "
			"%3.0f %3.0f %3.0f %3.0f\n",
			cpulist[c].cs_kstat->ks_instance,
			(int) ((DELTA(c, cpu_vminfo.hat_fault) +
			    DELTA(c, cpu_vminfo.as_fault)) / etime),
			(int) (DELTA(c, cpu_vminfo.maj_fault) / etime),
			(int) (DELTA(c, cpu_sysinfo.xcalls) / etime),
			(int) (DELTA(c, cpu_sysinfo.intr) / etime),
			(int) (DELTA(c, cpu_sysinfo.intrthread) / etime),
			(int) (DELTA(c, cpu_sysinfo.pswitch) /etime),
			(int) (DELTA(c, cpu_sysinfo.inv_swtch) /etime),
			(int) (DELTA(c, cpu_sysinfo.cpumigrate) /etime),
			(int) (DELTA(c, cpu_sysinfo.mutex_adenters) /etime),
			(int) ((DELTA(c, cpu_sysinfo.rw_rdfails) +
			    DELTA(c, cpu_sysinfo.rw_wrfails)) / etime),
			(int) (DELTA(c, cpu_sysinfo.syscall) / etime),
			DELTA(c, cpu_sysinfo.cpu[CPU_USER]) * percent,
			DELTA(c, cpu_sysinfo.cpu[CPU_KERNEL]) * percent,
			DELTA(c, cpu_sysinfo.cpu[CPU_WAIT]) * percent,
			DELTA(c, cpu_sysinfo.cpu[CPU_IDLE]) * percent);
	}
	fflush(stdout);
}

/*
 * Get the KIDs for subsequent cpu_stat_load operations.
 */
static void
cpu_stat_init(void)
{
	kstat_t *ksp;
	int i;

	ncpus = 0;
	for (ksp = kc->kc_chain; ksp; ksp = ksp->ks_next)
		if (strcmp(ksp->ks_module, "cpu_stat") == 0)
			ncpus++;

	safe_zalloc((void **) &cpulist, ncpus * sizeof (cpuinfo_t), 1);

	ncpus = 0;
	for (ksp = kc->kc_chain; ksp; ksp = ksp->ks_next) {
		if (strcmp(ksp->ks_module, "cpu_stat") != 0)
			continue;
		/*
		 * insertion sort by CPU id
		 */
		for (i = ncpus - 1; i >= 0; i--) {
			if (cpulist[i].cs_kstat->ks_instance < ksp->ks_instance)
				break;
			cpulist[i + 1].cs_kstat = cpulist[i].cs_kstat;
		}
		cpulist[i + 1].cs_kstat = ksp;
		ncpus++;
	}

	if (ncpus == 0)
		fail(0, "can't find any cpu statistics");
}

/*
 * Load per-CPU statistics
 */
static int
cpu_stat_load(void)
{
	int i;

	for (i = 0; i < ncpus; i++) {
		cpulist[i].cs_old = cpulist[i].cs_new;
		if (kstat_read(kc, cpulist[i].cs_kstat,
		    (void *) &cpulist[i].cs_new) == -1)
			return (1);
	}
	return (0);
}

static void
usage(void)
{
	fprintf(stderr,
		"Usage: mpstat [interval [count]]\n");
	exit(1);
}

static void
fail(int do_perror, char *message, ...)
{
	va_list args;

	va_start(args, message);
	fprintf(stderr, "%s: ", cmdname);
	vfprintf(stderr, message, args);
	va_end(args);
	if (do_perror)
		fprintf(stderr, ": %s", strerror(errno));
	fprintf(stderr, "\n");
	exit(2);
}

static void
safe_zalloc(void **ptr, int size, int free_first)
{
	if (free_first && *ptr != NULL)
		free(*ptr);
	if ((*ptr = (void *) malloc (size)) == NULL)
		fail(1, "malloc failed");
	memset(*ptr, 0, size);
}
