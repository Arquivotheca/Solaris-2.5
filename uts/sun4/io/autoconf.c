/*
 * Copyright (c) 1992, 1993, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)autoconf.c	1.53	94/07/05 SMI"

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

static void parse_idprom(void);
static void add_root_props(dev_info_t *devi);
static void add_zs_props(dev_info_t *);
static void add_options_props(dev_info_t *);
static u_int softlevel1(caddr_t);

#ifdef HWC_DEBUG
static void di_dfs(dev_info_t *, int (*)(dev_info_t *, caddr_t), caddr_t);
#endif

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
		"obio",
		"vme",	/* do we really *need* this to boot? */
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
	 * Infer meanings to the members of the idprom buffer
	 */
	parse_idprom();

	/*
	 * Determine if an FPU is attached
	 */

#ifndef	MPSAS	/* no fpu module yet in MPSAS */
	fpu_probe();
	if (!fpu_exists) {
		printf("No FPU in configuration\n");
	} else if (cputype == CPU_SUN4_260 || cputype == CPU_SUN4_110) {
		/*
		 * It doesn't matter what fpu_version we have, none of
		 * them are supported.
		 *
		 * For reference, the fpu_version number means:
		 * 0: FAB1-FAB4 need compiler and kernel workarounds
		 * 1: FAB5/FAB6 need compiler and kernel workarounds
		 * 2: GNUFPC    has a bug that has no workaround
		 * 99% of the installed base is FAB4
		 * 15% of 4_110 has no FPU at all
		 * The compiler workarounds were removed in 5.0.
		 */
		printf("FPU version %d present but not supported\n",
			fpu_version);
		strcpy(CPU->cpu_type_info.pi_fputypes, "unsupported");
		fpu_exists = 0;
	}
#endif	/* !MPSAS */

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

#ifdef	sun4
/*
 * check the on_cpu and not_on_cpu prop fields for devi.
 * return DDI_NOT_WELL_FORMED if those property fields for devi
 * device specify that it not be loaded on this cputype.
 * Otherwise, return DDI_SUCCESS.
 *
 * XXX	Not clear that we should be supporting this at all.
 */

#define	get_prop(di, pname, flag, pval, plen) \
	ddi_prop_op(DDI_DEV_T_NONE, di, PROP_LEN_AND_VAL_ALLOC, \
	flag | DDI_PROP_DONTPASS | DDI_PROP_CANSLEEP, \
	pname, (caddr_t)pval, plen)

int
impl_check_cpu(dev_info_t *devi)
{
	register int i;
	int *tprop, tlen, on_cpu, not_on_cpu, error, mach;

	if (get_prop(devi, "on_cpu", 0, &tprop, &tlen) == DDI_SUCCESS) {
		/*
		 * There's a list of cpu's for which this child is valid.
		 */
		error = DDI_NOT_WELL_FORMED;
		for (i = 0; i * sizeof (int) < tlen; i++) {
			on_cpu = tprop[i];
			/*
			 * If it can be on any cpu (why bother?) or
			 * the architecture matches this one and either
			 * the machine id matches this one or
			 * the machine id is 0xff, it's a match.
			 */
			if (on_cpu == CPU_ANY ||
			    ((on_cpu & CPU_ARCH) == (cputype & CPU_ARCH) &&
			    ((mach = (on_cpu & CPU_MACH)) ==
			    (cputype & CPU_MACH) || mach == CPU_MACH))) {
				error = DDI_SUCCESS;
				break;
			}
		}
		kmem_free(tprop, tlen);
		if (error != DDI_SUCCESS)
			return (error);
	}

	if (get_prop(devi, "not_on_cpu", 0, &tprop, &tlen) == DDI_SUCCESS) {
		/*
		 * There's a list of cpu's for which this child is not valid.
		 */
		error = DDI_SUCCESS;
		for (i = 0; i * sizeof (int) < tlen; i++) {
			not_on_cpu = tprop[i];
			/*
			 * If it cannot be on any cpu (why bother?) or
			 * the architecture matches this one and either
			 * the machine id matches this one or
			 * the machine id is 0xff, it's a match.
			 */
			if (not_on_cpu == CPU_ANY ||
			    ((not_on_cpu & CPU_ARCH) == (cputype & CPU_ARCH) &&
			    ((mach = (not_on_cpu & CPU_MACH)) ==
			    (cputype & CPU_MACH) || mach == CPU_MACH))) {
				error = DDI_NOT_WELL_FORMED;
				break;
			}
		}
		kmem_free(tprop, tlen);
		if (error != DDI_SUCCESS)
			return (error);
	}
	return (DDI_SUCCESS);
}
#undef	get_prop
#endif	/* sun4 */

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
	    idprom.id_format == IDFORM_1) {
		cputype = idprom.id_machine;
	} else {
		/*
		 * Plain ole Paranoia ..
		 */
		prom_printf("Machine type set to SUN4_260.\n");
		idprom.id_format = 0;	/* Make sure later tests fail */
		cputype = CPU_SUN4_260;
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

#include <sys/eeprom.h>

static void
add_zs_props(dev_info_t *dip)
{
	int two = 2;
	int true = 1;

	(void) e_ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
	    "zs-usec-delay", (caddr_t)&two, (int)sizeof (int));

	/*
	 * MJ: XX: On OBP machines we have the property "port-a-ignore-cd"
	 * MJ: XX: and "port-b-ignore-cd" to see whether or not we ignore
	 * MJ: XX: carrier detect for a port. There is no corresponding
	 * MJ: XX: mechanism for non-OBP machines except via the config
	 * MJ: XX: file. However, in 4.1, ttysoftcar(8) was introduced
	 * MJ: XX: which established for any port whether DCD needs to be
	 * MJ: XX: asserted or not. I am going to assume that this s/w
	 * MJ: XX: mechanism will also be used in 5.0. As per tooch,
	 * MJ: XX: it seems safe to *assume* that these values are true
	 * MJ: XX: until ttysoftcar comes in and says so otherwise.
	 */

	if (ddi_get_instance(dip) == 0) {
		(void) e_ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
		    "port-a-ignore-cd", (caddr_t)&true, (int)sizeof (int));
		(void) e_ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
		    "port-b-ignore-cd", (caddr_t)&true, (int)sizeof (int));
	}

	/*
	 * See whether or not rts/dtr flow control should be used
	 */
	if (ddi_get_instance(dip) == 0) {
		if (EEPROM->ee_diag.eed_ttya_def.eet_rtsdtr == NO_RTSDTR) {
			(void) e_ddi_prop_create(DDI_DEV_T_NONE, dip,
			    DDI_PROP_CANSLEEP, "port-a-rts-dtr-off",
			    (caddr_t)&true, (int)sizeof (int));
		}
		if (EEPROM->ee_diag.eed_ttyb_def.eet_rtsdtr == NO_RTSDTR) {
			(void) e_ddi_prop_create(DDI_DEV_T_NONE, dip,
			    DDI_PROP_CANSLEEP, "port-b-rts-dtr-off",
			    (caddr_t)&true, (int)sizeof (int));
		}
	}

	/*
	 * Establish which zs is the kbd/ms duart.
	 */
	if (ddi_get_instance(dip) == 1) {
		(void) e_ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
		    "keyboard", (caddr_t)&true, (int)sizeof (int));
	}
}

