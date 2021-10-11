/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)vmstat.c	1.12	95/01/27 SMI"

/* from UCB 5.4 5/17/86 */
/* from SunOS 4.1, SID 1.31 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <memory.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <kstat.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/sysinfo.h>
#ifdef FIXME
#include <sys/buf.h>
#endif FIXME
#include <sys/vfs.h>
#include <sys/dnlc.h>
#include <sys/vmmeter.h>
#include <poll.h>

static	kstat_ctl_t	*kc;		/* libkstat cookie */
static	int		ncpus;
static	kstat_t		**cpu_stat_list = NULL;
static	kstat_t		*sysinfo_ksp, *vminfo_ksp, *system_misc_ksp;
static	kstat_named_t	*deficit_knp, *lbolt_knp, *clk_intr_knp;

struct diskinfo {
	struct diskinfo *next;
	kstat_t *ks;
	kstat_io_t new_kios, old_kios;
	int selected;
};

#define	DISK_DELTA(x) (disk->new_kios.x - disk->old_kios.x)
#define	NULLDISK (struct diskinfo *)0

static	struct diskinfo zerodisk;
static	struct diskinfo *firstdisk = NULLDISK;
static	struct diskinfo *lastdisk = NULLDISK;
static	struct diskinfo *snip = NULLDISK;

static	char	*cmdname = "vmstat";
static	int	hz;
static	int	pagesize;
static	int	etime;
static	int	limit = 4;		/* limit for drive display */
static	int	ndrives = 0;
static	int	lines = 1;
static	int	swflag = 0, cflag = 0;
static	int	iter = 0, interval = 0, poll_interval = 0;

struct disk_selection {
	struct disk_selection *next;
	char ks_name[KSTAT_STRLEN];
};

static	struct disk_selection *disk_selections = NULL;

typedef struct {
	cpu_sysinfo_t	cpu_sysinfo;
	cpu_vminfo_t	cpu_vminfo;
	sysinfo_t	sysinfo;
	vminfo_t	vminfo;
	long		deficit;
} all_stat_t;

static	all_stat_t	s_new, s_old;

#define	pgtok(a) ((a) * (pagesize >> 10))
#define	denom(x) ((x) ? (x) : 1)
#define	DELTA(x) (s_new.x - s_old.x)
#define	REPRINT	19

static	void	dovmstats(void);
static	void	printhdr(int);
static	void	dosum(void);
static	void	dointr(void);
static	void	docachestats(void);
static	void	usage(void);
static	void	all_stat_init(void);
static	int	all_stat_load(void);
static	void	fail(int, char *, ...);
static	void	safe_zalloc(void **, int, int);
static	void	safe_kstat_read(kstat_ctl_t *, kstat_t *, void *);
static	kstat_t	*safe_kstat_lookup(kstat_ctl_t *, char *, int, char *);
static	void	*safe_kstat_data_lookup(kstat_t *, char *);
static	void	init_disks(void);
static	void	select_disks(void);
static	int	diskinfo_load(void);

int
main(int argc, char **argv)
{
	struct disk_selection **dsp;

	pagesize = sysconf(_SC_PAGESIZE);
	hz = sysconf(_SC_CLK_TCK);

	if ((kc = kstat_open()) == NULL)
		fail(1, "kstat_open(): can't open /dev/kstat");
	all_stat_init();

	/* time, in seconds, since boot */
	etime = lbolt_knp->value.l / hz;

	argc--, argv++;
	while (argc > 0 && argv[0][0] == '-') {
		char *cp = *argv++;
		argc--;
		while (*++cp) {
			switch (*cp) {

			case 'S':
				swflag = !swflag;
				break;
			case 's':
				dosum();
				exit(0);
				/* NOTREACHED */
			case 'i':
				dointr();	/* per-device interrupts */
				exit(0);
				/* NOTREACHED */
			case 'c':
				cflag++;
				break;
#ifdef FIXME
			case 'b':
				dobufstats();	/* undocumented for now,   */
				exit(0);	/* waiting for 5.0 support */
				/* NOTREACHED */
#endif FIXME
			default:
				usage();
			}
		}
	}

	/*
	 * Choose drives to be displayed.  Priority
	 * goes to (in order) drives supplied as arguments,
	 * then any other active drives that fit.
	 */
	dsp = &disk_selections;
	while (argc > 0 && !isdigit(argv[0][0])) {
		safe_zalloc((void **) dsp, sizeof (**dsp), 0);
		(void) strncpy((*dsp)->ks_name, *argv, KSTAT_STRLEN - 1);
		dsp = &((*dsp)->next);
		argc--, argv++;
	}
	*dsp = NULL;
	init_disks();

	if (argc > 0) {
		interval = atoi(argv[0]);
		poll_interval = 1000 * interval;
		if (interval <= 0)
			usage();
		iter = (1 << 30);
		if (argc > 1) {
			iter = atoi(argv[1]);
			if (iter <= 0)
				usage();
		}
	}
	if (cflag) {
		docachestats();
		exit(0);
	}

	(void) signal(SIGCONT, printhdr);
	dovmstats();
	while (--iter > 0) {
		(void) poll(NULL, 0, poll_interval);
		dovmstats();
	}
	return (0);
}

