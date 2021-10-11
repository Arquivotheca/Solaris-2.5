/*
 * Copyright (c) 1988-1993, by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)modctl.c	1.66	95/09/13	SMI"

/*
 * modctl system call for loadable module support.
 */

#include <sys/param.h>
#include <sys/user.h>
#include <sys/systm.h>
#include <sys/exec.h>
#include <sys/file.h>
#include <sys/conf.h>
#include <sys/time.h>
#include <sys/reboot.h>
#include <sys/fs/ufs_fsdir.h>
#include <sys/kmem.h>
#include <sys/sysconf.h>
#include <sys/cmn_err.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/bootconf.h>

#include <sys/modctl.h>
#include <sys/kobj.h>
#include <sys/devops.h>
#include <sys/autoconf.h>
#include <sys/hwconf.h>
#include <sys/callb.h>
#include <sys/debug.h>
#include <sys/cpuvar.h>

static int mod_circdep(struct modctl *);
static int modinfo(int, struct modinfo *);

static void mod_uninstall_all();
static struct modlinkage *mod_getinfo(struct modctl *, struct modinfo *);
static struct modctl *allocate_modp(char *, char *);

static int mod_load(struct modctl *, int);
static int mod_unload(struct modctl *);
static int modinstall(struct modctl *);
static int moduninstall(struct modctl *);

static struct modctl *mod_hold_by_name(char *);
static struct modctl *mod_hold_by_id(modid_t);
static struct modctl *mod_hold_next_by_id(modid_t);
static struct modctl *mod_hold_loaded_mod(char *, int, int *);
static struct modctl *mod_hold_installed_mod(char *, int, int *);

static void mod_release(struct modctl *);
static void mod_make_dependent(struct modctl *, struct modctl *);
static int mod_install_requisites(struct modctl *);
static int mod_hold_dependents(struct modctl *, int);
static void mod_release_dependents(struct modctl *);

/*
 * The following modconf* variables are used in modctl_modconfig() to
 * ensure that only one thread at a time can do a MODCONFIG modctl()
 */
static char modconfig_busy = 0;
static kcondvar_t modconfig_cv;
static kmutex_t modconfig_lock;

kcondvar_t mod_cv;
kcondvar_t mod_uninstall_cv;	/* Communication between swapper and the */
				/* uninstall daemon. */
kmutex_t mod_unload_lock;	/* protects autounload request state */
kmutex_t mod_lock;		/* protects mod structures */
kmutex_t mod_uninstall_lock;	/* protects mod_uninstall_cv */
kmutex_t instub_lock;
kmutex_t kobj_lock;		/* module arena lock */
int mod_mix_changed;		/* incremented every time the mod mix chgs */

int mod_no_unload;		/* temp lock to prevent unloading */
int modrootloaded; 		/* set after root driver and fs are loaded */
int moddebug = 0x0;		/* debug flags for module writers */
int swaploaded;			/* set after swap driver and fs are loaded */
int last_module_id;

#define	KOBJ_SPACE	(1024 * 1024)

int kobj_map_space_len = KOBJ_SPACE;

struct devnames *devnamesp;
struct devnames orphanlist, deletedlist;
krwlock_t	devinfo_tree_lock;

#define	MAJBINDFILE "/etc/name_to_major"
#define	SYSBINDFILE "/etc/name_to_sysnum"

static char *majbind = MAJBINDFILE;
static char *sysbind = SYSBINDFILE;

extern int obpdebug;
#define	DEBUGGER_PRESENT	((boothowto & RB_DEBUG) || (obpdebug != 0))

void
mod_setup(void)
{
	char *sysname;
	struct sysent *callp;
	int callnum, exectype, strmod;
	int	num_devs;
	int	i;
	register char *lockname;

	extern kmutex_t class_lock;
	extern char *execswnames[];

	/*
	 * initialize the mod_lock before calling kobj_load_module() as
	 * mod_lock is used in kobj_load_module() (Bug: 1136729)
	 */
	mutex_init(&mod_lock, "loadable module lock", MUTEX_DEFAULT, NULL);
	cv_init(&mod_cv, "loadable module cv", CV_DEFAULT, NULL);

	/*
	 * Module arena lock
	 */
	mutex_init(&kobj_lock, "mod pool lock", MUTEX_DEFAULT, NULL);

	/*
	 * Sync up with the work that
	 * the stand-alone linker has
	 * already done.
	 */
	(void) kobj_sync();
	/*
	 * Initialize the list of loaded driver dev_ops.
	 * XXX - This must be done before reading the system file so that
	 * forceloads of drivers will work.
	 */
	(void) mod_sysctl(SYS_SET_KVAR, NULL);
	mutex_init(&mod_unload_lock, "autounload lock", MUTEX_DEFAULT, NULL);
	mutex_init(&mod_uninstall_lock, "module uninstall lock",
	    MUTEX_DEFAULT, NULL);
	cv_init(&mod_uninstall_cv, "module uninstall cv", CV_DEFAULT, NULL);
	mutex_init(&modconfig_lock, "modconfig lock", MUTEX_DEFAULT, NULL);
	cv_init(&modconfig_cv, "modconfig cv", CV_DEFAULT, NULL);
	mutex_init(&instub_lock, "modprintf lock", MUTEX_DEFAULT, NULL);

	/*
	 * Initialize data for the ddi orphan and deleted lists and
	 * the devinfo tree lock.  This stuff probably belongs in
	 * something called ddi_init.
	 */
	mutex_init(&(orphanlist.dn_lock), "dn orphs", MUTEX_DEFAULT, NULL);
	cv_init(&(orphanlist.dn_wait), "dn orphs", CV_DEFAULT, NULL);
	mutex_init(&(deletedlist.dn_lock), "dn del", MUTEX_DEFAULT, NULL);
	cv_init(&(deletedlist.dn_wait), "dn del", CV_DEFAULT, NULL);
	rw_init(&(devinfo_tree_lock), "Devinfo tree", RW_DEFAULT, NULL);

	/*
	 *	packing info is in .note section of
	 *	/platform/architecture/kernel/unix
	 *	or whatever was booted.
	 *
	 *	unix is the first in the modules chain
	 *	by convention.
	 */

	kobj_get_packing_info(modules.mod_filename);

	num_devs = read_binding_file(majbind, mb_hashtab);
	devcnt = num_devs + 30;		/* Some space for expansion */
	devopsp = (struct dev_ops **)
	    kmem_zalloc(devcnt * sizeof (struct dev_ops *), KM_NOSLEEP);

	for (i = 0; i < devcnt; i++)
		devopsp[i] = &mod_nodev_ops;

	init_devnamesp(devcnt);
	make_aliases(mb_hashtab);

	(void) read_binding_file(sysbind, sb_hashtab);
	init_syscallnames(NSYSCALL);

	/*
	 * Allocate loadable system call locks.
	 */
	for (callnum = 0, callp = sysent; callnum < NSYSCALL;
	    callnum++, callp++) {
		if (LOADABLE_SYSCALL(callp)) {
			if ((sysname = mod_getsysname(callnum)) != NULL) {
				callp->sy_lock = (krwlock_t *)
				    kobj_zalloc(sizeof (krwlock_t), KM_SLEEP);
				rw_init(callp->sy_lock, sysname,
				    RW_DEFAULT, DEFAULT_WT);
			} else {
				callp->sy_flags &= ~SE_LOADABLE;
				callp->sy_callc = nosys;
			}
		}
	}

	/*
	 * Allocate loadable exec locks.  (Note: Assumes all execs are loadable)
	 */
	for (exectype = 0; exectype < nexectype; exectype++) {
		execsw[exectype].exec_lock = (krwlock_t *)
			    kobj_zalloc(sizeof (krwlock_t), KM_SLEEP);
		lockname = (execswnames[exectype] ? execswnames[exectype] : "");
		rw_init(execsw[exectype].exec_lock, lockname,
			RW_DEFAULT, DEFAULT_WT);
	}
	mutex_init(&execsw_lock, "execsw[] lock", MUTEX_DEFAULT, DEFAULT_WT);

	/*
	 * Allocate lock for class[]
	 * Since particular scheduling classes are not "bound" to class ids
	 * like syscalls and execs (via name_to_sysnum file and execswnames)
	 * the locks are not allocated until after a class is "bound" to
	 * a cid.  (see getcid())
	 */
	mutex_init(&class_lock, "class[] lock", MUTEX_DEFAULT, DEFAULT_WT);

	/*
	 * Initialize f_lock filed for staticly bound streams.
	 * Initialize fmodsw_lock.
	 */
	for (strmod = 0; strmod < fmodcnt; strmod++) {
		if (fmodsw[strmod].f_name[0] != '\0')
			fmodsw[strmod].f_lock = STATIC_STREAM;
	}
	mutex_init(&fmodsw_lock, "fmodsw[] lock", MUTEX_DEFAULT, DEFAULT_WT);

	read_class_file();
}

