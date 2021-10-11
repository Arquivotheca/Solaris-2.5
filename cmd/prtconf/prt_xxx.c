/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#ident	"@(#)prt_xxx.c	1.5	92/08/06 SMI"	/* SVr4 */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <kvm.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/ddipropdefs.h>
#include <sys/avintr.h>
/* #include <sys/scsi/scsi.h> */
#include "prt_xxx.h"

extern char *malloc(), *mfail;
extern int _error();
extern void indent_to_level();
extern kvm_t *kd;

/* #define DPRINT */
#ifdef	DPRINT
static char pdebug_flag = 1;
#define dprintf if (pdebug_flag) printf
#endif DPRINT

#define DEVI_PDP(d) DEVI(d)->devi_parent_data
#define DEVI_CDP(d) DEVI(d)->devi_driver_data

/*
 * Since scsi.h is unincludable at this point, this is a huge hack...
 * The scsi device parent private data for SCSA is the scsi_device
 * structure which includes the target and lun info...
 */

#define _SYS_SCSI_SCSI_TYPES_H
#include <sys/scsi/scsi_address.h>

/*
 * All we really want out of this is the sd_address structure,
 * so, since it's the first field, that's all we get.
 */
struct scsi_device  {
	struct scsi_address sd_address;
};

/*
 * Default case at end of list...
 * Format is parent name, size of data, function to print parent private info.
 * To ignore parent data, set pp_size field to 0.
 * Use pp_getdata function, if there is more data than just than parent/priv.
 * data (i.e. parent_private points to additional data.
 */

void obio_print(), obio_fetch();
void scsi_print();

struct pp_data prt_ppdata[] = {
{ "scsibus",	sizeof (struct scsi_device),		 0,	scsi_print},
{ "",		sizeof (struct ddi_parent_private_data), obio_fetch,obio_print},
};

#define NPRT_PPDATA	((sizeof (prt_ppdata))/(sizeof (struct pp_data)))

int nprt_ppdata = NPRT_PPDATA;

static struct pp_data *
matchppdata(dev_info_t *dp)
{
	int i;
	dev_info_t *pdev;
	char *parent = "";
	struct pp_data *ppp;

	if ((pdev = (dev_info_t *)(DEVI(dp)->devi_parent)) != 0)
		parent = DEVI(pdev)->devi_name;

#ifdef	DPRINT
	dprintf("matchppdata: Node <%s> parent <%s>, ",
		DEVI(dp)->devi_name, parent);
#endif	DPRINT

	ppp = prt_ppdata;
	for (i = 0; i < nprt_ppdata; ++i, ++ppp)
		if (strcmp(ppp->pp_parent, parent) == 0)
			break;

	if (i >= nprt_ppdata)
		ppp = &prt_ppdata[NPRT_PPDATA - 1];

	return (ppp);
}

void
getppdata(dev_info_t *dp)
{
	struct pp_data *ppp;
	char *p;

	if ((ppp = matchppdata(dp)) == (struct pp_data *)0)
		return;

	if (DEVI_PDP(dp) == (caddr_t)0)  {
#ifdef	DPRINT
		dprintf("getppdata: No data for node <%s>.\n",
		    DEVI(dp)->devi_name);
#endif	DPRINT
		return;
	}

	if (ppp->pp_size == 0)  {
#ifdef	DPRINT
		dprintf("No data to be fetched.\n");
#endif	DPRINT
		DEVI_PDP(dp) = (caddr_t)0;
		return;
	}

	/*
	 * Allocate space for and read in parent private data...
	 */

	if ((p = malloc(ppp->pp_size)) == 0)
		exit(_error(mfail));

	if (kvm_read(kd, (u_long)(DEVI_PDP(dp)), p, ppp->pp_size) !=
	    ppp->pp_size)
		exit(_error("kvm_read of parent private data fails"));

	DEVI_PDP(dp) = (caddr_t)p;

#ifdef	DPRINT
	dprintf("read <%d> bytes.\n", ppp->pp_size);
#endif	DPRINT

	/*
 	 * Is there any extra data to be fetched by driver?
	 */

	if (ppp->pp_getdata != 0)
		(*ppp->pp_getdata)(dp);

	return;
}

void
printppdata(dev_info_t *dp, int ilev)
{
	struct pp_data *ppp;

	if ((ppp = matchppdata(dp)) == (struct pp_data *)0)
		return;

	if ((ppp->pp_size == 0) || (ppp->pp_print == 0))
		return;

	(*ppp->pp_print)(dp, ilev);
	return;
}

