/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ident "@(#)pcmcia.c	1.63	95/08/15 SMI"

/*
 * PCMCIA NEXUS
 *	The PCMCIA nexus is a pseudo-driver which presents an
 *	idealized adapter interface to the rest of the system.
 *	As far as users of the nexus are concerned, there are <n>
 *	logical sockets in the system and a single physical
 *	adapter.
 *
 *	This driver will become the core of the pcmcia "misc"
 *	module.
 *
 *	The nexus will attempt to load all possible PCMCIA
 *	adapter specific drivers and build a model of the
 *	ideal adapter.
 *
 *	The nexus also exports events to an event manager
 *	driver if it has registered.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/buf.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/conf.h>
#include <sys/stat.h>
#include <sys/autoconf.h>
#include <sys/vtoc.h>
#include <sys/dkio.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/kstat.h>
#include <sys/kmem.h>
#include <sys/modctl.h>
#include <sys/kobj.h>

#include <sys/pctypes.h>
#include <sys/pcmcia.h>
#include <sys/sservice.h>
#include <pcmcia/sys/cs_types.h>
#include <pcmcia/sys/cis.h>
#include <pcmcia/sys/cis_handlers.h>
#include <pcmcia/sys/cs.h>

int pcmcia_identify(dev_info_t *);
int pcmcia_attach(dev_info_t *, ddi_attach_cmd_t);
int pcmcia_detach(dev_info_t *, ddi_detach_cmd_t);
ddi_intrspec_t pcmcia_get_intrspec(dev_info_t *, dev_info_t *, u_int);
int pcmcia_ctlops(dev_info_t *, dev_info_t *, ddi_ctl_enum_t, void *, void *);
int pcmcia_prop_op(dev_t, dev_info_t *, dev_info_t *, ddi_prop_op_t,
			int, char *, caddr_t, int *);

/*
 * note that the presence of this function is to work around
 * an ugly kludge imposed by the way tactical engineering chose
 * to resolve a SCSA bug (or not resolve as the case may be)
 *
 * DMA is NOT implemented
 */
int pcmcia_dma_mctl(dev_info_t *, dev_info_t *, ddi_dma_handle_t,
			enum ddi_dma_ctlops, off_t *, u_int *, caddr_t *, u_int);

static
struct bus_ops pcmciabus_ops = {
#if defined(BUSO_REV) && BUSO_REV >= 2
	BUSO_REV,			/* XXX */
	i_ddi_bus_map,
	pcmcia_get_intrspec,
	i_ddi_add_intrspec,
	i_ddi_remove_intrspec,
	i_ddi_map_fault,
	ddi_no_dma_map,
	ddi_no_dma_allochdl,
	ddi_no_dma_freehdl,
	ddi_no_dma_bindhdl,
	ddi_no_dma_unbindhdl,
	ddi_no_dma_flush,
	ddi_no_dma_win,
	pcmcia_dma_mctl,
	pcmcia_ctlops,
	pcmcia_prop_op
#else
	i_ddi_bus_map,
	pcmcia_get_intrspec,
	i_ddi_add_intrspec,
	i_ddi_remove_intrspec,
	i_ddi_map_fault,
	ddi_no_dma_map,
	pcmcia_dma_mctl,
	pcmcia_ctlops,
	pcmcia_prop_op
#endif
};

static struct dev_ops pcmciadevops = {
	DEVO_REV,
	0,
	ddi_no_info,
	pcmcia_identify,
	nulldev,
	pcmcia_attach,
	pcmcia_detach,
	nodev,
	(struct cb_ops *)NULL,
	&pcmciabus_ops
};

struct pcmcia_adapter pcmcia_adapters[PCMCIA_MAX_ADAPTERS];
int    pcmcia_num_adapters;
pcmcia_logical_socket_t pcmcia_sockets[PCMCIA_MAX_SOCKETS];
int    pcmcia_num_sockets;
pcmcia_logical_window_t pcmcia_windows[PCMCIA_MAX_WINDOWS];
int    pcmcia_num_windows;
struct power_entry pcmcia_power_table[PCMCIA_MAX_POWER];
int	pcmcia_num_power;

struct pcmcia_mif *pcmcia_mif_handlers = NULL;
pcm_dev_node_t *pcmcia_devnodes = NULL;

char pcm_default_drv[MODMAXNAMELEN] = "pcmem";
char pcm_unknown_drv[MODMAXNAMELEN] = "*unknown*";

kmutex_t pcmcia_global_lock;

static int (*pcmcia_card_services)(int, ...) = NULL;

/*
 * Map of device types from Function ID tuple
 * to strings.
 */
static char *pcmcia_def_dev_map[] = {
	"multi",
	"memory",
	"serial",
	"parallel",
	"ata",
	"video",
	"lan",
	"scsi"
};

/*
 * Mapping of the device "type" to names acceptable to
 * the DDI
 */
static char *pcmcia_dev_type[] = {
	"multifunction",
	"byte",
	"serial",
	"parallel",
	"block",
	"display",
	"network",
	"block"
};

/*
 * Device Type tuple mappings to device type category
 * These are frequently confusing so aren't used except
 * when a memory card has a CIS and we need to distinguish
 * further and sometimes for the rare I/O card.
 */
static char *pcmcia_devtuple_type[] = {
	"null",
	"memory",		/* ROM */
	"memory",		/* OTPROM */
	"memory",		/* EPROM */
	"memory",		/* EEPROM */
	"memory",		/* FLASH */
	"memory",		/* SRAM */
	"memory",		/* DRAM */
	"unknown",
	"unknown",
	"unknown",
	"unknown",
	"unknown",
	"funcspec",		/* some I/O device */
	"extended"
};

/*
 * PCMCIA Device <-> Driver name construction and search order patterns.
 * This table defines the patterns used to construct names and the
 * search order.  The zeroth entry is done first and will go until
 * the first alias is found or until all the patterns are exhausted.
 * Room is left to allow adding patterns dynamically in the future if
 * this becomes necessary.
 */
u_int pcm_search_pat[16] = {
	PCMD_FUNCID | PCMD_FUNCE | PCMD_MANFID | PCMD_VERS1,
	PCMD_FUNCID | PCMD_MANFID | PCMD_VERS1,
	PCMD_FUNCID | PCMD_MANFID,
	PCMD_FUNCID | PCMD_VERS1,
	PCMD_MANFID | PCMD_VERS1,
	PCMD_MANFID,
	PCMD_VERS1,
	PCMD_FUNCID | PCMD_FUNCE,
	PCMD_FUNCID,
		/*
		 * need to think this out better
		 * since it only makes sense for RAM
		 * cards with CIS and those few I/O
		 * cards that don't claim to be memory
		 */
	PCMD_DEVTYPE
};
int pcm_num_search = 10;

#ifdef PCMCIA_DEBUG
int pcmcia_debug = 0;
extern void pcm_prints(char *);
extern void pcmprintf(char *, void *, void *, void *,
			void *, void *, void *, void *, void *, void *);
#endif
#ifdef	PCMCIA_NO_CARDSERVICES
static int (*pcmcia_socket_services)(int, ...) = NULL;
#endif
static f_tt *pcmcia_cs_event = NULL;
static int pcmcia_cs_module;
static dev_info_t *pcmcia_dip;

/*
 * XXX - See comments in cs.c
 */
static f_tt *pcmcia_cis_parser = NULL;

#if defined(i86)
/* not currently used here but will need to be when nexus is restructured */
struct pcmcia_resources pcmcia_resources;
#endif

extern struct pc_socket_services pc_socket_services;

/* some function declarations */
extern pcm_adapter_callback(dev_info_t *, int, int, int);
extern int pcm_identify_device(int, struct pcm_device_info *, char *, char *);
extern void pcmcia_init_adapter(char *, int, struct dev_ops *);
extern void pcmcia_merge_power(struct power_entry *);
extern void pcmcia_do_resume(int, pcmcia_logical_socket_t *);
extern void pcmcia_resume(int, pcmcia_logical_socket_t *);
extern void pcmcia_do_suspend(int, pcmcia_logical_socket_t *);
extern void pcm_event_manager(int, int, void *);
extern void pcmcia_init_socket();
extern void pcmcia_create_dev_info(dev_info_t *, int);
extern pcmcia_create_device(dev_info_t *, ss_make_device_node_t *);
static void pcmcia_init_devinfo(dev_info_t *,
				char *, struct pcm_device_info *, char *);
static void pcm_patch_devinfo(dev_info_t *);
#if defined(i86) || defined(__ppc)
static void pcmcia_x86_specific(dev_info_t *);
#endif

/* Card&Socket Services entry points */
static int GetCookiesAndDip(sservice_t *);
static int SSGetAdapter(get_adapter_t *);
static int SSGetPage(get_page_t *);
static int SSGetSocket(get_socket_t *);
static int SSGetStatus(get_ss_status_t *);
static int SSGetWindow(get_window_t *);
static int SSInquireAdapter(inquire_adapter_t *);
static int SSInquireSocket(inquire_socket_t *);
static int SSInquireWindow(inquire_window_t *);
static int SSResetSocket(int, int);
static int SSSetPage(set_page_t *);
static int SSSetSocket(set_socket_t *);
static int SSSetWindow(set_window_t *);
static int SSSetIRQHandler(set_irq_handler_t *);
static int SSClearIRQHandler(clear_irq_handler_t *);

/* Undocumented DDI functions */
void mod_rele_dev_by_devi(dev_info_t *);

/*
 * This is the loadable module wrapper.
 * It is essentially boilerplate so isn't documented
 */

extern struct mod_ops mod_driverops;

static struct modldrv modldrv = {
	&mod_driverops,		/* Type of module. This one is a driver */
	"PCMCIA nexus driver",	/* Name of the module. */
	&pcmciadevops,		/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};

int
_init()
{
	mutex_init(&pcmcia_global_lock, "PCMCIA nexus lock",
			MUTEX_DEFAULT, NULL);
	return (mod_install(&modlinkage));
}