struct modctla {
	int cmd;
};

struct modloada {
	int cmd;
	int use_path;
	char *filename;
};

struct modunloada {
	int cmd;
	int id;
};

struct modinfoa {
	int cmd;
	int id;
	struct modinfo *modinfo;
};

struct modres {
	int cmd;
	int id;
	int *data;
};

struct modconf {
	int cmd;
	int subcmd;
	int *data;
};

struct modbind {
	int cmd;
	char *name;
	int len;
	int *major;
};

static int modctl_modload(struct modloada *, rval_t *);
static int modctl_modunload(struct modunloada *, rval_t *);
static int modctl_modinfo(struct modinfoa *, rval_t *);
static int modctl_modreserve(struct modres *, rval_t *);
static int modctl_modconfig(struct modconf *, rval_t *);
static int modctl_add_major(struct modconf *, rval_t *);
static int modctl_getmodpath(struct modconf *, rval_t *);
static int modctl_read_sysbinding_file(void);
static int modctl_getmaj(struct modbind *, rval_t *);
static int modctl_getname(struct modbind *, rval_t *);

/*ARGSUSED1*/
static int
modctl_modload(struct modloada *uap, rval_t *rvp)
{
	register struct modctl *modp;
	int retval = 0;
	char *filenamep;

	if ((u_int)uap->filename >= USERLIMIT)
		return (EFAULT);

	filenamep = kmem_zalloc(MOD_MAXPATH, KM_SLEEP);

	if (copyinstr(uap->filename, filenamep, MOD_MAXPATH, 0)) {
		retval = EFAULT;
		goto out;
	}

	filenamep[MOD_MAXPATH - 1] = 0;
	modp = mod_hold_installed_mod(filenamep, uap->use_path, &retval);

	if (modp == NULL)
		goto out;

	modp->mod_loadflags |= MOD_NOAUTOUNLOAD;
	rvp->r_val1 = modp->mod_id;
	mod_release_mod(modp);
out:
	kmem_free(filenamep, MOD_MAXPATH);

	return (retval);
}

/*ARGSUSED1*/
static int
modctl_modunload(struct modunloada *uap, rval_t *rvp)
{
	if (uap->id == 0) {
		mod_uninstall_all();
#ifdef CANRELOAD
		modreap();
#endif
		return (0);
	} else
		return (modunload(uap->id));
}

/*ARGSUSED1*/
static int
modctl_modinfo(struct modinfoa *uap, rval_t *rvp)
{
	int retval;
	struct modinfo modi;

	if ((u_int)uap->modinfo >= USERLIMIT)
		return (EFAULT);

	copyin((caddr_t)uap->modinfo, (caddr_t)&modi, sizeof (struct modinfo));
	retval = modinfo(uap->id, &modi);

	if (retval == 0)
		if (copyout((caddr_t)&modi, (caddr_t)uap->modinfo,
		    sizeof (struct modinfo)) != 0)
			retval = EFAULT;
	return (retval);
}

/*
 * Return the last major number in the range of permissible major numbers.
 */
/*ARGSUSED1*/
static int
modctl_modreserve(struct modres *uap, rval_t *rvp)
{
	if (copyout((caddr_t)&devcnt,
	    (caddr_t)uap->data, sizeof (int)) != 0)
		return (EFAULT);
	return (0);
}

/*ARGSUSED1*/
static int
modctl_modconfig(struct modconf *uap, rval_t *rvp)
{
	struct modconfig *mcp;
	major_t major;
	int error;

	/*
	 * We only want to allow one thread at a time to do a MODCONFIG
	 * modctl() call.  All other threads will block but can return
	 * if a signal (such as ^C) is received.
	 */
	mutex_enter(&modconfig_lock);
	while (modconfig_busy) {
		if (cv_wait_sig(&modconfig_cv, &modconfig_lock) == 0) {
			mutex_exit(&modconfig_lock);
			return (EINTR);
		}
	}
	modconfig_busy = 1;
	mutex_exit(&modconfig_lock);

	mcp = kmem_zalloc(sizeof (struct modconfig), KM_SLEEP);

	if (copyin((caddr_t)(uap->data), (caddr_t)mcp,
	    sizeof (struct modconfig)) != 0)
		error = EFAULT;
	else {
		if (mcp->drvname[0] != '\0') {
			if ((major = ddi_name_to_major(mcp->drvname)) != -1) {
				if (make_devname(mcp->drvname, major) == 0)
					error = make_one_node(major, mcp);
				else
					error = EINVAL;
			} else
				error = EINVAL;
		} else {
			make_all_nodes(mcp);
			error = 0;
		}
	}

	kmem_free(mcp, sizeof (struct modconfig));

	mutex_enter(&modconfig_lock);
	modconfig_busy = 0;
	cv_signal(&modconfig_cv);
	mutex_exit(&modconfig_lock);

	return (error);
}

