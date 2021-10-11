
#pragma ident   "@(#)ecc.c	1.41	95/09/15 SMI"

/*LINTLIBRARY*/

/*
 * Copyright (c) 1989 by Sun Microsystems, Inc.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/machparam.h>
#include <sys/machsystm.h>
#include <sys/machthread.h>
#include <sys/cpu.h>
#include <sys/cpuvar.h>
#include <vm/page.h>
#include <vm/hat.h>
#include <vm/seg.h>
#include <vm/seg_kmem.h>
#include <sys/map.h>
#include <sys/vmmac.h>
#include <sys/mman.h>
#include <sys/cmn_err.h>
#include <sys/async.h>
#include <sys/spl.h>
#include <sys/trap.h>
#include <sys/machtrap.h>
#include <sys/promif.h>
#include <sys/debug.h>
#include <sys/x_call.h>
#include <sys/ivintr.h>
#include <sys/cred.h>
#include <sys/atomic_prim.h>
#include <sys/module.h>

static void ecc_error_init(void);

static int ce_log_mem_err(struct ecc_flt *ecc);
static void ce_log_unum(int found_unum, int persistent,
		int len, char *unum, short syn_code);
static void ce_log_syn_code(short syn_code);
static void ce_scrub_mem_err();

static int ue_log_mem_err(struct ecc_flt *ecc, char *unum);
static int ue_reset_ecc(u_longlong_t *flt_addr);
static void ue_page_giveup(u_int pagenum, struct page *pp);
static int ue_check_upa_func(void);

static int kill_procs_on_page(u_longlong_t *flt_addr);
static caddr_t map_paddr_to_vaddr(u_int aligned_addr);
static void unmap_vaddr(u_int aligned_addr, caddr_t vaddr);
static int cpu_log_err(u_longlong_t *p_afsr, u_longlong_t *p_afar, u_char id);
static int cpu_log_bto_err(u_longlong_t *afsr, u_longlong_t *afar, u_char inst);
static void cpu_flush_ecache();

int ecc_gen(int high_bytes, int low_bytes);

extern u_int set_error_enable_tl1(volatile u_longlong_t *neer);

/*
 * This table used to determine which bit(s) is(are) bad when an ECC
 * error occurrs.  The array is indexed by the 8-bit syndrome which
 * comes from the Datapath Error Register.  The entries
 * of this array have the following semantics:
 *
 *      00-63   The number of the bad bit, when only one bit is bad.
 *      64      ECC bit C0 is bad.
 *      65      ECC bit C1 is bad.
 *      66      ECC bit C2 is bad.
 *      67      ECC bit C3 is bad.
 *      68      ECC bit C4 is bad.
 *      69      ECC bit C5 is bad.
 *      70      ECC bit C6 is bad.
 *      71      ECC bit C7 is bad.
 *      72      Two bits are bad.
 *      73      Three bits are bad.
 *      74      Four bits are bad.
 *      75      More than Four bits are bad.
 *      76      NO bits are bad.
 * Based on "Galaxy Memory Subsystem SPECIFICATION" rev 0.6, pg. 28.
 */
char ecc_syndrome_tab[] =
{
76, 64, 65, 72, 66, 72, 72, 73, 67, 72, 72, 73, 72, 73, 73, 74,
68, 72, 72, 32, 72, 57, 75, 72, 72, 37, 49, 72, 40, 72, 72, 44,
69, 72, 72, 33, 72, 61,  4, 72, 72, 75, 53, 72, 45, 72, 72, 41,
72,  0,  1, 72, 10, 72, 72, 75, 15, 72, 72, 75, 72, 73, 73, 72,
70, 72, 72, 42, 72, 59, 39, 72, 72, 75, 51, 72, 34, 72, 72, 46,
72, 25, 29, 72, 27, 74, 72, 75, 31, 72, 74, 75, 72, 75, 75, 72,
72, 75, 36, 72,  7, 72, 72, 54, 75, 72, 72, 62, 72, 48, 56, 72,
73, 72, 72, 75, 72, 75, 22, 72, 72, 18, 75, 72, 73, 72, 72, 75,
71, 72, 72, 47, 72, 63, 75, 72, 72,  6, 55, 72, 35, 72, 72, 43,
72,  5, 75, 72, 75, 72, 72, 50, 38, 72, 72, 58, 72, 52, 60, 72,
72, 17, 21, 72, 19, 74, 72, 75, 23, 72, 74, 75, 72, 75, 75, 72,
73, 72, 72, 75, 72, 75, 30, 72, 72, 26, 75, 72, 73, 72, 72, 75,
72,  8, 13, 72,  2, 72, 72, 73,  3, 72, 72, 73, 72, 75, 75, 72,
73, 72, 72, 73, 72, 75, 16, 72, 72, 20, 75, 72, 75, 72, 72, 75,
73, 72, 72, 73, 72, 75, 24, 72, 72, 28, 75, 72, 75, 72, 72, 75,
74, 12,  9, 72, 14, 72, 72, 75, 11, 72, 72, 75, 72, 75, 75, 74,
};
#define	SYND_TBL_SIZE 256

#define	MAX_CE_ERROR	255
#define	UNUM_NAMLEN	60
#define	MAX_SIMM	256
#define	MAX_CE_FLTS	10
#define	MAX_UE_FLTS	5

struct  ce_info {
	char    name[UNUM_NAMLEN];
	short	intermittent_cnt;
	short	persistent_cnt;
};

struct ce_info  *mem_ce_simm = NULL;
int mem_ce_simm_size = 0;

short	max_ce_err = MAX_CE_ERROR;

int	report_ce_console = 0;	/* don't print messages on console */
int	report_ce_log = 0;
int	log_ce_error = 0;
int	ce_errors_disabled = 0;

struct	ecc_flt *ce_flt = NULL;	/* correctable errors in process */
int	ce_flt_size = 0;
int	nce = 0;
int	oce = 0;
u_int	ce_inum, ce_pil = PIL_1;

struct	ecc_flt *ue_flt = NULL;	/* uncorrectable errors in process */
int	ue_flt_size = 0;
int	nue = 0;
int	oue = 0;
u_int	ue_inum, ue_pil = PIL_2;

int	user_ue_panic = 1;	/* bugid 1156625 */
int	async_err_panic = 0;
int	ce_verbose = 0;
int	ce_show_data = 0;
int	ce_debug = 0;
int	ue_verbose = 1;
int	ue_show_data = 0;
int	ue_debug = 0;
int	reset_debug = 0;

#define	MAX_UPA_FUNCS	120 /* XXX - 30 max sysio/pci devices on sunfire */
struct upa_func  register_func[MAX_UPA_FUNCS];
int nfunc = 0;

/*
 * Initialize error handling functionality. Note that we no longer need
 * to initialize the E-Cache Error Enable Register since the prom now
 * does initialize errors for the master cpu, as per bugid 1211444.
 */
