/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#ifndef _SYS_FPU_FPUSYSTM_H
#define	_SYS_FPU_FPUSYSTM_H

#pragma ident	"@(#)fpusystm.h	1.3	94/10/05 SMI"

/*
 * ISA-dependent FPU interfaces
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _KERNEL

struct fpu;
struct regs;
struct core;

extern int fpu_exists;
extern int fpu_version;

void fpu_probe(void);
void fp_disable(void);
void fp_disabled(struct regs *);
void fp_enable(kfpu_t *);
void fp_fksave(kfpu_t *);
void fp_runq(struct regs *);
void fp_core(struct core *);
void fp_load(kfpu_t *);
void fp_save(kfpu_t *);
void fp_restore(kfpu_t *);
void syncfpu(void);

#endif	/* _KERNEL */

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_FPU_FPUSYSTM_H */