#define	MINLINES	10
#define	MAXLINES	48
#define	LOSCREENLINES	34
#define	HISCREENLINES	48

#define	MINCOLS		10
#define	MAXCOLS		120
#define	LOSCREENCOLS	80
#define	HISCREENCOLS	120

static void
add_options_props(dev_info_t *dip)
{
	char rows[4], cols[4];
	u_int t_rows, t_cols;
	int w, h;

	(void) e_ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
	    "sd-address-map", "0001101120213060", 17);
	if (EEPROM->ee_diag.eed_keyclick == EED_KEYCLICK) {
		(void) e_ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
			"keyboard-click?", "true", 6);
	} else {
		(void) e_ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
			"keyboard-click?", "false", 6);
	}


	/*
	 * Unfortunately, there's no way to ask the PROM
	 * monitor how big it thinks the screen is, so we have
	 * to duplicate what the PROM monitor does.
	 */

	switch (EEPROM->ee_diag.eed_scrsize) {
	case EED_SCR_1152X900:
	case EED_SCR_1024X1024:
	case EED_SCR_1600X1280:
	case EED_SCR_1440X1440:
	case EED_SCR_640X480:
		t_cols = EEPROM->ee_diag.eed_colsize;
		if (t_cols < MINCOLS)
			t_cols = LOSCREENCOLS;
		else if (t_cols > MAXCOLS)
			t_cols = HISCREENCOLS;
		t_rows = EEPROM->ee_diag.eed_rowsize;

		if (t_rows < MINLINES)
			t_rows = LOSCREENLINES;
		else if (t_rows > MAXLINES)
			t_rows = HISCREENLINES;
		break;

	default:
		/*
		 * Default to something reasonable.
		 */
		t_cols = LOSCREENCOLS;
		t_rows = LOSCREENLINES;
		break;
	}

	(void) sprintf(cols, "%d", t_cols);
	(void) e_ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
	    "screen-#columns", cols, 4);
	(void) sprintf(rows, "%d", t_rows);
	(void) e_ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
	    "screen-#rows", rows, 4);
	w = h = 0;
	switch (EEPROM->ee_diag.eed_scrsize) {
	case EED_SCR_1152X900:
		w = 1152;
		h = 900;
		break;
	case EED_SCR_1024X1024:
		h = w = 1024;
		break;
	case EED_SCR_1600X1280:
		w = 1600;
		h = 1280;
		break;
	case EED_SCR_1440X1440:
		w = 1440;
		h = 1440;
		break;
	case EED_SCR_640X480:
		w = 640;
		h = 480;
		break;
	default:
		break;
	}
	if (w && h) {
		(void) e_ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
		    "screen-#xpixels", (caddr_t)&w, (int)sizeof (int));
		(void) e_ddi_prop_create(DDI_DEV_T_NONE, dip, DDI_PROP_CANSLEEP,
		    "screen-#ypixels", (caddr_t)&h, (int)sizeof (int));
	}
}


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
 * sun4 root nexus uses GENERIC addressing, mark it as such with a
 * property...
 */
