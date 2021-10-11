/*
 * Copyright (c) 1989, 1990 by Sun Microsystems, Inc.
 */

#ident	"@(#)memerr.c	1.13	94/03/31 SMI"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/user.h>
#include <sys/vnode.h>

#include <sys/buserr.h>
#include <sys/intreg.h>
#include <sys/psw.h>
#include <sys/cpu.h>
#include <sys/pte.h>
#include <sys/mmu.h>
#include <sys/memerr.h>
#include <sys/kmem.h>
#include <sys/eccreg.h>
#include <sys/enable.h>
#include <sys/trap.h>
#include <sys/obpdefs.h>
#include <sys/promif.h>
#include <sys/machsystm.h>

#include <vm/hat.h>
#include <vm/page.h>
#include <vm/seg.h>
#include <vm/seg_kmem.h>

#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

int parity_hard_error = 0;

extern void page_giveup(caddr_t, struct pte *, struct page *);

static void ecc_error(u_int);
static int atoi(char *);

static void memerr_init_parity(void);
static void parity_error(u_int);
static void pr_u_num110(caddr_t, u_int);
static int bank_num(caddr_t);
static int parerr_retry(caddr_t);
static int check_ldd(u_int, caddr_t);
static int parerr_recover(caddr_t, u_int, struct pte *, unsigned,
		struct regs *);

static void memerr_init260(void);
static void softecc_260(void);
static void memlog_260(union eccreg *);

static void memerr_init330(void);
static void memlog_330(int, unsigned);

static void memerr_init470(void);
static void softecc_470(void);
static void memlog_470(union eccreg *);

/* round address to VAC linesize */
#define	ROUND_ADDR(a, size)	((caddr_t) ((int) (a) & -(size)))

/*
 * Memory error handling for various sun4s
 */
void
memerr_init(void)
{
	switch (cputype) {
	case CPU_SUN4_110:
		memerr_init_parity();
		break;
	case CPU_SUN4_330:
		memerr_init330();
		break;
	case CPU_SUN4_260:
		memerr_init260();
		break;
	case CPU_SUN4_470:
		memerr_init470();
		break;
	}
}

/*
 * Called on unrecoverable memory errors at user level.
 * Send SIGBUS to the current LWP.
 */
void
memerr_signal(addr)
	caddr_t	addr;
{
	k_siginfo_t siginfo;
	register proc_t *p = ttoproc(curthread);

	bzero((caddr_t)&siginfo, sizeof (siginfo));
	siginfo.si_signo = SIGBUS;
	siginfo.si_code = FC_HWERR;
	siginfo.si_addr = addr;
	mutex_enter(&p->p_lock);
	sigaddq(p, curthread, &siginfo, KM_NOSLEEP);
	mutex_exit(&p->p_lock);
}

/*
 * Handle asynchronous parity/ECC memory errors.
 * XXX - Fix ECC handling
 */
void
asyncerr_sun4()
{
	u_int		err;
	caddr_t		vaddr;

	err = MEMERR->me_err;	/* read the error bits */
	switch (cputype) {
	case CPU_SUN4_110:
		parity_error(err);
		break;

	case CPU_SUN4_330:
		vaddr = (caddr_t)MEMERR->me_vaddr;
		memerr_330(MERR_ASYNC, err, vaddr, 0, (struct regs *)0);
		break;

	case CPU_SUN4_260:
	case CPU_SUN4_470:
		ecc_error(err);
		break;
	default:
		printf("memory error handler: unknown CPU\n");
		break;
	}
}


static void
parity_error(per)
	u_int		    per;
{
	char		   *mess = 0;
	struct ctx	   *ctx;
	struct pte	    pme;
	extern struct ctx  *ctxs;

	per = MEMERR->me_err;

	/*
	 * Since we are going down in
	 * flames, disable further
	 * memory error interrupts to
	 * prevent confusion.
	 */
	MEMERR->me_err &= ~ER_INTENA;

	if ((per & PER_ERR) != 0) {
		printf("Parity Error Register %b\n", per, PARERR_BITS);
		mess = "parity error";
	}
	if (!mess) {
		printf("Memory Error Register %b\n", per, ECCERR_BITS);
		mess = "unknown memory error";
	}
	printf("DVMA = %x, context = %x, virtual address = %x\n",
	    (per & PER_DVMA) >> PER_DVMA_SHIFT,
	    (per & PER_CTX) >> PER_CTX_SHIFT,
	    MEMERR->me_vaddr);

	ctx = mmu_getctx();

	mmu_setctx(&ctxs[(per & PER_CTX) >> PER_CTX_SHIFT]);
	mmu_getpte((caddr_t) MEMERR->me_vaddr, &pme);
	printf("pme = %x, physical address = %x\n", *(int *) &pme,
	    ptob(((struct pte *) & pme)->pg_pfnum)
	    + (MEMERR->me_vaddr & PGOFSET));

	/*
	 * print the U-number of the
	 * failure
	 */
	if (cputype == CPU_SUN4_110)
		pr_u_num110((caddr_t) MEMERR->me_vaddr, per);

	mmu_setctx(ctx);

	/*
	 * Clear the latching by
	 * writing to the top nibble of
	 * the memory address register
	 */
	MEMERR->me_vaddr = 0;

	panic(mess);
	/* NOTREACHED */
}