/*ARGSUSED1*/
static int
modctl_add_major(struct modconf *uap, rval_t *rvp)
{
	struct modconfig mc;
	int i;
	struct aliases alias;
	struct aliases *ap;
	char name[256];
	char *drvname;

	if (!suser(CRED()))
		return (EPERM);
	bzero((caddr_t)&mc, sizeof (struct modconfig));
	if (copyin((caddr_t)(uap->data), (caddr_t)&mc,
	    sizeof (struct modconfig)) != 0)
		return (EFAULT);
	if ((drvname = ddi_major_to_name(mc.major)) != NULL &&
	    strcmp(drvname, mc.drvname) != 0)
		return (EINVAL);
	ap = mc.ap;
	for (i = 0; i < mc.num_aliases; i++) {
		bzero((caddr_t)&alias, sizeof (struct aliases));
		if (copyin((caddr_t)ap, (caddr_t)&alias,
		    sizeof (struct aliases)) != 0)
			return (EFAULT);
		if (copyin(alias.a_name, (caddr_t)name,
		    alias.a_len) != 0)
			return (EFAULT);
		make_mbind(name, mc.major, mb_hashtab);
		ap = alias.a_next;
	}
	if (mc.drvclass[0] != '\0')
		add_class(mc.drvname, mc.drvclass);
	make_mbind(mc.drvname, mc.major, mb_hashtab);
	return (make_devname(mc.drvname, mc.major));
}

/*ARGSUSED1*/
static int
modctl_getmodpath(struct modconf *uap, rval_t *rvp)
{
	if (copyout((caddr_t)default_path,
	    (caddr_t)uap->data, strlen(default_path) + 1) != 0)
		return (EFAULT);
	return (0);
}

static int
modctl_read_sysbinding_file(void)
{
	(void) read_binding_file(sysbind, sb_hashtab);
	return (0);
}

/*ARGSUSED1*/
static int
modctl_getmaj(struct modbind *uap, rval_t *rvp)
{
	char name[256];
	int retval;
	major_t major;

	if ((retval = copyinstr((caddr_t)(uap->name), (caddr_t)name,
	    uap->len < 256 ? uap->len : 256, 0)) != 0)
		return (retval);
	if ((major = ddi_name_to_major(name)) == -1)
		return (ENODEV);
	if (copyout((caddr_t)&major,
	    (caddr_t)uap->major, sizeof (major_t)) != 0)
		return (EFAULT);
	return (0);
}

/*ARGSUSED1*/
static int
modctl_getname(struct modbind *uap, rval_t *rvp)
{
	char *name;
	major_t major;

	if (copyin((caddr_t)(uap->major), (caddr_t)&major, 4) != 0)
		return (EFAULT);

	if ((name = ddi_major_to_name(major)) == NULL)
		return (ENODEV);
	if ((strlen(name) + 1) > uap->len)
		return (ENOSPC);
	return (copyoutstr((caddr_t)name,
		(caddr_t)uap->name, uap->len, NULL));
}

int
modctl(register struct modctla *uap, rval_t *rvp)
{
	if (!suser(CRED()) && uap->cmd != MODINFO)
		return (EPERM);

	switch (uap->cmd) {
	case MODLOAD:		/* load a module */
		return (modctl_modload((struct modloada *)uap, rvp));

	case MODUNLOAD:		/* unload a module */
		return (modctl_modunload((struct modunloada *)uap, rvp));

	case MODINFO:		/* get module status */
		return (modctl_modinfo((struct modinfoa *)uap, rvp));

	case MODRESERVED:	/* get last major number in range */
		return (modctl_modreserve((struct modres *)uap, rvp));

	case MODCONFIG:		/* build device tree */
		return (modctl_modconfig((struct modconf *)uap, rvp));

	case MODADDMAJBIND:	/* read major binding file */
		return (modctl_add_major((struct modconf *)uap, rvp));

	case MODGETPATH:	/* get modpath */
		return (modctl_getmodpath((struct modconf *)uap, rvp));

	case MODREADSYSBIND:	/* read system call binding file */
		return (modctl_read_sysbinding_file());

	case MODGETMAJBIND:	/* get major number for named device */
		return (modctl_getmaj((struct modbind *)uap, rvp));

	case MODGETNAME:	/* get name of device given major number */
		return (modctl_getname((struct modbind *)uap, rvp));

	default:
		return (EINVAL);
	}
}

/*
 * This is the primary kernel interface to load a module.
 *
 * This version loads and installs the named module.
 */
int
modload(char *subdir, char *filename)
{
	register struct modctl *modp;
	register int size, id;
	register char *fullname;
	int retval;

	if (subdir != NULL) {
		/*
		 * allocate enough space for <subdir>/<filename><NULL>
		 */
		size = strlen(subdir) + strlen(filename) + 2;
		fullname = kmem_zalloc(size, KM_SLEEP);
		sprintf(fullname, "%s/%s", subdir, filename);
	} else {
		fullname = filename;
	}

	modp = mod_hold_installed_mod(fullname, 1, &retval);
	if (modp) {
		id = modp->mod_id;
		mod_release_mod(modp);
	}

	if (subdir != NULL)
		kmem_free(fullname, size);

	if (retval == 0) {
		CPU_STAT_ADDQ(CPU, cpu_sysinfo.modload, 1);
		return (id);
	}

	return (-1);
}

/*
 * Load a module.
 */
int
modloadonly(char *subdir, char *filename)
{
	register struct modctl *modp;
	register char *fullname;
	register int size, id;
	int retval;

	if (subdir != NULL) {
		/*
		 * allocate enough space for <subdir>/<filename><NULL>
		 */
		size = strlen(subdir) + strlen(filename) + 2;
		fullname = kmem_zalloc(size, KM_SLEEP);
		sprintf(fullname, "%s/%s", subdir, filename);
	} else {
		fullname = filename;
	}

	modp = mod_hold_loaded_mod(fullname, 1, &retval);
	if (modp) {
		id = modp->mod_id;
		mod_release_mod(modp);
	}

	if (subdir != NULL)
		kmem_free(fullname, size);

	if (retval == 0)
		return (id);
	return (-1);
}

/*
 * Uninstall and unload a module.
 */
int
modunload(register int id)
{
	register struct modctl *modp;
	int retval;

	if ((modp = mod_hold_by_id((modid_t)id)) == NULL)
		return (EINVAL);

	if (modp->mod_loadflags & MOD_PACKED) {
		retval = EBUSY;
		goto out;
	}

	if ((retval = moduninstall(modp)) == 0) {
		retval = mod_unload(modp);
		if (retval != 0) {
			cmn_err(CE_WARN, "%s uninstalled but not unloaded",
				modp->mod_filename);
		}
		else
			CPU_STAT_ADDQ(CPU, cpu_sysinfo.modunload, 1);
	}
out:
	mod_release_mod(modp);
	return (retval);
}

