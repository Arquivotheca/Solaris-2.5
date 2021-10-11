/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#ifndef _SYS_MACHSYSTM_H
#define	_SYS_MACHSYSTM_H

#pragma ident	"@(#)machsystm.h	1.10	95/09/05 SMI"

/*
 * Numerous platform-dependent interfaces that don't seem to belong
 * in any other header file.
 *
 * This file should not be included by code that purports to be
 * platform-independent.
 */

#include <sys/scb.h>
#include <sys/varargs.h>

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

extern unsigned int vac_mask;

extern void vac_flushall(void);

extern int obpdebug;

struct cpu;
extern void init_intr_threads(struct cpu *);

struct _kthread;
extern struct _kthread *clock_thread;
extern void init_clock_thread(void);

extern struct scb *set_tbr(struct scb *);
extern void reestablish_curthread(void);
extern int setup_panic(char *, va_list);

extern void send_dirint(int, int);
extern void setsoftint(u_int);
extern void siron(void);
extern int swapl(int, int *);
#ifdef	XXX
extern void set_interrupt_target(int);
#endif	XXX

extern int getprocessorid(void);
extern caddr_t set_trap_table(void);
extern void get_asyncflt(volatile u_longlong_t *afsr);
extern void set_asyncflt(volatile u_longlong_t *afsr);
extern void get_asyncaddr(volatile u_longlong_t *afar);
extern void clr_datapath(void);
extern void reset_ecc(caddr_t vaddr);
extern void stphys(int physaddr, int value);
extern int ldphys(int physaddr);
extern void scrubphys(caddr_t vaddr);
extern void read_paddr_data(caddr_t paddr, u_int asi, u_longlong_t *data);

struct regs;

extern void kern_setup1(void);
extern void startup(void);
extern void post_startup(void);

extern int noprintf;
extern int vac;
extern int cache;
extern int use_cache;
extern int use_ic;
extern int use_dc;
extern int use_ec;
extern int use_mp;
extern int do_pg_coloring;
extern int use_page_coloring;
extern int pokefault;
extern u_int module_wb_flush;
extern volatile u_int aflt_ignored;

extern u_int cpu_nodeid[];

#endif /* _KERNEL */

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_MACHSYSTM_H */