int
_fini()
{
	return (mod_remove(&modlinkage));
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

int
pcmcia_identify(dev_info_t *devi)
{
	char *dname = ddi_get_name(devi);

	if (strcmp(dname, "pcmcia") == 0)
		return (DDI_IDENTIFIED);
	else
		return (DDI_NOT_IDENTIFIED);
}

/*
 * pcmcia_attach()
 *	the attach routine must make sure that everything needed is present
 *	including real hardware.  The sequence of events is:
 *		attempt to load all adapter drivers
 *		attempt to load Card Services (which _depends_on pcmcia)
 *		initialize logical sockets
 *		report the nexus exists
 */

int
pcmcia_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	char *adapter_list, *cp, adapter [MODMAXNAMELEN+1], *dp, *csname;
	int proplen, i, cslen;
	int module;


	/*
	 * resume from a checkpoint
	 */
	if (cmd == DDI_RESUME) {
		return (DDI_SUCCESS);
	}

	pcmcia_dip = dip;	/* save for future */
	/*
	 * get the list of adapters to check for.  It is possible
	 * to have all platforms in the list since all are attempted
	 * to be loaded and only the ones found (that load) are used
	 */
	if (ddi_getlongprop(DDI_DEV_T_NONE, dip, DDI_PROP_DONTPASS,
				ADAPT_PROP, (caddr_t)&adapter_list, &proplen) !=
		DDI_PROP_SUCCESS) {
		cmn_err(CE_WARN, "pcmcia: no adapter property!");
		return (DDI_FAILURE);
	}

	/*
	 * we allow an override to the default Card Services
	 * module.  If the property is present, use that name
	 */
	if (ddi_getlongprop(DDI_DEV_T_NONE, dip, DDI_PROP_DONTPASS,
				CS_PROP, (caddr_t)&csname, &cslen) !=
		DDI_PROP_SUCCESS) {
		csname = DEFAULT_CS_NAME;
		cslen = 0;
	}

	i = MODMAXNAMELEN;
	(void) ddi_getlongprop_buf(DDI_DEV_T_NONE, dip, DDI_PROP_DONTPASS,
					DEF_DRV_PROP, pcm_default_drv,
					&i);
	(void) ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
				PCM_DEVICETYPE, "pcmcia", sizeof ("pcmcia"));

	for (cp = adapter_list, dp = adapter, i = 0; cp != NULL; ) {
		/*
		 * step through the list of names and do a modload on each one.
		 * names are separated either by a space ' '
		 */
		if (i < MODMAXNAMELEN && *cp != ' ' && *cp != '\0') {
			*dp++ = *cp++;
			i++;
		} else {
			if (*cp == '\0') {
				cp = NULL;
			} else
				cp++;	/* step past ',' or ' ' */

			*dp = '\0';

			/* we have a module name, does it exist? */

			module = modload("drv", adapter);
#if defined(PCMCIA_DEBUG)
			if (pcmcia_debug)
				printf("\tmodule name: %s module id: %x\n",
						adapter, module);
#endif

			if (module != -1) {
				major_t major;
				struct dev_ops *ops;

				/*
				 * we have a loaded module
				 * find its major device number and
				 * force it to be installed and held
				 */
				major = ddi_name_to_major(adapter);
				ops = ddi_hold_installed_driver(major);
				if (ops == NULL) {
					modunload(module);
				} else {
					/*
					 * find out details and setup
					 * the logical sockets
					 */
					pcmcia_init_adapter(adapter,
								module, ops);
				}
			}
			dp = adapter;
			i = 0;
		}
	}
	/* don't need the properties anymore so free them */
	kmem_free(adapter_list, proplen);
	if (cslen != 0)
		kmem_free(csname, cslen);

	if (pcmcia_num_adapters == 0) {
		/* if no adapters present, fail all the way around */
		cmn_err(CE_CONT, "?pcmcia: no PCMCIA adapters found\n");
		return (DDI_FAILURE);
	}

	/*
	 * load Card Services after the adapter drivers.  We may
	 * have to unload the adapter drivers if CS isn't found
	 * This changes when we restructure.
	 */

	pcmcia_cs_module = modload("misc", "cs");

	if (pcmcia_cs_module != -1 || pcmcia_card_services != NULL) {
		/*
		 * we need to load it and lock it in place.  We also want
		 * to have the devinfo for it set with the entry point for
		 * socket services.
		 */
		i = 0;
	} else {
		/*
		 * unload the adapter drivers since CS isn't present
		 */
		for (i = 0; i < pcmcia_num_adapters; i++) {
			major_t major, old = 0;

			major = ddi_name_to_major(pcmcia_adapters[i].pca_name);
			if ((int) major == -1 || major == old)
				continue;
			ddi_rele_driver(major);
			modunload(pcmcia_adapters[i].pca_module);
			old = major;
		}
		/*
		 * unload the Card Services module if it was loaded
		 */
		if (pcmcia_cs_module != -1)
			modunload(pcmcia_cs_module);

		cmn_err(CE_WARN, "pcmcia: no CS module!\n");
		return (DDI_FAILURE);
	}

	ddi_report_dev(dip);	/* directory/device naming */

#if defined(i86)
	/*
	 * x86 specific initializations
	 *
	 * for interim versions, this is where we look at the
	 * properties for "reserved" resources.
	 * x86 needs to have I/O addresses, IRQ levels and memory
	 * reserved for PC Card drivers.
	 */
	pcmcia_x86_specific(dip);
#endif

	return (DDI_SUCCESS);
}

/*
 * pcmcia_detach
 *	unload everything and then detach the nexus
 */
/* ARGSUSED */
int
pcmcia_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	int i = DDI_FAILURE;

	switch (cmd) {
	case DDI_DETACH:
				/* need to unload any adapter drivers */
				/* really need to check error return */
#ifdef	XXX
		for (i = 0; i < pcmcia_num_adapters; i++)
			modunload(pcmcia_adapters[i].pca_module);
		i = modunload(pcmcia_cs_module);
#endif
		return (i);

	/*
	 * resume from a checkpoint
	 * We don't do anything special here since the adapter
	 * driver will generate resume events that we intercept
	 * and convert to insert events.
	 */
	case DDI_SUSPEND:
		return (DDI_SUCCESS);

	default:
		return (DDI_FAILURE);
	}
}

/*
 * pcmcia_ctlops
 *	handle the nexus control operations for the cases where
 *	a PC Card driver gets called and we need to modify the
 *	devinfo structure or otherwise do bus specific operations
 */
int
pcmcia_ctlops(dev_info_t *dip, dev_info_t *rdip,
	ddi_ctl_enum_t ctlop, void *arg, void *result)
{
	int e;
	char name[64];
	struct ddi_parent_private_data *ppd;
#if defined(PCMCIA_DEBUG)
	if (pcmcia_debug) {
		printf("pcmciactlops(%x, %x, %x, %x, %x)\n",
			(int)dip, (int)rdip, ctlop, (int) arg, (int) result);
		if (rdip != NULL && ddi_get_name(rdip) != NULL)
			printf("\t[%s]\n", ddi_get_name(rdip));
	}
#endif

	switch (ctlop) {
	case DDI_CTLOPS_REPORTDEV:
		if (rdip == (dev_info_t *)0)
			return (DDI_FAILURE);

		cmn_err(CE_CONT, "?%s%d at %s in socket %d\n",
			ddi_get_name(rdip),
			ddi_get_instance(rdip),
			ddi_get_name(dip),
			ddi_getprop(DDI_DEV_T_NONE, rdip,
				    DDI_PROP_DONTPASS, PCM_DEV_SOCKET, -1));

		return (DDI_SUCCESS);

	case DDI_CTLOPS_INITCHILD:
		/*
		 * we get control here before the child is called.
		 * we can change things if necessary.  This is where
		 * the CardServices hook gets planted.
		 */
#if defined(PCMCIA_DEBUG)
		if (pcmcia_debug)
			printf("pcmcia: init child: %s(%d) \n",
				ddi_get_name(arg), ddi_get_instance(arg));
#endif

		/*
		 * We don't want driver.conf files that stay in
		 * pseudo device form.  It is acceptable to have
		 * .conf files add properties only.
		 */
		if (DEVI(arg)->devi_nodeid == DEVI_PSEUDO_NODEID) {
			cmn_err(CE_WARN, "%s%d: %s.conf invalid",
				ddi_get_name((dev_info_t *)arg),
				ddi_get_instance((dev_info_t *)arg),
				ddi_get_name((dev_info_t *)arg));
			return (DDI_FAILURE);
		}
		if (ddi_getprop(DDI_DEV_T_NONE, (dev_info_t *) arg,
				DDI_PROP_DONTPASS, PCM_DEV_SOCKET,
				-1) == -1) {
			pcm_patch_devinfo((dev_info_t *)arg);
		}
		if (ddi_getprop(DDI_DEV_T_NONE, (dev_info_t *) arg,
				DDI_PROP_DONTPASS, CS_PROP, NULL) == NULL) {
#if defined(PCMCIA_DEBUG)
			if (pcmcia_debug)
				printf("\tno card-services so create it\n");
#endif
			ddi_prop_create(DDI_DEV_T_NONE, (dev_info_t *)arg,
					DDI_PROP_CANSLEEP, CS_PROP,
					(caddr_t)&pcmcia_card_services,
					sizeof (pcmcia_card_services));
		}

		/*
		 * make sure names are relative to socket number
		 */
		sprintf(name, "%d", ddi_getprop(DDI_DEV_T_NONE,
						(dev_info_t *) arg,
						DDI_PROP_CANSLEEP,
						PCM_DEV_SOCKET, -1));
		ddi_set_name_addr((dev_info_t *) arg, name);

		e = DDI_SUCCESS;

#if defined(PCMCIA_DEBUG)
		if (pcmcia_debug)
			printf("pcmcia: system init done for %s [%s] "
				"nodeid: %x\n",
				ddi_get_name(arg), ddi_get_name_addr(arg),
				DEVI(arg)->devi_nodeid);
#endif

		return (e);

	case DDI_CTLOPS_UNINITCHILD:
		ddi_set_name_addr((dev_info_t *) arg, NULL);
		ddi_remove_minor_node(dip, NULL);
		return (DDI_SUCCESS);

	case DDI_CTLOPS_SLAVEONLY:
		/* PCMCIA devices can't ever be busmaster until CardBus */
		return (DDI_SUCCESS);

	case DDI_CTLOPS_SIDDEV:
		/* in general this is true. */
		return (DDI_SUCCESS);

	case DDI_CTLOPS_POWER:
		/* let CardServices know about this */

	default:
		/* most things default to general ops */
		return (ddi_ctlops(dip, rdip, ctlop, arg, result));
	}
}

/*
 * pcmcia_dma_mctl()
 *	DMA control functions
 *
 *	Note that in PCMCIA R2.1, there is no DMA and the existing
 *	hardware doesn't support it.  In PC Card 3.0, DMA has been
 *	defined but 3.0 hardware doesn't really exist yet.
 *	This function was added solely to support broken SCSA
 *	and a driver that uses SCSA.  By the time SCSA gets fixed,
 *	DMA will likey need to be supported since new hardware is
 *	on the way.  At that time, more than just the IOPB functions
 *	will be needed.
 */
int
pcmcia_dma_mctl(dev_info_t *dip, dev_info_t *rdip,
    ddi_dma_handle_t handle, enum ddi_dma_ctlops request,
    off_t *limp, u_int *lenp, caddr_t *objp, u_int flags)
{
        switch (request) {
		case DDI_DMA_IOPB_ALLOC: {
      			auto ddi_dma_lim_t defalt = {
				(u_long)0,
#if defined(i386)
				(u_long)0xffffff,
				(u_int)0,
				(u_int)0x00000001,
				(u_int)DMA_UNIT_8,
				(u_int)0,
				(u_int)0x86<<24+0,
				(u_int)0xffff,
				(u_int)0xffff,
				(u_int)512,
				(int)1,
				(u_int)0xffffffff
#endif
			};

        		if (!limp)
                		limp = (off_t *)&defalt;

#if defined(BUSO_REV) && BUSO_REV >= 2
        		return (i_ddi_mem_alloc(dip, (ddi_dma_lim_t *)limp,
					(u_int)lenp, 0, 0, NULL, objp,
					NULL, NULL));
#else
        		return (i_ddi_mem_alloc(dip, (ddi_dma_lim_t *)limp,
					(u_int)lenp, 0, 0, objp, (u_int *)0));
#endif
		}
		case DDI_DMA_IOPB_FREE:
			i_ddi_mem_free((caddr_t)objp, 0);
			return (DDI_SUCCESS);

		case DDI_DMA_SMEM_ALLOC:
		case DDI_DMA_SMEM_FREE:
		default:
			return (DDI_FAILURE);
	}
}

/*
 * pcmcia_prop_op()
 *	we don't have properties in PROM per se so look for them
 * 	only in the devinfo node.  Future may allow us to find
 *	certain CIS tuples via this interface if a user asks for
 *	a property of the form "cistpl-<tuplename>" but not yet.
 */
int
pcmcia_prop_op(dev_t dev, dev_info_t *dip, dev_info_t *ch_dip,
    ddi_prop_op_t prop_op, int mod_flags,
    char *name, caddr_t valuep, int *lengthp)
{
	return (ddi_bus_prop_op(dev, dip, ch_dip, prop_op,
				mod_flags | DDI_PROP_NOTPROM,
				name, valuep, lengthp));
}

/*
 * pcmcia_get_intrspec()
 *	We don't want to use the rootnex version so we always
 *	return NULL to avoid panics. In the future, this could
 *	provide real information.
 */
/* ARGSUSED */
ddi_intrspec_t
pcmcia_get_intrspec(dev_info_t *dip, dev_info_t *rdip, u_int index)
{
	return (NULL);
}


/*
 * pcmcia_init_adapter
 *	step through each adapter found and get the dev_info_t
 *	and then setup the private interface for later use.
 *	we also determine the characteristics of the adapter.
 *	Once this is done, setup the "logical socket" mapping
 */
