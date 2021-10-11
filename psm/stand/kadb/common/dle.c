/*
 * Copyright (c) 1987 by Sun Microsystems, Inc.
 */

#ident  "@(#)dle.c 1.3     94/10/11 SMI"

#include <sys/types.h>
#include "sys/lance.h"
#include "sys/dle.h"
#include "sys/comvec.h"
#include "sys/openprom.h"
#include "symtab.h"
#include "sys/errno.h"
#include "sys/idprom.h"

extern union sunromvec *romp;	/* libprom.a wants one of these declared. */
extern int errno;	/* for peekl error checking */

static struct dle_memory_image *dle_iopb;
static struct dle_mem_header hdr_image;	/* storage for what hdr looks like */
u_int dle_read(int handle, char *buf, unsigned int bufsize);
u_int dle_write(int handle, char *buf, unsigned int size);
unsigned int dle_k_init_block();
unsigned int dle_get_vmem(int size);
unsigned int dle_get_daddr();
void	dle_w_rap(short value);
u_short dle_r_rap();
void	dle_w_rdp(short value);
u_short dle_r_rdp();
void feedback();	/* feedback for the user */
unsigned int dle_dma_addr;	/* holder for dma io address */

struct romvec_obp my_romvec;

/*
 * one time initialization of the data structures
 * called the first time entering the kernel agent for networking.
 * the strategy here is to build a local header structure here that
 * will be copied into the io memory when the time comes to use the
 * le chip.  This routine should return -1 if the initialization is
 * not possible at this time.
 */
int
dle_init()
{
	char		octet[6];	/* ethernet address */
	struct lmd *rmdp;		/* pointer to rec. mem. desc */
	int rmd; 			/* rmd index */
	unsigned int	offset;		/* used for DMA offset calculations */
	struct lance_init_block *ibp;	/* pointer to init block */
	int i;
	unsigned int addr;

	/* find the dma space */
	dle_iopb = (struct dle_memory_image *)
	    dle_get_vmem(sizeof (struct dle_memory_image));

	if (dle_iopb == (struct dle_memory_image *) -1) {
		return (-1);	/* kernel is not at the correct point */
	}

	dle_dma_addr = dle_get_daddr(); /* get the address for the chip */
	if (dle_dma_addr == -1) {
		return (-1);
	}

	/* set up all of the data structures */

	/* first build the initialization block */
	ibp = &(hdr_image.ib);	/* get addr of our scratch header */
	ibp->ib_prom = 0;	/* not promiscuous */
	ibp->ib_loop = 0;	/* no loopback */
	ibp->ib_drty = 0;	/* no disable retry */
	ibp->ib_dtcr = 0;	/* enable CRC */
	ibp->ib_dtx = 0;	/* enable transmission */
	ibp->ib_drx = 0;	/* enable receive */

	dle_getmacaddr(octet);	/* retreive the ethernet addr */

	ibp->ib_padr[0] = octet[1];	/* stuff octet into the chip */
	ibp->ib_padr[1] = octet[0];
	ibp->ib_padr[2] = octet[3];
	ibp->ib_padr[3] = octet[2];
	ibp->ib_padr[4] = octet[5];
	ibp->ib_padr[5] = octet[4];

	for (i = 0; i < 4; i++) {	/* don't accept Multicast addresses */
		ibp->ib_ladrf[i] = 0;
	}

	/* setup the receive ring pointer */

	addr = DLE_OFFSET(hdr.rmd) + (unsigned int)dle_dma_addr;
	if (addr & 0x7) {
		printf("Bad RMDp address\n");
		return (-1);
	}
	ibp->ib_rdrp.lr_laddr = addr;
	ibp->ib_rdrp.lr_haddr = addr >> 16;
	ibp->ib_rdrp.lr_len = RLEN;

	/* setup the receive memory descriptors (RMDs) */

	for (rmd = 0; rmd < NUMRMDS; rmd++) {
		rmdp = &(hdr_image.rmd[rmd]);

		if ((unsigned int)rmdp & 0x3) {	/* is this address bad */
			return (-1);
		}

		/* set up rmd pointers */
		addr = DLE_OFFSET(rbuf) + (unsigned int)dle_dma_addr +
		    (DLE_INDIV_RBUFSIZE * rmd);
		rmdp->lmd_ladr = (u_short) addr;
		rmdp->lmd_hadr = (u_short) (addr >> 16);
		rmdp->lmd_bcnt = (u_short) -(DLE_INDIV_RBUFSIZE);
		rmdp->lmd_flags = LMD_OWN;
		rmdp->lmd_mcnt = 0;
	}

	/*
	 * The transmit ring is of length one because I don't plan on using
	 * this in a way that a number of buffers will be queued to be
	 * sent at once.  This also means that chaining of *transissions*
	 * will not be done.
	 */

	/* setup the transmit ring pointer */

	addr = DLE_OFFSET(hdr.tmd) + (unsigned int)dle_dma_addr;
	if (addr & 0x7) {
		printf("Bad TMDp address\n");
		return (-1);
	}
	ibp->ib_tdrp.lr_laddr = addr;
	ibp->ib_tdrp.lr_haddr = addr >> 16;
	ibp->ib_tdrp.lr_len = 0;

	/* setup the TMD */
	addr = DLE_OFFSET(tbuf) + (unsigned int)dle_dma_addr;
	hdr_image.tmd.lmd_ladr = (u_short) addr;
	hdr_image.tmd.lmd_hadr = (int) addr >> 16;
	hdr_image.tmd.lmd_bcnt = 0;	/* filled in on transmission */
	hdr_image.tmd.lmd_flags3 = 0;	/* filled in on transmission */
	hdr_image.tmd.lmd_flags = 0;	/* filled in on transmission */

	obp_write = romp->obp.op2_write;
	obp_read = romp->obp.op2_read;
	my_romvec = romp->obp;		/* copy it */
	my_romvec.v_xmit_packet = dle_write;
	my_romvec.op2_write = (int (*)()) dle_write;
	my_romvec.v_poll_packet = dle_read;
	my_romvec.op2_read = (int (*)()) dle_read;

	return (dle_regs_init());	/* give arch implementation a chance */
}