/*
 * Return status of a loaded module.
 */
static int
modinfo(register int id, register struct modinfo *modinfop)
{
	struct modctl *modp;
	modid_t mid;

	mid = modinfop->mi_id;
	if (modinfop->mi_info & MI_INFO_ALL) {
		while ((modp = mod_hold_next_by_id(mid++)) != NULL) {
			if ((modinfop->mi_info & MI_INFO_CNT) ||
			    modp->mod_installed)
				break;
			mod_release_mod(modp);
		}
		if (modp == NULL)
			return (EINVAL);
	} else {
		modp = mod_hold_by_id(id);
		if (modp == NULL)
			return (EINVAL);
		if (!(modinfop->mi_info & MI_INFO_CNT) &&
		    !modp->mod_installed) {
			mod_release_mod(modp);
			return (EINVAL);
		}
	}

	modinfop->mi_state = 0;
	if (modp->mod_loaded) {
		modinfop->mi_state = MI_LOADED;
		kobj_getmodinfo(modp->mod_mp, modinfop);
	}
	if (modp->mod_installed) {
		modinfop->mi_state |= MI_INSTALLED;
		(void) mod_getinfo(modp, modinfop);
	}

	modinfop->mi_id = modp->mod_id;
	modinfop->mi_loadcnt = modp->mod_loadcnt;
	strcpy(modinfop->mi_name, modp->mod_modname);

	mod_release_mod(modp);
	return (0);
}

static char *mod_stub_err = "mod_hold_stub: Couldn't load stub module %s";
static char *no_err = "No error function for weak stub %s";

/*
 * used by the stubs themselves to load and hold a module.
 * Returns 0 if the stub doesn't need to call mod_release_stub.
 *	   1 if the stub should call mod_release_stub.
 *	   -1 if the stub should just call the err_fcn.
 * Note that this code is stretched out so that we avoid subroutine calls
 * and optimize for the most likely case.  That is,the case where the
 * module is loaded and installed and not held.  In that case we just inc
 * the mod_stub flag and continue.
 */

int
mod_hold_stub(struct mod_stub_info *stub)
{
	register struct modctl *mp;
	register struct mod_modinfo *mip;

	mip = stub->mods_modinfo;

	mutex_enter(&mod_lock);
	/*
	 * This gross flag is to prevent stack overflow when using modprintf
	 * See comment in modprintf
	 */
	instubs++;
	/* we do mod_hold_by_modctl inline for speed */
mod_check_again:
	if ((mp = mip->mp) != NULL) {
		if (mp->mod_busy == 0) {
			if (mp->mod_installed) {
			/* no one home so grab the stub lock if installed */
				mp->mod_stub++;
				instubs--;
				mutex_exit(&mod_lock);
				return (0);
			} else {
				mp->mod_busy = 1;
				mp->mod_inprogress_thread =
					(curthread == NULL ? (kthread_id_t)-1 :
					    curthread);
			}
		/* Busy, check to see if this thread holds the lock */
		} else if (mp->mod_inprogress_thread ==
		    (curthread == NULL ? (kthread_id_t)-1 : curthread)) {
			/* mod_circdep */
			if (!mp->mod_busy || !mp->mod_installed) {
				mutex_exit(&mod_lock);
				cmn_err(CE_PANIC,
				    "stub not busy or not installed");
			}
			/*
			 * At this point module is held by this thread
			 * so no need to call mod_release_stub.
			 */
			mp->mod_stub++;
			instubs--;
			mutex_exit(&mod_lock);
			return (0);
		} else {
			/* gotta wait */
			if (mod_hold_by_modctl(mp))
				goto mod_check_again;
			/*
			 * what we have now may have been unloaded!, in
			 * that case, mip->mp will be NULL, we'll hit this
			 * module and load again..
			 */
			cmn_err(CE_PANIC, "mod_hold_stub should have blocked");
		}
		mutex_exit(&mod_lock);
	} else {
		/* first time we've hit this module */
		mutex_exit(&mod_lock);
		mp = mod_hold_by_name(mip->modm_module_name);
		mip->mp = mp;
	}
	ASSERT(mp != NULL);
	/* In most cases, module will be installed */
	if (!mp->mod_installed) {
		/* Module not loaded, if weak stub don't load it */
		if (stub->mods_weak) {
			if (stub->mods_errfcn == NULL) {
				mod_release_mod(mp);
				cmn_err(CE_PANIC, no_err,
				    mip->modm_module_name);
			}
		} else {
			/* Not a weak stub so load the module */
			if (mod_load(mp, 1) != 0 || modinstall(mp) != 0) {
				mod_release_mod(mp);
				if (stub->mods_errfcn == NULL) {
					cmn_err(CE_PANIC, mod_stub_err,
					    mip->modm_module_name);
				} else {
					mutex_enter(&mod_lock);
					instubs--;
					mutex_exit(&mod_lock);
					return (-1);
				}
			}
		}
	}
	/* we're holding the busy bit so we can just inc the stub count */
	mp->mod_stub++;
	/* Module is held and loaded */
	mutex_enter(&mod_lock);
	instubs--;
	mutex_exit(&mod_lock);
	return (1);
}

void
mod_release_stub(struct mod_stub_info *stub, int hold_flag)
{
	register struct modctl *mp;

	/* inline mod_release_mod */
	mp = stub->mods_modinfo->mp;
	mutex_enter(&mod_lock);
	if (!hold_flag) {
		mp->mod_stub--;
		if (mp->mod_want)
			cv_broadcast(&mod_cv);
		mutex_exit(&mod_lock);
		return;
	}
	ASSERT(mp->mod_busy);
	ASSERT(mp->mod_stub == 1);
	mp->mod_busy = 0;
	mp->mod_inprogress_thread = NULL;
	mp->mod_stub--;
	if (mp->mod_want)
		cv_broadcast(&mod_cv);
	mutex_exit(&mod_lock);
}

static struct modctl *
mod_hold_loaded_mod(char *filename, int usepath, int *status)
{
	register struct modctl *modp;
	register int retval;
	/*
	 * Hold the module.
	 */
	modp = mod_hold_by_name(filename);
	if (modp) {
		retval = mod_load(modp, usepath);
		if (retval != 0) {
			mod_release_mod(modp);
			modp = NULL;
		}
		*status = retval;
	} else {
		*status = ENOSPC;
	}
	return (modp);
}

