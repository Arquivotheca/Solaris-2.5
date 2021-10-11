/*
 * Copyright (c) 1988-1993, by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)modsubr.c	1.38	95/08/24	SMI"

#include <sys/param.h>
#include <sys/modctl.h>
#include <sys/open.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <sys/stat.h>
#include <sys/mode.h>
#include <sys/pathname.h>
#include <sys/vnode.h>
#include <sys/ddi_impldefs.h>
#include <sys/esunddi.h>
#include <sys/sunddi.h>
#include <sys/hwconf.h>
#include <sys/file.h>
#include <sys/varargs.h>
#include <sys/thread.h>
#include <sys/cred.h>
#include <sys/autoconf.h>
#include <sys/kobj.h>
#include <sys/consdev.h>

#include <sys/debug.h>

extern struct dev_ops nodev_ops;
extern struct dev_ops mod_nodev_ops;
extern char **syscallnames;

extern int servicing_interrupt(void);

int instubs;
int in_modprintf = 0;

#define	dprintf if (drvconfig_debug) printf

int drvconfig_debug;

struct mod_noload {
	struct mod_noload *mn_next;
	char *mn_name;
};

static void init_stubs(struct modctl *, struct mod_modinfo *);
static int makedirs(char *);
static int make_node(dev_info_t *, struct modconfig *);
static void append(struct hwc_spec *, struct par_list *);
static void add_spec(struct hwc_spec *, struct par_list **);
static void impl_free_hwc(struct hwc_spec *);
static void impl_free_parlist(struct par_list *);
static int match_parent(dev_info_t *, char *, char *);
static void make_children(dev_info_t *, char *, struct par_list *, int *);
static int nm_hash(char *);

struct dev_ops *
mod_hold_dev_by_major(major_t major)
{
	register struct dev_ops **devopspp, *ops;
	int loaded;
	register char *drvname;

	ASSERT((unsigned)major < devcnt);
	LOCK_DEV_OPS(&(devnamesp[major].dn_lock));
	devopspp = &devopsp[major];
	loaded = 1;
	while (loaded && !CB_DRV_INSTALLED(*devopspp)) {
		UNLOCK_DEV_OPS(&(devnamesp[major].dn_lock));
		drvname = ddi_major_to_name(major);
		if (drvname == NULL)
			return (NULL);
		loaded = (modload("drv", drvname) != -1);
		LOCK_DEV_OPS(&(devnamesp[major].dn_lock));
	}
	if (loaded) {
		INCR_DEV_OPS_REF(*devopspp);
		ops = *devopspp;
	} else {
		ops = NULL;
	}
	UNLOCK_DEV_OPS(&(devnamesp[major].dn_lock));
	return (ops);
}

/* #define	DEBUG_RELE 1 */
#ifdef	DEBUG_RELE
static int mod_rele_pause = DEBUG_RELE;
#endif	DEBUG_RELE

void
mod_rele_dev_by_major(major_t major)
{
	register struct dev_ops *dp;
	register kmutex_t *lp;

	ASSERT((unsigned)major < devcnt);
	dp = devopsp[major];
	lp = &(devnamesp[major].dn_lock);
	ASSERT(CB_DRV_INSTALLED(dp));

#ifdef	DEBUG_RELE
	LOCK_DEV_OPS(lp);
	if (!DEV_OPS_HELD(dp))  {
		char *s;
		static char *msg =
		    "mod_rele_dev_by_major: "
		    "unheld driver!";

		printf("mod_rele_dev_by_major: Major dev <%d>, name <%s>\n",
		    major, (s = ddi_major_to_name(major)) ? s : "unknown");
		if (mod_rele_pause)
			debug_enter(msg);
		else
			printf("%s\n", msg);
		UNLOCK_DEV_OPS(lp);
		return;			/* XXX: Note changed behaviour */
	}
	UNLOCK_DEV_OPS(lp);
#endif	DEBUG_RELE

	if (!DEV_OPS_HELD(dp))  {
		cmn_err(CE_PANIC,
		    "mod_rele_dev_by_major: Unheld driver: major number <%d>",
		    (int)major);
	}
	LOCK_DEV_OPS(lp);
	DECR_DEV_OPS_REF(dp);
	UNLOCK_DEV_OPS(lp);
}

struct dev_ops *
mod_hold_dev_by_devi(dev_info_t *devi)
{
	register major_t major;
	register char *name;

	name = ddi_get_name(devi);
	if ((major = ddi_name_to_major(name)) == -1)
		return (NULL);
	return (mod_hold_dev_by_major(major));
}

void
mod_rele_dev_by_devi(dev_info_t *devi)
{
	register major_t major;
	register char *name;

	name = ddi_get_name(devi);
	if ((major = ddi_name_to_major(name)) == -1)
		return;
	mod_rele_dev_by_major(major);
}

int
nomod_zero()
{
	return (0);
}

int
nomod_minus_one()
{
	return (-1);
}

int
nomod_einval()
{
	return (EINVAL);
}

unsigned int
install_stub(struct mod_stub_info *stub)
{
	if (!servicing_interrupt()) {
		/*
		 * XXX - If stubs are meant to load drivers, we should call
		 * ddi_install_driver here.
		 */
		if (modload(NULL, stub->mods_modinfo->modm_module_name) != -1) {
			return (stub->mods_func_adr);
		}
	}

	if (stub->mods_errfcn == NULL) {
		modprintf("install_stub couldn't modload %s\n",
			stub->mods_modinfo->modm_module_name);
		panic("install_stub");
		/*NOTREACHED*/
	} else {
		return ((unsigned int)stub->mods_errfcn);
	}
}

