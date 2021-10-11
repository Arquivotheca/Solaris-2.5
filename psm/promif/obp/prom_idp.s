/*
 * Copyright (c) 1991, Sun Microsystems, Inc.
 */

#pragma ident	"@(#)prom_idp.s	1.6	94/06/10 SMI"

#include <sys/asm_linkage.h>

#define	IDPROMBASE	0x00000000
#define	IDPROMSIZE	0x20
#define	ASI_CNTRL	2

/*
 * This is for sun4's only.
 * Mach's with OBP should dork with properties.
 *
 * Read the ID prom.
 * This is mapped from IDPROMBASE for IDPROMSIZE bytes in the
 * ASI_CNTRL address space for byte access only.
 */

#if defined(lint)

#include <sys/types.h>

/* ARGSUSED */
void
prom_sunmon_getidprom(caddr_t addr)
{}

#else	/* lint */

	ENTRY(prom_sunmon_getidprom)
	set	IDPROMBASE, %g1
	clr	%g2
1:
	lduba	[%g1 + %g2]ASI_CNTRL, %g3 ! get id prom byte
	add	%g2, 1, %g2		! interlock
	stb	%g3, [%o0]		! put it out
	cmp	%g2, IDPROMSIZE		! done yet?
	bne,a	1b
	add	%o0, 1, %o0		! delay slot
	retl				! leaf routine return
	nop
	SET_SIZE(prom_sunmon_getidprom)

#endif	/* lint */
