/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_ATOMIC_PRIM_H
#define	_SYS_ATOMIC_PRIM_H

#pragma ident	"@(#)atomic_prim.h	1.3	95/07/07 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef	_ASM

#if defined(_KERNEL)
extern void atadd_ext(u_longlong_t *, int);
extern void atadd_word(u_int *, int);
extern void atadd_hword(u_short *, int);
extern void atinc_cidx_extword(longlong_t *, longlong_t *, longlong_t);
extern int atinc_cidx_word(int *, int);
extern short atinc_cidx_hword(short *, short);

extern void rwlock_word_init(u_int *);
extern int rwlock_word_enter(u_int *, int);
extern void rwlock_word_exit(u_int *, int);
extern void rwlock_hword_init(u_short *);
extern int rwlock_hword_enter(u_short *, int);
extern void rwlock_hword_exit(u_short *, int);


#endif	/* _KERNEL */

#endif	_ASM

#define	WRITER_LOCK	0x0
#define	READER_LOCK	0x1

#define	HWORD_WLOCK	0xffff
#define	WORD_WLOCK	0xffffffff

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ATOMIC_PRIM_H */
