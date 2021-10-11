/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_CONF_H
#define	_SYS_CONF_H

#pragma ident	"@(#)conf.h	1.36	94/05/10 SMI"	/* SVr4.0 11.21	*/

#include <sys/t_lock.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _KERNEL

/*
 * XXX  Given that drivers need to include this file,
 *	<sys/systm.h> probably shouldn't be here, as
 *	it legitamizes (aka provides prototypes for)
 *	all sorts of functions that aren't in the DKI/SunDDI
 */
#include <sys/systm.h>
#include <sys/devops.h>

extern struct dev_ops **devopsp;

#define	STREAMSTAB(maj)	devopsp[(maj)]->devo_cb_ops->cb_str

#endif	/* _KERNEL */

#if defined(_KERNEL) && defined(__STDC__)

#include <sys/types.h>
#include <sys/buf.h>
#include <sys/cred.h>
#include <sys/uio.h>
#include <sys/poll.h>
#include <vm/as.h>

extern int devi_identify(dev_info_t *devi);
extern int devi_probe(dev_info_t *devi);
extern int devi_attach(dev_info_t *devi, ddi_attach_cmd_t cmd);
extern int devi_detach(dev_info_t *devi, ddi_detach_cmd_t cmd);
extern int devi_reset(dev_info_t *devi, ddi_reset_cmd_t cmd);

extern int dev_open(dev_t *devp, int flag, int type, cred_t *credp);
extern int dev_close(dev_t dev, int flag, int type, cred_t *credp);

extern dev_info_t *dev_get_dev_info(dev_t dev, int otyp);
extern int dev_to_instance(dev_t dev);

extern int bdev_strategy(struct buf *bp);
extern int bdev_print(dev_t dev, caddr_t str);
extern int bdev_dump(dev_t dev, caddr_t addr, daddr_t blkno, int blkcnt);
extern int bdev_size(dev_t dev);

extern int cdev_read(dev_t dev, struct uio *uiop, cred_t *credp);
extern int cdev_write(dev_t dev, struct uio *uiop, cred_t *credp);
extern int cdev_size(dev_t dev);
extern int cdev_ioctl(dev_t dev,
    int cmd, int arg, int mode, cred_t *credp, int *rvalp);
extern int cdev_devmap(dev_t dev, dev_info_t *dip, ddi_devmap_data_t *dvdp,
    ddi_devmap_cmd_t cmd, off_t offset, unsigned int len, unsigned int prot,
    cred_t *credp);
extern int cdev_mmap(int (*mapfunc)(dev_t, off_t, int),
    dev_t dev, off_t off, int prot);
extern int cdev_segmap(dev_t dev, off_t off, struct as *as, caddr_t *addrp,
    off_t len, u_int prot, u_int maxprot, u_int flags, cred_t *credp);
extern int cdev_poll(dev_t dev,
    short events, int anyyet, short *reventsp, struct pollhead **pollhdrp);
extern int cdev_prop_op(dev_t dev, dev_info_t *dip, ddi_prop_op_t prop_op,
    int mod_flags, char *name, caddr_t valuep, int *lengthp);

#endif /* _KERNEL && __STDC__ */


/*
 * Device flags.
 *
 * Bit 0 to bit 15 are reserved for kernel.
 * Bit 16 to bit 31 are reserved for different machines.
 */
#define	D_NEW		0x00	/* new-style driver */
#define	D_OLD		0x01	/* old-style driver */
#define	D_TAPE		0x08	/* Magtape device (no bdwrite when cooked) */

/*
 * Added for pre-4.0 drivers backward compatibility.
 */
#define	D_NOBRKUP	0x10	/* No breakup needed for new drivers */