void
error_init()
{
	char tmp_name[MAXSYSNAME];
	dnode_t node;

	if ((mem_ce_simm == NULL) && (ce_flt == NULL) && (ue_flt == NULL)) {
		ecc_error_init();
	}

	node = prom_rootnode();
	if ((node == OBP_NONODE) || (node == OBP_BADNODE)) {
		cmn_err(CE_CONT, "error_init: node 0x%x\n", (u_int)node);
		return;
	}
	if ((prom_getproplen(node, "reset-reason") != -1) &&
	    (prom_getprop(node, "reset-reason", tmp_name) != -1)) {
		if (reset_debug) {
			cmn_err(CE_CONT,
			    "System booting after %s\n", tmp_name);
		} else if (strncmp(tmp_name, "FATAL", 5) == 0) {
			cmn_err(CE_CONT,
			    "System booting after fatal error %s\n", tmp_name);
		}
	}
}

/*
 * Allocate error arrays based on ncpus.
 */
static void
ecc_error_init(void)
{
	register int size;
	extern int ncpus;

	mem_ce_simm_size = MAX_SIMM * ncpus;
	size = ((sizeof (struct ce_info)) * mem_ce_simm_size);
	mem_ce_simm = (struct ce_info *)kmem_zalloc(size, KM_SLEEP);
	if (mem_ce_simm == NULL) {
		cmn_err(CE_PANIC, "No space for CE unum initialization");
	}

	ce_flt_size = MAX_CE_FLTS * ncpus;
	size = ((sizeof (struct ecc_flt)) * ce_flt_size);
	ce_flt = (struct ecc_flt *)kmem_zalloc(size, KM_SLEEP);
	if (ce_flt == NULL) {
		cmn_err(CE_PANIC, "No space for CE error initialization");
	}
	ce_inum = add_softintr(ce_pil, handle_ce_error, (caddr_t)ce_flt, 0);

	ue_flt_size = MAX_UE_FLTS * ncpus;
	size = ((sizeof (struct ecc_flt)) * ue_flt_size);
	ue_flt = (struct ecc_flt *)kmem_zalloc(size, KM_SLEEP);
	if (ue_flt == NULL) {
		cmn_err(CE_PANIC, "No space for UE error initialization");
	}
	ue_inum = add_softintr(ue_pil, handle_ue_error, (caddr_t)ue_flt, 0);
}

/*
 * can be called from setup_panic at pil > XCALL_PIL, so use xt_all
 */
void
error_disable()
{
	register int n, nf;
	caddr_t arg;
	volatile u_longlong_t neer = 0;
	afunc errdis_func;

	xt_all((u_int)&set_error_enable_tl1, (u_int)&neer,
		(u_int)0, (u_int)0, (u_int)0);
	nf = nfunc;
	for (n = 0; n < nf; n++) {
		if (register_func[n].ftype != DIS_ERR_FTYPE)
			continue;
		errdis_func = register_func[n].func;
		ASSERT(errdis_func != NULL);
		arg = register_func[n].farg;
		(void) (*errdis_func)(arg);
	}
}

int
ue_error(u_longlong_t *afsr, u_longlong_t *afar, u_char ecc_synd,
	u_char size, u_char offset, u_short id, u_short inst, afunc log_func)
{
	register int tnue;
	union ull {
		u_longlong_t	afsr;
		u_longlong_t	afar;
		u_int		i[2];
	} j, k;

	j.afsr = *afsr;
	k.afar = *afar;
	if (ue_flt == NULL) {		/* ring buffer not initialized */
		cmn_err(CE_PANIC,
"UE Error init: AFSR 0x%08x %08x AFAR 0x%08x %08x Synd 0x%x Id %d Inst %d",
			j.i[0], j.i[1], k.i[0], k.i[1], ecc_synd, id, inst);
		/* NOTREACHED */
	}

	tnue = atinc_cidx_word(&nue, ue_flt_size);
	if (ue_flt[tnue].flt_in_proc == 1) {	/* ring buffer wrapped */
		cmn_err(CE_PANIC,
"UE Error space: AFSR 0x%08x %08x AFAR 0x%08x %08x Synd 0x%x Id %d Inst %d",
			j.i[0], j.i[1], k.i[0], k.i[1], ecc_synd, id, inst);
		/* NOTREACHED */
	}

	ue_flt[tnue].flt_in_proc = 1;
	ue_flt[tnue].flt_stat = *afsr;
	ue_flt[tnue].flt_addr = *afar;
	ue_flt[tnue].flt_synd = ecc_synd;
	ue_flt[tnue].flt_size = size;
	ue_flt[tnue].flt_offset = offset;
	ue_flt[tnue].flt_upa_id = id;
	ue_flt[tnue].flt_inst = inst;
	ue_flt[tnue].flt_func = log_func;
	setsoftint(ue_inum);
	return (0);
}

u_int
handle_ue_error(struct ecc_flt *pue)
{
	static char buf[UNUM_NAMLEN];
	char *unum = &buf[0];
	struct ecc_flt ecc;
	struct ecc_flt *pecc = &ecc;
	register u_int fatal = 0;
	register int toue;
	union ull {
		u_longlong_t	afsr;
		u_longlong_t	afar;
		u_int		i[2];
	} j, k;

	toue = atinc_cidx_word(&oue, ue_flt_size);
	if (pue[toue].flt_in_proc != 1) { /* Ring buffer lost in space */
		cmn_err(CE_PANIC, "Harderror queue out of sync");
		/* NOTREACHED */
	}
	ecc.flt_stat = pue[toue].flt_stat;
	ecc.flt_addr = pue[toue].flt_addr;
	ecc.flt_synd = pue[toue].flt_synd;
	ecc.flt_size = pue[toue].flt_size;
	ecc.flt_offset = pue[toue].flt_offset;
	ecc.flt_upa_id = pue[toue].flt_upa_id;
	ecc.flt_inst = pue[toue].flt_inst;
	ecc.flt_func = pue[toue].flt_func;
	pue[oue].flt_in_proc = 0;

	j.afsr = ecc.flt_stat;
	k.afar = ecc.flt_addr;
	fatal = ue_log_mem_err(pecc, unum);
	if (fatal == 2) {
		return (1);		/* XXX - hack alert for sysio */
	} else if (fatal == 1) {
		cmn_err(CE_PANIC,
"Harderror: AFSR 0x%08x %08x AFAR 0x%08x %08x Id %d Inst %d SIMM %s",
		j.i[0], j.i[1], k.i[0], k.i[1],
		ecc.flt_upa_id, ecc.flt_inst, unum);
	} else if (user_ue_panic) {	/* bugid 1156625 */
		cmn_err(CE_PANIC,
"User Harderror: AFSR 0x%08x %08x AFAR 0x%08x %08x Id %d Inst %d SIMM %s",
		j.i[0], j.i[1], k.i[0], k.i[1],
		ecc.flt_upa_id, ecc.flt_inst, unum);
	} else {
		(void) handle_kill_proc(&j.afsr, &k.afar, unum);
	}
	return (1);
}

static int
ue_log_mem_err(struct ecc_flt *ecc, char *unum)
{
	int len = 0, fatal = 0;
	union ull {
		u_longlong_t	afar;
		u_int		i[2];
	} k;
	afunc log_func;

	k.afar = ecc->flt_addr;
	k.i[1] &= 0xFFFFFFF8;	/* byte alignment for get-unumber */
	(void) prom_get_unum(-1, k.i[1], k.i[0], unum, UNUM_NAMLEN, &len);
	if (len <= 1)
		(void) sprintf(unum, "%s", "Decoding Failed");

	/*
	 * Check for SDB copyout on other cpu(s).
	 * Don't bother to change cpu_check_cp to a xt/tl1 function for now,
	 * as we know that we are calling this function at ue_pil = PIL_2;
	 * Check for Sysio DVMA and PIO parity errors.
	 */
	xc_all(cpu_check_cp, (u_int)ecc, 0);
	ue_check_upa_func();

	/*
	 * Call specific error logging routine.
	 */
	log_func = ecc->flt_func;
	(void) ue_reset_ecc(&k.afar);
	if (log_func != NULL) {
		fatal += (*log_func)(ecc, unum);
	}
	return (fatal);
}

