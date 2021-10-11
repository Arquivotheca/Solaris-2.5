/*
 * Copyright (c) 1991 Sun Microsystems, Inc.
 */

#ident	"@(#)decode_xdb.c	1.20	93/05/31 SMI"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/autoconf.h>
#include <sys/pte.h>
#include <sys/cmn_err.h>
#include <sys/bt.h>
#include <sys/mmu.h>

int debug_autoconf = 0;
#define	APRINTF	if (debug_autoconf) printf

#define	CSR_UNIT(pageval)	((pageval >> (20 - MMU_PAGESHIFT)) & 0xff)
#define	ECSR_UNIT(pageval)	((pageval >> (24 - MMU_PAGESHIFT)) & 0xfe)

/*
 * Supposedly, this routine is the only place that enumerates details
 * of the encoding of the "bustype" part of the physical address.
 * This routine depends on details of the physical address map, and
 * will probably be somewhat different for different machines.
 */
void
decode_address(space, addr)
	u_int space;
	u_int addr;
{
	u_int residue = addr & MMU_PAGEOFFSET;
	u_int nibble = space & 0xf;
	u_int pageval = (addr >> MMU_PAGESHIFT)
		+ (nibble << (32 - MMU_PAGESHIFT));

	APRINTF("decode_address: space= %x, addr= %x\n", space, addr);

#if	defined(SPO_VIRTUAL)
	if (space == SPO_VIRTUAL) {
		cmn_err(CE_CONT, "?SPO_VIRTUAL: addr=0x%8x", addr);
		return;
	}
#endif	/* SPO_VIRTUAL */

	if (space != nibble) {
		cmn_err(CE_CONT, "?unknown: "
			"space=0x%x, addr=0x%8x", space, addr);
		return;
	}

	if (nibble == 0xf) {
		switch ((pageval >> (28 - MMU_PAGESHIFT)) & 0xf) {
			/* sorry about the bitmasks here, we *know* 'em */
			case 0xf: {
				cmn_err(CE_CONT, "?local(cpu ?x?) ");
				pageval &= 0x03ffff;	/* 18 bits */
				break;
			}

			case 0xe: {
				u_int device_id = CSR_UNIT(pageval);
				cmn_err(CE_CONT, "?csr(unit=0x%2x) ",
					device_id);
				pageval &= 0x0003ff;	/* 10 bits */
				break;
			}

			default: {
				u_int device_id = ECSR_UNIT(pageval);
				cmn_err(CE_CONT, "?ecsr(unit=0x%2x) ",
					device_id);
				pageval &= 0x003fff;	/* 14 bits */
				break;
			}
		}

		cmn_err(CE_CONT, "?addr=0x%6x.%3x", pageval, residue);
		return;
	}

	if (nibble & 8) {
		u_int sbus = (pageval >> 18) & 0xf;
		u_int slot = (pageval >> 16) & 0x3;
		cmn_err(CE_CONT, "?sbus%d, slot%d, addr=0x%6x.%3x",
			sbus, slot, (pageval & 0xffff), residue);
		return;
	}

	cmn_err(CE_CONT, "?paddr=0x%6x.%3x", pageval, residue);
}


/*
 * Compute the address of an I/O device within standard address
 * ranges and return the result.  This is used by DKIOCINFO
 * ioctl to get the best guess possible for the actual address
 * the card is at.
 */
int
getdevaddr(addr)
	caddr_t addr;
{
	struct pte pte;
	u_int pageno;
	u_int high_byte;

	mmu_getkpte(addr, &pte);
	pageno = pte.PhysicalPageNumber;
	high_byte = pageno >> 16;

	APRINTF("getdevaddr: pageno= 0x%x\n", pageno);

	switch (high_byte) {
		case 0xf0: {	/* local */
			u_int physaddr = mmu_ptob(pageno & 0x0ffff);
			return (physaddr);
		}
		case 0xfe: {	/* ecsr */
			u_int physaddr = mmu_ptob(pageno & 0x0ffff);
			return (physaddr);
		}
		case 0xff: {	/* csr */
			u_int physaddr = mmu_ptob(pageno & 0x00fff);
			return (physaddr);
		}
		default: {
			u_int physaddr = mmu_ptob(pageno & 0xffff);
			return (physaddr);
		}
	}
	/*NOTREACHED*/
}

/*
 * figure out what type of bus a page frame number points at.
 * NOTE: returns a BT_ macro, not the top four bits. most
 * things called "bustype" are actually just the top four
 * bits of the pte, which are part of the physical address
 * space as defined in the architecture and which change between
 * various implementations.
 * FIXME: we need pageno + AC!
 */
int
impl_bustype(pageno)
	u_int pageno;
{
	extern int pa_in_nvsimmlist(u_longlong_t);
	u_int space = pageno >> (32 - MMU_PAGESHIFT);

	APRINTF("bustype: pageno= 0x%6x\n", pageno);

	if (space < 8) {	/* hack */
		if (pa_in_nvsimmlist(mmu_ptob((u_longlong_t)pageno)))
			return (BT_NVRAM);
		return (BT_DRAM);
	}

	if (space == 0xf) {
		return (BT_OBIO);
	}

	if (space & 8) {
		return (BT_SBUS);
	}

	return (BT_UNKNOWN);
}