void
pcmcia_init_adapter(char *name, int module, struct dev_ops *ops)
{
	int i, unit, n, base;
	dev_info_t *dip;
	pcmcia_if_t *ls_if;
	struct pcmcia_adapter_nexus_private *priv;
	int major;

	/*
	 * we can't be sure that the module name we were given is the
	 * real module name, it may just be the driver name.  We
	 * can find the real name by finding the module info itself
	 * and using that name for the lookup if it is different
	 * E.g. The stp4020 driver is device node SUNW,pcmcia.
	 */

	major = ddi_name_to_major(name);
	if (major > 0) {
		for (i = 0, dip = NULL; dip == NULL && i < PCMCIA_MAX_ADAPTERS;
			i++) {
			dip = dev_get_dev_info(makedevice(major, i), VCHR);
		}
	}
	if (major < 0 || dip == NULL) {
		/* is there any thing to do in this case??? */
		dip = NULL;
	} else {
#if defined(PCMCIA_DEBUG)
		if (pcmcia_debug)
			printf("pcmcia_init_adapter: name: %s, devi_name: %s\n",
				name, DEVI(dip)->devi_name);
#endif
		name = DEVI(dip)->devi_name;
	}

	base = pcmcia_num_adapters;
	for (unit = 0, i = pcmcia_num_adapters;
		i < PCMCIA_MAX_ADAPTERS &&
		unit < PCMCIA_MAX_ADAPTERS; unit++) {
		dip = ddi_find_devinfo(name, unit, 1);
#if defined(PCMCIA_DEBUG)
		if (pcmcia_debug)
			printf("pcmcia_init_adapter: dip=%x (%s:%d)\n",
				(int)dip, name, unit);
#endif
		if (dip != NULL) {
			/* since we required a driver, it must be attached */
			priv = (struct pcmcia_adapter_nexus_private *)
				ddi_get_driver_private(dip);
			if (priv == NULL) {
				cmn_err(CE_WARN, "pcmcia: bad driver: %s",
					name);
				return;
			}

			/*
			 * we now build up a map of the adapters and
			 * sockets available in order to implement the
			 * Card Services view of the system.  All
			 * necessary information is maintained in
			 * the per adapter and per socket structures.
			 */

			i = pcmcia_num_adapters++;
			strcpy(pcmcia_adapters[i].pca_name, name);
			pcmcia_adapters[i].pca_ops = ops;
			pcmcia_adapters[i].pca_dip = dip;
			pcmcia_adapters[i].pca_unit = i - base;
			pcmcia_adapters[i].pca_module = module;
			ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
						"adapter", (caddr_t)&i,
						sizeof (int));
#if defined(PCMCIA_DEBUG)
			if (pcmcia_debug) {
				printf("\t%d pcmcia adapters found so far\n",
					pcmcia_num_adapters);
				printf("\tprivate data area at %x\n",
								(int)priv);
				printf("\tdip=%x and save dip=%x\n",
					(int)dip, (int)priv->an_dip);
				printf("\tadapter %d @ %x\n",
					i, (int)&pcmcia_adapters[i]);
			}
#endif

			/* needed for interrupt setup for CS */
			pcmcia_adapters[i].pca_iblock = priv->an_iblock;
			pcmcia_adapters[i].pca_idev = priv->an_idev;

			ls_if = priv->an_if;
#if defined(PCMCIA_DEBUG)
			if (pcmcia_debug)
				printf("\tinterface at %x\n", (int)ls_if);
#endif
			pcmcia_adapters[i].pca_if = ls_if;
			if (ls_if != NULL) {
				inquire_adapter_t conf;
				int sock, win;

				if (ls_if->pcif_inquire_adapter != NULL)
					GET_CONFIG(ls_if, dip, &conf);

				/* power entries for adapter */
				pcmcia_adapters[i].pca_power =
					conf.power_entry;
				pcmcia_adapters[i].pca_numpower =
					conf.NumPower;

				for (n = 0; n < conf.NumPower; n++)
				    pcmcia_merge_power(&conf.power_entry[n]);

				/* now setup the per socket info */
				for (sock = 0; sock < conf.NumSockets;
					sock++) {
					n = sock + pcmcia_num_sockets;
					pcmcia_sockets[n].ls_socket = sock;
					pcmcia_sockets[n].ls_if = ls_if;
					pcmcia_sockets[n].ls_adapter =
						&pcmcia_adapters[i];
					pcmcia_sockets[n].ls_cs_events = 0L;
				}
				pcmcia_num_sockets += conf.NumSockets;
				pcmcia_adapters[i].pca_numsockets =
					conf.NumSockets;

				/* now setup the per window information */
				for (win = 0; win < conf.NumWindows; win++) {
					n = win + pcmcia_num_windows;
					pcmcia_windows[n].lw_window = win;
					pcmcia_windows[n].lw_if = ls_if;
					pcmcia_windows[n].lw_adapter =
						&pcmcia_adapters[i];
				}
				pcmcia_num_windows += conf.NumWindows;
				SET_CALLBACK(ls_if, dip,
						pcm_adapter_callback, i);
			}
		}
	}
#if defined(PCMCIA_DEBUG)
	if (pcmcia_debug) {
		printf("logical sockets:\n");
		for (i = 0; i < pcmcia_num_sockets; i++) {
			printf("\t%d: phys sock=%d, if=%x, adapt=%x\n",
				i, pcmcia_sockets[i].ls_socket,
				(int)pcmcia_sockets[i].ls_if,
				(int)pcmcia_sockets[i].ls_adapter);
		}
		printf("logical windows:\n");
		for (i = 0; i < pcmcia_num_windows; i++) {
			printf("\t%d: phys_window=%d, if=%x, adapt=%x\n",
				i, pcmcia_windows[i].lw_window,
				(int)pcmcia_windows[i].lw_if,
				(int)pcmcia_windows[i].lw_adapter);
		}
		printf("\tpcmcia_num_power=%d\n", pcmcia_num_power);
		for (n = 0; n < pcmcia_num_power; n++)
			printf("\t\tPowerLevel: %d\tValidSignals: %x\n",
				pcmcia_power_table[n].PowerLevel,
				pcmcia_power_table[n].ValidSignals);
	}
#endif
}

#if defined(i86) && defined(PCMCIA_DEBUG)
void
pcmprintf(char *str, void * a, void * b, void * c, void * d, void * e,
		void * f, void * g, void * h, void * i)
{
	char	buff[256];

	sprintf(buff, str, a, b, c, d, e, f, g, h, i);
	pcm_prints(buff);
}

void
pcm_prints(char *s)
{
	if (!s)
		return;		/* sanity check for s == 0 */
	while (*s)
		cnputc (*s++, 0);
}
#endif

/*
 * pcm_phys_to_log_socket()
 *	from an adapter and socket number return the logical socket
 */
pcm_phys_to_log_socket(struct pcmcia_adapter *adapt, int socket)
{
	register pcmcia_logical_socket_t *sockp;
	int i;

	for (i = 0, sockp = &pcmcia_sockets[0];
		i < pcmcia_num_sockets; i++, sockp++) {
		if (sockp->ls_socket == socket && sockp->ls_adapter == adapt)
			break;
	}
	if (i >= pcmcia_num_sockets) {
#if defined(PCMCIA_DEBUG)
		if (pcmcia_debug)
			printf("\tbad socket/adapter: %x/%x != %x/%x\n",
				socket, (int)adapt, pcmcia_num_sockets,
				pcmcia_num_adapters);
#endif
		return (-1);
	}

	return (i);		/* want logical socket */
}

/*
 * pcm_adapter_callback()
 * 	this function is called back by the adapter driver at interrupt time.
 *	It is here that events should get generated for the event manager if it
 *	is present.  It would also be the time where a device information
 *	tree could be constructed for a card that was added in if we
 *	choose to create them dynamically.
 */

#if defined(PCMCIA_DEBUG)
char *cblist[] = {
	"removal",
	"insert",
	"ready",
	"battery-warn",
	"battery-dead",
	"status-change",
	"write-protect", "reset", "unlock", "client-info", "eject-complete",
	"eject-request", "erase-complete", "exclusive-complete",
	"exclusive-request", "insert-complete", "insert-request",
	"reset-complete", "reset-request", "timer-expired",
	"resume", "suspend"
};
#endif

pcm_adapter_callback(dev_info_t *dip, int adapter, int event, int socket)
{
	register pcmcia_logical_socket_t *sockp;

#if defined(PCMCIA_DEBUG)
	if (pcmcia_debug) {
		printf("pcm_adapter_callback: %x %x %x %x: ",
			(int)dip, adapter, event, socket);
		printf("[%s]\n", cblist[event]);
	}
#endif

	if (adapter >= pcmcia_num_adapters || adapter < 0) {
#if defined(PCMCIA_DEBUG)
		if (pcmcia_debug)
			printf("\tbad adapter number: %d : %d\n",
				adapter, pcmcia_num_adapters);
#endif
		return (1);
	}

	/* get the logical socket since that is what CS knows */
	socket = pcm_phys_to_log_socket(&pcmcia_adapters[adapter], socket);
	if (socket == -1) {
		cmn_err(CE_WARN, "pcmcia callback - bad logical socket\n");
		return (0);
	}
	sockp = &pcmcia_sockets[socket];
	switch (event) {
	case -1:		/* special case of adapter going away */
	case PCE_CARD_INSERT:
		sockp->ls_cs_events |= PCE_E2M(PCE_CARD_INSERT) |
			PCE_E2M(PCE_CARD_REMOVAL);
		break;
	case PCE_CARD_REMOVAL:
				/* disable interrupts at this point */
		sockp->ls_cs_events |= PCE_E2M(PCE_CARD_INSERT) |
			PCE_E2M(PCE_CARD_REMOVAL);

		break;
	case PCE_PM_RESUME:
		pcmcia_do_resume(socket, sockp);
		/* event = PCE_CARD_INSERT; */
		break;
	case PCE_PM_SUSPEND:
		pcmcia_do_suspend(socket, sockp);
		/* event = PCE_CARD_REMOVAL; */
		break;
	default:
		/* nothing to do */
		break;
	}

#if defined(PCMCIA_DEBUG)
	if (pcmcia_debug) {
		printf("\tevent %d, event mask=%x, match=%x (log socket=%d)\n",
			event,
			(int)sockp->ls_cs_events,
			(int)(sockp->ls_cs_events & PCE_E2M(event)), socket);
	}
#endif

	if (pcmcia_cs_event && sockp->ls_cs_events & (1 << event)) {
#if defined(PCMCIA_DEBUG)
		if (pcmcia_debug)
			printf("\tcalling CS event handler (%x) "
				"with event=%d\n",
				pcmcia_cs_event, event);
#endif
		CS_EVENT(event, socket);
	}

	/* let the event manager(s) know about the event */
	pcm_event_manager(event, socket, NULL);

	return (0);
}

/*
 * pcm_event_manager()
 *	checks for registered management driver callback handlers
 *	if there are any, call them if the event warrants it
 */
void
pcm_event_manager(int event, int socket, void *arg)
{
	struct pcmcia_mif *mif;

	for (mif = pcmcia_mif_handlers; mif != NULL; mif = mif->mif_next) {
#if defined(PCMCIA_DEBUG)
		if (pcmcia_debug)
			printf("event: event=%d, mif_events=%x (tst:%d)\n",
				event, (int)*(u_long *)mif->mif_events,
				PR_GET(mif->mif_events, event));
#endif
		if (PR_GET(mif->mif_events, event)) {
			mif->mif_function(mif->mif_id, event, socket, arg);
		}
	}

}

/*
 * pcm_find_devinfo()
 *	this is a wrapper around DDI calls to "find" any
 *	devinfo node and then from there find the one associated
 *	with the socket
 */
dev_info_t *
pcm_find_devinfo(char *name, int socket)
{
	dev_info_t *dip;
	int sock;

	dip = ddi_find_devinfo(name, -1, 0);
	if (dip == NULL)
		return (NULL);
	/*
	 * we have at least a base level dip
	 * see if there is one (this or a sibling)
	 * that has the correct socket number
	 * if there is, return that one else
	 * NULL so a new one is created
	 */
#if defined(PCMCIA_DEBUG)
	if (pcmcia_debug)
		printf("find: initial dip = %x, socket=%d, name=%s "
			"(instance=%d, socket=%d, name=%s)\n",
			(int)dip, socket, name, ddi_get_instance(dip),
			ddi_getprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
					PCM_DEV_SOCKET, -1),
			ddi_get_name(dip));
