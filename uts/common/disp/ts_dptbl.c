/*
 * Copyright (c) 1987 by Sun Microsystems, Inc.
 */

/*	Copyright (c) 1990, 1991 UNIX System Laboratories, Inc.	*/
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989, 1990 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF		*/
/*	UNIX System Laboratories, Inc.				*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)ts_dptbl.c	1.10	94/04/07 SMI"

#include <sys/proc.h>
#include <sys/priocntl.h>
#include <sys/class.h>
#include <sys/disp.h>
#include <sys/ts.h>
#include <sys/tspriocntl.h>

/*
 * The purpose of this file is to allow a user to make their own
 * ts_dptbl. The contents of this file should be included in the
 * ts_dptbl(4) man page with proper instructions for making
 * and replacing the TS_DPTBL.kmod in modules/sched. This was the
 * only way to provide functionality equivalent to the mkboot/cunix
 * method in SVr4 without having the utilities mkboot/cunix in
 * SunOS/Svr4.
 * It is recommended that the system calls be used to change the time
 * quantums instead of re-building the module.
 * There are also other tunable time sharing parameters in here also
 * that used to be in param.c
 */

#ifndef BUILD_STATIC

/*
 * This is the loadable module wrapper.
 */
#include <sys/modctl.h>

extern struct mod_ops mod_miscops;

/*
 * Module linkage information for the kernel.
 */

static struct modlmisc modlmisc = {
	&mod_miscops, "Time sharing dispatch table"
};

static struct modlinkage modlinkage = {
	MODREV_1, &modlmisc, 0
};

_init()
{
	return (mod_install(&modlinkage));
}

_info(modinfop)
	struct modinfo *modinfop;
{
	return (mod_info(&modlinkage, modinfop));
}

#ifdef _VPIX
#define	TSGPUP0	3	/* Global priority for TS user priority 0 */
#define	TSGPKP0	65	/* Global priority for TS kernel priority 0 */
#else
#define	TSGPUP0	0	/* Global priority for TS user priority 0 */
#define	TSGPKP0	60	/* Global priority for TS kernel priority 0 */
#endif

#endif BUILD_STATIC

/*
 * array of global priorities used by ts procs sleeping or
 * running in kernel mode after sleep
 */

pri_t config_ts_kmdpris[] = {
	TSGPKP0,    TSGPKP0+1,  TSGPKP0+2,  TSGPKP0+3,
	TSGPKP0+4,  TSGPKP0+5,  TSGPKP0+6,  TSGPKP0+7,
	TSGPKP0+8,  TSGPKP0+9,  TSGPKP0+10, TSGPKP0+11,
	TSGPKP0+12, TSGPKP0+13, TSGPKP0+14, TSGPKP0+15,
	TSGPKP0+16, TSGPKP0+17, TSGPKP0+18, TSGPKP0+19,
	TSGPKP0+20, TSGPKP0+21, TSGPKP0+22, TSGPKP0+23,
	TSGPKP0+24, TSGPKP0+25, TSGPKP0+26, TSGPKP0+27,
	TSGPKP0+28, TSGPKP0+29, TSGPKP0+30, TSGPKP0+31,
	TSGPKP0+32, TSGPKP0+33, TSGPKP0+34, TSGPKP0+35,
	TSGPKP0+36, TSGPKP0+37, TSGPKP0+38, TSGPKP0+39
};

