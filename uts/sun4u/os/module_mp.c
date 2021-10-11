/*
 * Copyright (c) 1990 - 1991, 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)module_mp.c	1.6	94/01/25 SMI"


#include <sys/machparam.h>
#include <sys/module.h>

extern void mp_mmu_flushall();
extern void mp_mmu_flushctx();
extern void mp_mmu_flushrgn();
extern void mp_mmu_flushseg();
extern void mp_mmu_flushpage();
extern void mp_mmu_flushpagectx();
extern void mp_pac_pageflush();

void	(*s_mmu_flushall)() = 0;
void	(*s_mmu_flushctx)() = 0;
void	(*s_mmu_flushrgn)() = 0;
void	(*s_mmu_flushseg)() = 0;
void	(*s_mmu_flushpage)() = 0;
void	(*s_mmu_flushpagectx)() = 0;
void	(*s_pac_pageflush)() = 0;

extern void mp_vac_flush();

void	(*s_vac_flush)() = 0;

extern void mp_cache_flushall();
extern void mp_cache_flushctx();
extern void mp_cache_flushrgn();
extern void mp_cache_flushseg();
extern void mp_cache_flushpage();
extern void mp_cache_flushpagectx();

void		(*s_cache_flushall)() = 0;
void		(*s_cache_flushctx)() = 0;
void		(*s_cache_flushrgn)() = 0;
void		(*s_cache_flushseg)() = 0;
void		(*s_cache_flushpage)() = 0;
void		(*s_cache_flushpagectx)() = 0;

/*
 * Support for multiple processors
 */
#define	TAKE(name)	{ s_##name = v_##name; v_##name = mp_##name; }

void
mp_setfunc()
{
#ifdef FIXME
	extern int mxcc;

	TAKE(mmu_flushctx);
	TAKE(mmu_flushrgn);
	TAKE(mmu_flushseg);
	TAKE(mmu_flushpage);
	TAKE(mmu_flushpagectx);
	TAKE(vac_flush);
	TAKE(cache_flushall);
	TAKE(cache_flushctx);
	TAKE(cache_flushrgn);
	TAKE(cache_flushseg);
	TAKE(cache_flushpage);
	TAKE(cache_flushpagectx);
	TAKE(pac_pageflush);
#undef	TAKE
#endif /* FIXME */
}