/*
 * Added for MT-safe drivers (in DDI portion of flags).
 *
 * D_MP (D_MTSAFE) and unsafe (D_MP not specified) applies to all drivers
 * as well as STREAMS modules:
 *	Unsafe	- executes with the "unsafe_driver" mutex held.
 *	D_MP - multithreaded driver.
 *
 * The remainder of the flags apply only to STREAMS modules and drivers.
 *
 * If a STREAMS driver or module is not unsafe then it can optionally select
 * inner and outer perimeters. The four mutually exclusive options that
 * define the presence and scope of the inner perimeter are:
 *	D_MTPERMOD - per module single threaded.
 *	D_MTQPAIR - per queue-pair single threaded.
 *	D_MTPERQ - per queue instance single threaded.
 *	(none of the above) - no inner perimeter restricting concurrency
 *
 * The presence	of the outer perimeter is declared with:
 *	D_MTOUTPERIM - a per-module outer perimeter. Can be combined with
 *		D_MTPERQ, D_MTQPAIR, and D_MP.
 *
 * The concurrency when entering the different STREAMS entry points can be
 * modified with:
 *	D_MTPUTSHARED - modifier for D_MTPERQ, D_MTQPAIR, and D_MTPERMOD
 *		specifying that the put procedures should not be
 *		single-threaded at the inner perimeter.
 *	D_MTOCEXCL - modifier for D_MTOUTPERIM specifying that the open and
 *		close procedures should be single-threaded at the outer
 *		perimeter.
 */
#define	D_MTOCEXCL	0x0800	/* modify: open/close are exclusive at outer */
#define	D_MTPUTSHARED	0x1000	/* modify: put procedures are hot */
#define	D_MTPERQ	0x2000	/* per queue instance single-threaded */
#define	D_MTQPAIR	0x4000	/* per queue-pair instance single-threaded */
#define	D_MTPERMOD	0x6000	/* per module single-threaded */
#define	D_MTOUTPERIM	0x8000	/* r/w outer perimeter around whole modules */
#define	D_MTSAFE	0x20	/* multi-threaded module or driver */

/* The inner perimeter scope bits */
#define	D_MTINNER_MASK	(D_MP|D_MTPERQ|D_MTQPAIR|D_MTPERMOD)

/* Inner perimeter modification bits */
#define	D_MTINNER_MOD	(D_MTPUTSHARED)

/* Outer perimeter modification bits */
#define	D_MTOUTER_MOD	(D_MTOCEXCL)

/* All the MT flags */
#define	D_MTSAFETY_MASK (D_MTINNER_MASK|D_MTOUTPERIM|D_MTPUTSHARED|\
			D_MTINNER_MOD|D_MTOUTER_MOD)

#define	D_MP		D_MTSAFE /* ddi/dki approved flag */

#define	D_64BIT		0x200	/* Driver supports 64-bit offsets, blk nos. */

#define	D_SYNCSTR	0x400	/* Module or driver has Synchronous STREAMS */
				/* extended qinit structure */

#define	FMNAMESZ	8

struct fmodsw {
	char		f_name[FMNAMESZ+1];
	struct  streamtab *f_str;
	int		f_flag;
	krwlock_t	*f_lock;
};

#ifdef _KERNEL

extern int allocate_fmodsw(char *);
extern void free_fmodsw(struct fmodsw *);
extern int findmod(char *);
extern int findfmodbyindex(char *);
extern int fmod_lock(int, int);
extern int fmod_unlock(int);
#define	STATIC_STREAM		(krwlock_t *) 0xffffffff
#define	LOADABLE_STREAM(s)	((s)->f_lock != STATIC_SCHED)
#define	ALLOCATED_STREAM(s)	((s)->f_lock != NULL)
#define	STREAM_INSTALLED(s)	((s)->f_str != NULL)

extern kmutex_t  fmodsw_lock;

extern struct fmodsw fmodsw[];

extern int	devcnt;
extern int	fmodcnt;
#endif /* _KERNEL */

/*
 * Line discipline switch.
 */
struct linesw {
	int	(*l_open)();
	int	(*l_close)();
	int	(*l_read)();
	int	(*l_write)();
	int	(*l_ioctl)();
	int	(*l_input)();
	int	(*l_output)();
	int	(*l_mdmint)();
};
#ifdef _KERNEL
extern struct linesw linesw[];

extern int	linecnt;
#endif /* _KERNEL */
/*
 * Terminal switch
 */
struct termsw {
	int	(*t_input)();
	int	(*t_output)();
	int	(*t_ioctl)();
};
#ifdef _KERNEL
extern struct termsw termsw[];

extern int	termcnt;
#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CONF_H */
