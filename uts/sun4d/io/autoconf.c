/*
 * Copyright (c) 1991-1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)autoconf.c	1.71	94/11/10 SMI"

/*
 * Setup the system to run on the current machine.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/cmn_err.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/bootconf.h>
#include <sys/ethernet.h>
#include <sys/kmem.h>
#include <sys/cpu.h>
#include <sys/cpuvar.h>
#include <sys/idprom.h>
#include <sys/debug.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/esunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/promif.h>
#include <sys/obpdefs.h>
#include <sys/modctl.h>
#include <sys/hwconf.h>
#include <sys/avintr.h>
#include <sys/autoconf.h>
#include <sys/instance.h>
#include <sys/systeminfo.h>
#include <sys/fpu/fpusystm.h>
#include <sys/syserr.h>

/*
 * External Functions (should be in header files)
 */
extern int mem_unit_good(char *status);
extern void halt(char *);	/* fix syserr.h - should be void */

#if !defined(SAS) && !defined(MPSAS)

/*
 * Local functions
 */
static int reset_leaf_device(dev_info_t *, void *);

static int getlongprop_buf(int id, char *name, char *buf, int maxlen);
static void add_root_props(dev_info_t *devi);
static int get_neighbors(dev_info_t *, caddr_t);
static void parse_idprom(void);
static u_int softlevel1(caddr_t);

static void di_dfs(dev_info_t *, int (*)(dev_info_t *, caddr_t), caddr_t);
static int check_status(int id, char *name, dev_info_t *parent);
static int  get_boardnum(int, dev_info_t *);

/*
 * The following several variables are related to
 * the configuration process, and are used in initializing
 * the machine.
 */
dev_info_t *top_devinfo;
idprom_t idprom;

int sun4d_model = MODEL_UNKNOWN;

#define	NAME_SC2000 "SUNW,SPARCcenter-2000"
#define	NAME_SC1000 "SUNW,SPARCserver-1000"
#define	BUFLEN 24

/*
 * The Presto driver relies on n_xdbus.
 */
u_int n_xdbus;				/* number XDbuses on system */
int good_xdbus = -1;

#endif	/* !SAS && !MPSAS */

/*
 * Machine type we are running on.
 */
short cputype;

#define	CONFIGDEBUG

#ifdef	CONFIGDEBUG

static int configdebug = 0;

#define	CPRINTF(x)		if (configdebug) printf(x)
#define	CPRINTF1(x, a)		if (configdebug) printf(x, a)
#define	CPRINTF2(x, a, b)	if (configdebug) printf(x, a, b)
#define	CPRINTF3(x, a, b, c)	if (configdebug) printf(x, a, b, c)

#else

#define	CPRINTF(x)
#define	CPRINTF1(x, a)
#define	CPRINTF2(x, a, b)
#define	CPRINTF3(x, a, b, c)

#endif

/*
 * Return the favoured drivers of this implementation
 * architecture.  These drivers MUST be present for
 * the system to boot at all.
 *
 * XXX - rootnex must be loaded before options because of the ddi
 *	 properties implementation.
 *
 * Used in loadrootmodules() in the swapgeneric module.
 */
char *
get_impl_module(int first)
{
	static char **p;
	static char *impl_module_list[] = {
		"rootnex",
		"options",
		"dma",
		"sad",
		(char *)0
	};

	if (first)
		p = impl_module_list;
	if (*p != (char *)0)
		return (*p++);
	else
		return ((char *)0);
}

/*
 * Configure the hardware on the system.
 * Called before the rootfs is mounted
 */
void
configure(void)
{
	register int major;
	register dev_info_t *dip;

	/* We better have released boot by this time! */

	ASSERT(!bootops);

	/*
	 * Initialize the machine type
	 */
	parse_idprom();

	/*
	 * Determine if an FPU is attached
	 */

#ifndef	MPSAS	/* no fpu module yet in MPSAS */
	fpu_probe();
#endif
	if (!fpu_exists) {
		printf("No FPU in configuration\n");
	}

	/*
	 * This following line fixes bugid 1041296; we need to do a
	 * prom_nextnode(0) because this call ALSO patches the DMA+
	 * bug in Campus-B and Phoenix. The prom uncaches the traptable
	 * page as a side-effect of devr_next(0) (which prom_nextnode calls),
	 * so this *must* be executed early on.
	 */
	(void) prom_nextnode((dnode_t)0);

	/*
	 * Initialize devices on the machine.
	 * Uses configuration tree built by the PROMs to determine what
	 * is present, and builds a tree of prototype dev_info nodes
	 * corresponding to the hardware which identified itself.
	 */
#if !defined(SAS) && !defined(MPSAS)
	/*
	 * Record that devinfos have been made for "rootnex."
	 */
	major = ddi_name_to_major("rootnex");
	devnamesp[major].dn_flags |= DN_DEVI_MADE;

	/*
	 * Create impl. specific root node properties...
	 */
	add_root_props(top_devinfo);

	/*
	 * Set the name part of the address to make the root conform
	 * to canonical form 1.  (Eliminates special cases later).
	 */
	dip = ddi_root_node();
	if (impl_ddi_sunbus_initchild(dip) != DDI_SUCCESS)
		cmn_err(CE_PANIC, "Could not initialize root nexus");


#ifdef	DDI_PROP_DEBUG
	(void) ddi_prop_debug(1);	/* Enable property debugging */
#endif	DDI_PROP_DEBUG

#endif	/* !SAS && !MPSAS */
}