static void
memerr_init_parity(void)
{
	MEMERR->me_per = PER_INTENA | PER_CHECK;
}

/*
 * print the U-number(s) of the failing memory location(s).
 */
static void
pr_u_num110(virt_addr, per)
	caddr_t		    virt_addr;
	u_int		    per;
{
	int		    bank;
	int		    bits;
	int		    u_num_offs = 0;

	static char	   *u_num[] = {
		"U1503", "U1502", "U1501", "U1500",
		"U1507", "U1506", "U1505", "U1504",
		"U1511", "U1510", "U1509", "U1508",
		"U1515", "U1514", "U1513", "U1512",
		"U1603", "U1602", "U1601", "U1600",
		"U1607", "U1606", "U1605", "U1604",
		"U1611", "U1610", "U1609", "U1608",
		"U1615", "U1614", "U1613", "U1612"
	};

	bank = bank_num(virt_addr);
	if (bank == -1) {
		printf("\nNo U-number can be calculated for this ");
		printf("memory configuration.\n\n");
		return;
	}
#ifdef BOOT_STICK
/*  XXX:  FIXME!!!  The code bracketed by BOOT_STICK must be */
/*  redone for ON4.2 when we backport the root dev stick  (KNH)  */
	if (((prom_memory_physical())->size == 4 * 1024 * 1024) ||
	    ((prom_memory_physical())->size == 16 * 1024 * 1024)) {
		u_num_offs = 16;
	}
#endif BOOT_STICK
	if ((bits = ((per & PER_ERR) >> 1)) > 2)
		bits = 3;
	printf(" simm = %s\n", u_num[4 * bank + bits + u_num_offs]);
}

static int
bank_num(virt_addr)
	caddr_t		    virt_addr;
{
	u_long		    phys_addr;
	int		    bank = -1;
	int		    temp;
	int		    memsize = 0;

#ifdef BOOT_STICK
	memsize = (prom_memory_physical())->size;
#endif BOOT_STICK

	memsize = memsize >> 20;
	phys_addr = map_getpgmap((caddr_t) virt_addr);

	switch (memsize) {
	case 16:
		bank = (phys_addr & 0x3) ^ 0x3;
		break;
	case 8:
	case 32:
		bank = (phys_addr & 0x7) ^ 0x7;
		break;
	case 20:
		temp = (phys_addr & 0x3);	/* need bits 14:13 and 24 */
		bank = (((phys_addr >> 9) & 0x4) | temp) ^ 0x7;
		break;
	default:
		/*
		 * no U-number can be
		 * calculated for this
		 * memory size.
		 */
		bank = -1;
		break;
	}

	return (bank);
}

/*
 * Since there is no implied ordering of the memory cards, we store
 * a zero terminated list of pointers to eccreg's that are active so
 * that we only look at existent memory cards during softecc() handling.
 */
union eccreg	   *ecc_alive[MAX_ECC + 1];

int		    prtsoftecc = 1;
extern int	    noprintf;
int		    memintvl = MEMINTVL;

static void
ecc_error(err)
	u_int		    err;
{
	char		   *mess = 0;
	struct ctx	   *ctx;
	struct pte	    pme;
	extern struct ctx  *ctxs;

	if ((err & EER_ERR) == EER_CE) {
		MEMERR->me_err = ~EER_CE_ENA & 0xff;
		switch (cputype) {
		case CPU_SUN4_260:
			softecc_260();
			break;
		case CPU_SUN4_470:
			softecc_470();
			break;
		default:
			printf("ecc handler: unknown CPU\n");
		}
		return;
	}
	/*
	 * Since we are going down in
	 * flames, disable further
	 * memory error interrupts to
	 * prevent confusion.
	 */
	MEMERR->me_err &= ~ER_INTENA;

	if ((err & EER_ERR) != 0) {
		printf("Memory Error Register %b\n", err, ECCERR_BITS);
		if (err & EER_TIMEOUT)
			mess = "memory timeout error";
		if (err & EER_UE)
			mess = "uncorrectable ECC error";
		if (err & EER_WBACKERR)
			mess = "writeback error";
	}
	if (!mess) {
		printf("Memory Error Register %b\n", err, ECCERR_BITS);
		mess = "unknown memory error";
	}
	printf("DVMA = %x, context = %x, virtual address = %x\n",
	    (MEMERR->me_err & EER_DVMA) >> EER_DVMA_SHIFT,
	    (MEMERR->me_err & EER_CTX) >> EER_CTX_SHIFT,
	    MEMERR->me_vaddr);

	ctx = mmu_getctx();
	mmu_setctx(&ctxs[(MEMERR->me_err & EER_CTX) >> EER_CTX_SHIFT]);
	mmu_getpte((caddr_t) MEMERR->me_vaddr, &pme);
	printf("pme = %x, physical address = %x\n", *(int *) &pme,
	    ptob(((struct pte *) & pme)->pg_pfnum)
	    + (MEMERR->me_vaddr & PGOFSET));
	mmu_setctx(ctx);

	/*
	 * Clear the latching by
	 * writing to the top nibble of
	 * the memory address register
	 */
	MEMERR->me_vaddr = 0;

	/*
	 * turn on interrupts, else sync will not go through
	 */
	set_intreg(IR_ENA_INT, 1);

	panic(mess);
	/* NOTREACHED */
}

