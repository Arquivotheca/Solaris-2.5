/*
 * Copyright (c) 1993 - 1995 by Sun Microsystems, Inc.
 * All Rights Reserved
 */

#pragma ident	"@(#)audio_4231.c	1.39	95/10/06 SMI"

/*
 * AUDIO Chip driver - for CS 4231
 *
 * The basic facts:
 * 	- The digital representation is 8-bit u-law by default.
 *	  The high order bit is a sign bit, the low order seven bits
 *	  encode amplitude, and the entire 8 bits are inverted.
 */

#include <sys/errno.h>
#include <sys/param.h>
#include <sys/user.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/ioccom.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/file.h>
#include <sys/debug.h>
#include <sys/stat.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#include <sys/sunddi.h>
#include <sys/audioio.h>
#include <sys/audiovar.h>
#include <sys/audio_4231.h>
#include <sys/sysmacros.h>
#include <sys/audiodebug.h>
#include <sys/ddi.h>

/*
 * Local routines
 */
static int audio_4231_getinfo(dev_info_t *, ddi_info_cmd_t, void *, void **);
static int audio_4231_attach(dev_info_t *, ddi_attach_cmd_t);
static int audio_4231_detach(dev_info_t *, ddi_detach_cmd_t);
static int audio_4231_identify(dev_info_t *);
static int audio_4231_open(queue_t *, dev_t *, int, int, cred_t *);
static void audio_4231_close(aud_stream_t *);
static aud_return_t audio_4231_ioctl(aud_stream_t *, queue_t *, mblk_t *);
static aud_return_t audio_4231_mproto(aud_stream_t *, mblk_t *);
static void audio_4231_start(aud_stream_t *);
static void audio_4231_stop(aud_stream_t *);
static uint_t audio_4231_setflag(aud_stream_t *, enum aud_opflag, uint_t);
static aud_return_t audio_4231_setinfo(aud_stream_t *, mblk_t *, int *);
static void audio_4231_queuecmd(aud_stream_t *, aud_cmd_t *);
static void audio_4231_flushcmd(aud_stream_t *);
static void audio_4231_chipinit(cs_unit_t *);
static void audio_4231_loopback(cs_unit_t *, uint_t);
static uint_t audio_4231_outport(struct aud_4231_chip *, uint_t);
static uint_t audio_4231_output_muted(cs_unit_t *, uint_t);
static uint_t audio_4231_inport(struct aud_4231_chip *, uint_t);
static uint_t audio_4231_play_gain(struct aud_4231_chip *, uint_t, uchar_t);
uint_t audio_4231_record_gain(struct aud_4231_chip *, uint_t, uchar_t);
static uint_t audio_4231_monitor_gain(struct aud_4231_chip *, uint_t);
static uint_t audio_4231_playintr(cs_unit_t *);
static void audio_4231_recintr(cs_unit_t *);
static void audio_4231_pollready();
extern uint_t audio_4231_cintr();
#ifdef HONEY_DEBUG
extern uint_t audio_4231_eb2cintr();
#endif
extern uint_t audio_4231_eb2recintr();
extern uint_t audio_4231_eb2playintr();

static void audio_4231_initlist(apc_dma_list_t *, cs_unit_t *);
static void audio_4231_insert(aud_cmd_t *, ddi_dma_handle_t, uint_t,
					apc_dma_list_t *, cs_unit_t *);
static void audio_4231_remove(uint_t, apc_dma_list_t *, cs_unit_t *);
static void audio_4231_clear(apc_dma_list_t *, cs_unit_t *);
static void audio_4231_samplecalc(cs_unit_t *, uint_t, uint_t);
static uint_t audio_4231_sampleconv(cs_stream_t *, uint_t);
static void audio_4231_recordend(cs_unit_t *, apc_dma_list_t *);
static void audio_4231_initcmdp(aud_cmd_t *, uint_t);
void audio_4231_pollppipe(cs_unit_t *);
void audio_4231_pollpipe(cs_unit_t *);
void audio_4231_workaround(cs_unit_t *);
void audio_4231_config_queue(aud_stream_t *);
static int audio_4231_prop_ops(dev_t, dev_info_t *, ddi_prop_op_t, int,
	    char *, caddr_t, int *);
void audio_4231_eb2cycpend(caddr_t);
extern void call_debug();
static void audio_4231_timeout();

/*
 * Local declarations
 */
cs_unit_t *cs_units;		/* device controller array */
static size_t cs_units_size;	/* size of allocated devices array */
static ddi_iblock_cookie_t audio_4231_trap_cookie;
static uint_t CS4231_reva;

static uint_t audio_4231_acal = 0;

/*
 * Count of devices attached
 */
static uint_t devcount = 0;
/*
 * counter to keep track of the number of
 * dma's that we have done we bust always be 2 behind in
 * freeing up so that we don't free up dma bufs in progress
 * typ_playlength is saved in order to exact the number of
 * samples that we have played.
 * The calculation is as follows
 * output.samples = (output.samples - nextcount) +
 *				 (typ_playcount - current count);
 */


/*
 * This is the size of the STREAMS buffers we send up the read side
 */
int audio_4231_bsize = AUD_CS4231_BSIZE;
int audio_4231_play_bsize = AUD_CS4231_MAXPACKET;
int audio_4231_play_hiwater = 0;
int audio_4231_play_lowater = 0;
int audio_4231_cmdpool = AUD_CS4231_CMDPOOL;
int audio_4231_recbufs = AUD_CS4231_RECBUFS;
int audio_4231_no_cd = 0;

#define	LOCK_HILEVEL(unitp)	mutex_enter(&(unitp)->lock)
#define	UNLOCK_HILEVEL(unitp)	mutex_exit(&(unitp)->lock)
#define	ASSERT_UNITLOCKED(unitp) \
	ASSERT(MUTEX_HELD(&(unitp)->lock))

#define	OR_SET_BYTE_R(handle, addr, val, tmpval) {	\
	    tmpval = ddi_getb(handle, addr);		\
	    tmpval |= val; 				\
	    ddi_putb(handle, addr, tmpval);		\
	}
#define	OR_SET_LONG_R(handle, addr, val, tmpval) {	\
	    tmpval = ddi_getl(handle, addr);		\
	    tmpval |= val; 				\
	    ddi_putl(handle, addr, tmpval);		\
	}
#define	NOR_SET_LONG_R(handle, addr, val, tmpval, mask) {	\
	    tmpval = ddi_getl(handle, addr);		\
	    tmpval &= ~(mask);				\
	    tmpval |= val; 				\
	    ddi_putl(handle, addr, tmpval);		\
	}

#define	AND_SET_BYTE_R(handle, addr, val, tmpval) {	\
	    tmpval = ddi_getb(handle, addr);		\
	    tmpval &= val; 				\
	    ddi_putb(handle, addr, tmpval);		\
	}
#define	AND_SET_LONG_R(handle, addr, val, tmpval) {	\
	    tmpval = ddi_getl(handle, addr);		\
	    tmpval &= val; 				\
	    ddi_putl(handle, addr, tmpval);		\
	}

#define	EB2_REC_CSR	&unitp->eb2_record_dmar->eb2csr
#define	EB2_PLAY_CSR	&unitp->eb2_play_dmar->eb2csr
#define	EB2_REC_ACR	&unitp->eb2_record_dmar->eb2acr
#define	EB2_REC_BCR	&unitp->eb2_record_dmar->eb2bcr
#define	EB2_PLAY_ACR	&unitp->eb2_play_dmar->eb2acr
#define	EB2_PLAY_BCR	&unitp->eb2_play_dmar->eb2bcr
#define	APC_DMACSR	&unitp->chip->dmaregs.dmacsr
#define	CS4231_IAR	&unitp->chip->pioregs.iar
#define	CS4231_IDR	&unitp->chip->pioregs.idr

/*
 * XXX - This driver only supports one CS 4231 device
 */
#define	MAXUNITS	(1)

#define	RECORD_DIRECTION 1
#define	PLAY_DIRECTION	0

#define	AUDIO_ENCODING_DVI	(104)	/* DVI ADPCM PCM XXXXX */
#define	CS_TIMEOUT	9000000
#define	CS_POLL_TIMEOUT	100000

#define	NEEDS_HW_INIT	0x494e4954


/*
 * Declare audio ops vector for CS4231 support routines
 */
static struct aud_ops audio_4231_ops = {
	audio_4231_close,
	audio_4231_ioctl,
	audio_4231_mproto,
	audio_4231_start,
	audio_4231_stop,
	audio_4231_setflag,
	audio_4231_setinfo,
	audio_4231_queuecmd,
	audio_4231_flushcmd
};


/*
 * Streams declarations
 */

static struct module_info audio_4231_modinfo = {
	AUD_CS4231_IDNUM,		/* module ID number */
	AUD_CS4231_NAME,		/* module name */
	AUD_CS4231_MINPACKET,	/* min packet size accepted */
	AUD_CS4231_MAXPACKET,	/* max packet size accepted */
	AUD_CS4231_HIWATER,	/* hi-water mark */
	AUD_CS4231_LOWATER,	/* lo-water mark */
};

/*
 * Queue information structure for read queue
 */
static struct qinit audio_4231_rinit = {
	audio_rput,		/* put procedure */
	audio_rsrv,		/* service procedure */
	audio_4231_open,	/* called on startup */
	audio_close,		/* called on finish */
	NULL,			/* for 3bnet only */
	&audio_4231_modinfo,	/* module information structure */
	NULL,			/* module statistics structure */
};

/*
 * Queue information structure for write queue
 */
static struct qinit audio_4231_winit = {
	audio_wput,		/* put procedure */
	audio_wsrv,		/* service procedure */
	NULL,			/* called on startup */
	NULL,			/* called on finish */
	NULL,			/* for 3bnet only */
	&audio_4231_modinfo,	/* module information structure */
	NULL,			/* module statistics structure */
};

static struct streamtab audio_4231_str_info = {
	&audio_4231_rinit,	/* qinit for read side */
	&audio_4231_winit,	/* qinit for write side */
	NULL,			/* mux qinit for read */
	NULL,			/* mux qinit for write */
				/* list of modules to be pushed */
};

static 	struct cb_ops cb_audiocs_prop_op = {
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
	audio_4231_prop_ops,	/* cb_prop_op */
	&audio_4231_str_info,	/* cb_stream */
	(int)(D_NEW | D_MP)	/* cb_flag */
};

/*
 * Declare ops vectors for auto configuration.
 */
struct dev_ops audiocs_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* devo_refcnt */
	audio_4231_getinfo,	/* devo_getinfo */
	audio_4231_identify,	/* devo_identify */
	nulldev,		/* devo_probe */
	audio_4231_attach,	/* devo_attach */
	audio_4231_detach,	/* devo_detach */
	nodev,			/* devo_reset */
	&(cb_audiocs_prop_op),	/* devo_cb_ops */
	(struct bus_ops *)NULL,	/* devo_bus_ops */
#ifdef APC_POWER
	ddi_power		/* devo_power */
#endif
};

/*
 * The APC chip can support full 32-bit DMA addresses
 *
 */
ddi_dma_lim_t apc_dma_limits = {
	(ulong_t)0x00000000,	/* dlim_addr_lo */
	(ulong_t)0xffffffff,	/* dlim_addr_hi */
	(uint_t)-1,		/* dlim_cntr_max */
	(uint_t)0x74,		/* dlim_burstsizes (1,4,8,16) */
	0x04,			/* dlim_minxfer */
	1024,			/* dlim_speed */
};

#ifdef OLD
ddi_dma_attr_t	apc_dma_attr = {
	DMA_ATTR_V0,		/* version */
	0x00000000,		/* dlim_addr_lo */
	0xfffffffe,		/* dlim_addr_hi */
	(uint_t)-1,		/* 12 bit word counter (16 mb) 3FFC */
	0x03,			/* alignment */
	0x14,			/* 4 and 16 byte transfers */
	0x04,			/* min xfer size */
	0xFFFFFF,		/* maxxfersz 16 Mb */
	(uint_t)-1,		/* segment size */
	0x01,			/* no scatter gather */
	0x04,			/* XXX granularity ?? */
};
#endif
ddi_dma_attr_t	apc_dma_attr = {
	DMA_ATTR_V0,		/* version */
	0x00000000,		/* dlim_addr_lo */
	0xfffffffe,		/* dlim_addr_hi */
	0x3FFC,			/* 12 bit word counter (16 mb) 3FFC */
	0x01,			/* alignment */
	0x74,			/* 4 and 16 byte transfers */
	0x01,			/* min xfer size */
	0xFFFF,			/* maxxfersz 64K */
	0xFFFF,			/* segment size */
	0x01,			/* no scatter gather */
	0x01,			/* XXX granularity ?? */
};

static apc_dma_list_t dma_played_list[DMA_LIST_SIZE];
static apc_dma_list_t dma_recorded_list[DMA_LIST_SIZE];

/*
 * This driver requires that the Device-Independent Audio routines
 * be loaded in.
 */
char _depends_on[] = "misc/diaudio";


/*
 * Loadable module wrapper for SVr4 environment
 */
#include <sys/conf.h>
#include <sys/modctl.h>


extern struct mod_ops mod_driverops;

static struct modldrv audio_4231_modldrv = {
	&mod_driverops,		/* Type of module */
	"CS4231 audio driver",	/* Descriptive name */
	&audiocs_ops		/* Address of dev_ops */
};

static struct modlinkage audio_4231_modlinkage = {
	MODREV_1,
	(void *)&audio_4231_modldrv,
	NULL
};


int
_init()
{
	return (mod_install(&audio_4231_modlinkage));
}


int
_fini()
{
	return (mod_remove(&audio_4231_modlinkage));
}


int
_info(struct modinfo *modinfop)
{
	return (mod_info(&audio_4231_modlinkage, modinfop));
}


/*
 * Return the opaque device info pointer for a particular unit
 */
/*ARGSUSED*/
static int
audio_4231_getinfo(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg,
    void **result)
{
	dev_t dev;
	int error;

	dev = (dev_t)arg;

	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		if (cs_units[CS_UNIT(dev)].dip == NULL) {
			error = DDI_FAILURE;
		} else {
			*result = (void *)cs_units[CS_UNIT(dev)].dip;
			error = DDI_SUCCESS;
		}
		break;

	case DDI_INFO_DEVT2INSTANCE:
		/* Instance num is unit num (with minor number flags masked) */
		*result = (void *)CS_UNIT(dev);
		error = DDI_SUCCESS;
		break;

	default:
		error = DDI_FAILURE;
		break;
	}
	return (error);
}


/*
 * Called from autoconf.c to locate device handlers
 */
static int
audio_4231_identify(dev_info_t *dip)
{

#ifdef HONEY_DEBUG
	if (strcmp(ddi_get_name(dip), "pci108e,1000") == 0) {
		return (DDI_IDENTIFIED);
	}
#else
	if (strcmp(ddi_get_name(dip), "SUNW,CS4231") == 0) {
		return (DDI_IDENTIFIED);
	}
#endif

	cmn_err(CE_WARN, "audiocs: NOT Identified!!");
	return (DDI_NOT_IDENTIFIED);
}


/*
 * Attach to the device.
 */
/*ARGSUSED*/
static int
audio_4231_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	aud_stream_t *as, *output_as, *input_as;
	cs_unit_t *unitp;
	char *tmp;
	struct aud_cmd *pool;
	uint_t instance;
	char name[16];		/* XXX - A Constant! */
	int i;
	int power[3];		/* For adding power property */
	int proplen;
	ddi_device_acc_attr_t attr;

	attr.devacc_attr_version = DDI_DEVICE_ATTR_V0;
	attr.devacc_attr_dataorder = DDI_STRICTORDER_ACC;

		/*
		 * XXX - The fixed maximum number of units is bogus.
		 */
		if (devcount > MAXUNITS) {
			cmn_err(CE_WARN, "audiocs: multiple audio devices?");
			return (DDI_FAILURE);
		}

		ATRACEINIT();

		/* Get this instance number (becomes the low order unitnum) */
		instance = ddi_get_instance(dip);
		ASSERT(instance <= CS_UNITMASK);

		/*
		 * Each unit has a 'aud_state_t' that contains generic audio
		 * device state information.  Also, each unit has a
		 * 'cs_unit_t' that contains device-specific data.
		 * Allocate storage for them here.
		 */
		if (cs_units == NULL) {
			cs_units_size = MAXUNITS * sizeof (cs_unit_t);
			cs_units = kmem_zalloc(cs_units_size, KM_NOSLEEP);
			if (cs_units == NULL)
				return (DDI_FAILURE);
		}

		unitp = &cs_units[devcount];

	switch (cmd) {
	case DDI_ATTACH:
		break;

	case DDI_RESUME:
		LOCK_HILEVEL(unitp);
		if (!unitp->suspended) {
			UNLOCK_HILEVEL(unitp);
			return (DDI_SUCCESS);
		}
		/*
		 * XXX CPR call audio_4231_setinfo here with
		 * saved state.
		 */
		audio_4231_chipinit(unitp);
		unitp->suspended = B_FALSE;
		UNLOCK_HILEVEL(unitp);
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}

	switch (ddi_getprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "pio?", -1)) {
	case 1:
		cmn_err(CE_WARN,
		    "audiocs: DMA driver on the P1.X Platform");
		return (DDI_FAILURE);
	case 0:
	default:
		break;
	}

	if (ddi_prop_op(DDI_DEV_T_ANY, dip, PROP_LEN_AND_VAL_ALLOC,
	    DDI_PROP_DONTPASS, "dma-model", (caddr_t) &tmp, &proplen) ==
	    DDI_PROP_SUCCESS) {
		if (strcmp(tmp, "eb2dma") == 0) {
			unitp->eb2dma = B_TRUE;
		} else {
			unitp->eb2dma = B_FALSE; /* APC DMA */
		}
	} else {
		unitp->eb2dma = B_FALSE; /* APC DMA */
	}

#ifdef HONEY_DEBUG
	unitp->eb2dma = B_TRUE;	/* XXX FIX me No dma2 for now */