#endif
	while (dip != NULL && (strcmp(name, ddi_get_name(dip)) ||
				(sock = ddi_getprop(DDI_DEV_T_NONE, dip,
						DDI_PROP_DONTPASS,
						PCM_DEV_SOCKET, -1)) !=
				socket)) {
#if defined(PCMCIA_DEBUG)
		if (pcmcia_debug)
			printf("\tinstance=%d, socket=%d, name=%s\n",
				ddi_get_instance(dip),
				ddi_getprop(DDI_DEV_T_ANY, dip,
					    DDI_PROP_DONTPASS,
					    PCM_DEV_SOCKET, -2),
				ddi_get_name(dip));
#endif
		if (sock == -1 && ddi_get_parent_data(dip) != NULL) {
			/*
			 * only occurs if the framework stripped the
			 * properties during the usual startup modunload
			 */
#if defined(PCMCIA_DEBUG)
			if (pcmcia_debug)
				printf("\tnow patch properties and retry\n");
#endif
			pcm_patch_devinfo(dip);
		} else {
			dip = (dev_info_t *)DEVI(dip)->devi_sibling;
		}
	}
#if defined(PCMCIA_DEBUG)
	if (pcmcia_debug && dip != NULL)
		printf("\treturning non-NULL dip (%s)\n",
			ddi_get_name(dip));
#endif
	return (dip);
}

/*
 * pcmcia_set_em_handler()
 *	This is called by the managment and event driver to tell
 *	the nexus what to call.  Multiple drivers are allowed
 *	but normally only one will exist.
 */

pcmcia_set_em_handler(void (*handler)(), caddr_t events, int elen,
			u_long id, void **cs, void **ss)
{
	struct pcmcia_mif *mif, *tmp;

	if (handler == NULL) {
		/* NULL means remove the handler based on the ID */
		if (pcmcia_mif_handlers == NULL)
			return (0);
		mutex_enter(&pcmcia_global_lock);
		if (pcmcia_mif_handlers->mif_id == id) {
			mif = pcmcia_mif_handlers;
			pcmcia_mif_handlers = mif->mif_next;
			kmem_free((caddr_t)mif, sizeof (struct pcmcia_mif));
		} else {
			for (mif = pcmcia_mif_handlers;
				mif->mif_next != NULL &&
				mif->mif_next->mif_id != id;
				mif = mif->mif_next)
				;
			if (mif->mif_next != NULL &&
			    mif->mif_next->mif_id == id) {
				tmp = mif->mif_next;
				mif->mif_next = tmp->mif_next;
				kmem_free((caddr_t)tmp,
						sizeof (struct pcmcia_mif));
			}
		}
		mutex_exit(&pcmcia_global_lock);
	} else {

		if (pcmcia_num_adapters == 0) {
			return (ENXIO);
		}
		if (elen > EM_EVENTSIZE)
			return (EINVAL);

		mif = (struct pcmcia_mif *)
			kmem_zalloc(sizeof (struct pcmcia_mif),
					KM_NOSLEEP);
		if (mif == NULL)
			return (ENOSPC);

		mif->mif_function = handler;
		bcopy(events, (caddr_t)mif->mif_events, elen);
		mif->mif_id = id;
		mutex_enter(&pcmcia_global_lock);
		mif->mif_next = pcmcia_mif_handlers;
		pcmcia_mif_handlers = mif;
		if (cs != NULL)
			*cs = (void *)pcmcia_card_services;
		if (ss != NULL) {
			*ss = (void *)SocketServices;
		}

		mutex_exit(&pcmcia_global_lock);
	}
	return (0);
}

/*
 * pcm_fix_bits(u_char *data, int num, int dir)
 *	shift socket bits left(0) or right(0)
 *	This is used when mapping logical and physical
 */
void
pcm_fix_bits(socket_enum_t src, socket_enum_t dst, int num, int dir)
{
	int i;

	PR_ZERO(dst);

	if (dir == 0) {
				/* LEFT */
		for (i = 0; i <= (sizeof (dst) * PR_WORDSIZE) - num; i++) {
			if (PR_GET(src, i))
				PR_SET(dst, i + num);
		}
	} else {
				/* RIGHT */
		for (i = num; i < sizeof (dst) * PR_WORDSIZE; i++) {
			if (PR_GET(src, i))
			    PR_SET(dst, i - num);
		}
	}
}

#if defined(PCMCIA_DEBUG)
char *ssfuncs[128] = {
	"GetAdapter", "GetPage", "GetSocket", "GetStatus", "GetWindow",
	"InquireAdapter", "InquireSocket", "InquireWindow", "ResetSocket",
	"SetPage", "SetAdapter", "SetSocket", "SetWindow", "SetIRQHandler",
	"ClearIRQHandler",
	/* 15 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 25 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 35 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 45 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 55 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 65 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 75 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 85 */ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	/* 95 */ NULL, NULL, NULL,
	"CSIsActiveDip",
	"CSInitDev", "CSRegister", "CSCISInit", "CSUnregister",
	"CISGetAddress", "CISSetAddress", "CSCardRemoved", "CSGetCookiesAndDip"
};
#endif
/*
 * SocketServices
 *	general entrypoint for Card Services to find
 *	Socket Services.  Finding the entry requires
 *	a _depends_on[] relationship.
 *
 *	In some cases, the work is done locally but usually
 *	the paramters are adjusted and the adapter driver
 *	code asked to do the work.
 */
SocketServices(int function, ...)
{
	va_list arglist;
	ulong args[16];
	csregister_t *reg;
	sservice_t *serv;
	dev_info_t *dip;
	struct ddi_parent_private_data *ppd;

	va_start(arglist, function);

#if defined(PCMCIA_DEBUG)
	if (pcmcia_debug > 1)
		printf("SocketServices called for function %d [%s]\n",
			function,
			((function < 128) && ssfuncs[function] != NULL) ?
			ssfuncs[function] : "UNKNOWN");
#endif

	switch (function) {
	case CSRegister:
	case CISGetAddress:
	case CISSetAddress:

		reg = va_arg(arglist, csregister_t *);

		if (reg->cs_magic != PCCS_MAGIC ||
		    reg->cs_version != PCCS_VERSION) {
			cmn_err(CE_WARN,
				"pcmcia: CSRegister (%x, %x, %x, %x) *ERROR*",
				(int)reg->cs_magic, (int)reg->cs_version,
				reg->cs_card_services, reg->cs_event);
			return (BAD_FUNCTION);
		}

		switch (function) {
		    case CISGetAddress:
			reg->cs_event = pcmcia_cis_parser;
			return (SUCCESS);
		    case CISSetAddress:
			pcmcia_cis_parser = reg->cs_event;
			return (SUCCESS);
		    case CSRegister:
			pcmcia_card_services = reg->cs_card_services;
			pcmcia_cs_event	= (f_tt *)reg->cs_event;
			break;
		}

		/*
		 * we now "fake" card insertion events for Card Services
		 * so it can determine what we have.  pcmcia_init_socket
		 * will generate the fake card insertions and also provides
		 * the place to add future devinfo handling
		 */

		pcmcia_init_socket();

		return (SUCCESS);

	case CSUnregister:
		pcmcia_card_services = NULL;
		pcmcia_cs_event = NULL;
		return (SUCCESS);

	case CSCISInit:
		args[0] = va_arg(arglist, int);
#if defined(PCMCIA_DEBUG)
		if (pcmcia_debug)
			printf("CSCISInit: CIS is initialized on socket %d\n",
				(int)args[0]);
#endif
		/*
		 * now that the CIS has been parsed (there may not
		 * be one but the work is done) we can create the
		 * device information structures.
		 */
		pcmcia_create_dev_info(pcmcia_dip, args[0]);
		return (SUCCESS);

	case CSInitDev:
#if defined(PCMCIA_DEBUG)
		if (pcmcia_debug)
			printf("CSInitDev: initialize device\n");
#endif
		/*
		 * this is where we create the /devices entries
		 * that let us out into the world
		 */

		(void) pcmcia_create_device(pcmcia_dip,
					va_arg(arglist,
						ss_make_device_node_t *));
		return (SUCCESS);

	case CSCardRemoved:
		args[0] = va_arg(arglist, u_long);
#if defined(PCMCIA_DEBUG)
		if (pcmcia_debug)
			printf("CSCardRemoved! (socket=%d)\n", (int)args[0]);
#endif
		if (args[0] < pcmcia_num_sockets) {
			/* break the association of dip and socket */
			dip = pcmcia_sockets[args[0]].ls_dip;
			pcmcia_sockets[args[0]].ls_dip = NULL;
			if (dip != NULL) {
				(void) ddi_prop_remove(DDI_DEV_T_NONE,
							dip, PCM_DEV_ACTIVE);
#if 0
				/*
				 * if card is unknown, remove
				 * the dip since we will never
				 * be able to get to it.  Also,
				 * remove any unattached dips since
				 * they aren't needed now and will come
				 * back when the card is re-inserted
				 */
				if (DDI_CF2(dip) == 0) {
					/* if it isn't attached, nuke it */
					/* CSTYLED */
					ppd = (struct ddi_parent_private_data *)
						ddi_get_parent_data(dip);
					ddi_set_parent_data(dip, NULL);
					ddi_remove_child(dip, 0);
					kmem_free((caddr_t)ppd,
							sizeof (*ppd));
				}
#endif
			}
#if defined(PCMCIA_DEBUG)
			else {
				if (pcmcia_debug)
					printf("CardRemoved: no dip present "
						"on socket %d!\n",
						(int)args[0]);
			}
#endif
		}
		return (SUCCESS);

	case CSGetCookiesAndDip:
		serv = va_arg(arglist, sservice_t *);
		if (serv != NULL) {
			return (GetCookiesAndDip(serv));
		}
		return (BAD_SOCKET);

	case CSGetActiveDip:
		/*
		 * get the dip associated with the card currently
		 * in the specified socket
		 */
		args[0] = va_arg(arglist, u_long);
		return ((long)pcmcia_sockets[args[0]].ls_dip);

		/*
		 * the remaining entries are SocketServices calls
		 */
	case SS_GetAdapter:
		return (SSGetAdapter(va_arg(arglist, get_adapter_t *)));
	case SS_GetPage:
		return (SSGetPage(va_arg(arglist, get_page_t *)));
	case SS_GetSocket:
		return (SSGetSocket(va_arg(arglist, get_socket_t *)));
	case SS_GetStatus:
		return (SSGetStatus(va_arg(arglist, get_ss_status_t *)));
	case SS_GetWindow:
		return (SSGetWindow(va_arg(arglist, get_window_t *)));
	case SS_InquireAdapter:
		return (SSInquireAdapter(va_arg(arglist, inquire_adapter_t *)));
	case SS_InquireSocket:
		return (SSInquireSocket(va_arg(arglist, inquire_socket_t *)));
	case SS_InquireWindow:
		return (SSInquireWindow(va_arg(arglist, inquire_window_t *)));
	case SS_ResetSocket:
		args[0] = va_arg(arglist, uint);
		args[1] = va_arg(arglist, int);
		return (SSResetSocket(args[0], args[1]));
	case SS_SetPage:
		return (SSSetPage(va_arg(arglist, set_page_t *)));
	case SS_SetSocket:
		return (SSSetSocket(va_arg(arglist, set_socket_t *)));
	case SS_SetWindow:
		return (SSSetWindow(va_arg(arglist, set_window_t *)));
	case SS_SetIRQHandler:
		return (SSSetIRQHandler(va_arg(arglist, set_irq_handler_t *)));
	case SS_ClearIRQHandler:
		return (SSClearIRQHandler(va_arg(arglist,
						    clear_irq_handler_t *)));
	default:
		return (BAD_FUNCTION);
	}
}

/*
 * pcmcia_init_socket()
 *	fake insertion events for the cards that were inserted
 *	prior to the nexus being loaded.  This is where initial
 *	device information trees could be made.
 */
void
pcmcia_init_socket()
{
	int s;
	get_ss_status_t stat;
	struct pcmcia_adapter *adapt;
	pcmcia_if_t *ls_if;
	int present;

	if (pcmcia_cs_event == NULL) {
		return;
	}

	for (s = 0; s < pcmcia_num_sockets; s++) {
		int socket;

		adapt = pcmcia_sockets[s].ls_adapter;
		ls_if = pcmcia_sockets[s].ls_if;
		socket = pcmcia_sockets[s].ls_socket;
		present = 0;

		if (ls_if == NULL || ls_if->pcif_get_status == NULL) {
			continue;
		}

		stat.socket = socket;
#if defined(PCMCIA_DEBUG)
		if (pcmcia_debug) {
			printf("pcmcia_init_socket: <get_status>\n");
		}
#endif
		if (GET_STATUS(ls_if, adapt->pca_dip, &stat) ==
		    SUCCESS) {

#if defined(PCMCIA_DEBUG)
			if (pcmcia_debug)
				printf("\tsocket=%x, CardState=%x\n",
					s, stat.CardState);
#endif
			/* now have socket info -- do we have events? */
			if ((stat.CardState & SBM_CD) == SBM_CD) {
				present = 1;
				(void) SSResetSocket(s, RESET_MODE_FULL);
				CS_EVENT(PCE_CARD_INSERT, s);
			}

			if ((stat.CardState & (SBM_BVD1|SBM_BVD2)) !=
			    (SBM_BVD1|SBM_BVD2) && present) {
				if (stat.CardState & SBM_BVD1)
					CS_EVENT(PCE_CARD_BATTERY_WARN, s);
				else
					CS_EVENT(PCE_CARD_BATTERY_DEAD, s);
			}
		}
	}
}