static struct modctl *
mod_hold_installed_mod(char *name, int usepath, int *r)
{
	struct modctl *modp;
	register int retval;

	/*
	 * Hold the module.
	 */
	modp = mod_hold_by_name(name);
	if (modp) {
		retval = mod_load(modp, usepath);
		if (retval != 0) {
			mod_release_mod(modp);
			modp = NULL;
			*r = retval;
		} else {
			if ((*r = modinstall(modp)) != 0) {
				/*
				 * We loaded it, but failed to _init() it.
				 * Be kind to developers -- force it
				 * out of memory now so that the next
				 * attempt to use the module will cause
				 * a reload.  See 1093793.
				 */
				(void) mod_unload(modp);
				mod_release_mod(modp);
				modp = NULL;
			}
		}
	} else {
		*r = ENOSPC;
	}
	return (modp);
}

static char
	*mod_excl_err = "module %s(%s) is EXCLUDED and will not be loaded\n";
static char *mod_init_err = "loadmodule:%s(%s): _init() error %d\n";

/*
 * This routine is needed for dependencies.  Users specify dependencies
 * by declaring a character array initialized to filenames of dependents.
 * So the code that handles dependents deals with filenames (and not
 * module names) because that's all it has.  We load by filename and once
 * we've loaded a file we can get the module name.
 * Unfortunately there isn't a single unified filename/modulename namespace.
 * C'est la vie.
 *
 * We allow the name being looked up to be prepended by an optional
 * subdirectory e.g. we can lookup (NULL, "fs/ufs") or ("fs", "ufs")
 */
struct modctl *
mod_find_by_filename(char *subdir, char *filename)
{
	register struct modctl *modp;
	register int sublen;

	mutex_enter(&mod_lock);
	/* ASSERT(MUTEX_HELD(&mod_lock));	XXX bug - not obeyed */
	if (subdir != NULL)
		sublen = strlen(subdir);
	else
		sublen = 0;

	for (modp = modules.mod_next; modp != &modules; modp = modp->mod_next)
		if (sublen) {
			register char *mod_filename = modp->mod_filename;

			if (strncmp(subdir, mod_filename, sublen) == 0 &&
			    mod_filename[sublen] == '/' &&
			    strcmp(filename, &mod_filename[sublen + 1]) == 0)
				break;
		} else
			if (strcmp(filename, modp->mod_filename) == 0)
				break;

	mutex_exit(&mod_lock);
	if (modp == &modules)
		modp = NULL;
	return (modp);
}

/*
 * Check for circular dependencies.  This is called from do_dependents()
 * in kobj.c.  If we are the thread already loading this module, then
 * we're trying to load a dependent that we're already loading which
 * means the user specified circular dependencies.
 */
static int
mod_circdep(struct modctl *modp)
{
	register kthread_id_t thread;

	thread = (curthread == NULL ? (kthread_id_t)-1 : curthread);
	return (modp->mod_inprogress_thread == thread);
}

static struct modlinkage *
mod_getinfo(
	register struct modctl *modp,
	register struct modinfo *modinfop)
{
	struct modlinkage *(*func)();
	struct modlinkage *retval;

	ASSERT(modp->mod_busy);

	func = (struct modlinkage *(*)())kobj_lookup(modp->mod_mp, "_info");

	if (kobj_addrcheck(modp->mod_mp, (caddr_t)func)) {
		modprintf("_info() not defined properly\n");
		return (NULL);
	}

	retval = (*func)(modinfop);  	/* call _info() function */

	if (moddebug & MODDEBUG_USERDEBUG)
		modprintf("Returned from _info, retval = %x\n", retval);

	return (retval);
}

static void
modadd(struct modctl *mp)
{
	ASSERT(MUTEX_HELD(&mod_lock));

	mp->mod_id = last_module_id++;
	mp->mod_next = &modules;
	mp->mod_prev = modules.mod_prev;
	modules.mod_prev->mod_next = mp;
	modules.mod_prev = mp;
}

/*ARGSUSED*/
static struct modctl *
allocate_modp(char *filename, char *modname)
{
	register struct modctl *mp;

	mp = (struct modctl *)kobj_zalloc(sizeof (*mp), KM_SLEEP);

	mp->mod_modname = kobj_zalloc(strlen(modname) + 1, KM_SLEEP);
	strcpy(mp->mod_modname, modname);
	return (mp);
}

#ifdef	notdef
static void
free_modp(struct modctl *modp)
{
	if (modp->mod_filename)
		kobj_free(modp->mod_filename, strlen(modp->mod_filename) + 1);
	kobj_free(modp, sizeof (*modp));
}
#endif	/* unused code */

/*
 * Get the value of a symbol.  This is a wrapper routine that
 * calls kobj_getsymvalue().  kobj_getsymvalue() may go away but this
 * wrapper will prevent callers from noticing.
 */
unsigned int
modgetsymvalue(char *name, int kernelonly)
{
	return (kobj_getsymvalue(name, kernelonly));
}

/*
 * Get the symbol nearest an address.  This is a wrapper routine that
 * calls kobj_getsymname().  kobj_getsymname() may go away but this
 * wrapper will prevent callers from noticing.
 */
char *
modgetsymname(unsigned int value, unsigned int *offset)
{
	return (kobj_getsymname(value, offset));
}

/*
 * Lookup a symbol in a specified module.  This is a wrapper routine that
 * calls kobj_lookup().  kobj_lookup() may go away but this
 * wrapper will prevent callers from noticing.
 */
u_long
modlookup(char *modname, char *symname)
{
	struct modctl *modp;
	u_long val;

	if ((modp = mod_hold_by_name(modname)) == NULL)
		return (0);
	val = (u_long)kobj_lookup(modp->mod_mp, symname);
	mod_release_mod(modp);
	return (val);
}

/*
 * Ask the user for the name of the system file and the default path
 * for modules.
 */
#define	MAXINPUTLEN 64		/* this should go somewhere else */

void
mod_askparams()
{
	static char s0[MAXINPUTLEN];

	int fd;

	if ((fd = kobj_open(systemfile)) != -1)
		kobj_close(fd);
	else
		systemfile = NULL;

	/*CONSTANTCONDITION*/
	while (1) {
		modprintf("Name of system file [%s]:  ",
			systemfile ? systemfile : "/dev/null");

		gets(s0);

		if (s0[0] == '\0')
			break;
		else if (strcmp(s0, "/dev/null") == 0) {
			systemfile = NULL;
			break;
		} else {
			if ((fd = kobj_open(s0)) != -1) {
				kobj_close(fd);
				systemfile = s0;
				break;
			}
		}
		modprintf("can't find file %s\n", s0);
	}
}

