/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 * rewritten from UCB 4.13 83/09/25
 * rewritten from SunOS 4.1 SID 1.18 89/10/06
 */

#pragma	ident	"@(#)iostat.c	1.9	94/07/22 SMI"

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

static	kstat_ctl_t	*kc;		/* libkstat cookie */
static	int	ncpus;
static	kstat_t	**cpu_stat_list = NULL;

#define	DISK_OLD	0x0001
#define	DISK_NEW	0x0002
#define	DISK_EXTENDED	0x0004
#define	DISK_NORMAL	(DISK_OLD | DISK_NEW)

#define	REPRINT 19

struct diskinfo {
	struct diskinfo *next;
	kstat_t *ks;
	kstat_io_t new_kios, old_kios;
	int selected;
};

#define	NULLDISK (struct diskinfo *)0
static	struct diskinfo zerodisk;
static	struct diskinfo *firstdisk = NULLDISK;
static	struct diskinfo *lastdisk = NULLDISK;
static	struct diskinfo *snip = NULLDISK;

static	cpu_stat_t	old_cpu_stat, new_cpu_stat;

#define	DISK_DELTA(x) (disk->new_kios.x - disk->old_kios.x)

#define	CPU_DELTA(x) (new_cpu_stat.x - old_cpu_stat.x)

static	char	*cmdname = "iostat";

static	int	hz;
static	double	etime;		/* elapsed time */
static	double	percent;	/* 100 / etime */
static	int	tohdr = 1;

static	int	do_tty = 0;
static	int	do_disk = 0;
static	int	do_cpu = 0;
static	int	do_interval = 0;
static	char	disk_header[80];
static	int	limit = 4;		/* limit for drive display */
static	int	ndrives = 0;

struct disk_selection {
	struct disk_selection *next;
	char ks_name[KSTAT_STRLEN];
};

static	struct disk_selection *disk_selections = (struct disk_selection *)NULL;

static	void	printhdr(int);
static	void	show_disk(struct diskinfo *);
static	void	cpu_stat_init(void);
static	int	cpu_stat_load(void);
static	void	usage(void);
static	void	fail(int, char *, ...);
static	void	safe_zalloc(void **, int, int);
static	kid_t	safe_kstat_read(kstat_ctl_t *, kstat_t *, void *);
static	void	init_disks(void);
static	void	select_disks(void);
static	int	diskinfo_load(void);