/*
 * This routine transforms either a prototype or canonical form 1 dev_info
 * node into a canonical form 2 dev_info node.  If the transformation fails,
 * the node is removed.
 */
int
impl_proto_to_cf2(dev_info_t *dip)
{
	int error, circular;
	struct dev_ops *ops;
	register major_t major;
	register struct devnames *dnp;

	if ((major = ddi_name_to_major(ddi_get_name(dip))) == -1)
		return (DDI_FAILURE);

	if ((ops = mod_hold_dev_by_major(major)) == NULL)
		return (DDI_FAILURE);

	/*
	 * Wait for or get busy/changing.  We need to stall here because
	 * of the alternate path for h/w devinfo nodes.
	 */

	dnp = &(devnamesp[major]);
	LOCK_DEV_OPS(&(dnp->dn_lock));

	/*
	 * Is this thread already installing this driver?
	 * If yes, mark it as a circular dependency and continue.
	 * If not, wait for other threads to finish with this driver.
	 */
	if (DN_BUSY_CHANGING(dnp->dn_flags) &&
	    (dnp->dn_busy_thread == curthread))  {
		dnp->dn_circular++;
	} else {
		while (DN_BUSY_CHANGING(dnp->dn_flags))
			cv_wait(&(dnp->dn_wait), &(dnp->dn_lock));
		dnp->dn_flags |= DN_BUSY_LOADING;
		dnp->dn_busy_thread = curthread;
	}
	circular = dnp->dn_circular;
	UNLOCK_DEV_OPS(&(dnp->dn_lock));

	/*
	 * If it's a prototype node, transform to CF1.
	 */
	if ((error = ddi_initchild(ddi_get_parent(dip), dip)) != DDI_SUCCESS) {
		/*
		 * Retain h/w devinfos, eliminate .conf file devinfos
		 */
		if (ddi_get_nodeid(dip) == DEVI_PSEUDO_NODEID)
			(void) ddi_remove_child(dip, 0);
		if (error == DDI_NOT_WELL_FORMED)	/* An artifact ... */
			error = DDI_FAILURE;
		ddi_rele_driver(major);
		goto out;
	}

	if (!DDI_CF2(dip)) {
		DEVI(dip)->devi_ops = ops;
		if ((error = impl_initdev(dip)) == DDI_SUCCESS) {
			LOCK_DEV_OPS(&(dnp->dn_lock));
			dnp->dn_flags |= DN_DEVS_ATTACHED;
			UNLOCK_DEV_OPS(&(dnp->dn_lock));
		}
		/*
		 * Driver Release/remove child done in impl_initdev!
		 * (for error case.)
		 */
		goto out;
	}

	/*
	 * This assert replaces some code to make sure the driver is
	 * actually attached to the dip -- it had better be at this point.
	 */
	ASSERT(ddi_get_driver(dip) == ops);

out:
	LOCK_DEV_OPS(&(dnp->dn_lock));
	if (circular)
		dnp->dn_circular--;
	else  {
		dnp->dn_flags &= ~(DN_BUSY_CHANGING_BITS);
		dnp->dn_busy_thread = NULL;
		cv_broadcast(&(dnp->dn_wait));
	}
	UNLOCK_DEV_OPS(&(dnp->dn_lock));
	return (error);
}

/*ARGSUSED*/
int
impl_check_cpu(dev_info_t *devi)
{
	return (DDI_SUCCESS);
}

/*
 * XXX	Why is this an impl function?
 */
int
impl_probe_attach_devi(dev_info_t *dev)
{
	register int r;

	if (devi_identify(dev) != DDI_IDENTIFIED)
		return (DDI_FAILURE);

	switch (r = devi_probe(dev)) {
	case DDI_PROBE_DONTCARE:
	case DDI_PROBE_SUCCESS:
		break;
	default:
		return (r);
	}

	return (devi_attach(dev, DDI_ATTACH));
}

/*
 * On sun4d, we have an interesting situation -- because the system
 * boards include a bootbus with zs devices, and the boards are pluggable,
 * and the bootbus path can change if we remove the 'A' cpu module, the
 * instance number code gets confused and assigns incorrect and different
 * zs instance numbers.  We make out own assignments based on board# and
 * zs instance # within the board (0 or 1).  If the inherited property
 * board# is not defined, we can infer it from the device-id property.
 *
 * Note use of ddi_getprop to bypass unattached drivers prop_op(9e)
 * function.
 *
 * Note:  The algortithm used fixes ttya/b to board#0, ttyc/d to board#1, ...
 */

