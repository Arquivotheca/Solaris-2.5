/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#ident	"@(#)ppp_wrapper.c	1.14	94/08/30 SMI"

#include <sys/types.h>
#include <sys/syslog.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/ddi.h>
#ifndef _SunOS4
#include <sys/conf.h>
#include <sys/modctl.h>		/* Jupiter Only */
#else
#include <sys/param.h>
#include <sun/vddrv.h>
#include "4.1.h"
#endif

#ifndef ISERE_TREE
#define	SUNDDI 1
#endif

#ifndef BUILD_STATIC

/*
 * Globals for PPP loadable module wrapper
 */
extern int		ppp_initialise();
extern void		ppp_terminate();

extern int 		ppp_busy;
extern struct streamtab	ppp_info;
extern char 		ppp_module_name[];

#ifndef _SunOS4

/*
 * Loadable driver wrapper for the PPP module/SVR4
 */

#if !defined(SUNDDI)
extern		dseekneg_flag;
#endif


/*
 * Module linkage information for the kernel.
 */

static struct fmodsw mod_fsw = {
	"ppp",			/* f_name */
	&ppp_info,		/* f_str */

#if defined(SUNDDI)
	D_NEW | D_MP,		/* f_flag */
#else
	&dseekneg_flag

#endif  /* defined(SUNDDI) */

};

static struct modlstrmod modlstrmod = {
	&mod_strmodops,		/* strmod_modops */
	ppp_module_name,	/* strmod_linkinfo */
	&mod_fsw		/* strmod_fmodsw */
};

static struct modlinkage modlinkage = {
	MODREV_1,		/* ml_rev, has to be MODREV_1 */
	&modlstrmod,		/* ml_linkage, NULL-terminated list of */
	NULL			/*  linkage structures			*/
};



int
_init(void)
{
	register int	rc;

	if ((rc = ppp_initialise()))
		return (rc);

	rc = mod_install(&modlinkage);
	if (rc != 0) {
		ppp_terminate();
	}
	return (rc);
}

int
_fini(void)
{
	int	rc;

	if (ppp_busy)
		return (EBUSY);

	rc = mod_remove(&modlinkage);
	if (rc != 0)
		return (rc);
	ppp_terminate();
	return (0);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}


#else /* 4.1.x */


/*
 * Loadable driver wrapper for the PPP module/4.1.x
 */

/*
 * Module linkage information for the kernel.
 */

static struct vdlstr {
	int	Str_magic;
	char 	*Str_name;
} vd = { VD_TYPELESS, ppp_module_name };


extern int 		fmodcnt;
extern struct fmodsw	fmodsw[];

static int
/*ARGSUSED*/
unload(vdp)
struct vddrv		*vdp;
{
	register int	i, s;

	if (ppp_busy)
		return (EBUSY);

	ppp_terminate();

	s = splstr();	/* just in case */

	for (i = 0; i < fmodcnt; i++)
		if (strcmp(fmodsw[i].f_name,
				ppp_info.st_rdinit->qi_minfo->mi_idname) != 0) {
			*fmodsw[i].f_name = '\0';
			fmodsw[i].f_str = (struct streamtab *) NULL;
			break;
		}

	(void) splx(s);
	if (i == fmodcnt) {
		log(LOG_DEBUG, "failed to unload STREAMS PPP module\n");
		return (ENXIO);
	}

	return (0);
}

/*ARGSUSED*/
xxxinit(function_code, vdp, vdi, vds)
u_int		function_code;
struct vddrv	*vdp;
addr_t		vdi;
struct vdstat	*vds;
{
	register 		i;
	register struct fmodsw	*fp;

	switch (function_code) {

	case VDLOAD:

		vdp->vdd_vdtab = (struct vdlinkage *)&vd;

		fp = (struct fmodsw *) NULL;

		for (i = 0; i < fmodcnt; i++) {

			if (fmodsw[i].f_str == NULL)
				fp = &fmodsw[i];

			else if (strcmp(fmodsw[i].f_name,
				ppp_info.st_rdinit->qi_minfo->mi_idname) != 0) {
				log(LOG_DEBUG, "ppp module already loaded\n");
				return (EEXIST);
			}
		}

		if (fp) {
			(void) strcpy(fp->f_name,
				ppp_info.st_rdinit->qi_minfo->mi_idname);

			fp->f_str = &ppp_info;

/*
 * perform module initialisation
 */
			if (ppp_initialise()) {
				log(LOG_DEBUG, "PPP failed initialisation\n");
				*fp->f_name = '\0';
				fp->f_str = (struct streamtab *) NULL;
				return (ENOMEM);
			}

			return (0);
		}
		log(LOG_DEBUG, "failed to load STREAMS ppp module\n");
		return (ENOSPC);

	case VDUNLOAD:
		return (unload(vdp));

	case VDSTAT:
		return (0);

	default:
		return (EIO);
	}
}

#endif /* _SunOS4 */
#endif /* BUILD_STATIC */