int
main(int argc, char **argv)
{
	int i;
	int iter = 0;
	int interval = 0, poll_interval = 0;	/* delay between display */
	char ch;
	struct diskinfo *disk;

	if ((kc = kstat_open()) == NULL)
		fail(1, "kstat_open(): can't open /dev/kstat");

	argc--, argv++;
	while (argc && **argv == '-') {
		char *cp;

		for (cp = *argv + 1; *cp; cp++)
		switch (*cp) {
		case 't':
			do_tty++;
			break;
		case 'd':
			do_disk = DISK_OLD;
			break;
		case 'D':
			do_disk = DISK_NEW;
			break;
		case 'x':
			do_disk = DISK_EXTENDED;
			limit = 0; /* unlimited */
			break;
		case 'c':
			do_cpu++;
			break;
		case 'I':
			do_interval++;
			break;
		case 'l':
			if (argc > 1) {
				limit = atoi(argv[1]);
				if (limit < 1)
					usage();
				argc--, argv++;
				break;
			} else
				usage();
		default:
			usage();
		}
	argc--, argv++;
	}

	/* if no output classes explicity specified, use defaults */
	if (do_tty == 0 && do_disk == 0 && do_cpu == 0)
		do_tty = do_cpu = 1, do_disk = DISK_OLD;

	ch = do_interval ? 'i' : 's';
	switch (do_disk) {
	    case DISK_OLD:
		sprintf(disk_header, " Kp%c tp%c serv ", ch, ch);
		break;
	    case DISK_NEW:
		sprintf(disk_header, " rp%c wp%c util ", ch, ch);
		break;
	    case DISK_EXTENDED:
		sprintf(disk_header, "disk      r/%c  w/%c   Kr/%c   Kw/%c "
			"wait actv  svc_t  %%%%w  %%%%b ", ch, ch, ch, ch);
		break;
	}

	hz = sysconf(_SC_CLK_TCK);
	cpu_stat_init();

	if (do_disk) {
		/*
		 * Choose drives to be displayed.  Priority
		 * goes to (in order) drives supplied as arguments,
		 * then any other active drives that fit.
		 */
		struct disk_selection **dsp = &disk_selections;
		while (argc > 0 && !isdigit(argv[0][0])) {
			safe_zalloc((void **)dsp, sizeof (**dsp), 0);
			strncpy((*dsp)->ks_name, *argv, KSTAT_STRLEN - 1);
			dsp = &((*dsp)->next);
			argc--, argv++;
		}
		*dsp = (struct disk_selection *)NULL;
		if (do_disk)
			init_disks();
	}

	if (argc > 0) {
		if ((interval = atoi(argv[0])) <= 0)
			fail(0, "negative interval");
		poll_interval = 1000 * interval;
		argc--, argv++;
	}
	if (argc > 0) {
		if ((iter = atoi(argv[0])) <= 0)
			fail(0, "negative count");
		argc--, argv++;
	}
	if (argc != 0)
		usage();

	signal(SIGCONT, printhdr);
loop:
	if (--tohdr == 0)
		printhdr(0);

	while (kstat_chain_update(kc) || cpu_stat_load() || diskinfo_load()) {
		printf("<<State change>>\n");
		cpu_stat_init();
		if (do_disk)
			init_disks();
		printhdr(0);
	}
	etime = 0.0;
	for (i = 0; i < CPU_STATES; i++)
		etime += CPU_DELTA(cpu_sysinfo.cpu[i]);

	percent = (etime > 0.0) ? 100.0 / etime : 0.0;
	etime = (etime / ncpus) / hz;
	if (etime == 0.0)
		etime = (double)interval;
	if (etime == 0.0)
		etime = 1.0;

	if (do_disk & DISK_EXTENDED) /* show data for the first disk */
		show_disk(firstdisk);

	if (do_tty)
		if (do_interval)
			printf(" %3.0f %4.0f",
				(float)CPU_DELTA(cpu_sysinfo.rawch),
				(float)CPU_DELTA(cpu_sysinfo.outch));
		else
			printf(" %3.0f %4.0f",
				(float)CPU_DELTA(cpu_sysinfo.rawch) / etime,
				(float)CPU_DELTA(cpu_sysinfo.outch) / etime);

	if (do_disk & DISK_NORMAL)
		for (disk = firstdisk; disk; disk = disk->next)
			if (disk->selected)
				show_disk(disk);

	if (do_cpu)
		printf(" %2.0f %2.0f %2.0f %2.0f",
			CPU_DELTA(cpu_sysinfo.cpu[CPU_USER])   * percent,
			CPU_DELTA(cpu_sysinfo.cpu[CPU_KERNEL]) * percent,
			CPU_DELTA(cpu_sysinfo.cpu[CPU_WAIT])   * percent,
			CPU_DELTA(cpu_sysinfo.cpu[CPU_IDLE])   * percent);

	putchar('\n');

	if (do_disk & DISK_EXTENDED) /* show data for the rest of the disks */
		for (disk = firstdisk->next; disk; disk = disk->next)
			if (disk->selected) {
				show_disk(disk);
				putchar('\n');
			}

	fflush(stdout);

	if (--iter && interval > 0) {
		poll(NULL, 0, poll_interval);
		goto loop;
	}
	exit(0);
}

static void
printhdr(int sig)
{
	struct diskinfo *disk;

	if (do_disk & DISK_EXTENDED)
		printf("%57s ", "extended disk statistics");
	if (do_tty)
		printf("      tty");
	if (do_disk & DISK_NORMAL)
		for (disk = firstdisk; disk; disk = disk->next)
			if (disk->selected)
				printf(" %12.8s ", disk->ks->ks_name);
	if (do_cpu)
		printf("         cpu");
	putchar('\n');

	if (do_disk & DISK_EXTENDED)
		printf(disk_header);
	if (do_tty)
		printf(" tin tout");
	if (do_disk & DISK_NORMAL)
		for (disk = firstdisk; disk; disk = disk->next)
			if (disk->selected)
				printf(disk_header);
	if (do_cpu)
		printf(" us sy wt id");
	putchar('\n');

	tohdr = (do_disk & DISK_EXTENDED) ? 1 : REPRINT;
}