static int
ue_check_upa_func(void)
{
	afunc ue_func;
	caddr_t arg;

	register int n, nf;
	int fatal = 0;

	nf = nfunc;
	for (n = 0; n < nf; n++) {
		if (register_func[n].ftype != UE_ECC_FTYPE)
			continue;
		ue_func = register_func[n].func;
		ASSERT(ue_func != NULL);
		arg = register_func[n].farg;
		fatal = (*ue_func)(arg);
	}
	return (fatal);
}

/*
 * hat_kill_procs does not currently work, as per bugid 1156625.
 */
u_int
handle_kill_proc(u_longlong_t *afsr, u_longlong_t *afar, char *unum)
{
	int fatal = 0;
	union ul {
		u_longlong_t	afsr;
		u_longlong_t	afar;
		u_int		i[2];
	} j, k;

	j.afar = *afsr;
	k.afar = *afar;
	fatal = kill_procs_on_page(&k.afar);
	if (fatal) {
		if (unum) {
			cmn_err(CE_PANIC,
"UE User Error Killing Processes: AFSR 0x%08x %08x AFAR 0x%08x %08x unum %s",
				j.i[0], j.i[1], k.i[0], k.i[1], unum);
		} else {
			cmn_err(CE_PANIC,
	"User Error Killing Processes: AFSR 0x%08x %08x AFAR 0x%08x %08x",
				j.i[0], j.i[1], k.i[0], k.i[1]);
		}
	}
	return (fatal);
}

/*
 * Kill all user processes using a certain page
 */
static int
kill_procs_on_page(u_longlong_t *flt_addr)
{
	struct page *pp;
	u_int pagenum;
	union ull {
		u_longlong_t	afar;
		u_int		i[2];
	} k;
	caddr_t addr;

	k.afar = *flt_addr;
	addr = (caddr_t)k.i[1];
	/* pagenum = *flt_addr >> MMU_PAGESHIFT; */
	pagenum = k.i[0] >> MMU_PAGESHIFT;
	pagenum |= k.i[1] >> MMU_PAGESHIFT;
	pp = page_numtopp(pagenum, SE_EXCL);
	if (pp == (page_t *)NULL) {
		cmn_err(CE_CONT, "Error recovery: no page structure\n");
		return (-1);
	}
	if (pp->p_vnode == 0) {
		cmn_err(CE_CONT, "Error recovery: no vnode\n");
		return (-1);
	}
	hat_pagesync(pp, HAT_ZERORM);
	/* XXX - addr should be a vaddr, for the signal info */
	if (PP_ISMOD(pp)) {
		if (hat_kill_procs(pp, addr) != 0) {
			return (-1);
		}
	}
	return (0);
}

static int
ue_reset_ecc(u_longlong_t *flt_addr)
{
	volatile u_longlong_t neer;
	u_int aligned_addr;
	caddr_t vaddr;
	register int stuck_bit = 0;
	union ul {
		u_longlong_t	afsr;
		u_longlong_t	afar;
		u_int		i[2];
	} j, k, l;
	extern cpuset_t cpu_ready_set;
	extern int fpu_exists;

	/*
	 * XXX - cannot do block commit instructions w/out fpu regs,
	 *	 we may not emulate this unless we have a reasonable
	 *	 way to flush the cache(s)
	 */
	if (fpu_exists == 0)
		return (-1);
	/*
	 * 64 byte alignment for block load/store operations
	 */
	k.afar = *flt_addr;
	aligned_addr = k.i[1] & 0xFFFFFFC0;
	vaddr = map_paddr_to_vaddr(aligned_addr);
	if (vaddr == NULL)
		return (1);

	/*
	 * disable ECC errors, flush the cache(s)
	 */
	xc_attention(cpu_ready_set);

	neer = EER_ISAPEN;
	xt_all((u_int)&set_error_enable_tl1, (u_int)&neer, (u_int)0,
			(u_int)0, (u_int)0);
	cpu_flush_ecache();

	reset_ecc(vaddr);

	/*
	 * clear any ECC errors
	 */
	get_asyncflt(&j.afsr);
	get_asyncaddr(&l.afar);
	if ((j.afsr & P_AFSR_UE) || (j.afsr & P_AFSR_CE)) {
		clr_datapath();
		if (j.afsr & P_AFSR_UE)
			stuck_bit = 1;
		if (ue_debug)
			cmn_err(CE_CONT,
			"\tue_reset_ecc: AFSR 0x%08x %08x AFAR 0x%08x %08x\n",
				j.i[0], j.i[1], l.i[0], l.i[1]);
		set_asyncflt(&j.afsr);
	}
	xc_dismissed(cpu_ready_set);
	unmap_vaddr(aligned_addr, vaddr);

	if (stuck_bit) {
		struct page *pp;
		u_int pagenum;

		/* pagenum = *flt_addr >> MMU_PAGESHIFT; */
		pagenum = k.i[0] >> MMU_PAGESHIFT;
		pagenum |= k.i[1] >> MMU_PAGESHIFT;
		pp = page_numtopp(pagenum, SE_EXCL);
		ue_page_giveup(pagenum, pp);
	}
	return (0);
}

static void
ue_page_giveup(u_int pagenum, struct page *pp)
{
	int fl = B_INVAL;

	hat_pageunload(pp);
	VN_DISPOSE(pp, fl, -1, kcred);
	cmn_err(CE_CONT, "page %x marked out of service.\n", ptob(pagenum));
}

int
ce_error(u_longlong_t *afsr, u_longlong_t *afar, u_char ecc_synd,
	u_char size, u_char offset, u_short id, u_short inst, afunc log_func)
{
	register int tnce, nnce;
	u_char flt_in_proc = 1;

	ASSERT(ce_flt != NULL);
	tnce = atinc_cidx_word(&nce, ce_flt_size);
	if (ce_flt[tnce].flt_in_proc == 1) {	/* ring buffer wrapped */
		if (ce_errors_disabled)		/* normal */
			return (0);
		else				/* abnormal */
			flt_in_proc = 3;
	}

	/*
	 * Check 2 places away in the ring buffer, and turn off
	 * correctable errors if we are about to fill up our ring buffer.
	 */
	if ((nnce = tnce + 2) > ce_flt_size)
		nnce = 0;
	if (ce_flt[nnce].flt_in_proc == 1) {
		ce_errors_disabled = 1;
		flt_in_proc = 2;
	}

	ce_flt[tnce].flt_in_proc = flt_in_proc;
	ce_flt[tnce].flt_stat = *afsr;
	ce_flt[tnce].flt_addr = *afar;
	ce_flt[tnce].flt_synd = ecc_synd;
	ce_flt[tnce].flt_size = size;
	ce_flt[tnce].flt_offset = offset;
	ce_flt[tnce].flt_upa_id = id;
	ce_flt[tnce].flt_inst = inst;
	ce_flt[tnce].flt_func = log_func;
	setsoftint(ce_inum);
	return (0);
}