/*
 * Turn this off (before FCS) when "board#" property is propagated to all proms.
 */
#define	PROM_BOARD_NUM_WORKAROUND	1


#define	ZS_SLAVE	"slave"
#define	BOARD		"board#"
#define	DEVICE_ID	"device-id"	/* Only used with board# workaraond */

static int
sun4d_assign_zs_instance(dev_info_t *dev)
{
	int	board, slave;
	static char *pmsg = "Unable to determine board# for <%s>";

#ifdef	PROM_BOARD_NUM_WORKAROUND
	int	deviceid;
#endif	PROM_BOARD_NUM_WORKAROUND

	/*
	 * Get inherited properties for slave (zs local instance # 0 or 1),
	 * board# from PROM, and default to using f(device-id) property
	 * value to compute board#.  We expect slave to be defined locally,
	 * and default to value zero if we don't find it, but this is
	 * probably a reasonable assumption to expect it to be defined.
	 *
	 * Software zs nodes should have already been merged into h/w nodes.
	 */

	ASSERT(strcmp(ddi_get_name(dev), "zs") == 0);
	ASSERT(ddi_get_nodeid(dev) != DEVI_PSEUDO_NODEID);

	slave = ddi_getprop(DDI_DEV_T_ANY, dev, DDI_PROP_DONTPASS, ZS_SLAVE, 0);
	board = ddi_getprop(DDI_DEV_T_ANY, dev, 0, BOARD, -1);

#ifdef	PROM_BOARD_NUM_WORKAROUND
	if (board == -1)  {
		deviceid = ddi_getprop(DDI_DEV_T_ANY, dev, 0, "device-id", -1);
		if (deviceid != -1)  {
			/* From architecture specification */
			board = deviceid >> 4;
		}
	}
#endif	PROM_BOARD_NUM_WORKAROUND

	if (board == -1)  {
		cmn_err(CE_PANIC, pmsg, ddi_get_name_addr(dev));
		/*NOTREACHED*/
	}

	/*
	 * Instance# = board# * (number of zs per board) + duart instance
	 * Instance# = (board * 2) + slave;
	 */
	return ((board << 1) + slave);
}
#undef	ZS_SLAVE
#undef	BOARD
#undef	DEVICE_ID

/*
 * This routine transforms a canonical form 1 dev_info node into a
 * canonical form 2 dev_info node.  If the transformation fails, the
 * node is removed.
 */
int
impl_initdev(dev_info_t *dev)
{
	register struct dev_ops *ops;
	register int r;
	register int is_zs_device;
	register int is_cpu_device;

	ops = ddi_get_driver(dev);
	ASSERT(ops);
	ASSERT(DEV_OPS_HELD(ops));

	/*
	 * 1100913: For sun4d (ONLY), zs devices, don't let the ddi instance
	 * number code assign the instance #, we'll do it based on board#.
	 */
	is_zs_device = (strcmp(DEVI(dev)->devi_name, "zs") == 0);

	/*
	 * 1140626: For sun4d cpu devices, don't let the ddi instance
	 * number code assign the instance #, as it is assigned based
	 * on the cpu-id in the attach routine.
	 */
	is_cpu_device = ((strcmp(DEVI(dev)->devi_name, "cpu") == 0) ||
			(strcmp(DEVI(dev)->devi_name, "TI,TMS390Z55") == 0));

	/*
	 * assign the instance number
	 */
	if (is_zs_device)
		DEVI(dev)->devi_instance = sun4d_assign_zs_instance(dev);
	else if (is_cpu_device)
		DEVI(dev)->devi_instance = 0;
	else
		DEVI(dev)->devi_instance = e_ddi_assign_instance(dev);

	if ((r = impl_probe_attach_devi(dev)) == DDI_SUCCESS)  {
		if ((!is_zs_device) && (!is_cpu_device))
			e_ddi_keep_instance(dev);
		return (r);
	}

	/*
	 * Partial probe or failed probe/attach...
	 * Retain leaf device driver nodes for deferred attach.
	 * (We need to retain the assigned instance number for
	 * deferred attach.  The call to e_ddi_free_instance is
	 * advisory -- it will retain the instance number if it's
	 * ever been kept before.)
	 */
	ddi_set_driver(dev, NULL);		/* dev --> CF1 */
	ddi_rele_driver(ddi_name_to_major(ddi_get_name(dev)));
	if (!NEXUS_DRV(ops))  {
		if (!is_zs_device)
			e_ddi_keep_instance(dev);
	} else {
		if (!is_zs_device)
			e_ddi_free_instance(dev);
		(void) ddi_uninitchild(dev);
		/*
		 * Retain h/w nodes in prototype form.
		 */
		if (ddi_get_nodeid(dev) == DEVI_PSEUDO_NODEID)
			(void) ddi_remove_child(dev, 0);
	}

	return (r);
}

