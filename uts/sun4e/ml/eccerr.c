#ident	"@(#)eccerr.c	1.8	94/03/31 SMI"

/*
 * Copyright (c) 1992-93 by Sun Microsystems, Inc.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/intreg.h>
#include <sys/memerr.h>
#include <sys/buserr.h>
#include <sys/pte.h>
#include <sys/mmu.h>
#include <sys/eccreg.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>


/*
 * Since there is no implied ordering of the memory cards, we store
 * a zero terminated list of pointers to eccreg's that are active so
 * that we only look at existent memory cards during ecc_correctable()
 * handling.
 */

struct eccreg	*ecc_alive[MAX_ECC+1];

int		prtsoftecc	= 1;
int		memintvl	= MEMINTVL;
extern int	noprintf;

void	ecc_memlog();

/*
 * Probe for all ECC memory cards on the SBus/P2 bus
 * and perform initialization.
 *
 * We stash the address of each board found in ecc_alive[].
 * We assume that the boards are already enabled and the
 * PROM monitor has properly initialized each by writing
 * zeroes to every word of memory.
 */
void
ecc_init()
{
	register struct eccreg **ecc_nxt;
	register struct eccreg *ecc;
	ecc_nxt = ecc_alive;

	for (ecc = ECCREG; ecc < &ECCREG[MAX_ECC]; ++ecc) {
		long	val;

	if (ddi_peekl((dev_info_t *)0, (long *)ecc, &val) == DDI_SUCCESS) {
			ecc->ecc_synd_status = 0; /* clear syndrome fields */
			ecc->ecc_enable |= ECC_INTENABLE;
			*ecc_nxt++ = ecc;
		}
	}
	*ecc_nxt = (struct eccreg *)0;		/* terminate list */
}	/* end of ecc_init */

/*
 * Turn on correctable error interrupts. Interrupts from this
 * source occur on IU level 7.
 */
void
ecc_enableint(struct eccreg *ecc)
{
	ecc->ecc_enable |= ECC_INTENABLE;
}	/* end of ecc_enableint */

/*
 * P2 bus interrupts are evidence of one of two conditions:
 * (1) Correctable ECC errors.
 * (2) An indication of monsters.
 * We will assume (1), and try to report the source.
 *
 * o Probe memory cards to find which one(s) had ecc error(s).
 * o If prtsoftecc is non-zero, log messages regarding the failing
 *   syndrome.
 * o Clear the latched error on the memory card.
 */
int
ecc_correctable()
{
	register struct eccreg	**ecc_nxt;
	register struct eccreg	*ecc;
	register int		found_error = 0;
	/* Don't need to include kernel.h hz = _HZ = 100 */
	register int	hz = 100;

	for (ecc_nxt = ecc_alive; *ecc_nxt != (struct eccreg *)0; ++ecc_nxt) {
		register u_int	synd_status;

		ecc = *ecc_nxt;
		synd_status = ecc->ecc_synd_status;
		if (synd_status & SY_CE_MASK) {
			if (prtsoftecc) {
				noprintf = 1;	/* (not on the console) */
				ecc_memlog(ecc, 0);	/* log the error */
				noprintf = 0;
			}
			found_error = 1;
			ecc->ecc_synd_status = 0;	/* clear latching */
			ecc->ecc_enable &=
			    ~ECC_INTENABLE; /* disable CE reporting */
			(void) timeout(ecc_enableint, (caddr_t)ecc,
			    memintvl * hz);
		}
	}
	return (found_error);
}	/* end of ecc_correctable */

/*
 * Probe memory cards to find which one(s) had uncorrectable ecc error(s).
 * Log messages regarding the failing syndrome.  Then clear the latching
 * on the memory card.
 */
int
ecc_uncorrectable(addr)
caddr_t	addr;
{
	struct pte	pme;
	int		found_error = 0;
	struct eccreg	**ecc_nxt;
	struct eccreg	*ecc;

	mmu_getpte((caddr_t)addr, &pme);
	for (ecc_nxt = ecc_alive; *ecc_nxt != (struct eccreg *)0; ++ecc_nxt)
	{

		register u_int	synd_status;

		ecc = *ecc_nxt;
		synd_status = ecc->ecc_synd_status;
		if (synd_status & SY_UE_MASK)
		{
			noprintf = 0;		/* (not on the console) */
			ecc_memlog(ecc, 1);		/* log the error */
			noprintf = 0;
			found_error = 1;
			ecc->ecc_synd_status = 0;	/* clear latching */
/*
 * Disable further error detection of all kinds:
 * we're about to panic.
 */
			ecc->ecc_enable &= (~ECC_INTENABLE | ECC_CORRECT_);
		}
	}
	return (found_error);
}	/* end of ecc_uncorrectable */