static void
dovmstats(void)
{
	int i;
	double percent_factor;
	struct diskinfo *disk;
	ulong_t updates;
	int adj;	/* number of excess columns */

	while (kstat_chain_update(kc) || all_stat_load() || diskinfo_load()) {
		(void) printf("<<State change>>\n");
		all_stat_init();
		init_disks();
		printhdr(0);
	}

	etime = 0;
	for (i = 0; i < CPU_STATES; i++)
		etime += DELTA(cpu_sysinfo.cpu[i]);

	percent_factor = 100.0 / denom(etime);
	etime = denom((etime / ncpus + (hz >> 1)) / hz);
	updates = denom(DELTA(sysinfo.updates));

	if (--lines == 0)
		printhdr(0);

	adj = 0;

#define	ADJ(n)	((adj <= 0) ? n : (adj >= n) ? 1 : n - adj)
#define	adjprintf(fmt, n, val)	adj -= (n + 1) - printf(fmt, ADJ(n), val)

	adjprintf(" %*lu", 1, DELTA(sysinfo.runque) / updates);
	adjprintf(" %*lu", 1, DELTA(sysinfo.waiting) / updates);
	adjprintf(" %*lu", 1, DELTA(sysinfo.swpque) / updates);
	adjprintf(" %*lu", 6, pgtok((int)DELTA(vminfo.swap_avail) / updates));
	adjprintf(" %*lu", 5, pgtok((int)DELTA(vminfo.freemem) / updates));
	adjprintf(" %*lu", 3, swflag?
		DELTA(cpu_vminfo.swapin) / etime :
		DELTA(cpu_vminfo.pgrec) / etime);
	adjprintf(" %*lu", 3, swflag?
		DELTA(cpu_vminfo.swapout) / etime :
		(DELTA(cpu_vminfo.hat_fault) + DELTA(cpu_vminfo.as_fault))
			/ etime);
	adjprintf(" %*lu", 2, pgtok(DELTA(cpu_vminfo.pgpgin)) / etime);
	adjprintf(" %*lu", 2, pgtok(DELTA(cpu_vminfo.pgpgout)) / etime);
	adjprintf(" %*lu", 2, pgtok(DELTA(cpu_vminfo.dfree)) / etime);
	adjprintf(" %*ld", 2, pgtok(s_new.deficit));
	adjprintf(" %*lu", 2, DELTA(cpu_vminfo.scan) / etime);
	for (disk = firstdisk; disk; disk = disk->next)
		if (disk->selected) {
			double hr_etime = (double)DISK_DELTA(wlastupdate);
			if (hr_etime == 0.0)
				hr_etime = (double)NANOSEC;
			adjprintf(" %*.0f", 2,
				((double)DISK_DELTA(reads) +
				(double)DISK_DELTA(writes)) /
				hr_etime * (double)NANOSEC);
		}
	for (i = ndrives; i < limit; i++)
		adjprintf(" %*d", 2, 0);
	adjprintf(" %*lu", 4, DELTA(cpu_sysinfo.intr) / etime - hz);
	adjprintf(" %*lu", 4, DELTA(cpu_sysinfo.syscall) / etime);
	adjprintf(" %*lu", 4, DELTA(cpu_sysinfo.pswitch) / etime);
	adjprintf(" %*.0f", 2,
		DELTA(cpu_sysinfo.cpu[CPU_USER]) * percent_factor);
	adjprintf(" %*.0f", 2,
		DELTA(cpu_sysinfo.cpu[CPU_KERNEL]) * percent_factor);
	adjprintf(" %*.0f\n", 2,
		(DELTA(cpu_sysinfo.cpu[CPU_IDLE])
		+ DELTA(cpu_sysinfo.cpu[CPU_WAIT])) * percent_factor);
	(void) fflush(stdout);
}