static struct {
	u_char		    m_syndrome;
	char		    m_bit[3];
}		    memlogtab[] = {

	0x01, "64", 0x02, "65", 0x04, "66", 0x08, "67", 0x0B, "30", 0x0E, "31",
	0x10, "68", 0x13, "29", 0x15, "28", 0x16, "27", 0x19, "26", 0x1A, "25",
	0x1C, "24", 0x20, "69", 0x23, "07", 0x25, "06", 0x26, "05", 0x29, "04",
	0x2A, "03", 0x2C, "02", 0x31, "01", 0x34, "00", 0x40, "70", 0x4A, "46",
	0x4F, "47", 0x52, "45", 0x54, "44", 0x57, "43", 0x58, "42", 0x5B, "41",
	0x5D, "40", 0x62, "55", 0x64, "54", 0x67, "53", 0x68, "52", 0x6B, "51",
	0x6D, "50", 0x70, "49", 0x75, "48", 0x80, "71", 0x8A, "62", 0x8F, "63",
	0x92, "61", 0x94, "60", 0x97, "59", 0x98, "58", 0x9B, "57", 0x9D, "56",
	0xA2, "39", 0xA4, "38", 0xA7, "37", 0xA8, "36", 0xAB, "35", 0xAD, "34",
	0xB0, "33", 0xB5, "32", 0xCB, "14", 0xCE, "15", 0xD3, "13", 0xD5, "12",
	0xD6, "11", 0xD9, "10", 0xDA, "09", 0xDC, "08", 0xE3, "23", 0xE5, "22",
	0xE6, "21", 0xE9, "20", 0xEA, "19", 0xEC, "18", 0xF1, "17", 0xF4, "16",
};

static void
memerr_init260(void)
{
	register union eccreg **ecc_nxt;
	register union eccreg *ecc;

	/*
	 * Map in ECC error registers.
	 */
	map_setpgmap((caddr_t) ECCREG,
	    PG_V | PG_KW | PGT_OBIO | btop(OBIO_ECCREG0_ADDR));

	/*
	 * Go probe for all memory
	 * cards and perform
	 * initialization. The address
	 * of the cards found is
	 * stashed in ecc_alive[]. We
	 * assume that the cards are
	 * already enabled and the base
	 * addresses have been set
	 * correctly by the monitor.
	 * Memory error interrupts will
	 * not be enabled until we take
	 * over the console (consconfig
	 * -> stop_mon_clock).
	 */
	ecc_nxt = ecc_alive;
	for (ecc = ECCREG; ecc < &ECCREG[MAX_ECC]; ecc++) {
		if (ddi_peekc((dev_info_t *)0, (char *) ecc,
		    (char *)0) != DDI_SUCCESS)
			continue;
		MEMERR->me_err = 0;	/* clear intr from mem register */
		ecc->ecc_s.syn |= SY_CE_MASK;	/* clear syndrome fields */
		ecc->ecc_s.ena |= ENA_SCRUB_MASK;
		ecc->ecc_s.ena |= ENA_BUSENA_MASK;
		*ecc_nxt++ = ecc;
	}
	*ecc_nxt = (union eccreg *) 0;	/* terminate list */
}

/*
 * Routine to turn on correctable error reporting.
 */
void
ce_enable(ecc)
	register union eccreg *ecc;
{
	ecc->ecc_s.ena |= ENA_BUSENA_MASK;
}

/*
 * Probe memory cards to find which one(s) had ecc error(s).
 * If prtsoftecc is non-zero, log messages regarding the failing
 * syndrome.  Then clear the latching on the memory card.
 */
static void
softecc_260()
{
	register union eccreg **ecc_nxt, *ecc;

	for (ecc_nxt = ecc_alive; *ecc_nxt != (union eccreg *) 0; ecc_nxt++) {
		ecc = *ecc_nxt;
		if (ecc->ecc_s.syn & SY_CE_MASK) {
			if (prtsoftecc) {
				noprintf = 1;	/* (not on the console) */
				memlog_260(ecc);	/* log the error */
				noprintf = 0;
			}
			ecc->ecc_s.syn |= SY_CE_MASK;	/* clear latching */
			/*
			 * disable
			 * board
			 */
			ecc->ecc_s.ena &= ~ENA_BUSENA_MASK;
			(void) timeout(ce_enable, (caddr_t) ecc, memintvl*HZ);
		}
	}
}