#endif

	switch (ddi_getprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "internal-loopback", B_FALSE)) {
	case 1:
		unitp->internal_cd = B_FALSE;
		break;
	case 0:
		unitp->internal_cd = B_TRUE;
		break;
	default:
		break;
	}

	if (audio_4231_no_cd) {
		unitp->internal_cd = B_TRUE;
	}
	/*
	 * Allocate command list buffers, initialized below
	 */
	unitp->allocated_size = audio_4231_cmdpool * sizeof (aud_cmd_t);
	unitp->allocated_memory = kmem_zalloc(unitp->allocated_size,
	    KM_NOSLEEP);
	if (unitp->allocated_memory == NULL)
		return (DDI_FAILURE);

	/*
	 * Map in the registers for the EB2 device
	 */
	if (unitp->eb2dma == B_TRUE) {
#ifdef HONEY_DEBUG
		attr.devacc_attr_endian_flags = DDI_STRUCTURE_BE_ACC;
		if (ddi_regs_map_setup(dip, 2,
		    (caddr_t *)&unitp->chip, 0x200000,
		    sizeof (struct aud_4231_pioregs), &attr,
			    &unitp->cnf_handle) != DDI_SUCCESS) {
			/* Deallocate structures allocated above */
			kmem_free(unitp->allocated_memory,
			    unitp->allocated_size);
			cmn_err(CE_WARN, "attach: failure 1");
			return (DDI_FAILURE);
		}

		attr.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;
		if (ddi_regs_map_setup(dip, 2,
		    (caddr_t *)&unitp->eb2_play_dmar, 0x700000,
		    sizeof (struct eb2_dmar), &attr,
			&unitp->cnf_handle_eb2play) != DDI_SUCCESS) {
			/* Deallocate structures allocated above */
			kmem_free(unitp->allocated_memory,
			    unitp->allocated_size);
			cmn_err(CE_WARN, "attach: failure 2");
			return (DDI_FAILURE);
		}

		/* Map in the ebus record CSR etc. */
		attr.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;
		if (ddi_regs_map_setup(dip, 2,
		    (caddr_t *)&unitp->eb2_record_dmar, 0x702000,
		    sizeof (struct eb2_dmar), &attr,
			&unitp->cnf_handle_eb2record) != DDI_SUCCESS) {
			/* Deallocate structures allocated above */
			kmem_free(unitp->allocated_memory,
			    unitp->allocated_size);
			cmn_err(CE_WARN, "attach: failure 3");
			return (DDI_FAILURE);
		}
		ATRACE(audio_4231_attach, 'RECS',
		    ddi_getl(unitp->cnf_handle_eb2record,
		    EB2_REC_CSR));

		/* Map in the codec_auxio . */
		attr.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;
		if (ddi_regs_map_setup(dip, 2,
		    (caddr_t *)&unitp->audio_auxio, 0x722000,
		    sizeof (ulong_t), &attr,
			&unitp->cnf_handle_auxio) != DDI_SUCCESS) {
			/* Deallocate structures allocated above */
			kmem_free(unitp->allocated_memory,
			    unitp->allocated_size);
			cmn_err(CE_WARN, "attach: failure 4");
			return (DDI_FAILURE);
		}
#else
		attr.devacc_attr_endian_flags = DDI_STRUCTURE_BE_ACC;
		if (ddi_regs_map_setup(dip, 1,
		    (caddr_t *)&unitp->chip, 0,
		    sizeof (struct aud_4231_pioregs), &attr,
			    &unitp->cnf_handle) != DDI_SUCCESS) {
			/* Deallocate structures allocated above */
			kmem_free(unitp->allocated_memory,
			    unitp->allocated_size);
			ddi_regs_map_free(&unitp->cnf_handle);
			return (DDI_FAILURE);
		}

		attr.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;
		if (ddi_regs_map_setup(dip, 2,
		    (caddr_t *)&unitp->eb2_play_dmar, 0,
		    sizeof (struct eb2_dmar), &attr,
			&unitp->cnf_handle_eb2play) != DDI_SUCCESS) {
			/* Deallocate structures allocated above */
			kmem_free(unitp->allocated_memory,
			    unitp->allocated_size);
			ddi_regs_map_free(&unitp->cnf_handle_eb2play);
			return (DDI_FAILURE);
		}

		/* Map in the ebus record CSR etc. */
		attr.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;
		if (ddi_regs_map_setup(dip, 3,
		    (caddr_t *)&unitp->eb2_record_dmar, 0,
		    sizeof (struct eb2_dmar), &attr,
			&unitp->cnf_handle_eb2record) != DDI_SUCCESS) {
			/* Deallocate structures allocated above */
			kmem_free(unitp->allocated_memory,
			    unitp->allocated_size);
			ddi_regs_map_free(&unitp->cnf_handle_eb2record);
			return (DDI_FAILURE);
		}
		ATRACE(audio_4231_attach, 'RECS',
		    ddi_getl(unitp->cnf_handle_eb2record,
		    EB2_REC_CSR));

		/* Map in the codec_auxio . */
		attr.devacc_attr_endian_flags = DDI_STRUCTURE_LE_ACC;
		if (ddi_regs_map_setup(dip, 4,
		    (caddr_t *)&unitp->audio_auxio, 0,
		    sizeof (ulong_t), &attr,
			&unitp->cnf_handle_auxio) != DDI_SUCCESS) {
			/* Deallocate structures allocated above */
			kmem_free(unitp->allocated_memory,
			    unitp->allocated_size);
			return (DDI_FAILURE);
		}
#endif
	/*
	 * Map in the APC style DMA
	 */
	} else {
		attr.devacc_attr_endian_flags = DDI_STRUCTURE_BE_ACC;
		if (ddi_regs_map_setup(dip, 0, (caddr_t *) &unitp->chip, 0,
		    sizeof (struct aud_4231_chip), &attr,
			    &unitp->cnf_handle) != DDI_SUCCESS) {
			/* Deallocate structures allocated above */
			kmem_free(unitp->allocated_memory,
			    unitp->allocated_size);
			ddi_regs_map_free(&unitp->cnf_handle);
			return (DDI_FAILURE);
		}
	}

	unitp->dip = dip;
	unitp->distate.ddstate = (caddr_t)unitp;
	unitp->distate.monitor_gain = 0;
	unitp->distate.output_muted = B_FALSE;
	unitp->distate.ops = &audio_4231_ops;
	/* XXXX unitp->chip = (struct aud_4231_chip *)regs; */
	unitp->playcount = 0;
	unitp->recordcount = 0;
	unitp->typ_playlength = 0;
	unitp->recordlastent = 0;

	/*
	 * Set up pointers between audio streams
	 */
	unitp->control.as.control_as = &unitp->control.as;
	unitp->control.as.output_as = &unitp->output.as;
	unitp->control.as.input_as = &unitp->input.as;
	unitp->output.as.control_as = &unitp->control.as;
	unitp->output.as.output_as = &unitp->output.as;
	unitp->output.as.input_as = &unitp->input.as;
	unitp->input.as.control_as = &unitp->control.as;
	unitp->input.as.output_as = &unitp->output.as;
	unitp->input.as.input_as = &unitp->input.as;

	as = &unitp->control.as;
	output_as = as->output_as;
	input_as = as->input_as;

	ASSERT(as != NULL);
	ASSERT(output_as != NULL);
	ASSERT(input_as != NULL);

	/*
	 * Initialize the play stream
	 */
	output_as->distate = &unitp->distate;
	output_as->type = AUDTYPE_DATA;
	output_as->mode = AUDMODE_AUDIO;
	output_as->signals_okay = B_FALSE;
	output_as->info.gain = AUD_CS4231_DEFAULT_PLAYGAIN;
	output_as->info.sample_rate = AUD_CS4231_SAMPLERATE;
	output_as->info.channels = AUD_CS4231_CHANNELS;
	output_as->info.precision = AUD_CS4231_PRECISION;
	output_as->info.encoding = AUDIO_ENCODING_ULAW;
	output_as->info.minordev = CS_MINOR_RW;
	output_as->info.balance = AUDIO_MID_BALANCE;
	output_as->info.buffer_size = audio_4231_play_bsize;

	/*
	 * Set the default output port according to capabilities
	 */
	output_as->info.avail_ports = AUDIO_SPEAKER |
				    AUDIO_HEADPHONE | AUDIO_LINE_OUT;
	output_as->info.port = AUDIO_SPEAKER;
	output_as->traceq = NULL;
	output_as->maxfrag_size = AUD_CS4231_MAX_BSIZE;

	/*
	 * Initialize the record stream (by copying play stream
	 * and correcting some values)
	 */
	input_as->distate = &unitp->distate;
	input_as->type = AUDTYPE_DATA;
	input_as->mode = AUDMODE_AUDIO;
	input_as->signals_okay = B_FALSE;
	input_as->info = output_as->info;
	input_as->info.gain = AUD_CS4231_DEFAULT_RECGAIN;
	input_as->info.sample_rate = AUD_CS4231_SAMPLERATE;
	input_as->info.channels = AUD_CS4231_CHANNELS;
	input_as->info.precision = AUD_CS4231_PRECISION;
	input_as->info.encoding = AUDIO_ENCODING_ULAW;
	input_as->info.minordev = CS_MINOR_RO;
	if (unitp->internal_cd) {
		input_as->info.avail_ports = AUDIO_MICROPHONE |
		    AUDIO_LINE_IN | AUDIO_INTERNAL_CD_IN;
	} else {
		input_as->info.avail_ports = AUDIO_MICROPHONE |
		    AUDIO_LINE_IN;
	}
	input_as->info.port = AUDIO_MICROPHONE;
	input_as->info.buffer_size = audio_4231_bsize;
	input_as->traceq = NULL;
	input_as->maxfrag_size = AUD_CS4231_MAX_BSIZE;

	/*
	 * Control stream info
	 */
	as->distate = &unitp->distate;
	as->type = AUDTYPE_CONTROL;
	as->mode = AUDMODE_NONE;
	as->signals_okay = B_TRUE;
	as->info.minordev = CS_MINOR_CTL;
	as->traceq = NULL;

	/*
	 * Initialize virtual chained DMA command block free
	 * lists.  Reserve a couple of command blocks for record
	 * buffers.  Then allocate the rest for play buffers.
	 */
	pool = (aud_cmd_t *)unitp->allocated_memory;
	unitp->input.as.cmdlist.free = NULL;
	unitp->output.as.cmdlist.free = NULL;
	for (i = 0; i < audio_4231_cmdpool; i++) {
		struct aud_cmdlist *list;

		list = (i < audio_4231_recbufs) ?
		    &unitp->input.as.cmdlist :
		    &unitp->output.as.cmdlist;
		pool->next = list->free;
		list->free = pool++;
	}

	for (i = 0; i < DMA_LIST_SIZE; i++) {
		dma_recorded_list[i].cmdp = (aud_cmd_t *)NULL;
		dma_recorded_list[i].buf_dma_handle = NULL;
		dma_played_list[i].cmdp = (aud_cmd_t *)NULL;
		dma_played_list[i].buf_dma_handle = NULL;
	}

	/*
	 * We only expect one hard interrupt address at level 5.
	 * for the apc dma interface.
	 * For the ebus 2 (cheerio) we are going to have 2
	 * levels of interrupt. One for record and one for
	 * playback. They are at the same level for now.
	 * HW intr 8.
	 */
	if (!unitp->eb2dma) {

		if (ddi_add_intr(dip, 0, &audio_4231_trap_cookie,
		    (ddi_idevice_cookie_t *)0, audio_4231_cintr,
		    (caddr_t)0) != DDI_SUCCESS) {
			cmn_err(CE_WARN,
			    "audiocs: bad 0 interrupt specification");
			goto remhardint;
		}


	/*
	 * Map in the eb2 playback interrupts
	 * Record first? XXX We don't need to go to
	 * the cintr routine for Eb2, we can just map in the
	 * real intr routines.
	 */

	} else {
#ifdef HONEY_DEBUG
		if (ddi_add_intr(dip, 0, &audio_4231_trap_cookie,
		    (ddi_idevice_cookie_t *)0, audio_4231_eb2cintr,
		    (caddr_t)0) != DDI_SUCCESS) {
			cmn_err(CE_WARN,
			    "audiocs: bad 0 interrupt specification");
			ddi_remove_intr(dip, (uint_t)0, audio_4231_trap_cookie);
			goto remhardint;
		}
#else
		if (ddi_add_intr(dip, 0, &audio_4231_trap_cookie,
		    (ddi_idevice_cookie_t *)0, audio_4231_eb2playintr,
		    (caddr_t)0) != DDI_SUCCESS) {
			cmn_err(CE_WARN,
			    "audiocs: bad 0 interrupt specification");
			ddi_remove_intr(dip, (uint_t)0, audio_4231_trap_cookie);
			goto remhardint;
		}

		if (ddi_add_intr(dip, 1, &audio_4231_trap_cookie,
		    (ddi_idevice_cookie_t *)0, audio_4231_eb2recintr,
		    (caddr_t)0) != DDI_SUCCESS) {
			cmn_err(CE_WARN,
			    "audiocs: bad 1 interrupt specification");
			goto remhardint;
		}

#endif
	}

	mutex_init(&unitp->lock, "audio_4231 intr level lock",
	    MUTEX_DRIVER, (void *)audio_4231_trap_cookie);

	output_as->lock = input_as->lock = as->lock = &unitp->lock;

	cv_init(&unitp->output.as.cv, "aud odrain cv", CV_DRIVER,
	    &audio_4231_trap_cookie);
	cv_init(&unitp->control.as.cv, "aud wopen cv", CV_DRIVER,
	    &audio_4231_trap_cookie);

	/*
	 * Initialize the audio chip
	 */
	LOCK_HILEVEL(unitp);
	audio_4231_chipinit(unitp);
	UNLOCK_HILEVEL(unitp);

	ddi_report_dev(dip);

	strcpy(name, "sound,audio");
	if (ddi_create_minor_node(dip, name, S_IFCHR, instance,
	    DDI_NT_AUDIO, 0) == DDI_FAILURE) {
		goto remhardint;
	}

	strcpy(name, "sound,audioctl");
	if (ddi_create_minor_node(dip, name, S_IFCHR,
	    instance | CS_MINOR_CTL, DDI_NT_AUDIO, 0) ==
	    DDI_FAILURE) {
		goto remminornode;
	}

	ddi_report_dev(dip);
#ifdef APC_POWER
	/* Add power managemnt properties */
	unitp->timestamp[0] = 0;		/* Chips are busy */
	drv_getparm(TIME, unitp->timestamp+1);
	unitp->timestamp[2] = unitp->timestamp[1];
	if (ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
	    "pm_timestamp", (caddr_t)unitp->timestamp,
	    sizeof (unitp->timestamp)) != DDI_SUCCESS)
		cmn_err(CE_WARN, "audiocs: Can't create pm property\n");
	power[0] = power[1] = power[2] = 1;
	if (ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
	    "pm_norm_pwr", (caddr_t)power, sizeof (power))
	    != DDI_SUCCESS)
		cmn_err(CE_WARN, "audiocs: Can't create pm property\n");
#endif


	/* Increment the device count now that this one is enabled */
	devcount = 0x00; /* for CPR XXX fix me */

	return (DDI_SUCCESS);

	/*
	 * Error cleanup handling
	 */
remminornode:
	ddi_remove_minor_node(dip, NULL);

remhardint:
	ddi_remove_intr(dip, (uint_t)0, audio_4231_trap_cookie);
	mutex_destroy(&unitp->lock);
	cv_destroy(&unitp->control.as.cv);
	cv_destroy(&unitp->output.as.cv);

freemem:
	/* Deallocate structures allocated above */
	kmem_free(unitp->allocated_memory, unitp->allocated_size);

unmapregs:
	ddi_regs_map_free(&unitp->cnf_handle);
	if (unitp->eb2dma) {
		ddi_regs_map_free(&unitp->cnf_handle_eb2play);
		ddi_regs_map_free(&unitp->cnf_handle_eb2record);
	}
	return (DDI_FAILURE);
}


/*
 * Detach from the device.
 */
/*ARGSUSED*/
static int
audio_4231_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	cs_unit_t *unitp;

	/* XXX - only handles a single detach at present */
	unitp = &cs_units[0];

	switch (cmd) {
	case DDI_DETACH:
		break;
	case DDI_SUSPEND:
		LOCK_HILEVEL(unitp);
		if (unitp->suspended) {
			UNLOCK_HILEVEL(unitp);
			return (DDI_SUCCESS);
		}
		unitp->suspended = B_TRUE;
		UNLOCK_HILEVEL(unitp);
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}

	ddi_remove_minor_node(dip, NULL);

	mutex_destroy(&unitp->lock);

	cv_destroy(&unitp->control.as.cv);
	cv_destroy(&unitp->output.as.cv);

	ddi_remove_intr(dip, (uint_t)0, audio_4231_trap_cookie);

		ddi_regs_map_free(&unitp->cnf_handle);
	if (unitp->eb2dma) {
		ddi_regs_map_free(&unitp->cnf_handle_eb2play);
		ddi_regs_map_free(&unitp->cnf_handle_eb2record);
	}

	kmem_free(unitp->allocated_memory, unitp->allocated_size);

	return (DDI_SUCCESS);
}

static int
audio_4231_prop_ops(dev_t dev, dev_info_t *dip, ddi_prop_op_t prop_op,
int mod_flags, char *name, caddr_t valuep, int *lengthp)
{
	cs_unit_t	*unitp;

	/* XXX - only handles a single detach at present */
	unitp = &cs_units[0];

	if (strcmp(name, "pm_timestamp") == 0 &&
	    ddi_prop_modify(DDI_DEV_T_NONE, unitp->dip, mod_flags, name,
	    (caddr_t)unitp->timestamp, sizeof (unitp->timestamp))
	    != DDI_PROP_SUCCESS)
		cmn_err(CE_WARN, "audiocs_prop_op: Can't modify property");

	return (ddi_prop_op(dev, dip, prop_op, mod_flags, name, valuep,
	    lengthp));
}

/*
 * Device open routine: set device structure ptr and call generic routine
 */