u_int
handle_ce_error(struct ecc_flt *pce)
{
	struct ecc_flt ecc;
	struct ecc_flt *pecc = &ecc;
	register int toce;
	volatile u_longlong_t neer;
	extern int noprintf;

	toce = atinc_cidx_word(&oce, ce_flt_size);
	if (pce[toce].flt_in_proc == 2) {	/* Ring buffer almost full */
		neer = (EER_ISAPEN | EER_NCEEN);
		xt_all((u_int)&set_error_enable_tl1, (u_int)&neer, (u_int)0,
			(u_int)0, (u_int)0);
		if (ce_verbose || ce_debug)
			cmn_err(CE_CONT, "Disabled softerrors\n");
	} else if (pce[toce].flt_in_proc == 3) { /* Ring buffer wrapped */
		cmn_err(CE_CONT, "CE Error queue wrapped\n");
		return (1);
	} else if (pce[toce].flt_in_proc != 1) { /* Ring buffer lost in space */
		cmn_err(CE_CONT, "CE Error queue out of sync\n");
		return (1);
	}
	ecc.flt_stat = pce[toce].flt_stat;
	ecc.flt_addr = pce[toce].flt_addr;
	ecc.flt_synd = pce[toce].flt_synd;
	ecc.flt_size = pce[toce].flt_size;
	ecc.flt_offset = pce[toce].flt_offset;
	ecc.flt_upa_id = pce[toce].flt_upa_id;
	ecc.flt_inst = pce[toce].flt_inst;
	ecc.flt_func = pce[toce].flt_func;
	pce[toce].flt_in_proc = 0;

	if (report_ce_log || report_ce_console)
		log_ce_error = 1;
	if (report_ce_console)
		noprintf = 0;
	else
		noprintf = 1;
	if (ce_log_mem_err(pecc)) {	/* only try to scrub memory errors */
		ce_scrub_mem_err(pecc);
	}
	if (noprintf)
		noprintf = 0;
	log_ce_error = 0;

	/* we just freed up a space, so turn errors back on */
	if (ce_errors_disabled) {
		neer = (EER_ISAPEN | EER_NCEEN | EER_CEEN);
		xt_all((u_int)&set_error_enable_tl1, (u_int)&neer, (u_int)0,
			(u_int)0, (u_int)0);
		ce_errors_disabled = 0;
	}
	return (1);
}

static int
ce_log_mem_err(struct ecc_flt *ecc)
{
	short syn_code, found_unum = 0;
	static char buf[UNUM_NAMLEN];
	char *unum = &buf[0];
	int len = 0, offset = 0;
	int persistent = 0, memory_error = 1;
	short loop, ce_err = 1;
	u_int aligned_addr;
	union ull {
		u_longlong_t	afar;
		u_longlong_t	afsr;
		u_int		i[2];
	} j, k;
	afunc log_func;

	j.afsr = ecc->flt_stat;
	k.afar = ecc->flt_addr;
	/*
	 * Use the 8-bit syndrome to index the ecc_syndrome_tab to get
	 * the code indicating which bit(s) is(are) bad.
	 */
	if ((ecc->flt_synd <= 0) || (ecc->flt_synd >= SYND_TBL_SIZE)) {
		cmn_err(CE_CONT,
"CE Error: AFSR 0x%08x %08x AFAR 0x%08x %08x Bad Syndrome 0x%x Id %d Inst %d\n",
			j.i[0], j.i[1], k.i[0], k.i[1],
			ecc->flt_synd, ecc->flt_upa_id, ecc->flt_inst);
		syn_code = 0;
	} else {
		syn_code = ecc_syndrome_tab[ecc->flt_synd];
	}

	/*
	 * Size of CPU transfer is 3 and offset 0 (ie, 8 byte aligned), may be
	 * larger for SYSIO, etc. byte alignment required for get-unumber.
	 */
	if (ecc->flt_size > 3)
		offset = ecc->flt_offset * 8;

	if (syn_code < 72) {
		if (syn_code < 64)
			offset = offset + (7 - syn_code / 8);
		else
			offset = offset + (7 - syn_code % 8);
		aligned_addr = (k.i[1] + offset) & 0xFFFFFFF8;
		(void) prom_get_unum((int)syn_code, aligned_addr, k.i[0],
					unum, UNUM_NAMLEN, &len);
		if (len > 1) {
			found_unum = 1;
		} else {
			(void) sprintf(unum, "%s", "Decoding Failed");
		}
	} else if (syn_code < 76) {
		aligned_addr = k.i[1] & 0xFFFFFFF8;
		(void) prom_get_unum(-1, aligned_addr, k.i[0], unum,
					UNUM_NAMLEN, &len);
		if (len <= 1)
			(void) sprintf(unum, "%s", "Decoding Failed");
		cmn_err(CE_PANIC,
"CE/UE Error: AFSR 0x%08x%08x AFAR 0x%08x%08x Synd 0x%x Id %d Inst %d SIMM %s",
			j.i[0], j.i[1], k.i[0], k.i[1],
			ecc->flt_synd, ecc->flt_upa_id, ecc->flt_inst, unum);
	}

	/*
	 * Call specific error logging routine.
	 * Note that if we want to save information about non-memory errors,
	 * we need to find another way, not related to the unum, of saving
	 * this info, because otherwise we lose all the pertinent related info.
	 * If the specific error logging routine says it's possibly a
	 * memory error, then check if the error is persistent.
	 * We will get a tiny number of not-really-intermittent memory
	 * errors from bus and/or uncorrectable error overwrites.
	 */
	log_func = ecc->flt_func;
	if (log_func != NULL) {
		memory_error += (*log_func)(ecc, unum);
		if (memory_error) {
			if (ecc->flt_addr != 0) {
				loop = 1;
				if (ecc->flt_size == 3) {
					aligned_addr = k.i[1] & 0xFFFFFFF8;
				} else {
					aligned_addr = k.i[1] & 0xFFFFFFF0 +
						(ecc->flt_offset * 8);
				}
				persistent = read_ecc_data(aligned_addr, loop,
							ce_err, 0);
			}
		} else {
			cmn_err(CE_CONT,
				"Non-memory-related Correctable ECC Error.\n");
		}
	}

	/*
	 * Do not bother to log non-memory CE errors... the relevant
	 * CE memory error logging routine should be verbose about these
	 * errors.
	 */
	if (memory_error)
		ce_log_unum(found_unum, persistent, len, unum, syn_code);
	if ((!(memory_error)) || (log_ce_error) || (ce_verbose))
		ce_log_syn_code(syn_code);

	/* Display entire cache line */
	if ((ce_show_data) && (ecc->flt_addr != 0)) {
		loop = 8;
		aligned_addr = k.i[1] & 0xFFFFFFF0;
		(void) read_ecc_data(aligned_addr, loop, ce_err, 1);
	}

	return (persistent);
}