static void
memlog_260(ecc)
	register union eccreg *ecc;
{
	register int	    i;
	register u_char	    syn;
	register u_int	    err_addr;
	int		    unum;

	syn = (ecc->ecc_s.syn & SY_SYND_MASK) >> SY_SYND_SHIFT;
	/*
	 * Compute board offset of
	 * error by subtracting the
	 * board base address from the
	 * physical error address and
	 * then masking off the extra
	 * bits.
	 */
	err_addr = ((ecc->ecc_s.syn & SY_ADDR_MASK) << SY_ADDR_SHIFT);
	if (ecc->ecc_s.ena & ENA_TYPE_MASK)
		err_addr -=
		    (ecc->ecc_s.ena & ENA_ADDR_MASKL) << ENA_ADDR_SHIFTL;
	else
		err_addr -= (ecc->ecc_s.ena & ENA_ADDR_MASK) << ENA_ADDR_SHIFT;
	if ((ecc->ecc_s.ena & ENA_BDSIZE_MASK) == ENA_BDSIZE_8MB)
		err_addr &= MEG8;	/* mask to full address */
	if ((ecc->ecc_s.ena & ENA_BDSIZE_MASK) == ENA_BDSIZE_16MB)
		err_addr &= MEG16;	/* mask to full address */
	if ((ecc->ecc_s.ena & ENA_BDSIZE_MASK) == ENA_BDSIZE_32MB)
		err_addr &= MEG32;	/* mask to full address */
	printf("mem%d: soft ecc addr %x syn %b ",
	    ecc - ECCREG, err_addr, syn, SYNDERR_BITS);
	for (i = 0; i < (sizeof (memlogtab) / sizeof (memlogtab[0])); i++)
		if (memlogtab[i].m_syndrome == syn)
			break;
	if (i < (sizeof (memlogtab) / sizeof (memlogtab[0]))) {
		printf("%s ", memlogtab[i].m_bit);
		/*
		 * Compute U number on
		 * board, first figure
		 * out which half is
		 * it.
		 */
		if (atoi(memlogtab[i].m_bit) >= LOWER) {
			if ((ecc->ecc_s.ena & ENA_TYPE_MASK) == TYPE0) {
				switch (err_addr & ECC_BITS) {
				case U14XX:
					unum = 14;
					break;
				case U16XX:
					unum = 16;
					break;
				case U18XX:
					unum = 18;
					break;
				case U20XX:
					unum = 20;
					break;
				}
			} else {
				switch (err_addr & PEG_ECC_BITS) {
				case U14XX:
					unum = 14;
					break;
				case U16XX:
					unum = 16;
					break;
				case PEG_U18XX:
					unum = 18;
					break;
				case PEG_U20XX:
					unum = 20;
					break;
				}
			}
		} else {
			if ((ecc->ecc_s.ena & ENA_TYPE_MASK) == TYPE0) {
				switch (err_addr & ECC_BITS) {
				case U15XX:
					unum = 15;
					break;
				case U17XX:
					unum = 17;
					break;
				case U19XX:
					unum = 19;
					break;
				case U21XX:
					unum = 21;
					break;
				}
			} else {
				switch (err_addr & PEG_ECC_BITS) {
				case U15XX:
					unum = 15;
					break;
				case U17XX:
					unum = 17;
					break;
				case PEG_U19XX:
					unum = 19;
					break;
				case PEG_U21XX:
					unum = 21;
					break;
				}
			}
		}
		printf("U%d%s\n", unum, memlogtab[i].m_bit);
	} else
		printf("No bit information\n");
}

static int
atoi(num_str)
	register char	   *num_str;
{
	register int	    num;

	num = (*num_str++ & 0xf) * 10;
	num = num + (*num_str & 0xf);
	return (num);
}
/*
 * Moonshine memory boards have 4 banks of 72 bits per line,
 * what follows is the mapping of the syndrome bits to U numbers
 * for bank 0.	The 64 data bits d[0:63] are followed by the 8
 * syndrome/check bits s[0:7] or SX, S0, S1, S2, S4, S8, S16, S32.
 *
 * The U numbers for other banks can be calculated by multiplying the bank
 * number by 200 and adding to the bank zero U numbers given below:
 */

/* bit number to u number mapping */
static unsigned char u470[72] = {
	4, 5, 6, 7, 8, 9, 10, 11,			/* bits 0-7 */
	12, 13, 14, 15, 16, 17, 18, 19,			/* bits 8-15 */
	104, 105, 106, 107, 108, 109, 110, 111,		/* bits 16-23 */
	112, 113, 114, 115, 116, 117, 118, 119,		/* bits 24-31 */
	20, 21, 22, 23, 24, 25, 26, 27,			/* bits 32-39 */
	28, 29, 30, 31, 32, 33, 34, 35,			/* bits 40-47 */
	120, 121, 122, 123, 124, 125, 126, 127,		/* bits 48-55 */
	128, 129, 130, 131, 132, 133, 134, 135,		/* bits 56-63 */
	0, 1, 2, 3, 100, 101, 102, 103			/* check bits */
};

/* syndrome to bit number mapping */
static unsigned char syntab[] = {
	0xce, 0xcb, 0xd3, 0xd5, 0xd6, 0xd9, 0xda, 0xdc,	/* bits 0-7 */
	0x23, 0x25, 0x26, 0x29, 0x2a, 0x2c, 0x31, 0x34,	/* bits 8-15 */
	0x0e, 0x0b, 0x13, 0x15, 0x16, 0x19, 0x1a, 0x1c, /* bits 16-23 */
	0xe3, 0xe5, 0xe6, 0xe9, 0xea, 0xec, 0xf1, 0xf4,	/* bits 24-31 */
	0x4f, 0x4a, 0x52, 0x54, 0x57, 0x58, 0x5b, 0x5d, /* bits 32-39 */
	0xa2, 0xa4, 0xa7, 0xa8, 0xab, 0x45, 0xb0, 0xb5, /* bits 40-47 */
	0x8f, 0x8a, 0x92, 0x94, 0x97, 0x98, 0x9b, 0x9d,	/* bits 48-55 */
	0x62, 0x64, 0x67, 0x68, 0x6b, 0x6d, 0x70, 0x75,	/* bits 56-63 */
	0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80	/* check bits */
};

