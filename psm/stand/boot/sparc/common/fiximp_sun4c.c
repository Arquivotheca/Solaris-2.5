/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 */

#ident	"@(#)fiximp_sun4c.c	1.16	94/11/21 SMI" /* From SunOS 4.1.1 */

#define	IOC
#include <sys/types.h>
#include <sys/machparam.h>
#include <sys/cpu.h>
#include <sys/mmu.h>
#include <sys/pte.h>
#include <sys/promif.h>
#include <sys/obpdefs.h>
#include <sys/idprom.h>
#include <sys/intreg.h>

#define	INTREG_NAME	"interrupt-enable"

/* see sun/io/le.c:leuninit() */
#define	LANCE_CSR0		0
#define	LANCE_STOP		0x0004		/* RW1 Stop */

extern caddr_t le_page;
extern caddr_t ie_page;

#define	XDELAY(x)  { int i, j; for (i = x; i >= 0; i--) j++; }

/*
 * The following variables are machine-dependent, and are used by boot
 * and kadb. They may also be used by other standalones. Modify with caution!
 * These default values are appropriate for the SS1. Other OBP-based
 * machines will use getprop() to modify the defaults if necessary.
 * A specific arch only needs to dork with these if its values are
 * DIFFERENT from these defaults.
 */
#define	NWINDOWS_DEFAULT	8
#define	NPMGRPS_DEFAULT		128
#define	VAC_DEFAULT		1
#define	VACSIZE_DEFAULT		0x10000
#define	VACLINESIZE_DEFAULT	16
#define	SEGMASK_DEFAULT		0x7f
#define	PAGESHIFT_DEFAULT	12
#define	PAGESIZE_DEFAULT	(1 << PAGESHIFT_DEFAULT)
#define	HOLE_START 		((caddr_t)((NPMGRPPERCTX/2) * PMGRPSIZE))
#define	HOLE_END		((caddr_t)((caddr_t)0 - HOLE_START))
#define	NPMGPERCTX_DEFAULT	4096

/*
 *  XXX: this is already declared in mmu.h
 */
int vac_nlines = 0;

short cputype;
int nwindows = NWINDOWS_DEFAULT;
u_int npmgrps = NPMGRPS_DEFAULT;
int vac = VAC_DEFAULT;
int vac_size = VACSIZE_DEFAULT;
int vac_linesize = VACLINESIZE_DEFAULT;
int mmu_3level = 0;		/* assume 2-level until proven otherwise */
u_int segmask = SEGMASK_DEFAULT;
int pagesize = PAGESIZE_DEFAULT;
caddr_t	hole_start = HOLE_START;
caddr_t	hole_end = HOLE_END;
u_int pageshift = PAGESHIFT_DEFAULT;
u_int npmgrpperctx = NPMGPERCTX_DEFAULT;

/*
 * XXX: This is a cheesy fix to a icache flush problem in krtld. It's
 * been put here because there is no generic sun4x file to put it in
 * (short of creating a new one for one variable). This should be
 * fixed in the platform-dependent boot project.
 */
u_int icache_flush = 0;

#ifdef DEBUG
static int debug = 1;
#else DEBUG
static int debug = 0;
#endif DEBUG

#define	dprintf		if (debug) printf

#define	ADDR_TO_FIX 0xfff00000

static void l14enable(void);