/*
 * Reset all the pure leaf drivers on the system at halt time
 * We deliberately skip children of the 'pseudo' nexus, as they
 * don't have any hardware to reset.
 */
void
reset_leaves(void)
{
	ddi_walk_devs(top_devinfo, reset_leaf_device, 0);
}

/*ARGSUSED1*/
static int
reset_leaf_device(dev_info_t *dev, void *arg)
{
	struct dev_ops *ops;

	if (DEVI(dev)->devi_nodeid == DEVI_PSEUDO_NODEID)
		return (DDI_WALK_PRUNECHILD);

	if ((ops = DEVI(dev)->devi_ops) != (struct dev_ops *)0 &&
	    ops->devo_cb_ops != 0 && ops->devo_reset != nodev) {
		CPRINTF2("resetting %s%d\n", ddi_get_name(dev),
			ddi_get_instance(dev));
		(void) devi_reset(dev, DDI_RESET_FORCE);
	}

	return (DDI_WALK_CONTINUE);
}

static int
getlongprop_buf(int id, char *name, char *buf, int maxlen)
{
	int size;

	size = prom_getproplen((dnode_t)id, name);
	if (size <= 0 || (size > maxlen - 1))
		return (-1);

	if (-1 == prom_getprop((dnode_t)id, name, buf))
		return (-1);

	/*
	 * Workaround for bugid 1085575 - OBP may return a "name" property
	 * without null terminating the string with '\0'.  When this occurs,
	 * append a '\0' and return (size + 1).
	 */
	if (strcmp("name", name) == 0) {
		if (buf[size - 1] != '\0') {
			buf[size] = '\0';
			size += 1;
		}
	}
	return (size);
}

/*
 * We set the cpu type from the idprom, if we can.
 * Note that we just read out the contents of it, for the most part.
 * Except for cputype, sigh.
 */
void
setcputype(void)
{
	/*
	 * We cache the idprom info early on so that we don't
	 * rummage thru the NVRAM unnecessarily later.
	 */
	if (prom_getidprom((caddr_t)&idprom, sizeof (idprom)) == 0 &&
	    idprom.id_format == IDFORM_1)
		cputype = idprom.id_machine;
	else {
		/*
		 * Plain ole Paranoia ..
		 */
		prom_printf("Machine type set to OBP architecture\n");
		idprom.id_format = 0;	/* Make sure later tests fail */
		cputype = OBP_ARCH;
	}

	/*
	 * set global n_xdbus from root node property
	 */
	if (-1 == prom_getprop((dnode_t)0, "n-xdbus", (caddr_t)&n_xdbus)) {
		n_xdbus = 0;
		sun4d_model = MODEL_UNKNOWN;
	} else if (n_xdbus == 2) {
		sun4d_model = MODEL_SC2000;
	} else if (n_xdbus == 1) {

		char name[BUFLEN];

		if (-1 == prom_getprop((dnode_t)0, "name", name)) {
			sun4d_model = MODEL_UNKNOWN;
			return;
		}
		name[BUFLEN -1] = '\0';

		if (strncmp(name, NAME_SC1000, BUFLEN) == 0) {
			sun4d_model = MODEL_SC1000;
		} else if (strncmp(name, NAME_SC2000, BUFLEN) == 0) {

			int badbus;
			sun4d_model = MODEL_SC2000;

			if (-1 == prom_getprop((dnode_t)0, "disabled-xdbus",
			    (caddr_t)&badbus))
				good_xdbus = -1;
			else if (badbus == 0)
				good_xdbus = 1;
			else if (badbus == 1)
				good_xdbus = 0;
		} else
			halt(name);
	}
}

/*
 *  Here is where we actually infer meanings to the members of idprom_t
 */
static void
parse_idprom(void)
{
	if (idprom.id_format == IDFORM_1) {
		register int i;

		(void) localetheraddr((struct ether_addr *)idprom.id_ether,
		    (struct ether_addr *)NULL);

		i = idprom.id_machine << 24;
		i = i + idprom.id_serial;
		numtos((u_long) i, hw_serial);
	} else
		prom_printf("Invalid format code in IDprom.\n");
}


/*
 * Add and remove implementation specific software defined properties
 * for a device. Can be used to override or supplement any properties
 * derived from the prom. Almost by definition this is ugly.
 *
 * MJ: Should be in a separate machine specific file.
 */

/*
 * XXX: This will need another field to handle property undefs.
 * and non-wildcarded properties.
 */

struct prop_def {
	char	*prop_name;
	int	prop_len;
	caddr_t	prop_value;
};


/*
 * Add statically defined root properties to this list...
 */