void
obio_fetch(dev_info_t *dp)
{
	struct regspec *rp;
	struct rangespec *rnp;
	struct intrspec *ip;
	struct ddi_parent_private_data *pdp = DEVI_PD(dp);
	unsigned n;

	if (DEVI_PDP(dp) == (caddr_t)0)  {
#ifdef	DPRINT
		dprintf("obio_fetch: No data for node <%s>.\n",
		    DEVI(dp)->devi_name);
#endif	DPRINT
		return;
	}

	if (sparc_pd_getnreg(dp) != 0)  {
		n = (unsigned)(sparc_pd_getnreg(dp) * sizeof (struct regspec));
		if ((rp = (struct regspec *)malloc(n)) == 0)
			exit(_error(mfail));

		if (kvm_read(kd, (u_long)(pdp->par_reg), (char *)rp, n) != n)
			exit(_error(
			    "kvm_read of parent private regsiter data fails"));

		pdp->par_reg = rp;
	}

	if (sparc_pd_getnintr(dp) != 0)  {
		n = (unsigned)(sparc_pd_getnintr(dp) * 
		    sizeof (struct intrspec));
		if ((ip = (struct intrspec *)malloc(n)) == 0)
			exit(_error(mfail));

		if (kvm_read(kd, (u_long)(pdp->par_intr), (char *)ip, n) != n)
			exit(_error(
			    "kvm_read of parent private interrupt data fails"));

		pdp->par_intr = ip;
	}

	if (sparc_pd_getnrng(dp) != 0)  {
		n = (unsigned)(sparc_pd_getnrng(dp) *
		    sizeof (struct rangespec));
		if ((rnp = (struct rangespec *)malloc(n)) == 0)
			exit(_error(mfail));

		if (kvm_read(kd, (u_long)(pdp->par_rng), (char *)rnp, n) != n)
			exit(_error(
			    "kvm_read of parent private range data fails"));

		pdp->par_rng = rnp;
	}

}

void
obio_printregs(struct regspec *rp, int ilev)
{
	indent_to_level(ilev);
	(void)printf("    Bus Type=0x%x, Address=0x%x, Size=%x\n",
	    rp->regspec_bustype, rp->regspec_addr, rp->regspec_size);
}

void
obio_printranges(struct rangespec *rp, int ilev)
{
	indent_to_level(ilev);
	(void)printf("    Ch: %.2x,%.8x Pa: %.2x,%.8x, Sz: %x\n",
	    rp->rng_cbustype, rp->rng_coffset,
	    rp->rng_bustype, rp->rng_offset,
	    rp->rng_size);
}

void
obio_printintr(struct intrspec *ip, int ilev)
{
	indent_to_level(ilev);
	(void)printf("    Interrupt Priority=0x%x (ipl %d)",
	    ip->intrspec_pri, INT_IPL(ip->intrspec_pri));
	if (ip->intrspec_vec)
		(void)printf(", vector=0x%x (%d)",
		    ip->intrspec_vec, ip->intrspec_vec);
	printf("\n");
}

void
obio_print(dev_info_t *dp, int ilev)
{
	int i;
	struct regspec *rp;

	if (DEVI_PDP(dp) == (caddr_t)0)  {
#ifdef	DPRINT
		dprintf("obio_print: No data for node <%s>.\n",
		    DEVI(dp)->devi_name);
#endif	DPRINT
		return;
	}

#ifdef	DPRINT
	dprintf("obio_print: node <%s>\n", DEVI(dp)->devi_name);
#endif	DPRINT

	if (sparc_pd_getnreg(dp) != 0)  {
		indent_to_level(ilev);
		printf("Register Specifications:\n");
	}

	for (i = 0; i < sparc_pd_getnreg(dp); ++i)
		obio_printregs(sparc_pd_getreg(dp, i), ilev);

	if (sparc_pd_getnrng(dp) != 0)  {
		indent_to_level(ilev);
		printf("Range Specifications:\n");
	}

	for (i = 0; i < sparc_pd_getnrng(dp); ++i)
		obio_printranges(sparc_pd_getrng(dp, i), ilev);

	if (sparc_pd_getnintr(dp) != 0)  {
		indent_to_level(ilev);
		printf("Interrupt Specifications:\n");
	}

	for (i = 0; i < sparc_pd_getnintr(dp); ++i)
		obio_printintr(sparc_pd_getintr(dp, i), ilev);

	return;
}

void
scsi_print(dev_info_t *dp, int ilev)
{
	struct scsi_device sd;
	unsigned n = sizeof (struct scsi_device);

#ifdef	DPRINT
	dprintf("scsi_print: node <%s>\n", DEVI(dp)->devi_name);
#endif	DPRINT

	if (DEVI_CDP(dp) == 0)
		return;

	if (kvm_read(kd, (u_long)(DEVI_CDP(dp)), (char *)(&sd), n) != n)
		exit(_error("kvm_read of driver private scsi data fails"));

	indent_to_level(ilev);
	printf("Target <%d>, Lun <%d>\n",
	    sd.sd_address.a_target, sd.sd_address.a_lun);

	return;
}