/*
 * Install all the stubs for a module.
 */
void
install_stubs_by_name(struct modctl *modp, char *name)
{
	register char *namebufp;
	register char *filenamep;

	char namebuf[MODMAXNAMELEN + 12];
	struct mod_modinfo *mp;
	char *p;

	p = name;
	filenamep = name;

	while (*p) {
		if (*p++ == '/')
			filenamep = p;
	}

	/*
	 * concatenate "name" with "_modname" then look up this symbol
	 * in the kernel.  If not found, we're done.
	 * If found, then find the "mod" info structure and call init_stubs().
	 */
	namebufp = namebuf;

	while (*filenamep && *filenamep != '.')
		*namebufp++ = *filenamep++;

	strcpy(namebufp, "_modinfo");

	if ((mp = (struct mod_modinfo *)modgetsymvalue(namebuf, 1)) != 0)
		init_stubs(modp, mp);
}

static void
init_stubs(struct modctl *modp, struct mod_modinfo *mp)
{
	register struct mod_stub_info *sp;
	register int i;

	u_int offset;
	unsigned int funcadr;
	char *funcname;

	modp->mod_modinfo = mp;

	/*
	 * fill in all stubs for this module.  we can't be lazy, since
	 * some calls could come in from interrupt level, and we
	 * can't modlookup then (symbols may be paged out)
	 */
	sp = mp->modm_stubs;
	for (i = 0; sp->mods_func_adr; i++, sp++) {
		funcname = modgetsymname(sp->mods_stub_adr, &offset);
		if (funcname == NULL) {
		    modprintf("init_stubs: couldn't find symbol in module %s\n",
			mp->modm_module_name);
			panic("init_stubs");
			/*NOTREACHED*/
		}
		funcadr = (u_long)kobj_lookup(modp->mod_mp, funcname);

		if (kobj_addrcheck(modp->mod_mp, (caddr_t)funcadr)) {
			modprintf("%s:%s() not defined properly\n",
				mp->modm_module_name, funcname);
			panic("init_stubs 1");
		}
		sp->mods_func_adr = funcadr;
	}
	mp->mp = modp;
}

void
reset_stubs(register struct modctl *modp)
{
	register struct mod_stub_info *stub;

	if (modp->mod_modinfo) {
		for (stub = modp->mod_modinfo->modm_stubs;
		    stub->mods_func_adr; stub++) {
			if (stub->mods_weak)
				stub->mods_func_adr =
				    (unsigned int)stub->mods_errfcn;
			else
				stub->mods_func_adr =
				    (unsigned int)stub_install_common;
		}
		modp->mod_modinfo->mp = NULL;
	}
}

#ifdef notdef

struct modctl *
mod_find_by_id(int id)
{
	struct modctl *modp;

	mutex_enter(&mod_lock);
	for (modp = modules.mod_next; modp != &modules; modp = modp->mod_next) {
		if (modp->mod_id == id)
			break;
	}
	mutex_exit(&mod_lock);
	if (modp == &modules)
		return (0);
	return (modp);
}

#endif /* notdef */

static int
makedirs(char *line)
{
	register char *p;
	register char *ip;
	char c;
	struct vattr vattr;
	struct vnode *vp;
	register int error;

	ip = p = line;
	while (*ip == '/')		/* strip leading slashes */
		ip++;
	while ((ip = strchr(ip, '/')) != NULL) {;
		c = *ip;
		*ip = '\0';
		vattr.va_type = VDIR;
		vattr.va_mode = 0755 & PERMMASK;
		vattr.va_mask = AT_TYPE|AT_MODE;
		error = vn_create(p, UIO_SYSSPACE, &vattr,
		    EXCL, 0, &vp, CRMKDIR);
		switch (error) {
		case 0:
			ASSERT(vp);
			VN_RELE(vp);
			break;

		case EEXIST:
			error = 0;
			break;

		default:
			return (error);
			/*NOTREACHED*/
		}
		*ip++ = c;
	}
	return (0);
}

#define	NODE_PREPEND 	".."
#define	NODE_PREPEND_LEN	2

/*
 * make_node creates the device special files for an instance of
 * a device in the devfs (/devices).  Each node created will have
 * a '..' prepended to the name.  This provides programs that invoke
 * modctl(MODCONFIG) with a way of determining what nodes were
 * created by the system call.
 */