static void
show_disk(struct diskinfo *disk)
{
	double rps, wps, tps, krps, kwps, kps, avw, avr, serv, w_pct, r_pct;
	double etime, hr_etime;

	hr_etime = (double)DISK_DELTA(wlastupdate);
	if (hr_etime == 0.0)
		hr_etime = (double)NANOSEC;
	etime = hr_etime / (double)NANOSEC;

	rps	= (double)DISK_DELTA(reads) / etime;
		/* reads per second */

	wps	= (double)DISK_DELTA(writes) / etime;
		/* writes per second */

	tps	= rps + wps;
		/* transactions per second */

	krps	= (double)DISK_DELTA(nread) / etime / 1024.0;
		/* kilobytes reads per second */

	kwps	= (double)DISK_DELTA(nwritten) / etime / 1024.0;
		/* kilobytes written per second */

	kps	= krps + kwps;
		/* kilobytes transferred per second */

	avw	= (double)DISK_DELTA(wlentime) / hr_etime;
		/* average number of transactions waiting */

	avr	= (double)DISK_DELTA(rlentime) / hr_etime;
		/* average number of transactions running */

	serv	= tps > 0 ? (avw + avr) / tps * 1000.0 : 0.0;
		/* average service time in milliseconds */

	w_pct	= (double)DISK_DELTA(wtime) / hr_etime * 100.0;
		/* % of time there is a transaction waiting for service */

	r_pct	= (double)DISK_DELTA(rtime) / hr_etime * 100.0;
		/* % of time there is a transaction running */

	if (do_interval) {
		rps	*= etime;
		wps	*= etime;
		tps	*= etime;
		krps	*= etime;
		kwps	*= etime;
		kps	*= etime;
	}

	switch (do_disk) {
	    case DISK_OLD:
		printf(" %3.0f %3.0f %4.0f ", kps, tps, serv);
		break;
	    case DISK_NEW:
		printf(" %3.0f %3.0f %4.1f ", rps, wps, r_pct);
		break;
	    case DISK_EXTENDED:
		printf("%-8.8s"
		    " %4.1f %4.1f %6.1f %6.1f %4.1f %4.1f %6.1f %3.0f %3.0f ",
			disk->ks->ks_name,
			rps, wps, krps, kwps, avw, avr, serv, w_pct, r_pct);
		break;
	}
}

/*
 * Get list of cpu_stat KIDs for subsequent cpu_stat_load operations.
 */

static void
cpu_stat_init(void)
{
	kstat_t *ksp;

	ncpus = 0;
	for (ksp = kc->kc_chain; ksp; ksp = ksp->ks_next)
		if (strncmp(ksp->ks_name, "cpu_stat", 8) == 0)
			ncpus++;

	safe_zalloc((void **)&cpu_stat_list, ncpus * sizeof (kstat_t *), 1);

	ncpus = 0;
	for (ksp = kc->kc_chain; ksp; ksp = ksp->ks_next)
		if (strncmp(ksp->ks_name, "cpu_stat", 8) == 0 &&
		    kstat_read(kc, ksp, NULL) != -1)
			cpu_stat_list[ncpus++] = ksp;

	if (ncpus == 0)
		fail(1, "can't find any cpu statistics");

	memset(&new_cpu_stat, 0, sizeof (cpu_stat_t));
}

static int
cpu_stat_load(void)
{
	int i, j;
	cpu_stat_t cs;
	ulong *np, *tp;

	old_cpu_stat = new_cpu_stat;
	memset(&new_cpu_stat, 0, sizeof (cpu_stat_t));

	/* Sum across all cpus */

	for (i = 0; i < ncpus; i++) {
		if (kstat_read(kc, cpu_stat_list[i], (void *)&cs) == -1)
			return (1);
		np = (ulong *)&new_cpu_stat.cpu_sysinfo;
		tp = (ulong *)&cs.cpu_sysinfo;
		for (j = 0; j < sizeof (cpu_sysinfo_t); j += sizeof (ulong_t))
			*np++ += *tp++;
		np = (ulong *)&new_cpu_stat.cpu_vminfo;
		tp = (ulong *)&cs.cpu_vminfo;
		for (j = 0; j < sizeof (cpu_vminfo_t); j += sizeof (ulong_t))
			*np++ += *tp++;
	}
	return (0);
}

static void
usage(void)
{
	fprintf(stderr,
	    "Usage: iostat [-tdDxcI] [-l n] [disk ...] [interval [count]]\n");
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
	if ((*ptr = (void *)malloc(size)) == NULL)
		fail(1, "malloc failed");
	memset(*ptr, 0, size);
}

static kid_t
safe_kstat_read(kstat_ctl_t *kc, kstat_t *ksp, void *data)
{
	kid_t kstat_chain_id = kstat_read(kc, ksp, data);

	if (kstat_chain_id == -1)
		fail(1, "kstat_read(%x, '%s') failed", kc, ksp->ks_name);
	return (kstat_chain_id);
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
			safe_zalloc((void **)&disk->next,
				sizeof (struct diskinfo), 0);
			disk = disk->next;
			disk->next = NULLDISK;
		}
		disk->ks = ksp;
		memset((void *)&disk->new_kios, 0, sizeof (kstat_io_t));
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
	if (firstdisk == NULLDISK)
		fail(0, "No disks to measure");
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
			    (void *)&disk->new_kios) == -1)
				return (1);
		}
	}
	return (0);
}
