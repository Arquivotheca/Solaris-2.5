/*
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

#ident	"@(#)intr.c	1.76	95/03/06 SMI"

#include <sys/types.h>
#include <sys/thread.h>
#include <sys/cpuvar.h>
#include <vm/seg_kp.h>
#include <sys/asm_linkage.h>
#include <sys/var.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/sysmacros.h>
#include <sys/avintr.h>
#include <sys/t_lock.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/privregs.h>
#include <sys/sunddi.h>

/*
 * do_robin enables intr round-robin distribution. Set it to
 * 0 causes a static assignment of each Sbus intr to a CPU.
 */
int do_robin = 0;

/*
 * last_cpu_id gives do_itr_round_robin a hint when to wrap around
 * so we don't spend time looping whole cpu[] array. Default to the
 * whole cpu array, cpu[0] -> cpu[NCPU - 1].
 */
u_int last_cpu_id = NCPU - 1;

int int_count[16] = { 0, };	/* used by _interrupt in locore.s */

/*
 * Number of SBus interrupt handlers supported per level same slot,
 * tuneable from the /etc/system file.
 * Most sbus cards only use one interrupt per level, and a very small
 * number have been found which use two.
 * Generally unchanged but available for supporting existing
 * equipment from outside vendors which may use more interrupt
 * handlers then allowed by default.
 */
int sbus_nvect = 2;

/*
 * BW Interrupt Table - Misc Interrupt assignment (bit masks)
 */
#define	INTTABLE_PROFILE	(1 << 0)
#define	INTTABLE_TICK		(1 << 1)

extern u_int intr_get_table(u_int level);
extern void intr_clear_table(u_int level, u_int mask);
extern void poll_obp_mbox(struct regs *rp);
extern void intr_sbi_release(u_int mask, u_int device_id);
extern int  intr_sbi_take(u_int mask, u_int device_id);


/*
 * note_not_idle - some cpu is leaving idle()
 */
void
note_not_idle()
{
}

/*
 * BEWARE: Here begins a maze of twisty little passages, all different
 */

/*
 * non-ambiguous (cpu) interrupt source id's are:
 * [6..15], [32*sbus+14..15]
 */
#define	INTR_MESSAGE(broadcast, target, intsid, levels)	\
	(((broadcast	& 0x0001) << 31) +		\
	((target	& 0x00ff) << 23) +		\
	((intsid	& 0x00ff) << 15) +		\
	(levels		& 0x7fff))

#define	INTR_SOFTUID(level)	(6 + ((level >> 1) & 0x7))

#define	INTR_HIM(device_id, level)	\
	INTR_MESSAGE(0, device_id, INTR_SOFTUID(level), (1 << (level-1)))

extern void intr_igr_set();	/* intr_misc.s */

/*
 * WARNING - This still doesn't conform completely to the scheme
 * described in the sun4d architecture manual.
 */
#ifdef DEBUG
static int intr_debug = 0;
#define	INTR_PRINTF	if (intr_debug) printf
static int nexi_debug = 0;
#define	NEXI_PRINTF	if (nexi_debug) printf
#else DEBUG
#define	INTR_PRINTF
#define	NEXI_PRINTF
#endif DEBUG

/*
 * sun4d manifest constants
 */
#define	MAX_OBIO	10
#define	MAX_SBUS	14
#define	SBUS_INTR_TBL_MASK	(0x3fff)
#define	SLOTS_PER_SBI	2		/* log of actual slots, for shifting */

#define	SBI_DEVICE(unit)		((unit << 4) + 0x2)
#define	SBI_TAKE_MASK(LEVEL)		(0xf << (LEVEL * 4))
#define	SBI_HOTSLOTS(LEVEL)		(0x1 << (LEVEL * 4))
#define	SBI_TAKE_ALL			(0xffffffffUL)

typedef struct {
	int spurious;
} spur_count_t;

static struct autovec *sbus_table[MAX_SBUS];
static struct autovec *bbus_table[MAX_OBIO];
static struct autovec *soft_table;