static void
memerr_init470(void)
{
	register union eccreg **ecc_nxt;
	register union eccreg *ecc;

	/*
	 * Map in ECC error registers.
	 *
	 * MJ: NON DDI COMPLIANT
	 */
	map_setpgmap((caddr_t) ECCREG,
	    PG_V | PG_KW | PGT_OBIO | btop(OBIO_ECCREG1_ADDR));

	ecc_nxt = ecc_alive;
	for (ecc = ECCREG; ecc < &ECCREG[MAX_ECC]; ecc++) {
		MEMERR->me_err = 0;
		if (ddi_peekc((dev_info_t *)0, (char *)ecc,
		    (char *)0) != DDI_SUCCESS)
			continue;
		ecc->ecc_m.err = 0;		 /* clear error latching */
		ecc->ecc_m.err = 1;		 /* clear CE led */
		ecc->ecc_m.err = 2;		 /* clear UE led */
		ecc->ecc_m.syn = 0;		 /* clear syndrome fields */
		/*
		 * this is correct ordering for first time, the
		 * prom has already done this, paranoia
		 */
		ecc->ecc_m.ena |= MMB_ENA_ECC|MMB_ENA_BOARD|MMB_ENA_SCRUB;
		*ecc_nxt++ = ecc;
	}
	*ecc_nxt = (union eccreg *) 0;	/* terminate list */
	MEMERR->me_err = 0;
	MEMERR->me_err = EER_INTENA | EER_CE_ENA;
}

/*
 * turn on reporting of correctable ecc errors
 */
void
enable_ce_470(ecc)
	register union eccreg *ecc;
{
	if (prtsoftecc) {
		noprintf = 1;
		printf("resetting ecc handling\n");
		noprintf = 0;
	}
	ecc->ecc_m.err = 1;		/* reset latching, turn off CE led */
	MEMERR->me_err = EER_INTENA|EER_CE_ENA;	/* reenable ecc reporting */
}

static void
softecc_470()
{
	register union eccreg **ecc_nxt, *ecc;

	for (ecc_nxt = ecc_alive; *ecc_nxt != (union eccreg *) 0; ecc_nxt++) {
		ecc = *ecc_nxt;
		if (ecc->ecc_m.err & MMB_CE_ERROR) {
			if (prtsoftecc) {
				noprintf = 1; /* not on the console */
				memlog_470(ecc);	/* log the error */
				noprintf = 0;
			}

			/*
			 * reset ecc errors in memory board
			 * 0 - enables latching of next ecc error
			 * 1 - enables latching of next ecc error and
			 *		clears CE led
			 * 2 - enables latching of next ecc error and
			 * 		clears UE led
			 */
			ecc->ecc_m.err = 0;

			/* disable ecc error reporting temporarily */
			MEMERR->me_err &= ~(EER_CE_ENA);

			/* turn ecc error reporting at a later time */
			(void) timeout(enable_ce_470, (caddr_t)ecc,
			    memintvl * HZ);
		}
	}
}

static void
memlog_470(ecc)
	register union eccreg *ecc;
{
	register int	    bit;
	register u_char	    syn;
	register u_int	    err_addr;
	register int	    bank;
	register int	    mask;

	bank = ffs((long)ecc->ecc_m.err & MMB_BANK) - 1;
	syn = (ecc->ecc_m.syn & (0xff << (8*bank))) >> (8*bank);
	switch ((ecc->ecc_m.ena & MMB_BDSIZ) >> MMB_BDSIZ_SHIFT) {
		case 1:
			mask = MMB_PA_128;
			break;
		default:
		case 0:
			mask = MMB_PA_32;
			break;
	}
	err_addr = (ecc->ecc_m.err & mask) >> MMB_PA_SHIFT;
	printf("mem%d: soft ecc addr %x syn %b ",
		ecc - ECCREG, err_addr, syn, SYNDERR_BITS);
	for (bit = 0; bit < sizeof (syntab); bit++)
		if (syntab[bit] == syn)
			break;
	if (bit < sizeof (syntab)) {
		printf("bit %d ", bit);
		printf("U%d\n", 2000 + u470[bit] + bank*200);
	} else
		printf("No bit information\n");
}

static void
memerr_init330(void)
{
	MEMERR->me_per = PER_INTENA | PER_CHECK;
}

/* zero is not really a valid index into the next two arrays */
static int	    xb2b[] = {
	8 << 20, 96 << 20, 64 << 20, 48 << 20,
	72 << 20, 40 << 20, 24 << 20, 16 << 20};
static int	    xb1s[] = {
	22, 24, 24, 22, 24, 24, 22, 22};

static char	    simerrfmt[] =
	"Memory error in SIMM U%d on %s card\n";
static char	    simerrsfmt[] =
	"Memory error somewhere in SIMMs U%d through U%d on %s card\n";