static int
make_node(dev_info_t *dev_info, struct modconfig *mc)
{
	char node[256];
	char *dot_node;
	struct ddi_minor_data *dmdp;
	char *p;
	char *q;
	char *name;
	struct vattr vattr, vattr1;
	mode_t fmode;
	struct vnode *vp;
	int error;
	int len;
	int create_dot_file;

	ASSERT(DDI_CF2(dev_info));
	sprintf(node, "%s", mc->rootdir);
	p = node + strlen(node);
	if (DEVI(dev_info)->devi_minor != NULL) {
		(void) ddi_pathname(dev_info, p);
		if ((error = makedirs(node)) != 0)
			return (error);
	} else
		return (0);

	/*
	 * prepend the '..' to the device name.
	 * dot_node is the device name (full path)
	 * with '..' prepended to the device name.
	 */
	p = strrchr(node, '/');
	*p = '\0';
	p++;
	dot_node = (char *)kmem_alloc(NODE_PREPEND_LEN + 256, KM_SLEEP);
	sprintf(dot_node, "%s/%s%s", node, NODE_PREPEND, p);
	p--;
	*p = '/';	/* restore node - we need it later */
	len = strlen(node);
	p = node + len;			/* set p to point to the end of node */
	q = dot_node + len + NODE_PREPEND_LEN;	/* set q to point to the */
						/* end of dot_node */

	for (dmdp = (struct ddi_minor_data *)
	    DEVI(dev_info)->devi_minor;
	    dmdp != NULL;
	    dmdp = (struct ddi_minor_data *)dmdp->next) {
		if (dmdp->type != DDM_MINOR)
			continue;
		bzero((caddr_t)&vattr, sizeof (struct vattr));
		name = dmdp->ddm_name;
		dprintf("minor data: %s, %s, %x, %x, %x, %x\n",
		    DEVI(dev_info)->devi_name, name,
		    getmajor(dmdp->ddm_dev),
		    getminor(dmdp->ddm_dev),
		    dmdp->ddm_spec_type, (int)dmdp->ddm_node_type);

		sprintf(p, ":%s", name);
		fmode = (mode_t)(dmdp->ddm_spec_type | 0600);
		vattr.va_type = IFTOVT(fmode);
		vattr.va_mode = (fmode & MODEMASK);
		vattr.va_mask = AT_TYPE|AT_MODE|AT_RDEV;
		vattr.va_rdev = dmdp->ddm_dev;

		/*
		 * Check to see if the node already exists.  If it does,
		 * verify that it is a device special file.
		 */
		create_dot_file = 1;
		error = lookupname(node, UIO_SYSSPACE, FOLLOW,
			NULLVPP, &vp);
		switch (error) {
			case 0:
				bzero((caddr_t)&vattr1, sizeof (struct vattr));
				vattr1.va_mask = AT_TYPE|AT_RDEV;
				VOP_GETATTR(vp, &vattr1, 0, CRED());
				VN_RELE(vp);
				if ((vattr1.va_type == VCHR ||
					vattr1.va_type == VBLK) &&
					vattr1.va_type == vattr.va_type &&
					vattr1.va_rdev == vattr.va_rdev) {
							create_dot_file = 0;
							break;
				}
			default:
				if (error != ENOENT) {
					/*
					 * The device node exists
					 * but is not valid
					 * so remove it.
					 */
					(void) vn_remove(node, UIO_SYSSPACE,
					    RMFILE);
				}
				break;
		}

		/*
		 * If create_dot_file is set, then
		 * either the node does not already exist or it exists
		 * but is not a device special file (removed above)
		 *
		 * In either case, create the '..' version of the node.
		 */
		if (create_dot_file) {
			sprintf(q, ":%s", name);

			if ((error = vn_create(dot_node, UIO_SYSSPACE, &vattr,
			    EXCL, 0, &vp, CRMKNOD)) == EEXIST) {
				if ((error = lookupname(dot_node, UIO_SYSSPACE,
				    FOLLOW, NULLVPP, &vp)) != 0) {
					printf("Can't lookup %s to get mode\n",
					    dot_node);
					kmem_free((void *)dot_node,
					    256 + NODE_PREPEND_LEN);
					return (error);
				}
				bzero((caddr_t)&vattr1, sizeof (struct vattr));
				vattr1.va_mask = AT_TYPE|AT_RDEV;
				VOP_GETATTR(vp, &vattr1, 0, CRED());
				VN_RELE(vp);
				if ((vattr1.va_type != VCHR &&
				    vattr1.va_type != VBLK) ||
				    vattr1.va_type != vattr.va_type ||
				    vattr1.va_rdev != vattr.va_rdev) {
					(void) vn_remove(dot_node, UIO_SYSSPACE,
					    RMFILE);
					if ((error = vn_create(dot_node,
					    UIO_SYSSPACE, &vattr, EXCL, 0,
					    &vp, CRMKNOD)) != 0 &&
					    error != EEXIST)
						dprintf("vncreate error %d\n",
						    error);
					if (error == 0)
						VN_RELE(vp);
				}
			} else if (error) {
					dprintf("vncreate error %d\n", error);
			} else
					VN_RELE(vp);
		}
	}
	kmem_free((void *)dot_node, 256 + NODE_PREPEND_LEN);
	return (0);
}

struct modctl *
mod_getctl(struct modlinkage *modlp)
{
	struct modctl *modp;

	mutex_enter(&mod_lock);
	for (modp = modules.mod_next; modp != &modules;
	    modp = modp->mod_next) {
		if (modp->mod_linkage == modlp) {
			ASSERT(modp->mod_busy);
			break;
		}
	}
	mutex_exit(&mod_lock);
	if (modp == &modules)
		modp = NULL;
	return (modp);
}

static void
append(struct hwc_spec *spec, struct par_list *par)
{
	struct hwc_spec *hwc, *last;

	ASSERT(par->par_specs);
	for (hwc = par->par_specs; hwc; hwc = hwc->hwc_next) {
		last = hwc;
	}
	last->hwc_next = spec;
}

/*
 * Chain together specs whose parent's module name is the same.
 */