/*ARGSUSED*/
static int
audio_4231_open(queue_t *q, dev_t *dp, int oflag, int sflag, cred_t *credp)
{
	cs_unit_t *unitp;
	aud_stream_t *as = NULL;
	minor_t minornum;
	int status;

	/*
	 * Check that device is legal:
	 * Base unit number must be valid.
	 * If not a clone open, must be the control device.
	 */
	if (CS_UNIT(*dp) > devcount) {
		return (ENODEV);
	}

	minornum = geteminor(*dp);

	/*
	 * Get address of generic audio status structure
	 */
	unitp = &cs_units[CS_UNIT(*dp)];

	/*
	 * Get the correct audio stream
	 */
	if (minornum == unitp->output.as.info.minordev || minornum == 0)
		as = &unitp->output.as;
	else if (minornum == unitp->input.as.info.minordev)
		as = &unitp->input.as;
	else if (minornum == unitp->control.as.info.minordev)
		as = &unitp->control.as;

	if (as == NULL)
		return (ENODEV);

	LOCK_AS(as);
	ATRACE(audio_4231_open, 'OPEN', as);

	/*
	 * Pick up the control device.
	 * Init softstate if HW state hasn't been done yet.
	 */
	if (as == as->control_as) {
		as->type = AUDTYPE_CONTROL;
	} else {
		as = (oflag & FWRITE) ? as->output_as : as->input_as;
		as->type = AUDTYPE_DATA;
		if (!unitp->hw_output_inited ||
		    !unitp->hw_input_inited) {
			as->output_as->info.sample_rate = AUD_CS4231_SAMPLERATE;
			as->output_as->info.channels = AUD_CS4231_CHANNELS;
			as->output_as->info.precision = AUD_CS4231_PRECISION;
			as->output_as->info.encoding = AUDIO_ENCODING_ULAW;
			as->input_as->info.sample_rate = AUD_CS4231_SAMPLERATE;
			as->input_as->info.channels = AUD_CS4231_CHANNELS;
			as->input_as->info.precision = AUD_CS4231_PRECISION;
			as->input_as->info.encoding = AUDIO_ENCODING_ULAW;
		}
	}

	if (ISDATASTREAM(as) && ((oflag & (FREAD|FWRITE)) == FREAD))
		as = as->input_as;

	if (ISDATASTREAM(as)) {
		minornum = as->info.minordev | CS_CLONE_BIT;
		sflag = CLONEOPEN;
	} else {
		minornum = as->info.minordev;
	}

	status = audio_open(as, q, dp, oflag, sflag);
	if (status != 0) {
		ATRACE(audio_4231_open, 'LIAF', as);
		goto done;
	}
	ATRACE(audio_4231_open, 'HERE', as);
	/*
	 * Reset to 8bit u-law mono (default) on open.
	 * This is here for compatibility with the man page
	 * interface for open of /dev/audio as described in audio(7).
	 */
	if (as == as->output_as && as->input_as->readq == NULL) {
		ATRACE(audio_4231_open, 'OUTA', as);
		as->output_as->info.sample_rate = AUD_CS4231_SAMPLERATE;
		as->output_as->info.channels = AUD_CS4231_CHANNELS;
		as->output_as->info.precision = AUD_CS4231_PRECISION;
		as->output_as->info.encoding = AUDIO_ENCODING_ULAW;
		unitp->hw_output_inited = B_FALSE;
		unitp->hw_input_inited = B_FALSE;
	} else if (as == as->input_as && as->output_as->readq == NULL) {
		ATRACE(audio_4231_open, 'INNA', as);
		as->input_as->info.sample_rate = AUD_CS4231_SAMPLERATE;
		as->input_as->info.channels = AUD_CS4231_CHANNELS;
		as->input_as->info.precision = AUD_CS4231_PRECISION;
		as->input_as->info.encoding = AUDIO_ENCODING_ULAW;
		unitp->hw_output_inited = B_FALSE;
		unitp->hw_input_inited = B_FALSE;
	}

	if (ISDATASTREAM(as) && (oflag & FREAD)) {
		/*
		 * Set input bufsize now, in case the value was patched
		 */
		as->input_as->info.buffer_size = audio_4231_bsize;

		audio_process_input(as->input_as);
	}

	*dp = makedevice(getmajor(*dp), CS_UNIT(*dp) | minornum);

done:
	UNLOCK_AS(as);
	ATRACE(audio_4231_open, 'DONE', as);
	return (status);
}


/*
 * Device-specific close routine, called from generic module.
 * Must be called with UNITP lock held.
 */
static void
audio_4231_close(aud_stream_t *as)
{
	cs_unit_t *unitp;

	ASSERT_ASLOCKED(as);
	unitp = UNITP(as);

	/*
	 * Reset status bits.  The device will already have been stopped.
	 */
	ATRACE(audio_4231_close, 'DONE', as);
	if (as == as->output_as) {
		audio_4231_clear((apc_dma_list_t *)&dma_played_list, unitp);
		/*
		 * If the variable has been tuned in /etc/system
		 * then set it to the tuned param else set it
		 * to the default.
		 */
		if (audio_4231_play_bsize != AUD_CS4231_MAXPACKET) {
			as->output_as->info.buffer_size =
			    audio_4231_play_bsize;
		} else {
			as->output_as->info.buffer_size =
			    AUD_CS4231_MAXPACKET;
		}
		unitp->output.samples = (uint_t)0x00;
		unitp->output.error = B_FALSE;
	} else {
		unitp->input.samples = (uint_t)0x00;
		unitp->input.error = B_FALSE;
	}

	if (as == as->control_as) {
		as->control_as->info.open = B_FALSE;
	}

	if (!as->output_as->info.open && !as->input_as->info.open &&
		    !as->control_as->info.open) {
		unitp->hw_output_inited = B_FALSE;
		unitp->hw_input_inited = B_FALSE;
	}


	/*
	 * If a user process mucked up the device, reset it when fully
	 * closed
	 */
	if (unitp->init_on_close && !as->output_as->info.open &&
	    !as->input_as->info.open) {
		audio_4231_chipinit(unitp);
		unitp->init_on_close = B_FALSE;
	}

	ATRACE(audio_4231_close, 'CLOS', as);

}


/*
 * Process ioctls not already handled by the generic audio handler.
 *
 * If AUDIO_CHIP is defined, we support ioctls that allow user processes
 * to muck about with the device registers.
 * Must be called with UNITP lock held.
 */
static aud_return_t
audio_4231_ioctl(aud_stream_t *as, queue_t *q, mblk_t *mp)
{
	struct iocblk *iocp;
	aud_return_t change;
	caddr_t uaddr;
	int loop;
	audio_device_t	*devtp;

	ASSERT_ASLOCKED(as);
	change = AUDRETURN_NOCHANGE; /* detect device state change */

	iocp = (struct iocblk *)(void *)mp->b_rptr;

	switch (iocp->ioc_cmd) {
	default:
	einval:
		ATRACE(audio_4231_ioctl, 'LVNI', as);
		/* NAK the request */
		audio_ack(q, mp, EINVAL);
		goto done;

	case AUDIO_GETDEV:	/* return device type */
		ATRACE(audio_4231_ioctl, 'VEDG', as);
		uaddr = *(caddr_t *)(void *)mp->b_cont->b_rptr;
		freemsg(mp->b_cont);
		mp->b_cont = allocb(sizeof (audio_device_t), BPRI_MED);
		if (mp->b_cont == NULL) {
			audio_ack(q, mp, ENOSR);
			goto done;
		}

		devtp = (audio_device_t *)(void *)mp->b_cont->b_rptr;
		mp->b_cont->b_wptr += sizeof (audio_device_t);
		strcpy(devtp->name, CS_DEV_NAME);
		if (UNITP(as)->internal_cd) {
			strcpy(devtp->version, CS_DEV_VERSION);
		} else {
			if (UNITP(as)->eb2dma) {
				strcpy(devtp->version, CS_DEV_VERSION_C);
			} else {
				strcpy(devtp->version, CS_DEV_VERSION_B);
			}
		}

		strcpy(devtp->config, CS_DEV_CONFIG_ONBRD1);

		audio_copyout(q, mp, uaddr, sizeof (audio_device_t));
		break;

	case AUDIO_DIAG_LOOPBACK: /* set clear loopback mode */
		loop = *(int *)(void *)mp->b_cont->b_rptr; /* true to enable */
		UNITP(as)->init_on_close = B_TRUE; /* reset device later */
		ATRACE(audio_4231_ioctl, 'POOL', as);
		audio_4231_loopback(UNITP(as), loop);
		/* Acknowledge the request and we're done */
		audio_ack(q, mp, 0);
		change = AUDRETURN_CHANGE;
		break;
	}

done:
	return (change);
}


/*
 * audio_4231_mproto - handle synchronous M_PROTO messages
 *
 * This driver does not support any M_PROTO messages, but we must
 * free the message.
 */
/*ARGSUSED*/
static aud_return_t
audio_4231_mproto(aud_stream_t *as, mblk_t *mp)
{
	freemsg(mp);
	return (AUDRETURN_NOCHANGE);
}


/*
 * The next routine is used to start reads or writes.
 * If there is a change of state, enable the chip.
 * If there was already i/o active in the desired direction,
 * or if i/o is paused, don't bother re-initializing the chip.
 * Must be called with UNITP lock held.
 */
static void
audio_4231_start(aud_stream_t *as)
{
	cs_stream_t *css;
	int pause;
	cs_unit_t *unitp;
	ddi_acc_handle_t handle;
	uchar_t 	tmpval;
	ulong_t		ltmpval;

	ASSERT_ASLOCKED(as);
	ATRACE(audio_4231_start, '  AS', as);
	unitp = UNITP(as);
	handle = unitp->cnf_handle;
	if (as == as->output_as) {
		ATRACE(audio_4231_start, 'OUAS', as);
		css = &UNITP(as)->output;
	} else {
		ATRACE(audio_4231_start, 'INAS', as);
		css = &UNITP(as)->input;
	}

	pause = as->info.pause;

	/*
	 * If we are paused this must mean that we were paused while
	 * playing or recording. In this case we just wasnt to hit
	 * the APC_PPAUSE or APC_CPAUSE bits and resume from where
	 * we left off. If we are starting or re-starting we just
	 * want to start playing as in the normal case.
	 */

	/* If already active, paused, or nothing queued to the device, done */
	if (css->active || pause || (css->cmdptr == NULL)) {

		/*
		 * if not active, and not paused, and the cmdlist
		 * pointer is NULL, but there are buffers queued up
		 * for the play, then we may have been called as a
		 * result of the audio_resume, so set the cmdptr
		 * to the head of the list and start the play side.
		 */

		if (!css->active && !pause && as->cmdlist.head &&
		    as->cmdlist.head->next && (as == as->output_as)) {
			css->cmdptr = as->cmdlist.head;
		} else {
			ATRACE(audio_4231_start, 'RET ', css);
			return;
		}
	}

	css->active = B_TRUE;

	if (!UNITP(as)->hw_output_inited ||
		    !UNITP(as)->hw_input_inited) {

		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.iar,
		    (uchar_t)(IAR_MCE | PLAY_DATA_FR));
		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.idr,
		    (uchar_t)DEFAULT_DATA_FMAT);

		drv_usecwait(100); /* chip bug workaround */
		audio_4231_pollready();
		drv_usecwait(1000);	/* chip bug */

		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.iar,
		    (uchar_t)(IAR_MCE | CAPTURE_DFR));
		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.idr,
		    (uchar_t)DEFAULT_DATA_FMAT);
		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.iar,
		    (uchar_t)(CAPTURE_DFR));

		drv_usecwait(100); /* chip bug workaround */
		audio_4231_pollready();
		drv_usecwait(1000);	/* chip bug */
		UNITP(as)->hw_output_inited = B_TRUE;
		UNITP(as)->hw_input_inited = B_TRUE;
	}

	if (!(pause) && (as == as->output_as)) {
		ATRACE(audio_4231_start, 'IDOU', as);
		audio_4231_initlist((apc_dma_list_t *)
			    &dma_played_list, UNITP(as));
		/*
		 * We must clear off the pause first. If you
		 * don't it won't continue from a abort.
		 */
		if (!unitp->eb2dma) {
			AND_SET_LONG_R(handle,
			    &CSR(as)->dmaregs.dmacsr,
			    PLAY_UNPAUSE, ltmpval);
			ATRACE(audio_4231_start, 'rscp',
			CSR(as)->dmaregs.dmacsr);
		} else {

			ddi_putb(handle,
			    &UNITP(as)->chip->pioregs.iar,
			    INTERFACE_CR);
			AND_SET_BYTE_R(handle,
			    &UNITP(as)->chip->pioregs.iar,
			    PEN_DISABLE, tmpval);

			OR_SET_LONG_R(unitp->cnf_handle_eb2play,
			    EB2_PLAY_CSR,
			    EB2_RESET, ltmpval);
			audio_4231_eb2cycpend((caddr_t)EB2_PLAY_CSR);
			AND_SET_LONG_R(unitp->cnf_handle_eb2play,
			    EB2_PLAY_CSR,
			    ~EB2_RESET, ltmpval);
			ATRACE(audio_4231_start, 'EB2S',
			    ddi_getl(unitp->cnf_handle_eb2play,
			    EB2_PLAY_CSR));
		}

		if (audio_4231_playintr(unitp)) {
			if (!unitp->eb2dma) {
				NOR_SET_LONG_R(handle,
				    &CSR(as)->dmaregs.dmacsr,
				    PLAY_SETUP, ltmpval,
				    APC_INTR_MASK);
				ATRACE(audio_4231_start, 'RSCP',
				CSR(as)->dmaregs.dmacsr);
			} else {
			OR_SET_LONG_R(unitp->cnf_handle_eb2play,
			    EB2_PLAY_CSR,
			    EB2_PLAY_SETUP, ltmpval);
			ATRACE(audio_4231_start, 'CSR0',
			    ddi_getl(unitp->cnf_handle_eb2play,
			    EB2_PLAY_CSR));
				/*
				 * We need to double prime the eb2
				 * engine for chaining if possible.
				 */
#ifdef HONEY_CHAIN1
				audio_4231_playintr(unitp);
#endif
			}
			ddi_putb(handle,
			    &UNITP(as)->chip->pioregs.iar,
				(uchar_t)(INTERFACE_CR));
			OR_SET_BYTE_R(handle,
			    &UNITP(as)->chip->pioregs.idr,
			    PEN_ENABLE, tmpval);
			ATRACE(audio_4231_start, 'rdiP',
			UNITP(as)->chip->pioregs.idr);
		}
	} else if (!(pause) && (as == as->input_as)) {
		ATRACE(audio_4231_start, 'IDIN', as);
		audio_4231_initlist((apc_dma_list_t *)
			    &dma_recorded_list, UNITP(as));
		if (!unitp->eb2dma) {
			AND_SET_LONG_R(handle,
			    &CSR(as)->dmaregs.dmacsr,
			    CAP_UNPAUSE, ltmpval);
			audio_4231_recintr(unitp);
			NOR_SET_LONG_R(handle,
			    &CSR(as)->dmaregs.dmacsr,
			    CAP_SETUP, ltmpval, APC_INTR_MASK);
		} else {
			ATRACE(audio_4231_start, 'RCSR',
			    ddi_getl(unitp->cnf_handle_eb2record,
			    EB2_REC_CSR));
			OR_SET_LONG_R(unitp->cnf_handle_eb2record,
			    EB2_REC_CSR,
			    EB2_RESET, ltmpval);
			ATRACE(audio_4231_start, 'RCS1',
			    ddi_getl(unitp->cnf_handle_eb2record,
			    EB2_REC_CSR));

			audio_4231_eb2cycpend((caddr_t)EB2_REC_CSR);
			ATRACE(audio_4231_start, 'CYCL',
			    ddi_getl(unitp->cnf_handle_eb2record,
			    EB2_REC_CSR));
			AND_SET_LONG_R(unitp->cnf_handle_eb2record,
			    EB2_REC_CSR,
			    ~EB2_RESET, ltmpval);
			ATRACE(audio_4231_start, 'RSET',
			    ddi_getl(unitp->cnf_handle_eb2record,
			    EB2_REC_CSR));
			audio_4231_recintr(unitp);

			ATRACE(audio_4231_start, 'RST1',
			    ddi_getl(unitp->cnf_handle_eb2record,
			    EB2_REC_CSR));
			AND_SET_LONG_R(unitp->cnf_handle_eb2record,
			    EB2_REC_CSR,
			    ~EB2_DISAB_CSR_DRN, ltmpval);
			OR_SET_LONG_R(unitp->cnf_handle_eb2record,
			    EB2_REC_CSR,
			    EB2_CAP_SETUP, ltmpval);

			ATRACE(audio_4231_start, 'RRSR',
			    ddi_getl(unitp->cnf_handle_eb2record,
			    EB2_REC_CSR));
			ATRACE(audio_4231_start, 'RACR',
			    ddi_getl(unitp->cnf_handle_eb2record,
			    EB2_REC_ACR));
			ATRACE(audio_4231_start, 'RBCR',
			    ddi_getl(unitp->cnf_handle_eb2record,
			    EB2_REC_BCR));
		}
		audio_4231_pollready();
		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.iar,
		    (uchar_t)(INTERFACE_CR));
		OR_SET_BYTE_R(handle,
		    &UNITP(as)->chip->pioregs.idr,
		    CEN_ENABLE, tmpval);
	}

}


/*
 * The next routine is used to stop reads or writes.
 * Must be called with UNITP lock held.
 */

static void
audio_4231_stop(aud_stream_t *as)
{
	cs_unit_t *unitp;
	ddi_acc_handle_t handle;
	uchar_t tmpval;
	ulong_t ltmpval;

	ASSERT_ASLOCKED(as);
	ATRACE(audio_4231_stop, '  AS', as);

	unitp = UNITP(as);
	handle = unitp->cnf_handle;
	/*
	 * For record.
	 * You HAVE to make sure that all of the DMA is freed up
	 * on the stop and DMA is first stopped in this routine.
	 * Otherwise the flow of code will progress in such a way
	 * that the dma memory will be freed, the device closed
	 * and an intr will come in and scribble on supposedly
	 * clean memory (verify_pattern in streams with 0xcafefeed)
	 * This causes a panic in allocb...on the subsequent alloc of
	 * this block of previously freed memory.
	 * The CPAUSE stops the dma and the recordend frees up the
	 * dma. We poll for the pipe empty bit rather than handling it
	 * in the intr routine because it breaks the code flow since stop and
	 * pause share the same functionality.
	 */

	if (as == as->output_as) {
		UNITP(as)->output.active = B_FALSE;
		if (!UNITP(as)->eb2dma) {
			NOR_SET_LONG_R(handle, &CSR(as)->dmaregs.dmacsr,
			    APC_PPAUSE, ltmpval, APC_INTR_MASK);
			audio_4231_pollppipe(UNITP(as));
			audio_4231_clear((apc_dma_list_t *)&dma_played_list,
			    unitp);
		} else {
			AND_SET_LONG_R(unitp->cnf_handle_eb2play,
			    EB2_PLAY_CSR,
			    (~EB2_INT_EN), ltmpval);
			AND_SET_LONG_R(unitp->cnf_handle_eb2play,
			    EB2_PLAY_CSR,
			    (~EB2_EN_DMA), ltmpval);
			ATRACE(audio_4231_stop, 'PCSR',
			    ddi_getl(unitp->cnf_handle_eb2play,
			    EB2_PLAY_CSR));
		}
	} else {
		UNITP(as)->input.active = B_FALSE;
		if (!UNITP(as)->eb2dma) {
			NOR_SET_LONG_R(handle, &CSR(as)->dmaregs.dmacsr,
				APC_CPAUSE, ltmpval, APC_INTR_MASK);
			audio_4231_pollpipe(UNITP(as));
			audio_4231_recordend(UNITP(as),
			    (apc_dma_list_t *)&dma_recorded_list);
			audio_4231_clear((apc_dma_list_t *)&dma_recorded_list,
			    unitp);
		} else {
			AND_SET_LONG_R(unitp->cnf_handle_eb2record,
			    EB2_REC_CSR,
			    (~EB2_EN_DMA | ~EB2_INT_EN), ltmpval);

		}
		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.iar,
		    (uchar_t)(INTERFACE_CR));
		AND_SET_BYTE_R(handle,
		    &UNITP(as)->chip->pioregs.idr,
		    CEN_DISABLE, tmpval);
	}
}