static spur_count_t sbus_spurious[MAX_SBUS * 16];
static spur_count_t bbus_spurious[MAX_OBIO * 16];
static spur_count_t soft_spurious[16];
static spur_count_t table_spurious[16];

#define	BBUS_AUTOVEC(cpu_id)		(bbus_table[cpu_id >> 1])
#define	SBUS_AUTOVEC(bus)		(sbus_table[bus])

#define	SBUS_SPURIOUS(bus, pri)		(sbus_spurious + (bus << 4) + pri)
#define	TABLE_SPURIOUS(pri)		(table_spurious + pri)
#define	SOFT_SPURIOUS(pri)		(soft_spurious + pri)
#define	BBUS_SPURIOUS(cpu_id, pri)	\
	(bbus_spurious + ((cpu_id >> 1) << 4) + pri)

/*
 * sbi_isr_init - initialize sbi, protecting against OBP & boot program
 */
static void
sbi_isr_init(u_int unit)
{
	u_int device_id = SBI_DEVICE(unit);
	u_int sbi_mask = SBI_TAKE_ALL;
	u_int taken = intr_sbi_take(sbi_mask, device_id);

	INTR_PRINTF("sbi_isr_init: intr_sbi_take - "
		"unit=%d, device_id=0x%x, sbi_mask=0x%x, taken=0x%x\n",
		unit, device_id, sbi_mask, taken);

	taken &= sbi_mask;

	if (taken != 0) {
		/* yes, printf - we want to always see this msg */
		printf("sbi_isr_init: cleaning up left over state "
			"unit=%d, pending=0x%d\n", unit, taken);
		intr_sbi_release(taken, device_id);
	} else {
		INTR_PRINTF("sbi_isr_init: unit=%d, no interrupts pending\n",
			unit);
	}
}

/*
 * nexus_note_sbus - field interrupts from an SBus
 */
int
nexus_note_sbus(u_int unit, struct autovec *handler)
{
	struct autovec *old = SBUS_AUTOVEC(unit);

	SBUS_AUTOVEC(unit) = handler;

	if (old != 0) {
		NEXI_PRINTF("nexus_note_sbus: unit=%d, handler=0x%x\n",
			unit, old);
		return (-1);
	}

	/*
	 * Note: at this point in time, OBP/boot may have left the devices
	 * in a funny state in that they don't behave well w.r.t. our hardware.
	 * In particular, we've got to clean up the SBI-ISR.
	 */
	sbi_isr_init(unit);
	return (0);
}

/*
 * nexus_note_bbus - field interrupts from a bootbus
 */
int
nexus_note_bbus(u_int cpu_id, struct autovec *handler)
{
	struct autovec *old = BBUS_AUTOVEC(cpu_id);

	BBUS_AUTOVEC(cpu_id) = handler;

	if (old != 0) {
		NEXI_PRINTF("nexus_note_bbus: cpu_id=%d, handler=0x%x\n",
			cpu_id, old);
		return (-1);
	}

	/* bbus_bbc_init(unit); ??? */
	return (0);
}

/*
 * nexus_note_soft - field (soft) interrupts
 */
int
nexus_note_soft(struct autovec *handler)
{
	struct autovec *old = soft_table;

	soft_table = handler;

	if (old != 0) {
		NEXI_PRINTF("nexus_note_soft: handler=0x%x\n", old);
		return (-1);
	}

	/* xxx_init(unit); ??? */
	return (0);
}


#ifdef DEBUG
u_int xxintr_cnt[NCPU];
#endif DEBUG

/*
 * sbus_request - service a single bus's request(s)
 *
 * Note: this routine is very performance sensitive.
 */
