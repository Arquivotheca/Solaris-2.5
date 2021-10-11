/*
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

#ifndef _SYS_PCB_H
#define	_SYS_PCB_H

#pragma ident	"@(#)pcb.h	1.22	94/11/18 SMI"

#include <sys/regset.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Sun software process control block
 */

#ifndef _ASM
typedef struct pcb {
	int	pcb_flags;	/* various state flags */
	struct	rwindow pcb_xregs; /* locals+ins fetched/set via /proc */
	enum { XREGNONE = 0, XREGPRESENT, XREGMODIFIED }
		pcb_xregstat;		/* state of contents of pcb_xregs */
	enum { STEP_NONE = 0, STEP_REQUESTED, STEP_ACTIVE, STEP_WASACTIVE }
		pcb_step;		/* used while single-stepping */
	caddr_t	pcb_tracepc;		/* used while single-stepping */
	long	pcb_instr;		/* /proc: instruction at stop */
	caddr_t	pcb_trap0addr;		/* addr of user level trap 0 handler */
} pcb_t;
#endif /* ! _ASM */

/* pcb_flags */
#define	FIX_ALIGNMENT	0x01	/* fix unaligned references */
#define	INSTR_VALID	0x02	/* value in pcb_instr is valid (/proc) */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PCB_H */
