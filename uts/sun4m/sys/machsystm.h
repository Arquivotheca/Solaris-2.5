/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#ifndef _SYS_MACHSYSTM_H
#define	_SYS_MACHSYSTM_H

#pragma ident	"@(#)machsystm.h	1.14	95/02/07 SMI"

/*
 * Numerous platform-dependent interfaces that don't seem to belong
 * in any other header file.
 *
 * This file should not be included by code that purports to be
 * platform-independent.
 */

#include <sys/scb.h>
#include <sys/varargs.h>
#include <vm/hat_srmmu.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _KERNEL

extern void mp_halt(char *);

extern int splzs(void);
#ifndef splimp
/* XXX go fix kobj.c so we can kill splimp altogether! */
extern int splimp(void);
#endif

extern void enable_dvma(void);
extern void disable_dvma(void);
extern void set_intreg(int, int);

extern char mon_clock_on;
extern trapvec mon_clock14_vec;
extern trapvec kclock14_vec;

extern void init_mon_clock(void);
extern void start_mon_clock(void);
extern void stop_mon_clock(void);
extern void write_scb_int(int, struct trapvec *);

extern void vx_handler(char *);
extern void kvm_dup(void);

extern int Cpudelay;
extern void setcpudelay(void);

extern void setintrenable(int);

extern unsigned int vac_mask;
extern int vac_hashwusrflush;

extern void vac_flushall(void);

extern int dvmasize;
extern struct map *dvmamap;
extern char DVMA[];		/* on 4m ?? */

extern u_long getdvmapages(int, u_long, u_long, u_int, u_int, int);
extern void putdvmapages(u_long, int);
extern u_long get_map_pages(int, struct map *, u_int, int);
extern int pte2atype(void *, u_long, u_long *, u_int *);

extern void ppmapinit(void);

extern void kncinit(void);
extern caddr_t kalloca(u_int, int, int, int);
extern void kfreea(caddr_t, int);

extern int sx_vrfy_pfn(u_int, u_int);

extern int obpdebug;

struct cpu;
extern void init_intr_threads(struct cpu *);

struct _kthread;
extern struct _kthread *clock_thread;
extern void init_clock_thread(void);

extern struct scb *set_tbr(struct scb *);
extern void reestablish_curthread(void);
extern int setup_panic(char *, va_list);

extern void set_intmask(int, int);
extern void set_itr_bycpu(int);
extern void send_dirint(int, int);
extern void setsoftint(u_int);
extern void siron(void);

extern void set_interrupt_target(int);
extern int getprocessorid(void);
extern int swapl(int, int *);
extern int atomic_tas(int *);

extern void memctl_getregs(u_int *, u_int *, u_int *);
extern void memctl_set_enable(u_int, u_int);
extern void msi_sync_mode(void);

extern u_long get_sfsr(void);
extern void flush_writebuffers(void);
extern void flush_writebuffers_to(caddr_t);
extern void flush_vme_writebuffers(void);

struct regs;
extern void vik_fixfault(struct regs *, caddr_t *, u_int);

struct memlist;
extern u_longlong_t get_max_phys_size(struct memlist *);

extern void bpt_reg(u_int, u_int);
extern void turn_cache_on(int);
extern void cache_init(void);

struct ptp;
struct pte;
extern int pf_is_memory(u_int);
extern void mmu_readpte(struct pte *, struct pte *);
/* mmu_readptp is actually mmu_readpte */
extern void mmu_readptp(struct ptp *, struct ptp *);

extern void rd_ptbl_as(struct as **, struct as **);
extern void rd_ptbl_base(caddr_t *, caddr_t *);

extern void rd_ptbl_next(struct ptbl **, struct ptbl **);
extern void rd_ptbl_prev(struct ptbl **, struct ptbl **);

extern void rd_ptbl_parent(struct ptbl **, struct ptbl **);

extern void rd_ptbl_flags(u_char *, u_char *);
extern void rd_ptbl_vcnt(u_char *, u_char *);
extern void rd_ptbl_lcnt(u_short *, u_short *);

extern const char sbus_to_sparc_tbl[];
extern void sbus_set_64bit(u_int slot);

extern int noprintf;
extern int vac;
extern int cache;
extern int use_cache;
extern int use_ic;
extern int use_dc;
extern int use_ec;
extern int use_mp;
extern int use_vik_prefetch;
extern int use_mxcc_prefetch;
extern int use_store_buffer;
extern int use_multiple_cmds;
extern int use_rdref_only;
extern int use_table_walk;
extern int use_mix;
extern int do_pg_coloring;
extern int use_page_coloring;
extern int mxcc;
extern int vme;
extern int pokefault;
extern u_int module_wb_flush;
extern volatile u_int aflt_ignored;
extern u_int system_fatal;
extern int ross_iobp_workaround;
extern int ross_hw_workaround2;
extern int ross_hd_bug;
extern char tbr_wr_addr_inited;
extern int nvsimm_present;
extern int ross_iopb_workaround;

extern u_int cpu_nodeid[];

#endif /* _KERNEL */

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_MACHSYSTM_H */
