/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#ifndef _SYS_MACHSYSTM_H
#define	_SYS_MACHSYSTM_H

#pragma ident	"@(#)machsystm.h	1.3	93/07/09 SMI"

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

extern int splzs(void);
#ifndef splimp
/* XXX go fix kobj.c so we can kill splimp altogether! */
extern int splimp(void);
#endif

extern void enable_dvma(void);
extern void disable_dvma(void);
extern void set_intreg(int, int);
extern int pte2atype(void *, u_long, u_long *, u_int *);
extern int read_confreg(void);

extern char mon_clock_on;
extern trapvec mon_clock14_vec;
extern trapvec kclock14_vec;

extern void start_mon_clock(void);
extern void stop_mon_clock(void);
extern void write_scb_int(int, struct trapvec *);

extern void v_handler(int, char *);

extern void kvm_dup(void);

extern int Cpudelay;

extern unsigned int vac_mask;
extern int vac_hashwusrflush;

extern void vac_flushallctx(void);

extern int dvmasize;
extern int xdvmasize;
extern struct map *dvmamap;
extern char DVMA[];

extern u_long getdvmapages(int, u_long, u_long, u_int, u_int, int);
extern void putdvmapages(u_long, int);
extern caddr_t kalloca(u_int, int, int, int);
extern void kfreea(caddr_t, int);

extern int obpdebug;

struct cpu;
extern void init_intr_threads(void);

struct _kthread;
extern struct _kthread *clock_thread;
extern void init_clock_thread(void);

extern struct scb *set_tbr(struct scb *);
extern void curthread_setup(struct cpu *);
extern int setup_panic(char *, va_list);

extern void setsoftint(u_int);
extern void siron(void);

extern void kern_setup1(void);
extern void startup(void);
extern void post_startup(void);

extern void ppmapinit(void);
extern void kncinit(void);

extern void setintrenable(int);
extern void setvideoenable(int);
extern int getvideoenable(void);

extern int read_confreg(void);

#endif /* _KERNEL */

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_MACHSYSTM_H */