static const int pagesize = PAGESIZE;
static const int mmu_pagesize = MMU_PAGESIZE;
static const int mmu_pageoffset = MMU_PAGEOFFSET;

static struct prop_def root_props[] = {
{ "PAGESIZE",		sizeof (int),		(caddr_t)&pagesize },
{ "MMU_PAGESIZE",	sizeof (int),		(caddr_t)&mmu_pagesize},
{ "MMU_PAGEOFFSET",	sizeof (int),		(caddr_t)&mmu_pageoffset}
};

#define	NROOT_PROPS	(sizeof (root_props) / sizeof (struct prop_def))

static void
add_root_props(dev_info_t *devi)
{
	int i;
	struct prop_def *rpp;

	for (i = 0, rpp = root_props; i < NROOT_PROPS; ++i, ++rpp) {
		(void) e_ddi_prop_create(DDI_DEV_T_NONE, devi, 0,
		    rpp->prop_name, rpp->prop_value, rpp->prop_len);
	}

	/*
	 * workaround properties go here...
	 */

	/*
	 * Create the root node boolean property
	 * corresponding to addressing type supported in the root node:
	 *
	 * Choices are:
	 *	"relative-addressing" (OBP PROMS)
	 *	"generic-addressing"  (Sun4 -- pseudo OBP/DDI)
	 */

	(void) e_ddi_prop_create(DDI_DEV_T_NONE, devi, 0,
	    DDI_RELATIVE_ADDRESSING, (caddr_t)0, 0);
}

#if 0	/* Not needed for sun4d */
static void
add_options_props(dev_info_t *dip)
{
	char buf[9], buf2[33];
	int nmade, i, j;

	/*
	 * Translate options property "sd-targets" to "sd-address-map",
	 * if it exists.
	 */
	bzero(buf, 9);
	bzero(buf2, 33);
	nmade = 8;
	if (ddi_getlongprop_buf(DDI_DEV_T_ANY, dip, DDI_PROP_DONTPASS,
	    "sd-targets", (caddr_t)buf, &nmade) != DDI_PROP_SUCCESS) {
		return;
	}

	i = j = 0;
	while (i < nmade && buf[i]) {
		buf2[j++] = buf[i++];
		buf2[j++] = 0;
	}
	(void) e_ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
	    "sd-address-map", buf2, j);
}

static void
add_esp_props(dev_info_t *dip)
{
	static int first = 1;
	if (cputype == CPU_SUN4C_60 && first) {
		(void) e_ddi_prop_create(DDI_DEV_T_NONE, dip,
		    DDI_PROP_CANSLEEP, "noisy-scsibus",
		    (caddr_t)&first, sizeof (first));
		first = 0;
	}
}

void
impl_add_dev_props(dev_info_t *dip)
{
	/*
	 * In this implementation, we only have a few
	 * root properties to deal with.
	 */
	if (dip == top_devinfo) {
		add_root_props(dip);
	} else if (strcmp(DEVI(dip)->devi_name, "options") == 0) {
		add_options_props(dip);
	} else if (strcmp(DEVI(dip)->devi_name, "esp") == 0) {
		add_esp_props(dip);
	}
}
#endif

void
impl_rem_dev_props(dev_info_t *dip)
{
	ddi_prop_remove_all(dip);
	e_ddi_prop_remove_all(dip);
}

/*
 * Allow for implementation specific correction of PROM property values.
 */

/*ARGSUSED*/
void
impl_fix_props(dev_info_t *dip, dev_info_t *ch_dip, char *name, int len,
    caddr_t buffer)
{
	/*
	 * There are no adjustments needed in this implementation.
	 */
}

#define	NAME_LEN 80

/*ARGSUSED1*/
static int
get_neighbors(dev_info_t *di, caddr_t arg)
{
	register int nid, snid, cnid;
	dev_info_t *parent;
	char buf[NAME_LEN];

	if (di == NULL)
		return (DDI_WALK_CONTINUE);

	nid = ddi_get_nodeid(di);
	snid = (int)prom_nextnode((dnode_t)nid);
	cnid = (int)prom_childnode((dnode_t)nid);

	if (snid && (snid != -1) && ((parent = ddi_get_parent(di)) != NULL)) {
		/*
		 * add the first sibling that passes check_status()
		 */
		for (; snid && (snid != -1);
		    snid = (int)prom_nextnode((dnode_t)snid)) {
			if (0 < getlongprop_buf(snid, OBP_NAME, buf,
			    NAME_LEN)) {
				if (check_status(snid, buf, parent) ==
				    DDI_SUCCESS) {
					(void) ddi_add_child(parent, buf,
					    snid, -1);
					break;
				}
			}
		}
	}

	if (cnid && (cnid != -1)) {
		/*
		 * add the first child that passes check_status()
		 */
		if (0 < getlongprop_buf(cnid, OBP_NAME, buf, NAME_LEN)) {
			if (check_status(cnid, buf, di) == DDI_SUCCESS) {
				(void) ddi_add_child(di, buf, cnid, -1);
			} else {
				for (cnid = (int)prom_nextnode((dnode_t)cnid);
				    cnid && (cnid != -1);
				    cnid = (int)prom_nextnode((dnode_t)cnid)) {
					if (0 < getlongprop_buf(cnid, OBP_NAME,
					    buf, NAME_LEN)) {
						if (check_status(cnid, buf, di)
						    == DDI_SUCCESS) {
							(void) ddi_add_child(
							    di, buf, cnid, -1);
							break;
						}
					}
				}
			}
		}
	}

	return (DDI_WALK_CONTINUE);
}