static void
ce_log_unum(int found_unum, int persistent,
		int len, char *unum, short syn_code)
{
	register int i;
	struct  ce_info *psimm = mem_ce_simm;

	ASSERT(psimm != NULL);
	if (found_unum) {
	    for (i = 0; i < mem_ce_simm_size; i++) {
		if (psimm[i].name[0] == NULL) {
			(void) strncpy(psimm[i].name, unum, len);
			if (persistent) {
				psimm[i].persistent_cnt = 1;
				psimm[i].intermittent_cnt = 0;
			} else {
				psimm[i].persistent_cnt = 0;
				psimm[i].intermittent_cnt = 1;
			}
			break;
		} else if (strncmp(unum, psimm[i].name, len) == 0) {
			if (persistent)
				psimm[i].persistent_cnt += 1;
			else
				psimm[i].intermittent_cnt += 1;
			if ((psimm[i].persistent_cnt +
			    psimm[i].intermittent_cnt) > max_ce_err) {
				cmn_err(CE_CONT,
					"Multiple Softerrors: ");
				cmn_err(CE_CONT,
			"Seen %d Intermittent and %d Corrected Softerrors ",
					psimm[i].intermittent_cnt,
					psimm[i].persistent_cnt);
				cmn_err(CE_CONT, "from SIMM %s\n", unum);
				cmn_err(CE_CONT,
					"\tCONSIDER REPLACING THE SIMM.\n");
				psimm[i].persistent_cnt = 0;
				psimm[i].intermittent_cnt = 0;
				log_ce_error = 1;
			}
			break;
		}
	    }
	    if (i >= mem_ce_simm_size)
		cmn_err(CE_CONT, "Softerror: mem_ce_simm[] out of space.\n");
	}

	if (log_ce_error) {
		if (persistent) {
			cmn_err(CE_CONT,
				"Softerror: Persistent ECC Memory Error");
			if (unum != "?") {
				if (syn_code < 72)
					cmn_err(CE_CONT,
						" Corrected SIMM %s\n", unum);
				else
					cmn_err(CE_CONT,
					" Possible Corrected SIMM %s\n",
						unum);
			} else {
				cmn_err(CE_CONT, "\n");
			}
		} else {
			cmn_err(CE_CONT,
			"Softerror: Intermittent ECC Memory Error SIMM %s\n",
				unum);
		}
	}
}


static void
ce_log_syn_code(short syn_code)
{
	if (syn_code < 64) {
		cmn_err(CE_CONT, "\tECC Data Bit %2d was corrected", syn_code);
	} else if (syn_code < 72) {
		cmn_err(CE_CONT, "\tECC Check Bit %2d was corrected",
			syn_code - 64);
	} else {
		switch (syn_code) {
		case 72:
		    cmn_err(CE_CONT, "\tTwo ECC Bits were corrected");
		    break;
		case 73:
		    cmn_err(CE_CONT, "\tThree ECC Bits were corrected");
		    break;
		case 74:
		    cmn_err(CE_CONT, "\tFour ECC Bits were corrected");
		    break;
		case 75:
		    cmn_err(CE_CONT, "\tMore than Four ECC Bits ");
		    cmn_err(CE_CONT, "were corrected");
		    break;
		default:
		    break;
		}
	}
	cmn_err(CE_CONT, "\n");
}

static void
ce_scrub_mem_err(struct ecc_flt *ecc)
{
	volatile u_longlong_t neer;
	u_int aligned_addr;
	caddr_t vaddr;
	union ul {
		u_longlong_t	afsr;
		u_longlong_t	afar;
		u_int		i[2];
	} j, k;
	extern cpuset_t cpu_ready_set;
	extern int fpu_exists;

	/*
	 * XXX - cannot do block commit instructions w/out fpu regs,
	 *	 we may not emulate this unless we have a reasonable
	 *	 way to flush the cache(s)
	 */
	if (fpu_exists == 0)
		return;

	/*
	 * 64 byte alignment for block load/store operations
	 * try to map paddr before called xc_attention...
	 */
	k.afar = ecc->flt_addr;
	aligned_addr = k.i[1] & 0xFFFFFFC0;
	vaddr = map_paddr_to_vaddr(aligned_addr);
	if (vaddr == NULL)
		return;

	/*
	 * disable ECC errors, flush and reenable the cache(s), then
	 * scrub memory, then check afsr for errors, and reenable errors
	 * XXX - For Spitfire, just flush the whole ecache because it's
	 *	 only 2 pages, change for a different implementation.
	 *	 disable ECC errors, flush cache(s)
	 */

	xc_attention(cpu_ready_set);

	neer = EER_ISAPEN;
	xt_all((u_int)&set_error_enable_tl1, (u_int)&neer, (u_int)0,
			(u_int)0, (u_int)0);
	cpu_flush_ecache();

	scrubphys(vaddr);

	/*
	 * clear any ECC errors
	 */
	get_asyncflt(&j.afsr);
	get_asyncaddr(&k.afar);
	if ((j.afsr & P_AFSR_UE) || (j.afsr & P_AFSR_CE)) {
		clr_datapath();
		if (ce_debug)
			cmn_err(CE_CONT,
		"\tce_scrub_mem_err: AFSR 0x%08x %08x AFAR 0x%08x %08x\n",
					j.i[0], j.i[1], k.i[0], k.i[1]);
		set_asyncflt(&j.afsr);
	}
	/*
	 * enable ECC errors, unmap vaddr
	 */
	neer |= (EER_NCEEN | EER_CEEN);
	xt_all((u_int)&set_error_enable_tl1, (u_int)&neer, (u_int)0,
			(u_int)0, (u_int)0);

	xc_dismissed(cpu_ready_set);
	unmap_vaddr(aligned_addr, vaddr);
}

static caddr_t
map_paddr_to_vaddr(u_int aligned_addr)
{
	u_long a;
	struct page *pp;
	caddr_t cvaddr, vaddr;
	u_int pfn, pagenum, pgoffset, len = 1;
	extern int pf_is_memory(uint);

	pagenum = aligned_addr >> MMU_PAGESHIFT;
	pgoffset = aligned_addr & MMU_PAGEOFFSET;
	pp = page_numtopp(pagenum, SE_SHARED);
	if (pp == NULL) {
		if ((ce_debug) || (ue_debug))
			cmn_err(CE_CONT,
		"\tmap_paddr_to_vaddr: aligned_addr 0x%x, pagenum 0x%x\n",
			aligned_addr, pagenum);
		return (NULL);
	}
	pfn = pp->p_pagenum;
	if (pf_is_memory(pfn)) {
		a = rmalloc(kernelmap, len);
		if (a == NULL) {
			if ((ce_debug) || (ue_debug))
				cmn_err(CE_CONT,
			"\tmap_paddr_to_vaddr: aligned_addr 0x%x, len 0x%x\n",
				aligned_addr, len);
			return (NULL);
		}
		cvaddr = (caddr_t) kmxtob(a);
		segkmem_mapin(&kvseg, cvaddr, (u_int)mmu_ptob(len),
				(PROT_READ | PROT_WRITE), pfn, HAT_NOCONSIST);
		vaddr = (caddr_t) (cvaddr + pgoffset);
		return (vaddr);
	} else {
		if ((ce_debug) || (ue_debug))
			cmn_err(CE_CONT,
		"\tmap_paddr_to_vaddr: aligned_addr 0x%x, pp 0x%x, pfn 0x%x\n",
			aligned_addr, pp, pfn);
		return (NULL);
	}
}

