/*
 * Copyright (C) 1991, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)strplumb.c	1.19	94/12/20 SMI"

#include	<sys/param.h>
#include	<sys/types.h>
#include	<sys/user.h>
#include	<sys/vfs.h>
#include	<sys/vnode.h>
#include	<sys/file.h>
#include	<sys/stream.h>
#include	<sys/stropts.h>
#include	<sys/strsubr.h>
#include	<sys/dlpi.h>
#include	<sys/vnode.h>
#include	<sys/socket.h>
#include	<sys/sockio.h>
#include	<net/if.h>

#include	<sys/cred.h>
#include	<sys/sysmacros.h>

#include	<sys/sad.h>
#include	<sys/kstr.h>
#include	<sys/bootconf.h>

#include	<sys/errno.h>
#include	<sys/modctl.h>
#include	<sys/sunddi.h>

#include	<inet/tcp.h>

/*
 * Module linkage information for the kernel.
 */
extern struct mod_ops mod_miscops;

/*
 * External function declarations.
 */
extern int	prom_devname_from_pathname(char *path, char *buf);

static struct modlmisc modlmisc = {
	&mod_miscops, "Configure STREAMS Plumbing."
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlmisc, NULL
};

int
_init()
{
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

int	strplumbdebug = 0;

int		ARP_MAJ;
int		TCP_MAJ;
int		UDP_MAJ;
int		ICMP_MAJ;
int		IP_MAJ;
int		RAWIP_MAJ;
int		CLONE_MAJ;
int		LOOP_MAJ;
int		APP_MAJ;

#define		ARP		"arp"
#define		TCP		"tcp"
#define		UDP		"udp"
#define		ICMP		"icmp"
#define		IP		"ip"
#define		CLONE		"clone"
#define		TIMOD		"timod"

#define		DRIVER		"drv"
#define		STRMOD		"strmod"

/*
 * There must be at least one NULL entry in this table...
 */
static struct strp_list {
	char *type;
	char *name;
} strp_list[] = {
	{DRIVER, CLONE},
	{DRIVER, IP},
	{DRIVER, TCP},
	{DRIVER, UDP},
	{DRIVER, ICMP},
	{DRIVER, ARP},
	{STRMOD, TIMOD},
	{NULL, NULL}
};

/*
 * Called from swapgeneric.c:loadrootmodules() in the network boot case to
 * get all the plumbing drivers and their .conf files properly loaded
 * and added to the defer list, so they are here when asked for later.
 */

int
strplumb_get_driver_list(register int flag, char **type, char **name)
{
	static struct strp_list *p;
	if (flag)
		p = strp_list;
	else if (p->name)
		++p;

	if (p->name == NULL)
		return (0);

	*type = p->type;
	*name = p->name;
	return (1);
}

/*
 * Do streams plumbing for internet protocols.
 */
int
strplumb()
{
	vnode_t		*vp;
	vnode_t		*nvp;
	int		fd, more;
	int		muxid;
	int		error;
	dev_t		maj;
	dev_t		min;
	dev_t		lastmin;
	char		*mods[5];
	char		ifname[32];
	dev_t		ndev;
	char		*name, *type;
	int		unit;

	if (strplumbdebug)
		printf("Entered strplumb\n");

	more = strplumb_get_driver_list(1, &type, &name);
	while (more)  {
		register int err;

		if (strcmp(type, DRIVER) == 0)
			err = ddi_install_driver(name);
		else
			err = modload(type, name);
		if (err < 0)  {
			printf("strplumb: can't install module %s/%s\n",
			    type, name);
			return (-1);
		}
		more = strplumb_get_driver_list(0, &type, &name);
	}

	IP_MAJ = ddi_name_to_major(IP);
	TCP_MAJ = ddi_name_to_major(TCP);
	UDP_MAJ = ddi_name_to_major(UDP);
	ICMP_MAJ = ddi_name_to_major(ICMP);
	ARP_MAJ = ddi_name_to_major(ARP);
	CLONE_MAJ = ddi_name_to_major(CLONE);

	/* First set up the autopushes */

	if (strplumbdebug)
		printf(
	"strplumb: Autopush `module' TCP when `device' TCP is opened\n");
	maj = TCP_MAJ;
	min = -1;
	mods[0] = TCP;
	mods[1] = (char *)NULL;
	if ((error = kstr_autopush(SET_AUTOPUSH, &maj, &min, NULL, mods))) {
		printf("kstr_autopush(SET/TCP) failed: %d\n", error);
		return (0);
	}

	if (strplumbdebug)
		printf(
	"strplumb: Autopush `module' UDP when `device' UDP is opened\n");
	maj = UDP_MAJ;
	min = -1;
	mods[0] = UDP;
	mods[1] = (char *)NULL;
	if ((error = kstr_autopush(SET_AUTOPUSH, &maj, &min, NULL, mods))) {
		printf("kstr_autopush(SET/UDP) failed: %d\n", error);
		return (0);
	}

	if (strplumbdebug)
		printf(
	"strplumb: Autopush `module' ICMP when `device' ICMP is opened\n");
	maj = ICMP_MAJ;
	min = -1;
	mods[0] = ICMP;
	mods[1] = (char *)NULL;
	if ((error = kstr_autopush(SET_AUTOPUSH, &maj, &min, NULL, mods))) {
		printf("kstr_autopush(SET/ICMP) failed: %d\n", error);
		return (0);
	}

	if (strplumbdebug)
		printf(
	"strplumb: Autopush `module' ARP when `device' ARP is opened\n");
	maj = ARP_MAJ;
	min = -1;
	mods[0] = ARP;
	mods[1] = (char *)NULL;
	if ((error = kstr_autopush(SET_AUTOPUSH, &maj, &min, NULL, mods))) {
		printf("kstr_autopush(SET/ARP) failed: %d\n", error);
		return (0);
	}

	if (strplumbdebug)
		printf("strplumb: configure default tcp stream\n");
	if (error = kstr_open(CLONE_MAJ, IP_MAJ, &nvp, (int *)NULL)) {
		printf("strplumb: kstr_open CLONE_MAJ/IP_MAJ failed error %d\n",
		    error);
		goto bad;
	}
	if (error = kstr_open(CLONE_MAJ, TCP_MAJ, &vp, &fd)) {
		printf(
		    "strplumb: kstr_open CLONE_MAJ/TCP_MAJ failed error %d\n",
		    error);
		goto bad;
	}
	if (error = kstr_ioctl(vp, TCP_IOC_DEFAULT_Q, 0)) {
		printf(
		    "strplumb: kstr_ioctl TCP_IOC_DEFAULT_Q failed error %d\n",
		    error);
		goto bad;
	}
	if (error = kstr_plink(nvp, fd, &muxid)) {
		printf("strplumb: kstr_plink failed, error %d\n", error);
		goto bad;
	}
	(void) kstr_close(NULL, fd);

	(void) bzero(ifname, sizeof (ifname));
	if (error = resolve_netdrv(ifname, &unit, &ndev)) {
		printf("strplumb: resolve_netdrv: %d\n", error);
		return (error);
	}
	if (strlen(ifname) == 0) {
		/* No interface to plumb */
		if (strplumbdebug)
			printf("strplumb: no interface to plumb\n");
		(void) kstr_close(nvp, -1);
		return (0);
	}

	if (strplumbdebug)
		printf("strplumb: ifname %s, ndev %d, unit %d\n",
			ifname, ndev, unit);

	/*
	 * Now set up the links. Ultimately, we should have two
	 * streams permanently linked underneath IP. One
	 * stream consists of the ARP-[ifname] combination, while the
	 * other consists of  ARP-IP-[ifname]. The second combination
	 * seems a little weird, but is linked underneath IP
	 * just to keep it around.
	 */
	if (strplumbdebug)
		printf("strplumb: Push ARP onto %s and pin underneath IP\n",
		    ifname);
	if (error = kstr_open(CLONE_MAJ, ndev, &vp, &fd)) {
		printf("strplumb: kstr_open CLONE/<net> failed error %d\n",
		    error);
		goto bad;
	}
	if (error = kstr_push(vp, ARP)) {
		printf("strplumb: kstr_push ARP failed, error %d\n", error);
		goto bad;
	}
	if (error = setifunit(vp, unit)) {
		printf("strplumb: setifunit ARP failed, error %d\n", error);
		goto bad;
	}
	if (error = kstr_plink(nvp, fd, &muxid)) {
		printf("strplumb: kstr_plink IP/<net> failed, error %d\n",
		    error);
		goto bad;
	}
	(void) kstr_close(NULL, fd);

	if (strplumbdebug)
		printf(
		    "strplumb: Push IP and ARP onto %s and pin underneath IP\n",
		    ifname);
	if (error = kstr_open(CLONE_MAJ, ndev, &vp, &fd)) {
		printf("strplumb: kstr_open CLONE/<net> failed error %d\n",
		    error);
		goto bad;
	}
	if (error = kstr_push(vp, IP)) {
		printf("strplumb: kstr_push IP failed, error %d\n", error);
		goto bad;
	}
	if (error = setifunit(vp, unit)) {
		printf("strplumb: setifunit IP failed, error %d\n", error);
		goto bad;
	}
	if (error = kstr_push(vp, ARP)) {
		printf("strplumb: kstr_push ARP failed, error %d\n", error);
		goto bad;
	}
	if (error = kstr_plink(nvp, fd, &muxid)) {
		printf("strplumb: kstr_plink IP-ARP/<net> failed, error %d\n",
		    error);
		goto bad;
	}
	(void) kstr_close(NULL, fd);
	(void) kstr_close(nvp, -1);

bad:
	if (strplumbdebug)
		printf("Leaving strplumb()\n");
	return (0);

}

/*
 * Can be set thru /etc/system file in the
 * case of local booting. In the case of
 * diskless booting it is too late by the
 * time this function is called to get the
 * specified driver loaded in.
 */
char	*ndev_name = 0;
int	ndev_unit = 0;

static int
resolve_netdrv(ifname, unitp, ndev)
	char	*ifname;
	int	*unitp;
	dev_t	*ndev;
{
	minor_t			unit;
	extern struct modctl	*mod_find_by_filename();
	struct modctl		*modp;
	struct modldrv		**modldrvpp;
	char			fname[25];
	dev_t			dev;
	extern struct bootops	*bootops;
	char			*devname;

	/*
	 * If we booted diskless then strplumb() will
	 * have been called from rootconf(). All we
	 * can do in that case is plumb the network
	 * device that we booted from.
	 *
	 * If we booted from a local disk, we will have
	 * been called from main(), and normally we defer the
	 * plumbing of interfaces until /sbin/bcheckrc.
	 * This can be overridden by
	 * setting "ndev_name" in /etc/system.
	 */
	if ((strncmp(backfs.bo_fstype, "nfs", 3) == 0) ||
	    (strncmp(rootfs.bo_fstype, "nfs", 3) == 0)) {
		if (strncmp(rootfs.bo_fstype, "nfs", 3) == 0)
			devname = rootfs.bo_name;
		else
			devname = backfs.bo_name;
		(void) prom_devname_from_pathname(devname, ifname);
		dev = ddi_pathname_to_dev_t(devname);
		if (dev == (dev_t)-1) {
			cmn_err(CE_CONT, "Cannot assemble drivers for %s\n",
				devname);
			return (ENXIO);
		}
#ifdef i386
		unit = 0;
#else
		unit = getminor(dev);
#endif
		*ndev = (dev_t)getmajor(dev);

		if (strplumbdebug)
			printf("strplumb: network device maj %d, unit %d\n",
			    (int)*ndev, (int)unit);

		sprintf(ifname + strlen(ifname), "%d", unit);
		*unitp = unit;
	} else {
		if (strplumbdebug) {
			if (ndev_name != (char *)NULL)
				printf("strplumb: ndev_name %s\n", ndev_name);
			printf("strplumb: ndev_unit %d\n", ndev_unit);
		}

		if (ndev_name != (char *)NULL) {
			if (ddi_install_driver(ndev_name) != DDI_SUCCESS) {
				printf("strplumb: Can't install drv/%s\n",
				    ndev_name);
				return (0);
			}
		}

		if (ndev_name == (char *)NULL) {
			ifname[0] = 0;
			return (0);	/* May be acceptable */
		}

		*ndev = (dev_t)ddi_name_to_major(ndev_name);
		if (strplumbdebug)
			printf("strplumb: non-nfs: maj %d, unit %d\n",
			    (int)*ndev, ndev_unit);

		sprintf(ifname, "%s%d", ndev_name, ndev_unit);
		*unitp = ndev_unit;
	}
	return (0);
}

static int
setifunit(vp, unit)
	vnode_t		*vp;
	int		unit;

{
	struct strioctl	iocb;

	iocb.ic_cmd = IF_UNITSEL;
	iocb.ic_timout = 15;
	iocb.ic_len = sizeof (unit);
	iocb.ic_dp = (char *)&unit;

	return (kstr_ioctl(vp, I_STR, (int)&iocb));
}