/*
 * Get or set a particular flag value.  Must be called with UNITP lock
 * held.
 */
static uint_t
audio_4231_setflag(aud_stream_t *as, enum aud_opflag op, uint_t val)
{
	cs_stream_t *css;

/*	ASSERT_ASLOCKED(as); */

	css = (as == as->output_as) ? &UNITP(as)->output : &UNITP(as)->input;

	switch (op) {
	case AUD_ERRORRESET:	/* read reset error flag atomically */
		val = css->error;
		css->error = B_FALSE;
		break;

	case AUD_ACTIVE:	/* GET only */
		val = css->active;
		break;
	}

	return (val);
}


/*
 * Get or set device-specific information in the audio state structure.
 * Must be called with UNITP lock held.
 */
static aud_return_t
audio_4231_setinfo(aud_stream_t *as, mblk_t *mp, int *error)
{
	cs_unit_t *unitp;
	struct aud_4231_chip *chip;
	audio_info_t *ip;
	uint_t sample_rate, channels, precision, encoding;
	uint_t o_sample_rate, o_channels, o_precision, o_encoding;
	uint_t gain;
	uint_t capcount, playcount;
	uchar_t balance;
	uchar_t	tmp_bits;
	ddi_acc_handle_t handle;
	uint_t  tmp_pcount, tmp_ccount;

	ASSERT_ASLOCKED(as);

	unitp = UNITP(as);
	handle = unitp->cnf_handle;

	tmp_pcount = tmp_ccount = capcount = playcount = 0;
	/*
	 * Set device-specific info into device-independent structure
	 */
	if (!unitp->eb2dma) {
		tmp_pcount = ddi_getl(handle,
			    &unitp->chip->dmaregs.dmapc);
		tmp_ccount = ddi_getl(handle,
			    &unitp->chip->dmaregs.dmacc);
	} else {
		tmp_pcount = ddi_getl(unitp->cnf_handle_eb2play,
			    EB2_PLAY_BCR);
		tmp_ccount = ddi_getl(unitp->cnf_handle_eb2record,
			    EB2_REC_BCR);
	}

	if (unitp->input.active) {
		capcount =
		    audio_4231_sampleconv(&unitp->input, tmp_ccount);
	} else if (unitp->output.active) {
		playcount =
		    audio_4231_sampleconv(&unitp->output, tmp_pcount);
	}

	if (playcount > unitp->output.samples) {
		if (unitp->output.samples > 0) {
			as->output_as->info.samples = unitp->output.samples;
		} else {
			as->output_as->info.samples = 0;
		}
	} else {
		as->output_as->info.samples =
			    unitp->output.samples - playcount;
	}
	if (capcount > unitp->input.samples) {
		if (unitp->input.samples > 0) {
			as->input_as->info.samples = unitp->input.samples;
		} else {
			as->input_as->info.samples = 0;
		}
	} else {
		as->input_as->info.samples =
			    unitp->input.samples - capcount;
	}

	as->output_as->info.active = unitp->output.active;
	as->input_as->info.active = unitp->input.active;

	/*
	 * If getinfo, 'mp' is NULL...we're done
	 */
	if (mp == NULL)
		return (AUDRETURN_NOCHANGE);

	ip = (audio_info_t *)(void *)mp->b_cont->b_rptr;

	chip = unitp->chip;

	/*
	 * If any new value matches the current value, there
	 * should be no need to set it again here.
	 * However, it's work to detect this so don't bother.
	 */
	if (Modify(ip->play.gain) || Modifyc(ip->play.balance)) {
		if (Modify(ip->play.gain))
			gain = ip->play.gain;
		else
			gain = as->output_as->info.gain;

		if (Modifyc(ip->play.balance))
			balance = ip->play.balance;
		else
			balance = as->output_as->info.balance;

		as->output_as->info.gain = audio_4231_play_gain(chip,
		    gain, balance);
		as->output_as->info.balance = balance;
	}

	if (Modify(ip->record.gain) || Modifyc(ip->record.balance)) {
		if (Modify(ip->record.gain))
			gain = ip->record.gain;
		else
			gain = as->input_as->info.gain;

		if (Modifyc(ip->record.balance))
			balance = ip->record.balance;
		else
			balance = as->input_as->info.balance;

		as->input_as->info.gain = audio_4231_record_gain(chip,
		    gain, balance);
		as->input_as->info.balance = balance;
	}

	if (Modify(ip->record.buffer_size)) {
		if ((ip->record.buffer_size <= 0) ||
		    (ip->record.buffer_size > AUD_CS4231_MAX_BSIZE)) {
			*error = EINVAL;
		} else {
			as->input_as->info.buffer_size = ip->record.buffer_size;
		}
	}

	if (Modify(ip->play.buffer_size)) {
		if ((ip->play.buffer_size <= 0) ||
		    (ip->play.buffer_size > AUD_CS4231_MAX_BSIZE)) {
			*error = EINVAL;
		} else {
			if (as == as->output_as) {
				freezestr(as->output_as->writeq);
				strqset(as->output_as->writeq, QMAXPSZ, 0,
				    ip->play.buffer_size);
				unfreezestr(as->output_as->writeq);
				as->output_as->info.buffer_size =
				    ip->play.buffer_size;
			}
		}
	}

	if (Modify(ip->monitor_gain)) {
		as->distate->monitor_gain = audio_4231_monitor_gain(chip,
		    ip->monitor_gain);
	}

	if (Modifyc(ip->output_muted)) {
		as->distate->output_muted = audio_4231_output_muted(unitp,
		    ip->output_muted);
	}


	if (Modify(ip->play.port)) {
		as->output_as->info.port = audio_4231_outport(chip,
		    ip->play.port);
	}

	if (Modify(ip->record.port)) {
		as->input_as->info.port = audio_4231_inport(chip,
					    ip->record.port);
	}

	/*
	 * Save the old settings on any error of the folowing 4
	 * reset all back to the old and exit.
	 * DBRI compatability.
	 */

	o_sample_rate = as->info.sample_rate;
	o_encoding = as->info.encoding;
	o_precision = as->info.precision;
	o_channels = as->info.channels;

	/*
	 * Set the sample counters atomically, returning the old values.
	 */
	if (Modify(ip->play.samples) || Modify(ip->record.samples)) {
		if (as->output_as->info.open) {
			as->output_as->info.samples = unitp->output.samples;
			if (Modify(ip->play.samples))
				unitp->output.samples = ip->play.samples;
		}

		if (as->input_as->info.open) {
			as->input_as->info.samples = unitp->input.samples;
			if (Modify(ip->record.samples))
				unitp->input.samples = ip->record.samples;
		}
	}

	if (Modify(ip->play.sample_rate))
		sample_rate = ip->play.sample_rate;
	else if (Modify(ip->record.sample_rate))
		sample_rate = ip->record.sample_rate;
	else if ((unitp->hw_output_inited == B_FALSE) ||
		(unitp->hw_input_inited == B_FALSE))
		sample_rate = NEEDS_HW_INIT;
	else
		sample_rate = as->info.sample_rate;

	if (Modify(ip->play.channels))
		channels = ip->play.channels;
	else if (Modify(ip->record.channels))
		channels = ip->record.channels;
	else if ((unitp->hw_output_inited == B_FALSE) ||
		(unitp->hw_input_inited == B_FALSE))
		channels = NEEDS_HW_INIT;
	else
		channels = as->info.channels;


	if (Modify(ip->play.precision))
		precision = ip->play.precision;
	else if (Modify(ip->record.precision))
		precision = ip->record.precision;
	else if ((unitp->hw_output_inited == B_FALSE) ||
		(unitp->hw_input_inited == B_FALSE))
		precision = NEEDS_HW_INIT;
	else
		precision = as->info.precision;

	if (Modify(ip->play.encoding))
		encoding = ip->play.encoding;
	else if (Modify(ip->record.encoding))
		encoding = ip->record.encoding;
	else if ((unitp->hw_output_inited == B_FALSE) ||
		(unitp->hw_input_inited == B_FALSE))
		encoding = NEEDS_HW_INIT;
	else
		encoding = as->info.encoding;

	/*
	 * If setting to the current format, do not do anything.  Otherwise
	 * check and see if this is a valid format.
	 */

	if ((sample_rate == as->info.sample_rate) &&
	    (channels == as->info.channels) &&
	    (precision == as->info.precision) &&
	    (encoding == as->info.encoding) &&
	    (unitp->hw_output_inited == B_TRUE ||
		    unitp->hw_input_inited == B_TRUE)) {
		goto done;
	}

	/*
	 * setup default values if none specified and the audio
	 * chip has not been initialized
	 */
	if (sample_rate == NEEDS_HW_INIT)
		sample_rate = AUD_CS4231_SAMPLERATE;

	/*
	 * If we get here we must want to change the data format
	 * Changing the data format is done for both the play and
	 * record side for now.
	 */
	switch (sample_rate) {
	case 8000:		/* ULAW and ALAW */
		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.iar,
		    (uchar_t)(IAR_MCE | PLAY_DATA_FR));
		tmp_bits = chip->pioregs.idr;
		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.idr,
		    (uchar_t)CHANGE_DFR(tmp_bits, CS4231_DFR_8000));
		drv_usecwait(100); /* chip bug workaround */
		audio_4231_pollready();
		drv_usecwait(1000);	/* chip bug */
		sample_rate = AUD_CS4231_SAMPR8000;
		break;
	case 9600:		/* SPEECHIO */
		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.iar,
		    (uchar_t)(IAR_MCE | PLAY_DATA_FR));
		tmp_bits = chip->pioregs.idr;
		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.idr,
		    (uchar_t)CHANGE_DFR(tmp_bits, CS4231_DFR_9600));
		drv_usecwait(100); /* chip bug workaround */
		audio_4231_pollready();
		drv_usecwait(1000);	/* chip bug */
		sample_rate = AUD_CS4231_SAMPR9600;
		break;
	case 11025:
		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.iar,
		    (uchar_t)(IAR_MCE | PLAY_DATA_FR));
		tmp_bits = chip->pioregs.idr;
		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.idr,
		    (uchar_t)CHANGE_DFR(tmp_bits, CS4231_DFR_11025));
		drv_usecwait(100); /* chip bug workaround */
		audio_4231_pollready();
		drv_usecwait(1000);	/* chip bug */
		sample_rate = AUD_CS4231_SAMPR11025;
		break;
	case 16000:		/* G_722 */
		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.iar,
		    (uchar_t)(IAR_MCE | PLAY_DATA_FR));
		tmp_bits = chip->pioregs.idr;
		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.idr,
		    (uchar_t)CHANGE_DFR(tmp_bits, CS4231_DFR_16000));
		drv_usecwait(100); /* chip bug workaround */
		audio_4231_pollready();
		drv_usecwait(1000);	/* chip bug */
		sample_rate = AUD_CS4231_SAMPR16000;
		break;
	case 18900:		/* CDROM_XA_C */
		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.iar,
		    (uchar_t)(IAR_MCE | PLAY_DATA_FR));
		tmp_bits = chip->pioregs.idr;
		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.idr,
		    (uchar_t)CHANGE_DFR(tmp_bits, CS4231_DFR_18900));
		drv_usecwait(100); /* chip bug workaround */
		audio_4231_pollready();
		drv_usecwait(1000);	/* chip bug */
		sample_rate = AUD_CS4231_SAMPR18900;
		break;
	case 22050:
		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.iar,
		    (uchar_t)(IAR_MCE | PLAY_DATA_FR));
		tmp_bits = chip->pioregs.idr;
		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.idr,
		    (uchar_t)CHANGE_DFR(tmp_bits, CS4231_DFR_22050));
		drv_usecwait(100); /* chip bug workaround */
		audio_4231_pollready();
		drv_usecwait(1000);	/* chip bug */
		sample_rate = AUD_CS4231_SAMPR22050;
		break;
	case 32000:		/* DAT_32 */
		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.iar,
		    (uchar_t)(IAR_MCE | PLAY_DATA_FR));
		tmp_bits = chip->pioregs.idr;
		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.idr,
		    (uchar_t)CHANGE_DFR(tmp_bits, CS4231_DFR_32000));
		drv_usecwait(100); /* chip bug workaround */
		audio_4231_pollready();
		drv_usecwait(1000);	/* chip bug */
		sample_rate = AUD_CS4231_SAMPR32000;
		break;
	case 37800:		/* CDROM_XA_AB */
		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.iar,
		    (uchar_t)(IAR_MCE | PLAY_DATA_FR));
		tmp_bits = chip->pioregs.idr;
		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.idr,
		    (uchar_t)CHANGE_DFR(tmp_bits, CS4231_DFR_37800));
		drv_usecwait(100); /* chip bug workaround */
		audio_4231_pollready();
		drv_usecwait(1000);	/* chip bug */
		sample_rate = AUD_CS4231_SAMPR37800;
		break;
	case 44100:		/* CD_DA */
		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.iar,
		    (uchar_t)(IAR_MCE | PLAY_DATA_FR));
		tmp_bits = chip->pioregs.idr;
		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.idr,
		    (uchar_t)CHANGE_DFR(tmp_bits, CS4231_DFR_44100));
		drv_usecwait(100); /* chip bug workaround */
		audio_4231_pollready();
		drv_usecwait(1000);	/* chip bug */
		sample_rate = AUD_CS4231_SAMPR44100;
		break;
	case 48000:		/* DAT_48 */
		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.iar,
		    (uchar_t)(IAR_MCE | PLAY_DATA_FR));
		tmp_bits = chip->pioregs.idr;
		ddi_putb(handle,
		    &UNITP(as)->chip->pioregs.idr,
		    (uchar_t)CHANGE_DFR(tmp_bits, CS4231_DFR_48000));
		drv_usecwait(100); /* chip bug workaround */
		audio_4231_pollready();
		drv_usecwait(1000);	/* chip bug */
		sample_rate = AUD_CS4231_SAMPR48000;
		break;
	default:
		*error = EINVAL;
		break;
	} /* switch on sampling rate */

	if ((encoding == NEEDS_HW_INIT) || (Modify(ip->play.encoding)) ||
		    (Modify(ip->record.encoding)) && *error != EINVAL) {

		if (encoding == NEEDS_HW_INIT)
			encoding = AUDIO_ENCODING_ULAW;
		if (channels == NEEDS_HW_INIT)
			channels = AUD_CS4231_CHANNELS;
		if (precision == NEEDS_HW_INIT)
			precision = AUD_CS4231_PRECISION;
		/*
		 * If a process wants to modify the play or record format,
		 * another process can not have it open for recording.
		 */
		if (as->input_as->info.open &&
		    as->output_as->info.open &&
		    (as->input_as->readq != as->output_as->readq)) {
			*error = EBUSY;
			goto playdone;
		}

		switch (encoding) {
		case AUDIO_ENCODING_ULAW:
			if (Modify(channels) && (channels != 1))
				*error = EINVAL;
			ddi_putb(handle,
			    &UNITP(as)->chip->pioregs.iar,
				(uchar_t)IAR_MCE | PLAY_DATA_FR);
			tmp_bits = ddi_getb(handle,
			    &chip->pioregs.idr);
			ddi_putb(handle,
			    &UNITP(as)->chip->pioregs.idr,
				(uchar_t)CHANGE_ENCODING(tmp_bits,
				    CS4231_DFR_ULAW));
			tmp_bits = ddi_getb(handle,
			    &chip->pioregs.idr);
			ddi_putb(handle,
			    &UNITP(as)->chip->pioregs.idr,
				(uchar_t)(CS4231_MONO_ON(tmp_bits)));

			ddi_putb(handle,
			    &UNITP(as)->chip->pioregs.iar,
				(uchar_t)IAR_MCE | CAPTURE_DFR);
			tmp_bits = ddi_getb(handle,
			    &chip->pioregs.idr);
			ddi_putb(handle,
			    &UNITP(as)->chip->pioregs.idr,
				(uchar_t)CHANGE_ENCODING(tmp_bits,
				    CS4231_DFR_ULAW));
			tmp_bits = ddi_getb(handle,
			    &chip->pioregs.idr);
			ddi_putb(handle,
			    &UNITP(as)->chip->pioregs.idr,
				(uchar_t)(CS4231_MONO_ON(tmp_bits)));
			channels = 0x01;
			encoding = AUDIO_ENCODING_ULAW;
			break;
		case AUDIO_ENCODING_ALAW:
			if (Modify(channels) && (channels != 1))
				*error = EINVAL;
			ddi_putb(handle,
			    &UNITP(as)->chip->pioregs.iar,
				(uchar_t)IAR_MCE | PLAY_DATA_FR);
			tmp_bits = ddi_getb(handle,
			    &chip->pioregs.idr);
			ddi_putb(handle,
			    &UNITP(as)->chip->pioregs.idr,
				(uchar_t)CHANGE_ENCODING(tmp_bits,
				    CS4231_DFR_ALAW));
			tmp_bits = ddi_getb(handle,
			    &chip->pioregs.idr);
			ddi_putb(handle,
			    &UNITP(as)->chip->pioregs.idr,
				(uchar_t)(CS4231_MONO_ON(tmp_bits)));

			ddi_putb(handle,
			    &UNITP(as)->chip->pioregs.iar,
				(uchar_t)IAR_MCE | CAPTURE_DFR);
			tmp_bits = ddi_getb(handle,
			    &chip->pioregs.idr);
			ddi_putb(handle,
			    &UNITP(as)->chip->pioregs.idr,
				(uchar_t)CHANGE_ENCODING(tmp_bits,
				    CS4231_DFR_ALAW));
			tmp_bits = ddi_getb(handle,
			    &chip->pioregs.idr);
			ddi_putb(handle,
			    &UNITP(as)->chip->pioregs.idr,
				(uchar_t)(CS4231_MONO_ON(tmp_bits)));
			channels = 0x01;
			encoding = AUDIO_ENCODING_ALAW;
			break;
		case AUDIO_ENCODING_LINEAR:
			if (Modify(channels) && (channels != 2) &&
				    (channels != 1))
				*error = EINVAL;

			ddi_putb(handle,
			    &UNITP(as)->chip->pioregs.iar,
				(uchar_t)IAR_MCE | PLAY_DATA_FR);
			tmp_bits = ddi_getb(handle,
			    &chip->pioregs.idr);
			ddi_putb(handle,
			    &UNITP(as)->chip->pioregs.idr,
				(uchar_t)CHANGE_ENCODING(tmp_bits,
				    CS4231_DFR_LINEARBE));
			tmp_bits = ddi_getb(handle,
			    &chip->pioregs.idr);
			if (channels == 2) {
			    ddi_putb(handle,
				&UNITP(as)->chip->pioregs.idr,
				    (uchar_t)(CS4231_STEREO_ON(tmp_bits)));
			} else {
			    ddi_putb(handle,
				&UNITP(as)->chip->pioregs.idr,
				    (uchar_t)(CS4231_MONO_ON(tmp_bits)));
			}

			ddi_putb(handle,
			    &UNITP(as)->chip->pioregs.iar,
				(uchar_t)IAR_MCE | CAPTURE_DFR);
			tmp_bits = ddi_getb(handle,
			    &chip->pioregs.idr);
			ddi_putb(handle,
			    &UNITP(as)->chip->pioregs.idr,
				(uchar_t)CHANGE_ENCODING(tmp_bits,
				    CS4231_DFR_LINEARBE));
			tmp_bits = ddi_getb(handle,
			    &chip->pioregs.idr);
			if (channels == 2) {
			    ddi_putb(handle,
				&UNITP(as)->chip->pioregs.idr,
				    (uchar_t)(CS4231_STEREO_ON(tmp_bits)));
			} else {
			    ddi_putb(handle,
				&UNITP(as)->chip->pioregs.idr,
				    (uchar_t)(CS4231_MONO_ON(tmp_bits)));
			}
			encoding = AUDIO_ENCODING_LINEAR;
			break;
		case AUDIO_ENCODING_DVI:
			/* XXXX REV 2.0 FUTURE SUPPORT HOOK */
			if (Modify(channels) && (channels != 2) &&
				    (channels != 1))
				*error = EINVAL;

			ddi_putb(handle,
			    &UNITP(as)->chip->pioregs.iar,
				(uchar_t)IAR_MCE | PLAY_DATA_FR);
			tmp_bits = ddi_getb(handle,
			    &chip->pioregs.idr);
			ddi_putb(handle,
			    &UNITP(as)->chip->pioregs.idr,
				(uchar_t)CHANGE_ENCODING(tmp_bits,
				    CS4231_DFR_ADPCM));
			tmp_bits = ddi_getb(handle,
			    &chip->pioregs.idr);
			if (channels == 2) {
			    ddi_putb(handle,
				&UNITP(as)->chip->pioregs.idr,
				    (uchar_t)(CS4231_STEREO_ON(tmp_bits)));
			} else {
			    ddi_putb(handle,
				&UNITP(as)->chip->pioregs.idr,
				    (uchar_t)(CS4231_MONO_ON(tmp_bits)));
			}

			ddi_putb(handle,
			    &UNITP(as)->chip->pioregs.iar,
				(uchar_t)IAR_MCE | CAPTURE_DFR);
			tmp_bits = ddi_getb(handle,
			    &chip->pioregs.idr);
			ddi_putb(handle,
			    &UNITP(as)->chip->pioregs.idr,
				(uchar_t)CHANGE_ENCODING(tmp_bits,
				    CS4231_DFR_ADPCM));
			tmp_bits = ddi_getb(handle,
			    &chip->pioregs.idr);
			if (channels == 2) {
			    ddi_putb(handle,
				&UNITP(as)->chip->pioregs.idr,
				    (uchar_t)(CS4231_STEREO_ON(tmp_bits)));
			} else {
			    ddi_putb(handle,
				&UNITP(as)->chip->pioregs.idr,
				    (uchar_t)(CS4231_MONO_ON(tmp_bits)));
			}

			encoding = AUDIO_ENCODING_DVI;
			break;
		default:
			*error = EINVAL;
		} /* switch on audio encoding */
	playdone:;

	}

	/*
	 * We don't want to init if it is the control device
	 */
	if (as == &unitp->input.as || as == &unitp->output.as) {
		unitp->hw_output_inited = B_TRUE;
		unitp->hw_input_inited = B_TRUE;
	}