/*
 * The save area for save and restore
 */

static short saved_csr0, saved_rap;
unsigned int saved_initblock_ioaddr;
static struct dle_memory_image mem_save; /* save area for kernel mem */
static union sunromvec *save_romp;

/*
 * This is called to save the state of the le driver for the kernel.
 */
void
dle_save_le()
{
	int i;
	char *sptr, *dptr;	/* for copy */
	unsigned int low, high;

	saved_rap = dle_r_rap();

	dle_w_rap(LANCE_CSR0);		/* save CSR0 */
	saved_csr0 = dle_r_rdp();

	if (saved_csr0 & LANCE_STOP)
		printf("****** LE CHIP WAS STOPPED ******\n");

	dle_w_rdp(LANCE_STOP);	/* stop chip to access registers */

	/* get the kernel's init block addr */
	saved_initblock_ioaddr = dle_k_init_block();

	/* save the kernel's memory so dle can reuse the memory */
	bcopy(dle_iopb, &mem_save, sizeof (struct dle_memory_image));

	save_romp = romp;
}

/*
 * This is called to restore the state of the le chip for the kernel driver
 * at the time the we are about to go back to the kernel.
 */
void
dle_restore_le()
{
	unsigned int loops;
	int i;
	char *sptr, *dptr;	/* for copy */

	/* the chip is stopped already */

	dle_w_rap(LANCE_CSR1);
	dle_w_rdp(saved_initblock_ioaddr & 0xffff);	/* restore csr1 */

	dle_w_rap(LANCE_CSR2);
	dle_w_rdp((saved_initblock_ioaddr >> 16) & 0xff); /* restore csr2 */

	dle_w_rap(LANCE_CSR3);
	dle_w_rdp(LANCE_BSWP | LANCE_ACON | LANCE_BCON); /* restore csr3 */
	/* restore the kernel's iopb (IO parameter block) */
	bcopy(&mem_save, dle_iopb, sizeof (struct dle_memory_image));

	/* this is where the hard part starts */
	dle_w_rap(LANCE_CSR0);

	/* it looks like we have lost these bits just by stopping the chip */
	/* LANCE_ERR LANCE_BABL LANCE_CERR LANCE_MISS LANCE_MERR */
	/* LANCE_RINT LANCE_TINT LANCE_RXON LANCE_TXON */

	/*
	 * its OK to not worrry about LANCE_INTR since it is only
	 * looked at once and if it is not set when it is expected,
	 * the kernel driver will leinit
	 */

	/* don't worry about LANCE_TDMD */

	dle_w_rdp(LANCE_INIT|LANCE_IDON); /* tell the chip to initialize */

	loops = 0xfffff;
	while ((!(dle_r_rdp() & LANCE_IDON)) && --loops) /* init not done */
		;

	if (loops == 0) {
		printf("dle_restore: chip INIT didn't complete\n");
		return;
	}

	dle_w_rdp(LANCE_IDON); /* clear it */

	dle_w_rdp(LANCE_STRT | LANCE_INEA); /* start it */

	dle_w_rap(saved_rap);	/* restore the rap */
}