/* ARGSUSED */
static void
printhdr(int sig)
{
	int i;
	struct diskinfo *disk;

	(void) printf(" procs     memory            page            ");
	(void) printf("disk          faults      cpu\n");

	if (swflag)
		(void) printf(" r b w   swap  free  si  so pi po fr de sr ");
	else
		(void) printf(" r b w   swap  free  re  mf pi po fr de sr ");

	for (disk = firstdisk; disk; disk = disk->next)
		if (disk->selected)
			(void) printf("%c%c ",
				disk->ks->ks_name[0], disk->ks->ks_name[2]);
	for (i = ndrives; i < limit; i++)
		(void) printf("-- ");

	(void) printf("  in   sy   cs us sy id\n");
	lines = REPRINT;
}

static void
dosum(void)
{
	struct ncstats ncstats;
	u_long nchtotal;
	u_longlong_t nchhits;

	if (all_stat_load() != 0)
		fail(1, "all_stat_load() failed");
	safe_kstat_read(kc, safe_kstat_lookup(kc, "unix", 0, "ncstats"),
		(void *) &ncstats);
	(void) printf("%9lu swap ins\n", s_new.cpu_vminfo.swapin);
	(void) printf("%9lu swap outs\n", s_new.cpu_vminfo.swapout);
	(void) printf("%9lu pages swapped in\n", s_new.cpu_vminfo.pgswapin);
	(void) printf("%9lu pages swapped out\n", s_new.cpu_vminfo.pgswapout);
	(void) printf("%9lu total address trans. faults taken\n",
		s_new.cpu_vminfo.hat_fault + s_new.cpu_vminfo.as_fault);
	(void) printf("%9lu page ins\n", s_new.cpu_vminfo.pgin);
	(void) printf("%9lu page outs\n", s_new.cpu_vminfo.pgout);
	(void) printf("%9lu pages paged in\n", s_new.cpu_vminfo.pgpgin);
	(void) printf("%9lu pages paged out\n", s_new.cpu_vminfo.pgpgout);
	(void) printf("%9lu total reclaims\n", s_new.cpu_vminfo.pgrec);
	(void) printf("%9lu reclaims from free list\n",
		s_new.cpu_vminfo.pgfrec);
	(void) printf("%9lu micro (hat) faults\n", s_new.cpu_vminfo.hat_fault);
	(void) printf("%9lu minor (as) faults\n", s_new.cpu_vminfo.as_fault);
	(void) printf("%9lu major faults\n", s_new.cpu_vminfo.maj_fault);
	(void) printf("%9lu copy-on-write faults\n",
		s_new.cpu_vminfo.cow_fault);
	(void) printf("%9lu zero fill page faults\n", s_new.cpu_vminfo.zfod);
	(void) printf("%9lu pages examined by the clock daemon\n",
		s_new.cpu_vminfo.scan);
	(void) printf("%9lu revolutions of the clock hand\n",
		s_new.cpu_vminfo.rev);
	(void) printf("%9lu pages freed by the clock daemon\n",
		s_new.cpu_vminfo.dfree);
	(void) printf("%9lu forks\n", s_new.cpu_sysinfo.sysfork);
	(void) printf("%9lu vforks\n", s_new.cpu_sysinfo.sysvfork);
	(void) printf("%9lu execs\n", s_new.cpu_sysinfo.sysexec);
	(void) printf("%9lu cpu context switches\n", s_new.cpu_sysinfo.pswitch);
	(void) printf("%9lu device interrupts\n", s_new.cpu_sysinfo.intr);
	(void) printf("%9lu traps\n", s_new.cpu_sysinfo.trap);
	(void) printf("%9lu system calls\n", s_new.cpu_sysinfo.syscall);
	nchtotal = ncstats.hits + ncstats.misses + ncstats.long_look;
	nchhits = ncstats.hits;
	(void) printf("%9lu total name lookups (cache hits %lu%%)\n", nchtotal,
		(u_long)(nchhits * 100 / denom(nchtotal)));
	(void) printf("%9d toolong\n", ncstats.long_enter + ncstats.long_look);
	(void) printf("%9lu user   cpu\n", s_new.cpu_sysinfo.cpu[CPU_USER]);
	(void) printf("%9lu system cpu\n", s_new.cpu_sysinfo.cpu[CPU_KERNEL]);
	(void) printf("%9lu idle   cpu\n", s_new.cpu_sysinfo.cpu[CPU_IDLE]);
	(void) printf("%9lu wait   cpu\n", s_new.cpu_sysinfo.cpu[CPU_WAIT]);
}