static void
unmap_vaddr(u_int aligned_addr, caddr_t vaddr)
{
	caddr_t a, cvaddr;
	u_int pgoffset, len = 1;
	extern struct seg kvseg;

	pgoffset = aligned_addr & MMU_PAGEOFFSET;
	cvaddr = (caddr_t) (vaddr - pgoffset);
	segkmem_mapout(&kvseg, cvaddr, (u_int)mmu_ptob(len));
	a = (caddr_t) (btokmx(cvaddr));
	rmfree(kernelmap, len, (u_long) a);
}

int
read_ecc_data(u_int aligned_addr, short loop, short ce_err, short verbose)
{
	union {
		volatile u_longlong_t	afsr;
		volatile u_longlong_t	afar;
		u_int			i[2];
	} j, k;
	register short i;
	int persist = 0;
	u_int ecc_0;
	caddr_t paddr;
	volatile u_longlong_t neer;
	union {
		u_longlong_t data;
		u_int i[2];
	} d;
	extern cpuset_t cpu_ready_set;

	/*
	 * disable ECC errors, flush and reenable the caches, read the data
	 */
	xc_attention(cpu_ready_set);

	neer = EER_ISAPEN;
	xt_all((u_int)&set_error_enable_tl1, (u_int)&neer, (u_int)0,
			(u_int)0, (u_int)0);
	cpu_flush_ecache();

	for (i = 0; i < loop; i++) {
		paddr = (caddr_t) (aligned_addr + (i * 8));
		read_paddr_data(paddr, ASI_MEM, &d.data);
		if (verbose) {
			if (ce_err) {
				ecc_0 = ecc_gen(d.i[0], d.i[1]);
				cmn_err(CE_CONT,
				"\tPaddr 0x%08x, Data 0x%08x %08x, ECC 0x%x\n",
				paddr, d.i[0], d.i[1], ecc_0);
			} else {
				cmn_err(CE_CONT,
				"\tPaddr 0x%08x, Data 0x%08x %08x\n",
				paddr, d.i[0], d.i[1]);
			}
		}
	}
	get_asyncflt(&j.afsr);
	get_asyncaddr(&k.afar);
	if ((j.afsr & (u_int)P_AFSR_UE) || (j.afsr & (u_int)P_AFSR_CE)) {
		if ((ce_debug) || (ue_debug))
			cmn_err(CE_CONT,
			"\tread_ecc_data: AFSR 0x%08x %08x AFAR 0x%08x %08x\n",
			j.i[0], j.i[1], k.i[0], k.i[1]);
		clr_datapath();
		set_asyncflt(&j.afsr);
		persist = 1;
	}
	neer |= (EER_NCEEN | EER_CEEN);
	xt_all((u_int)&set_error_enable_tl1, (u_int)&neer, (u_int)0,
			(u_int)0, (u_int)0);

	xc_dismissed(cpu_ready_set);

	return (persist);
}

struct {		/* sec-ded-s4ed ecc code */
	long hi, lo;
} ecc_code[8] = {
	0xee55de23, 0x16161161,
	0x55eede93, 0x61612212,
	0xbb557b8c, 0x49494494,
	0x55bb7b6c, 0x94948848,
	0x16161161, 0xee55de23,
	0x61612212, 0x55eede93,
	0x49494494, 0xbb557b8c,
	0x94948848, 0x55bb7b6c,
};

int
ecc_gen(int high_bytes, int low_bytes)
{
	int i, j;
	u_char checker, bit_mask;
	struct {
		unsigned long hi, lo;
	} hex_data, masked_data[8];

	hex_data.hi = high_bytes;
	hex_data.lo = low_bytes;

	/* mask out bits according to sec-ded-s4ed ecc code */
	for (i = 0; i < 8; i++) {
		masked_data[i].hi = hex_data.hi & ecc_code[i].hi;
		masked_data[i].lo = hex_data.lo & ecc_code[i].lo;
	}

	/*
	 * xor all bits in masked_data[i] to get bit_i of checker,
	 * where i = 0 to 7
	 */
	checker = 0;
	for (i = 0; i < 8; i++) {
		bit_mask = 1 << i;
		for (j = 0; j < 32; j++) {
			if (masked_data[i].lo & 1) checker ^= bit_mask;
			if (masked_data[i].hi & 1) checker ^= bit_mask;
			masked_data[i].hi >>= 1;
			masked_data[i].lo >>= 1;
		}
	}
	return (checker);
}

void
register_upa_func(type, func, arg)
u_short type;
afunc func;
caddr_t arg;
{
	register int n;

	n = atinc_cidx_word(&nfunc, MAX_UPA_FUNCS);
	register_func[n].ftype = type;
	register_func[n].func = func;
	register_func[n].farg = arg;
}

/*
 * correctable ecc errors from the cpu
 * As per machcpuvar.h note: "The mid is the same as the cpu id.
 * We might want to change this later"
 */
/* ARGSUSED */
void
cpu_ce_error(struct regs *rp, u_int p_afsr1, u_int p_afar1,
	u_int dp_err, u_int p_afar0)
{
	u_short sdbh, sdbl;
	u_char e_syndh, e_syndl;
	u_char size = 3;	/* 8 byte alignment */
	u_short id = (u_short) getprocessorid();
	u_short inst = (u_short) CPU->cpu_id;
	union ul {
		u_longlong_t	afsr;
		u_longlong_t	afar;
		u_int		i[2];
	} j, k;

	j.i[0] = (dp_err & 0x1);
	j.i[1] = p_afsr1;
	k.i[0] = p_afar0;
	k.i[1] = p_afar1 & ~0xf;
	sdbh = (u_short) ((dp_err >> 1) & 0x3FF);
	sdbl = (u_short) ((dp_err >> 11) & 0x3FF);
	e_syndh = (u_char) (sdbh & (u_int)P_DER_E_SYND);
	e_syndl = (u_char) (sdbl & (u_int)P_DER_E_SYND);

	if ((sdbl >> 8) & 1) {
		(void) ce_error(&j.afsr, &k.afar, e_syndl, size, 0, id, inst,
				cpu_log_ce_err);
	}
	if ((sdbh >> 8) & 1) {
		k.i[1] |= 0x8;		/* set bit 3 if error in sdbh */
		(void) ce_error(&j.afsr, &k.afar, e_syndh, size, 0, id, inst,
				cpu_log_ce_err);
	}
	if ((((sdbl >> 8) & 1) == 0) && (((sdbh >> 8) & 1) == 0)) {
		/* ECC error with no SDB info */
		(void) ce_error(&j.afsr, &k.afar, 0, size, 0, id, inst,
				cpu_log_ce_err);
	}
}

/*
 * As per machcpuvar.h note: "The mid is the same as the cpu id.
 * We might want to change this later"
 */
