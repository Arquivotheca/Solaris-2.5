/*
 * Copyright (c) 1994, 1995 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_PCI_H
#define	_SYS_PCI_H

#pragma ident	"@(#)pci.h	1.11	95/09/22 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * PCI Configuration Header offsets
 */
#define	PCI_CONF_VENID		0x0	/* vendor id, 2 bytes */
#define	PCI_CONF_DEVID		0x2	/* device id, 2 bytes */
#define	PCI_CONF_COMM		0x4	/* command register, 2 bytes */
#define	PCI_CONF_STAT		0x6	/* status register, 2 bytes */
#define	PCI_CONF_REVID		0x8	/* revision id, 1 byte */
#define	PCI_CONF_PROGCLASS	0x9	/* programming class code, 1 byte */
#define	PCI_CONF_SUBCLASS	0xA	/* sub-class code, 1 byte */
#define	PCI_CONF_BASCLASS	0xB	/* basic class code, 1 byte */
#define	PCI_CONF_CACHE_LINESZ	0xC	/* cache line size, 1 byte */
#define	PCI_CONF_LATENCY_TIMER	0xD	/* latency timer, 1 byte */
#define	PCI_CONF_HEADER		0xE	/* header type, 1 byte */
#define	PCI_CONF_BIST		0xF	/* builtin self test, 1 byte */
#define	PCI_CONF_BASE0		0x10	/* base register 0, 4 bytes */
#define	PCI_CONF_BASE1		0x14	/* base register 1, 4 bytes */
#define	PCI_CONF_BASE2		0x18	/* base register 2, 4 bytes */
#define	PCI_CONF_BASE3		0x1c	/* base register 3, 4 bytes */
#define	PCI_CONF_BASE4		0x20	/* base register 4, 4 bytes */
#define	PCI_CONF_BASE5		0x24	/* base register 5, 4 bytes */
#define	PCI_CONF_CIS		0x28	/* Cardbus CIS Pointer */
#define	PCI_CONF_SUBVENID	0x2c	/* Subsystem Vendor ID */
#define	PCI_CONF_SUBSYSID	0x2e	/* Subsystem ID */
#define	PCI_CONF_ROM		0x30	/* ROM base register, 4 bytes */
#define	PCI_CONF_ILINE		0x3c	/* interrupt line, 1 byte */
#define	PCI_CONF_IPIN		0x3d	/* interrupt pin, 1 byte */
#define	PCI_CONF_MIN_G		0x3e	/* minimum grant, 1 byte */
#define	PCI_CONF_MAX_L		0x3f	/* maximum grant, 1 byte */

/*
 * PCI to PCI bridge configuration space header format
 */
#define	PCI_BCNF_PRIBUS		0x18	/* primary bus number */
#define	PCI_BCNF_SECBUS		0x19	/* secondary bus number */
#define	PCI_BCNF_SUBBUS		0x1a	/* subordinate bus number */

/*
 * PCI command register bits
 */
#define	PCI_COMM_IO		0x0001   /* I/O access enable */
#define	PCI_COMM_MAE		0x0002   /* memory access enable */
#define	PCI_COMM_ME		0x0004   /* master enable */
#define	PCI_COMM_SPEC_CYC	0x0008
#define	PCI_COMM_MEMWR_INVAL	0x0010
#define	PCI_COMM_PALETTE_SNOOP	0x0020
#define	PCI_COMM_PARITY_DETECT	0x0040
#define	PCI_COMM_WAIT_CYC_ENAB	0x0080
#define	PCI_COMM_SERR_ENABLE	0x0100
#define	PCI_COMM_BACK2BACK_ENAB	0x0200

/*
 * PCI Interrupt pin value
 */
#define	PCI_INTA	1
#define	PCI_INTB	2
#define	PCI_INTC	3
#define	PCI_INTD	4

/*
 * PCI status register bits
 */
#define	PCI_STAT_66MHZ		0x0020   /* 66 MHz capable */
#define	PCI_STAT_UDF		0x0040   /* UDF supported */
#define	PCI_STAT_FBBC		0x0080   /* Fast Back-to-Back Capable */
#define	PCI_STAT_S_PERROR	0x0100   /* Data Parity Reported */
#define	PCI_STAT_DEVSELT	0x0600   /* Device select timing */
#define	PCI_STAT_S_TARG_AB	0x0800   /* Signaled Target Abort */
#define	PCI_STAT_R_TARG_AB	0x1000   /* Received Target Abort */
#define	PCI_STAT_R_MAST_AB	0x2000   /* Received Master Abort */
#define	PCI_STAT_S_SYSERR	0x4000   /* Signaled System Error */
#define	PCI_STAT_PERROR		0x8000   /* Detected Parity Error */