static char	    c_cpu[] = "CPU";
#ifdef BOOT_STICK
static char	    c_9u[] = "9U Memory";
#endif /* BOOT_STICK */
static char	    c_3u1[] = "First 3U Memory";
static char	    c_3u2[] = "Second 3U Memory";

#define	SIMERR(s, u)		printf(simerrfmt, u, s)
#define	SIMERRS(s, u, v)	printf(simerrsfmt, u, v, s)

static void
simlog_330(errreg, board, simno)
	unsigned	    errreg;
	char		   *board;
	int		    simno;
{
	/*
	 * If PA0 and PA1 are zeros,
	 * access goes to the third
	 * simm in the row, i.e. U703
	 * and so on.
	 */
	if (!(errreg & PER_ERR))
		SIMERRS(board, simno, simno + 3);
	if (errreg & PER_ERR00)
		SIMERR(board, simno);
	if (errreg & PER_ERR08)
		SIMERR(board, simno + 1);
	if (errreg & PER_ERR16)
		SIMERR(board, simno + 2);
	if (errreg & PER_ERR24)
		SIMERR(board, simno + 3);
}

static void
memlog_330(vaddr, errreg)
	int		    vaddr;
	unsigned	    errreg;
{
	int		    paddr;	/* 32-bit physical address of error */
	int		    conf;	/* memory configuration code */
	int		    obmsize;	/* bytes of onboard memory */
	int		    obmshft;	/* shift factor for for cpu card */
	int		    shft;	/* shift factor for 3U mem board */
	int		    base;	/* base address of "this" board */
	char		   *c_3u;	/* which 3U card is involved */
#ifdef BOOT_STICK
	int		    x9ushft;	/* shift factor for 9U mem card */
#endif /* BOOT_STICK */

	paddr = ((map_getpgmap((caddr_t) vaddr) & 0x7FFFF) << 13) |
	    (vaddr & 0x1FFF);

	printf("Parity Error: Physical Address 0x%x (Virtual Address 0x%x ",
	    paddr, vaddr);
	printf("Error Register 0x%b\n", errreg, PARERR_BITS);

	conf = read_confreg();
	obmshft = (conf & 1) ? 22 : 24; /* shift distance per bank */
	obmsize = 2 << obmshft; /* always 2 sets of 4 simms */
	conf = (conf & CONF) >> CONF_POS;	/* leave only xb1 config */

	if (paddr < obmsize) {
		/*
		 * Error is somewhere
		 * on the CPU board.
		 */
		simlog_330(errreg, c_cpu,
		    1300 + 4 * (paddr >> obmshft));
	} else {
		/*
		 * If we have a 9U
		 * card, the error is
		 * somewhere on it.
		 */
#ifdef BOOT_STICK
		if (((prom_memory_physical())->size - obmsize) > (48 << 20))
			x9ushft = 24;
		else
			x9ushft = 22;

		if (((prom_memory_physical())->size - obmsize) == (32 << 20)) {
			printf(
	"If you have a 9U memory card populated with 1Meg SIMMs:\n");
			simlog_330(errreg, c_9u,
			    1600 + 100 * ((paddr - obmsize) >> (x9ushft + 1)) +
			    4 * ((paddr >> x9ushft) & 1));

			x9ushft = 24;
			printf(
	"If you have a 9U memory card populated with 4Meg SIMMs:\n");
		} else
			printf("If you have a 9U memory card:\n");

		simlog_330(errreg, c_9u,
		    1600 + 100 * ((paddr - obmsize) >> (x9ushft + 1)) +
		    4 * ((paddr >> x9ushft) & 1));
#endif BOOT_STICK

		printf("If you DO NOT have a 9U memory card:\n");

		if (paddr < xb2b[conf]) {
			/*
			 * Error is on the first 3U card;
			 * set xb1s[2] to the "other" simm size
			 */
			xb1s[2] = (22 + 24) - obmshft;
			base = obmsize;
			shft = xb1s[conf];
			c_3u = c_3u1;
		} else {
			/*
			 * Error is on
			 * the second
			 * 3U card
			 */
			base = xb2b[conf];
#ifdef BOOT_STICK
		if (((prom_memory_physical())->size - xb2b[conf]) > (16 << 20))
				shft = 24;
			else
#endif BOOT_STICK
				shft = 22;
			c_3u = c_3u2;
		}
		/*
		 * First 8(32)meg is
		 * low half of 3U card,
		 * Second 8(32)meg is
		 * high half of 3U
		 * card. Selection of
		 * the 4(16)meg row is
		 * done by PA22(24).
		 */
		simlog_330(errreg, c_3u,
		    700 + 100 * ((paddr - base) >> (shft + 1)) +
		    ((paddr >> shft) & 1));
	}
}

/*
 * Mostly these should be parity errors.
 * The per field is the contents of the parity error reg.
 * Addr is the virtual address of the error.
 * Fault is the type of fault (TEXT or DATA); rp is the regs pointer
 * from trap.
 * This routine applies to sun4/330 implementations.
 */
void
memerr_330(type, per, addr, faulttype, rp)
	u_int type, per;
	caddr_t addr;
	unsigned faulttype;		/* Only if MERR_SYNC */
	struct regs *rp;		/* ditto */
{
	char *mess = 0;
	struct pte pme;