/* have the debugger le driver start using the le chip */
int
dle_attach()
{
	unsigned int loops;

	dle_w_rap(LANCE_CSR0);
/*	dle_w_rdp(LANCE_STOP);  /* stop chip, make regs available */

	/* setup the CSRs */

	dle_w_rap(LANCE_CSR1);
	dle_w_rdp((u_short)(dle_dma_addr + DLE_OFFSET(hdr.ib)) & 0xffff);

	dle_w_rap(LANCE_CSR2);
	dle_w_rdp(((unsigned int)dle_dma_addr >> 16) & 0xff);

	dle_w_rap(LANCE_CSR3);
	dle_w_rdp(LANCE_BSWP | LANCE_ACON | LANCE_BCON); /* set csr3 */

	dle_w_rap(LANCE_CSR0);
	dle_w_rdp(0);

	/* copy initialization block, rmds and tmds into place */
	bcopy(&hdr_image, DLE_OFFSET(hdr) + (char *)dle_iopb,
	    sizeof (hdr_image));

	/* give all RMDs to chip */
	dle_flush_incoming();

	/* set the ownership for the TMD */
	dle_iopb->hdr.tmd.lmd_flags = 0;

	dle_w_rdp(LANCE_INIT | LANCE_IDON); /* tell the chip to initialize */

	loops = 0x10000;
	while ((!(dle_r_rdp() & LANCE_IDON)) && --loops) /* init done */
		;
	if (loops == 0) {
		printf("Initialization didn't complete\n");
		return (-1);
	}

	dle_w_rdp(LANCE_IDON); /* clear the bit */

	romp = (union sunromvec *)&my_romvec;

	/* start the le chip */
	dle_w_rdp(LANCE_STRT); /* start it */
}

/* have the debugger le driver stop using the le chip */
dle_detach()
{
	/* stop the chip */
	dle_w_rap(LANCE_CSR0);
	dle_w_rdp(LANCE_STOP);  /* stop chip */
}

/* this throws away any packets received and prepares for more to come */
dle_flush_incoming()
{
	unsigned int rmd;

	for (rmd = 0; rmd < NUMRMDS; rmd++)	/* all RMDs */
		if (!(dle_iopb->hdr.rmd[rmd].lmd_flags & LMD_OWN))
			dle_iopb->hdr.rmd[rmd].lmd_flags = LMD_OWN;
}

#define	RMDINC(RmdNum) (RmdNum = ((RmdNum + 1) % NUMRMDS))
#define	NEXTRMD(RmdNum) ((RmdNum + 1) % NUMRMDS)
#define	PREVRMD(RmdNum) ((RmdNum - 1) % NUMRMDS)
#define	MIN(a, b) (((a) < (b)) ? (a) : (b))