/*
 * PCI class codes
 */
#define	PCI_CLASS_NONE		0x0	/* class code for pre-2.0 devices */
#define	PCI_CLASS_MASS		0x1	/* Mass storage Controller class */
#define	PCI_CLASS_NET		0x2	/* Network Controller class */
#define	PCI_CLASS_DISPLAY	0x3	/* Display Controller class */
#define	PCI_CLASS_MM		0x4	/* Multimedia Controller class */
#define	PCI_CLASS_MEM		0x5	/* Memory Controller class */
#define	PCI_CLASS_BRIDGE	0x6	/* Bridge Controller class */
#define	PCI_CLASS_COMM		0x7	/* Communications Controller class */
#define	PCI_CLASS_PERIPH	0x8	/* Peripheral Controller class */
#define	PCI_CLASS_INPUT		0x9	/* Input Device class */
#define	PCI_CLASS_DOCK		0xa	/* Docking Station class */
#define	PCI_CLASS_PROCESSOR	0xb	/* Processor class */
#define	PCI_CLASS_SERIALBUS	0xc	/* Serial Bus class */

/*
 * PCI Sub-class codes - base class 0x0 (no new devices should use this code).
 */
#define	PCI_NONE_NOTVGA		0x0	/* All devices except VGA compatible */
#define	PCI_NONE_VGA		0x1	/* VGA compatible */

/*
 * PCI Sub-class codes - base class 0x1
 */
#define	PCI_MASS_SCSI		0x0	/* SCSI bus Controller */
#define	PCI_MASS_IDE		0x1	/* IDE Controller */
#define	PCI_MASS_FD		0x2	/* floppy disk Controller */
#define	PCI_MASS_IPI		0x3	/* IPI bus Controller */
#define	PCI_MASS_RAID		0x4	/* RAID Controller */
#define	PCI_MASS_OTHER		0x80	/* Other Mass Storage Controller */

/*
 * programming interface for IDE
 */
#define	PCI_IDE_IF_NATIVE_PRI	0x01	/* primary channel is native */
#define	PCI_IDE_IF_PROG_PRI	0x02	/* primary can operate in either mode */
#define	PCI_IDE_IF_NATIVE_SEC	0x04	/* secondary channel is native */
#define	PCI_IDE_IF_PROG_SEC	0x08	/* sec. can operate in either mode */
#define	PCI_IDE_IF_MASK		0x0f	/* programming interface mask */

/*
 * PCI Sub-class codes - base class 0x2
 */
#define	PCI_NET_ENET		0x0	/* Ethernet Controller */
#define	PCI_NET_TOKEN		0x1	/* Token Ring Controller */
#define	PCI_NET_FDDI		0x2	/* FDDI Controller */
#define	PCI_NET_ATM		0x3	/* ATM Controller */
#define	PCI_NET_OTHER		0x80	/* Other Network Controller */

/*
 * PCI Sub-class codes - base class 0x3
 */
#define	PCI_DISPLAY_VGA		0x0   /* VGA device */
#define	PCI_DISPLAY_XGA		0x1   /* XGA device */
#define	PCI_DISPLAY_OTHER	0x80  /* Other Display Device */

/*
 * programming interface for display
 */
#define	PCI_DISPLAY_IF_VGA	0x0b	/* VGA compatible */
#define	PCI_DISPLAY_IF_8514	0x1b	/* 8514 compatible */

/*
 * PCI Sub-class codes - base class 0x4
 */
#define	PCI_MM_VIDEO		0x0   /* Video device */
#define	PCI_MM_AUDIO		0x1   /* Audio device */
#define	PCI_MM_OTHER		0x80  /* Other Multimedia Device */

/*
 * PCI Sub-class codes - base class 0x5
 */
#define	PCI_MEM_RAM		0x0   /* RAM device */
#define	PCI_MEM_FLASH		0x1   /* FLASH device */
#define	PCI_MEM_OTHER		0x80  /* Other Memory Controller */

/*
 * PCI Sub-class codes - base class 0x6
 */
#define	PCI_BRIDGE_HOST		0x0   /* Host/PCI Bridge */
#define	PCI_BRIDGE_ISA		0x1   /* PCI/ISA Bridge */
#define	PCI_BRIDGE_EISA		0x2   /* PCI/EISA Bridge */
#define	PCI_BRIDGE_MC		0x3   /* PCI/MC Bridge */
#define	PCI_BRIDGE_PCI		0x4   /* PCI/PCI Bridge */
#define	PCI_BRIDGE_PCMCIA	0x5   /* PCI/PCMCIA Bridge */
#define	PCI_BRIDGE_NUBUS	0x6   /* PCI/NUBUS Bridge */
#define	PCI_BRIDGE_CARDBUS	0x7   /* PCI/CARDBUS Bridge */
#define	PCI_BRIDGE_OTHER	0x80  /* PCI/Other Bridge Device */