static void
sbus_request(u_int pri, u_int unit)
{
	struct autovec *av = SBUS_AUTOVEC(unit);
	u_int device_id = SBI_DEVICE(unit);
	u_int sbi_mask = SBI_TAKE_MASK(pri);
	u_int taken = intr_sbi_take(sbi_mask, device_id);
	u_int hot = SBI_HOTSLOTS(pri);
	int nvect = sbus_nvect;

	INTR_PRINTF("sbus_request: pri=%d, unit=%d\n", pri, unit);
	INTR_PRINTF("sbus_request: intr_sbi_take - "
		"device_id=0x%x, sbi_mask=0x%x, taken=0x%x\n",
		device_id, sbi_mask, taken);

	taken &= sbi_mask;

	if ((taken == 0) || (av == 0)) {
		SBUS_SPURIOUS(unit, pri)->spurious++;
		INTR_PRINTF("sbus_request: unit=0x%x, pri=%d spurious?\n",
			unit, pri);
		return;
	}

	/*
	 * Index into autovec array offset by priority level.
	 */
	av += ((pri * nvect) << SLOTS_PER_SBI);

	/*
	 * Loop until interrupts for this Sbus and pri level are handled.
	 */
	while (taken) {
		if ((taken & hot) != 0) {
			struct autovec *handler = av;
			u_int rc = DDI_INTR_UNCLAIMED;
			int i;
#ifdef DEBUG
			xxintr_cnt[CPU->cpu_id]++;
#endif DEBUG
			/*
			 * Loop until a handler is found for this level.
			 */
			for (i = nvect; i > 0; i--, handler++) {
				u_int (*av_vector)();
				kmutex_t *av_mutex;

				/*
				 * When vectors are removed the hole
				 * is filled by the last vector in the
				 * list.  So if what we are looking at
				 * is NULL we've looked at them all.
				 */
				if ((av_vector = handler->av_vector) == NULL)
					break;

				INTR_PRINTF("sbus_request:"
				    " hot = 0x%x, taken = 0x%x, av=0x%s\n",
				    hot, taken, av);

				av_mutex = handler->av_mutex;
				if (av_mutex == 0) {
					INTR_PRINTF("sbus_request: "
						"av_mutex(%d, %d) == 0!\n",
						unit, pri);
					rc = (*av_vector)(handler->av_intarg);
				} else {
					mutex_enter(av_mutex);
					rc = (*av_vector)(handler->av_intarg);
					mutex_exit(av_mutex);
				}

				/*
				 * A slight optimization is to stop looking
				 * once a handler has claimed an interrupt
				 * as this is the most common situation.
				 * Interrupts will not be dropped if another
				 * is pending.
				 */
				if (rc == DDI_INTR_CLAIMED)
					goto intrclaimed;

			}

			if (rc == DDI_INTR_UNCLAIMED) {
				SBUS_SPURIOUS(unit, pri)->spurious++;
				INTR_PRINTF("sbus_request: "
				    "handler(%d, %d) rc==DDI_INTR_UNCLAIMED!\n",
				    unit, pri);
			}

intrclaimed:
			/*
			 * responsible for "squelching" the interrupt
			 * source.
			 */
			intr_sbi_release(hot, device_id);
			INTR_PRINTF("sbus_request: intr_sbi_release - "
				"hot=0x%x, device_id=0x%x\n",
				hot, device_id);

			/* make a note that this slot has been handled. */
			taken &= (~hot);
		}

		hot <<= 1;
		av += nvect; /* next sbus slot */
	}
}

/*
 * sbus_intr - sbus-based devices
 */
void
sbus_intr(u_int pri)
{
	u_int busses_mask = intr_get_table(pri) & SBUS_INTR_TBL_MASK;
	int i;

	INTR_PRINTF("sbus_intr: intr_get_table - "
		"pri=%d, busses_mask=0x%x\n",
		pri, busses_mask);

	if (busses_mask == 0) {
		TABLE_SPURIOUS(pri)->spurious++;
		INTR_PRINTF("sbus_intr: no table bits (%d) - spurious?\n", pri);
		return;
	}

	intr_clear_table(pri, busses_mask);

	INTR_PRINTF("sbus_intr: intr_clear_table - "
		"pri=%d, busses_mask=0x%x\n",
		pri, busses_mask);

	/*
	 * Loop until all interrupts has been handled for all Sbuses
	 * on this level.
	 */
	i = 0;
	while (busses_mask) {
		if (busses_mask & 0x1) {
			sbus_request(pri, i);
		}
		busses_mask >>= 1;
		i++;
	}
}