/*
 * pcmcia_merge_power()
 *	The adapters may have different power tables so it
 *	is necessary to construct a single power table that
 *	can be used throughout the system.  The result is
 *	a merger of all capabilities.  The nexus adds
 *	power table entries one at a time.
 */
void
pcmcia_merge_power(struct power_entry *power)
{
	int i;
	struct power_entry pwr;

	pwr = *power;

	for (i = 0; i < pcmcia_num_power; i++) {
		if (pwr.PowerLevel == pcmcia_power_table[i].PowerLevel) {
			if (pwr.ValidSignals ==
				pcmcia_power_table[i].ValidSignals) {
				return;
			} else {
				/* partial match */
				pwr.ValidSignals &=
					~pcmcia_power_table[i].ValidSignals;
			}
		}
	}
	/* what's left becomes a new entry */
	if (pcmcia_num_power == PCMCIA_MAX_POWER)
		return;
	pcmcia_power_table[pcmcia_num_power++] = pwr;
}

/*
 * pcmcia_do_suspend()
 *	tell CS that a suspend has happened by passing a
 *	card removal event.  Then cleanup the socket state
 *	to fake the cards being removed so resume works
 */
void
pcmcia_do_suspend(int socket, pcmcia_logical_socket_t *sockp)
{
	get_ss_status_t stat;
	struct pcmcia_adapter *adapt;
	pcmcia_if_t *ls_if;
	dev_info_t *dip;

#ifdef	XXX
	if (pcmcia_cs_event == NULL) {
		return;
	}
#endif

	ls_if = sockp->ls_if;
	adapt = sockp->ls_adapter;
	dip = sockp->ls_dip;

	if (ls_if == NULL || ls_if->pcif_get_status == NULL) {
		return;
	}

	stat.socket = socket;
#if defined(PCMCIA_DEBUG)
	if (pcmcia_debug) {
		printf("pcmcia_do_suspend(%d, %x)\n", socket, (int)sockp);
	}
#endif

	if (GET_STATUS(ls_if, adapt->pca_dip, &stat) != SUCCESS)
		return;

	/*
	 * If there is a card in the socket, then we need to send
	 *	everyone a PCE_CARD_REMOVAL event, and remove the
	 *	card active property.
	 */

#ifdef	CHECK_CARD_INSERTED
	if ((stat.CardState & SBM_CD) == SBM_CD) {
#endif
	    if (pcmcia_cs_event &&
			(sockp->ls_cs_events & (1 << PCE_CARD_REMOVAL))) {
		CS_EVENT(PCE_CARD_REMOVAL, socket);
	    }
	    pcm_event_manager(PCE_CARD_REMOVAL, socket, NULL);
	    if (dip != NULL) {
		(void) ddi_prop_remove(DDI_DEV_T_NONE,
					dip, PCM_DEV_ACTIVE);
		sockp->ls_dip = NULL;
	    }
#ifdef	CHECK_CARD_INSERTED
	} /* if (SBM_CD) */
#endif
}

/*
 * pcmcia_do_resume()
 *	tell CS that a suspend has happened by passing a
 *	card removal event.  Then cleanup the socket state
 *	to fake the cards being removed so resume works
 */
void
pcmcia_do_resume(int socket, pcmcia_logical_socket_t *sockp)
{
	get_ss_status_t stat;
	struct pcmcia_adapter *adapt;
	pcmcia_if_t *ls_if;

#ifdef	XXX
	if (pcmcia_cs_event == NULL) {
		return;
	}
#endif

	ls_if = sockp->ls_if;
	adapt = sockp->ls_adapter;

	if (ls_if == NULL || ls_if->pcif_get_status == NULL) {
		return;
	}

	stat.socket = socket;
#if defined(PCMCIA_DEBUG)
	if (pcmcia_debug) {
		printf("pcmcia_do_resume(%d, %x)\n", socket, (int)sockp);
	}
#endif
	if (GET_STATUS(ls_if, adapt->pca_dip, &stat) ==
	    SUCCESS) {

#if defined(PCMCIA_DEBUG)
		if (pcmcia_debug)
			printf("\tsocket=%x, CardState=%x\n",
				socket, stat.CardState);
#endif
		/* now have socket info -- do we have events? */
		if ((stat.CardState & SBM_CD) == SBM_CD) {
			if (pcmcia_cs_event &&
			    (sockp->ls_cs_events & (1 << PCE_CARD_INSERT))) {
				CS_EVENT(PCE_CARD_INSERT, socket);
			}

			/* we should have card removed from CS soon */
			pcm_event_manager(PCE_CARD_INSERT, socket, NULL);
		}
	}
}

/*
 * pcmcia_map_power_set()
 *	Given a power table entry and level, find it in the
 *	master table and return the index in the adapter table.
 */

pcmcia_map_power_set(struct pcmcia_adapter *adapt, int level, int which)
{
	int plevel, i;
	struct power_entry *pwr = (struct power_entry *)adapt->pca_power;
	plevel = pcmcia_power_table[level].PowerLevel;
	/* mask = pcmcia_power_table[level].ValidSignals; */
	for (i = 0; i < adapt->pca_numpower; i++)
		if (plevel == pwr[i].PowerLevel &&
		    pwr[i].ValidSignals & which)
			return (i);
	return (0);
}

/*
 * pcmcia_map_power_get()
 *	Given an adapter power entry, find the appropriate index
 *	in the master table.
 */
pcmcia_map_power_get(struct pcmcia_adapter *adapt, int level, int which)
{
	int plevel, i;
	struct power_entry *pwr = (struct power_entry *)adapt->pca_power;
	plevel = pwr[level].PowerLevel;
	/* mask = pwr[level].ValidSignals; */
	for (i = 0; i < pcmcia_num_power; i++)
		if (plevel == pcmcia_power_table[i].PowerLevel &&
		    pcmcia_power_table[i].ValidSignals & which)
			return (i);
	return (0);
}

/*
 * XXX - SS really needs a way to allow the caller to express
 *	interest in PCE_CARD_STATUS_CHANGE events.
 */
static u_long
pcm_event_map[32] = {
	PCE_E2M(PCE_CARD_WRITE_PROTECT)|PCE_E2M(PCE_CARD_STATUS_CHANGE),
	PCE_E2M(PCE_CARD_UNLOCK)|PCE_E2M(PCE_CARD_STATUS_CHANGE),
	PCE_E2M(PCE_EJECTION_REQUEST)|PCE_E2M(PCE_CARD_STATUS_CHANGE),
	PCE_E2M(PCE_INSERTION_REQUEST)|PCE_E2M(PCE_CARD_STATUS_CHANGE),
	PCE_E2M(PCE_CARD_BATTERY_WARN)|PCE_E2M(PCE_CARD_STATUS_CHANGE),
	PCE_E2M(PCE_CARD_BATTERY_DEAD)|PCE_E2M(PCE_CARD_STATUS_CHANGE),
	PCE_E2M(PCE_CARD_READY)|PCE_E2M(PCE_CARD_STATUS_CHANGE),
	PCE_E2M(PCE_CARD_REMOVAL)|PCE_E2M(PCE_CARD_INSERT)|
					PCE_E2M(PCE_CARD_STATUS_CHANGE),
};
pcm_mapevents(u_long eventmask)
{
	register u_long mask;
	register int i;

	for (i = 0, mask = 0; eventmask && i < 32; i++) {
		if (eventmask & (1 << i)) {
			mask |= pcm_event_map[i];
			eventmask &= ~(1 << i);
		}
	}
	return (mask);
}


#if defined(i86)
/*
 * pcmcia_x86_specific
 *	we need to find out what resources are available for
 *	PC Card drivers.  For now, they are provided via
 *	several properties.
 */
static void
pcmcia_x86_specific(dev_info_t *dip)
{
	/* nothing defined yet.  May add 486SL detection */
}

#endif

/*
 * pcm_get_nodeid()
 *	generates a unique nodeid for a PC Card
 *	it currently has a monotonically increasing counter.
 *	It might make some sense to use an address in
 *	the CIS for those cards that have one, but not all do
 *	and it might get remapped during swaps.
 */
/* ARGSUSED */
pcm_get_nodeid(int socket)
{
	static int nodeid = 1;
	return (nodeid++);
}

/*
 * pcmcia_create_dev_info()
 *	either find or create the device information structure
 *	for the card just inserted.  We don't care about removal yet.
 *	In any case, we will only do this at CS request
 */
void
pcmcia_create_dev_info(dev_info_t *pdip, int socket)
{
	char driver[2*MODMAXNAMELEN], ident[2*MODMAXNAMELEN];
	struct pcm_device_info card_info;
	client_reg_t reg;

#if defined(PCMCIA_DEBUG)
	if (pcmcia_debug)
		printf("create dev_info_t for device in socket %d\n",
			socket);
#endif

	/* Card Services calls needed to get CIS info */
	reg.dip = NULL;
	reg.Attributes = INFO_SOCKET_SERVICES;
	reg.EventMask = 0;
	reg.event_handler = NULL;
	reg.Version = CS_VERSION;

	if (pcmcia_card_services(RegisterClient, &card_info.pd_handle,
				    &reg) != CS_SUCCESS) {
#if defined(PCMCIA_DEBUG)
		if (pcmcia_debug)
			printf("pcmcia: RegisterClient failed\n");
#endif
		return;
	} else {
#if defined(PCMCIA_DEBUG)
		if (pcmcia_debug)
			printf("pcmcia_create_dev_info: handle = %x\n",
				(int)card_info.pd_handle);
#endif
		card_info.pd_type = -1;	/* no type to start */
		card_info.pd_socket = socket;
		driver[0] = '\0';
		ident[0] = '\0';
		(void) pcm_identify_device(socket, &card_info,
						driver, ident);
		pcmcia_init_devinfo(pdip, driver, &card_info, ident);
		pcm_event_manager(PCE_DEV_IDENT, socket, driver);
	}
}

/*
 * pcmcia_init_devinfo()
 *	if there isn't a device info structure, create one
 *	if there is, we don't do much
 */
