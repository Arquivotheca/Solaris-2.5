/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)ipddi.c	1.23	94/02/14 SMI"

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/stream.h>
#include <sys/errno.h>
#include <sys/modctl.h>
#include <sys/sunddi.h>
#include <sys/stat.h>
#include <sys/ddi.h>

#define	D_SD_CHAR_MAJOR 93
#define	D_SD_COMMENT "IP Streams device"
#define	D_SD_INFO ipinfo
#define	D_SD_NAME "ip"
#define	D_SD_OPS_NAME ip_ops
/*
 * D_SD_FLAGS in the other tcp/ip modules have to these D_SD_FLAGS since they
 * are effectively clones of the ip driver with their module autopushed.
 */
#define	D_SD_FLAGS D_MP|D_MTPERMOD|D_MTPUTSHARED
#define	D_SM_COMMENT "IP Streams module"
#define	D_SM_INFO ipinfo
#define	D_SM_NAME "ip"
#define	D_SM_OPS_NAME ip_mops
#define	D_SM_FLAGS D_MP|D_MTPERMOD|D_MTPUTSHARED

extern	int	nulldev();
extern	int	nodev();

#ifdef D_SD_INFO
extern	struct mod_ops mod_driverops;
extern	struct streamtab D_SD_INFO;

static int _mi_driver_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
			void **result);
static	int	_mi_driver_attach(dev_info_t * devi,  ddi_attach_cmd_t cmd);
static	int	_mi_driver_identify(dev_info_t * devi);
static  int	_mi_driver_detach(dev_info_t * devi,  ddi_detach_cmd_t cmd);

/* inter-module dependencies */
char _depends_on[] = "drv/ip";

static struct cb_ops _mi_driver_ops = {
	nulldev,		/* cb_open */
	nulldev,		/* cb_close */
	nodev,			/* cb_strategy */
	nodev,			/* cb_print */
	nodev,			/* cb_dump */
	nodev,			/* cb_read */
	nodev,			/* cb_write */
	nodev,			/* cb_ioctl */
	nodev,			/* cb_devmap */
	nodev,			/* cb_mmap */
	nodev,			/* cb_segmap */
	nochpoll,		/* cb_chpoll */
	ddi_prop_op,		/* cb_prop_op */
	&D_SD_INFO,		/* cb_stream */
	(int)D_SD_FLAGS		/* cb_flag */
};

static struct dev_ops D_SD_OPS_NAME = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	_mi_driver_info,	/* devo_getinfo */
	_mi_driver_identify,	/* devo_identify */
	nulldev,		/* devo_probe */
	_mi_driver_attach,	/* devo_attach */
	_mi_driver_detach,	/* devo_detach */
	nodev,			/* devo_reset */
	&_mi_driver_ops,	/* devo_cb_ops */
	(struct bus_ops *)NULL	/* devo_bus_ops */
};

static struct modldrv modldrv = {
	&mod_driverops,	 /* Type of module */
	D_SD_COMMENT,
	&D_SD_OPS_NAME,		/* driver ops */
};
#endif

#ifdef D_SM_INFO
extern	struct streamtab D_SM_INFO;
extern struct mod_ops mod_strmodops;

static struct fmodsw _mi_module_fmodsw = {
	D_SM_NAME,
	&D_SM_INFO,
	D_SM_FLAGS
};

static struct modlstrmod modlstrmod = {
	&mod_strmodops, D_SM_COMMENT, &_mi_module_fmodsw
};
#endif



static struct modlinkage modlinkage = {
	MODREV_1,
	(void *) &modlstrmod,
	(void *) &modldrv,
	NULL
};

_init()
{
	return (mod_install(&modlinkage));
}

int
_fini()
{
	return (mod_remove(&modlinkage));
}

_info(modinfop)
	struct modinfo *modinfop;
{
	return (mod_info(&modlinkage, modinfop));
}

#ifdef D_SD_INFO
static	dev_info_t	* _mi_driver_dev_info;

/* ARGSUSED */
static int
_mi_driver_attach(devi, cmd)
	dev_info_t	* devi;
	ddi_attach_cmd_t cmd;
{
	_mi_driver_dev_info = devi;
	if (ddi_create_minor_node(devi, D_SD_NAME, S_IFCHR,
	    0, NULL, CLONE_DEV) == DDI_FAILURE) {
		ddi_remove_minor_node(devi, NULL);
		return (-1);
	}
	return (DDI_SUCCESS);
}
/* ARGSUSED */
static int
_mi_driver_detach(devi, cmd)
	dev_info_t	 * devi;
	ddi_detach_cmd_t cmd;
{
	return (DDI_SUCCESS);
}


/* ARGSUSED */
static int
_mi_driver_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
	void **result)
{
	register int error;

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		if (_mi_driver_dev_info == NULL) {
			error = DDI_FAILURE;
		} else {
			*result = (void *) _mi_driver_dev_info;
			error = DDI_SUCCESS;
		}
		break;
	case DDI_INFO_DEVT2INSTANCE:
		*result = (void *)0;
		error = DDI_SUCCESS;
		break;
	default:
		error = DDI_FAILURE;
	}
	return (error);
}

static int
_mi_driver_identify(devi)
	dev_info_t * devi;
{
	if (strcmp((char *)ddi_get_name(devi), D_SD_NAME) == 0)
		return (DDI_IDENTIFIED);
	return (DDI_NOT_IDENTIFIED);
}
#endif