/*
 * Control automatic unloading of modules. A count is incremented each
 * time a disable request is made, and decremented each time a enable
 * request is made. Much better interface than the moddebug variable.
 *
 * The static req_state variable is protected by it's own mutex lock,
 * mod_unload_lock.
 *
 * Two requests are supported:
 * MOD_AUTOUNLOAD_ON - please turn on module autounloading.
 * MOD_AUTOUNLOAD_OFF - please turn off module autounloading.
 * MOD_AUTOUNLOAD_STATUS - please return the autounload request status.
 *
 * The subsequent value of req_state is returned. If it is 0, then module
 * autounloading is permitted.
 */
int
modunloadctrl(enum mod_unload_ctrl request)
{
	static int req_state;

	ASSERT(MUTEX_HELD(&mod_unload_lock));

	switch (request) {
	case MOD_AUTOUNLOAD_OFF:
		++req_state;
		break;
	case MOD_AUTOUNLOAD_ON:
		if (req_state > 0)
			--req_state;
		break;
	case MOD_AUTOUNLOAD_STATUS:
		break;	/* do nothing - just return the state. */
	}
	return (req_state);
}

static char *loading_msg = "loading '%s' id %d\n";
static char *load_msg = "load '%s' id %d loaded @ 0x%x/0x%x size %d/%d\n";

static int
mod_load(struct modctl *mp, int usepath)
{
	register int status = 0;
	register int driver_mutex = 0;
	register struct modinfo *modinfop = NULL;

	ASSERT(MUTEX_NOT_HELD(&mod_lock));
	ASSERT(mp->mod_busy);

	if (mp->mod_loaded)
		return (0);

	if (mod_sysctl(SYS_CHECK_EXCLUDE, mp->mod_modname) != 0 ||
	    mod_sysctl(SYS_CHECK_EXCLUDE, mp->mod_filename) != 0) {
		if (moddebug & MODDEBUG_LOADMSG) {
			modprintf(mod_excl_err, mp->mod_filename,
				mp->mod_modname);
		}
		return (ENXIO);
	}
	if (UNSAFE_DRIVER_LOCK_HELD()) {
		driver_mutex = 1;
		mutex_exit(&unsafe_driver);
	}
	if (moddebug & MODDEBUG_LOADMSG2)
		modprintf(loading_msg, mp->mod_filename, mp->mod_id);

	kobj_load_module(mp, usepath);

	if (mp->mod_mp) {
		mp->mod_loaded = 1;
		mp->mod_loadcnt++;
		if (moddebug & MODDEBUG_LOADMSG) {
			modprintf(load_msg, mp->mod_filename, mp->mod_id,
				((struct module *)mp->mod_mp)->text,
				((struct module *)mp->mod_mp)->data,
				((struct module *)mp->mod_mp)->text_size,
				((struct module *)mp->mod_mp)->data_size);
		}
	} else {
		status = ENOENT;
		if (moddebug & MODDEBUG_ERRMSG) {
			modprintf("error loading '%s', program '%s'\n",
				mp->mod_filename,
				u.u_comm ? u.u_comm : "unix");
		}
	}
	if (driver_mutex)
		mutex_enter(&unsafe_driver);
	if (status == 0) {
		modinfop = (struct modinfo *)kmem_zalloc(
				sizeof (struct modinfo), KM_SLEEP);
		/*
		 * XXX - There *MUST* be a better way to get this.
		 */
		mp->mod_linkage =  mod_getinfo(mp, modinfop);
		kmem_free(modinfop, sizeof (struct modinfo));
		(void) mod_sysctl(SYS_SET_MVAR, (void *)mp);
		install_stubs_by_name(mp, mp->mod_modname);
	}
	return (status);
}

static char *unload_msg = "unloading %s, module id %d, loadcnt %d.\n";

static int
mod_unload(struct modctl *mp)
{
	register int status = EBUSY;

	ASSERT(MUTEX_NOT_HELD(&mod_lock));
	ASSERT(mp->mod_busy);

	if (mp->mod_loadflags & MOD_PACKED)
		return (status); /* EBUSY */

	if (mod_hold_dependents(mp, 0)) {
		if (!mp->mod_installed && mp->mod_loaded) {
			if (moddebug & MODDEBUG_LOADMSG)
				modprintf(unload_msg, mp->mod_modname,
					mp->mod_id, mp->mod_loadcnt);
			/* reset stub functions to call the binder again */
			reset_stubs(mp);
			kobj_unload_module(mp); /* free the memory */
			mp->mod_loaded = 0;
			mp->mod_linkage = NULL;
			status = 0;
		}
		if (!mp->mod_installed && !mp->mod_loaded)
			status = 0;
		mod_release_dependents(mp);
	}
	return (status);
}

static int
modinstall(struct modctl *mp)
{
	register int val;
	int (*func)(void);

	ASSERT(MUTEX_NOT_HELD(&mod_lock));
	ASSERT(mp->mod_busy && mp->mod_loaded);

	if (mp->mod_installed)
		return (0);

	if (moddebug & MODDEBUG_LOADMSG)
		modprintf("installing %s, module id %d.\n",
			mp->mod_modname, mp->mod_id);

	ASSERT(mp->mod_mp != NULL);
	if (mod_install_requisites(mp) == -1) {
		(void) mod_unload(mp);
		return (ENXIO);
	}

	if (moddebug & MODDEBUG_ERRMSG) {
		modprintf("init '%s' id %d loaded @ 0x%x/0x%x size %d/%d\n",
			mp->mod_filename, mp->mod_id,
			((struct module *)mp->mod_mp)->text,
			((struct module *)mp->mod_mp)->data,
			((struct module *)mp->mod_mp)->text_size,
			((struct module *)mp->mod_mp)->data_size);
	}

	func = (int (*)())kobj_lookup(mp->mod_mp, "_init");

	if (kobj_addrcheck(mp->mod_mp, (caddr_t)func)) {
		modprintf("_init() is not defined properly\n");
		return (EFAULT);
	}

	if (moddebug & MODDEBUG_USERDEBUG) {
		modprintf("breakpoint before calling _init()\n");
			if (DEBUGGER_PRESENT)
				debug_enter("_init");
	}

	val = (*func)();		/* call _init */

	if (moddebug & MODDEBUG_USERDEBUG)
		modprintf("Returned from _init, val = %x\n", val);

	if (val == 0)
		mp->mod_installed = 1;
	else if (moddebug & MODDEBUG_ERRMSG)
		modprintf(mod_init_err, mp->mod_filename, mp->mod_modname, val);

	return (val);
}

static char *finidef = "_fini() not defined properly in %s\n";
static char *finiret = "Returned from _fini for %s, status = %x\n";