	mmu_getpte((caddr_t)addr, &pme);

	/* Log the error */
	/* SAC: Differs from sun4c */
	memlog_330((int) addr, per);

	if (type == MERR_SYNC) {
		/* Probably need to do a parerr_addr */
		/* and re-log! */

		/* Do a retry -- could be flakey hardware */
		/* SAC: Not in sun4c */
		if (parerr_retry(addr) == 0) {
			/* it worked */
			printf(
		    "Parity error is transient; system operation continues\n");
			return;
		}

		/* Retry failed, so try recovery */
		if (parerr_recover(addr, per, &pme, faulttype, rp) == 0) {
			printf("System operation can continue\n");
			return;
		}

		/* Recovery failed, so reset and panic */
		printf("System operation cannot continue, ");
		printf("will test location anyway.\n");
		(void) parerr_reset(addr, &pme);
		mess = "parity error";
	} else {
		/* DVMA error -- no way to retry or recover */
		MEMERR->me_vaddr = 0;	/* reset to allow more errors */
		printf("System operation cannot continue, ");
		printf("will test location anyway.\n");
		(void) parerr_reset(addr, &pme);
		mess = "Aynchronous parity error - DVMA operation";
		/*
		 * locore.s has disabled interrupts before
		 * calling us.  We must re-enable before panic'ing in case we
		 * are going to dump across the ethernet, as nfs_dump requires
		 * that interrupts be enabled.
		 */
		set_intreg(IR_ENA_INT, 1);
	}

	panic(mess);
	/*NOTREACHED*/
}

/*
 * parerr_retry()
 * Sometimes we get reports of parity errors when everything is really
 * okay, so just retry the fetch and see what happens.
 *
 * SAC: NEED TO ADD THIS TO SUN4C
 */
int
parerr_retry(addr)
	caddr_t		    addr;
{
	long		    retval = 0;
	long		    taddr = 0;

	/*
	 * there may have been more
	 * than one byte parity error,
	 * test out a long word peek.
	 * Adjust the address to do
	 * that
	 */

	addr = (caddr_t) ((int) addr & ~3);		/* round down */

	if (vac)
		vac_flushone(addr);	/* always flush the line before use */

	/* read and check for error */
	retval = ddi_peekl((dev_info_t *)0, (long *) addr, (long *) &taddr);

	if (vac)
		vac_flushone(addr);

	if (retval == DDI_FAILURE) {
		printf(" HARD Parity error \n");
		parity_hard_error++;
	}
	return (retval);

}

/*
 * Patterns to use to determine if a location has a hard or soft parity
 * error.
 * The zero is also an end-of-list marker, as well as a pattern.
 */
static long parerr_patterns[] = {
	0xAAAAAAAA,	/* Alternating ones and zeroes */
	0x55555555,	/* Alternate the other way */
	0x01010101,	/* Walking ones ... */
	0x02020202,	/* ... four bytes at once ... */
	0x04040404,	/* ... from right to left */
	0x08080808,
	0x10101010,
	0x20202020,
	0x40404040,
	0x80808080,
	0x7f7f7f7f,	/* And now walking zeros, from left to right */
	0xbfbfbfbf,
	0xdfdfdfdf,
	0xefefefef,
	0xf7f7f7f7,
	0xfbfbfbfb,
	0xfdfdfdfd,
	0xfefefefe,
	0xffffffff,	/* All ones */
	0x00000000,	/* All zeroes -- must be last! */
};

/*
 * Reset a parity error so that we can continue operation.
 * Also, see if we get another parity error at the same location.
 * Return 0 if error reset, -1 if not.
 * We need to test all the words in a cache line, if cache is on.
 */
int
parerr_reset(addr, ppte)
	caddr_t		 addr;
	struct pte	*ppte;
{
	int	retval;
	long	*bad_addr, *paddr;
	long	i, j;
	int	k;

	/*
	 * We need to set the protections to make sure that we can write
	 * to this page.  Since we are giving up the page anyway, we
	 * don't worry about restoring the protections.
	 * Must flush *before* changing permissions.
	 */
	vac_flushone(addr);	/* SAC: better than sun4c */

	ppte->pg_prot = KW;
	mmu_setpte(addr, *ppte);

	/*
	 * If the cache is on, make sure we reset all bad words in this
	 * cache-line image.
	 * XXX Unfortunately, we can't use parerr_addr because there's
	 * XXX no guarantee that resetting a bad word will fix it.  We have to
	 * XXX reset the entire cache-line image.
	 */
	if (vac) {
#ifdef notdef
		caddr_t caddr = addr;

		while (caddr = parerr_addr(caddr))
			*(long *)((int)caddr & ~3) = 0;
#else notdef
		long *laddr;

		laddr = (long *) ROUND_ADDR(addr, vac_linesize);
		for (i = 0; i < vac_linesize; i += sizeof (*laddr))
			*laddr++ = 0;
#endif notdef
	}

