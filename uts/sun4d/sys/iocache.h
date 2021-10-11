/*
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

#ifndef _SYS_IOCACHE_H
#define	_SYS_IOCACHE_H

#pragma ident	"@(#)iocache.h	1.16	94/01/12 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef _ASM
extern int ioc;
#define	ioc_flush(line)
#define	ioc_pteset(pte, flag)
#define	ioc_mbufset(pte, addr)

void stream_dvma_init(caddr_t va_sbi);
void flush_sbus_wrtbuf(caddr_t va_sbi, int slot_id);
void invalid_sbus_rdbuf(caddr_t va_sbi, int slot_id);

int get_sbus_burst_size(caddr_t va_sbi, int slot_id);
void set_sbus_burst_size(caddr_t va_sbi, int slot_id, int burstsize);

u_int get_sbus_intr_id(caddr_t va_sbi);
u_int set_sbus_intr_id(caddr_t va_sbi, u_int new_id);
u_int *get_slot_ctlreg(caddr_t va_sbi, int slot_id);

#endif !_ASM

#ifdef	__cplusplus
}
#endif

#endif	/* !_SYS_IOCACHE_H */