tsdpent_t	config_ts_dptbl[] = {

/*	glbpri		qntm	tqexp	slprt	mxwt	lwt */

	TSGPUP0+0,	20,	 0,	50,	    0,	50,
	TSGPUP0+1,	20,	 0,	50,	    0,	50,
	TSGPUP0+2,	20,	 0,	50,	    0,	50,
	TSGPUP0+3,	20,	 0,	50,	    0,	50,
	TSGPUP0+4,	20,	 0,	50,	    0,	50,
	TSGPUP0+5,	20,	 0,	50,	    0,	50,
	TSGPUP0+6,	20,	 0,	50,	    0,	50,
	TSGPUP0+7,	20,	 0,	50,	    0,	50,
	TSGPUP0+8,	20,	 0,	50,	    0,	50,
	TSGPUP0+9,	20,	 0,	50,	    0,	50,
	TSGPUP0+10,	16,	 0,	51,	    0,	51,
	TSGPUP0+11,	16,	 1,	51,	    0,	51,
	TSGPUP0+12,	16,	 2,	51,	    0,	51,
	TSGPUP0+13,	16,	 3,	51,	    0,	51,
	TSGPUP0+14,	16,	 4,	51,	    0,	51,
	TSGPUP0+15,	16,	 5,	51,	    0,	51,
	TSGPUP0+16,	16,	 6,	51,	    0,	51,
	TSGPUP0+17,	16,	 7,	51,	    0,	51,
	TSGPUP0+18,	16,	 8,	51,	    0,	51,
	TSGPUP0+19,	16,	 9,	51,	    0,	51,
	TSGPUP0+20,	12,	10,	52,	    0,	52,
	TSGPUP0+21,	12,	11,	52,	    0,	52,
	TSGPUP0+22,	12,	12,	52,	    0,	52,
	TSGPUP0+23,	12,	13,	52,	    0,	52,
	TSGPUP0+24,	12,	14,	52,	    0,	52,
	TSGPUP0+25,	12,	15,	52,	    0,	52,
	TSGPUP0+26,	12,	16,	52,	    0,	52,
	TSGPUP0+27,	12,	17,	52,	    0,	52,
	TSGPUP0+28,	12,	18,	52,	    0,	52,
	TSGPUP0+29,	12,	19,	52,	    0,	52,
	TSGPUP0+30,	 8,	20,	53,	    0,	53,
	TSGPUP0+31,	 8,	21,	53,	    0,	53,
	TSGPUP0+32,	 8,	22,	53,	    0,	53,
	TSGPUP0+33,	 8,	23,	53,	    0,	53,
	TSGPUP0+34,	 8,	24,	53,	    0,	53,
	TSGPUP0+35,	 8,	25,	54,	    0,	54,
	TSGPUP0+36,	 8,	26,	54,	    0,	54,
	TSGPUP0+37,	 8,	27,	54,	    0,	54,
	TSGPUP0+38,	 8,	28,	54,	    0,	54,
	TSGPUP0+39,	 8,	29,	54,	    0,	54,
	TSGPUP0+40,	 4,	30,	55,	    0,	55,
	TSGPUP0+41,	 4,	31,	55,	    0,	55,
	TSGPUP0+42,	 4,	32,	55,	    0,	55,
	TSGPUP0+43,	 4,	33,	55,	    0,	55,
	TSGPUP0+44,	 4,	34,	55,	    0,	55,
	TSGPUP0+45,	 4,	35,	56,	    0,	56,
	TSGPUP0+46,	 4,	36,	57,	    0,	57,
	TSGPUP0+47,	 4,	37,	58,	    0,	58,
	TSGPUP0+48,	 4,	38,	58,	    0,	58,
	TSGPUP0+49,	 4,	39,	58,	    0,	59,
	TSGPUP0+50,	 4,	40,	58,	    0,	59,
	TSGPUP0+51,	 4,	41,	58,	    0,	59,
	TSGPUP0+52,	 4,	42,	58,	    0,	59,
	TSGPUP0+53,	 4,	43,	58,	    0,	59,
	TSGPUP0+54,	 4,	44,	58,	    0,	59,
	TSGPUP0+55,	 4,	45,	58,	    0,	59,
	TSGPUP0+56,	 4,	46,	58,	    0,	59,
	TSGPUP0+57,	 4,	47,	58,	    0,	59,
	TSGPUP0+58,	 4,	48,	58,	    0,	59,
	TSGPUP0+59,	 2,	49,	59,	32000,	59
};

pri_t config_ts_maxumdpri = sizeof (config_ts_dptbl) / sizeof (tsdpent_t) - 1;

/*
 * Return the address of config_ts_dptbl
 */
tsdpent_t *
ts_getdptbl()
{
	return (config_ts_dptbl);
}

/*
 * Return the address of config_ts_kmdpris
 */
pri_t *
ts_getkmdpris()
{
	return (config_ts_kmdpris);
}

/*
 * Return the address of ts_maxumdpri
 */
pri_t
ts_getmaxumdpri()
{
	return (config_ts_maxumdpri);
}