u_int
cpu_log_ce_err(struct ecc_flt *ecc, char *unum)
{
	union ul {
		u_longlong_t	afsr;
		u_longlong_t	afar;
		u_int		i[2];
	} j, k;
	j.afsr = ecc->flt_stat;
	k.afar = ecc->flt_addr;

		/* overwrite policy - this message should never occur! */
	if (j.afsr & P_AFSR_UE) {
		cmn_err(CE_CONT,
"CPU%d CE Error: AFSR 0x%08x %08x AFAR 0x%08x %08x Overwritten by Harderror\n",
			ecc->flt_inst, j.i[0], j.i[1], k.i[0], k.i[1]);
		return (1);
	}

	if (ecc->flt_synd == 0)
		cmn_err(CE_CONT,
			"CE Error: CPU%d ECC Error With No SDB Info\n",
				ecc->flt_inst);

	if ((ce_verbose) || (ecc->flt_synd == 0)) {
		cmn_err(CE_CONT,
		"CPU%d CE Error: AFSR 0x%08x %08x, AFAR 0x%08x %08x, SIMM %s\n",
			ecc->flt_inst, j.i[0], j.i[1], k.i[0], k.i[1], unum);
		cmn_err(CE_CONT,
			"\tSyndrome 0x%x, Size %d, Offset %d UPA MID %d\n",
			ecc->flt_synd, ecc->flt_offset,
			ecc->flt_size, ecc->flt_upa_id);
	}
	return (1);		/* always memory related */
}

/*
 * As per machcpuvar.h note: "The mid is the same as the cpu id.
 * We might want to change this later"
 */
u_int
cpu_check_cp(struct ecc_flt *ecc)
{
	union ul {
		u_longlong_t	afsr;
		u_longlong_t	afar;
		u_int		i[2];
	} j, k;

	j.afsr = ecc->flt_stat;
	k.afar = ecc->flt_addr;
	/* Check afsr for fatal cp bit.  */
	get_asyncflt(&j.afsr);
	get_asyncaddr(&k.afar);
	/*  if (afsr & P_AFSR_CP) */
	if (j.i[1] & P_AFSR_CP) {
		cmn_err(CE_PANIC,
"CPU%d UE Error: ECache Copyout on CPU%d: AFSR 0x%08x %08x AFAR 0x%08x %08x",
		ecc->flt_inst, CPU->cpu_id, j.i[0], j.i[1], k.i[0], k.i[1]);
		/* NOTREACHED */
	}
	return (0);
}

/*
 * Access error trap handler for asynchronous cpu errors.
 * This routine is called to handle a data or instruction access error.
 * All fatal failures are completely handled by this routine
 * (by panicing).  Since handling non-fatal failures would access
 * data structures which are not consistent at the time of this
 * interrupt, these non-fatal failures are handled later in a
 * soft interrupt at a lower level.
 *
 * As per machcpuvar.h note: "The mid is the same as the cpu id.
 * We might want to change this later"
 */
/* ARGSUSED */
void
cpu_async_error(struct regs *rp, u_int p_afsr1, u_int p_afar1,
	u_int dp_err, u_int p_afar0)
{
	u_short sdbh, sdbl;
	u_char e_syndh, e_syndl;
	u_short id = (u_short) getprocessorid();
	u_short inst = (u_short) CPU->cpu_id;
	u_int fatal = 0;
	volatile u_longlong_t neer;
	union ul {
		u_longlong_t	afsr;
		u_longlong_t	afar;
		u_int		i[2];
	} j, k;
	volatile greg_t pil;
	extern int pokefault;
	extern greg_t getpil();


	j.i[0] = (dp_err & 0x1);
	j.i[1] = p_afsr1;
	k.i[0] = p_afar0;
	k.i[1] = p_afar1 & ~0xf;
	sdbh = (u_short) ((dp_err >> 1) & 0x3FF);
	sdbl = (u_short) ((dp_err >> 11) & 0x3FF);
	e_syndh = (u_char) (sdbh & (u_int)P_DER_E_SYND);
	e_syndl = (u_char) (sdbl & (u_int)P_DER_E_SYND);

	pil = getpil();
	ASSERT(pil <= PIL_MAX);
	if (async_err_panic) {
		cmn_err(CE_PANIC,
"CPU%d Async Err: AFSR 0x%08x %08x AFAR 0x%08x %08x SDBH 0x%x SDBL 0x%x PIL %d",
			inst, j.i[0], j.i[1], k.i[0], k.i[1], sdbh, sdbl, pil);
	}
	/*
	 * Log the error, check for all miscellaneous fatal errors.
	 */
	fatal = cpu_log_err(&j.afsr, &k.afar, inst);

	/*
	 * UE error is fatal if multiple errors or priv bit set.
	 * Tip from kbn: if ME and 2 sdb syndromes, then 2 different addresses
	 * (ie, always fatal), if !ME and 2 sdb syndromes, then same address.
	 * We catch this because cpu_log_ue_err always returns fatal for ME,
	 * but we don't call ue_error twice if we have both sdb syndromes and
	 * !ME, because we have already died or killed the process at addr.
	 */
	if (j.afsr & P_AFSR_UE) {
		u_char size = 3;	/* 8 byte alignment */

		if ((sdbl >> 9) & 1) {
			(void) ue_error(&j.afsr, &k.afar, e_syndl, size,
					0, id, inst, cpu_log_ue_err);
		}
		if ((sdbh >> 9) & 1) {
			k.i[1] |= 0x8;	/* set bit 3 if error in sdbh */
			(void) ue_error(&j.afsr, &k.afar, e_syndh, size,
					0, id, inst, cpu_log_ue_err);
		}
		if ((((sdbl >> 9) & 1) == 0) && (((sdbh >> 9) & 1) == 0)) {
			(void) ue_error(&j.afsr, &k.afar, 0, size,
					0, id, inst, cpu_log_ue_err);
		}
		fatal = 1;
	}

	if ((j.i[1] & P_AFSR_TO) || (j.i[1] & P_AFSR_BERR)) {
		if ((!(pokefault)) && (!(curthread->t_nofault)))
			fatal = cpu_log_bto_err(&j.afsr, &k.afar, inst);
	}

	if (!(fatal)) {
		/*
		 * reenable errors, flush cache
		 */
		cpu_flush_ecache();
		neer = (EER_ISAPEN | EER_NCEEN | EER_CEEN);
		xt_one((int)inst, (u_int)&set_error_enable_tl1, (u_int)&neer,
			(u_int)0, (u_int)0, (u_int)0);
	}
}