/*
 * slow_intr - slowbus devices {zs@12}
 */
void
slow_intr(u_int pri)
{
	u_char cpu_id = CPU->cpu_id;
	struct autovec *table = BBUS_AUTOVEC(cpu_id);

	ASSERT(pri < 16);

	if (table == 0) {
		BBUS_SPURIOUS(cpu_id, pri)->spurious++;
		INTR_PRINTF("slow_intr: cpu_id=0x%x, pri=%d spurious?\n",
			cpu_id, pri);
		return;
	} else {
		struct autovec *av = table + pri;
		u_int (*av_vector)() = av->av_vector;
		caddr_t av_intarg = av->av_intarg;
		kmutex_t *av_mutex = av->av_mutex;
		u_int rc;

		if (av_vector == 0) {
			BBUS_SPURIOUS(cpu_id, pri)->spurious++;
			INTR_PRINTF("slow_intr: av_vector(%d, %d) == 0!\n",
				cpu_id, pri);
		} else if (av_mutex == 0) {
			INTR_PRINTF("slow_intr: av_mutex(%d, %d) == 0!\n",
				cpu_id, pri);
			rc = (*av_vector)(av_intarg);
		} else {
			mutex_enter(av_mutex);
			rc = (*av_vector)(av_intarg);
			mutex_exit(av_mutex);
		}

		if (rc == 0) {
			BBUS_SPURIOUS(cpu_id, pri)->spurious++;
			INTR_PRINTF("slow_intr: handler(%d, %d) rc == 0!\n",
				cpu_id, pri);
		}
	}
}

/*
 * soft_pseudo_intr - dispatch hardware "soft" interrupt
 * must call *each and every* routine registered for this level!
 * we also accumulate results (via rc |= xx)
 */
u_int
soft_pseudo_intr(caddr_t av_intarg)
{
	struct autovec **private = (struct autovec **)av_intarg;
	struct autovec *av;
	u_int rc = 0;		/* meaningless? */
	u_int (*av_vector)();

	if (private == 0) {
		/* PSEUDO_SPURIOUS(xx)->spurious++; */
		INTR_PRINTF("soft_pseudo_intr: private == 0!\n");
		return (0);
	}

	if (*private == 0) {
		/* PSEUDO_SPURIOUS(xx)->spurious++; */
		INTR_PRINTF("soft_pseudo_intr: *private == 0!\n");
		return (0);
	}

	/*
	 * 'private' is a list of ISR's, an ISR == 0 terminates the list
	 */
	for (av = *private; (av_vector = av->av_vector) != 0; av++) {
		kmutex_t *av_mutex = av->av_mutex;
		caddr_t av_intarg = av->av_intarg;

		if (av_mutex == 0) {
			INTR_PRINTF("soft_pseudo_intr: av_mutex == 0!\n");
			rc |= (*av_vector)(av_intarg);
		} else {
			mutex_enter(av_mutex);
			rc |= (*av_vector)(av_intarg);
			mutex_exit(av_mutex);
		}
	}

	return (rc);
}

static struct autovec frozbot[16];	/* dynamically allocate? */

/*
 * init_soft_stuffs - set up redirection to std autovec mechanism
 * Don't be fooled! It's not as simple as (*snibtz) = (*xvec);
 */
void
init_soft_stuffs()
{
	struct autovec *const *xbase = vectorlist + INTLEVEL_SOFT;
	struct autovec *table = frozbot;
	int i;

	/*
	 * 'vectorlist(s)' are lists of ISR's, an ISR == 0 terminates the list
	 */
	for (i = 1; i < 16; i++) {
		struct autovec *const *xvec = xbase + i;
		struct autovec *snibtz = frozbot + i;

		/* bzero(snibtz, sizeof (struct autovec)); */
		snibtz->av_vector = soft_pseudo_intr;
		snibtz->av_intarg = (caddr_t)xvec;
		snibtz->av_mutex = 0;
	}

	nexus_note_soft(table);
}