static void
pcmcia_init_devinfo(dev_info_t *pdip, char *driver,
			struct pcm_device_info *info, char *ident)
{
	int major, unit;
	dev_info_t *dip;
	dev_info_t *protodip = NULL;

#if defined(PCMCIA_DEBUG)
	if (pcmcia_debug)
		printf("init_devinfo(%s, %d)\n", driver, info->pd_socket);
#endif
	major = ddi_name_to_major(driver);

#if defined(PCMCIA_DEBUG)
	if (pcmcia_debug && major < 0)
		printf("no major device number for %s\n", driver);
#endif
	unit = info->pd_socket;
	dip = pcm_find_devinfo(driver, unit);
	if (dip != NULL &&
	    ddi_getprop(DDI_DEV_T_NONE, dip, DDI_PROP_DONTPASS,
			PCM_DEV_SOCKET, -1) != -1) {
		/* it already exist but isn't a .conf file */
#if defined(PCMCIA_DEBUG)
		if (pcmcia_debug)
			printf("\tfound existing device node (%s)\n",
				ddi_get_name(dip));
#endif
		/* need to check if stripped or not */
		if (strlen(ident) > 0) {
			if (ddi_prop_modify(DDI_DEV_T_NONE, dip,
						0, PCM_DEV_MODEL,
						ident,
						strlen(ident) + 1) ==
			    DDI_PROP_NOT_FOUND) {
				ddi_prop_create(DDI_DEV_T_NONE, dip,
						0, PCM_DEV_MODEL,
						ident,
						strlen(ident) + 1);
			}
		}
	} else {
		struct regspec *regs;	/* register property tuple */
		char *dtype;
		struct ddi_parent_private_data *ppd;
#if defined(PCMCIA_DEBUG)
		if (pcmcia_debug)
			printf("pcmcia: create child [%s](%d): %s\n",
				driver, info->pd_socket, ident);
#endif
		/*
		 * create the dev info structure so we can
		 * fill it with good values
		 */
		if (major < 0) {
			/*
			 * special case where driver is unknown
			 * but enough info to have unique short string
			 * is available.
			 */
			if (strcmp(driver, pcm_unknown_drv) != 0)
				ident = driver;
			dip = ddi_add_child(pdip, ident,
					    info->pd_nodeid,
					    -1);
		} else {
			protodip = ddi_find_devinfo(driver, -1, 1);
			dip = ddi_add_child(pdip, driver, info->pd_nodeid,
					    -1);
		}
		if (dip != NULL) {
			ppd = (struct ddi_parent_private_data *)
				kmem_zalloc(sizeof (*ppd),
						KM_NOSLEEP);
			if (ppd != NULL) {
				regs = kmem_zalloc(sizeof (struct regspec),
							KM_NOSLEEP);
				ppd->par_nreg = 1;
				ppd->par_reg = regs;
				ddi_set_parent_data(dip, (caddr_t)ppd);
			}
		}
		if (dip == NULL || ppd == NULL)
			return;
#if 0
		/*
		 * we need canonical form 1 so start out that way
		 * even though it isn't really correct
		 */
		sprintf(name, "%d", info->pd_socket);
		ddi_set_name_addr(dip, name);
#endif
		/*
		 * initialize the registers property
		 * so we can have a unique entity.
		 */
				/* socket is unique */
		regs->regspec_bustype = info->pd_socket;

/*
		(void) ddi_prop_create(DDI_DEV_T_NONE, dip, 0,
					"registers",
					(caddr_t)regs, sizeof (regs));
*/
		/* init the device type */
		if (info->pd_type < (sizeof (pcmcia_dev_type) /
					(sizeof (char *))))
			dtype = pcmcia_dev_type[info->pd_type];
		else
			dtype = "unknown";
		(void) ddi_prop_create(DDI_DEV_T_NONE, dip, 0,
					PCM_DEVICETYPE, dtype,
					strlen(dtype) + 1);
		if (strlen(ident) > 0)
			(void) ddi_prop_create(DDI_DEV_T_NONE, dip,
						0, PCM_DEV_MODEL,
						ident,
						strlen(ident) + 1);
		pcmcia_sockets[info->pd_socket].ls_dip = dip;
		/* create a "socket" property */
		(void) ddi_prop_create(DDI_DEV_T_NONE, dip, 0,
					PCM_DEV_SOCKET,
					(caddr_t) regs,
					sizeof (int));

		/* additional 1275 defined properties */
		(void) ddi_prop_create(DDI_DEV_T_NONE, dip, 0,
					PCM_DEV_TYPE, NULL, 0);
	}
	/* create boolean property to indicate active state */
	if (dip != NULL && ddi_getprop(DDI_DEV_T_NONE, dip,
					DDI_PROP_DONTPASS, PCM_DEV_ACTIVE,
					-1) == -1) {
		(void) ddi_prop_create(DDI_DEV_T_NONE, dip,
					0, PCM_DEV_ACTIVE,
					NULL, 0);
#if defined(PCMCIA_DEBUG)
		if (pcmcia_debug)
			printf("\tjust added \"active\" to %s in %d\n",
				ddi_get_name(dip),
				ddi_getprop(DDI_DEV_T_NONE, dip,
					    DDI_PROP_DONTPASS,
					    PCM_DEV_SOCKET, -1));
#endif
	}
	pcmcia_sockets[info->pd_socket].ls_dip = dip;

	/*
	 * this triggers probe/attach
	 * Note that this only happens on second
	 * or later instances.  Don't hold the driver
	 * modloaded since that makes unload impossible
	 */
	if (protodip != NULL && impl_proto_to_cf2(dip) == DDI_SUCCESS)
		mod_rele_dev_by_devi(dip);
}

/*
 * pcmcia_get_devinfo(socket)
 *	entry point to allow finding the device info structure
 *	for a given logical socket.  Used by event manager
 */
dev_info_t *
pcmcia_get_devinfo(int socket)
{
	return (pcmcia_sockets[socket].ls_dip);
}

/*
 * CSGetCookiesAndDip()
 *	get info needed by CS to setup soft interrupt handler
 */
static
GetCookiesAndDip(sservice_t *serv)
{
	pcmcia_logical_socket_t *socket;

	if (serv->get_cookies.socket >= pcmcia_num_sockets)
		return (BAD_SOCKET);

	socket = &pcmcia_sockets[serv->get_cookies.socket];
	serv->get_cookies.dip = socket->ls_adapter->pca_dip;
	serv->get_cookies.iblock = socket->ls_adapter->pca_iblock;
	serv->get_cookies.idevice = socket->ls_adapter->pca_idev;
	return (SUCCESS);
}

/*
 * Note:
 *	The following functions that start with 'SS'
 *	implement SocketServices interfaces.  They
 *	simply map the socket and/or window number to
 *	the adapter specific number based on the general
 *	value that CardServices uses.
 *
 *	See the descriptions in SocketServices for
 *	details.  Also refer to specific adapter drivers
 *	for implementation reference.
 */

static int
SSGetAdapter(get_adapter_t *adapter)
{
	int n;
	get_adapter_t info;

	adapter->state = (unsigned) 0xFFFFFFFF;
	adapter->SCRouting = 0xFFFFFFFF;

	for (n = 0; n < pcmcia_num_adapters; n++) {
		GET_ADAPTER(pcmcia_adapters[n].pca_if,
			pcmcia_adapters[n].pca_dip, &info);
		adapter->state &= info.state;
		adapter->SCRouting &= info.SCRouting;
	}

	return (SUCCESS);
}

static
SSGetPage(get_page_t *page)
{
	pcmcia_logical_window_t *window;
	get_page_t newpage;
	int retval, win;

	if (page->window > pcmcia_num_windows) {
		return (BAD_WINDOW);
	}

	window = &pcmcia_windows[page->window];
	newpage = *page;
	win = newpage.window = window->lw_window; /* real window */

	retval = GET_PAGE(window->lw_if, window->lw_adapter->pca_dip,
		&newpage);
	if (retval == SUCCESS) {
		*page = newpage;
		page->window = win;
	}
	return (retval);
}

static
SSGetSocket(get_socket_t *socket)
{
	int retval, sock;
	get_socket_t newsocket;
	pcmcia_logical_socket_t *sockp;

	sock = socket->socket;
	if (sock > pcmcia_num_sockets) {
		return (BAD_SOCKET);
	}
	sockp = &pcmcia_sockets[sock];
	newsocket = *socket;
	newsocket.socket = sockp->ls_socket;
	retval = GET_SOCKET(sockp->ls_if, sockp->ls_adapter->pca_dip,
				&newsocket);
	if (retval == SUCCESS) {
		newsocket.VccLevel = pcmcia_map_power_get(sockp->ls_adapter,
							newsocket.VccLevel,
							VCC);
		newsocket.Vpp1Level = pcmcia_map_power_get(sockp->ls_adapter,
							newsocket.Vpp1Level,
							VPP1);
		newsocket.Vpp2Level = pcmcia_map_power_get(sockp->ls_adapter,
							newsocket.Vpp2Level,
							VPP2);
		*socket = newsocket;
		socket->socket = sock;
	}

	return (retval);
}

static int
SSGetStatus(get_ss_status_t *status)
{
	get_ss_status_t newstat;
	int sock, retval;
	pcmcia_logical_socket_t *sockp;

	sock = status->socket;
	if (sock > pcmcia_num_sockets) {
		return (BAD_SOCKET);
	}
	sockp = &pcmcia_sockets[sock];
	newstat = *status;
	newstat.socket = sockp->ls_socket;
	retval = GET_STATUS(sockp->ls_if, sockp->ls_adapter->pca_dip,
				&newstat);
	if (retval == SUCCESS) {
		*status = newstat;
		status->socket = sock;
	}

	return (retval);
}

static int
SSGetWindow(get_window_t *window)
{
	int win, retval;
	get_window_t newwin;
	pcmcia_logical_window_t *winp;

	win = window->window;
	winp = &pcmcia_windows[win];
	newwin = *window;
	newwin.window = winp->lw_window;

	retval = GET_WINDOW(winp->lw_if, winp->lw_adapter->pca_dip,
				&newwin);
	if (retval == SUCCESS) {
		newwin.socket = pcm_phys_to_log_socket(winp->lw_adapter,
							newwin.socket);
		newwin.window = win;
		*window = newwin;
	}
	return (retval);
}

/*
 * SSInquireAdapter()
 *	Get the capabilities of the "generic" adapter
 *	we are exporting to CS.
 */
static int
SSInquireAdapter(inquire_adapter_t *adapter)
{
	adapter->NumSockets = pcmcia_num_sockets;
	adapter->NumWindows = pcmcia_num_windows;
	adapter->NumEDCs = 0;
	/*
	 * notes: Adapter Capabilities are going to be difficult to
	 * determine with reliability.  Fortunately, most of them
	 * don't matter under Solaris or can be handled transparently
	 */
	adapter->AdpCaps = 0;	/* need to fix these */
	/*
	 * interrupts need a little work.  For x86, the valid IRQs will
	 * be restricted to those that the system has exported to the nexus.
	 * for SPARC, it will be the DoRight values.
	 */
	adapter->ActiveHigh = 0;
	adapter->ActiveLow = 0;
	adapter->power_entry = pcmcia_power_table; /* until we resolve this */
	adapter->NumPower = pcmcia_num_power;
	return (SUCCESS);
}

static int
SSInquireSocket(inquire_socket_t *socket)
{
	int retval, sock;
	inquire_socket_t newsocket;
	pcmcia_logical_socket_t *sockp;

	sock = socket->socket;
	if (sock > pcmcia_num_sockets)
		return (BAD_SOCKET);
	newsocket = *socket;
	sockp = &pcmcia_sockets[sock];
	newsocket.socket = sockp->ls_socket;
	retval = INQUIRE_SOCKET(sockp->ls_if, sockp->ls_adapter->pca_dip,
				&newsocket);
	if (retval == SUCCESS) {
		*socket = newsocket;
		socket->socket = sock;
	}
	return (retval);
}

static int
SSInquireWindow(inquire_window_t *window)
{
	int retval, win;
	pcmcia_logical_window_t *winp;
	inquire_window_t newwin;
	int slide;

	win = window->window;
	if (win > pcmcia_num_windows)
		return (BAD_WINDOW);

	winp = &pcmcia_windows[win];
	newwin = *window;
	newwin.window = winp->lw_window;
	retval = INQUIRE_WINDOW(winp->lw_if, winp->lw_adapter->pca_dip,
				&newwin);
#if defined(PCMCIA_DEBUG)
		if (pcmcia_debug)
			printf("SSInquiteWindow: win=%d, pwin=%d\n",
				win, newwin.window);
#endif
	if (retval == SUCCESS) {
		*window = newwin;
		/* just in case */
		window->iowin_char.IOWndCaps &= ~WC_BASE;
		slide = winp->lw_adapter->pca_unit *
			winp->lw_adapter->pca_numsockets;
		/*
		 * note that sockets are relative to the adapter.
		 * we have to adjust the bits to show a logical
		 * version.
		 */

		pcm_fix_bits(newwin.Sockets, window->Sockets, slide, 0);

#if defined(PCMCIA_DEBUG)
		if (pcmcia_debug)
			printf("iw: orig bits=%x, new bits=%x\n",
				(int)*(u_long *)newwin.Sockets,
				(int)*(u_long *)window->Sockets);
#endif
		window->window = win;
	}
	return (retval);
}

static int
SSResetSocket(int socket, int mode)
{
	pcmcia_logical_socket_t *sockp;

	if (socket >= pcmcia_num_sockets)
		return (BAD_SOCKET);

	sockp = &pcmcia_sockets[socket];
	return (RESET_SOCKET(sockp->ls_if, sockp->ls_adapter->pca_dip,
				sockp->ls_socket, mode));
}

