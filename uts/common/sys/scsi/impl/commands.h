/*
 * Copyright (c) 1989-1991, by Sun Microsystems, Inc.
 */

#ifndef	_SYS_SCSI_IMPL_COMMANDS_H
#define	_SYS_SCSI_IMPL_COMMANDS_H

#pragma ident	"@(#)commands.h	1.18	94/02/01 SMI"


#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Implementation dependent (i.e., Vendor Unique) command definitions.
 * This file is included by <sys/scsi/generic/commands.h>
 */

/*
 * Implementation dependent view of a SCSI command descriptor block
 */

/*
 * Standard SCSI control blocks definitions.
 *
 * These go in or out over the SCSI bus.
 *
 * The first 11 bits of the command block are the same for all three
 * defined command groups.  The first byte is an operation which consists
 * of a command code component and a group code component. The first 3 bits
 * of the second byte are the unit number.
 *
 * The group code determines the length of the rest of the command.
 * Group 0 commands are 6 bytes, Group 1 are 10 bytes, and Group 5
 * are 12 bytes.  Groups 2-4 are Reserved. Groups 6 and 7 are Vendor
 * Unique.
 *
 */

/*
 * At present, our standard cdb's will reserve enough space for
 * use with up to Group 5 commands. This may have to change soon
 * if optical disks have 20 byte or longer commands. At any rate,
 * the Sun SCSI implementation has no problem handling arbitrary
 * length commands; it is just more efficient to declare it as
 * certain size (to avoid runtime allocation overhead).
 */

#define	CDB_SIZE	CDB_GROUP5


union scsi_cdb {		/* scsi command description block */
	struct {
		u_char	cmd;		/* cmd code (byte 0) */
#if defined(_BIT_FIELDS_LTOH)
		u_char tag	:5;	/* rest of byte 1 */
		u_char lun	:3;	/* lun (byte 1) */
#elif defined(_BIT_FIELDS_HTOL)
		u_char	lun	:3,	/* lun (byte 1) */
			tag	:5;	/* rest of byte 1 */
#else
#error	One of _BIT_FIELDS_LTOH or _BIT_FIELDS_HTOL must be defined
#endif	/* _BIT_FIELDS_LTOH */
		union {			/* bytes 2 - 31 */

		u_char	scsi[CDB_SIZE-2];
		/*
		 *	G R O U P   0   F O R M A T (6 bytes)
		 */
#define		scc_cmd		cdb_un.cmd
#define		scc_lun		cdb_un.lun
#define		g0_addr2	cdb_un.tag
#define		g0_addr1	cdb_un.sg.g0.addr1
#define		g0_addr0	cdb_un.sg.g0.addr0
#define		g0_count0	cdb_un.sg.g0.count0
#define		g0_vu_1		cdb_un.sg.g0.vu_57
#define		g0_vu_0		cdb_un.sg.g0.vu_56
#define		g0_flag		cdb_un.sg.g0.flag
#define		g0_link		cdb_un.sg.g0.link
	/*
	 * defines for SCSI tape cdb.
	 */
#define		t_code		cdb_un.tag
#define		high_count	cdb_un.sg.g0.addr1
#define		mid_count	cdb_un.sg.g0.addr0
#define		low_count	cdb_un.sg.g0.count0
		struct scsi_g0 {
			u_char addr1;	/* middle part of address */
			u_char addr0;	/* low part of address */
			u_char count0;	/* usually block count */
#if defined(_BIT_FIELDS_LTOH)
			u_char link	:1; /* another command follows 	*/
			u_char flag	:1; /* interrupt when done 	*/
			u_char rsvd	:4; /* reserved 		*/
			u_char vu_56	:1; /* vendor unique (byte5 bit6) */
			u_char vu_57	:1; /* vendor unique (byte5 bit7) */
#elif defined(_BIT_FIELDS_HTOL)
			u_char vu_57	:1; /* vendor unique (byte 5 bit 7) */
			u_char vu_56	:1; /* vendor unique (byte 5 bit 6) */
			u_char rsvd	:4; /* reserved */
			u_char flag	:1; /* interrupt when done */
			u_char link	:1; /* another command follows */
#else
#error	One of _BIT_FIELDS_LTOH or _BIT_FIELDS_HTOL must be defined
#endif	/* _BIT_FIELDS_LTOH */
		} g0;