/*
 * cpu_intr - other half of setsoftint {soft@1, soft@4, soft@6}
 */
void
cpu_intr(u_int pri)
{
	u_char cpu_id = CPU->cpu_id;
	struct autovec *table = soft_table;

	if (table == 0) {
		SOFT_SPURIOUS(0)->spurious++;
		INTR_PRINTF("cpu_intr: cpu_id=0x%x, pri=%d spurious?\n",
			cpu_id, pri);
		return;
	} else {
		struct autovec *av = table + pri;
		u_int (*av_vector)() = av->av_vector;
		caddr_t av_intarg = av->av_intarg;
		kmutex_t *av_mutex = av->av_mutex;
		u_int rc;

		if (av_vector == 0) {
			SOFT_SPURIOUS(pri)->spurious++;
			INTR_PRINTF("cpu_intr: av_vector(%d) == 0!\n", pri);
		} else if (av_mutex == 0) {
			INTR_PRINTF("cpu_intr: av_mutex(%d) == 0!\n", pri);
			rc = (*av_vector)(av_intarg);
		} else {
			mutex_enter(av_mutex);
			rc = (*av_vector)(av_intarg);
			mutex_exit(av_mutex);
		}

		if (rc == 0) {
			SOFT_SPURIOUS(pri)->spurious++;
			INTR_PRINTF("cpu_intr: handler(pri) rc == 0!\n", pri);
		}
	}
}

#ifdef DEBUG
extern struct cpu cpu0;		/* should be standardized (ie. in a hdr) */

static u_int igr_sendcount[16] = {0, };
static u_int gross_softhack = 0;
static u_int gross_softcount = 0;
#endif DEBUG

#define	CPU_DEVICEID(cpu_id)	(cpu_id << 3)

/*
 * xmit_cpu_intr
 */
void
xmit_cpu_intr(u_int cpu_id, u_int pri)
{
	u_int device_id = CPU_DEVICEID(cpu_id);
	u_int msg = INTR_HIM(device_id, pri);

#ifdef DEBUG
	if (cpu_id != cpu0.cpu_id) {
		gross_softcount++;
		if (gross_softhack != 0) {
			u_char cpu_id = cpu0.cpu_id;
			u_int device_id = CPU_DEVICEID(cpu_id);
			u_int new_msg = INTR_HIM(device_id, pri);
			msg = new_msg;
		}
	}

	igr_sendcount[pri]++;
#endif DEBUG

	intr_igr_set(msg);
}

/*
 * setsoftint - primitive for ddi_impl.c
 */
void
setsoftint(u_int pri)
{
	u_char cpu_id = CPU->cpu_id;
	xmit_cpu_intr(cpu_id, pri);
}

/*
 * siron - primitive for sun/os/softint.c
 */
void
siron()
{
	setsoftint(1);
}

extern void cpu_intr(u_int);
struct autovec prof_handler;

#ifdef DEBUG
u_int x14cnt[NCPU];
u_int x14all;
#endif DEBUG

int	snooping = 0;		/* So it goes in the data, not bss */

/*
 * level14_handler - field level14 events: profiling & OBP mailbox messages
 */
void
level14_handler(struct regs *rp)
{
	extern struct cpu cpu0;

#ifdef DEBUG
	x14all++;
	x14cnt[CPU->cpu_id]++;
#endif DEBUG

	/*
	 * jump into PROM if requested
	 */
	poll_obp_mbox(rp);

	/*
	 * if profiling
	 */
	if (prof_handler.av_vector)
		(*prof_handler.av_vector)(prof_handler.av_intarg);

	if (CPU->cpu_id == cpu0.cpu_id && snooping) {
		debug_enter("deadman timer interrupt");
	}

	/*
	 * any other level14 processing should go here,
	 */
	cpu_intr(14);
}


/* eof */