static int
SSSetPage(set_page_t *page)
{
	int window, retval;
	set_page_t newpage;
	pcmcia_logical_window_t *winp;

	window = page->window;
	if (window > pcmcia_num_windows) {
#if defined(PCMCIA_DEBUG)
		if (pcmcia_debug)
			printf("SSSetPage: window=%d (of %d)\n",
				window, pcmcia_num_windows);
#endif
		return (BAD_WINDOW);
	}

	winp = &pcmcia_windows[window];
	newpage = *page;
	newpage.window = winp->lw_window;
	retval = SET_PAGE(winp->lw_if, winp->lw_adapter->pca_dip, &newpage);
	if (retval == SUCCESS) {
		newpage.window = window;
		*page = newpage;
	}
#if defined(PCMCIA_DEBUG)
	if (pcmcia_debug && retval != SUCCESS)
		printf("\tSetPage: returning error %x\n", retval);
#endif
	return (retval);
}

static int
SSSetWindow(set_window_t *win)
{
	int socket, window, retval;
	set_window_t newwin;
	pcmcia_logical_window_t *winp;
	pcmcia_logical_socket_t *sockp;

	window = win->window;
	if (window > pcmcia_num_windows)
		return (BAD_WINDOW);

	socket = win->socket;
	if (socket > pcmcia_num_sockets) {
		return (BAD_SOCKET);
	}

	winp = &pcmcia_windows[window];
	sockp = &pcmcia_sockets[socket];
	winp->lw_socket = socket; /* reverse map */
	newwin = *win;
	newwin.window = winp->lw_window;
	newwin.socket = sockp->ls_socket;
	retval = SET_WINDOW(winp->lw_if, winp->lw_adapter->pca_dip, &newwin);
	if (retval == SUCCESS) {
		newwin.window = window;
		newwin.socket = socket;
		*win = newwin;
	}
	return (retval);
}

static int
SSSetSocket(set_socket_t *socket)
{
	int sock, retval;
	pcmcia_logical_socket_t *sockp;
	set_socket_t newsock;

	sock = socket->socket;
	if (sock > pcmcia_num_sockets) {
		return (BAD_SOCKET);
	}

	newsock = *socket;
	sockp = &pcmcia_sockets[sock];
	/* note: we force CS to always get insert/removal events */
	sockp->ls_cs_events = pcm_mapevents(newsock.SCIntMask) |
		PCE_E2M(PCE_CARD_INSERT) | PCE_E2M(PCE_CARD_REMOVAL);
#if defined(PCMCIA_DEBUG)
	if (pcmcia_debug)
		printf("SetSocket: SCIntMask = %x\n", newsock.SCIntMask);
#endif
	newsock.socket = sockp->ls_socket;
	newsock.VccLevel = pcmcia_map_power_set(sockp->ls_adapter,
						newsock.VccLevel, VCC);
	newsock.Vpp1Level = pcmcia_map_power_set(sockp->ls_adapter,
						newsock.Vpp1Level, VPP1);
	newsock.Vpp2Level = pcmcia_map_power_set(sockp->ls_adapter,
						newsock.Vpp2Level, VPP2);
	retval = SET_SOCKET(sockp->ls_if, sockp->ls_adapter->pca_dip,
				&newsock);
	if (retval == SUCCESS) {
		newsock.socket = sock;
		newsock.VccLevel = pcmcia_map_power_get(sockp->ls_adapter,
							newsock.VccLevel,
							VCC);
		newsock.Vpp1Level = pcmcia_map_power_get(sockp->ls_adapter,
							newsock.Vpp1Level,
							VPP1);
		newsock.Vpp2Level = pcmcia_map_power_get(sockp->ls_adapter,
							newsock.Vpp2Level,
							VPP2);
		*socket = newsock;
	}
	return (retval);
}

static int
SSSetIRQHandler(set_irq_handler_t *handler)
{
	int sock, retval;
	pcmcia_logical_socket_t *sockp;
	set_irq_handler_t newhandler;

	sock = handler->socket;
	if (sock > pcmcia_num_sockets) {
		return (BAD_SOCKET);
	}

	sockp = &pcmcia_sockets[sock];
	newhandler = *handler;
	newhandler.socket = sockp->ls_socket;
	retval = SET_IRQ(sockp->ls_if, sockp->ls_adapter->pca_dip,
				&newhandler);
	if (retval == SUCCESS) {
		newhandler.socket = sock;
		*handler = newhandler;
	}
	return (retval);
}

static int
SSClearIRQHandler(clear_irq_handler_t *handler)
{
	int sock, retval;
	pcmcia_logical_socket_t *sockp;
	clear_irq_handler_t newhandler;

	sock = handler->socket;
	if (sock > pcmcia_num_sockets) {
		return (BAD_SOCKET);
	}

	sockp = &pcmcia_sockets[sock];
	newhandler = *handler;
	newhandler.socket = sockp->ls_socket;
	retval = CLEAR_IRQ(sockp->ls_if, sockp->ls_adapter->pca_dip,
				&newhandler);
	if (retval == SUCCESS) {
		newhandler.socket = sock;
		*handler = newhandler;
	}
	return (retval);
}


pcm_make_dev_string(char *buff, int pat, int bound,
		    cistpl_funcid_t *funcid,
		    cistpl_manfid_t *manfid,
		    cistpl_funce_t *funce,
		    cistpl_jedec_t *jedec,
		    cistpl_vers_1_t *vers1,
		    cistpl_device_t *device)
{
	int len, i, n;
	int need_delim;
	char tmp[16];

	if (buff == NULL)
		return (0);

#if defined(PCMCIA_DEBUG)
	if (pcmcia_debug)
		printf("pcm_make_dev_string(%x, %x, %x, "
			"%x, %x, %x, %x, %x, %x)\n",
			(int)buff, pat, bound, (int)funcid, (int)manfid,
			(int)funce, (int)jedec, (int)vers1, (int)device);
#endif

	*buff = '\0';
	len = 0;

	if (bound < 12)
		return (0);

	strcpy(buff, PCMDEV_PREFIX);
	len = strlen(PCMDEV_PREFIX);
	buff += len;

	need_delim = 0;
	if (pat & PCMD_FUNCID) {
		need_delim++;
#if defined(PCMCIA_DEBUG)
	if (pcmcia_debug)
		printf("pcm: funcid: %x\n", funcid->function);
#endif
		if (funcid->function <
		    (sizeof (pcmcia_def_dev_map)/sizeof (char *)))
			strcpy(buff, pcmcia_def_dev_map[funcid->function]);
		else
			sprintf(buff, "f%x", funcid->function);
		buff += (i = strlen(buff));
		len += i;
	}

	if (pat & PCMD_FUNCE) {
		sprintf(tmp, "%s%x.%x", need_delim ? "/" : "",
			funce->function, funce->subfunction);
		i = strlen(tmp);
		if ((len + i) > bound)
			return (len);
		strcpy(buff, tmp);
		buff += i;
		len += i;
		need_delim++;
	}

	if (pat & PCMD_MANFID) {
		sprintf(tmp, "%s%x.%x", need_delim ? "/" : "",
			manfid->manf, manfid->card);
		i = strlen(tmp);
		if ((len + i) > bound)
			return (len);
		strcpy(buff, tmp);
		buff += i;
		len += i;
		need_delim++;
	}

	if (pat & PCMD_JEDEC) {
		char *del;
		if (need_delim)
			del = "/";
		else
			del = "";
		for (n = 0; n < jedec->nid && len < bound; n++) {
			sprintf(tmp, "%s%x.%x", del,
				jedec->jid[n].id,
				jedec->jid[n].info);
			del = ".";
			i = strlen(tmp);
			if ((len + i) > bound) {
				break;
			}
			strcpy(buff, tmp);
			buff += i;
			len += len;
		}
		need_delim++;
	}

	if (pat & PCMD_VERS1) {
		char *del;
		if (need_delim) {
			del = "/";
		} else {
			del = NULL;
		}

		for (n = 0; n < vers1->ns && len < bound; n++) {
			if (vers1->pi[n] == NULL ||
			    vers1->pi[n] == '\0' ||
			    vers1->pi[n] == (u_char *)'\177') {
#if defined(PCMCIA_DEBUG)
				if (pcmcia_debug)
					printf("\tvers1->pi[%d]=[%s]\n",
						n, vers1->pi[n]);
#endif
				continue;
			}
			i = strlen((char *) vers1->pi[n]);
			if ((len + i + 1) > bound)
				break;
			if (del != NULL) {
				*buff++ = *del;
				len++;
			}
			strcpy(buff, (char *) vers1->pi[n]);
			buff += i;
			len += i;
			del = " ";
		}
		need_delim++;
	}

	if (pat & PCMD_DEVTYPE) {
		char *del;
		int t;
		/*
		 * does this make sense or should it be cftable stuff???
		 * we also have to try both _A and _C forms.
		 * multifunction really breaks down on this.  Hope
		 * this isn't needed for them (there better be
		 * function ids).
		 */
		if (need_delim) {
			del = "/";
		} else {
			del = "";
		}

		if (device->cistpl_device_node[0].type <=
		    CISTPL_DEVICE_DTYPE_EXTEND)
			t = device->cistpl_device_node[0].type;
		else
			t = 0;
		sprintf(tmp, "%s%s", del, pcmcia_devtuple_type[t]);
		i = strlen(tmp);
		if ((len + i + 1) < bound) {
			strcpy(buff, tmp);
			buff += i;
			len += i;
		}
	}

	if (len == strlen(PCMDEV_PREFIX))
		len = 0;

	return (len);
}

/*
 * pcm_identify_device()
 *	identify the card found in the socket
 *	the basic algorithm is:
 *	if no CIS present: assume it is the default device type
 *	if a CIS is present, look for a driver that has an
 *	alias that matches one of the ones generated from the CIS.
 *	start with a very specific string and work down to more
 *	general.  The string is based on values from tuples in the
 *	following string tuple sequence:
 *		<funcid, funce, manfid, vers_1>
 *		<funcid, manfid, vers_1>
 *		<funcid, vers_1>
 *		<manfid, vers_1>
 *		<vers_1>
 *		<funcid, funce>
 *		<funcid>
 *		<devtype, ioaddr...>
 *	this is all subject to change
 */