		/*
		 *	G R O U P   1   F O R M A T  (10 byte)
		 */
#define		g1_reladdr	cdb_un.tag
#define		g1_rsvd0	cdb_un.sg.g1.rsvd1
#define		g1_addr3	cdb_un.sg.g1.addr3	/* msb */
#define		g1_addr2	cdb_un.sg.g1.addr2
#define		g1_addr1	cdb_un.sg.g1.addr1
#define		g1_addr0	cdb_un.sg.g1.addr0	/* lsb */
#define		g1_count1	cdb_un.sg.g1.count1	/* msb */
#define		g1_count0	cdb_un.sg.g1.count0	/* lsb */
#define		g1_vu_1		cdb_un.sg.g1.vu_97
#define		g1_vu_0		cdb_un.sg.g1.vu_96
#define		g1_flag		cdb_un.sg.g1.flag
#define		g1_link		cdb_un.sg.g1.link
		struct scsi_g1 {
			u_char addr3;	/* most sig. byte of address */
			u_char addr2;
			u_char addr1;
			u_char addr0;
			u_char rsvd1;	/* reserved (byte 6) */
			u_char count1;	/* transfer length (msb) */
			u_char count0;	/* transfer length (lsb) */
#if defined(_BIT_FIELDS_LTOH)
			u_char link	:1; /* another command follows 	*/
			u_char flag	:1; /* interrupt when done 	*/
			u_char rsvd0	:4; /* reserved 		*/
			u_char vu_96	:1; /* vendor unique (byte9 bit6) */
			u_char vu_97	:1; /* vendor unique (byte9 bit7) */
#elif defined(_BIT_FIELDS_HTOL)
			u_char vu_97	:1; /* vendor unique (byte 9 bit 7) */
			u_char vu_96	:1; /* vendor unique (byte 9 bit 6) */
			u_char rsvd0	:4; /* reserved */
			u_char flag	:1; /* interrupt when done */
			u_char link	:1; /* another command follows */
#else
#error	One of _BIT_FIELDS_LTOH or _BIT_FIELDS_HTOL must be defined
#endif	/* _BIT_FIELDS_LTOH */
		} g1;


		/*
		 *	G R O U P   5   F O R M A T  (12 byte)
		 */
#define		scc5_reladdr	cdb_un.tag
#define		scc5_addr3	cdb_un.sg.g5.addr3	/* msb */
#define		scc5_addr2	cdb_un.sg.g5.addr2
#define		scc5_addr1	cdb_un.sg.g5.addr1
#define		scc5_addr0	cdb_un.sg.g5.addr0	/* lsb */
#define		scc5_count3	cdb_un.sg.g5.count3	/* msb */
#define		scc5_count2	cdb_un.sg.g5.count2
#define		scc5_count1	cdb_un.sg.g5.count1
#define		scc5_count0	cdb_un.sg.g5.count0	/* lsb */
#define		scc5_vu_1	cdb_un.sg.g5.v117
#define		scc5_vu_0	cdb_un.sg.g5.v116
#define		scc5_flag	cdb_un.sg.g5.flag
		struct scsi_g5 {
			u_char addr3;	/* most sig. byte of address */
			u_char addr2;
			u_char addr1;
			u_char addr0;
			u_char count3;	/* most sig. byte of count */
			u_char count2;
			u_char count1;
			u_char count0;
			u_char rsvd1;	/* reserved */
#if defined(_BIT_FIELDS_LTOH)
			u_char link	:1; /* another command follows 	*/
			u_char flag	:1; /* interrupt when done 	*/
			u_char rsvd0	:4; /* reserved 		*/
			u_char vu_116	:1; /* vendor unique (byte11,bit6) */
			u_char vu_117	:1; /* vendor unique (byte11,bit7) */
#elif defined(_BIT_FIELDS_HTOL)
			u_char vu_117	:1; /* vendor unique (byte 11 bit 7) */
			u_char vu_116	:1; /* vendor unique (byte 11 bit 6) */
			u_char rsvd0	:4; /* reserved */
			u_char flag	:1; /* interrupt when done */
			u_char link	:1; /* another command follows */
#else
#error	One of _BIT_FIELDS_LTOH or _BIT_FIELDS_HTOL must be defined
#endif	/* _BIT_FIELDS_LTOH */
		} g5;
		}sg;
	} cdb_un;
	u_char cdb_opaque[CDB_SIZE];	/* addressed as an opaque char array */
	u_long cdb_long[CDB_SIZE / sizeof (long)]; /* as a longword array */
};


/*
 *	Various useful Macros for SCSI commands
 */

/*
 * defines for getting/setting fields within the various command groups
 */