static void
add_spec(struct hwc_spec *spec, struct par_list **par)
{
	register struct par_list *lpl;
	register major_t major;

	register char *ch;
	char *last_at, *last_slash;

	ch = spec->hwc_parent_name;
	last_at = last_slash = NULL;
	while (*ch != '\0') {
		if (*ch == '@')
			last_at = ch;
		if (*ch == '/')
			last_slash = ch;
		ch++;
	}
	if (last_at)
		*last_at = '\0';
	if (last_slash)
		ch = last_slash+1;
	else
		ch = spec->hwc_parent_name;

	if ((major = ddi_name_to_major(ch)) == -1) {
		cmn_err(CE_WARN, "add_spec: No major number for %s", ch);
		impl_free_hwc(spec);
		return;
	}
	if (last_at)
		*last_at = '@';

	/*
	 * Scan the list looking for a matching parent module.
	 */
	for (lpl = *par; lpl; lpl = lpl->par_next) {
		if (major == lpl->par_major) {
			append(spec, lpl);
			return;
		}
	}
	/*
	 * Didn't find a match on the list.  Make a new parent list.
	 */
	lpl = (struct par_list *)kmem_zalloc(sizeof (*lpl), KM_SLEEP);
	lpl->par_major = major;
	lpl->par_next = *par;
	*par = lpl;
	lpl->par_specs = spec;
}

/*
 * Sort a list of hardware conf specifications by parent module name.
 */

struct par_list *
sort_hwc(struct hwc_spec *hwc)
{
	struct hwc_spec *spec, *list = hwc;
	struct par_list *pl = NULL;

	while (list) {
		spec = list;
		list = list->hwc_next;
		spec->hwc_next = NULL;
		add_spec(spec, &pl);
	}
	return (pl);
}

#ifdef HWC_DEBUG
static void
print_hwc(struct hwc_spec *hwc)
{
	struct hwc_spec *spec;

	modprintf("parent %s\n", hwc->hwc_parent_name);
	while (hwc) {
		modprintf("\tchild %s\n", ddi_get_name(hwc->hwc_proto));
		hwc = hwc->hwc_next;
	}
}

static void
print_par(struct par_list *par)
{
	while (par) {
		print_hwc(par->par_specs);
		par = par->par_next;
	}
}
#endif /* HWC_DEBUG */

/*
 * gather_globalprops is passed a list of entries created from the
 * driver.conf(4) file of a driver. If an entry does not have
 * a name, then it does not identify a possible instance of a device,
 * and instead holds global driver properties. The properties on such
 * nodes are moved to the devnames structure of the driver, and the
 * original property node is removed. gather_globalprops returns the list
 * of entries with all driver global property nodes removed.
 */
static struct hwc_spec *
gather_globalprops(struct devnames *dnp, struct hwc_spec *hwc)
{
	struct hwc_spec *tmp = NULL;
	struct hwc_spec *devinfo_hwc = NULL;	/* returned list */
	struct hwc_spec *devinfo_hwc_tail = NULL;
	struct hwc_spec *global_props_hwc;	/* working property entry */
	ddi_prop_t	*global_propp_tail;	/* end of property list */
	ddi_prop_t	*node_propp;		/* node system property list */
	void	hwc_free(struct hwc_spec *hwcp);

	global_props_hwc = NULL;
	global_propp_tail = dnp->dn_global_prop_ptr;
	tmp = hwc;
	while (tmp != NULL) {
		/*
		 * The presence of a name means that this entry in the
		 * driver.conf(4) file refers to a possible device
		 * instance.
		 *
		 * The absence of a name indicates that this entry
		 * is a list of driver global properties.
		 */
		if (ddi_get_name(tmp->hwc_proto) != NULL) {
			/*
			 * This is a prototype devinfo node, not a
			 * property node. Move it to the list
			 * of prototype devinfo nodes that will be
			 * returned, preserving order.
			 */
			if (devinfo_hwc == NULL) {
				devinfo_hwc = tmp;
				tmp = tmp->hwc_next;
				devinfo_hwc_tail = devinfo_hwc;
				devinfo_hwc_tail->hwc_next = NULL;
			} else {
				devinfo_hwc_tail->hwc_next = tmp;
				tmp = tmp->hwc_next;
				devinfo_hwc_tail->hwc_next->hwc_next = NULL;
				devinfo_hwc_tail = devinfo_hwc_tail->hwc_next;
			}
			continue;
		}
		/*
		 * This node contains driver global properties.
		 * It will not be added to the list of entries
		 * which will be returned.
		 */
		global_props_hwc = tmp;
		tmp = tmp->hwc_next;
		global_props_hwc->hwc_next = NULL;

		/*
		 * The global properties are created as normal system
		 * properties by the parser. They are on the system property
		 * list of the prototype devinfo node for this entry.
		 *
		 * Move the entire list to the end of the current
		 * global property list. This also happens to preserve the
		 * property order that was listed in the driver.conf(4) file,
		 * though this is not documented.
		 */
		node_propp =
		    DEVI(global_props_hwc->hwc_proto)->devi_sys_prop_ptr;
		if (node_propp) {
			/*
			 * Move the nodes list to the end of the driver
			 * global property list on the devnames structure.
			 */
			if (global_propp_tail == NULL) {
				dnp->dn_global_prop_ptr = node_propp;
				global_propp_tail = node_propp;
			} else {
				global_propp_tail->prop_next = node_propp;
			}
			/* Find the end of the list for the next set. */
			while (global_propp_tail->prop_next != NULL)
				global_propp_tail =
				    global_propp_tail->prop_next;

			/* Finally, remove these properties from the node */
			DEVI(global_props_hwc->hwc_proto)->devi_sys_prop_ptr =
			    NULL;
		}

		hwc_free(global_props_hwc);
	}

	return (devinfo_hwc);
}