/*
 * PCI Sub-class codes - base class 0x7
 */
#define	PCI_COMM_GENERIC_XT	0x0   /* XT Compatible Serial Controller */
#define	PCI_COMM_PARALLEL	0x1   /* Parallel Port Controller */
#define	PCI_COMM_OTHER		0x80  /* Other Communications Controller */

/*
 * PCI Sub-class codes - base class 0x8
 */
#define	PCI_PERIPH_PIC		0x0   /* Generic PIC */
#define	PCI_PERIPH_DMA		0x1   /* Generic DMA Controller */
#define	PCI_PERIPH_TIMER	0x2   /* Generic System Timer Controller */
#define	PCI_PERIPH_RTC		0x3   /* Generic RTC Controller */
#define	PCI_PERIPH_OTHER	0x80  /* Other System Peripheral */

/*
 * PCI Sub-class codes - base class 0x9
 */
#define	PCI_INPUT_KEYBOARD	0x0   /* Keyboard Controler */
#define	PCI_INPUT_DIGITIZ	0x1   /* Digitizer (Pen) */
#define	PCI_INPUT_MOUSE		0x2   /* Mouse Controller */
#define	PCI_INPUT_OTHER		0x80  /* Other Input Controller */

/*
 * PCI Sub-class codes - base class 0xa
 */
#define	PCI_DOCK_GENERIC	0x0   /* Generic Docking Station */
#define	PCI_DOCK_OTHER		0x80  /* Other Type of Docking Station */

/*
 * PCI Sub-class codes - base class 0xb
 */
#define	PCI_PROCESSOR_386	0x0   /* 386 */
#define	PCI_PROCESSOR_486	0x1   /* 486 */
#define	PCI_PROCESSOR_PENT	0x2   /* Pentium */
#define	PCI_PROCESSOR_ALPHA	0x10  /* Alpha */
#define	PCI_PROCESSOR_COPROC	0x40  /* Co-processor */

/*
 * PCI Sub-class codes - base class 0xc
 */
#define	PCI_SERIAL_FIRE		0x0   /* FireWire (IEEE 1394) */
#define	PCI_SERIAL_ACCESS	0x1   /* ACCESS.bus */
#define	PCI_SERIAL_SSA		0x2   /* SSA */

/* PCI header decode */
#define	PCI_HEADER_MULTI	0x80  /* multi-function device */
#define	PCI_HEADER_ZERO		0x00  /* type zero PCI header */
#define	PCI_HEADER_ONE		0x01  /* type one PCI header */
#define	PCI_HEADER_PPB		PCI_HEADER_ONE  /* type one PCI to PCI Bridge */

#define	PCI_HEADER_TYPE_M	0x3f  /* type mask for header */

/*
 * Base register bit definitions.
 */
#define	PCI_BASE_SPACE_M    0x00000001  /* memory space indicator */
#define	PCI_BASE_SPACE_IO   0x1   /* IO space */
#define	PCI_BASE_SPACE_MEM  0x0   /* memory space */

#define	PCI_BASE_TYPE_MEM   0x0   /* 32-bit memory address */
#define	PCI_BASE_TYPE_LOW   0x2   /* less than 1Mb address */
#define	PCI_BASE_TYPE_ALL   0x4   /* 64-bit memory address */
#define	PCI_BASE_TYPE_RES   0x6   /* reserved */

#define	PCI_BASE_TYPE_M		0x00000006  /* type indicator mask */
#define	PCI_BASE_PREF_M		0x00000008  /* prefetch mask */
#define	PCI_BASE_M_ADDR_M	0xfffffff0  /* memory address mask */
#define	PCI_BASE_IO_ADDR_M	0xfffffffe  /* I/O address mask */

/* XXX need command and status definitions */