#define	XVECTOR(n)		\
int	xlvl##n##_spurious;	\
struct autovec xlvl##n[NVECT]

#define	VECTOR(n)		\
int	level##n##_spurious;	\
struct autovec level##n[NVECT]

/*ARGSUSED*/
static u_int
softlevel1(caddr_t arg)
{
	/* XXX what about sun4m's unsafe_driver mumbo-jumbo? */
	softint();
	return (1);	/* so locore believes we handled it */
}

/*
 * These structures are used in locore.s to jump to device interrupt routines.
 * They also provide vmstat assistance.
 * They will index into the string table generated by autoconfig
 * but in the exact order addintr sees them. This allows IOINTR to quickly
 * find the right counter to increment.
 * (We use the fact that the arrays are initialized to 0 by default).
 */

/*
 * Initial interrupt vector information.
 * Each of these macros defines both the "spurious-int" counter and
 * the list of autovec structures that will be used by locore.s
 * to distribute interrupts to the interrupt requestors.
 * Each list is terminated by a null.
 * Lists are scanned only as needed: hard ints
 * stop scanning when the int is claimed; soft ints
 * scan the entire list. If nobody on the list claims the
 * interrupt, then a spurious interrupt is reported.
 *
 * These should all be initialized to zero, except for the
 * few interrupts that we have handlers for built into the
 * kernel that are not installed by calling "addintr".
 * I would like to eventually get everything going through
 * the "addintr" path.
 * It might be a good idea to remove VECTORs that are not
 * actually processed by locore.s
 */

/*
 * We cannot differentiate onboard or S-Bus interrupts on sun4c
 * No VME interrupts on sun4c (or sun4d)
 */

/*
 * software vectored interrupts:
 *
 * Level1 is special (softcall handler), so we initialize it to always
 * call softlevel1 first.
 * Only levels 1, 4, and 6 are allowed in sun4c, as the others cannot be
 * generated.
 */

XVECTOR(1) = {	{syserr_handler, (caddr_t)1},
		{softlevel1},		/* time-scheduled tasks */
		{0}};
XVECTOR(2) = {{0}};			/* not possible for sun4c */
XVECTOR(3) = {{0}};			/* not possible for sun4c */
XVECTOR(4) = {{syserr_handler, (caddr_t)4}, {0}};
XVECTOR(5) = {{0}};			/* not possible for sun4c */
XVECTOR(6) = {{0}};
XVECTOR(7) = {{0}};			/* not possible for sun4c */
XVECTOR(8) = {{syserr_handler, (caddr_t)8}, {0}};
XVECTOR(9) = {{0}};			/* not possible for sun4c */
XVECTOR(10) = {{0}};			/* not possible for sun4c */
XVECTOR(11) = {{0}};			/* not possible for sun4c */
XVECTOR(12) = {{0}};			/* not possible for sun4c */
XVECTOR(13) = {{0}};			/* not possible for sun4c */
XVECTOR(14) = {{0}};			/* not possible for sun4c */
XVECTOR(15) = {{0}};			/* not possible for sun4c */

/*
 * For the sun4m, these are "otherwise unclaimed sparc interrupts", but for
 * us, they're all hardware interrupts
 */

VECTOR(1) = {{0}};
VECTOR(2) = {{0}};
VECTOR(3) = {{0}};
VECTOR(4) = {{0}};
VECTOR(5) = {{0}};
VECTOR(6) = {{0}};
VECTOR(7) = {{0}};
VECTOR(8) = {{0}};
VECTOR(9) = {{0}};
VECTOR(10) = {{0}};
VECTOR(11) = {{0}};
VECTOR(12) = {{0}};
VECTOR(13) = {{0}};
VECTOR(14) = {{0}};
VECTOR(15) = {{0}};

/*
 * indirection table, to save us some large switch statements
 * And so we can share avintr.c with sun4m, which actually uses large tables.
 * NOTE: This must agree with "INTLEVEL_foo" constants in
 *	<sun/autoconf.h>
 */
