/*
 *	Copyright (c) 1993 Sun Microsystems, Inc.
 */

/*
 * semaphore.h:
 *
 * definitions for thread synchronization primitives: semaphores
 * This is the public part of the interface to semaphores. The
 * private (implementation-specific) part is contained in
 * <arch>/sema_impl.h
 */

#ifndef _SYS_SEMAPHORE_H
#define	_SYS_SEMAPHORE_H

#pragma ident	"@(#)semaphore.h	1.4	94/07/29 SMI"

#ifndef	_ASM
#ifdef _KERNEL
#include <sys/thread.h>
#endif
#endif


#ifdef	__cplusplus
extern "C" {
#endif

#ifndef	_ASM

/*
 * Semaphores.
 */
typedef enum {
	SEMA_DEFAULT,
	SEMA_DRIVER
} ksema_type_t;

typedef struct _ksema {
	void	* _opaque[2];	/* 2 words on 4 byte alignment */
} ksema_t;

#if defined(_KERNEL)

#define	SEMA_HELD(x)		(sema_held((x)))

extern	void	sema_init(ksema_t *, unsigned int, char *,
		    ksema_type_t, void *);
extern	void	sema_destroy(ksema_t *);
extern	void	sema_p(ksema_t *);
extern	int	sema_p_sig(ksema_t *);
extern	void	sema_v(ksema_t *);
extern	int	sema_tryp(ksema_t *);
extern	int	sema_held(ksema_t *);
extern	void	sema_mutex_init(void);		/* internal initialization */

#endif	/* defined(_KERNEL) */
#endif	/* _ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SEMAPHORE_H */
