/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

/*
 * Power management chip
 *
 *  sw state info
 *  only *one* per system ...
 */

#ifndef	_SYS_PMCVAR_H
#define	_SYS_PMCVAR_H

#pragma ident	"@(#)pmcvar.h	1.12	94/09/28 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

typedef enum {
	NORMAL = 0,
	SUSPEND = 1
} pmc_states;

typedef struct {
	dev_info_t		*dip;
	kmutex_t		pmc_lock;	/* Driver lock */
	kcondvar_t		a2d_cv;
	struct pollhead		poll;		/* for connection changes */
	int			poll_event;	/* Event has occurred */
	pmc_states		state;
	ddi_iblock_cookie_t	pmcibc;
	struct pmc_reg		*pmcregs;	/* addr of pmc regs */
	u_char			save_d2a;	/* for suspend/resume */
	u_char			*fbctl;		/* frame buffer pm reg */
} pmc_unit;

typedef struct {
	char	*name;
	int	instance;
	int	cmpt;
	int	reg;
	u_char	mask;	/* Which part of register to write to */
} pmc_device;

/*
 * Devices which pmc knows it can power manage.  Note that because of a
 * hardware bug le should not be switched off (stops heartbeat clock as well!
 */
static pmc_device pmc_devices[] = {
	{"SUNW,S240",	-1,	0,	PMC_PWRKEY,	PMC_PWRKEY_PWR},
	{"SUNW,Sherman", -1,	0,	PMC_PWRKEY,	PMC_PWRKEY_PWR},
	{"FMI,MB86904",	-1,	1,	PMC_CPU,	PMC_CPU_PWR},
/*	{"le",		0,	0,	PMC_ENET,	PMC_ENET_PWR}, */
	{"esp",		0,	0,	PMC_ESP,	PMC_ESP_PWR},
	{"zs",		0,	2,	PMC_ZSAB,	0x01},
	{"zs",		0,	1,	PMC_ZSAB,	0x02},
	{"SUNW,DBRIe",	0,	1,	PMC_ISDN,	PMC_ISDN_PWR},
	{"SUNW,DBRIe",	0,	2,	PMC_AUDIO,	PMC_AUDIO_PWR},
	{"cgsix",	0,	1,	PMC_D2A,	0xff},
	{"bwtwo",	0,	1,	PMC_D2A,	0xff},
	{ NULL,		0,	0,	0,		0}
};

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PMCVAR_H */