u_int
dle_read(int handle, char *ubuf, unsigned int ubufsize)
{
	static unsigned int lru_rmd = 0;
	register unsigned int strt_rmd;	/* index of first rmd of packet */
	register unsigned int end_rmd;	/* index of last rmd of packet */
	unsigned char flags;	/* tmp flags holder */
	unsigned char found;	/* says I found something */
	unsigned packet_size, bytes_left;

	if (handle != DLE_BOGUS_HANDLE) { /* not for me */
		return ((*obp_read)(handle, ubuf, ubufsize));
	}

start_over:

	/* find the start of a packet */
	strt_rmd = lru_rmd;
	found = 0;
	do {
		flags = dle_iopb->hdr.rmd[strt_rmd].lmd_flags;
		if (flags & LMD_OWN) /* I don't own it, skip it */
			continue;
		/* check for errors first */
		if (flags & LMD_ERR) {
			if (flags & LMD_STP) {
				end_rmd = strt_rmd;
				goto error;
			}
			continue;
		}
		if (flags & LMD_STP) {
			found = 1;
			break;	/* found start of packet */
		}
		/*
		 * we either:
		 *  - started in the middle of a chained packet or
		 *  - we have a hole in the rmds due to a problem in
		 *    managing these buffers
		 * we can tell if it is a whole if the previous rmd
		 * is owned by the le chip.
		 */

		if (dle_iopb->hdr.rmd[PREVRMD(strt_rmd)].lmd_flags
		    & LMD_OWN) { /* yup a hole */
			dle_iopb->hdr.rmd[strt_rmd].lmd_flags = LMD_OWN;
		}
	} while (lru_rmd != RMDINC(strt_rmd));

	if (!found) {	/* No Packets */
		return (0);
	}

	/* fast path */
	if ((dle_iopb->hdr.rmd[strt_rmd].lmd_flags & (LMD_STP | LMD_ENP)) ==
	    (LMD_STP | LMD_ENP)) {
		end_rmd = strt_rmd;
		goto have_start_end;
	}

	/* we have the start of a packet, now find the end */
	end_rmd = NEXTRMD(strt_rmd);	/* because the fast path code above */
	found = 0;
	do {
		flags = dle_iopb->hdr.rmd[end_rmd].lmd_flags;

		/* check for errors first */
		if (flags & (LMD_OWN | LMD_STP)) { /* chip still filling it */
			lru_rmd = end_rmd; /* wait on this one */
			goto start_over;
		}

		if (flags & LMD_ERR) {	/* some sort of error */
			goto error;
		}

		if (flags & LMD_ENP) {	/* valid end */
			found = 1;	/* found end of packet */
			break;
		}

	} while (RMDINC(end_rmd) != strt_rmd);

	if (!found) {	/* No end, no error, something wrong */
		dle_flush_incoming();	/* clean up and give up */
		return (0);
	}

have_start_end:		/* finally we have a start and an end rmd */

	packet_size = dle_iopb->hdr.rmd[end_rmd].lmd_mcnt;
	if (packet_size > ubufsize) { /* too big */
		goto error;  /* toss strt_rmd - end_rmd and keep looking */
	}

	bytes_left = packet_size;
	lru_rmd = strt_rmd;
	do {
		unsigned char *my_bufp;
		unsigned int copy_bytes;

		my_bufp = &(dle_iopb->rbuf[lru_rmd * DLE_INDIV_RBUFSIZE]);
		copy_bytes = MIN(bytes_left, DLE_INDIV_RBUFSIZE);
		bcopy(my_bufp, ubuf, copy_bytes);
		bytes_left -= copy_bytes;
		ubuf += copy_bytes;
		dle_iopb->hdr.rmd[lru_rmd].lmd_flags = LMD_OWN; /* give rmd */
	} while (RMDINC(lru_rmd) != NEXTRMD(end_rmd));

	feedback();		/* show some action */
	return (packet_size);

error:
	/* clear the bad rmds (from strt_rmd to end_rmd) and start over */
	lru_rmd = strt_rmd;
	do {
		dle_iopb->hdr.rmd[lru_rmd].lmd_flags = LMD_OWN;
	} while (RMDINC(lru_rmd) != NEXTRMD(end_rmd));

	/* clear receive flags in CSR0 */
	dle_w_rap(LANCE_CSR0);		/* don't really use these anyway */
	dle_w_rdp(LANCE_BABL|LANCE_CERR|LANCE_MISS|LANCE_RINT);

	goto start_over; /* eliminated bad rmds may still have a good packet */
}

/* write a packet out on the net */
/* this routine is plugged in to the romvec for op2_write */
u_int
dle_write(int handle, char *buf, u_int size)
{
	char *le_bufp;
	unsigned int i;

	if (handle != DLE_BOGUS_HANDLE) { /* not for me */
		return ((*obp_write)(handle, buf, size));
	}

	if (size > DLE_TBUFSIZE)
		return (0);

	size = (size < MINPKTSIZE) ? MINPKTSIZE : size;
	dle_iopb->hdr.tmd.lmd_bcnt = -size;

	/* copy the buffer into the transmission buffer */
	bcopy(buf, dle_iopb->tbuf, size);

	dle_flush_incoming();	/* we only expect responses */

	dle_iopb->hdr.tmd.lmd_mcnt = 0;

	/* mark the ownership of the transmit ring to le */
	dle_iopb->hdr.tmd.lmd_flags = LMD_OWN | LMD_STP | LMD_ENP;

	/* tell the chip to check for transmission NOW */
	dle_w_rap(LANCE_CSR0);
	dle_w_rdp(LANCE_TDMD);  /* kick chip */

	return (size);
}

/*
 * Get the address of the kernel's init block
 * return the address that would be placed in
 * the chip's register.  Not the kernel virtual
 * address.  On some machines these addresses
 * will be the same but not on all.
 */
unsigned int
dle_k_init_block()
{
	/*
	 * you might think to read it out of CSR1 and CSR2
	 * those registers don't promise to report the correct
	 * values after the STOP bit is set and you must set
	 * the STOP bit to read the registers.
	 */
	struct asym *sym;
	static unsigned int addr = 0xffffffff;

	if (addr != 0xffffffff) {	/* we already figured this out */
		return (addr);
	}

	/* get it from the kernel somewhere */
	if ((sym = lookup("dle_k_ib")) == 0) {
		printf("dle_k_init_block: dle_k_ib not found\n");
		return (-1);
	}

	addr = peekl(sym->s_value);
	if (errno == EFAULT) {
		printf("dle_k_init_block: unable to read dle_k_ib value\n");
		return (-1);
	}

	return (addr);
}