done:
	/*
	 * Update the "real" info structure (and others) accordingly
	 */

	if (*error == EINVAL) {
		sample_rate = o_sample_rate;
		encoding = o_encoding;
		precision = o_precision;
		channels = o_channels;
	}


	/*
	 * one last chance to make sure that we have something
	 * working. Set to defaults if one of the 4 horsemen
	 * is zero.
	 */

	if (sample_rate == 0 || encoding == 0 || precision == 0 ||
			    channels == 0) {
		sample_rate = AUD_CS4231_SAMPLERATE;
		channels = AUD_CS4231_CHANNELS;
		precision = AUD_CS4231_PRECISION;
		encoding = AUDIO_ENCODING_ULAW;
		unitp->hw_output_inited = B_FALSE;
		unitp->hw_input_inited = B_FALSE;
	}

	ip->play.sample_rate = ip->record.sample_rate = sample_rate;
	ip->play.channels = ip->record.channels = channels;
	ip->play.precision = ip->record.precision = precision;
	ip->play.encoding = ip->record.encoding = encoding;

	as->input_as->info.sample_rate = sample_rate;
	as->input_as->info.channels = channels;
	as->input_as->info.precision = precision;
	as->input_as->info.encoding = encoding;

	as->output_as->info.sample_rate = sample_rate;
	as->output_as->info.channels = channels;
	as->output_as->info.precision = precision;
	as->output_as->info.encoding = encoding;

	as->control_as->info.sample_rate = sample_rate;
	as->control_as->info.channels = channels;
	as->control_as->info.precision = precision;
	as->control_as->info.encoding = encoding;

	/*
	 * Init the hi and lowater if they have been set in the
	 * /etc/system file
	 */

	audio_4231_config_queue(as);

	ddi_putb(handle,
	    &chip->pioregs.iar, (uchar_t)IAR_MCD);
	drv_usecwait(100); /* chip bug workaround */
	audio_4231_pollready();
	drv_usecwait(1000);	/* chip bug */

	return (AUDRETURN_CHANGE);
} /* end of setinfo */


static void
audio_4231_queuecmd(aud_stream_t *as, aud_cmd_t *cmdp)
{
	cs_stream_t *css;

	ASSERT_ASLOCKED(as);
	ATRACE(audio_4231_queuecmd, '  AS', as);
	ATRACE(audio_4231_queuecmd, ' CMD', cmdp);

	if (as == as->output_as)
		css = &UNITP(as)->output;
	else
		css = &UNITP(as)->input;

	/*
	 * This device doesn't do packets, so make each buffer its own
	 * packet.
	 */

	cmdp->lastfragment = cmdp;

	/*
	 * If the virtual controller command list is NULL, then the interrupt
	 * routine is probably disabled.  In the event that it is not,
	 * setting a new command list below is safe at low priority.
	 */
	if (css->cmdptr == NULL && !css->active) {
		ATRACE(audio_4231_queuecmd, 'NULL', as->cmdlist.head);
		css->cmdptr = as->cmdlist.head;
		if (!css->active) {
			audio_4231_start(as); /* go, if not paused */
		}
	}


}

/*
 * Flush the device's notion of queued commands.
 * Must be called with UNITP lock held.
 */
static void
audio_4231_flushcmd(aud_stream_t *as)
{
	ASSERT_ASLOCKED(as);
	ATRACE(audio_4231_flushcmd, 'SA  ', as);
	drv_usecwait(250);
	if (as == as->output_as) {
		UNITP(as)->output.cmdptr = NULL;
		audio_4231_clear((apc_dma_list_t *)&dma_played_list,
			    UNITP(as));
	} else {
		UNITP(as)->input.cmdptr = NULL;
		audio_4231_clear((apc_dma_list_t *)&dma_recorded_list,
			    UNITP(as));
	}
}

/*
 * Initialize the audio chip to a known good state.
 * called with UNITP LOCKED
 */
static void
audio_4231_chipinit(cs_unit_t *unitp)
{
	struct aud_4231_chip *chip;
	ddi_acc_handle_t handle;
	uchar_t	tmpval;
	ulong_t ltmpval;

	chip = unitp->chip;
	ASSERT(chip != NULL);
	handle = unitp->cnf_handle;

	/*
	 * The APC has a bug where the reset is not done
	 * until you do the next pio to the APC. This
	 * next write to the CSR causes the posted reset to
	 * happen.
	 */
	if (!unitp->eb2dma) {
		ddi_putl(handle, APC_DMACSR, APC_RESET);
		ddi_putl(handle, APC_DMACSR, 0x00);

		ltmpval = ddi_getl(handle, APC_DMACSR);

		OR_SET_LONG_R(handle, APC_DMACSR, APC_CODEC_PDN, ltmpval);
		drv_usecwait(20);
		AND_SET_LONG_R(handle, APC_DMACSR, ~APC_CODEC_PDN, ltmpval);
	} else {
		ddi_putl(unitp->cnf_handle_eb2play,
			    EB2_PLAY_CSR, EB2_RESET);
		audio_4231_eb2cycpend((caddr_t)EB2_PLAY_CSR);
		AND_SET_LONG_R(unitp->cnf_handle_eb2play,
		    EB2_PLAY_CSR,
		    ~EB2_RESET, ltmpval);
		ddi_putl(unitp->cnf_handle_eb2record,
		    EB2_REC_CSR, EB2_RESET);
		audio_4231_eb2cycpend((caddr_t)EB2_REC_CSR);
		AND_SET_LONG_R(unitp->cnf_handle_eb2record,
		    EB2_REC_CSR,
		    ~EB2_RESET, ltmpval);
#ifdef HONEY_DEBUG
		ddi_putl(unitp->cnf_handle_auxio,
		    &unitp->audio_auxio, 0x01);
		drv_usecwait(1000);
		ddi_putl(unitp->cnf_handle_auxio,
		    &unitp->audio_auxio, 0x00);
		audio_4231_pollready();
#endif

	}

	OR_SET_BYTE_R(handle, &chip->pioregs.iar,
		    (uchar_t)IAR_MCE, tmpval);
	drv_usecwait(100); /* chip bug workaround */
	audio_4231_pollready();
	drv_usecwait(1000);	/* chip bug */

	ddi_putb(handle,
	    &chip->pioregs.iar, IAR_MCE | MISC_IR);
	ddi_putb(handle,
	    &chip->pioregs.idr, MISC_IR_MODE2);
	ddi_putb(handle,
	    &chip->pioregs.iar, IAR_MCE | PLAY_DATA_FR);
	ddi_putb(handle,
	    &chip->pioregs.idr, DEFAULT_DATA_FMAT);

	drv_usecwait(100); /* chip bug workaround */
	audio_4231_pollready();
	drv_usecwait(1000);	/* chip bug */

	ddi_putb(handle,
	    &chip->pioregs.iar, IAR_MCE | CAPTURE_DFR);
	ddi_putb(handle,
	    &chip->pioregs.idr, DEFAULT_DATA_FMAT);

	drv_usecwait(100); /* chip bug workaround */
	audio_4231_pollready();
	drv_usecwait(1000);	/* chip bug */

	ddi_putb(handle,
	    &chip->pioregs.iar, VERSION_R);

	tmpval = ddi_getb(handle, &chip->pioregs.idr);
	if (tmpval & CS4231A)
		CS4231_reva = B_TRUE;
	else
		CS4231_reva = B_FALSE;

	/* Turn on the Output Level Bit to be 2.8 Vpp */
	ddi_putb(handle,
	    &chip->pioregs.iar, IAR_MCE | ALT_FEA_EN1R);
	ddi_putb(handle,
	    &chip->pioregs.idr, (uchar_t)(OLB_ENABLE | DACZ_ON));

	/* Turn on the hi pass filter */
	ddi_putb(handle,
	    &chip->pioregs.iar, IAR_MCE | ALT_FEA_EN2R);
	if (CS4231_reva)
		ddi_putb(handle,
		    &chip->pioregs.idr, (HPF_ON | XTALE_ON));
	else
		ddi_putb(handle, &chip->pioregs.idr, HPF_ON);

	ddi_putb(handle,
	    &chip->pioregs.iar, IAR_MCE | MONO_IOCR);
	ddi_putb(handle,
	    &chip->pioregs.idr, (uchar_t)0x00);

	/* Init the play and Record gain registers */

	unitp->output.as.info.gain = audio_4231_play_gain(chip,
	    AUD_CS4231_DEFAULT_PLAYGAIN, AUDIO_MID_BALANCE);
	unitp->input.as.info.gain = audio_4231_record_gain(chip,
	    AUD_CS4231_DEFAULT_RECGAIN, AUDIO_MID_BALANCE);
	unitp->input.as.info.port = audio_4231_inport(chip,
		    AUDIO_MICROPHONE);
	unitp->output.as.info.port = audio_4231_outport(chip,
		    AUDIO_SPEAKER);
	unitp->distate.monitor_gain = audio_4231_monitor_gain(chip,
		    LOOPB_OFF);

	ddi_putb(handle,
	    &chip->pioregs.iar, IAR_MCD);
	audio_4231_pollready();

	/*
	 * leave the auto-calibrate enabled to prevent
	 * floating test bit 55 from causing errors
	 */
	if (!audio_4231_acal) {
		ddi_putb(handle,
		    &chip->pioregs.iar, IAR_MCE | INTERFACE_CR);
		AND_SET_BYTE_R(handle, &chip->pioregs.idr,
		    ACAL_DISABLE, tmpval);
		ddi_putb(handle,
		    &chip->pioregs.iar, IAR_MCD);
		audio_4231_pollready();
	}

	unitp->distate.output_muted = audio_4231_output_muted(unitp, 0x0);
	unitp->hw_output_inited = B_TRUE;
	unitp->hw_input_inited = B_TRUE;
	/*
	 * Let the chip settle down before we continue. If we
	 * don't the dac's in the 4231 are left at a high DC
	 * offset. This causes a "pop" on the first record
	 */
	drv_usecwait(160000);

}


/*
 * Set or clear internal loopback for diagnostic purposes.
 * Must be called with UNITP lock held.
 */
static void
audio_4231_loopback(cs_unit_t *unitp, uint_t loop)
{

}

static uint_t
audio_4231_output_muted(cs_unit_t *unitp, uint_t val)
{

	ddi_acc_handle_t handle;
	uchar_t tmpval;
	/*
	 * Just do the mute on Index 6 & 7 R&L output.
	 */
	handle = unitp->cnf_handle;
	if (val) {
		ddi_putb(handle, CS4231_IAR, L_OUTPUT_CR);
		OR_SET_BYTE_R(handle, CS4231_IDR, (uchar_t)OUTCR_MUTE, tmpval);
		ddi_putb(handle, CS4231_IAR, R_OUTPUT_CR);
		OR_SET_BYTE_R(handle, CS4231_IDR, (uchar_t)OUTCR_MUTE, tmpval);
		unitp->distate.output_muted = B_TRUE;
	} else {

		ddi_putb(handle, CS4231_IAR, L_OUTPUT_CR);
		AND_SET_BYTE_R(handle, CS4231_IDR,
		    (uchar_t)OUTCR_UNMUTE, tmpval);
		ddi_putb(handle, CS4231_IAR, R_OUTPUT_CR);
		AND_SET_BYTE_R(handle, CS4231_IDR,
		    (uchar_t)OUTCR_UNMUTE, tmpval);
		unitp->distate.output_muted = B_FALSE;
	}

	return (unitp->distate.output_muted);

}

static uint_t
audio_4231_inport(struct aud_4231_chip *chip, uint_t val)
{

	uint_t ret_val;
	ddi_acc_handle_t handle;
	uchar_t tmpval;
	cs_unit_t *unitp;

	unitp = &cs_units[0];
	handle = unitp->cnf_handle;


	/*
	 * In the 4231 we can have line, MIC  or CDROM_IN enabled at
	 * one time. We cannot have a mix of all. MIC is mic on
	 * Index 0, LINE and CDROM (AUX1) are on Index 0 and 1 through
	 * the 4231 mux.
	 */
	ret_val = 0;

	if ((val & AUDIO_INTERNAL_CD_IN) && (unitp->internal_cd)) {
		ddi_putb(handle,
		    &chip->pioregs.iar, L_INPUT_CR);
		tmpval = ddi_getb(handle, &chip->pioregs.idr);
		ddi_putb(handle, &chip->pioregs.idr,
		    CDROM_ENABLE(tmpval));

		ddi_putb(handle,
		    &chip->pioregs.iar, R_INPUT_CR);
		tmpval = ddi_getb(handle, &chip->pioregs.idr);
		ddi_putb(handle, &chip->pioregs.idr,
		    CDROM_ENABLE(tmpval));

		ret_val = AUDIO_INTERNAL_CD_IN;
	}
	if ((val & AUDIO_LINE_IN)) {

		ddi_putb(handle,
		    &chip->pioregs.iar, L_INPUT_CR);
		tmpval = ddi_getb(handle, &chip->pioregs.idr);
		ddi_putb(handle, &chip->pioregs.idr,
		    LINE_ENABLE(tmpval));

		ddi_putb(handle,
		    &chip->pioregs.iar, R_INPUT_CR);
		tmpval = ddi_getb(handle, &chip->pioregs.idr);
		ddi_putb(handle, &chip->pioregs.idr,
		    LINE_ENABLE(tmpval));

		ret_val = AUDIO_LINE_IN;

	} else if (val & AUDIO_MICROPHONE) {

		ddi_putb(handle,
		    &chip->pioregs.iar, L_INPUT_CR);
		tmpval = ddi_getb(handle, &chip->pioregs.idr);
		ddi_putb(handle, &chip->pioregs.idr,
		    MIC_ENABLE(tmpval));

		ddi_putb(handle,
		    &chip->pioregs.iar, R_INPUT_CR);
		tmpval = ddi_getb(handle, &chip->pioregs.idr);
		ddi_putb(handle, &chip->pioregs.idr,
		    MIC_ENABLE(tmpval));

		ret_val = AUDIO_MICROPHONE;
	}

	if ((val & AUDIO_AUX1) && !unitp->internal_cd) {
		ddi_putb(handle,
		    &chip->pioregs.iar, L_INPUT_CR);
		tmpval = ddi_getb(handle, &chip->pioregs.idr);
		ddi_putb(handle, &chip->pioregs.idr,
		    CDROM_ENABLE(tmpval));

		ddi_putb(handle,
		    &chip->pioregs.iar, R_INPUT_CR);
		tmpval = ddi_getb(handle, &chip->pioregs.idr);
		ddi_putb(handle, &chip->pioregs.idr,
		    CDROM_ENABLE(tmpval));

		ret_val = AUDIO_AUX1;
	}

	return (ret_val);
}
/*
 * Must be called with UNITP lock held.
 */