#ifdef FIXME

dobufstats()
{
	int nbuf, totreads, hits, hitratio, cachesize, headers;
	struct bstats bstats;

	kvm_read(kd, nl[X_BSTATS].n_value, &bstats, sizeof (struct bstats));
	kvm_read(kd, nl[X_NBUF].n_value, &nbuf, sizeof (int));
	kvm_read(kd, nl[X_BUFALLOC].n_value, &cachesize, sizeof (int));
	kvm_read(kd, nl[X_BUFNHEAD].n_value, &headers, sizeof (int));
	totreads = bstats.n_bread;
	hits = bstats.n_bread_hits;
	hitratio = hits * 100 / denom(totreads);
	(void) printf(\
"nbuf<%d> headers<%d> cachesize<%d> reads<%d> hits<%d> hit ratio<%d>\n",
		nbuf, headers, cachesize, totreads, hits, hitratio);
	(void) printf(
"cache allocation: AGE<%d> LRU<%d> SLEEP<%d> Total:<%d>\n",
		bstats.n_ages, bstats.n_lrus, bstats.n_sleeps,
		bstats.n_ages + bstats.n_lrus + bstats.n_sleeps + cachesize);
}

#endif FIXME

static void
dointr(void)
{
	int i;
	ulong_t inttotal, clockintr, count;
	kstat_intr_t *ki;
	kstat_t *ksp;

	(void) printf("interrupt         total     rate\n");
	(void) printf("--------------------------------\n");

	clockintr = clk_intr_knp->value.l;
	(void) printf("%-12.8s %10lu %8lu\n", "clock", clockintr,
						clockintr / etime);
	inttotal = clockintr;

	for (ksp = kc->kc_chain; ksp; ksp = ksp->ks_next) {
		if (ksp->ks_type != KSTAT_TYPE_INTR)
			continue;
		if (kstat_read(kc, ksp, NULL) == -1)
			continue;
		ki = KSTAT_INTR_PTR(ksp);
		count = 0;
		for (i = 0; i < KSTAT_NUM_INTRS; i++)
			count += ki->intrs[i];
		(void) printf("%-12.8s %10lu %8lu\n",
			ksp->ks_name, count, count / etime);
		inttotal += count;
	}
	(void) printf("--------------------------------\n");
	(void) printf("Total        %10lu %8lu\n", inttotal, inttotal / etime);
}

static void
docachestats(void)
{
	int i;
	struct flushmeter f_old, f_new;
	kstat_t *flushmeter_ksp;

	if (!(flushmeter_ksp = kstat_lookup(kc, "unix", 0, "flushmeter")))
		fail(0, "this machine does not have a virtual address cache");
	safe_kstat_read(kc, flushmeter_ksp, (void *) &f_old);
	if (iter == 0) {
		(void) printf("flush statistics: (totals)\n");
		(void) printf("%8s%8s%8s%8s%8s%8s\n",
			"usr", "ctx", "rgn", "seg", "pag", "par");
		(void) printf(" %7d %7d %7d %7d %7d %7d\n",
			f_old.f_usr, f_old.f_ctx, f_old.f_region,
			f_old.f_segment, f_old.f_page, f_old.f_partial);
		return;
	}
	(void) printf("flush statistics: (interval based)\n");
	for (i = 0; i < iter; i++) {
		if (i % REPRINT == 0)
			(void) printf("%8s%8s%8s%8s%8s%8s\n",
				"usr", "ctx", "rgn", "seg", "pag", "par");
		(void) poll(NULL, 0, poll_interval);
		safe_kstat_read(kc, flushmeter_ksp, (void *) &f_new);
		(void) printf(" %7d %7d %7d %7d %7d %7d\n",
			f_new.f_usr - f_old.f_usr,
			f_new.f_ctx - f_old.f_ctx,
			f_new.f_region - f_old.f_region,
			f_new.f_segment - f_old.f_segment,
			f_new.f_page - f_old.f_page,
			f_new.f_partial- f_old.f_partial);
		f_old = f_new;
	}
}