/*
 * Delete all the global properties of the driver.
 * Called when the driver is unloaded.
 */
static void
ddi_drv_remove_globalprops(struct devnames *dnp)
{
	ddi_prop_t	*freep;
	ddi_prop_t	**list_head = &(dnp->dn_global_prop_ptr);
	ddi_prop_t	*propp = *list_head;

	/* remove default properties */
	while (propp != NULL)  {
		freep = propp;
		propp = propp->prop_next;
		kmem_free(freep->prop_name,
		    (size_t)(strlen(freep->prop_name) + 1));
		if (freep->prop_len != 0)
			kmem_free(freep->prop_val, (size_t)(freep->prop_len));
		kmem_free(freep, sizeof (ddi_prop_t));
	}

	*list_head = NULL;
}


static void
impl_free_hwc(struct hwc_spec *hwc)
{
	kmem_free(hwc->hwc_parent_name, strlen(hwc->hwc_parent_name) + 1);
	/*
	 * Free the prototype properties.
	 */
	ddi_prop_remove_all(hwc->hwc_proto);
	e_ddi_prop_remove_all(hwc->hwc_proto);

	/*
	 * Free the prototype name.
	 */
	kmem_free(DEVI(hwc->hwc_proto)->devi_name,
	    strlen(DEVI(hwc->hwc_proto)->devi_name) + 1);
	/*
	 * Free the prototype.
	 */
	kmem_free(hwc->hwc_proto, sizeof (*DEVI(hwc->hwc_proto)));
	/*
	 * Free the hwconf structure.
	 */
	kmem_free(hwc, sizeof (*hwc));
}

/*
 * Assumes major is valid.
 */
struct par_list *
impl_make_parlist(major_t major)
{
	register struct hwc_spec *hp;
	register struct par_list *pl;
	char confname[MAXNAMELEN];
	struct devnames *dnp;

	dnp = &devnamesp[major];
	if ((pl = dnp->dn_pl) != NULL || dnp->dn_global_prop_ptr != NULL)
		return (pl);

	sprintf(confname, "drv/%s.conf", ddi_major_to_name(major));
	hp = hwc_parse(confname);
	if (hp != NULL) {
		/*
		 * hwc_parse has the side-effect of possibly creating nodes
		 * containing driver global properties. These properties must
		 * be moved to the devnames structure and the extra entries
		 * removed from the hwc_spec list.
		 */
		hp = gather_globalprops(dnp, hp);

		/*
		 * Sort the specs by parent.
		 */
		pl = sort_hwc(hp);
	}
	return (pl);
}

/*
 * Free up the memory from the lists.
 */

static void
impl_free_parlist(struct par_list *pl)
{
	register struct par_list *saved_pl;
	register struct hwc_spec *hp, *hp1;

	while (pl) {
		/*
		 * Free the parent spec list.
		 */
		hp = pl->par_specs;
		while (hp) {
			hp1 = hp;
			hp = hp->hwc_next;
			impl_free_hwc(hp1);
		}
		saved_pl = pl;
		pl = pl->par_next;
		/*
		 * Free the parent list structure.
		 */
		kmem_free(saved_pl, sizeof (*saved_pl));
	}
}

/*
 * Make dev_info nodes for the named driver.
 * This is an internal function only called from ddi_hold_installed_driver.
 * This is effectively part of ddi_hold_installed_driver.  Do not call
 * this directly, call ddi_hold_installed_driver instead.
 */

int
impl_make_devinfos(major_t major)
{
	register struct par_list *pl, *saved_pl;
	register struct devnames *dnp;
	register int circular;
	int unit = 0;

	ASSERT((unsigned)major < devcnt);
	dnp = &devnamesp[major];
	circular = dnp->dn_circular;

	/*
	 * Read the .conf file, if it has not already been done.
	 * Stuff the data away for reuse.
	 * Note that impl_make_parlist will only make the list once.
	 */
	saved_pl = impl_make_parlist(major);
	if (saved_pl != NULL)  {
		LOCK_DEV_OPS(&(dnp->dn_lock));
		dnp->dn_pl = saved_pl;
		UNLOCK_DEV_OPS(&(dnp->dn_lock));
	}

	/*
	 * Walk the tree making children wherever they belong, and
	 * probe/attach them.  (That's the only way to tell they're real)
	 */
	for (pl = saved_pl; pl != NULL; pl = pl->par_next)  {
		/*
		 * For each parent, make sure it exists, and if it does,
		 * make children devinfos for it.
		 */
		if (ddi_hold_installed_driver(pl->par_major) != NULL) {
			register dev_info_t *pdevi;

			for (pdevi = devnamesp[pl->par_major].dn_head;
			    pdevi != NULL; pdevi = ddi_get_next(pdevi))  {
				if (DDI_CF2(pdevi))
					make_children(pdevi,
					    devnamesp[pl->par_major].dn_name,
					    pl, &unit);
			}
			ddi_rele_driver(pl->par_major);
		}
	}

	if ((saved_pl) && (!circular))  {
		impl_free_parlist(saved_pl);
		LOCK_DEV_OPS(&(dnp->dn_lock));
		dnp->dn_flags |= DN_DEVI_MADE;
		dnp->dn_pl = NULL;
		UNLOCK_DEV_OPS(&(dnp->dn_lock));
	}
	return (DDI_SUCCESS);
}

