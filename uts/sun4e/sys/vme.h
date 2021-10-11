/*	@(#)vme.h 1.7	90/07/23 SMI; SunOS 1E	*/

#ifndef	_SYS_VME_H
#define	_SYS_VME_H

#pragma ident	"@(#)vme.h	1.6	93/11/01 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * VME Interface Registers
 */

#define	OBIO_VME_ADDR	0xEFE00000	/* physical address of VME registers */

#define	VME_ADDR	0xFFFF2000	/* virtual address of VME registers */

/*
 * VME interface register offsets
 */
#define	LOCKER_OFFSET		0x00

#define	IACK1_OFFSET		0x03
#define	IACK2_OFFSET		0x05
#define	IACK3_OFFSET		0x07
#define	IACK4_OFFSET		0x09
#define	IACK5_OFFSET		0x0B
#define	IACK6_OFFSET		0x0D
#define	IACK7_OFFSET		0x0F

#define	MBOX_OFFSET		0x10	/* mailbox register */
#define	INTENABLE_OFFSET	0x14	/* interrupt enable register */
#define	A32MAP_OFFSET		0x18
#define	SLAVEMAP_OFFSET		0x1C

#define	VME_LOCKER	(VME_ADDR + LOCKER_OFFSET)

#define	VME_IACK1	(VME_ADDR + IACK1_OFFSET)
#define	VME_IACK2	(VME_ADDR + IACK2_OFFSET)
#define	VME_IACK3	(VME_ADDR + IACK3_OFFSET)
#define	VME_IACK4	(VME_ADDR + IACK4_OFFSET)
#define	VME_IACK5	(VME_ADDR + IACK5_OFFSET)
#define	VME_IACK6	(VME_ADDR + IACK6_OFFSET)
#define	VME_IACK7	(VME_ADDR + IACK7_OFFSET)

#define	VME_MBOX	(VME_ADDR + MBOX_OFFSET)
#define	VME_INTENABLE	(VME_ADDR + INTENABLE_OFFSET)
#define	VME_A32MAP	(VME_ADDR + A32MAP_OFFSET)
#define	VME_SLAVEMAP	(VME_ADDR + SLAVEMAP_OFFSET)


/*
 * Flags/masks for VME registers
 */

/*
 * Bus Locker flags/masks.
 */

#define	VME_LOCKENABLE	0x01	/* Enable Bus Locker Capability */
#define	VME_BUSREQ	0x02	/* Request VME Bus */
#define	VME_OWNED	0x80	/* Flag: 1 = VME Bus is owned */

/*
 * Mailbox register flags/masks.
 */

#define	VME_MBOXINTPEND	0x80	/* Mailbox interrupt is pending */
#define	VME_MBOXENABLE	0x40	/* Mailbox interrupt enable */

/*
 * Interrupt Handler flags/masks.
 */

#define	VME_ROUNDROBIN	0x01	/* Flag: */
				/*	1 = Round Robin Arbiter; */
				/*	0 = Single Level */
#define	VME_ENABIRQ1	0x02	/* Enable VME Interrupt 1 */
#define	VME_ENABIRQ2	0x04	/* Enable VME Interrupt 2 */
#define	VME_ENABIRQ3	0x08	/* Enable VME Interrupt 3 */
#define	VME_ENABIRQ4	0x10	/* Enable VME Interrupt 4 */
#define	VME_ENABIRQ5	0x20	/* Enable VME Interrupt 5 */
#define	VME_ENABIRQ6	0x40	/* Enable VME Interrupt 6 */
#define	VME_ENABIRQ7	0x80	/* Enable VME Interrupt 7 */

#define	VME_IRQENABLE_BITS	"\20\10IRQ7\7IRQ6\6IRQ5\5IRQ4\4IRQ3\3IRQ2\2IRQ1"

/*
 * A32 Map register flags/masks.
 */

#define	VME_LOOPB	0x01	/* VME loopback mode */

/*
 * Slavemap register flags/masks.
 */

#define	VME_BLOCKMODE	0x80	/* mask to enable/disable block mode xfers */
#define	VME_SDVMAMASK	0x0F	/* mask for SDVMA window base address */

#ifndef	_ASM

typedef struct vme_interface
{
	unsigned char	vme_locker;		/* r/w */
	unsigned char	pad1;
	unsigned char	pad2;
	unsigned char	vme_iack1;		/* r/o */
	unsigned char	pad3;
	unsigned char	vme_iack2;		/* r/o */
	unsigned char	pad4;
	unsigned char	vme_iack3;		/* r/o */
	unsigned char	pad5;
	unsigned char	vme_iack4;		/* r/o */
	unsigned char	pad6;
	unsigned char	vme_iack5;		/* r/o */
	unsigned char	pad7;
	unsigned char	vme_iack6;		/* r/o */
	unsigned char	pad8;
	unsigned char	vme_iack7;		/* r/o */
	unsigned char	vme_mbox;		/* r/w */
	unsigned char	pad9;
	unsigned char	pad10;
	unsigned char	pad11;
	unsigned char	vme_intenable;		/* r/w */
	unsigned char	pad12;
	unsigned char	pad13;
	unsigned char	pad14;
	unsigned char	vme_a32map;		/* r/w */
	unsigned char	pad15;
	unsigned char	pad16;
	unsigned char	pad17;
	unsigned char	vme_slavemap;		/* r/w */
} VME_INTERFACE;

#define	vme_registers		((VME_INTERFACE *)VME_ADDR)


#ifdef	_KERNEL
extern void	vme_init();
extern void	vme_dvma_probe();
extern void	vme_print();
extern void	vme_diagalloc();

extern int	vme_mbox_intcnt;
#endif	/* _KERNEL */

#endif	/* _ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* !_SYS_VME_H */