	/*
	 * Test the word with successive patterns, to see if the parity
	 * error was an ephemeral event or a permanent problem.
	 */
	bad_addr =
	    (long *) ROUND_ADDR(addr, vac ? vac_linesize : sizeof (long));
	k = 0;
	retval = 0;
	do {
		paddr = parerr_patterns;
		do {
			i = *paddr++;
			*bad_addr = i;		/* store pattern */
			if ((ddi_peekl((dev_info_t *)0,
			    bad_addr, &j) != DDI_SUCCESS) || (i != j)) {
				printf("parity error at %x with pattern %x.\n",
					bad_addr, i);
				/* trap clears for us on sun4 */
				retval++;
			}
			vac_flush((caddr_t) bad_addr, 4);
		} while (i);
		bad_addr++;
		k += sizeof (*bad_addr);
	} while (vac && k < vac_linesize);

	/* Convert return value to what caller expects */
	if (retval)
		retval = -1;
	printf("parity error at %x is %s.\n",
		ptob(ppte->pg_pfnum) + ((u_int)addr & PGOFSET),
		retval ? "permanent" :
			"transient");
	return (retval);
}

/*
 * Recover from a parity error.  Returns 0 if successful, -1 if not.
 */
/* ARGSUSED */
static int
parerr_recover(vaddr, per, pte, type, rp)
	caddr_t	vaddr;
	u_int per;
	struct	pte	*pte;
	unsigned	 type;
	struct regs	*rp;
{
	struct	page	*pp;

	/*
	 * If multiple parity errors, then can't do anything.
	 * SAC: Only in Sun4c architecture
	 */
#ifdef	notdef
	if (per & PER_MULTI) {
		vac_flushall();	/* must flush cache! */
		printf("parity recovery: more than one error\n");
		return (-1);
	}
#endif /* notdef */

	/*
	 * Pages with no page structures or vnode are kernel pages.
	 * We cannot regenerate them, since there is no copy of
	 * the data on disk.
	 * And if the page was being used for i/o, we can't recover?
	 * Can't recover if page is "kept", either.
	 */
	pp = page_numtopp(pte->pg_pfnum, PG_EXCL);
	if (pp == (struct page *) 0) {
		printf("parity recovery: no page structure\n");
		return (-1);
	}
	if (pp->p_vnode == &kvp) {
		printf("parity recovery: no vnode\n");
		return (-1);
	}

	/*
	 * Sync all the mappings for this page, so that we get
	 * the bits for all of them page, not just the one that got
	 * a parity error.
	 */
	hat_pagesync(pp, HAT_ZERORM);

	/*
	 * If the page was modified, then the disk version is out
	 * of date.  This means we have to kill all the processes
	 * that use the page.  We may find a kernel use of the page,
	 * which means we have to return an error indication so that
	 * the caller can panic.
	 *
	 * If the page wasn't modified, then check for a data fault from
	 * a load double instruction.  Some of those aren't recoverable.
	 */
	if (PP_ISMOD(pp)) {
		if (hat_kill_procs(pp, vaddr) != 0) {
			return (-1);
		}
	} else if (type == T_DATA_FAULT) {
		u_int	inst;
		int	isuser = USERMODE(rp->r_ps);

		inst = isuser ? fuword((int *)rp->r_pc) : *(u_int *)rp->r_pc;
		if (check_ldd(inst, vaddr) != 0) {
			if (isuser) {
				printf("pid %d killed: parity error on ldd\n",
					curproc->p_pid);
				uprintf("pid %d killed: parity error on ldd\n",
					curproc->p_pid);
				memerr_signal(vaddr);
			} else {
				printf("parity recovery: unrecoverable ldd\n");
				return (-1);
			}
		}
	}

	if (pp->p_vnode == NULL) {
		cmn_err(CE_WARN, "ECC error recovery: no vnode\n");
		return (-1);
	}

	/*
	 * Give up the page.  When the fetch is retried, the page
	 * will be automatically faulted in.  Note that we have to
	 * kill the processes before we give up the page, or else
	 * the page will no longer exist in the processes' address
	 * space.
	 */
	page_giveup(vaddr, pte, pp);
	return (0);
}

/*
 * Check if this is a load double (alternate) instruction.
 * If we faulted on the second full word and the destination register
 * was also one of the source registers, we can't recover.
 */
static int
check_ldd(inst, vaddr)
	register u_int inst;
	caddr_t	vaddr;
{
	register u_int rd, rs1, rs2;
	register int immflg;

	if ((inst & 0xc1780000) != 0xc0180000)
		return (0);			/* not LDD(A) */

	rd =  (inst >> 25) & 0x1e;		/* ignore low order bit */
	rs1 = (inst >> 14) & 0x1f;
	rs2 =  inst & 0x1f;
	immflg = (inst >> 13) & 1;

	if (rd == rs1 || (immflg == 0 && rd == rs2)) {
		/*
		 * We faulted on a load double and the first destination
		 * register was also one of the source registers.
		 * If the address is on a double-word boundary, then the
		 * fault was on the first load and we haven't clobbered
		 * the register yet.
		 * If the address is 4 mod 8, then we have already
		 * loaded the first word, we clobbered the register, and
		 * we can't recover.
		 * And if it is anything else, something is wrong,
		 * anyway.
		 */

		if (((int)vaddr & 7) != 0)
			return (-1);	/* can't recover */
	}

	return (0);
}