void
copy_prop(ddi_prop_t *propp, ddi_prop_t **cpropp)
{
	ddi_prop_t *dpp, *cdpp, *cdpp_prev, *pp, *ppprev;

	cdpp_prev = NULL;
	for (dpp = propp; dpp != NULL; dpp = dpp->prop_next) {
		cdpp = kmem_zalloc(sizeof (struct ddi_prop), KM_SLEEP);
		if (cdpp_prev != NULL)
			cdpp_prev->prop_next = cdpp;
		else {
			if (*cpropp == NULL)
				*cpropp = cdpp;
			else {
				pp = *cpropp;
				while (pp != NULL) {
					ppprev = pp;
					pp = pp->prop_next;
				}
				ppprev->prop_next = cdpp;
			}
		}
		cdpp->prop_dev = dpp->prop_dev;
		cdpp->prop_flags = dpp->prop_flags;
		if (dpp->prop_name != NULL) {
			cdpp->prop_name = kmem_zalloc(strlen(dpp->prop_name) +
				1, KM_SLEEP);
			strcpy(cdpp->prop_name, dpp->prop_name);
		}
		if ((cdpp->prop_len = dpp->prop_len) != 0) {
			cdpp->prop_val = kmem_zalloc(dpp->prop_len, KM_SLEEP);
			bcopy(dpp->prop_val, cdpp->prop_val, dpp->prop_len);
		}
		cdpp_prev = cdpp;
	}
}

/*
 * match_parent: ONLY called from impl_make_devinfos/make_children.
 *		 We assert that the parent must be properly held
 *		 so it is safe to search this branch of the tree.
 *
 * See if the parent name matches the dev_info node.
 *
 * The parent name can be NULL -> rootnexus,
 * a full OBP pathname ( eg. /sbus@x,y/foo@a,b/...),
 * a dev_info node name (eg. foo@a,b),
 * or a simple name (eg. foo).
 */

static int
match_parent(dev_info_t *devi, char *devi_driver_name, char *par_name)
{
	char pn[MAXNAMELEN], *ch;

	ASSERT(ddi_get_driver(devi));
	ASSERT(DEV_OPS_HELD(ddi_get_driver(devi)));

	/*
	 * If the parent name starts with '/',
	 * the entire pathname must match.
	 */
	if (*par_name == '/')
		return (strcmp(par_name, ddi_pathname(devi, pn)) == 0);
	/*
	 * If the parent name contains an '@',
	 * the devinfo name must match.
	 */
	for (ch = par_name; *ch != '\0'; ch++) {
		if (*ch == '@')
			return (strcmp(par_name, ddi_deviname(devi, pn)) == 0);
	}
	/*
	 * it's a match if it matches the devi_name field.
	 */
	if (strcmp(par_name, ddi_get_name(devi)) == 0)
		return (1);

	/*
	 * handle aliases
	 */
	return (strcmp(par_name, devi_driver_name) == 0);
}

/*
 * Scan the list of parents looking for a match with this dev_info node.
 * If there's a match, then for every spec on the parent list, make a
 * child of this node based on the spec.
 */

static void
make_children(dev_info_t *devi, char *devi_driver_name,
    struct par_list *pl, int *unitp)
{
	struct hwc_spec *hp;
	dev_info_t *cdip;

	/*
	 * make a child for every spec whose parent name matches
	 */
	for (hp = pl->par_specs; hp != NULL; hp = hp->hwc_next) {
		if (match_parent(devi, devi_driver_name,
		    hp->hwc_parent_name)) {
			register dev_info_t *proto;

			proto = hp->hwc_proto;
			cdip = ddi_add_child(devi, ddi_get_name(proto),
				DEVI_PSEUDO_NODEID, (*unitp)++);
			copy_prop(DEVI(proto)->devi_drv_prop_ptr,
			    &(DEVI(cdip)->devi_drv_prop_ptr));
			copy_prop(DEVI(proto)->devi_sys_prop_ptr,
			    &(DEVI(cdip)->devi_sys_prop_ptr));
			if (impl_check_cpu(cdip) != DDI_SUCCESS) {
				(void) ddi_remove_child(cdip, 0);
				(*unitp)--;	/* rescind hint */
				continue;
				/*NOTREACHED*/
			}

			/*
			 * Probe/attach the new child, if non-existant,
			 * get rid of it.
			 */
			if (impl_proto_to_cf2(cdip) == DDI_SUCCESS)
				mod_rele_dev_by_devi(cdip);
		}
	}
}

/*
 * Probe/attach all (hw) devinfo nodes belonging to this driver.
 */