/*
 * Get various KIDs for subsequent all_stat_load operations.
 */

static void
all_stat_init(void)
{
	kstat_t *ksp;

	/*
	 * Global statistics
	 */

	sysinfo_ksp	= safe_kstat_lookup(kc, "unix", 0, "sysinfo");
	vminfo_ksp	= safe_kstat_lookup(kc, "unix", 0, "vminfo");
	system_misc_ksp	= safe_kstat_lookup(kc, "unix", 0, "system_misc");

	safe_kstat_read(kc, system_misc_ksp, NULL);
	deficit_knp	= safe_kstat_data_lookup(system_misc_ksp, "deficit");
	lbolt_knp	= safe_kstat_data_lookup(system_misc_ksp, "lbolt");
	clk_intr_knp	= safe_kstat_data_lookup(system_misc_ksp, "clk_intr");

	/*
	 * Per-CPU statistics
	 */

	ncpus = 0;
	for (ksp = kc->kc_chain; ksp; ksp = ksp->ks_next)
		if (strncmp(ksp->ks_name, "cpu_stat", 8) == 0)
			ncpus++;

	safe_zalloc((void **) &cpu_stat_list, ncpus * sizeof (kstat_t *), 1);

	ncpus = 0;
	for (ksp = kc->kc_chain; ksp; ksp = ksp->ks_next)
		if (strncmp(ksp->ks_name, "cpu_stat", 8) == 0 &&
		    kstat_read(kc, ksp, NULL) != -1)
			cpu_stat_list[ncpus++] = ksp;

	if (ncpus == 0)
		fail(0, "can't find any cpu statistics");

	(void) memset(&s_new, 0, sizeof (all_stat_t));
}

/*
 * load statistics, summing across CPUs where needed
 */

static int
all_stat_load(void)
{
	int i, j;
	cpu_stat_t cs;
	ulong *np, *tp;

	s_old = s_new;
	(void) memset(&s_new, 0, sizeof (all_stat_t));

	/*
	 * Global statistics
	 */

	safe_kstat_read(kc, sysinfo_ksp, (void *) &s_new.sysinfo);
	safe_kstat_read(kc, vminfo_ksp, (void *) &s_new.vminfo);
	safe_kstat_read(kc, system_misc_ksp, NULL);
	s_new.deficit = deficit_knp->value.l;

	/*
	 * Per-CPU statistics.
	 * For now, we just sum across all CPUs.  In the future,
	 * we should add options to vmstat for per-CPU data.
	 */

	for (i = 0; i < ncpus; i++) {
		if (kstat_read(kc, cpu_stat_list[i], (void *) &cs) == -1)
			return (1);
		np = (ulong *) &s_new.cpu_sysinfo;
		tp = (ulong *) &cs.cpu_sysinfo;
		for (j = 0; j < sizeof (cpu_sysinfo_t); j += sizeof (ulong_t))
			*np++ += *tp++;
		np = (ulong *) &s_new.cpu_vminfo;
		tp = (ulong *) &cs.cpu_vminfo;
		for (j = 0; j < sizeof (cpu_vminfo_t); j += sizeof (ulong_t))
			*np++ += *tp++;
	}
	return (0);
}

static void
usage(void)
{
	(void) fprintf(stderr,
		"Usage: vmstat [-cisS] [disk ...] [interval [count]]\n");
	exit(1);
}

static void
fail(int do_perror, char *message, ...)
{
	va_list args;
	int save_errno = errno;

	va_start(args, message);
	(void) fprintf(stderr, "%s: ", cmdname);
	(void) vfprintf(stderr, message, args);
	va_end(args);
	if (do_perror)
		(void) fprintf(stderr, ": %s", strerror(save_errno));
	(void) fprintf(stderr, "\n");
	exit(2);
}

static void
safe_zalloc(void **ptr, int size, int free_first)
{
	if (free_first && *ptr != NULL)
		free(*ptr);
	if ((*ptr = (void *) malloc(size)) == NULL)
		fail(1, "malloc failed");
	(void) memset(*ptr, 0, size);
}