/*
 * This structure represents one entry of the 1275 "reg" property and
 * "assigned-addresses" property for a PCI node.  For the "reg" property, it
 * may be one of an arbitrary length array for devices with multiple address
 * windows.  For the "assigned-addresses" property, it denotes an assigned
 * physical address on the PCI bus.  It may be one entry of the six entries
 * for devices with multiple base registers.
 *
 * The physical address format is:
 *
 *             Bit#:  33222222 22221111 11111100 00000000
 *                    10987654 32109876 54321098 76543210
 *
 * pci_phys_hi cell:  np0000tt bbbbbbbb dddddfff rrrrrrrr
 * pci_phys_mid cell: hhhhhhhh hhhhhhhh hhhhhhhh hhhhhhhh
 * pci_phys_low cell: llllllll llllllll llllllll llllllll
 *
 * n          is 0 if the address is relocatable, 1 otherwise
 * p          is 1 if the addressable region is "prefetchable", 0 otherwise
 * t          is 1 if the address range is aliased
 * tt         is the type code, denoting which address space
 * bbbbbbbb   is the 8-bit bus number
 * ddddd      is the 5-bit device number
 * fff        is the 3-bit function number
 * rrrrrrrr   is the 8-bit register number
 * hh...hhh   is the 32-bit unsigned number
 * ll...lll   is the 32-bit unsigned number
 *
 * The physical size format is:
 *
 * pci_size_hi cell:  hhhhhhhh hhhhhhhh hhhhhhhh hhhhhhhh
 * pci_size_low cell: llllllll llllllll llllllll llllllll
 *
 * hh...hhh   is the 32-bit unsigned number
 * ll...lll   is the 32-bit unsigned number
 */
struct pci_phys_spec {
	u_int pci_phys_hi;		/* child's address, hi word */
	u_int pci_phys_mid;		/* child's address, middle word */
	u_int pci_phys_low;		/* child's address, low word */
	u_int pci_size_hi;		/* high word of size field */
	u_int pci_size_low;		/* low word of size field */
};

typedef struct pci_phys_spec pci_regspec_t;

/*
 * PCI masks for pci_phy_hi of PCI 1275 address cell.
 */
#define	PCI_REG_REG_M		0x000000ff  /* register mask */
#define	PCI_REG_FUNC_M		0x00000700  /* function mask */
#define	PCI_REG_DEV_M		0x0000f800  /* device mask */
#define	PCI_REG_BUS_M		0x00ff0000  /* bus number mask */
#define	PCI_REG_ADDR_M		0x03000000  /* address space mask */
#define	PCI_REG_ALIAS_M		0x20000000  /* aliased bit mask */
#define	PCI_REG_PF_M		0x40000000  /* prefetch bit mask */
#define	PCI_REG_REL_M		0x80000000  /* relocation bit mask */

#define	PCI_REG_REG_G(x)    (x & PCI_REG_REG_M)
#define	PCI_REG_FUNC_G(x)   ((x & PCI_REG_FUNC_M) >> 8)
#define	PCI_REG_DEV_G(x)    ((x & PCI_REG_DEV_M) >> 11)
#define	PCI_REG_BUS_G(x)    ((x & PCI_REG_BUS_M) >> 16)
#define	PCI_REG_ADDR_G(x)   ((x & PCI_REG_ADDR_M) >> 24)

/*
 * PCI bit encodings of pci_phy_hi of PCI 1275 address cell.
 */
#define	PCI_ADDR_MASK		0x03000000   /* configuration address */
#define	PCI_ADDR_CONFIG		0x00000000   /* configuration address */
#define	PCI_ADDR_IO		0x01000000   /* I/O address */
#define	PCI_ADDR_MEM32		0x02000000   /* 32-bit memory address */
#define	PCI_ADDR_MEM64		0x03000000   /* 64-bit memory address */
#define	PCI_ALIAS_B		0x20000000   /* aliased bit */
#define	PCI_PREFETCH_B		0x40000000   /* prefetch bit */
#define	PCI_RELOCAT_B		0x80000000   /* relocatable bit */
#define	PCI_CONF_ADDR_MASK	0x00ffffff   /* mask for config address */

/*
 * other interesting PCI constants
 */
#define	PCI_BASE_NUM	6	/* num of base regs in configuration header */
#define	PCI_BASE_SIZE	4	/* size of base reg in bytes */
#define	PCI_CONF_HDR_SIZE	256	/* configuration header size */

#define	PCI_HARDDEC_8514 2	/* number of reg entries for 8514 hard-decode */
#define	PCI_HARDDEC_VGA	3	/* number of reg entries for VGA hard-decode */
#define	PCI_HARDDEC_IDE	4	/* number of reg entries for IDE hard-decode */
#define	PCI_HARDDEC_IDE_PRI 2	/* number of reg entries for IDE primary */
#define	PCI_HARDDEC_IDE_SEC 2	/* number of reg entries for IDE secondary */

/*
 * PCI to PCI bus bridge
 */
#define	PCI_PPB_ROM		0x38	/* expansion ROM base address */
#define	PCI_PPB_BASE_NUM	0x2 	/* number of base registers */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PCI_H */