void
attach_driver_to_hw_nodes(major_t major, register struct dev_ops *ops)
{
	register dev_info_t *devi, *ndevi;
	register struct devnames *dnp = &(devnamesp[major]);

#ifdef lint
	ndevi = NULL;	/* See 1094364 */
#endif

	/* Assure the dev_ops has been prevented from being unloaded */
	ASSERT((unsigned)major < devcnt);
	ops = devopsp[major];
	ASSERT(DEV_OPS_HELD(ops));

	for (devi = dnp->dn_head; devi != NULL; devi = ndevi) {
		ndevi = ddi_get_next(devi);
		/*
		 * Added clause to skip s/w nodes, which was reduntant
		 */
		if (((!DDI_CF2(devi)) || DDI_DRV_UNLOADED(devi)) &&
		    (ddi_get_nodeid(devi) != DEVI_PSEUDO_NODEID)) {
			if (impl_proto_to_cf2(devi) == DDI_SUCCESS)
				mod_rele_dev_by_devi(devi);
		}
	}
}

void
impl_unattach_driver(major_t major)
{
	register dev_info_t *devi;

	for (devi = devnamesp[major].dn_head; devi != NULL;
	    devi = ddi_get_next(devi))
		ddi_set_driver(devi, NULL);
	ddi_drv_remove_globalprops(&devnamesp[major]);
}

void
impl_unattach_devs(major_t major)
{
	struct devnames *dnp = &(devnamesp[major]);
	register dev_info_t *dip, *ndip;

#ifdef lint
	ndip = NULL;	/* See 1094364 */
#endif

	/*
	 * The per-driver lock is held and all instances of this
	 * driver are already detached.  Here we restore the state
	 * of an unloaded, unattached driver by destroying pseudo
	 * devinfo nodes and unaming prom devinfo nodes, so they are
	 * back in prototype form.
	 */
	for (dip = dnp->dn_head; dip != NULL; dip = ndip)  {
		ndip = ddi_get_next(dip);
		/*
		 * Undo ddi_initchild...
		 */
		(void) ddi_uninitchild(dip);
		/*
		 * Remove pseudo nodeids, letting ddi_remove_child
		 * `know' that we already hold the per-driver lock.
		 */
		if (ddi_get_nodeid(dip) == DEVI_PSEUDO_NODEID)
			(void) ddi_remove_child(dip, 1);
	}
	dnp->dn_flags &= ~DN_DEVI_MADE;
}

#define	HASHTABSIZE 64
#define	HASHMASK (HASHTABSIZE-1)

struct bind *mb_hashtab[HASHTABSIZE];
struct bind *sb_hashtab[HASHTABSIZE];

static int mb_hashcnt[HASHTABSIZE];

static int
nm_hash(char *name)
{
	register char c;
	register int hash = 0;

	for (c = *name++; c; c = *name++)
		hash ^= c;

	return (hash & HASHMASK);
}

void
make_mbind(char *name, int major, struct bind **hashtab)
{
	register struct bind *bp1;
	register int namelen, hashndx;

	bp1 = (struct bind *)kmem_zalloc(sizeof (struct bind), KM_NOSLEEP);
	if (bp1 == NULL) {
		cmn_err(CE_PANIC, "Not enough memory for binding file");
		/*NOTREACHED*/
	}
	namelen = strlen(name);
	if ((bp1->b_name = kmem_zalloc(namelen + 1, KM_NOSLEEP)) == NULL) {
		cmn_err(CE_PANIC, "Not enough memory for binding file");
		/*NOTREACHED*/
	}
	strcpy(bp1->b_name, name);
	bp1->b_num = major;
	hashndx = nm_hash(name);
	bp1->b_next = hashtab[hashndx];
	hashtab[hashndx] = bp1;
	if (hashtab == mb_hashtab)
		mb_hashcnt[hashndx]++;
}

major_t
mod_name_to_major(char *name)
{
	register int hashndx;
	register struct bind *mbind;
	register major_t major;

	major = (major_t)(-1);
	hashndx = nm_hash(name);

	for (mbind = mb_hashtab[hashndx]; mbind; mbind = mbind->b_next) {
		if (strcmp(name, mbind->b_name) == 0) {
			major = mbind->b_num;
			break;
		}
	}
	return (major);
}

void
init_devnamesp(int size)
{
	register int hshndx;
	register struct bind *bp;
	register struct devnames *dp;
	register int i;
	char buffer[16];

	devnamesp = (struct devnames *)
			kobj_zalloc(size*sizeof (struct devnames), KM_NOSLEEP);
	if (devnamesp == NULL) {
		cmn_err(CE_PANIC, "Not enough memory for devnamesp array");
		/*NOTREACHED*/
	}

	for (i = 0, dp = devnamesp; i < size; ++i, ++dp)  {
		(void) sprintf(buffer, "dn %d", i);
		mutex_init(&(dp->dn_lock), buffer, MUTEX_DEFAULT,
		    NULL);
		cv_init(&(dp->dn_wait), buffer, CV_DEFAULT, NULL);
	}
	for (hshndx = 0; hshndx < HASHTABSIZE; hshndx++) {
		for (bp = mb_hashtab[hshndx]; bp; bp = bp->b_next) {
			(void) make_devname(bp->b_name, bp->b_num);
		}
	}
}

void
init_syscallnames(int size)
{
	register int hshndx;
	register struct bind *bp;

	syscallnames = (char **)kobj_zalloc(size * sizeof (char *), KM_NOSLEEP);

	if (syscallnames == NULL) {
		cmn_err(CE_PANIC, "Not enough memory for syscallnames array");
		/*NOTREACHED*/
	}

	for (hshndx = 0; hshndx < HASHTABSIZE; hshndx++)
		for (bp = sb_hashtab[hshndx]; bp; bp = bp->b_next)
			make_syscallname(bp->b_name, bp->b_num);

}