static uint_t
audio_4231_outport(struct aud_4231_chip *chip, uint_t val)
{
	uint_t ret_val;
	ddi_acc_handle_t handle;
	uchar_t tmpval;
	cs_unit_t *unitp;

	unitp = &cs_units[0];
	handle = unitp->cnf_handle;

	/*
	 *  Disable everything then selectively enable it.
	 */

	ret_val = 0;
	ddi_putb(handle,
	    &chip->pioregs.iar, MONO_IOCR);
	OR_SET_BYTE_R(handle, &chip->pioregs.idr,
	    (uchar_t)MONOIOCR_SPKRMUTE, tmpval);
	ddi_putb(handle,
	    &chip->pioregs.iar, PIN_CR);
	OR_SET_BYTE_R(handle, &chip->pioregs.idr,
	    (PINCR_LINE_MUTE | PINCR_HDPH_MUTE), tmpval);

	if (val & AUDIO_SPEAKER) {
		ddi_putb(handle,
		    &chip->pioregs.iar, MONO_IOCR);
		AND_SET_BYTE_R(handle, &chip->pioregs.idr,
		    ~MONOIOCR_SPKRMUTE, tmpval);
		ret_val |= AUDIO_SPEAKER;
	}
	if (val & AUDIO_HEADPHONE) {
		ddi_putb(handle,
		    &chip->pioregs.iar, PIN_CR);
		AND_SET_BYTE_R(handle, &chip->pioregs.idr,
		    ~PINCR_HDPH_MUTE, tmpval);
		ret_val |= AUDIO_HEADPHONE;
	}

	if (val & AUDIO_LINE_OUT) {
		ddi_putb(handle,
		    &chip->pioregs.iar, PIN_CR);
		AND_SET_BYTE_R(handle, &chip->pioregs.idr,
		    ~PINCR_LINE_MUTE, tmpval);
		ret_val |= AUDIO_LINE_OUT;
	}

	return (ret_val);
}

static uint_t
audio_4231_monitor_gain(struct aud_4231_chip *chip, uint_t val)
{
	int aten;
	ddi_acc_handle_t handle;
	cs_unit_t *unitp;

	unitp = &cs_units[0];
	handle = unitp->cnf_handle;

	aten = AUD_CS4231_MON_MAX_ATEN -
	    (val * (AUD_CS4231_MON_MAX_ATEN + 1) /
			    (AUDIO_MAX_GAIN + 1));

	/*
	 * Normal monitor registers are the index 13. Line monitor for
	 * now can be registers 18 and 19. Which are actually MIX to
	 * OUT directly We don't use these for now 8/3/93.
	 */

	ddi_putb(handle,
	    &chip->pioregs.iar, LOOPB_CR);
	if (aten >= AUD_CS4231_MON_MAX_ATEN) {
		ddi_putb(handle,
		    &chip->pioregs.idr, LOOPB_OFF);
	} else {

		/*
		 * Loop Back enable
		 * is in bit 0, 1 is reserved, thus the shift 2.
		 * all other aten and gains are in the low order
		 * bits, this one has to be differnt and be in the
		 * high order bits sigh...
		 */
		ddi_putb(handle, &chip->pioregs.idr,
		    ((aten << 2) | LOOPB_ON));
	}

	/*
	 * We end up returning a value slightly different than the one
	 * passed in - *most* applications expect this.
	 */
	return ((val == AUDIO_MAX_GAIN) ? AUDIO_MAX_GAIN :
	    ((AUD_CS4231_MAX_DEV_ATEN - aten) * (AUDIO_MAX_GAIN + 1) /
	    (AUD_CS4231_MAX_DEV_ATEN + 1)));
}

/*
 * Convert play gain to chip values and load them.
 * Return the closest appropriate gain value.
 * Must be called with UNITP lock held.
 */
static uint_t
audio_4231_play_gain(struct aud_4231_chip *chip, uint_t val, uchar_t balance)
{

	uint_t tmp_val, r, l;
	uint_t la, ra;
	u_char old_gain;
	ddi_acc_handle_t handle;
	cs_unit_t *unitp;

	unitp = &cs_units[0];
	handle = unitp->cnf_handle;


	r = l = val;
	if (balance < AUDIO_MID_BALANCE) {
		r = MAX(0, (int)(val -
		    ((AUDIO_MID_BALANCE - balance) << AUDIO_BALANCE_SHIFT)));
	} else if (balance > AUDIO_MID_BALANCE) {
		l = MAX(0, (int)(val -
		    ((balance - AUDIO_MID_BALANCE) << AUDIO_BALANCE_SHIFT)));
	}

	if (l == 0) {
		la = AUD_CS4231_MAX_DEV_ATEN;
	} else {
		la = AUD_CS4231_MAX_ATEN -
		    (l * (AUD_CS4231_MAX_ATEN + 1) / (AUDIO_MAX_GAIN + 1));
	}
	if (r == 0) {
		ra = AUD_CS4231_MAX_DEV_ATEN;
	} else {
		ra = AUD_CS4231_MAX_ATEN -
		    (r * (AUD_CS4231_MAX_ATEN + 1) / (AUDIO_MAX_GAIN + 1));
	}

	/* Load output gain registers */


	ddi_putb(handle,
	    &chip->pioregs.iar, L_OUTPUT_CR);
	old_gain = ddi_getb(handle, &chip->pioregs.idr);
	ddi_putb(handle,
	    &chip->pioregs.idr, GAIN_SET(old_gain, la));
	ddi_putb(handle,
	    &chip->pioregs.iar, R_OUTPUT_CR);
	old_gain = ddi_getb(handle, &chip->pioregs.idr);
	ddi_putb(handle,
	    &chip->pioregs.idr, GAIN_SET(old_gain, ra));

	if ((val == 0) || (val == AUDIO_MAX_GAIN)) {
		tmp_val = val;
	} else {
		if (l == val) {
			tmp_val = ((AUD_CS4231_MAX_ATEN - la) *
			    (AUDIO_MAX_GAIN + 1) /
				    (AUD_CS4231_MAX_ATEN + 1));
		} else if (r == val) {
			tmp_val = ((AUD_CS4231_MAX_ATEN - ra) *
			    (AUDIO_MAX_GAIN + 1) /
				    (AUD_CS4231_MAX_ATEN + 1));
		}
	}
	return (tmp_val);
}


/*
 * Convert record gain to chip values and load them.
 * Return the closest appropriate gain value.
 * Must be called with UNITP lock held.
 */
uint_t
audio_4231_record_gain(struct aud_4231_chip *chip, uint_t val, uchar_t balance)
{

	uint_t tmp_val, r, l;
	uint_t lg, rg;
	ddi_acc_handle_t handle;
	uchar_t tmpval;
	cs_unit_t *unitp;

	unitp = &cs_units[0];
	handle = unitp->cnf_handle;

	r = l = val;
	tmp_val = 0;

	if (balance < AUDIO_MID_BALANCE) {
		r = MAX(0, (int)(val -
		    ((AUDIO_MID_BALANCE - balance) << AUDIO_BALANCE_SHIFT)));
	} else if (balance > AUDIO_MID_BALANCE) {
		l = MAX(0, (int)(val -
		    ((balance - AUDIO_MID_BALANCE) << AUDIO_BALANCE_SHIFT)));
	}
	lg = l * (AUD_CS4231_MAX_GAIN + 1) / (AUDIO_MAX_GAIN + 1);
	rg = r * (AUD_CS4231_MAX_GAIN + 1) / (AUDIO_MAX_GAIN + 1);

	/* Load input gain registers */
	ddi_putb(handle,
	    &chip->pioregs.iar, L_INPUT_CR);
	tmpval = ddi_getb(handle, &chip->pioregs.idr);
	ddi_putb(handle,
	    &chip->pioregs.idr, RECGAIN_SET(tmpval, lg));
	ddi_putb(handle,
	    &chip->pioregs.iar, R_INPUT_CR);
	tmpval = ddi_getb(handle, &chip->pioregs.idr);
	ddi_putb(handle,
	    &chip->pioregs.idr, RECGAIN_SET(tmpval, rg));

	/*
	 * We end up returning a value slightly different than the one
	 * passed in - *most* applications expect this.
	 */
	if (l == val) {
		if (l == 0) {
			tmp_val = 0;
		} else {
			tmp_val = ((lg + 1) * AUDIO_MAX_GAIN) /
			    (AUD_CS4231_MAX_GAIN + 1);
		}
	} else if (r == val) {
		if (r == 0) {
			tmp_val = 0;
		} else {

			tmp_val = ((rg + 1) * AUDIO_MAX_GAIN) /
			    (AUD_CS4231_MAX_GAIN + 1);
		}
	}

	return (tmp_val);


}

uint_t
audio_4231_eb2recintr()
{
	cs_unit_t *unitp;
	ulong_t	ltmpval, eb2intr;


	/*
	 * Figure out which chip interrupted.
	 * Since we only have one chip, we punt and assume device zero.
	 */
	unitp = &cs_units[0];
	/* Acquire spin lock */

#ifndef HONEY_DEBUG
	LOCK_HILEVEL(unitp);
#endif

	/* clear off the intr first */
	eb2intr = ddi_getl(unitp->cnf_handle_eb2record, EB2_REC_CSR);
	OR_SET_LONG_R(unitp->cnf_handle_eb2record, EB2_REC_CSR,
	    EB2_TC, ltmpval);
	/*
	 * Check to see if it is a Err, if not check to see if
	 * it isn't a TC inte (which we are expecting), if not
	 * a TC intr it is a dev intr. Not expected for now.
	 */
	if (eb2intr & EB2_ERR_PEND) {
		cmn_err(CE_WARN,
		    "EB2 Error pending CSR is 0x%X, ACR = 0x%X, BCR = 0x%X",
		    eb2intr,
		    ddi_getl(unitp->cnf_handle_eb2record, EB2_PLAY_ACR),
		    ddi_getl(unitp->cnf_handle_eb2record, EB2_PLAY_BCR));
		OR_SET_LONG_R(unitp->cnf_handle_eb2record,
		    EB2_REC_CSR, EB2_RESET, eb2intr);
		audio_4231_eb2cycpend((caddr_t)EB2_REC_CSR);
		AND_SET_LONG_R(unitp->cnf_handle_eb2record,
		    EB2_REC_CSR,
		    ~EB2_RESET, eb2intr);
		ATRACE(audio_4231_eb2recintr, 'ERR ',
		    ddi_getl(unitp->cnf_handle_eb2record, EB2_REC_CSR));
		return (DDI_INTR_CLAIMED);
	} else if (!(eb2intr & EB2_TC)) {
			cmn_err(CE_WARN, "audiocs: Device interrupt, Why? 0x%X",
			    eb2intr);
		return (DDI_INTR_UNCLAIMED);
	}
	audio_4231_recintr(unitp);
#ifndef HONEY_DEBUG
	UNLOCK_HILEVEL(unitp);
#endif
	return (DDI_INTR_CLAIMED);
}

uint_t
audio_4231_eb2playintr()
{
	cs_unit_t *unitp;
	ulong_t	ltmpval, eb2intr;
	ulong_t byte_count, offset, orig_bytecount;


	/*
	 * Figure out which chip interrupted.
	 * Since we only have one chip, we punt and assume device zero.
	 */
	unitp = &cs_units[0];
	/* Acquire spin lock */

#ifndef HONEY_DEBUG
	LOCK_HILEVEL(unitp);
#endif
	/* check to see if this is an eb2 intr */
	eb2intr = ddi_getl(unitp->cnf_handle_eb2play,
	    EB2_PLAY_CSR);
	/*
	 * Check to see if it is a Err, if not check to see if
	 * it isn't a TC inte (which we are expecting), if not
	 * a TC intr it is a dev intr. Not expected for now.
	 */
	if (eb2intr & EB2_ERR_PEND) {
		byte_count = ddi_getl(unitp->cnf_handle_eb2play,
		    EB2_PLAY_BCR);
		cmn_err(CE_WARN,
		    "EB2 Error pending CSR is 0x%X, ACR = 0x%X, BCR = 0x%X",
		    eb2intr,
		    ddi_getl(unitp->cnf_handle_eb2play, EB2_PLAY_ACR),
		    byte_count);

		offset = unitp->typ_playlength - byte_count;

		OR_SET_LONG_R(unitp->cnf_handle_eb2play,
		    EB2_PLAY_CSR,
		    EB2_RESET, eb2intr);
		audio_4231_eb2cycpend((caddr_t)EB2_PLAY_CSR);
		AND_SET_LONG_R(unitp->cnf_handle_eb2play,
		    EB2_PLAY_CSR,
		    ~EB2_RESET, eb2intr);
		ATRACE(audio_4231_eb2playintr, 'ERR ',
		    ddi_getl(unitp->cnf_handle_eb2play, EB2_PLAY_CSR));

		ddi_putl(unitp->cnf_handle_eb2play, EB2_PLAY_ACR,
		(unitp->playlastaddr + offset));

		OR_SET_LONG_R(unitp->cnf_handle_eb2play,
		    EB2_PLAY_CSR, EB2_PLAY_SETUP, ltmpval);

	} else if (!(eb2intr & EB2_TC)) {
			cmn_err(CE_WARN, "audiocs: Device interrupt, Why? 0x%X",
			    eb2intr);
		return (DDI_INTR_UNCLAIMED);

	} else if (eb2intr & EB2_TC) {
		/* clear the intr */
		OR_SET_LONG_R(unitp->cnf_handle_eb2play,
		    EB2_PLAY_CSR, EB2_TC, ltmpval);
		audio_4231_playintr(unitp);
	}

#ifndef HONEY_DEBUG
	UNLOCK_HILEVEL(unitp);
#endif
	return (DDI_INTR_CLAIMED);
}

#ifdef HONEY_DEBUG
uint_t
audio_4231_eb2cintr()
{

	cs_unit_t *unitp;
	uint_t rc;
	ulong_t reccsr, playcsr;

	unitp = &cs_units[0];
	LOCK_HILEVEL(unitp);
	rc = DDI_INTR_UNCLAIMED;

	playcsr = ddi_getl(unitp->cnf_handle_eb2play, EB2_PLAY_CSR);
	reccsr = ddi_getl(unitp->cnf_handle_eb2record, EB2_REC_CSR);

	ATRACE(audio_4231_eb2cintr, 'PLCR', playcsr);
	ATRACE(audio_4231_eb2cintr, 'RECR', reccsr);

	if (playcsr & EB2_INT_PEND) {
		ATRACE(audio_4231_eb2cintr, 'PLAY', EB2_PLAY_CSR);
		audio_4231_eb2playintr();
		rc = DDI_INTR_CLAIMED;
	}
	if (reccsr & EB2_INT_PEND) {
		ATRACE(audio_4231_eb2cintr, 'RECR', EB2_REC_CSR);
		audio_4231_eb2recintr();
		rc = DDI_INTR_CLAIMED;
	}
	UNLOCK_HILEVEL(unitp);
	return (rc);
}
#endif

/*
 * Common interrupt routine. vectors to play of record.
 */
uint_t
audio_4231_cintr()
{
	cs_unit_t *unitp;
	struct aud_4231_chip *chip;
	long dmacsr, rc;
	uchar_t tmpval;
	ddi_acc_handle_t handle;
	ulong_t ltmpval;

	/* Acquire spin lock */

	/*
	 * Figure out which chip interrupted.
	 * Since we only have one chip, we punt and assume device zero.
	 */
	unitp = &cs_units[0];
	LOCK_HILEVEL(unitp);
	handle = unitp->cnf_handle;

	rc = DDI_INTR_UNCLAIMED;

	chip = unitp->chip;

	/* read and store the APC csr */
	dmacsr = ddi_getl(handle, &chip->dmaregs.dmacsr);

	/* clear all possible ints */
	ddi_putl(handle, &chip->dmaregs.dmacsr, dmacsr);

	ATRACE(audio_4231_cintr, 'RSCD', dmacsr);

	/*
	 * We want to update the record samples and play samples
	 * when we take an interrupt only. This is because of the
	 * prime condition that we do on a start. We end up
	 * getting dmasize ahead on the sample count because of the
	 * dual dma registers in the APC chip.
	 */


	if (dmacsr & APC_CI) {
		if (dmacsr & APC_CD) {
			if (unitp->input.active) {
				unitp->input.samples +=
					audio_4231_sampleconv(&unitp->input,
					    unitp->typ_reclength);
			}
			audio_4231_recintr(unitp);
		}

		rc = DDI_INTR_CLAIMED;
	}

	if ((dmacsr & APC_CMI) && (unitp->input.active != B_TRUE)) {
		ATRACE(audio_4231_cintr, 'PCON', dmacsr);
		unitp->input.active = B_FALSE;
		rc = DDI_INTR_CLAIMED;
	}

	if ((dmacsr & APC_PMI) && (unitp->output.active != B_TRUE)) {
		ATRACE(audio_4231_cintr, 'LPON', dmacsr);

		if (unitp->output.as.openflag) {
			audio_4231_samplecalc(unitp, unitp->typ_playlength,
				    PLAY_DIRECTION);
		}
		audio_4231_clear((apc_dma_list_t *)&dma_played_list,
				    unitp);
		unitp->output.active = B_FALSE;
		unitp->output.cmdptr = NULL;
		audio_process_output(&unitp->output.as);
		rc = DDI_INTR_CLAIMED;
	}
	if (dmacsr & APC_PI) {
		if (dmacsr & APC_PD) {
			if (unitp->output.active &&
			    ddi_getl(handle, &chip->dmaregs.dmapc)) {
				unitp->output.samples +=
					audio_4231_sampleconv(&unitp->output,
					    unitp->typ_playlength);
			}
			audio_4231_playintr(unitp);
			audio_process_output(&unitp->output.as);
		}

		rc = DDI_INTR_CLAIMED;

	}


	if (dmacsr & APC_EI) {
		ATRACE(audio_4231_cintr, '!RRE', dmacsr);
		rc = DDI_INTR_CLAIMED;
#ifdef AUDIOTRACE
		cmn_err(CE_WARN, "audio_4231_cintr: BUS ERROR! dmacsr 0x%x",
				    dmacsr);
#endif
	}

	UNLOCK_HILEVEL(unitp);
	ATRACE(audio_4231_cintr, 'TER ', dmacsr);
	return (rc);


}