static int
moduninstall(struct modctl *mp)
{
	register int status = 0;
	int (*func)(void);

	ASSERT(MUTEX_NOT_HELD(&mod_lock));
	ASSERT(mp->mod_busy);

	if (!mp->mod_installed)
		return (0);

	ASSERT(mp->mod_loaded);

	if (!mod_hold_dependents(mp, 1)) {
		status = EBUSY;
	} else {
		/*
		 * mod_hold_dependents() may give up the busy flag
		 * so we need to check to see that the module is
		 * still installed.
		 */
		if (mp->mod_installed) {
			if (moddebug & MODDEBUG_LOADMSG2)
				modprintf("uninstalling %s\n", mp->mod_modname);

			func = (int (*)())kobj_lookup(mp->mod_mp, "_fini");
			if (func == NULL) {
				status = EBUSY;	/* can't be unloaded */
				goto out;
			}

			if (kobj_addrcheck(mp->mod_mp, (caddr_t)func)) {
				modprintf(finidef, mp->mod_filename);
				status = EFAULT;
				goto out;
			}

			status = (*func)();  	/* call _fini() */
			if (status == 0 && (moddebug & MODDEBUG_LOADMSG))
				modprintf("uninstalled %s\n", mp->mod_modname);

			if (moddebug & MODDEBUG_USERDEBUG)
				modprintf(finiret, mp->mod_filename, status);

			if (status == 0)
				mp->mod_installed = 0;
		}
out:
		mod_release_dependents(mp);
	}
	return (status);
}

/*
 * Uninstall all modules.
 */

static void
mod_uninstall_all()
{
	register struct modctl *mp;
	register modid_t modid = 0;
	int status;

	do {
		while ((mp = mod_hold_next_by_id(modid)) != NULL) {
			/*
			 * If we were called from the uninstall daemon
			 * and the MOD_NOAUTOUNLOAD flag or
			 * MOD_PACKED flag is set, skip this module.
			 */
			if ((mp->mod_loadflags & MOD_NOAUTOUNLOAD) ||
				(mp->mod_loadflags & MOD_PACKED)) {
				modid = mp->mod_id;
				mod_release_mod(mp);
			} else {
				break;
			}
		}

		/*
		 * mp points to a held module.
		 */
		if (mp) {
			status = moduninstall(mp);
#ifndef CANRELOAD
			if (status == 0)
				status = mod_unload(mp);
#endif
			mod_release_mod(mp);
			modid = mp->mod_id;
		} else {
			modid = 0;
		}
	} while (modid != 0);
}

kthread_id_t mod_aul_thread;

void
mod_uninstall_daemon(void)
{
	callb_cpr_t cprinfo;
	mod_aul_thread = curthread;

	CALLB_CPR_INIT(&cprinfo, &mod_uninstall_lock, callb_generic_cpr, "mud");
	for (;;) {
		mutex_enter(&mod_uninstall_lock);
		CALLB_CPR_SAFE_BEGIN(&cprinfo);
		cv_wait(&mod_uninstall_cv, &mod_uninstall_lock);
		/*
		 * The whole daemon is safe for CPR except we don't want
		 * the daemon to run if FREEZE is issued and this daemon
		 * wakes up from the cv_wait above. In this case, it'll be
		 * blocked in CALLB_CPR_SAFE_END until THAW is issued.
		 *
		 * The reason of calling CALLB_CPR_SAFE_BEGIN twice is that
		 * mod_uninstall_lock is used to protect cprinfo and
		 * CALLB_CPR_SAFE_BEGIN assumes that this lock is held when
		 * called.
		 */
		CALLB_CPR_SAFE_END(&cprinfo, &mod_uninstall_lock);
		CALLB_CPR_SAFE_BEGIN(&cprinfo);
		mutex_exit(&mod_uninstall_lock);
		mutex_enter(&mod_unload_lock);
		if ((modunloadctrl(MOD_AUTOUNLOAD_STATUS) == 0) &&
		    ((moddebug & MODDEBUG_NOAUTOUNLOAD) == 0))
			mod_uninstall_all();

		mutex_exit(&mod_unload_lock);
	}
}

/*
 * Unload all uninstalled modules.
 */

void
modreap(void)
{
#ifdef CANRELOAD
	register struct modctl *mp;

	for (mp = modules.mod_next; mp != &modules; mp = mp->mod_next) {
		mutex_enter(&mod_lock);
		while (mod_hold_by_modctl(mp) == 1)
				continue;
		mutex_exit(&mod_lock);
		(void) mod_unload(mp);
		mod_release_mod(mp);
	}
#endif
	mutex_enter(&mod_uninstall_lock);
	cv_broadcast(&mod_uninstall_cv);
	mutex_exit(&mod_uninstall_lock);
}

/*
 * Hold the specified module.
 *
 * Return values:
 *	 0 ==> the module was held without "sleeping."
 *	 1 ==> the module was held and the current thread "slept."
 *
 * This is the module holding primitive.
 */
int
mod_hold_by_modctl(struct modctl *mp)
{
	register int driver_mutex = 0;

	ASSERT(MUTEX_HELD(&mod_lock));

	if (mp->mod_busy || mp->mod_stub) {
		if (UNSAFE_DRIVER_LOCK_HELD()) {
			driver_mutex = 1;
			mutex_exit(&unsafe_driver);
		}
		mp->mod_want = 1;
		cv_wait(&mod_cv, &mod_lock);
		mp->mod_want = 0;
		if (driver_mutex)
			mutex_enter(&unsafe_driver);
		/*
		 * Module may be unloaded by daemon.
		 * Nevertheless, modctl structure is still in linked list
		 * (i.e., off &modules), not freed!
		 * Caller is not supposed to assume "mp" is valid, but there
		 * is no reasonable way to detect this but using
		 * mp->mod_modinfo->mp == NULL check (follow the back pointer)
		 *   (or similar check depending on calling context)
		 * DON'T free modctl structure, it will be very problematic.
		 */
		return (1);	/* caller has to decide whether to re-try */
	}
	mp->mod_inprogress_thread =
		(curthread == NULL ? (kthread_id_t)-1 : curthread);
	mp->mod_busy = 1;
	return (0);
}

static struct modctl *
mod_hold_by_name(char *filename)
{
	register char *modname;
	register struct modctl *mp;
	char *curname, *newname;

	mutex_enter(&mod_lock);

	if ((modname = strrchr(filename, '/')) == NULL)
		modname = filename;
	else
		modname++;

	for (mp = modules.mod_next; mp != &modules; mp = mp->mod_next) {
		if (strcmp(modname, mp->mod_modname) == 0) {
			break;
		}
	}
	if (mp == &modules) { /* Not found */
		mp = allocate_modp(filename, modname);
		modadd(mp);
	}

	/*
	 * If the module was held, then it must be us who has it held.
	 */

	if (mod_circdep(mp))
		mp = NULL;
	else {
		while (mod_hold_by_modctl(mp) == 1)
			continue;

		/*
		 * If the name hadn't been set or has changed, allocate
		 * space and set it.  Free space used by previous name.
		 */
		curname = mp->mod_filename;
		if (curname == NULL || (curname != filename &&
		    modname != filename &&
		    strcmp(curname, filename) != 0)) {
			newname = kobj_zalloc(strlen(filename) + 1, KM_SLEEP);
			strcpy(newname, filename);
			mp->mod_filename = newname;
			if (curname != NULL)
				kmem_free(curname, strlen(curname) + 1);
		}
	}

	mutex_exit(&mod_lock);
	if (mp && moddebug & MODDEBUG_LOADMSG2)
		modprintf("Holding %s\n", mp->mod_filename);
	if (mp == NULL && moddebug & MODDEBUG_LOADMSG2)
		modprintf("circular dependency loading %s\n", filename);
	return (mp);
}