/*
 * Given a system call name, get its number.
 */
int
mod_getsysnum(char *name)
{
	register int hashndx;
	register struct bind *bind;
	register int sysnum = -1;

	hashndx = nm_hash(name);

	for (bind = sb_hashtab[hashndx]; bind; bind = bind->b_next) {
		if (strcmp(name, bind->b_name) == 0) {
			sysnum = bind->b_num;
			break;
		}
	}
	return (sysnum);
}

/*
 * Given a system call number, get the system call name.
 */
char *
mod_getsysname(int sysnum)
{
	extern char **syscallnames;

	return (syscallnames[sysnum]);
}

void
make_all_nodes(struct modconfig *mc)
{
	register char *name;
	register int i, alreadyloaded;
	register dev_info_t *inst;
	u_char *load_allp;

	load_allp = kmem_zalloc(devcnt * sizeof (u_char), KM_SLEEP);
	for (i = 0; i < devcnt; i++) {
		if ((name = devnamesp[i].dn_name) == NULL || *name == '\0')
			continue;
		/*
		 * Remember if the driver was already loaded ... see below
		 */
		alreadyloaded = CB_DRV_INSTALLED(devopsp[i]);
		if (ddi_hold_installed_driver(i) != NULL)
			load_allp[i] = alreadyloaded ? 1 : 2;
	}
	for (i = 0; i < devcnt; i++) {
		if (load_allp[i]) {
			/*
			 * If the driver was just loaded, then we can
			 * skip the deferred attach -- an optimization
			 * to reduce reduntant probe time.  If it was
			 * already loaded, we try the deferred attach.
			 */
			if (load_allp[i] == 1)		/* Already loaded? */
				(void) e_ddi_deferred_attach((major_t)i, NODEV);
			for (inst = devnamesp[i].dn_head; inst != NULL;
			    inst = (dev_info_t *)DEVI(inst)->devi_next) {
				if (DDI_CF2(inst))
					(void) make_node(inst, mc);
			}
			ddi_rele_driver(i);
		}
	}
	kmem_free(load_allp, devcnt * sizeof (char));
}

int
make_one_node(major_t major, struct modconfig *mc)
{
	register dev_info_t *inst;

	if (ddi_hold_installed_driver(major) != NULL) {
		(void) e_ddi_deferred_attach(major, NODEV);
		for (inst = devnamesp[major].dn_head; inst != NULL;
		    inst = (dev_info_t *)DEVI(inst)->devi_next)  {
			if (DDI_CF2(inst))
				(void) make_node(inst, mc);
		}
		ddi_rele_driver(major);
	} else
		return (ENXIO);

	/*
	 * Urp.. After installing the driver we have to tickle
	 * the clone driver so that it makes all the nodes that this
	 * driver may have attached under it's dev_info node.
	 */
	major = ddi_name_to_major("clone");
	if (major == (major_t)-1)
		cmn_err(CE_PANIC, "Clone device not found");
	if (ddi_hold_installed_driver(major) != NULL) {
		for (inst = devnamesp[major].dn_head; inst != NULL;
		    inst = (dev_info_t *)DEVI(inst)->devi_next) {
			(void) make_node(inst, mc);
		}
		ddi_rele_driver(major);
	} else
		return (EINVAL);
	return (0);
}

void
modprintf(char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	modvprintf(fmt, adx);
	va_end(adx);
}

void
modvprintf(char *fmt, va_list adx)
{
	static int doing_modprintf;

	mutex_enter(&instub_lock);
	if (!doing_modprintf && !instubs && cn_conf == 0) {
		doing_modprintf++;
		mutex_exit(&instub_lock);
		vprintf(fmt, adx);
	} else {
		/*
		 * Why this crock?  Well stubs is a broken interface that
		 * we need to eliminate in the future.  However, if you
		 * have moddebugging on and you hit a stub for a console
		 * device you get into a infinite recursion trying to
		 * print messages and  you keep hitting the stub for
		 * the iwscons driver and you watchdog reset.  So we set
		 * panicstr so that writekmsg in cmn_err.c goes right to the
		 * console.
		 */
		doing_modprintf++;
		in_modprintf++;
		mutex_exit(&instub_lock);
		vprintf(fmt, adx);
		mutex_enter(&instub_lock);
		in_modprintf--;
		mutex_exit(&instub_lock);
	}
	mutex_enter(&instub_lock);
	doing_modprintf--;
	mutex_exit(&instub_lock);
}


/*
 * mod_containing_pc - finds the module containing the pc and returns
 * the name in the buffer provided by the caller.  The buffer should be
 * MODMAXNAMELEN characters long.
 */
int
mod_containing_pc(caddr_t pc, char *modname)
{
	struct module *m;
	struct modctl *mcp;

	modname[0] = '\0';
	mutex_enter(&mod_lock);
	mcp = &modules;
	do {
		mcp = mcp->mod_next;

		if (!mcp->mod_installed)
			continue;

		m = (struct module *)mcp->mod_mp;
		if ((pc >= m->text) &&
		    (pc < (m->text + m->text_size))) {
			strcpy(modname, mcp->mod_modname);
			mutex_exit(&mod_lock);
			return (1);
		}
	} while (mcp != &modules);
	mutex_exit(&mod_lock);
	return (0);
}