/* ARGSUSED */
void
fiximp_sun4c(short cputype)
{
	int freq;
	dnode_t	rootnode;
	caddr_t i, j;
	u_short pm;
	int len;

	l14enable();

	rootnode = prom_nextnode((dnode_t)0);
	/*
	 * Trapped in a world we never made! Determine the physical
	 * constants which govern this universe.
	 */
	len = prom_getproplen(rootnode, "mmu-npmg");
	if (len != (int)OBP_BADNODE && len != (int)OBP_NONODE)
		prom_getprop(rootnode, "mmu-npmg", (caddr_t)&npmgrps);

	/*
	 *  Hack Alert!  This is gross.  But you see, it's like this:
	 *  the PROM on all sun4c's has a bug in which it does NOT
	 *  ever map the ADDR_TO_FIX.  So the addr has a random
	 *  mapping decided by the last power cycle (read: "the
	 *  the forces of evil").  This code will make sure it
	 *  is something reasonable.
	 */
	if (prom_getversion() == 0)
		setsegmap(ADDR_TO_FIX, PMGRP_INVALID);

	/*
	 *  Hack Alert!  This, too, is gross.
	 *  We need to invalidate all the pages and all the pmegs in
	 *  the range 0xf8000000 - 0xf8400000.  Otherwise, the PROM
	 *  allocation routines will just blindly map right over
	 *  boot. The PROM was being Mr. Nice Guy and doing this
	 *  for us, but the new boot design does not need it.  Argh!
	 */
	if (prom_getversion() >= 0) {
		pm = 0x10;
		/* first, remap the segs to a safe addr */
		for (i = (caddr_t)0xf8000000;
		    i < (caddr_t)0xf8400000;
		    i += (u_int)PMGRPSIZE) {
			setsegmap(i, pm);
			/* now invalidate all the pages in the segment */
			for (j = i;
			    j < i + (u_int)PMGRPSIZE;
			    j += (u_int)PAGESIZE)
				setpgmap(j, 0x0);
			/* finally invalidate the segment */
			setsegmap(i, PMGRP_INVALID);
		}
	}

	len = prom_getproplen(rootnode, "vac-size");
	if (len != (int)OBP_BADNODE && len != (int)OBP_NONODE)
		prom_getprop(rootnode, "vac-size", (caddr_t)&vac_size);

	len = prom_getproplen(rootnode, "vac-linesize");
	if (len != (int)OBP_BADNODE && len != (int)OBP_NONODE)
		prom_getprop(rootnode, "vac-linesize", (caddr_t)&vac_linesize);

	vac = (vac_size) ? 1 : 0;
	vac_nlines = (vac_size && vac_linesize) ? vac_size/vac_linesize : 0;

}

void
v0_silence_nets()
{
	dnode_t	node;
	char 	buf[OBP_MAXPATHLEN];
	struct prom_reg *rs;
	ihandle_t	fd;
	dnode_t sp[OBP_STACKDEPTH];
	pstack_t *stk;
	struct lanceregs {
		u_short lance_csr;
		u_short lance_rap;
	} *lr;
	extern short cputype;
	u_long pte;
	u_long pte_orig;
	caddr_t ie_csr;
	u_long ie_pfnum;

	pte_orig = getpgmap(le_page);
	/*
	 *  Even tho the V0 prom only supports the onboard le for
	 *  booting, the user may have other (active) le's in the
	 *  system.  So we have to search the whole tree and stop
	 *  them all.
	 */
	node = (dnode_t)0;
	stk = prom_stack_init(sp, sizeof (sp));

	/* we need to go to each node of type OBP_NETWORK... */
	do {
		u_long le_pfnum;

		node = prom_nextnode(node);
		node = prom_findnode_bydevtype(node, OBP_NETWORK, stk);
		if (node == OBP_NONODE)
			break;
		bzero((caddr_t) buf, sizeof (buf));
		prom_getprop(node, OBP_NAME, buf);
		dprintf("netdev = '%s'\n", buf);

		/*
		 * Better make sure this is an 'le' before
		 * treating it like one ..
		 */
		if (prom_strcmp("le", buf) != 0)
			continue;

		prom_getprop(node, "reg", buf);
		dprintf("buf = %x\n", buf);

		rs = (struct prom_reg *)buf;

		le_pfnum = (rs->lo) >> PAGESHIFT;

		/* map in Lance chip */
		pte = (PG_V|PG_KW|PG_NC|PGT_OBIO|le_pfnum);
		setpgmap(le_page, pte);

		lr = (struct lanceregs *)le_page;

		/*
		 * Stop the chip.
		 */
		lr->lance_rap = LANCE_CSR0;
		lr->lance_csr = LANCE_STOP;

	} while (node != OBP_NONODE);

	/* restore mapping of le_page */
	setpgmap(le_page, pte_orig);

	prom_stack_fini(stk);
}

static void
l14enable()
{
	caddr_t intreg;
	dnode_t intreg_node;
	dnode_t sp[OBP_STACKDEPTH];
	pstack_t *stk;

	stk = prom_stack_init(sp, sizeof (sp));
	intreg_node = prom_findnode_byname(prom_nextnode(0), INTREG_NAME, stk);
	prom_stack_fini(stk);
	if (intreg_node == OBP_NONODE)
		prom_panic("no interrupt-enable node?");
	if (prom_getprop(intreg_node, "address",
	    (caddr_t)&intreg) != sizeof (caddr_t))
		prom_panic("no address property for intreg?");

	/* Do what we started out to do */
	*intreg |= IR_ENA_CLK14;
}