static const int pagesize = PAGESIZE;
static const int mmu_pagesize = MMU_PAGESIZE;
static const int mmu_pageoffset = MMU_PAGEOFFSET;
static const char compat[] = "sun4";

static struct prop_def root_props[] = {
{ "compatible",		sizeof (compat),	(caddr_t)compat},
{ "PAGESIZE",		sizeof (int),		(caddr_t)&pagesize },
{ "MMU_PAGESIZE",	sizeof (int),		(caddr_t)&mmu_pagesize},
{ "MMU_PAGEOFFSET",	sizeof (int),		(caddr_t)&mmu_pageoffset},
{ DDI_GENERIC_ADDRESSING,	0,		(caddr_t)0 }
};

#define	NROOT_PROPS	(sizeof (root_props) / sizeof (struct prop_def))

static void
add_root_props(dev_info_t *devi)
{
	register int i;
	struct prop_def *rpp;

	for (i = 0, rpp = root_props; i < NROOT_PROPS; ++i, ++rpp) {
		(void) e_ddi_prop_create(DDI_DEV_T_NONE, devi, 0,
		    rpp->prop_name, rpp->prop_value, rpp->prop_len);
	}

}

void
impl_add_dev_props(dev_info_t *dip)
{
	char *name = ddi_get_name(dip);

	if (strcmp(name, "zs") == 0) {
		add_zs_props(dip);
	} else if (strcmp(name, "options") == 0) {
		add_options_props(dip);
	}
}

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
	 * There are no prom properties in this implementation.
	 */
}

static char *rootname;			/* massaged name of root nexus */

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

#ifdef HWC_DEBUG
static void
di_dfs(dev_info_t *devi, int (*f)(dev_info_t *, caddr_t), caddr_t arg)
{
	(void) (*f)(devi, arg);		/* XXX - return value? */
	if (devi) {
		di_dfs((dev_info_t *)DEVI(devi)->devi_child, f, arg);
		di_dfs((dev_info_t *)DEVI(devi)->devi_sibling, f, arg);
	}
}
#endif

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
 * VME interrupts are handled especially in locore.s with its own
 * vector.
 */

/*
 * software vectored interrupts:
 *
 * Level1 is special (softcall handler), so we initialize it to always
 * call softlevel1 first.
 * Only levels 1, 4, and 6 are allowed in sun4, as the others cannot be
 * generated.
 */

XVECTOR(1) = {{softlevel1}, {0}};	/* time-scheduled tasks */
XVECTOR(2) = {{0}};			/* not possible for sun4 */
XVECTOR(3) = {{0}};			/* not possible for sun4 */
XVECTOR(4) = {{0}};
XVECTOR(5) = {{0}};			/* not possible for sun4 */
XVECTOR(6) = {{0}};
XVECTOR(7) = {{0}};			/* not possible for sun4 */
XVECTOR(8) = {{0}};			/* not possible for sun4 */
XVECTOR(9) = {{0}};			/* not possible for sun4 */
XVECTOR(10) = {{0}};			/* not possible for sun4 */
XVECTOR(11) = {{0}};			/* not possible for sun4 */
XVECTOR(12) = {{0}};			/* not possible for sun4 */
XVECTOR(13) = {{0}};			/* not possible for sun4 */
XVECTOR(14) = {{0}};			/* not possible for sun4 */
XVECTOR(15) = {{0}};			/* not possible for sun4 */

/*
 * For the sun4m, these are "otherwise unclaimed sparc interrupts", but for
 * us, they're all hardware interrupts except VME interrupts.
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
	-1,	-1,	-1,	-1,	-1,	-1,	-1,	-1
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
 * settrap(), sun4 version.
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
 * sun4 case because they cannot ever be generated or are reserved for VME
 * interrupts).
 */
int
exclude_level(int lvl)
{
	switch (lvl) {

	case INTLEVEL_SOFT+1:
	case INTLEVEL_SOFT+4:
	case INTLEVEL_SOFT+6:
	case 1:
	case 4:			/* SCSI */
	case 6:			/* ethernet */
	case 8:			/* video retrace */
	case 12:		/* scc - serial i/o */
	case 14:		/* kprof */
		return (0);	/* don't exclude */
	/*
	 * The rest of the levels are reserved for VME interrupts,
	 * No other software interrupts can be generated by hardware.
	 */
	default:
		return (1);
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
			continue;
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
 * Referenced in common/cpr_driver.c: Power off machine
 * Don't know how to power off sun4.
 */
void
arch_power_down()
{
}