int
atoi(num_str)
register char	*num_str;
{
	register int num;

	num = (*num_str++ & 0xf) * 10;
	num = num + (*num_str & 0xf);
	return (num);
}	/* end of atoi */

u_int
ecc_base_address(ecc_ena)
u_int	ecc_ena;
{
	register u_int	ecc_offset;
	register u_int	ecc_base_addr;

	ecc_offset = (ecc_ena & ECC_BOARDID_) << ECC_BOARDID_SHIFT;
	ecc_base_addr = (ecc_ena & ECC_HILOMEM) ? ECC_LOMEM : ECC_HIMEM;
	return (ecc_base_addr | ecc_offset);
}	/* end of ecc_base_address */

#define	FOUR_MEG	0x0400000
#define	EIGHT_MEG	0x0800000
#define	TWELVE_MEG	0x0c00000
#define	SIXTEEN_MEG	0x1000000
#define	THIRTYTWO_MEG	0x2000000
#define	FORTYEIGHT_MEG	0x3000000
#define	SIXTYFOUR_MEG	0x4000000

u_int	ecc_board_size_tbl[4][2] = {
	{	SIXTEEN_MEG,	FOUR_MEG,	},
	{	THIRTYTWO_MEG,	EIGHT_MEG,	},
	{	FORTYEIGHT_MEG,	TWELVE_MEG,	},
	{	SIXTYFOUR_MEG,	SIXTEEN_MEG,	},
};

/*
 * Return an indication of the density of the DRAMs
 * used on this board.
 * Returns: 1 == 1Mbit DRAM ; 0 == 4Mbit DRAM
 */
u_int
ecc_dram_density(ecc_ena)
u_int	ecc_ena;
{
	return ((ecc_ena & ECC_RAMSIZE) >> 13);
}	/* end ecc_dram_density */

/*
 * Return the actual size of the indicated ECC
 * RAM board in bytes.
 */
u_int
ecc_board_size(ecc_ena, dram_density)
u_int	ecc_ena;
u_int	dram_density;
{
	register u_int	boardsize;

	boardsize = (ecc_ena & ECC_BOARDSIZE_) >> 11;
	if ((0 != boardsize) && (boardsize <= 4))
		if ((dram_density == 0) || (dram_density == 1))
			return (ecc_board_size_tbl[boardsize][dram_density]);
	printf("ecc_board_size: unknown board size\n");
	return (0);
}	/* end of ecc_board_size */

#define	BANKS		4

char *ecc_banks[BANKS][4] =
{
	{ "U0201", "U0202", "U0203", "U0204"	},
	{ "U0206", "U0207", "U0208", "U0209"	},
	{ "U0211", "U0212", "U0213", "U0214"	},
	{ "U0216", "U0217", "U0218", "U0219"	},
};

void
ecc_unum_print(offset, dram_density)
u_int	offset;
u_int	dram_density;
{
	u_int	bank;
	u_int	byte;

	if (dram_density)
		bank = offset >> 22;		/* 4MByte/bank */
	else bank = offset >> 24;		/* 16MByte/bank */
	byte = offset & 0x3;
	if (bank < BANKS)
		printf("ECC: Failed SIMM = '%s'\n", ecc_banks[bank][byte]);
	else printf("ECC: error in computing bank#: bank = %d\n", bank);
}	/* end of ecc_unum_print */

void
ecc_memlog(ecc, uncorrect)
struct eccreg	*ecc;
int		uncorrect;
{
	u_int	syndrome;
	u_int	err_addr;
	u_int	err_offset;
	u_int	enable;
	u_int	base_address;
	u_int	dram_density;
	u_int	board_size;

	syndrome	= ecc->ecc_synd_status & SY_SYND_MASK;
	err_addr	= ecc->ecc_paddr;
	enable		= ecc->ecc_enable;
	base_address	= ecc_base_address(enable);
	dram_density	= ecc_dram_density(enable);
	board_size	= ecc_board_size(enable, dram_density);

/*
 * Compute board offset of error by subtracting the board
 * base address from the physical error address and then
 * masking off the extra bits.
 */
	if (board_size != 0)
	{
		err_offset = err_addr - base_address;
		err_offset &= (board_size - 1);	/* mask to full address */
	printf("ECC board #%d: <%s ECC error> Address: 0x%x Syndrome: %b ",
		ecc - ECCREG,			/* ECC board # */
		uncorrect ? "hard" : "soft",	/* Error type */
		err_addr,		/* Address where error occured */
		syndrome,			/* Syndrome bits */
		SYNDERR_BITS);
		ecc_unum_print(err_offset, dram_density);
	}
}	/* end of ecc_memlog */