static struct modctl *
mod_hold_by_id(modid_t modid)
{
	register struct modctl *mp;

	mutex_enter(&mod_lock);

	for (mp = modules.mod_next; mp != &modules && mp->mod_id != modid;
	    mp = mp->mod_next)
		;

	if ((mp == &modules) || mod_circdep(mp))
		mp = NULL;
	else {
		while (mod_hold_by_modctl(mp) == 1)
			continue;
	}

	mutex_exit(&mod_lock);
	return (mp);
}

static struct modctl *
mod_hold_next_by_id(modid_t modid)
{
	register struct modctl *mp;

	mutex_enter(&mod_lock);

	for (mp = modules.mod_next; mp != &modules && mp->mod_id <= modid;
	    mp = mp->mod_next)
		;

	if ((mp == &modules) || mod_circdep(mp))
		mp = NULL;
	else {
		while (mod_hold_by_modctl(mp) == 1)
			continue;
	}

	mutex_exit(&mod_lock);
	return (mp);
}

static void
mod_release(struct modctl *mp)
{
	ASSERT(MUTEX_HELD(&mod_lock) && mp->mod_busy);
	mp->mod_busy = 0;
	mp->mod_inprogress_thread = NULL;
	cv_broadcast(&mod_cv);
}

void
mod_release_mod(struct modctl *mp)
{
	if (moddebug & MODDEBUG_LOADMSG2)
		modprintf("Releasing %s\n", mp->mod_filename);
	mutex_enter(&mod_lock);
	mod_release(mp);
	mutex_exit(&mod_lock);
}

int
mod_remove_by_name(char *name)
{
	register struct modctl *mp;
	register int retval;

	mp = mod_hold_by_name(name);

	if (mp == NULL) {
		return (EINVAL);
	}
	if ((retval = moduninstall(mp)) == 0) {
		retval = mod_unload(mp);
	}
	mod_release_mod(mp);
	return (retval);
}

/*
 * Record that module "dep" is dependent on module "on_mod."
 */

static void
mod_make_dependent(struct modctl *dependent, struct modctl *on_mod)
{
	register struct modctl_list *dep, *req;

	ASSERT(dependent->mod_busy && on_mod->mod_busy);

	mutex_enter(&mod_lock);
	for (dep = on_mod->mod_dependents; dep; dep = dep->modl_next)
		if (dep->modl_modp == dependent)
			break;

	if (dep == NULL) { /* Not recorded */
		dep = (struct modctl_list *)
			kobj_zalloc(sizeof (*dep), KM_SLEEP);
		dep->modl_modp = dependent;
		dep->modl_next = on_mod->mod_dependents;
		on_mod->mod_dependents = dep;
	}

	for (req = dependent->mod_requisites; req; req = req->modl_next)
		if (req->modl_modp == on_mod)
			break;

	if (req == NULL) { /* Not recorded */
		req = (struct modctl_list *)
			kobj_zalloc(sizeof (*req), KM_SLEEP);
		req->modl_modp = on_mod;
		req->modl_next = dependent->mod_requisites;
		dependent->mod_requisites = req;
	}
	mutex_exit(&mod_lock);
}

/*
 * Process dependency of the module represented by "dep" on the
 * module named by "on."
 *
 * Called from kobj_do_dependents() to load a module "on" on which
 * "dep" depends.
 */

struct modctl *
mod_load_requisite(struct modctl *dep, char *on)
{
	register struct modctl *on_mod;
	int retval;

	if ((on_mod = mod_hold_loaded_mod(on, 1, &retval)) != NULL) {
		mod_make_dependent(dep, on_mod);
	} else if (moddebug & MODDEBUG_ERRMSG) {
		modprintf("error processing %s on which module %s depends\n",
			on, dep->mod_modname);
	}
	return (on_mod);
}

static int
mod_install_requisites(struct modctl *modp)
{
	register struct modctl_list *modl;
	register struct modctl *req;
	register int status = 0;

	ASSERT(modp->mod_busy);
	for (modl = modp->mod_requisites; modl; modl = modl->modl_next) {
		req = modl->modl_modp;
		mutex_enter(&mod_lock);

		while (mod_hold_by_modctl(req) == 1)
			continue;

		mutex_exit(&mod_lock);
		status = modinstall(req);
		mod_release_mod(req);
		if (status != 0)
			break;
	}
	return (status);
}

static int
mod_hold_dependents(struct modctl *modp, int not_modstat)
{
	register struct modctl_list *modl, *undo;
	register struct modctl *dep;
	register int stat = 1;
	register int mod_stub;

	ASSERT(modp->mod_busy);

	mutex_enter(&mod_lock);
	/*
	 * We *must* give up our hold on "modp" here in order to
	 * avoid deadlock.  We avoid deadlock by always holding a
	 * series of modules in the same order.  We have arbitrarily
	 * chosen that order to be dependent module before requisite
	 * module.
	 */
	modp->mod_busy = 0;
	mod_stub = modp->mod_stub;
	modp->mod_stub = 0;
	modp->mod_inprogress_thread = NULL;
	modl = modp->mod_dependents;
	while (modl && stat) {
		dep = modl->modl_modp;
		if (mod_circdep(dep))
			cmn_err(CE_PANIC, "Deadlock can occur!");

		while (mod_hold_by_modctl(dep) == 1)
			continue;

		if (not_modstat == 0)
			stat = !dep->mod_loaded;
		else
			stat = !dep->mod_installed;
		modl = modl->modl_next;
	}

	if (!stat) {
		for (undo = modp->mod_dependents;
		    undo != modl;
		    undo = undo->modl_next) {
			mod_release(undo->modl_modp);
		}
	}

	while (mod_hold_by_modctl(modp) == 1)
		continue;

	modp->mod_stub = mod_stub;
	mutex_exit(&mod_lock);
	return (stat);
}

static void
mod_release_dependents(struct modctl *modp)
{
	register struct modctl_list *modl;
	register struct modctl *dep;

	ASSERT(modp->mod_busy);
	mutex_enter(&mod_lock);

	for (modl = modp->mod_dependents; modl; modl = modl->modl_next) {
		dep = modl->modl_modp;
		mod_release(dep);
	}
	mutex_exit(&mod_lock);
}