struct autovec *const vectorlist[] = {
/*
 * otherwise unidentified interrupts at SPARC levels 1..15
 */
	0,	level1,	level2,  level3,  level4,  level5,  level6,  level7,
	level8,	level9,	level10, level11, level12, level13, level14, level15,
/*
 * interrupts identified as "soft"
 */
	0,	xlvl1,	xlvl2,	xlvl3,	xlvl4,	xlvl5,	xlvl6,	xlvl7,
	xlvl8,	xlvl9,	xlvl10,	xlvl11,	xlvl12,	xlvl13,	xlvl14,	xlvl15,
};

/*
 * This string is pased to not_serviced() from locore.
 */
const char busname_vec[] = "iobus ";	/* only bus we know */

/*
 * This value is exported here for the functions in avintr.c
 */
const u_int maxautovec = (sizeof (vectorlist) / sizeof (vectorlist[0]));

/*
 * NOTE: if a device can generate interrupts on more than
 * one level, or if a driver services devices that interrupt
 * on more than one level, then the driver should install
 * itself on each of those levels.
 *
 * On Hard-ints, order of evaluation of the chains is:
 *   scan "unspecified" chain; if nobody claims,
 *	report spurious interrupt.
 * Scanning terminates with the first driver that claims it has
 * serviced the interrupt.
 *
 * On Soft-ints, order of evaulation of the chains is:
 *   scan the "unspecified" chain
 *   scan the "soft" chain
 * Scanning continues until some driver claims the interrupt (all softint
 * routines get called if no hardware int routine claims the interrupt and
 * if the software interrupt bit is on in the interrupt register).  If there
 * is no pending software interrupt, we report a spurious hard interrupt.
 * If soft int bit in interrupt register is on and nobody claims the interrupt,
 * report a spurious soft interrupt.
 */

/*
 * Check for machine specific interrupt levels which cannot be reasigned by
 * settrap(), sun4d version.
 */
/*ARGSUSED*/
int
exclude_settrap(int lvl)
{
	return (1);	/* i.e. we don't allow any! */
}

/*
 * Check for machine specific interrupt levels which cannot be set.
 */
/*ARGSUSED*/
int
exclude_level(int lvl)
{
	return (0);	/* in theory we can set any */
}

static char *rootname;		/* massaged name of root nexus */

/*
 * Create classes and major number bindings for the name of my root.
 * Called immediately before 'loadrootmodules'
 */
static void
impl_create_root_class(void)
{
	register int major;
	register size_t size;
	register char *cp;
	extern struct bootops *bootops;

	/*
	 * The name for the root nexus is exactly as the manufacturer
	 * placed it in the prom name property.  No translation.
	 */
	if ((major = ddi_name_to_major("rootnex")) == -1)
		cmn_err(CE_PANIC, "No major device number for 'rootnex'");

	size = (size_t) BOP_GETPROPLEN(bootops, "mfg-name");
	rootname = kmem_zalloc(size, KM_SLEEP);
	(void) BOP_GETPROP(bootops, "mfg-name", rootname);

	/*
	 * Fix conflict between OBP names and filesystem names.
	 * Substitute '_' for '/' in the name.  Ick.  This is only
	 * needed for the root node since '/' is not a legal name
	 * character in an OBP device name.
	 */
	for (cp = rootname; *cp; cp++)
		if (*cp == '/')
			*cp = '_';

	add_class(rootname, "root");
	make_mbind(rootname, major, mb_hashtab);

	/*
	 * The `platform' or `implementation architecture' name has been
	 * translated by boot to be proper for file system use.  It is
	 * the `name' of the platform actually booted.  Note the assumption
	 * is that the name will `fit' in the buffer platform (which is
	 * of size SYS_NMLN, which is far bigger than will actually ever
	 * be needed).
	 */
	(void) BOP_GETPROP(bootops, "impl-arch-name", platform);
}

/*
 * Create a tree from the PROM info
 */
static void
create_devinfo_tree(void)
{
	register int major;
	char buf[16];

	top_devinfo = (dev_info_t *)
	    kmem_zalloc(sizeof (struct dev_info), KM_SLEEP);

	DEVI(top_devinfo)->devi_name = rootname;
	DEVI(top_devinfo)->devi_instance = -1;

	DEVI(top_devinfo)->devi_nodeid = (int)prom_nextnode((dnode_t)0);

	sprintf(buf, "di %x", (int)top_devinfo);
	mutex_init(&(DEVI(top_devinfo)->devi_lock), buf, MUTEX_DEFAULT, NULL);

	major = ddi_name_to_major("rootnex");
	devnamesp[major].dn_head = top_devinfo;

	/*
	 * Record that devinfos have been made for "rootnex."
	 * di_dfs() is used to read the prom because it doesn't get the
	 * next sibling until the function returns, unlike ddi_walk_devs().
	 */
	di_dfs(ddi_root_node(), get_neighbors, 0);
}

/*
 * Setup the DDI but don't necessarilly init the DDI.  This will happen
 * later once /boot is released.
 */