void
feedback()
{
	static char fbchars[4] = "-\\|/";
	static char which = 0;

	putchar(fbchars[which++]);
	putchar('\b');
	which &= 0x3;
}

/* pointer to the registers of the le chip */
static struct lanceregs *regs_addr;

/*
 * called when actually asked to start network debugging
 * the is the arch dependent form of init called by dle_init function
 */
int
dle_regs_init()
{
	struct asym *sym;

	/* lookup the addresses of the registers */
	if ((sym = lookup("dle_regs")) == 0) {
		printf("dle_regs_init: dle_regs not found\n");
		return (-1);
	}
	regs_addr = (struct lanceregs *)peekl(sym->s_value);
	if (errno == EFAULT) {
		printf("dle_regs_init: unable to read dle_regs value\n");
		return (-1);
	}
}

/*
 * this routine returns the virtual address of the memory to be used
 * as the io parameter block for the dle driver and the le chip.   The
 * kernel sets aside this area as a place for the le chip to do DMA.
 * the address is acquired by the kernel and given to this code via a
 * global variable "dle_virt_addr" in the kernel.
 * Return values:	Failure 0xffffffff
 *			Success virtual address of memory
 */
unsigned int
dle_get_vmem(int size)
{
	unsigned int addr, *addrp;
	static unsigned int vaddr = 0xffffffff;
	struct asym *sym;

	if (vaddr != 0xffffffff) {	/* we already figured this out */
		return (vaddr);
	}

	if ((sym = lookup("dle_virt_addr")) == 0) {
		printf("dle_get_vmem: dle_virt_addr not found\n");
		return (-1);
	}

	vaddr = peekl(sym->s_value);
	if (errno == EFAULT) {
		printf("dle_get_vmem: unable to read dle_virt_addr value\n");
		return (-1);
	}

	return (vaddr);
}

/*
 * this routine returns the address to be given to the le chip
 * for the memory for its DMA access.
 * Return values 	Failure	-1
 *			Success	DMA address of dle memory
 */
unsigned int
dle_get_daddr()
{
	unsigned int addr, *addrp;
	static unsigned int daddr = 0xffffffff;
	struct asym *sym;

	if (daddr != 0xffffffff) {	/* we already figured this out */
		return (daddr);
	}

	if ((sym = lookup("dle_dma_addr")) == 0) {
		printf("dle_get_daddr: dle_dma_addr not found\n");
		return (-1);
	}

	daddr = peekl(sym->s_value);
	if (errno == EFAULT) {
		printf("dle_get_daddr: unable to read dle_dma_addr value\n");
		return (-1);
	}

	return (daddr);
}

/*
 * since the sun4c doesn't have a MMU bypass mode we need cooperation
 * from the kernel to map in the le registers before the driver does
 * and to keep those mappings for later entrances into the debugger.
 * The following routines use that mapping to access the registers.
 * If the MMU had a bypass mode, the registers could be accessed by
 * physical address and the kernel would not have to be concerned.
 */

/*
 * Write RAP (Register address pointer)
 */
void
dle_w_rap(short value)
{
	regs_addr->lance_rap = value;
}

/*
 * Read RAP (Register address pointer)
 */
u_short
dle_r_rap()
{
	u_short val = regs_addr->lance_rap;
	return (val);
}

/*
 * Write RDP (Register data pointer)
 */
void
dle_w_rdp(short value)
{
	regs_addr->lance_rdp = value;
}

/*
 * Read RDP (Register data pointer)
 */
u_short
dle_r_rdp()
{
	u_short val = regs_addr->lance_rdp;

	return (val);
}

/*
 * Get the ethernet address
 * returns 0 on success and -1 on failure
 */
int
dle_getmacaddr(char *ea)
{
	int i;
	register char *f;
	register char *t;
	idprom_t idprom;

	/*
	 * Extract it from the root node idprom property
	 * (or from the idprom in eeprom on sunmon)
	 */
	if (prom_getidprom((caddr_t) &idprom, sizeof (idprom)) == 0) {
		f = (char *) idprom.id_ether;
		t = ea;

		for (i = 0; i < sizeof (idprom.id_ether); ++i)
			*t++ = *f++;

		return (0);
	}
	return (-1); /* our world must be starting to explode */
}