uint_t
audio_4231_playintr(cs_unit_t *unitp)
{
	aud_cmd_t *cmdp;
	cs_stream_t *ds;
	ddi_dma_handle_t buf_dma_handle;
	ddi_dma_cookie_t buf_dma_cookie;
	uint_t	ccountp;
	u_int length, eb2_dma_off;
	ulong_t ltmpval, l1tmpval;
	ulong_t eb2intr;
	int e;
	uint_t retval = B_TRUE;
	int lastcount = 0;
	int need_processing = 0;
	ddi_acc_handle_t handle;

	ds = &unitp->output;		/* Get cs stream pointer */
	eb2_dma_off = B_FALSE;

	if (!unitp->eb2dma) {
		handle = unitp->cnf_handle;
	} else {
		handle = unitp->cnf_handle_eb2play;
		/* check to see if this is an eb2 intr */
		eb2intr = ddi_getl(unitp->cnf_handle_eb2play,
		    EB2_PLAY_CSR);
	}

	lastcount = (unitp->playcount % DMA_LIST_SIZE);
	if (lastcount > 0)
		lastcount--;
	else
		lastcount = DMA_LIST_SIZE - 1;
	ATRACE(audio_4231_playintr, 'tsal', lastcount);
	ATRACE(audio_4231_playintr, 'yalp', unitp->playcount);

	cmdp = unitp->output.cmdptr;

	if (cmdp == NULL) {
		unitp->output.active = B_FALSE;
		ATRACE(audio_4231_playintr, 'lluN', cmdp);
		goto done;
	}

	if (cmdp == dma_played_list[lastcount].cmdp) {
		ATRACE(audio_4231_playintr, 'emas', cmdp);
		if (cmdp->next != NULL) {
			cmdp = cmdp->next;
			unitp->output.cmdptr = cmdp;
			unitp->output.active = B_TRUE;
			ATRACE(audio_4231_playintr, 'dmcn', cmdp);
		} else {
			/*
			 * if the fifos have drained and there are no
			 * cmd buffers left to process, then clean up
			 * dma resources
			 */
			if (!unitp->eb2dma) {
				ltmpval = ddi_getl(handle,
				    &unitp->chip->dmaregs.dmapc);
			} else {
#ifdef HONEY_CHAIN1
				if (!(eb2intr & EB2_DMA_ON)) {
					ltmpval = 0x00;
					AND_SET_LONG_R(handle,
					    EB2_PLAY_CSR,
					    (~EB2_INT_EN),
					    l1tmpval);
					AND_SET_LONG_R(handle,
					    EB2_PLAY_CSR,
					    (~EB2_EN_DMA),
					    l1tmpval);
				}
				/* clear the intr */
				OR_SET_LONG_R(handle, EB2_PLAY_CSR,
				    EB2_TC, l1tmpval);
#endif
			}
			if (!ltmpval) {
				ATRACE(audio_4231_playintr, 'dmcL', cmdp);
				audio_4231_samplecalc(unitp,
					unitp->typ_playlength, PLAY_DIRECTION);
				audio_4231_clear((apc_dma_list_t *)
					&dma_played_list, unitp);
				unitp->output.active = B_FALSE;
				unitp->output.cmdptr = NULL;
			} else {
				/*
				 * if the fifo's are not empty, then wait for
				 * the next interrupt to clean up dma resources
				 * If there is no interrupt pending in
				 * the csr then we must be in a
				 * *prime* condition for the dma engine.
				 * in theis case we don't want to mark
				 * the active flag as FALSE because we
				 * are technically active.
				 */

				if (ddi_getl(handle,
				    &unitp->chip->dmaregs.dmacsr) & APC_PI) {

					unitp->output.active = B_FALSE;
				} else {
					unitp->output.active = B_TRUE;
				}
				ATRACE(audio_4231_playintr, 'dmcN', cmdp);
			}
			unitp->output.error = B_TRUE;
			goto done;
		}
	}

eb2again:
	if (unitp->output.active) {
		unitp->output.error = B_FALSE;
		ATRACE(audio_4231_playintr, ' DMC', unitp->output.cmdptr);

		/*
		 * Ignore null and non-data buffers
		 */
		while (cmdp != NULL && (cmdp->skip || cmdp->done)) {
			cmdp->done = B_TRUE;
			need_processing++;
			cmdp = cmdp->next;
			unitp->output.cmdptr = cmdp;
			ATRACE(audio_4231_playintr, 'DMCS',
				    unitp->output.cmdptr);
		}

		/*
		 * if no cmds to process and the fifo's have
		 * drained and no more dma in progress, then
		 * clean up resources
		 */
		if (!unitp->eb2dma) {
			ltmpval = ddi_getl(handle,
			    &unitp->chip->dmaregs.dmapc);
		} else {
			if (!(eb2intr & EB2_NADDR_LOADED) &&
			    (eb2intr & EB2_EN_NEXT)) {
				ltmpval = 0x00;
				eb2_dma_off = 0x01;
			}
		}
		if (!cmdp && !ltmpval && need_processing) {
			ATRACE(audio_4231_playintr, 'Lcmd', cmdp);
			audio_4231_samplecalc(unitp,
				unitp->typ_playlength, PLAY_DIRECTION);
			audio_4231_clear((apc_dma_list_t *)
				&dma_played_list, unitp);
			unitp->output.error = B_TRUE;
			unitp->output.active = B_FALSE;
			unitp->output.cmdptr = NULL;
			goto done;
		}
		/*
		 * Check for flow error EOF??
		 */
		if (cmdp == NULL) {
			/* Flow error condition */
			unitp->output.error = B_TRUE;
			unitp->output.active = B_FALSE;
			retval = B_FALSE;
			ATRACE(audio_4231_playintr, 'LLUN', cmdp);
			goto done;
		}

		if (unitp->output.cmdptr->skip ||
			    unitp->output.cmdptr->done) {
			ATRACE(audio_4231_playintr, 'piks', cmdp);
			need_processing++;
			goto done;
		}
		/*
		 * Transfer play data
		 */
		/*
		 * Setup for DMA transfers to the buffer from the device
		 */

		length = cmdp->enddata - cmdp->data;
		/*
		 * need 4 byte alignment
		 */
		if (length & 0x3)
			length &= 0xFFFFFFFc;
#ifndef HONEY_DEBUG
		if (cmdp->data == NULL || length == NULL ||
			    length > AUD_CS4231_BSIZE) {
			cmdp->skip = B_TRUE;
			need_processing++;
			goto done;
		}
#else
		if (cmdp->data == NULL || length == NULL) {
			cmdp->skip = B_TRUE;
			need_processing++;
			goto done;
		}
#endif

		e = ddi_dma_alloc_handle(unitp->dip, &apc_dma_attr,
						    DDI_DMA_DONTWAIT, NULL,
						    &buf_dma_handle);
		if (e == DDI_DMA_BADATTR) {
			cmn_err(CE_WARN,
			    "BAD_ATTR val 0x%X in playback!", e);
			return (B_FALSE);
		} else if (e != DDI_SUCCESS) {
			cmn_err(CE_WARN,
			    "DMA_ALLOC_HANDLE failed in playback!");
			return (B_FALSE);
		}

		e  = ddi_dma_addr_bind_handle(buf_dma_handle, (struct as *)0,
			    (caddr_t)cmdp->data, length,
			    DDI_DMA_WRITE, DDI_DMA_DONTWAIT, 0,
			    &buf_dma_cookie, &ccountp);

		switch (e) {
		case DDI_DMA_MAPPED:
			e = 0x00;
			break;
		case DDI_DMA_PARTIAL_MAP:
			cmn_err(CE_WARN,
			    "PARTIAL_MAP failed in playback!");
			break;
		case DDI_DMA_NORESOURCES:
			cmn_err(CE_WARN,
			    "DMA_NORESOURCES failed in playback!");
			break;
		case DDI_DMA_NOMAPPING:
			cmn_err(CE_WARN,
			    "NO_DMA_MAPPINGS failed in playback!");
			break;
		case DDI_DMA_TOOBIG:
			cmn_err(CE_WARN,
			    "DMA_ALLOC_TOOBIG failed in playback!");
			cmn_err(CE_WARN,
			    "Alloc of %d\n bytes attempted\n", length);
			break;
		default:
			cmn_err(CE_WARN,
			    "Unknown dma alloc failure 0x%d\n", e);
		}

		if (e) {
			return (B_FALSE);
		}

		if (buf_dma_handle) {
			if (!unitp->eb2dma) {
				ddi_putl(handle,
				    &unitp->chip->dmaregs.dmapnva,
				    buf_dma_cookie.dmac_address);
				ddi_putl(handle,
				    &unitp->chip->dmaregs.dmapnc,
				    buf_dma_cookie.dmac_size);
				ATRACE(audio_4231_playintr, ' AVP',
				unitp->chip->dmaregs.dmapva);
				ATRACE(audio_4231_playintr, 'TNCP',
				unitp->chip->dmaregs.dmapc);
				ATRACE(audio_4231_playintr, 'AVNP',
				unitp->chip->dmaregs.dmapnva);
				ATRACE(audio_4231_playintr, ' CNP',
				unitp->chip->dmaregs.dmapnc);
				ATRACE(audio_4231_playintr, 'RSCP',
				unitp->chip->dmaregs.dmacsr);
			} else {
				ATRACE(audio_4231_playintr, 'EB2C',
				    buf_dma_cookie.dmac_size);
				ddi_putl(unitp->cnf_handle_eb2play,
				    EB2_PLAY_BCR,
				    buf_dma_cookie.dmac_size);
				unitp->typ_playlength =
				    buf_dma_cookie.dmac_size;
				unitp->playlastaddr =
				    buf_dma_cookie.dmac_address;
				ATRACE(audio_4231_playintr, 'EB2A',
				    buf_dma_cookie.dmac_address);
				ddi_putl(unitp->cnf_handle_eb2play,
				    EB2_PLAY_ACR,
				    buf_dma_cookie.dmac_address);

				ATRACE(audio_4231_playintr, 'CHPC',
				    ddi_getl(unitp->cnf_handle_eb2play,
				    EB2_PLAY_BCR));
				ATRACE(audio_4231_playintr, 'CHPA',
				    ddi_getl(unitp->cnf_handle_eb2play,
				    EB2_PLAY_ACR));
				ATRACE(audio_4231_playintr, 'ECSR',
				    ddi_getl(unitp->cnf_handle_eb2play,
				    EB2_PLAY_CSR));
			}
			audio_4231_insert(cmdp, buf_dma_handle,
				    unitp->playcount,
				    (apc_dma_list_t *)&dma_played_list, unitp);
			unitp->playcount++;
			retval = B_TRUE;

			if (cmdp->next != NULL) {
				unitp->output.cmdptr = cmdp->next;
			}
			ATRACE(audio_4231_playintr, 'DMCN',
			    unitp->output.cmdptr);
			unitp->typ_playlength = length;
#ifdef HONEY_CHAIN1
			/*
			 * If we have written a valid cmd buf and
			 * at the time of writing that cmd buf to the
			 * eb2 engine we had underflowed, (indicated
			 * by the EB2_DMA_ON == 0) we need to try and
			 * write the next address again to re-enable
			 * chaining in the engine. Else we will
			 * proceed to run in non-chained mode.
			 * XXX this may not be the optimal way to
			 * do this.
			 */
			if (eb2_dma_off) {
				audio_4231_remove(unitp->playcount,
				    (apc_dma_list_t *)&dma_played_list, unitp);
				audio_gc_output(&unitp->output.as);
				goto eb2again;
			}
#endif
		} else {
			cmn_err(CE_WARN, "apc audio: NULL DMA handle!");
		}

	} /* END OF PLAY */

done:

	audio_4231_remove(unitp->playcount,
		    (apc_dma_list_t *)&dma_played_list, unitp);
	if (need_processing) {
		audio_gc_output(&unitp->output.as);
	}

	return (retval);

}

void
audio_4231_recintr(cs_unit_t *unitp)
{
	aud_cmd_t *cmdp;
	cs_stream_t *ds;
	ddi_dma_handle_t buf_dma_handle;
	ddi_dma_cookie_t buf_dma_cookie;
	ddi_acc_handle_t handle;
	u_int length;
	uint_t	ccountp;
	uchar_t tmpval;
	ulong_t ltmpval;
	int e;
	int int_active = 0;
	int lastcount = 0;

#define	Interrupt	1
#define	Active		2

	ds = &unitp->input;		/* Get cs stream pointer */

	handle = unitp->cnf_handle;
	ATRACE(audio_4231_recintr, 'LOCk', &unitp->input);

	/* General end of record condition */
	if (ds->active != B_TRUE) {
		ATRACE(audio_4231_recintr, 'RREA', &unitp->input);
		int_active |= Interrupt;
		goto done;
	}

	lastcount = (unitp->recordcount % DMA_LIST_SIZE);
	if (lastcount > 0)
		lastcount--;
	else
		lastcount = DMA_LIST_SIZE - 1;
	ATRACE(audio_4231_recintr, 'tsal', lastcount);
	ATRACE(audio_4231_recintr, ' cer', unitp->recordcount);

	cmdp = unitp->input.cmdptr;
	if (cmdp == NULL) {
		/* Were Done */
		unitp->input.error = B_TRUE;
		unitp->input.active = B_FALSE;
		unitp->input.cmdptr = NULL;
		int_active |= Interrupt;
		ATRACE(audio_4231_recintr, 'LLUN', cmdp);
		goto done;
	}
	if (cmdp == dma_recorded_list[lastcount].cmdp) {
		ATRACE(audio_4231_recintr, 'emas', cmdp);
		int_active |= Interrupt;
		if (cmdp->next != NULL) {
			cmdp = cmdp->next;
			unitp->input.cmdptr = cmdp;
			unitp->input.active = B_TRUE;
			ATRACE(audio_4231_recintr, 'dmcn', cmdp);
		} else {
			/*
			 * if the fifos have drained and there are no
			 * cmd buffers left to process, then clean up
			 * dma resources and shut down the codec
			 * XXXXX FIX ME HERE....EB2 no dmacc .
			 */
			if (!unitp->chip->dmaregs.dmacc) {
				ATRACE(audio_4231_recintr, 'dmcL', cmdp);
				unitp->input.active = B_FALSE;
				unitp->input.cmdptr = NULL;
				goto done;
			} else {
				/*
				 * if the fifo's are not empty, then wait for
				 * the next interrupt to clean up dma resources
				 */
				ATRACE(audio_4231_recintr, 'wolf', cmdp);
				return;
			}
		}
	}

	if (unitp->input.active) {
		cmdp = unitp->input.cmdptr;

		/*
		 * Ignore null and non-data buffers
		 */
		while (cmdp != NULL && (cmdp->skip || cmdp->done)) {
			cmdp->done = B_TRUE;
			cmdp = cmdp->next;


			/*
			 * if no commands available and the fifo's are
			 * not yet empty, then just wait for another interrupt
			 */
			ltmpval = ddi_getl(handle,
			    &unitp->chip->dmaregs.dmacc);
			if (!cmdp && ltmpval) {
				ATRACE(audio_4231_recintr, 'WOLF',
					unitp->input.cmdptr);
				return;
			}
			unitp->input.cmdptr = cmdp;
		}

		/*
		 * Check for flow error EOF??
		 */
		if (cmdp == NULL) {
			/* Flow error condition */
			unitp->input.error = B_TRUE;
			unitp->input.active = B_FALSE;
			unitp->input.cmdptr = NULL;
			int_active |= Interrupt;
			ATRACE(audio_4231_recintr, 'LLUN', cmdp);
			goto done;
		}

		/*
		 * Setup for DMA transfers to the buffer from the device
		 */
		length = cmdp->enddata - cmdp->data;

		if (cmdp->data == NULL || length == NULL ||
			    length > AUD_CS4231_BSIZE) {
			cmdp->skip = B_TRUE;
			goto done;
		}

		e = ddi_dma_alloc_handle(unitp->dip, &apc_dma_attr,
						DDI_DMA_DONTWAIT, NULL,
					    &buf_dma_handle);
		if (e == DDI_DMA_BADATTR) {
			cmn_err(CE_WARN,
			    "BAD_ATTR val 0x%X in record!", e);
		} else if (e != DDI_SUCCESS) {
			cmn_err(CE_WARN,
			    "DMA_ALLOC_HANDLE failed in record!");
		}

		e  = ddi_dma_addr_bind_handle(buf_dma_handle, (struct as *)0,
			    (caddr_t)cmdp->data, length,
			    DDI_DMA_READ, DDI_DMA_DONTWAIT, 0,
			    &buf_dma_cookie, &ccountp);

		switch (e) {
		case DDI_DMA_MAPPED:
			e = 0x00;
			break;
		case DDI_DMA_PARTIAL_MAP:
			cmn_err(CE_WARN,
			    "PARTIAL_MAP failed in record!");
			break;
		case DDI_DMA_NORESOURCES:
			cmn_err(CE_WARN,
			    "DMA_NORESOURCES failed in record!");
			break;
		case DDI_DMA_NOMAPPING:
			cmn_err(CE_WARN,
			    "NO_DMA_MAPPINGS failed in record!");
			break;
		case DDI_DMA_TOOBIG:
			cmn_err(CE_WARN,
			    "DMA_ALLOC_TOOBIG failed in record!");
			cmn_err(CE_WARN,
			    "Alloc of %d\n bytes attempted\n", length);
			break;
		default:
			cmn_err(CE_WARN,
			    "Unknown dma alloc failure 0x%d\n", e);
		}
		if (e) {
			return;
		}

		if (buf_dma_handle) {
			if (!unitp->eb2dma) {
				ddi_putl(handle,
				    &unitp->chip->dmaregs.dmacnva,
				    buf_dma_cookie.dmac_address);
				ddi_putl(handle,
				    &unitp->chip->dmaregs.dmacnc,
				    buf_dma_cookie.dmac_size);
			} else {
				ddi_putl(unitp->cnf_handle_eb2record,
				    EB2_REC_BCR,
				    buf_dma_cookie.dmac_size);
				ddi_putl(unitp->cnf_handle_eb2record,
				    EB2_REC_ACR,
				    buf_dma_cookie.dmac_address);
			}
			audio_4231_insert(cmdp, buf_dma_handle,
				    unitp->recordcount,
				    (apc_dma_list_t *)&dma_recorded_list,
				    unitp);
			if (unitp->recordcount < 1)
				cmdp->data = cmdp->enddata;
			unitp->recordcount++;
			int_active |= Active;
			if (cmdp->next != NULL) {
				unitp->input.cmdptr = cmdp->next;
			} else {
				cmdp->skip = B_TRUE;
			}
			unitp->typ_reclength = length;
		} else {
			cmn_err(CE_WARN, "apc audio: NULL DMA handle!");
		}

	} /* END OF RECORD */


done:

	/*
	 * If no IO is active, shut down device interrupts and
	 * dma from the dma engine.
	 */
	if ((int_active & Active)) {
		audio_4231_remove(unitp->recordcount,
		    (apc_dma_list_t *)&dma_recorded_list, unitp);
		audio_process_input(&unitp->input.as);
	} else {
		if (!unitp->eb2dma) {
			NOR_SET_LONG_R(handle,
			    APC_DMACSR, APC_CPAUSE,
			    ltmpval, APC_INTR_MASK);
		} else {
			AND_SET_LONG_R(unitp->cnf_handle_eb2record,
			    EB2_REC_CSR, ~(EB2_EN_DMA), ltmpval);
			AND_SET_LONG_R(unitp->cnf_handle_eb2record,
			    EB2_REC_CSR, ~(EB2_INT_EN), ltmpval);
		}
		audio_4231_pollpipe(unitp);
		ddi_putb(handle, CS4231_IAR, INTERFACE_CR);
		AND_SET_BYTE_R(handle, CS4231_IDR, CEN_DISABLE, tmpval);
		audio_4231_recordend(unitp, dma_recorded_list);
	}

} /* END OF RECORD */