void
setup_ddi(void)
{
	/*
	 * Initialize the instance number data base--this must be done
	 * after mod_setup and before the bootops are given up
	 */
	e_ddi_instance_init();
	impl_create_root_class();
	create_devinfo_tree();
	impl_ddi_callback_init();
}

static void
di_dfs(dev_info_t *devi, int (*f)(dev_info_t *, caddr_t), caddr_t arg)
{
	(void) (*f)(devi, arg);		/* XXX - return value? */
	if (devi) {
		di_dfs((dev_info_t *)DEVI(devi)->devi_child, f, arg);
		di_dfs((dev_info_t *)DEVI(devi)->devi_sibling, f, arg);
	}
}

/*
 * Check the status property of the device node passed as an argument.
 *
 *	if (status property is OKAY or undefined)
 *		return DDI_SUCCESS
 *	else
 *		print a warning
 *
 *	if (good mem-unit)
 *		return DDI_SUCCESS
 *	else
 *		return DDI_FAILURE
 */
int
check_status(int id, char *name, dev_info_t *parent)
{
	char	status_buf[NAME_LEN];
	char	path[OBP_MAXPATHLEN];
	int boardnum;
	int retval = DDI_FAILURE;
	int zero = 0;
	char *partially;
	char board_buf[24];

	if (0 >= getlongprop_buf(id, OBP_STATUS, status_buf, NAME_LEN))
		return (DDI_SUCCESS);

	if (strcmp(status_buf, OBP_ST_OKAY) == 0)
		return (DDI_SUCCESS);

	/*
	 * otherwise we print a warning
	 */

	/*
	 * get board number
	 */
	boardnum = get_boardnum(id, parent);
	if (boardnum != -1)
		sprintf(board_buf, " on board %d", boardnum);
	else
		board_buf[0] = '\0';

	/*
	 * is this an unjustly accused mem-unit?
	 */
	if (strncmp(name, "mem-unit", 8) == 0 &&
	    mem_unit_good(status_buf)) {
		retval = DDI_SUCCESS;
		partially = " partially";
	} else
		partially = (char *)&zero;	/* XXX Bogus: use "" */

	path[0] = '\0';
	prom_dnode_to_pathname((dnode_t)id, path);

	cmn_err(CE_WARN, "device '%s'%s%s unavailable.\n\tstatus: '%s'",
		path, board_buf, partially, status_buf);

	return (retval);
}

static int
get_boardnum(int nid, dev_info_t *par)
{
	int board_num;

	if (-1 != prom_getprop((dnode_t)nid, OBP_BOARDNUM,
	    (char *)&board_num))
		return (board_num);

	/*
	 * Look at current node and up the parent chain
	 * till we find a node with an OBP_BOARDNUM.
	 */
	while (par) {

		nid = ddi_get_nodeid(par);

		if (-1 != prom_getprop((dnode_t)nid, OBP_BOARDNUM,
		    (char *)&board_num))
			return (board_num);

		par = ddi_get_parent(par);
	}
	return (-1);
}

#ifdef HWC_DEBUG

static void
di_print(dev_info_t dev)
{
	register dev_info_t *di = dev;
	register int i, nreg, nintr;
	register struct regspec *rs;
	register struct intrspec *is;

	printf("%x %s@%d", ddi_get_nodeid(di), ddi_get_name(di),
		ddi_get_instance(di));

	if (DEVI_PD(di)) {
		nreg = sparc_pd_getnreg(di);
		for (i = 0; i < nreg; i++) {
			rs = sparc_pd_getreg(di, i);
			printf("[%x, %x, %x]", rs->regspec_bustype,
				rs->regspec_addr, rs->regspec_size);
		}
		nintr = sparc_pd_getnintr(di);
		for (i = 0; i < nintr; i++) {
			is = sparc_pd_getintr(di, i);
			printf("{%x, %x}", is->intrspec_pri, is->intrspec_vec);
		}
	}
	printf("\n");
}

static void
di_print_sp(dev_info_t *dev, char *space)
{
	register char *c;

	if (dev) {
		printf("%s", space);
		di_print(dev);
		for (c = space; *c; c++)
		;
		*c++ = ' ';
		*c++ = ' ';
		*c++ = ' ';
	} else {
		space[strlen(space)-3] = '\0';
	}
}

static void
di_print_tree(dev_info_t *dev)
{
	char space[128] = "";
	di_print_sp(dev, space);
	di_dfs((dev_info_t)DEVI(dev)->devi_child, (int (*)())di_print_sp,
		space);
}

#endif

/*
 * Called from common/cpr_driver.c: Power off machine
 * Let the firmware remove power, if it knows how to.
 */
void
arch_power_down()
{
	int is_defined = 0;
	char *wordexists = "p\" power-off\" find nip swap ! ";

	/*
	 * is_defined has value -1 when defined
	 */
	prom_interpret(wordexists, (int)(&is_defined), 0, 0, 0, 0);
	if (is_defined)
		prom_interpret("power-off", 0, 0, 0, 0, 0);
}