#define	GETCMD(cdb)		((cdb)->scc_cmd & 0x1F)
#define	GETGROUP(cdb)		(CDB_GROUPID((cdb)->scc_cmd))

#define	FORMG0COUNT(cdb, cnt)	(cdb)->g0_count0  = (cnt)

#define	FORMG0ADDR(cdb, addr) 	(cdb)->g0_addr2  = (addr) >> 16; \
				(cdb)->g0_addr1  = ((addr) >> 8) & 0xFF; \
				(cdb)->g0_addr0  = (addr) & 0xFF

#define	GETG0ADDR(cdb)		(((cdb)->g0_addr2 & 0x1F) << 16) + \
				((cdb)->g0_addr1 << 8) + ((cdb)->g0_addr0)

#define	GETG0TAG(cdb)		((cdb)->g0_addr2)

#define	FORMG0COUNT_S(cdb, cnt)	(cdb)->high_count  = (cnt) >> 16; \
				(cdb)->mid_count = ((cnt) >> 8) & 0xFF; \
				(cdb)->low_count = (cnt) & 0xFF

#define	FORMG1COUNT(cdb, cnt)	(cdb)->g1_count1 = ((cnt) >> 8); \
				(cdb)->g1_count0 = (cnt) & 0xFF

#define	FORMG1ADDR(cdb, addr)	(cdb)->g1_addr3  = (addr) >> 24; \
				(cdb)->g1_addr2  = ((addr) >> 16) & 0xFF; \
				(cdb)->g1_addr1  = ((addr) >> 8) & 0xFF; \
				(cdb)->g1_addr0  = (addr) & 0xFF

#define	GETG1ADDR(cdb)		((cdb)->g1_addr3 << 24) + \
				((cdb)->g1_addr2 << 16) + \
				((cdb)->g1_addr1 << 8)  + \
				((cdb)->g1_addr0)

#define	GETG1TAG(cdb)		(cdb)->g1_reladdr

#define	FORMG5COUNT(cdb, cnt)	(cdb)->scc5_count3 = ((cnt) >> 24); \
				(cdb)->scc5_count2 = ((cnt) >> 16); \
				(cdb)->scc5_count1 = ((cnt) >> 8); \
				(cdb)->scc5_count0 = (cnt) & 0xFF

#define	FORMG5ADDR(cdb, addr)	(cdb)->scc5_addr3  = (addr) >> 24; \
				(cdb)->scc5_addr2  = ((addr) >> 16) & 0xFF; \
				(cdb)->scc5_addr1  = ((addr) >> 8) & 0xFF; \
				(cdb)->scc5_addr0  = (addr) & 0xFF

#define	GETG5ADDR(cdb)		((cdb)->scc5_addr3 << 24) + \
				((cdb)->scc5_addr2 << 16) + \
				((cdb)->scc5_addr1 << 8)  + \
				((cdb)->scc5_addr0)

#define	GETG5TAG(cdb)		(cdb)->scc5_reladdr


/*
 * Shorthand macros for forming commands
 */

#define	MAKECOM_COMMON(pktp, devp, flag, cmd)	\
	(pktp)->pkt_address = (devp)->sd_address, \
	(pktp)->pkt_flags = (flag), \
	((union scsi_cdb *)(pktp)->pkt_cdbp)->scc_cmd = (cmd), \
	((union scsi_cdb *)(pktp)->pkt_cdbp)->scc_lun = \
	    (pktp)->pkt_address.a_lun

#define	MAKECOM_G0(pktp, devp, flag, cmd, addr, cnt)	\
	MAKECOM_COMMON((pktp), (devp), (flag), (cmd)), \
	FORMG0ADDR(((union scsi_cdb *)(pktp)->pkt_cdbp), (addr)), \
	FORMG0COUNT(((union scsi_cdb *)(pktp)->pkt_cdbp), (cnt))

#define	MAKECOM_G0_S(pktp, devp, flag, cmd, cnt, fixbit)	\
	MAKECOM_COMMON((pktp), (devp), (flag), (cmd)), \
	FORMG0COUNT_S(((union scsi_cdb *)(pktp)->pkt_cdbp), (cnt)), \
	((union scsi_cdb *)(pktp)->pkt_cdbp)->t_code = (fixbit)

#define	MAKECOM_G1(pktp, devp, flag, cmd, addr, cnt)	\
	MAKECOM_COMMON((pktp), (devp), (flag), (cmd)), \
	FORMG1ADDR(((union scsi_cdb *)(pktp)->pkt_cdbp), (addr)), \
	FORMG1COUNT(((union scsi_cdb *)(pktp)->pkt_cdbp), (cnt))