void
safe_kstat_read(kstat_ctl_t *kc, kstat_t *ksp, void *data)
{
	kid_t kstat_chain_id = kstat_read(kc, ksp, data);

	if (kstat_chain_id == -1)
		fail(1, "kstat_read(%x, '%s') failed", kc, ksp->ks_name);
}

kstat_t *
safe_kstat_lookup(kstat_ctl_t *kc, char *ks_module, int ks_instance,
	char *ks_name)
{
	kstat_t *ksp = kstat_lookup(kc, ks_module, ks_instance, ks_name);

	if (ksp == NULL)
		fail(0, "kstat_lookup('%s', %d, '%s') failed",
			ks_module == NULL ? "" : ks_module,
			ks_instance,
			ks_name == NULL ? "" : ks_name);
	return (ksp);
}

void *
safe_kstat_data_lookup(kstat_t *ksp, char *name)
{
	void *fp = kstat_data_lookup(ksp, name);

	if (fp == NULL)
		fail(0, "kstat_data_lookup('%s', '%s') failed",
			ksp->ks_name, name);
	return (fp);
}

static int
kscmp(kstat_t *ks1, kstat_t *ks2)
{
	int cmp;

	cmp = strcmp(ks1->ks_module, ks2->ks_module);
	if (cmp != 0)
		return (cmp);
	cmp = ks1->ks_instance - ks2->ks_instance;
	if (cmp != 0)
		return (cmp);
	return (strcmp(ks1->ks_name, ks2->ks_name));
}

static void
init_disks(void)
{
	struct diskinfo *disk, *prevdisk, *comp;
	kstat_t *ksp;

	zerodisk.next = NULLDISK;
	disk = &zerodisk;

	/*
	 * Patch the snip in the diskinfo list (see below)
	 */
	if (snip)
		lastdisk->next = snip;

	for (ksp = kc->kc_chain; ksp; ksp = ksp->ks_next) {

		if (ksp->ks_type != KSTAT_TYPE_IO ||
		    strcmp(ksp->ks_class, "disk") != 0)
			continue;
		prevdisk = disk;
		if (disk->next)
			disk = disk->next;
		else {
			safe_zalloc((void **) &disk->next,
				sizeof (struct diskinfo), 0);
			disk = disk->next;
			disk->next = NULLDISK;
		}
		disk->ks = ksp;
		(void) memset((void *) &disk->new_kios, 0, sizeof (kstat_io_t));
		disk->new_kios.wlastupdate = disk->ks->ks_crtime;
		disk->new_kios.rlastupdate = disk->ks->ks_crtime;

		/*
		 * Insertion sort on (ks_module, ks_instance, ks_name)
		 */
		comp = &zerodisk;
		while (kscmp(disk->ks, comp->next->ks) > 0)
			comp = comp->next;
		if (prevdisk != comp) {
			prevdisk->next = disk->next;
			disk->next = comp->next;
			comp->next = disk;
			disk = prevdisk;
		}
	}
	/*
	 * Put a snip in the linked list of diskinfos.  The idea:
	 * If there was a state change such that now there are fewer
	 * disks, we snip the list and retain the tail, rather than
	 * freeing it.  At the next state change, we clip the tail back on.
	 * This prevents a lot of malloc/free activity, and it's simpler.
	 */
	lastdisk = disk;
	snip = disk->next;
	disk->next = NULLDISK;

	firstdisk = zerodisk.next;
	select_disks();
}

static void
select_disks(void)
{
	struct diskinfo *disk;
	struct disk_selection *ds;

	ndrives = 0;
	for (disk = firstdisk; disk; disk = disk->next) {
		disk->selected = 0;
		for (ds = disk_selections; ds; ds = ds->next) {
			if (strcmp(disk->ks->ks_name, ds->ks_name) == 0) {
				disk->selected = 1;
				ndrives++;
				break;
			}
		}
	}
	for (disk = firstdisk; disk; disk = disk->next) {
		if (disk->selected)
			continue;
		if (limit && ndrives >= limit)
			break;
		disk->selected = 1;
		ndrives++;
	}
}

static int
diskinfo_load(void)
{
	struct diskinfo *disk;

	for (disk = firstdisk; disk; disk = disk->next) {
		if (disk->selected) {
			disk->old_kios = disk->new_kios;
			if (kstat_read(kc, disk->ks,
			    (void *) &disk->new_kios) == -1)
				return (1);
		}
	}
	return (0);
}