int
pcm_identify_device(int socket, struct pcm_device_info *info,
		    char *driver, char *ident)
{
	int i;
	int which = 0;
	char namebuff[2*MODMAXNAMELEN];
	int namelen;
	client_handle_t client;
	tuple_t tuple;
	cisinfo_t cisinfo;
	int non_dev_pat = 0;
	int have_long_pat = 0;

				/* structures for tuple parsing. */
	cistpl_vers_1_t vers1;
	cistpl_funcid_t funcid;
	cistpl_manfid_t manfid;
	cistpl_funce_t funce;
	cistpl_jedec_t jedec;
	cistpl_device_t device;

	client = info->pd_handle;

	tuple.Socket = socket;
	cisinfo.Socket = socket;

#if defined(PCMCIA_DEBUG)
	if (pcmcia_debug)
		printf("pcm_identify_device(%d, %x)\n", socket, (int)info);
#endif

	if (info != NULL) {
		/* default card information */
		info->pd_socket = socket;
		info->pd_nodeid = pcm_get_nodeid(socket);
		info->pd_type = PCM_TYPE_MEMORY;
	}

	if ((i = pcmcia_card_services(ValidateCIS, client, &cisinfo)) !=
	    SUCCESS || cisinfo.Tuples == 0) {
				/* no CIS so assume default */
		strcpy(driver, pcm_default_drv);
		*ident = '\0';
		return (1);
	}

	/*
	 * find and parse all tuples defined above
	 * record which ones exist so that we can
	 * skip those that don't.
	 * Note that we may not know the true card type
	 * if a function id isn't present.
	 *
	 * if there are both AM and CM versions defined,
	 * look for AM first and then CM if there isn't an AM
	 * this will break for multifunction cards in some cases
	 */

	/* Version 1 strings */

	tuple.DesiredTuple = CISTPL_VERS_1;
	if ((i = pcmcia_card_services(GetFirstTuple, client, &tuple)) ==
	    SUCCESS) {
		i = pcmcia_card_services(ParseTuple, client, &tuple,
					    &vers1, NULL);
		if (i != SUCCESS)
			cmn_err(CE_WARN, "CIS parse failed VERS_1");
		else
			which |= PCMD_VERS1;
	}

	/* Function ID */

	tuple.DesiredTuple = CISTPL_FUNCID;
	if ((i = pcmcia_card_services(GetFirstTuple, client, &tuple)) ==
	    SUCCESS) {
		i = pcmcia_card_services(ParseTuple, client, &tuple,
					    &funcid, NULL);
		if (i != SUCCESS)
			cmn_err(CE_WARN, "CIS parse failed FUNCID");
		else {
			which |= PCMD_FUNCID;
			info->pd_type = funcid.function;
		}
	}

	/* Manufacturer's ID */

	tuple.DesiredTuple = CISTPL_MANFID;
	if ((i = pcmcia_card_services(GetFirstTuple, client, &tuple)) ==
	    SUCCESS) {
		i = pcmcia_card_services(ParseTuple, client, &tuple,
					    &manfid, NULL);
		if (i != SUCCESS)
			cmn_err(CE_WARN, "CIS parse failed MANFID");
		else
			which |= PCMD_MANFID;
	}

	/* JEDEC ID */

	tuple.DesiredTuple = CISTPL_JEDEC_A;
	if ((i = pcmcia_card_services(GetFirstTuple, client, &tuple)) ==
	    SUCCESS) {
		i = pcmcia_card_services(ParseTuple, client, &tuple,
					    &jedec, NULL);
		if (i != SUCCESS)
			cmn_err(CE_WARN, "CIS parse failed JEDEC_A");
		else
			which |= PCMD_JEDEC;
	} else {
		tuple.DesiredTuple = CISTPL_JEDEC_C;
		if ((i = pcmcia_card_services(GetFirstTuple, client, &tuple)) ==
		    SUCCESS) {
			i = pcmcia_card_services(ParseTuple, client, &tuple,
					    &jedec, NULL);
			if (i != SUCCESS)
				cmn_err(CE_WARN, "CIS parse failed JEDEC_C");
			else
				which |= PCMD_JEDEC;
		}
	}

	/* DEVICE ID */

	tuple.DesiredTuple = CISTPL_DEVICE_A;
	if ((i = pcmcia_card_services(GetFirstTuple, client, &tuple)) ==
	    SUCCESS) {
		i = pcmcia_card_services(ParseTuple, client, &tuple,
					    &device, NULL);
		if (i != SUCCESS)
			cmn_err(CE_WARN, "CIS parse failed DEVICE_A");
		else
			which |= PCMD_DEVTYPE;
	} else {
		tuple.DesiredTuple = CISTPL_DEVICE;
		if ((i = pcmcia_card_services(GetFirstTuple, client, &tuple)) ==
		    SUCCESS) {
			i = pcmcia_card_services(ParseTuple, client, &tuple,
					    &device, NULL);
			if (i != SUCCESS)
				cmn_err(CE_WARN, "CIS parse failed DEVICE");
			else
				which |= PCMD_DEVTYPE;
		}
	}

	info->pd_tuples = which;

	/*
	 * now construct strings that are to be used
	 * to find driver aliases and look for a driver.
	 * quit when we find a driver
	 */
	for (i = 0; i < pcm_num_search; i++) {
		major_t major;
#if defined(PCMCIA_DEBUG)
		if (pcmcia_debug)
			printf("\twhich=%x & pcm_search_pat[%d]=%x\n",
				which, i, pcm_search_pat[i]);
#endif
		namelen = pcm_make_dev_string(namebuff,
						which & pcm_search_pat[i],
						sizeof (namebuff),
						&funcid, &manfid, &funce,
						&jedec, &vers1, &device);
#if defined(PCMCIA_DEBUG)
		if (pcmcia_debug)
			printf("pcm: trying namebuff=[%s] strlen(namebuff)=%d "
					"namelen=%d\n",
					namebuff, strlen(namebuff), namelen);
#endif
		if (!have_long_pat && namelen > 0) {
			/*
			 * we want to keep the longest name string
			 * around for possible identification purposes
			 * so copy this string into the "ident"
			 * pointer.  The 0th entry is most specific.
			 */
			strncpy(ident, namebuff, MODMAXNAMELEN * 2);

#if defined(PCMCIA_DEBUG)
			if (pcmcia_debug)
			    cmn_err(CE_CONT, "pcm: ident=[%s] strlen(ident)=%d "
				"namebuff=[%s] strlen(namebuff)=%d "
				"MODMAXNAMELEN *2 = %d\n",
				ident, strlen(ident), namebuff,
				strlen(namebuff),
				MODMAXNAMELEN *2);
#endif

			have_long_pat++;
		}

		if (namelen > 0 &&
		    (which & pcm_search_pat[i]) != PCMD_DEVTYPE)
			non_dev_pat++;

		if (namelen > (2 *MODMAXNAMELEN)) {
			/* truncate to fit module space */
			namelen = MODMAXNAMELEN;
			namebuff[namelen] = '\0';
		}

		if (namelen > 0 &&
		    ((major = ddi_name_to_major(namebuff)) != -1)) {
			strcpy(driver, ddi_major_to_name(major));
			return (1);
		}
		if (major == (major_t) -1 && namelen > MODMAXNAMELEN) {
			namelen = MODMAXNAMELEN;
			namebuff[MODMAXNAMELEN] = '\0';
			if (namelen > 0 &&
			    ((major = ddi_name_to_major(namebuff)) != -1)) {
				strcpy(driver, ddi_major_to_name(major));
				return (1);
			}
		}
	}
	if (!non_dev_pat) {
		/*
		 * need to determine if this is really a memory
		 * card or if it is an I/O card that just doesn't
		 * have enough valid properties defined to uniquely
		 * identify it.
		 */
		non_dev_pat = 0;
	}

	strcpy(driver, pcm_unknown_drv);
	return (0);
}

/*
 * pcm_pathname()
 *	make a partial path from dip.
 *	used to mknods relative to /devices/pcmcia/
 *
 * XXX - we now use ddi_get_name_addr to get the "address" portion
 *	of the name; that way, we only have to modify the name creation
 *	algorithm in one place
 */
void
pcm_pathname(dev_info_t *dip, char *name, char *path)
{
	sprintf(path, "%s@%s:%s", ddi_get_name(dip),
				ddi_get_name_addr(dip), name);
}

/*
 * pcm_patch_devinfo(dip)
 *	If a driver is modunloaded while the card is still
 *	present, the properties are destroyed and modload
 *	and attach will fail later.  We need to put all the properties
 *	back in if they don't exist.
 */
static void
pcm_patch_devinfo(dev_info_t *dip)
{
	int sock;
	struct ddi_parent_private_data *ppd;
	struct regspec *regs;

	if ((sock = ddi_getprop(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
				PCM_DEV_SOCKET, -1)) != -1) {
		return;
	}
	/*
	 * we have had the socket removed so the card is present
	 * since it is never removed by the pcmcia framework.
	 * Go find the socket and patch all the properties back.
	 */
	ppd = (struct ddi_parent_private_data *)
		ddi_get_parent_data(dip);

	if (ppd == NULL || ppd->par_nreg == 0)
		return;

	regs = ppd->par_reg;
	sock = regs->regspec_bustype; /* we tucked the socket here */
	if (sock >= pcmcia_num_sockets) {
		/* we can't find it so not really present */
		return;
	}
	/* reconstruct the socket property */
	(void) ddi_prop_create(DDI_DEV_T_NONE, dip, 0,
					PCM_DEV_SOCKET,
					(caddr_t) &sock,
					sizeof (int));

	if (pcmcia_sockets[sock].ls_dip == dip) {
		/* card is also active since we found it */
		(void) ddi_prop_create(DDI_DEV_T_NONE, dip,
					0, PCM_DEV_ACTIVE,
					NULL, 0);
	}
	/* P1275 property */
	(void) ddi_prop_create(DDI_DEV_T_NONE, dip, 0,
					PCM_DEV_TYPE, NULL, 0);
}

/*
 * pcmcia_create_device()
 * 	create the /devices entries for the driver
 *	it is assumed that the PC Card driver will do a
 *	RegisterClient for each subdevice.
 * 	The device type string is encoded here to match
 *	the standardized names when possible.
 * XXX - note that we may need to provide a way for the
 *	caller to specify the complete name string that
 *	we pass to ddi_set_name_addr
 */
/* ARGSUSED */
pcmcia_create_device(dev_info_t *dip, ss_make_device_node_t *init)
{
	int err = SUCCESS;
	struct pcm_make_dev device;

	/*
	 * Now that we have the name, create it.
	 */

	bzero((caddr_t) &device, sizeof (device));
	if (init->flags & SS_CSINITDEV_CREATE_DEVICE) {
		if ((err = ddi_create_minor_node(init->dip,
						    init->name,
						    init->spec_type,
						    init->minor_num,
						    init->node_type,
						    0)) != DDI_SUCCESS) {
#if defined(PCMCIA_DEBUG)
			if (pcmcia_debug)
				printf("pcmcia_create_device: failed create\n");
#endif
			return (BAD_ATTRIBUTE);
		}
		(void) pcm_pathname(init->dip, init->name, device.path);
#if defined(PCMCIA_DEBUG)
		if (pcmcia_debug)
			printf("pcmcia_create_device: created %s\n",
				device.path);
#endif
		device.dev =
			makedevice(ddi_name_to_major(ddi_get_name(init->dip)),
				    init->minor_num);
		device.flags |= (init->flags & SS_CSINITDEV_MORE_DEVICES) ?
			PCM_EVENT_MORE : 0;
		device.type = init->spec_type;
		device.op = SS_CSINITDEV_CREATE_DEVICE;
		device.socket = ddi_getprop(DDI_DEV_T_ANY, init->dip,
					    DDI_PROP_CANSLEEP, PCM_DEV_SOCKET,
					    -1);
	} else if (init->flags & SS_CSINITDEV_REMOVE_DEVICE) {
		device.op = SS_CSINITDEV_REMOVE_DEVICE;
		device.socket = ddi_getprop(DDI_DEV_T_ANY, init->dip,
					    DDI_PROP_CANSLEEP, PCM_DEV_SOCKET,
					    -1);
		if (init->name != NULL)
			strcpy(device.path, init->name);
		device.dev =
			makedevice(ddi_name_to_major(ddi_get_name(init->dip)),
				    0);
		ddi_remove_minor_node(init->dip, init->name);
	}

	/*
	 *	we send an event for ALL devices created.
	 *	To do otherwise ties us to using drvconfig
	 *	forever.  There are relatively few devices
	 *	ever created so no need to do otherwise.
	 *	The existence of the event manager must never
	 *	be visible to a PCMCIA device driver.
	 */
	pcm_event_manager(PCE_INIT_DEV, device.socket, &device);

	return (err);
}

/*
 * pcmcia_get_minors()
 *	We need to traverse the minor node list of the
 *	dip if there are any.  This takes two passes;
 *	one to get the count and buffer size and the
 *	other to actually copy the data into the buffer.
 *	The framework requires that the dip be locked
 *	during this time to avoid breakage
 */

int
pcmcia_get_minors(dev_info_t *dip, struct pcm_make_dev **minors)
{
	int count = 0;
	struct ddi_minor_data *dp;
	struct pcm_make_dev *md;
	int socket;

	socket = ddi_getprop(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
				    PCM_DEV_SOCKET, -1);
	mutex_enter(&(DEVI(dip)->devi_lock));
	if (DEVI(dip)->devi_minor != (struct ddi_minor_data *)NULL) {
		for (dp = DEVI(dip)->devi_minor;
			dp != (struct ddi_minor_data *)NULL;
			dp = dp->next) {
			count++; /* have one more */
		}
		/* we now know how many nodes to allocate */
		md = kmem_zalloc(count * sizeof (struct pcm_make_dev),
				    KM_NOSLEEP);
		if (md != NULL) {
			*minors = md;
			for (dp = DEVI(dip)->devi_minor;
				dp != (struct ddi_minor_data *)NULL;
				dp = dp->next, md++) {
				md->socket = socket;
				md->op = SS_CSINITDEV_CREATE_DEVICE;
				md->dev = dp->ddm_dev;
				md->type = dp->ddm_spec_type;
				pcm_pathname(dip, dp->ddm_name, md->path);
			}
		} else {
			count = 0;
		}
	}
	mutex_exit(&(DEVI(dip)->devi_lock));
	return (count);
}