#define	MAKECOM_G5(pktp, devp, flag, cmd, addr, cnt)	\
	MAKECOM_COMMON((pktp), (devp), (flag), (cmd)), \
	FORMG5ADDR(((union scsi_cdb *)(pktp)->pkt_cdbp), (addr)), \
	FORMG5COUNT(((union scsi_cdb *)(pktp)->pkt_cdbp), (cnt))


/*
 * Direct access disk format defines and parameters.
 *
 * This is still pretty ugly and is mostly derived
 * from Emulex MD21 specific formatting.
 */

#define	fmt_parm_bits		g0_addr2	/* for format options */
#define	fmt_interleave		g0_count0	/* for encode interleave */
#define	defect_list_descrip	g1_addr3	/* list description bits */

/*
 * defines for value of fmt_parm_bits.
 */

#define	FPB_BFI			0x04	/* bytes-from-index fmt */
#define	FPB_CMPLT		0x08	/* full defect list provided */
#define	FPB_DATA		0x10	/* defect list data provided */

/*
 * Defines for value of defect_list_descrip.
 */

#define	DLD_MAN_DEF_LIST	0x10	/* manufacturer's defect list */
#define	DLD_GROWN_DEF_LIST	0x08	/* grown defect list */
#define	DLD_BLOCK_FORMAT	0x00	/* block format */
#define	DLD_BFI_FORMAT		0x04	/* bytes-from-index format */
#define	DLD_PS_FORMAT		0x05	/* physical sector format */


/*
 * Disk defect list - used by format command.
 */
#define	RDEF_ALL	0	/* read all defects */
#define	RDEF_MANUF	1	/* read manufacturer's defects */
#define	RDEF_CKLEN	2	/* check length of manufacturer's list */
#define	ST506_NDEFECT	127	/* must fit in 1K controller buffer... */
#define	ESDI_NDEFECT	ST506_NDEFECT

struct scsi_bfi_defect {	/* defect in bytes from index format */
#if defined(_BIT_FIELDS_LTOH)
	unsigned head : 8;
	unsigned cyl  : 24;
#elif defined(_BIT_FIELDS_HTOL)
	unsigned cyl  : 24;
	unsigned head : 8;
#else
#error	One of _BIT_FIELDS_LTOH or _BIT_FIELDS_HTOL must be defined
#endif	/* _BIT_FIELDS_LTOH */
	long	bytes_from_index;
};

struct scsi_format_params {	/* BFI format list */
	u_short reserved;
	u_short length;
	struct  scsi_bfi_defect list[ESDI_NDEFECT];
};

/*
 * Defect list returned by READ_DEFECT_LIST command.
 */
struct scsi_defect_hdr {	/* For getting defect list size */
	u_char	reserved;
	u_char	descriptor;
	u_short	length;
};

struct scsi_defect_list {	/* BFI format list */
	u_char	reserved;
	u_char	descriptor;
	u_short	length;
	struct	scsi_bfi_defect list[ESDI_NDEFECT];
};

/*
 *
 * Direct Access device Reassign Block parameter
 *
 * Defect list format used by reassign block command (logical block format).
 *
 * This defect list is limited to 1 defect, as that is the only way we use it.
 *
 */

struct scsi_reassign_blk {
	u_short	reserved;
	u_short length;		/* defect length in bytes (defects * 4) */
	u_int 	defect;		/* Logical block address of defect */
};

/*
 * Direct Access Device Capacity Structure
 */

struct scsi_capacity {
	u_long	capacity;
	u_long	lbasize;
};

#ifdef	_KERNEL

/*
 * Functional versions of the above macros.
 */

#ifdef  __STDC__
extern void 	makecom_g0(struct scsi_pkt *pkt, struct scsi_device *devp,
				int flag, int cmd, int addr, int cnt);
extern void 	makecom_g0_s(struct scsi_pkt *pkt, struct scsi_device *devp,
				int flag, int cmd, int cnt, int fixbit);
extern void 	makecom_g1(struct scsi_pkt *pkt, struct scsi_device *devp,
				int flag, int cmd, int addr, int cnt);
extern void 	makecom_g5(struct scsi_pkt *pkt, struct scsi_device *devp,
				int flag, int cmd, int addr, int cnt);

#else   /* __STDC__ */

extern void 	makecom_g0();
extern void 	makecom_g0_s();
extern void 	makecom_g1();
extern void 	makecom_g5();

#endif  /* __STDC__ */

#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_IMPL_COMMANDS_H */
