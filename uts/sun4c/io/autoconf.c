/*
 * Copyright (c) 1992, 1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)autoconf.c	1.44	94/11/10 SMI"

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
#include <sys/mmu.h>
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

/*
 * The following several variables are related to
 * the configuration process, and are used in initializing
 * the machine.
 */

dev_info_t *top_devinfo;
idprom_t idprom;

#endif	/* !SAS && !MPSAS */

/*
 * Machine type we are running on.
 */
short cputype;

/*
 * Magic integer for bugid 1075558
 * Patch to non-zero to enable V2 FPU on 4c/60's.
 */
int use_4c60_fpu_v2;

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
	} else if (cputype == CPU_SUN4C_60 &&
	    fpu_version == 2 && use_4c60_fpu_v2 == 0) {
		/*
		 * For reference, the fpu_version number means:
		 *
		 * 2: GNUFPC/TI8847 - one known bug, no known workaround.
		 * 3: Weitek 3170 - no known bugs.
		 *
		 * See bugid 1075558 for additional details.
		 */
		printf("FPU version %d present but not supported\n",
			fpu_version);
		strcpy(CPU->cpu_type_info.pi_fputypes, "unsupported");
		fpu_exists = 0;
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
 * This routine transforms a canonical form 1 dev_info node into a
 * canonical form 2 dev_info node.  If the transformation fails, the
 * node is removed.
 */
int
impl_initdev(dev_info_t *dev)
{
	register struct dev_ops *ops;
	register int r;

	ops = ddi_get_driver(dev);
	ASSERT(ops);
	ASSERT(DEV_OPS_HELD(ops));

	DEVI(dev)->devi_instance = e_ddi_assign_instance(dev);

	if ((r = impl_probe_attach_devi(dev)) == DDI_SUCCESS)  {
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
		e_ddi_keep_instance(dev);
	} else {
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

/*
 * This routine allows drivers to easily retrieve the value of a property
 * for arbitrary sized properties. A pointer to the value of 'name' in
 * the property list for 'devi' is returned. If no value is found, NULL
 * is returned. The space is allocated using kmem_zalloc, so it is assumed
 * that this routine is NOT called from an interrupt routine.
 */
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
	 * Workaround for bugid 1085575 - many clones have root 'name'
	 * properties that are missing a terminating '\0' .. sigh.
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
 */
void
setcputype(void)
{
#if defined(SAS) || defined(MPSAS)
	cputype = CPU_SUN4C_60;
	idprom.id_format = 0;	/* Make sure later tests fail */
#else

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
		prom_printf("Using default machine type Sun4c/60\n");
		idprom.id_format = 0;	/* Make sure later tests fail */
		cputype = CPU_SUN4C_60;
	}

#endif	/* SAS || MPSAS */
}

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
static const char compat[] = "sun4c";

static struct prop_def root_props[] = {
{ "compatible",		sizeof (compat),	(caddr_t)compat},
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
	 * Workaround for bugid 1045662.
	 *
	 * Some sun4c implementations (cache+ machines; that is SS-2
	 * series) have a micro-tlb which can cause problems in some
	 * framebuffer drivers.
	 *
	 * Many older PROMs do not define a property to describe this
	 * problem, so we define a property here to indicate that the
	 * micro-tlb exists, inferring its existence from 'vac_linesize=32'
	 * which is true for all Calvin class machines and their clones.
	 *
	 * XXX	Having got this far, we now need a mechanism (property?)
	 *	for the drivers concerned to report their sensitivity to
	 *	this property so that the implementation can turn off the
	 *	micro-tlb on their behalf just after calling their attach
	 *	routine.
	 *
	 * XXX	And another thing .. this should be a boolean property.
	 */
	if (vac_linesize == 32) {
		auto int tlb = 1;

		/*
		 * e.g. for the SS-2, IPX and ELC plus their clones
		 * -- but check with the PROM in case they have a later
		 * version.
		 */
		if (prom_getproplen(prom_rootnode(), "sun4c-micro-tlb") < 0)
			(void) e_ddi_prop_create(DDI_DEV_T_NONE, devi, 0,
			    "sun4c-micro-tlb", (caddr_t)&tlb, sizeof (int));
	}

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

#if 0	/* Not needed for sun4c (but please don't remove it just yet) */
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

struct prop_ispec {
	u_int pri, vec;
};

/*
 * Allow for implementation specific correction of PROM property values.
 */

/*ARGSUSED1*/
void
impl_fix_props(dev_info_t *dip, dev_info_t *ch_dip, char *name, int len,
    caddr_t buffer)
{
	if (len == 0)
		return;

	/*
	 * OBP V0 (1.x proms) gives us physical addresses, but the
	 * framework and drivers really want relative addresses.
	 *
	 * If I'm a child of SBus, on an OBP V0 machine,
	 * I must convert my "register addresses" from physical
	 * to relative addresses according to the following formula:
	 *
	 * V2high = (V0low >> 25) & 3	(i.e. extract bits 25 and 26)
	 * V2low  = V0low & 1ffffff	(i.e. low 25 bits)
	 *
	 * XXX: Beware this formula on a sun4e!
	 */
	if ((prom_getversion() == 0) &&
	    (strcmp(ddi_get_name(dip), "sbus") == 0) &&
	    (strcmp(name, "reg") == 0))  {

		struct regspec *rp = (struct regspec *)buffer;
		int n = len / sizeof (struct regspec);
		int i;

		for (i = 0; i < n; ++i, ++rp)  {
			rp->regspec_bustype =
			    (rp->regspec_addr >> 25) & 3;
			rp->regspec_addr &= 0x1ffffff;
		}
	}

	/*
	 * The function sbus-intr>cpu was broken on v1.0 and v1.1 PROMs.
	 * This also affected the "intr" FCode (the commonest way to declare
	 * the "intr" property), since the "intr" FCode calls this function.
	 *
	 * SBus    SS1(correct)    SS1(v1.0, v1.1 PROMS)
	 *
	 * 7		9		13	<--wrong
	 * 6		8		9	<--wrong
	 * 5		7		7
	 * 4		5		5
	 * 3		3		3
	 * 2		2		2
	 * 1		1		1
	 */

#define	GOOD_BOOT_PROM	0x010003	/* Fix needed on < 1.3 prom */

	if (((int)prom_getversion() == 0) &&
	    ((int)prom_mon_id() < GOOD_BOOT_PROM) &&
	    (strcmp(ddi_get_name(dip), "sbus") == 0) &&
	    (strcmp(name, "intr") == 0))  {

		struct prop_ispec *l = (struct prop_ispec *)buffer;
		int n = len / sizeof (struct prop_ispec);
		int i;

		for (i = 0; i < n; ++i, ++l)  {
			if (l->pri == 9)
				l->pri = 8;
			if (l->pri == 13)
				l->pri = 9;
		}
	}
#undef	GOOD_BOOT_PROM

	/*
	 * 1088905, 1101979: The 'burst-sizes' property was slightly
	 * wrong on some early (2.0, 2.1 & 2.2) SS2 and IPX PROMs.
	 */
	if (prom_getversion() == 2 &&
	    (unsigned)prom_mon_id() < 0x0200003 &&
	    strcmp(ddi_get_name(dip), "sbus") == 0 &&
	    strcmp(name, "burst-sizes") == 0 &&
	    (cputype == CPU_SUN4C_75 || cputype == CPU_SUN4C_50)) {
		*(int *)buffer = 0x7f & (*(int *)buffer | 0x8);
	}
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
		/* Make a sibling */
		if (0 < getlongprop_buf(snid, OBP_NAME, buf, NAME_LEN))
			(void) ddi_add_child(parent, buf, snid, -1);
	}

	if (cnid && (cnid != -1)) {
		/* Make a child */
		if (0 < getlongprop_buf(cnid, OBP_NAME, buf, NAME_LEN))
			(void) ddi_add_child(di, buf, cnid, -1);
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
 * No VME interrupts on sun4c
 */

/*
 * software vectored interrupts:
 *
 * Level1 is special (softcall handler), so we initialize it to always
 * call softlevel1 first.
 * Only levels 1, 4, and 6 are allowed in sun4c, as the others cannot be
 * generated.
 */

XVECTOR(1) = {{softlevel1}, {0}};	/* time-scheduled tasks */
XVECTOR(2) = {{0}};			/* not possible for sun4c */
XVECTOR(3) = {{0}};			/* not possible for sun4c */
XVECTOR(4) = {{0}};
XVECTOR(5) = {{0}};			/* not possible for sun4c */
XVECTOR(6) = {{0}};
XVECTOR(7) = {{0}};			/* not possible for sun4c */
XVECTOR(8) = {{0}};			/* not possible for sun4c */
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
 * This table gives the mapping from onboard SBus level to sparc ipl.
 *
 * The fact that it's here (rather than in the 'sbus' nexus driver)
 * can be construed as a bug.  However, it's really a workaround for
 * the fact that we changed the specification of the interrupt mappings
 * between sun4c and sun4m architectures.  The way we should've made
 * such a non-backwards compatible change would've been to have changed
 * the 'name' property of the SBus nexus driver.  Oh well ..
 */
const char sbus_to_sparc_tbl[] = {
	-1,	1,	2,	3,	5,	7,	8,	9
};

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
 * settrap(), sun4c version.
 */
int
exclude_settrap(int lvl)
{
	if ((lvl == 10) ||	/* reserved for system clock */
	    (lvl == 15)) {	/* reserved */
		return (1);
	} else
		return (0);
}

/*
 * Check for machine specific interrupt levels which cannot be set (in the
 * sun4c case because they cannot ever be generated).
 */
int
exclude_level(int lvl)
{
	if (lvl && (lvl < INTLEVEL_SOFT || lvl == INTLEVEL_SOFT+1 ||
	    lvl == INTLEVEL_SOFT+4 || lvl == INTLEVEL_SOFT+6))
		return (0);	/* don't exclude */
	else
		return (1);	/* can't be generated by hardware */
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
	 * Walk the prom tree and create orphaned h/w devinfo nodes.
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
 * Can't power-off sun4c.
 */
void
arch_power_down()
{
}