static int
cpu_log_err(u_longlong_t *p_afsr, u_longlong_t *p_afar, u_char inst)
{
	union ul {
		u_longlong_t	afsr;
		u_longlong_t	afar;
		u_int		i[2];
	} j, k;

	j.afsr = *p_afsr;
	k.afar = *p_afar;

	/*
	 * The ISAP and ETP errors are supposed to cause a POR
	 * from the system, so we never ever see those messages.
	 */
	if (j.afsr & P_AFSR_ISAP) {
		cmn_err(CE_PANIC,
	"CPU%d System Address Parity Error: AFSR 0x%08x %08x AFAR 0x%08x %08x",
			inst, j.i[0], j.i[1], k.i[0], k.i[1]);
	}
	if (j.afsr & P_AFSR_ETP) {
		cmn_err(CE_PANIC,
	"CPU%d Ecache Tag Parity Error: AFSR 0x%08x %08x AFAR 0x%08x %08x",
			inst, j.i[0], j.i[1], k.i[0], k.i[1]);
	}
	/*
	 * IVUE, LDP, WP, and EDP are fatal because we have no address.
	 * So even if we kill the curthread, we can't be sure that we have
	 * killed everyone using tha data, and it could be updated incorrectly
	 * because we have a writeback cache.
	 */
	/* if (afsr & P_AFSR_IVUE) { */
	if (j.i[1] & P_AFSR_IVUE) {
		cmn_err(CE_PANIC,
"CPU%d Interrupt Vector Uncorrectable Error: AFSR 0x%08x %08x AFAR 0x%08x %08x",
			inst, j.i[0], j.i[1], k.i[0], k.i[1]);
	}
	/* if (afsr & P_AFSR_LDP) { */
	if (j.i[1] & P_AFSR_LDP) {
		cmn_err(CE_PANIC,
	"CPU%d Load Data Parity Error: AFSR 0x%08x %08x AFAR 0x%08x %08x",
			inst, j.i[0], j.i[1], k.i[0], k.i[1]);
	}
	/* if (afsr & P_AFSR_WP) { */
	if (j.i[1] & P_AFSR_WP) {
		(void) ue_reset_ecc(&k.afar);
		cmn_err(CE_PANIC,
	"CPU%d Writeback Data Parity Error: AFSR 0x%08x %08x AFAR 0x%08x %08x",
			inst, j.i[0], j.i[1], k.i[0], k.i[1]);
	}
	/* if (afsr & P_AFSR_EDP) { */
	if (j.i[1] & P_AFSR_EDP) {
		cmn_err(CE_PANIC,
"CPU%d Ecache SRAM Data Parity Error: AFSR 0x%08x %08x AFAR 0x%08x %08x",
			inst, j.i[0], j.i[1], k.i[0], k.i[1]);
	}
	/*
	 * CP bit indicates a fatal error.
	 */
	/* if (afsr & P_AFSR_CP) */
	if (j.i[1] & P_AFSR_CP) {
		cmn_err(CE_PANIC,
	"CPU%d Copyout Data Parity Error: AFSR 0x%08x %08x AFAR 0x%08x %08x",
			inst, j.i[0], j.i[1], k.i[0], k.i[1]);
	}
	return (0);
}

static int
cpu_log_bto_err(u_longlong_t *afsr, u_longlong_t *afar, u_char inst)
{
	int priv = 0, mult = 0;
	union ul {
		u_longlong_t	afsr;
		u_longlong_t	afar;
		u_int		i[2];
	} j, k;

	j.afsr = *afsr;
	k.afar = *afar;

	/* if (bto->flt_stat & P_AFSR_ME) */
	if (j.i[0] & 1)
		mult = 1;
	/* if (bto->flt_stat & P_AFSR_PRIV) { */
	if (j.i[1] & P_AFSR_PRIV)
		priv = 1;
	/*
	 * Timeout - quiet about t_nofault timeout
	 */
	/* if (afsr & P_AFSR_TO) { */
	if (j.i[1] & P_AFSR_TO) {
		if ((mult) && (priv)) {
			cmn_err(CE_PANIC,
	"CPU%d Mult. Priv. Timeout Error: AFSR 0x%08x %08x AFAR 0x%08x %08x\n",
				inst, j.i[0], j.i[1], k.i[0], k.i[1]);
		} else if (priv) {
			cmn_err(CE_PANIC,
	"CPU%d Privileged Timeout Error: AFSR 0x%08x %08x AFAR 0x%08x %08x\n",
				inst, j.i[0], j.i[1], k.i[0], k.i[1]);
		} else if (mult) {
			cmn_err(CE_PANIC,
"CPU%d Timeout Error with Mult. Errors: AFSR 0x%08x %08x AFAR 0x%08x %08x\n",
				inst, j.i[0], j.i[1], k.i[0], k.i[1]);
		}
	}
	/*
	 * Bus error
	 */
	/* if (afsr & P_AFSR_BERR) { */
	if (j.i[1] & P_AFSR_BERR) {
		if ((mult) && (priv)) {
			cmn_err(CE_PANIC,
"CPU%d Privileged Mult. Bus Error: AFSR 0x%08x %08x AFAR 0x%08x %08x\n",
				inst, j.i[0], j.i[1], k.i[0], k.i[1]);
		} else if (mult) {
			cmn_err(CE_PANIC,
"CPU%d Bus Error with Mult. Errors: AFSR 0x%08x %08x AFAR 0x%08x %08x\n",
				inst, j.i[0], j.i[1], k.i[0], k.i[1]);
		} else if (priv) {
			cmn_err(CE_PANIC,
	"CPU%d Privileged Bus Error: AFSR 0x%08x %08x AFAR 0x%08x %08x\n",
				inst, j.i[0], j.i[1], k.i[0], k.i[1]);
		}
	}
	return (0);
}

u_int
cpu_log_ue_err(struct ecc_flt *ecc, char *unum)
{
	u_char inst = ecc->flt_inst;
	int priv = 0, mult = 0;
	volatile u_longlong_t neer;
	union ul {
		u_longlong_t	afsr;
		u_longlong_t	afar;
		u_int		i[2];
	} j, k;

	j.afsr = ecc->flt_stat;
	k.afar = ecc->flt_addr;

	/* if (ecc->flt_stat & P_AFSR_ME) */
	if (j.i[0] & 1)
		mult = 1;
	/* if (ecc->flt_stat & P_AFSR_PRIV) { */
	if (j.i[1] & P_AFSR_PRIV)
		priv = 1;

	if ((mult) && (priv)) {
		cmn_err(CE_PANIC,
	"CPU%d Mult.Priv.UE Error: AFSR 0x%08x %08x AFAR 0x%08x %08x SIMM %s",
		inst, j.i[0], j.i[1], k.i[0], k.i[1], unum);
	} else if (mult) {
		cmn_err(CE_PANIC,
	"CPU%d Multiple UE Errors: AFSR 0x%08x %08x AFAR 0x%08x %08x SIMM %s",
		inst, j.i[0], j.i[1], k.i[0], k.i[1], unum);
	} else if (priv) {
		cmn_err(CE_PANIC,
	"CPU%d Priv. UE Error: AFSR 0x%08x %08x AFAR 0x%08x %08x SIMM %s",
		inst, j.i[0], j.i[1], k.i[0], k.i[1], unum);
	}
	if (!(user_ue_panic)) {
		/*
		 * reenable errors, flush cache
		 */
		cpu_flush_ecache();
		neer = (EER_ISAPEN | EER_NCEEN | EER_CEEN);
		xt_one((int)inst, (u_int)&set_error_enable_tl1, (u_int)&neer,
			(u_int)0, (u_int)0, (u_int)0);
	}
	return (0);
}

static void
cpu_flush_ecache()
{
	/*
	 * If !WP && !IVUE, need to flush E-cache here, then need to
	 * to re-enable I and D caches as per Spitfire manual 9.1.2.
	 */
	register u_int cf, rc;
	extern struct module_ops *moduleops;

	ASSERT(moduleops->cpuops.cache_flushall_tl1);
	cf = (u_int)moduleops->cpuops.cache_flushall_tl1;
	xt_all(cf, (u_int)0, (u_int)0, (u_int)0, (u_int)0);
	ASSERT(moduleops->cpuops.reenable_caches_tl1);
	rc = (u_int)moduleops->cpuops.reenable_caches_tl1;
	xt_all(rc, (u_int)0, (u_int)0, (u_int)0, (u_int)0);
}
