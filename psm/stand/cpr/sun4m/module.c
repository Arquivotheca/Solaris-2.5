/*
 * Copyright (c) 1990 - 1991, 1993 by Sun Microsystems, Inc.
 */

#pragma ident   "@(#)module.c 1.4     93/09/17 SMI"

#include <sys/types.h>
#include <sys/machparam.h>
#include <sys/module.h>
#include <sys/module_ross625.h>
#include <sys/mmu.h>
#include <sys/cpu.h>
#include <sys/promif.h>

/* Generic pointer to specific routine related to modules */

/*
 * module_setup() is called from locore.s, very early. For each
 * known module type it will call the xx_module_identify() routine.
 * The xx_module_setup() routine is then called for the first module
 * that is identified. The details of module identification is left
 * to the module specific code. Typical this is just based on
 * decoding the IMPL, VERS field of the MCR. Other schemes may be
 * necessary for some module module drivers that may support multiple
 * implementations.
 *
 * code adapted for CPRBOOT from sun4m module code.
 */


extern void	srmmu_noop();
extern int	srmmu_inoop();
extern void	srmmu_mmu_flushall();

extern void	tsu_turn_cache_on();
extern void	tsu_pac_flushall();

void    (*v_turn_cache_on)();
void    (*v_pac_flushall)();

#ifdef OBSOLETE

extern void	tsu_turn_cache_off();

extern void	vik_turn_cache_on();
extern void	vik_turn_cache_off();
extern void	vik_turn_mxcc_on();
extern void	vik_turn_mxcc_off();
void	vik_turn_mxcc_off_1();

void    (*v_turn_cache_off)();
void    (*v_turn_mxcc_on)();
void    (*v_turn_mxcc_off)();
void    (*v_cache_flushall)();

#endif OBSOLETE

void	tsu_module_setup(u_int mcr);
int	tsu_module_identify(u_int mcr);

void	vik_module_setup(u_int mcr);
int	vik_module_identify(u_int mcr);

void	ross625_module_setup(u_int mcr);
int	ross625_module_identify(u_int mcr);

void	swift_module_setup(u_int mcr);
int	swift_module_identify(u_int mcr);

/*
 * module driver table
 */

struct module_linkage module_info[] = {
	{ vik_module_identify, vik_module_setup },
	{ tsu_module_identify, tsu_module_setup },
	{ ross625_module_identify, ross625_module_setup },
	{ swift_module_identify, swift_module_setup },

/*
 * add module driver entries here
 */
};

int	module_info_size = sizeof (module_info) / sizeof (module_info[0]);

int use_table_walk = 0;
int mxcc = 0;

void
module_setup(mcr)
	int	mcr;
{
	int	i = module_info_size;

	struct module_linkage *p = module_info;

	while (i-- > 0) {
		if ((*p->identify_func)(mcr)) {
			(*p->setup_func)(mcr);
			return;
		}
		++p;
	}
	prom_printf("Unsupported module IMPL=%d VERS=%d\n\n",
		(mcr >> 28), ((mcr >> 24) & 0xf));
	prom_exit_to_mon();
	/*NOTREACHED*/
}

int
tsu_module_identify(u_int mcr)
{
	if (((mcr >> 24) & 0xff) == 0x41)
		return (1);

	return (0);
}

void		/*ARGSUSED*/
tsu_module_setup(u_int mcr)
{
	/*
	 * Sunergy cache (not Gypsy) is off normally, turn it on to
	 * speed it up
	 */
	v_turn_cache_on = tsu_turn_cache_on;
}

int
vik_module_identify(u_int mcr)
{
	u_int psr = getpsr();

	/* 1.2 or 3.X */
	if (((psr >> 24) & 0xff) == 0x40)
		return (1);

	/* 2.X */
	if (((psr >> 24) & 0xff) == 0x41 &&
	    ((mcr >> 24) & 0xff) == 0x00)
		return (1);

	return (0);
}

/* ARGSUSED */
void
vik_module_setup(u_int mcr)
{
	v_turn_cache_on = srmmu_noop;

	if (!(mcr & CPU_VIK_MB)) {
		/* viking with MXCC */
		mxcc = 1;
		use_table_walk = 1;
	}
}

/*
 * Identify Swift module
 */
int
swift_module_identify(u_int mcr)
{
	if (((mcr >> 24) & 0xff) == 0x04)
		return (1);

	return (0);
}

/*
 * Setup (attach?) Swift module
 */

/*ARGSUSED*/
void
swift_module_setup(u_int mcr)
{
	v_turn_cache_on = srmmu_noop;
}

int
ross625_module_identify(u_int mcr)
{
	return ((mcr & RT625_CTL_IDMASK) == RT625_CTL_ID);
}


void	/*ARGSUSED*/
ross625_module_setup(mcr)
	register int	mcr;
{
	v_turn_cache_on = srmmu_noop;
}

