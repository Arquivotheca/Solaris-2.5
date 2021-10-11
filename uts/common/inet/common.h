/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ifndef	_INET_COMMON_H
#define	_INET_COMMON_H

#pragma ident	"@(#)common.h	1.14	93/11/23 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#define	A_CNT(arr)	(sizeof (arr)/sizeof (arr[0]))
#define	A_END(arr)	(&arr[A_CNT(arr)])
#define	A_LAST(arr)	(&arr[A_CNT(arr)-1])

#ifdef lint
#define	ALIGN32(ptr)	(ptr ? 0 : 0)
#define	ALIGN16(ptr)	(ptr ? 0 : 0)
#else
#define	ALIGN32(ptr)	(ptr)
#define	ALIGN16(ptr)	(ptr)
#endif

#define	fallthru	/*FALLTHRU*/
#ifdef MI_HDRS
#define	false		((boolean_t)0)
#else
#define	false		B_FALSE
#endif
#define	getarg(ac, av)	(optind < ac ? av[optind++] : nilp(char))
#ifndef	MAX
#define	MAX(x1, x2)	((x1) >= (x2) ? (x1) : (x2))
#endif
#ifndef	MIN
#define	MIN(x1, x2)	((x1) <= (x2) ? (x1) : (x2))
#endif

/*
 * The MAX_XXX and MIN_XXX defines assume a two's complement architecture.
 * They should be overriden in led.h if this assumption is incorrect.
 */
#define	MAX_INT		((int)(MAX_UINT >> 1))
#define	MAX_LONG	((long)(MAX_ULONG >> 1))
#define	MAX_SHORT	((short)(MAX_USHORT >> 1))
#define	MAX_UINT	((unsigned int)~0)
#define	MAX_ULONG	((unsigned long)~0)
#define	MAX_USHORT	((unsigned short)~0)
#define	MIN_INT		(~MAX_INT)
#define	MIN_LONG	(~MAX_LONG)
#define	MIN_SHORT	(~MAX_SHORT)

#define	newa(t, cnt)	((t *)calloc(cnt, sizeof (t)))
#define	nilp(t)		((t *)0)
#define	nil(t)		((t)0)
#define	noop
#define	reg		register
#define	staticf		static
#ifdef MI_HDRS
#define	true		((boolean_t)1)
#else
#define	true		B_TRUE
#endif

#ifdef MI_HDRS
typedef	int	boolean_t;
#endif
typedef	int	(*pfi_t)();
typedef	void	(*pfv_t)();
typedef	boolean_t	(*pfb_t)();
typedef	pfi_t	(*pfpfi_t)();

#define	BE32_EQL(a, b)	(((u8 *)a)[0] == ((u8 *)b)[0] && \
			    ((u8 *)a)[1] == ((u8 *)b)[1] && \
			    ((u8 *)a)[2] == ((u8 *)b)[2] && \
			    ((u8 *)a)[3] == ((u8 *)b)[3])
#define	BE16_EQL(a, b)	(((u8 *)a)[0] == ((u8 *)b)[0] && \
			    ((u8 *)a)[1] == ((u8 *)b)[1])
#define	BE16_TO_U16(a)	((((u16)((u8 *)a)[0] << (u16)8) | \
			    ((u16)((u8 *)a)[1] & 0xFF)) & (u16)0xFFFF)
#define	BE32_TO_U32(a)	((((u32)((u8 *)a)[0] & 0xFF) << (u32)24) | \
			    (((u32)((u8 *)a)[1] & 0xFF) << (u32)16) | \
			    (((u32)((u8 *)a)[2] & 0xFF) << (u32)8)  | \
			    ((u32)((u8 *)a)[3] & 0xFF))
#define	U16_TO_BE16(u, a) ((((u8 *)a)[0] = (u8)((u) >> 8)), \
			    (((u8 *)a)[1] = (u8)(u)))
#define	U32_TO_BE32(u, a) ((((u8 *)a)[0] = (u8)((u) >> 24)), \
			    (((u8 *)a)[1] = (u8)((u) >> 16)), \
			    (((u8 *)a)[2] = (u8)((u) >> 8)), \
			    (((u8 *)a)[3] = (u8)(u)))

/*
 * Local Environment Definition, this may and should override the
 * the default definitions above where the local environment differs.
 */
#include <inet/led.h>
#include <sys/isa_defs.h>

#ifdef	_BIG_ENDIAN

#ifndef	ABE32_TO_U32
#define	ABE32_TO_U32(p)		(*((u32 *)p))
#endif

#ifndef	ABE16_TO_U16
#define	ABE16_TO_U16(p)		(*((u16 *)p))
#endif

#ifndef	U16_TO_ABE16
#define	U16_TO_ABE16(u, p)	(*((u16 *)p) = (u))
#endif

#ifndef	U32_TO_ABE16
#define	U32_TO_ABE16(u, p)	U16_TO_ABE16(u, p)
#endif

#ifndef	UA32_TO_U32
#define	UA32_TO_U32(p, u)	((u) = (((u32)((u8 *)p)[0] << 24) | \
				    ((u32)((u8 *)p)[1] << 16) | \
				    ((u32)((u8 *)p)[2] << 8) | \
				    (u32)((u8 *)p)[3]))
#endif

#ifndef	U32_TO_ABE32
#define	U32_TO_ABE32(u, p)	(*((u32 *)p) = (u))
#endif

#else

#ifndef	ABE16_TO_U16
#define	ABE16_TO_U16(p)		BE16_TO_U16(p)
#endif

#ifndef	ABE32_TO_U32
#define	ABE32_TO_U32(p)		BE32_TO_U32(p)
#endif

#ifndef	U16_TO_ABE16
#define	U16_TO_ABE16(u, p)	U16_TO_BE16(u, p)
#endif

#ifndef	U32_TO_ABE16
#define	U32_TO_ABE16(u, p)	U16_TO_ABE16(u, p)
#endif

#ifndef	U32_TO_ABE32
#define	U32_TO_ABE32(u, p)	U32_TO_BE32(u, p)
#endif

#ifndef	UA32_TO_U32
#define	UA32_TO_U32(p, u)	((u) = (((u32)((u8 *)p)[3] << 24) | \
				    ((u32)((u8 *)p)[2] << 16) | \
				    ((u32)((u8 *)p)[1] << 8) | \
				    (u32)((u8 *)p)[0]))
#endif

#endif

#ifdef	_KERNEL

/* Extra MPS mblk type */
#define	M_MI		64
/* Subfields for M_MI messages */
#define	M_MI_READ_RESET	1
#define	M_MI_READ_SEEK	2
#define	M_MI_READ_END	4

#ifndef EINVAL
#include <errno.h>
#endif

#ifdef MPS
#define	mi_adjmsg	adjmsg
#endif

#ifndef	CANPUTNEXT
#define	CANPUTNEXT(q)	canput((q)->q_next)
#endif

#endif /* _KERNEL */

#ifndef UNIX5_3
#define	EBASE		127

#ifndef EBADMSG
#define	EBADMSG		(EBASE-0)
#endif

#ifndef	ETIME
#define	ETIME		(EBASE-1)
#endif

#ifndef EPROTO
#define	EPROTO		(EBASE-2)
#endif

#endif /* UNIX5_3 */

#ifndef	GOOD_EXIT_STATUS
#define	GOOD_EXIT_STATUS	0
#endif

#ifndef	BAD_EXIT_STATUS
#define	BAD_EXIT_STATUS		1
#endif

#ifndef	is_ok_exit_status
#define	is_ok_exit_status(status)	(status == GOOD_EXIT_STATUS)
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _INET_COMMON_H */
