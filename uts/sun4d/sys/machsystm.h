/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#ifndef _SYS_MACHSYSTM_H
#define	_SYS_MACHSYSTM_H

#pragma ident	"@(#)machsystm.h	1.9	95/01/25 SMI"

/*
 * Numerous platform-dependent interfaces that don't seem to belong
 * in any other header file.
 *
 * This file should not be included by code that purports to be
 * platform-independent.
 */

#include <sys/scb.h>
#include <sys/varargs.h>
#include <sys/map.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _KERNEL

extern int splzs(void);
#ifndef splimp
/* XXX go fix kobj.c so we can kill splimp altogether! */
extern int splimp(void);
#endif

extern int mon_clock_on;
extern trapvec mon_clock14_vec;
extern trapvec kclock14_vec;

extern void write_scb_int(int, struct trapvec *);

extern void vx_handler(char *);
extern void kvm_dup(void);

extern int Cpudelay;

extern void setintrenable(int);

extern u_long getdvmapages(int, u_long, u_long, u_int, u_int,
	int, struct map *);
extern void putdvmapages(u_long, int, struct map *);
extern caddr_t kalloca(u_int, int, int, int);
extern void kfreea(caddr_t, int);

extern int obpdebug;

struct cpu;
extern void init_intr_threads(struct cpu *);

struct _kthread;
extern struct _kthread *clock_thread;
extern void init_clock_thread(void);

extern struct scb *set_tbr(struct scb *);
extern void curthread_setup(struct cpu *);
extern int setup_panic(char *, va_list);

extern void setsoftint(u_int);
extern void siron(void);

extern void level15_init(void);
extern void power_off(void);
extern void sun4d_stub_nopanic(void);

struct ptp;
struct pte;
extern int pf_is_memory(u_int);

extern u_int n_xdbus;
extern kmutex_t long_print_lock;

extern void set_cpu_revision(void);
extern void check_options(int);
extern void level15_enable_bbus(u_int);
extern int xdb_cpu_unit(int);
extern void start_mon_clock(void);
extern void stop_mon_clock(void);
extern void init_soft_stuffs(void);
extern u_int disable_traps(void);
extern void enable_traps(u_int psr_value);
extern void set_all_itr_by_cpuid(u_int);
extern u_int xdb_bb_status1_get(void);
extern u_int intr_vik_action_get(void);
extern void reestablish_curthread(void);
extern int intr_prescaler_get(void);
extern void set_cpu_revision(void);
extern int intr_tick_get_phyaddr(int cpuid);


#endif /* _KERNEL */

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_MACHSYSTM_H */
