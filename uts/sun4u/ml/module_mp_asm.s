/*
 *	Copyright (c) 1990 - 1991 by Sun Microsystems, Inc.
 *
 * "mp" layer for module interface. slides into the same
 * hooks as a normal module interface, replacing the service
 * routines for the specific module with linkages that will
 * force crosscalls.
 *
 */

#ident	"@(#)module_mp_asm.s	1.2	93/09/01 SMI"

#if defined(lint)
#include <sys/types.h>
#else	/* lint */
#include "assym.s"
#endif	/* lint */

#include <sys/machparam.h>
#include <sys/asm_linkage.h>
#include <sys/trap.h>

#if defined(lint)

void
mp_mmu_flushall(void)
{}

/* ARGSUSED */
void
mp_mmu_flushctx(u_int c_num)
{}

/* ARGSUSED */
void
mp_mmu_flushrgn(caddr_t addr)
{}

/* ARGSUSED */
void
mp_mmu_flushseg(caddr_t addr)
{}

void
mp_mmu_flushpage(void)
{}

void
mp_mmu_flushpagectx(void)
{}

void
mp_vac_flush(void)
{}

void
mp_cache_flushall(void)
{}

void
mp_cache_flushctx(void)
{}

void
mp_cache_flushrgn(void)
{}

void
mp_cache_flushseg(void)
{}

void
mp_cache_flushpage(void)
{}

void
mp_cache_flushpagectx(void)
{}

void
mp_pac_pageflush(void)
{}

#else	/* lint */

	.seg	".text"
	.align	4

/*
 * REVEC: XXX - empty REVEC; should be changed or deleted later
 */
#define	REVEC(name)

	ENTRY(mp_mmu_flushall)
	REVEC(mmu_flushall)
	SET_SIZE(mp_mmu_flushall)

	ENTRY(mp_mmu_flushctx)
	REVEC(mmu_flushctx)
	SET_SIZE(mp_mmu_flushctx)

	ENTRY(mp_mmu_flushrgn)
	REVEC(mmu_flushrgn)
	SET_SIZE(mp_mmu_flushrgn)

	ENTRY(mp_mmu_flushseg)
	REVEC(mmu_flushseg)
	SET_SIZE(mp_mmu_flushseg)

	ENTRY(mp_mmu_flushpage)
	REVEC(mmu_flushpage)
	SET_SIZE(mp_mmu_flushpage)

	ENTRY(mp_mmu_flushpagectx)
	REVEC(mmu_flushpagectx)
	SET_SIZE(mp_mmu_flushpagectx)

	ENTRY(mp_vac_flush)
	REVEC(vac_flush)
	SET_SIZE(mp_vac_flush)

	ENTRY(mp_cache_flushall)
	REVEC(cache_flushall)
	SET_SIZE(mp_cache_flushall)

	ENTRY(mp_cache_flushctx)
	REVEC(cache_flushctx)
	SET_SIZE(mp_cache_flushctx)

	ENTRY(mp_cache_flushrgn)
	REVEC(cache_flushrgn)
	SET_SIZE(mp_cache_flushrgn)

	ENTRY(mp_cache_flushseg)
	REVEC(cache_flushseg)
	SET_SIZE(mp_cache_flushseg)

	ENTRY(mp_cache_flushpage)
	REVEC(cache_flushpage)
	SET_SIZE(mp_cache_flushpage)

	ENTRY(mp_cache_flushpagectx)
	REVEC(cache_flushpagectx)
	SET_SIZE(mp_cache_flushpagectx)

	ENTRY(mp_pac_pageflush)
	REVEC(pac_pageflush)
	SET_SIZE(mp_pac_pageflush)

#endif	/* lint	*/