void
audio_4231_initlist(apc_dma_list_t *dma_list, cs_unit_t *unitp)
{
	int i;


	ATRACE(audio_4231_initlist, 'EREH', dma_list);
	for (i = 0; i < DMA_LIST_SIZE; i ++) {
		if (dma_list[i].cmdp || dma_list[i].buf_dma_handle)
			ATRACE(audio_4231_initlist, 'LIST', dma_list[i].cmdp);
			audio_4231_remove(i, dma_list, unitp);
	}
	if (dma_list == dma_recorded_list) {
		unitp->recordcount = 0;
	} else {
		unitp->playcount = 0;
	}
}

void
audio_4231_insert(aud_cmd_t *cmdp, ddi_dma_handle_t buf_dma_handle,
		    uint_t count, apc_dma_list_t *dma_list, cs_unit_t *unitp)
{
	count %= DMA_LIST_SIZE;
	ATRACE(audio_4231_insert, ' tnC', count);
	if (dma_list[count].cmdp || dma_list[count].buf_dma_handle) {
		cmn_err(CE_WARN, "audio_4231_insert: dma_list not cleared!");
		ATRACE(audio_4231_insert, 'futs', count);
	}
	if (dma_list[count].cmdp == (aud_cmd_t *)NULL) {
		ATRACE(audio_4231_insert, 'mdpC', cmdp);
		dma_list[count].cmdp = cmdp;
		dma_list[count].buf_dma_handle = buf_dma_handle;
		if (dma_list == dma_recorded_list) {
			ATRACE(audio_4231_insert, ' pac', dma_list);
			unitp->recordlastent = count;
		} else
			ATRACE(audio_4231_insert, 'yalp', dma_list);
	} else {
		cmn_err(CE_WARN, "apc audio: insert dma handle failed!");
	}
}

void
audio_4231_remove(uint_t count, apc_dma_list_t *dma_list, cs_unit_t *unitp)
{

	count = ((count - 3) % DMA_LIST_SIZE);
	ATRACE(audio_4231_remove, ' tnc', count);
	if (dma_list[count].cmdp != (aud_cmd_t *)NULL) {
		if (dma_list == dma_recorded_list)  {
			dma_list[count].cmdp->data =
			    dma_list[count].cmdp->enddata;
		}
		dma_list[count].cmdp->done = B_TRUE;
		ATRACE(audio_4231_remove, 'dmcr', dma_list[count].cmdp);
		if (dma_list[count].buf_dma_handle != NULL) {
			ddi_dma_unbind_handle(dma_list[count].buf_dma_handle);
			ddi_dma_free_handle(&dma_list[count].buf_dma_handle);
		} else {
			cmn_err(CE_WARN,
				"audio_4231_remove: NULL buf_dma_handle");
		}
		dma_list[count].buf_dma_handle = NULL;
		dma_list[count].cmdp = (aud_cmd_t *)NULL;
	}
}

/*
 * Called on a stop condition to free up all of the dma queued
 */
void
audio_4231_clear(apc_dma_list_t *dma_list, cs_unit_t *unitp)
{
	int i;

	ATRACE(audio_4231_clear, 'RLCu', dma_list);
	for (i = 3; i < (DMA_LIST_SIZE + 3); i++) {
		audio_4231_remove(i, dma_list, unitp);
	}
	if (dma_list == dma_recorded_list) {
		unitp->recordcount = 0;
	} else {
		unitp->playcount = 0;
	}
}

void
audio_4231_pollready()
{
	cs_unit_t *unitp;
	ddi_acc_handle_t handle;
	uchar_t iar, idr;
	register uint_t x = 0;


	/*
	 * Use the timeout routine for the older rev parts as these
	 * codecs may spin upto 15 secs blocking all other threads
	 */
	if (!CS4231_reva) {
		audio_4231_timeout();
		return;
	}

	unitp = &cs_units[0];
	handle = unitp->cnf_handle;
	ddi_putb(handle, CS4231_IAR, (uchar_t)IAR_MCD);

	/*
	 * Wait to see if chip is out of mode change enable
	 */
	iar = ddi_getb(handle, CS4231_IAR);

	while (iar == IAR_NOTREADY && x <= CS_TIMEOUT) {

		iar = ddi_getb(handle, CS4231_IAR);
			x++;
	}

	x = 0;

	/*
	 * Wait to see if chip has done the autocalibrate
	 */
	ddi_putb(handle, CS4231_IAR, TEST_IR);

	idr = ddi_getb(handle, CS4231_IDR);

	while (idr == AUTOCAL_INPROGRESS && x <= CS_TIMEOUT) {

			idr = ddi_getb(handle,
			    CS4231_IDR);
			x++;
	}
}

void
audio_4231_samplecalc(cs_unit_t *unitp,  uint_t dma_len, uint_t direction)
{
	uint_t samples, ncount, ccount = 0;
	ddi_acc_handle_t handle;
	ulong_t ltmpval, lntmpval = 0;

	/* 1 is recording, 0 is playing XXX Better way??? */

	handle = unitp->cnf_handle;
	ATRACE(audio_4231_samplecalc, 'HERE', unitp);
	if (direction) {
		dma_len = audio_4231_sampleconv(&unitp->input, dma_len);
		samples = unitp->input.samples;
		if (!unitp->eb2dma) {
			ltmpval = ddi_getl(handle,
			    &unitp->chip->dmaregs.dmacc);
			lntmpval = ddi_getl(handle,
			    &unitp->chip->dmaregs.dmacnc);
		} else {
			ltmpval = ddi_getl(unitp->cnf_handle_eb2record,
			    EB2_REC_BCR);
			/*
			 *  XXX eb2 FIX_ME what is the next counter
			 * The next counter on Eb2 is not readable
			 * if DMA_CHAINING is on, therefore we must
			 * subtract the buffer.size if record and
			 * AUD_4231_BSIZE if play ??
			 */
			lntmpval = unitp->input.as.info.buffer_size;
		}
		ncount = audio_4231_sampleconv(&unitp->input, lntmpval);
		ccount = audio_4231_sampleconv(&unitp->input, ltmpval);
		if (ccount != 0) {
			unitp->input.samples =
			    ((samples - ncount) + (dma_len - ccount));
		}
	} else {
		dma_len = audio_4231_sampleconv(&unitp->output, dma_len);
		samples = unitp->output.samples;
		if (!unitp->eb2dma) {
			ltmpval = ddi_getl(handle,
			    &unitp->chip->dmaregs.dmapc);
			lntmpval = ddi_getl(handle,
			    &unitp->chip->dmaregs.dmapnc);
		} else {
			ltmpval = ddi_getl(unitp->cnf_handle_eb2play,
			    EB2_PLAY_BCR);

			lntmpval = audio_4231_bsize;
		}
		ncount = audio_4231_sampleconv(&unitp->output, lntmpval);
		ccount = audio_4231_sampleconv(&unitp->output, ltmpval);
		if (ccount != 0) {
			unitp->output.samples =
			    ((samples - ncount) + (dma_len - ccount));
		}
	}
}

/*
 * Converts byte counts to sample counts
 */
uint_t
audio_4231_sampleconv(cs_stream_t *stream, uint_t length)
{

	uint_t samples;

	if (stream->as.info.channels == 2) {
		samples = (length/2);
	} else {
		samples = length;
	}
	if (stream->as.info.encoding == AUDIO_ENCODING_LINEAR) {
			samples = samples/2;
	}

	return (samples);
}

/*
 * This routine is used to adjust the ending record cmdp->data
 * since it is set in the intr routine we need to look it up
 * in the circular buffer, mark it as done and adjust the
 * cmdp->data point based on the current count in the capture
 * count. Once this is done call audio_process_input() and
 * also call audio_4231_clear() to free up all of the dma_handles.
 */
void
audio_4231_recordend(cs_unit_t *unitp, apc_dma_list_t *dma_list)
{

	uint_t	count, capcount, ncapcount, recend;
	int i;
	ddi_acc_handle_t handle;

	handle = unitp->cnf_handle;
	count = unitp->recordlastent;
	ATRACE(audio_4231_recordend, 'STAL', count);
	if (!unitp->eb2dma) {
		capcount = ddi_getl(handle,
			    &unitp->chip->dmaregs.dmacc);
		ncapcount = ddi_getl(handle,
			    &unitp->chip->dmaregs.dmacnc);
	} else {
		capcount = ddi_getl(unitp->cnf_handle_eb2record,
			    EB2_REC_BCR);
		ncapcount = ddi_getl(unitp->cnf_handle_eb2record,
			    EB2_REC_BCR);
		/* eb2 FIX_ME */
	}

	recend =  capcount;

	if (count > 0 && count < DMA_LIST_SIZE)
		count--;
	else if (unitp->recordlastent == unitp->recordcount)
		count = 0;
	else
		count = DMA_LIST_SIZE - 1;

	if (dma_list[count].cmdp != (aud_cmd_t *)NULL) {
		dma_list[count].cmdp->data =
		    (dma_list[count].cmdp->enddata - recend);
		dma_list[count].cmdp->done = B_TRUE;
		if (count != 0) {
			audio_4231_initcmdp(dma_list[count].cmdp,
			    unitp->input.as.info.encoding);
		}
	}


	for (i = 0; i < DMA_LIST_SIZE; i++) {
		if (dma_list[i].cmdp) {
			if (!dma_list[i].buf_dma_handle)
				cmn_err(CE_WARN,
				"audio_4231_recordend: NULL buf_dma_handle");
			/*
			 * mark all freed buffers as done
			 */
			dma_list[i].cmdp->done = B_TRUE;
			ATRACE(audio_4231_recordend, ' tnc', i);
			ATRACE(audio_4231_recordend, 'pdmc',
				dma_list[i].cmdp);
			if (dma_list[i].buf_dma_handle != NULL) {
				ddi_dma_unbind_handle(dma_list[i].
				    buf_dma_handle);
				ddi_dma_free_handle(&dma_list[i].
				    buf_dma_handle);
			} else {
				cmn_err(CE_WARN,
				    "audio_4231_remove: NULL buf_dma_handle");
			}
			dma_list[i].cmdp = (aud_cmd_t *)NULL;
			dma_list[i].buf_dma_handle = NULL;
		}
	}

	unitp->recordcount = 0;
	/*
	 * look for more buffers to process
	 */
	audio_process_input(&unitp->input.as);
}

/*
 * XXXXX this is a way gross hack to prevent the static from
 * being played at the end of the record. Until the *real*
 * cause can be found this will at least silence the
 * extra data.
 */
void
audio_4231_initcmdp(aud_cmd_t *cmdp, uint_t format)
{

	uint_t zerosample = 0;

	switch (format) {
	case AUDIO_ENCODING_ULAW:
		zerosample = 0xff; /* silence for ulaw format */
		break;
	case AUDIO_ENCODING_ALAW:
		zerosample = 0xd5;	/* zerosample for alaw */
		break;
	case AUDIO_ENCODING_LINEAR:
		zerosample = 0x00;	/* zerosample for linear */
		break;
	}
	ATRACE(audio_4231_initcmdp, 'PDMC', cmdp);
	ATRACE(audio_4231_initcmdp, 'TAMF', format);

	for (; cmdp->data < cmdp->enddata; ) {
		*cmdp->data++ = zerosample;
	}
}

void
audio_4231_pollpipe(cs_unit_t *unitp)
{

	ddi_acc_handle_t handle;
	ulong_t ltmpval, dmacsr;
	int x = 0;

	handle = unitp->cnf_handle;
	dmacsr = ddi_getl(handle, APC_DMACSR);

	while (!(dmacsr & APC_CM) && x <= CS_TIMEOUT) {
		dmacsr = ddi_getl(handle, APC_DMACSR);
		x++;
	}

}
void
audio_4231_workaround(cs_unit_t *unitp)
{

	ddi_acc_handle_t handle;
	uchar_t tmpval;

	handle = unitp->cnf_handle;
	/*
	 * This workaround is so that the 4231 will run a logical
	 * zero sample through the DAC when playback is disabled.
	 * Otherwise there can be a "zipper" noise when adjusting
	 * the play gain at idle.
	 */
	if (audio_4231_acal) {
		/*
		 * turn off auto-calibrate before
		 * running the zero sample thru
		 */
		ddi_putb(handle,
		    CS4231_IAR, (uchar_t)(IAR_MCE | INTERFACE_CR));
		AND_SET_BYTE_R(handle, CS4231_IDR, ACAL_DISABLE, tmpval);
		audio_4231_pollready();
	}

	if (!CS4231_reva) {
		ddi_putb(handle, CS4231_IAR, IAR_MCE);
		drv_usecwait(100);
		ddi_putb(handle, CS4231_IAR, (uchar_t)IAR_MCD);
		drv_usecwait(100);
		audio_4231_pollready();
		drv_usecwait(1000);
	}


	if (audio_4231_acal) {
		/*
		 * re-enable the auto-calibrate
		 */
		ddi_putb(handle,
		    CS4231_IAR, (uchar_t)(IAR_MCE | INTERFACE_CR));
		OR_SET_BYTE_R(handle, CS4231_IDR, CHIP_INACTIVE, tmpval);
		audio_4231_pollready();
	}
}

void
audio_4231_eb2cycpend(caddr_t eb2csr)
{

	int x = 0;

	while ((*eb2csr & EB2_CYC_PENDING) && (x <= CS_TIMEOUT)) {
		x++;
	}
}

/*
 * audio_4231_config_queue - Set the high and low water marks for a queue
 *
 */
void
audio_4231_config_queue(aud_stream_t *as)
{
	long hiwater, lowater;
	long onesec;

	ASSERT(as != NULL);
	ASSERT_ASLOCKED(as);

	/*
	 * Configure an output stream
	 */
	if (as == as->output_as) {
		/*
		 * If the write queue is not open, then just return
		 */
		if (as->writeq == NULL)
			return;

		onesec = (as->info.sample_rate * as->info.channels *
		    as->info.precision) / 8;

		hiwater = onesec * 3;
		hiwater = MIN(hiwater, 80000);
		lowater = hiwater * 2 / 3;

		/*
		 * Set the play stream hi and lowater marks based
		 * upon tunable variables in /etc/system is they
		 * have been set. For the future these should
		 * be ioctls().
		 */


		if (audio_4231_play_hiwater != 0) {
			hiwater = audio_4231_play_hiwater;
		}

		if (audio_4231_play_lowater != 0) {
			lowater = audio_4231_play_lowater;
		}

		/*
		 * Tweak the high and low water marks based on throughput
		 * expectations.
		 */
		freezestr(as->writeq);
		strqset(as->writeq, QHIWAT, 0, hiwater);
		strqset(as->writeq, QLOWAT, 0, lowater);
		unfreezestr(as->writeq);
	}
}

/*
 * Use the timeout routine to allow other threads to run if the chip
 * is doing a long auto-calibrate. This is a workaround for the CS4231
 * which can spin for CS_TIMEOUT ~ 15 secs. This is fixed in the
 * CS4231A part.
 */
static void
audio_4231_timeout()
{
	cs_unit_t *unitp;
	register uint_t x = 0;
	static	int	timeout_count = 0;
	ddi_acc_handle_t handle;

	unitp = &cs_units[0];
	handle = unitp->cnf_handle;
	ddi_putb(handle,
	    &unitp->chip->pioregs.iar, (u_char)IAR_MCD);

	/*
	 * Wait to see if chip is out of mode change enable
	 */
	while ((ddi_getb(handle, CS4231_IAR)) == IAR_NOTREADY &&
			x <= CS_TIMEOUT) {
		x++;
	}

	/*
	 * Wait to see if chip has done the autocalibrate
	 */
	ddi_putb(handle,
	    &unitp->chip->pioregs.iar, TEST_IR);
	if ((ddi_getb(handle, CS4231_IDR) == AUTOCAL_INPROGRESS)) {
		if (++timeout_count < CS_TIMEOUT)
			timeout(audio_4231_timeout, (caddr_t)NULL, 100);
		else {
			timeout_count = 0;
			cmn_err(CE_WARN,
				"audio_4231_timeout: codec not ready\n");
		}
	} else
		timeout_count = 0;
}

void
audio_4231_pollppipe(cs_unit_t *unitp)
{

	ulong_t ltmpval, dmacsr;
	ddi_acc_handle_t handle;
	int x = 0;

	handle = unitp->cnf_handle;

	dmacsr = ddi_getl(handle, APC_DMACSR);
	while (!(dmacsr & APC_PM) && x <= CS_TIMEOUT) {
		dmacsr = ddi_getl(handle, APC_DMACSR);
		x++;
	}

}
