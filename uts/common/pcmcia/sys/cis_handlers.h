/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _CIS_HANDLERS_H
#define	_CIS_HANDLERS_H

#pragma ident	"@(#)cis_handlers.h	1.16	95/07/17 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * This is the CIS tuple handler header file.
 *
 * Each tuple that we recognize and are prepared to handle is assigned a
 *	cistpl_callout_t structure.  This lets us specify a handler for
 *	this tuple, as well as flags that describe this tuple and which
 *	are used by the CIS interpreter and tuple parser.
 */
typedef struct cistpl_callout_t {
    cisdata_t	type;		/* type of tuple */
    cisdata_t	subtype;	/* only used for CISTPL_FUNCE */
    int		flags;		/* misc flags */
    int		(*handler)();	/* tuple handler */
    char	*text;		/* name of tuple */
} cistpl_callout_t;

/*
 * Flags that are used by a tuple handler to specify what action it
 *	should perform.
 */
#define	HANDTPL_NOERROR		0x000000000 /* no error */
#define	HANDTPL_SET_FLAGS	0x000000001 /* set tuple flags */
#define	HANDTPL_COPY_DONE	0x000000002 /* tuple data copy is done */
#define	HANDTPL_PARSE_LTUPLE	0x000000004 /* parse tuple, return opt data */

/*
 * This flag is returned by tuple handlers if they encounter an error. It
 *	is returned by cis_list_create if any of the tuple handlers have
 *	return an error while processing the CIS.
 *
 * Note that the following bit is reserved:
 *		#define	BAD_CIS_ADDR	0x080000000
 *	It appears in cis.h and is used to indicate that cis_list_create
 *	tried to read past the end of the mapped in CIS space.
 */
#define	HANDTPL_ERROR		0x001000000 /* handler returned an error */

/*
 * General-use constants and macros that aren't specific to a tuple.
 */
#define	CISTPL_EXT_BIT	0x080		/* additional extension bytes follow */

/*
 * Constants, macros and structures used by cistpl_devspeed and
 *	cis_convert_devspeed functions.
 */
#define	CISTPL_DEVSPEED_TABLE	0x000000001 /* use the device speed table */
#define	CISTPL_DEVSPEED_EXT	0x000000002 /* use the extended speed table */
#define	CISTPL_DEVSPEED_MAX_TBL	8 		/* max devspeed table entries */
#define	CISTPL_DEVSPEED_MAX_EXP	8		/* max exponent entries */
#define	CISTPL_DEVSPEED_MAX_MAN	16		/* max mantissa entries */
#define	CISTPL_DEVSPEED_TBL(t)	cistpl_devspeed_struct.table[(t) &	\
						(CISTPL_DEVSPEED_MAX_TBL - 1)]
#define	CISTPL_DEVSPEED_MAN(m)	cistpl_devspeed_struct.mantissa[(m) &	\
						(CISTPL_DEVSPEED_MAX_MAN - 1)]
#define	CISTPL_DEVSPEED_EXP(e)	cistpl_devspeed_struct.exponent[(e) &	\
						(CISTPL_DEVSPEED_MAX_EXP - 1)]
typedef struct cistpl_devspeed_struct_t {
    int		*table;
    int		*tenfac;
    int		*mantissa;
    int		*exponent;
} cistpl_devspeed_struct_t;

/*
 * Constants, flags and structure typedefs that are used by specific tuples.
 *
 * CISTPL_DEVICE and CISTPL_DEVICE_A
 */
#define	CISTPL_DEVICE_DTYPE_NULL	0x00	/* a NULL device (hole) */
#define	CISTPL_DEVICE_DTYPE_ROM		0x01	/* device is of type ROM */
#define	CISTPL_DEVICE_DTYPE_OTPROM	0x02	/* device is of type OTPROM */
#define	CISTPL_DEVICE_DTYPE_EPROM	0x03	/* device is of type EPROM */
#define	CISTPL_DEVICE_DTYPE_EEPROM	0x04	/* device is of type EEPROM */
#define	CISTPL_DEVICE_DTYPE_FLASH	0x05	/* device is of type FLASH */
#define	CISTPL_DEVICE_DTYPE_SRAM	0x06	/* device is of type SRAM */
#define	CISTPL_DEVICE_DTYPE_DRAM	0x07	/* device is of type DRAM */
#define	CISTPL_DEVICE_DTYPE_FUNCSPEC	0x0d	/* device is of type FUNCSPEC */
#define	CISTPL_DEVICE_DTYPE_EXTEND	0x0e	/* device is of type extended */
#define	CISTPL_DEVICE_WPS		0x01	/* WPS bit is set */
/*
 * CISTPL_DEVICE_MAX_DEVICES defines the maximum number of devices that
 *	we can parse in a CISTPL_DEVICE or CISTPL_DEVICE_A tuple
 */
#define	CISTPL_DEVICE_MAX_DEVICES	10
typedef struct cistpl_device_t {
	int		num_devices; /* number of devices found */
	struct cistpl_device_node { /*  */
		u_long	flags;	/* flags specific to this device (i.e. WPS) */
		u_long	speed;	/* device speed in device speed code format */
		u_long	nS_speed; /* device speed in nS */
		u_long	type;	/* device type */
		u_long	size;	/* device size */
		u_long	size_in_bytes; /* device size in bytes */
	} cistpl_device_node[CISTPL_DEVICE_MAX_DEVICES];
} cistpl_device_t;

/*
 * CISTPL_CONFIG
 */
		/* config reg offset */
#define	MAKE_CONFIG_REG_ADDR(base, reg)	(base + (reg * 2))
#define	CISTPL_CONFIG_MAX_CONFIG_REGS	128 /* max num config regs */
typedef struct cistpl_config_t {
    u_long	present;	/* register present flags */
    int		nr;		/* number of config registers found */
    int		hr;		/* highest config register index found */
    int		regs[CISTPL_CONFIG_MAX_CONFIG_REGS];	/* reg offsets */
    int		base;		/* base offset of config registers */
    int		last;		/* last config index */
} cistpl_config_t;

/*
 * CISTPL_VERS_1
 */
#define	CISTPL_VERS_1_MAX_PROD_STRINGS	4 /* max number product strings */
typedef struct cistpl_vers_1_t {
    u_long	major;		/* major version number */
    u_long	minor;		/* minor version number */
    int		ns;		/* number of information strings */
				/* pointers to product information strings */
    uchar_t	*pi[CISTPL_VERS_1_MAX_PROD_STRINGS];
} cistpl_vers_1_t;

/*
 * CISTPL_VERS_2
 */
typedef struct cistpl_vers_2_t {
    int		vers;		/* version number */
    int		comply;		/* level of compliance */
    int		dindex;		/* byte address of first data byte in card */
    int		reserved;	/* two reserved bytes */
    int		vspec8;		/* vendor specific (byte 8) */
    int		vspec9;		/* vendor specific (byte 9) */
    int		nhdr;		/* number of copies of CIS present on device */
    uchar_t    *oem;		/* Vendor of software that formatted card */
    uchar_t    *info;		/* Informational message about card */
} cistpl_vers_2_t;

/*
 * CISTPL_JEDEC_A
 */
#define	CISTPL_JEDEC_MAX_IDENTIFIERS	4
typedef struct cistpl_jedec_t {
	int	nid;		/* number of JEDEC identifiers present */
	struct {
		int id;		/* manufacturer id */
		int info;	/* manufacturer specific info */
	}	jid[CISTPL_JEDEC_MAX_IDENTIFIERS];
} cistpl_jedec_t;

/*
 * CISTPL_FORMAT
 */
typedef struct cistpl_format_t {
	int	type;	/* format type code */
	int	edc_length; /* error detection code length */
	int	edc_type; /* error detection code type */
	int	offset;	/* offset of first byte of data in this part */
	int	nbytes;	/* number of bytes of data in this partition */
	union {
		struct disk {
		    int bksize; /* block size */
		    int nblocks; /* num blocks data for disk-like device */
		    int edcloc; /* location of error detection code */
		} disk;
		struct mem {
		    int  flags; /* various flags */
		    int  reserved; /* reserved byte */
		    caddr_t  address; /* physical address for mem-like device */
		    int  edcloc; /* location of error detection code */
		} mem;
	}		dev;
} cistpl_format_t;

/*
 * device format types
 */
#define	TPLFMTTYPE_DISK	0x00	/* disk-like format */
#define	TPLFMTTYPE_MEM	0x01	/* memory-like format */
#define	TPLFMTTYPE_VS	0x80	/* vendor specific format */

/*
 * error detection code types
 */
#define	TPLFMTEDC_NONE	0x00	/* no error detection code */
#define	TPLFMTEDC_CKSUM	0x01	/* arithmetic checksum is used */
#define	TPLFMTEDC_CRC	0x02	/* 16-bit CRC */
#define	TPLFMTEDC_PCC	0x03	/* whole-partition arithmetic checksum */
#define	TPLFMTEDC_VS	0x80	/* vendor specific error checking */

#define	EDC_LENGTH_MASK	0x07
#define	EDC_TYPE_MASK   0x0f
#define	EDC_TYPE_SHIFT	3

/*
 * flags for memory-like devices
 */
#define	TPLFMTFLAGS_ADDR	0x01	/* address is valid */
#define	TPLFMTFLAGS_AUTO	0x02	/* automatically map memory region */

/*
 * CISTPL_GEOMETRY
 */
typedef struct cistpl_geometry_t {
	int spt;
	int tpc;
	int ncyl;
} cistpl_geometry_t;

/*
 * CISTPL_BYTEORDER
 */
typedef struct cistpl_byteorder_t {
	int order;		/* byte order code */
	int map;		/* byte mapping code */
} cistpl_byteorder_t;

/*
 * byte order and mapping codes
 */
#define	TPLBYTEORD_LOW	0x00	/* specifies little endian order */
#define	TPLBYTEORD_HIGH	0x01	/* specifies big endian order */
#define	TPLBYTEORD_VS	0x80	/* vendor specific order 0x80-0xFF */

#define	TPLBYTEMAP_LOW	0x00	/* byte zero is least significant byte */
#define	TPLBYTEMAP_HIGH	0x01	/* byte zero is most significant byte */
#define	TPLBYTEMAP_VS	0x80	/* vendor specific mapping */

/*
 * CISTPL_DATE
 */
typedef struct cistpl_date_t {
	int time;
	int day;
} cistpl_date_t;

/*
 * CISTPL_BATTERY
 */
typedef struct cistpl_battery_t {
	int rday;		/* replacement date */
	int xday;		/* expiration date */
} cistpl_battery_t;

/*
 * CISTPL_ORG
 */
typedef struct cistpl_org_t {
	int 	type;		/* data organization code */
	char   *desc;		/* text description of this organization */
} cistpl_org_t;

/*
 * CISTPL_MANFID
 */
typedef struct cistpl_manfid_t {
	int manf;		/* PCMCIA PC Card manufacturer code */
	int card;		/* manufacturer information */
} cistpl_manfid_t;

/*
 * CISTPL_FUNCID
 */
typedef struct cistpl_funcid_t {
	int function;		/* PC Card function code */
	int sysinit;		/* system initialization mask */
} cistpl_funcid_t;

/*
 * Function types for CISTPL_FUNCID; note that the TPLFUNC_UNKNOWN is
 *	not defined by the PCMCIA standard.
 */
#define	TPLFUNC_MULTI		0x000	/* multifunction card */
#define	TPLFUNC_MEMORY		0x001	/* memory card */
#define	TPLFUNC_SERIAL		0x002	/* serial I/O port */
#define	TPLFUNC_PARALLEL	0x003	/* parallel printer port */
#define	TPLFUNC_FIXED		0x004	/* fixed disk, silicon or removeable */
#define	TPLFUNC_VIDEO		0x005	/* video interface */
#define	TPLFUNC_LAN		0x006	/* Local Area Network adapter */
#define	TPLFUNC_UNKNOWN		0x0ff	/* unknown function(s) */

#define	TPLINIT_POST		0x01	/* POST should attempt configure */
#define	TPLINIT_ROM		0x02	/* map ROM during sys init */

/*
 * CISTPL_FUNCE
 */
typedef struct cistpl_funce_t {
	int function;		/* type of extended data */
	int subfunction;
	union {
		struct serial {
			int ua;	/* identification of UART in use */
			int uc;	/* identification of UART capabilities */
		} serial;
		struct modem {
			int fc;	/* supported flow control methods */
			int cb;	/* size of DCE command buffer */
			int eb;	/* size of DCE to DCE buffer */
			int tb;	/* size of DTE to DCE buffer */
		} modem;
		struct data_modem {
			int ud;	/* highest data rate */
			int ms;	/* modulation standards */
			/* err correct proto and non-CCITT modulation */
			int em;
			int dc;	/* data compression protocols */
			int cm;	/* command protocols */
			int ex;	/* escape mechanisms */
			int dy;	/* standardized data encryption */
			int ef;	/* misc. end user features */
			int ncd; /* number of country codes */
			u_char cd[16];	/* CCITT country code */
		} data_modem;
		struct fax {
			int uf;	/* highest data rate in DTE/UART interface */
			int fm;	/* supported CCITT modulation standards */
			int fy;	/* standardized data encryption */
			int fs;	/* feature selection */
			int ncf; /* number of country codes */
			u_char cf[16];	/* CCITT country codes */
		} fax;
		struct voice {
			int uv;	/* highest data rate */
			int nsr;
			int sr[16]; /* voice sampling rates (*100) */
			int nss;
			int ss[16]; /* voice sample sizes (*10) */
			int nsc;
			int sc[16]; /* voice compression methods */
		} voice;
		struct lan {
			int tech; /* network technology */
			long speed; /* media bit or baud rate */
			int media; /* network media supported */
			int con; /* open/closed connector standard */
			int id_sz; /* length of lan station id */
			u_char id[16]; /* station ID */
		} lan;
	}   data;
} cistpl_funce_t;

/* serial port subfunctions */
#define	TPLFE_SUB_SERIAL	0 /* serial port */
#define	TPLFE_SUB_MODEM_COMMON	1 /* common modem interface */
#define	TPLFE_SUB_MODEM_DATA	2 /* data modem services */
#define	TPLFE_SUB_MODEM_FAX	3 /* fax modem services */
#define	TPLFE_SUB_VOICE		4 /* voice services */
/* modem subfunctions for description of capabilities */
#define	TPLFE_CAP_MODEM_DATA	5 /* data modem capabilities */
#define	TPLFE_CAP_MODEM_FAX	6 /* fax modem capabilities */
#define	TPLFE_CAP_MODEM_VOICE	7 /* voice modem capabilities */
/* serial port subfunctions for description of capabilities */
#define	TPLFE_CAP_SERIAL_DATA	8 /* serial port capabilities - data modem */
#define	TPLFE_CAP_SERIAL_FAX	9 /* serial port capabilities - fax modem */
#define	TPLFE_CAP_SERIAL_VOICE 10 /* serial port capabilities - voice */

/* serial port UART definitions */
#define	TPLFE_UA_8250		0 /* Intel 8250 */
#define	TPLFE_UA_16450		1 /* NS 16450 */
#define	TPLFE_UA_16550		2 /* NS 16550 */

/* serial port capabilities definitions */
#define	TPLFE_UC_PARITY_SPACE	0x0001 /* space parity supported */
#define	TPLFE_UC_PARITY_MARK	0x0002 /* mark parity supported */
#define	TPLFE_UC_PARITY_ODD	0x0004 /* odd parity supported */
#define	TPLFE_UC_PARITY_EVEN	0x0008 /* even parity supported */
#define	TPLFE_UC_CS5		0x0100 /* 5 bit characters supported */
#define	TPLFE_UC_CS6		0x0200 /* 6 bit characters supported */
#define	TPLFE_UC_CS7		0x0400 /* 7 bit characters supported */
#define	TPLFE_UC_CS8		0x0800 /* 8 bit characters supported */
#define	TPLFE_UC_STOP_1		0x1000 /* 1 stop bit supported */
#define	TPLFE_UC_STOP_15	0x2000 /* 1.5 stop bits supported */
#define	TPLFE_UC_STOP_2		0x4000 /* 2 stop bits supported */

/* modem flow control methods */
#define	TPLFE_FC_TX_XONOFF	0x01 /* transmit XON/XOFF */
#define	TPLFE_FC_RX_XONOFF	0x02 /* receiver XON/XOFF */
#define	TPLFE_FC_TX_HW		0x04 /* transmit hardware flow control (CTS) */
#define	TPLFE_FC_RX_HW		0x08 /* receiver hardware flow control (RTS) */
#define	TPLFE_FC_TRANS		0x10 /* tranparent flow control */

/* modem modulation standards */
#define	TPLFE_MS_BELL103	0x0001 /* 300bps */
#define	TPLFE_MS_V21		0x0002 /* 300bps (V.21) */
#define	TPLFE_MS_V23		0x0004 /* 600/1200bps (V.23) */
#define	TPLFE_MS_V22AB		0x0008 /* 1200bps (V.22A V.22B) */
#define	TPLFE_MS_BELL212	0x0010 /* 2400bsp (US Bell 212) */
#define	TPLFE_MS_V22BIS		0x0020 /* 2400bps (V.22bis) */
#define	TPLFE_MS_V26		0x0040 /* 2400bps leased line (V.26) */
#define	TPLFE_MS_V26BIS		0x0080 /* 2400bps (V.26bis) */
#define	TPLFE_MS_V27BIS		0x0100 /* 4800/2400bps leased line (V.27bis) */
#define	TPLFE_MS_V29		0x0200 /* 9600/7200/4800 leased line (V.29) */
#define	TPLFE_MS_V32		0x0400 /* up to 9600bps (V.32) */
#define	TPLFE_MS_V32BIS		0x0800 /* up to 14400bps (V.32bis) */
#define	TPLFE_MS_VFAST		0x1000 /* up to 28800 V.FAST */

/* modem error correction/detection protocols */
#define	TPLFE_EM_MNP		0x01 /* MNP levels 2-4 */
#define	TPLFE_EM_V42		0x02 /* CCITT LAPM (V.42) */

/* modem data compression protocols */
#define	TPLFE_DC_V42BIS		0x01 /* CCITT compression V.42 */
#define	TPLFE_DC_MNP5		0x02 /* MNP compression (uses MNP 2, 3 or 4) */

/* modem command protocols */
#define	TPLFE_CM_AT1	0x01 /* ANSI/EIA/TIA 602 "Action" commands */
#define	TPLFE_CM_AT2	0x02 /* ANSI/EIA/TIA 602 "ACE/DCE IF Params" */
#define	TPLFE_CM_AT3	0x04 /* ANSI/EIA/TIA 602 "Ace Parameters" */
#define	TPLFE_CM_MNP_AT	0x08 /* MNP specificat AT commands */
#define	TPLFE_CM_V25BIS	0x10 /* V.25bis calling commands */
#define	TPLFE_CM_V25A	0x20 /* V.25bis test procedures */
#define	TPLFE_CM_DMCL	0x40 /* DMCL command mode */

/* modem escape mechanism */
#define	TPLFE_EX_BREAK		0x01 /* BREAK support standardized */
#define	TPLFE_EX_PLUS		0x02 /* +++ returns to command mode */
#define	TPLFE_EX_UD		0x04 /* user defined escape character */

/* modem miscellaneous features */
#define	TPLFE_EF_CALLERID	0x01 /* Caller ID is supported */

/* fax modulation standards */
#define	TPLFE_FM_V21C2	0x01 /* 300bps (V.21-C2) */
#define	TPLFE_FM_V27TER	0x02 /* 4800/2400bps (V.27ter) */
#define	TPLFE_FM_V29	0x04 /* 9600/7200/4800 leased line (V.29) */
#define	TPLFE_FM_V17	0x08 /* 14.4K/12K/9600/7200bps (V.17) */
#define	TPLFE_FM_V33	0x10 /* 14.4K/12K/9600/7200 lease line (V.33) */

/* fax feature selection */
#define	TPLFE_FS_T3		0x01 /* Group 2 (T.3) service class */
#define	TPLFE_FS_T4		0x02 /* Group 3 (T.4) service class */
#define	TPLFE_FS_T6		0x04 /* Group 4 (T.6) service class */
#define	TPLFE_FS_ECM		0x08 /* Error Correction Modeer */
#define	TPLFE_FS_VOICEREQ	0x10 /* voice requests allowed */
#define	TPLFE_FS_POLLING	0x20 /* polling support */
#define	TPLFE_FS_FTP		0x40 /* file transfer support */
#define	TPLFE_FS_PASSWORD	0x80 /* password support */

/* LAN tuple definitions */
#define	TPLFE_NETWORK_INFO	0x00

/* LAN technology types */
#define	TPLFE_LAN_TECH_ARCNET		0
#define	TPLFE_LAN_TECH_ETHERNET		1
#define	TPLFE_LAN_TECH_TOKENRING	2
#define	TPLFE_LAN_TECH_LOCALTALK	3
#define	TPLFE_LAN_TECH_FDDI		4
#define	TPLFE_LAN_TECH_ATM		5
#define	TPLFE_LAN_TECH_WIRELESS		6
#define	TPLFE_LAN_TECH_RESERVED		7

/* LAN media types */
#define	TPLFE_LAN_MEDIA_INHERENT	0
#define	TPLFE_LAN_MEDIA_UTP		1
#define	TPLFE_LAN_MEDIA_STP		2
#define	TPLFE_LAN_MEDIA_COAX		3
#define	TPLFE_LAN_MEDIA_FIBER		4
#define	TPLFE_LAN_MEDIA_AUI		5
#define	TPLFE_LAN_MEDIA_SSR_902		6
#define	TPLFE_LAN_MEDIA_SSR_2_4		7
#define	TPLFE_LAN_MEDIA_SSR_5_4		8
#define	TPLFE_LAN_MEDIA_DIFFUSE_IR	9
#define	TPLFE_LAN_MEDIA_PTP_IR		10
#define	TPLFE_LAN_MEDIA_RESERVED	11

/*
 * CISTPL_CFTABLE_ENTRY
 *
 * These flags and macros are used internally to the handler.
 */
	/* mask to get the config entry number from TPCE_INDX */
#define	CISTPL_CFTABLE_TPCE_CFGENTRYM		0x03f
		/* default config bit in TPCE_INDX */
#define	CISTPL_CFTABLE_TPCE_DEFAULTM		0x040
		/* interface config byte follows */
#define	CISTPL_CFTABLE_TPCE_IFM			0x080

		/* power bit mask for tpce_fs */
#define	CISTPL_CFTABLE_TPCE_FS_PWRM		0x003
		/* Vcc, Vpp1 and Vpp2 descriptions */
#define	CISTPL_CFTABLE_TPCE_FS_PWR_VPP2M	0x003
		/* Vcc and Vpp1=Vpp2 descriptions */
#define	CISTPL_CFTABLE_TPCE_FS_PWR_VPP1M	0x002
		/* Vcc description only */
#define	CISTPL_CFTABLE_TPCE_FS_PWR_VCCM		0x001
		/* no connection on sleep/power down */
#define	CISTPL_CFTABLE_PD_NC_SLEEPM		0x07d
		/* zero value required */
#define	CISTPL_CFTABLE_PD_ZEROM			0x07e
		/* no connection ever */
#define	CISTPL_CFTABLE_PD_NCM			0x07f

		/* timing data exists */
#define	CISTPL_CFTABLE_TPCE_FS_TDM		0x004
		/* WAIT scale mask */
#define	CISTPL_CFTABLE_TPCE_FS_TD_WAITM		0x003
#define	GET_TPCE_FS_TD_WAITS(sf)	((sf)& \
					    CISTPL_CFTABLE_TPCE_FS_TD_WAITM)
		/* RDY/BSY scale mask */
#define	CISTPL_CFTABLE_TPCE_FS_TD_RDYM		0x01c
#define	GET_TPCE_FS_TD_RDYS(sf)	(((sf)>>2)& \
					CISTPL_CFTABLE_TPCE_FS_TD_RDYM)
		/* RSVD scale mask */
#define	CISTPL_CFTABLE_TPCE_FS_TD_RSVDM		0x0e0
#define	GET_TPCE_FS_TD_RSVDS(sf)	(((sf)>>5)& \
					    CISTPL_CFTABLE_TPCE_FS_TD_RSVDM)

#define	CISTPL_CFTABLE_TPCE_FS_IOM		0x008	/* I/O data exists */
		/* I/O addr lines mask */
#define	CISTPL_CFTABLE_TPCE_FS_IO_ALM		0x01f
		/* RANGE bit in TPCE_IO */
#define	CISTPL_CFTABLE_TPCE_FS_IO_RANGEM	0x080
		/* max of 16 I/O ranges */
#define	CISTPL_CFTABLE_ENTRY_MAX_IO_RANGES	16

#define	CISTPL_CFTABLE_TPCE_FS_IRQM		0x010	/* IRQ data exists */
		/* extended IRQ mask exists */
#define	CISTPL_CFTABLE_TPCE_FS_IRQ_MASKM	0x010

#define	CISTPL_CFTABLE_TPCE_FS_MEMM		0x060	/* mem space mask */
		/* space selection byte ... */
#define	CISTPL_CFTABLE_TPCE_FS_MEM3M		0x060
		/* length (2 bytes) and card address (2 bytes) */
#define	CISTPL_CFTABLE_TPCE_FS_MEM2M		0x040
		/* single 2-byte length */
#define	CISTPL_CFTABLE_TPCE_FS_MEM1M		0x020
		/* max of 8 mem space descriptors */
#define	CISTPL_CFTABLE_ENTRY_MAX_MEM_WINDOWS	8
		/* number of bytes/page description */
#define	CISTPL_CFTABLE_TPCE_FS_MEM_PGSIZE	256
		/* host addr info present */
#define	CISTPL_CFTABLE_TPCE_FS_MEM_HOSTM	0x080

#define	CISTPL_CFTABLE_TPCE_FS_MISCM		0x080	/* misc fields mask */

/*
 * Constants, macros, structures and flags used by cistpl_pd_parse()
 *	cistpl_expd_parse() and the CISTPL_CFTABLE_ENTRY tuple handler.
 */
#define	CISTPL_PD_MAN(m)	cistpl_pd_struct.mantissa[m&15]
#define	CISTPL_PD_EXP(e)	cistpl_pd_struct.exponent[e&7]
typedef struct cistpl_pd_struct_t {
    int		*mantissa;
    int		*exponent;
} cistpl_pd_struct_t;

/*
 * These flags are passed to the caller in the cistpl_cftable_entry_t->flags
 *	field and indicate what interface information is available.  The low
 *	order byte of this field is reserved and no flags should be defined
 *	to exist there.
 */
#define	CISTPL_CFTABLE_TPCE_DEFAULT	0x000000100 /* this is a default conf */

/* interface config description present flags */
#define	CISTPL_CFTABLE_TPCE_IF		0x000000200 /* if config byte exists */
/*
 * When the CISTPL_CFTABLE_TPCE_IF flag is set, the following flags
 *	are available in the ifc member of the cistpl_cftable_entry_t
 *	structure.
 */
#define	CISTPL_CFTABLE_TPCE_IF_BVD	0x10		/* BVD active in PRR */
#define	CISTPL_CFTABLE_TPCE_IF_WP	0x20		/* WP active in PRR */
#define	CISTPL_CFTABLE_TPCE_IF_RDY	0x40		/* RDY active in PRR */
#define	CISTPL_CFTABLE_TPCE_IF_MWAIT	0x80		/* WAIT - mem cycles */

/* power description present flags */
#define	CISTPL_CFTABLE_TPCE_FS_PWR	0x000001000 /* power info exists */

/* timing description present flags */
#define	CISTPL_CFTABLE_TPCE_FS_TD	0x000010000 /* timing info exists */

/* I/O description present flags */
#define	CISTPL_CFTABLE_TPCE_FS_IO	0x000100000 /* I/O information exists */

/* IRQ description present flags */
#define	CISTPL_CFTABLE_TPCE_FS_IRQ	0x000200000 /* IRQ information exists */

/* memory space description present flags */
#define	CISTPL_CFTABLE_TPCE_FS_MEM	0x001000000 /* MEM space info exists */

/* misc description present flags */
#define	CISTPL_CFTABLE_TPCE_FS_MISC	0x002000000 /* MISC info exists */

/*
 * Power description flags and structures.
 *
 * The following eight values represent what the power description structure
 *	parameter selection byte tells us is present.  A copy of this byte
 *	is in the low order byte of each parameter's flag field.
 */
#define	CISTPL_CFTABLE_PD_NOMV		0x001	/* nominal supply voltage */
#define	CISTPL_CFTABLE_PD_MINV		0x002	/* minimum supply voltage */
#define	CISTPL_CFTABLE_PD_MAXV		0x004	/* maximum supply voltage */
#define	CISTPL_CFTABLE_PD_STATICI	0x008	/* continuous supply current */
		/* max current required averaged over 1 second */
#define	CISTPL_CFTABLE_PD_AVGI		0x010
		/* maximum current required averaged over 10mS */
#define	CISTPL_CFTABLE_PD_PEAKI		0x020
		/* power down supply curent required */
#define	CISTPL_CFTABLE_PD_PDOWNI	0x040
		/* power supply is about to blow up */
#define	CISTPL_CFTABLE_PD_RFU		0x080

/*
 * For each voltage/current parameter, there is an associated flags field.
 *	The following flags are in this field.  The low order byte of each
 *	of these flags fields also contains a copy of the power description
 *	structure parameter selection byte as read from the tuple, that's why
 *	we start the flag values at 0x0100 and go up from there.
 */
		/* this parameter exists */
#define	CISTPL_CFTABLE_PD_EXISTS	0x000000100
		/* multiply return value by 10 */
#define	CISTPL_CFTABLE_PD_MUL10		0x000000200
		/* no connection on sleep/power down */
#define	CISTPL_CFTABLE_PD_NC_SLEEP	0x000001000
		/* zero value required */
#define	CISTPL_CFTABLE_PD_ZERO		0x000002000
		/* no connection ever */
#define	CISTPL_CFTABLE_PD_NC		0x000004000

typedef struct cistpl_cftable_entry_pwr_t {
	ulong_t	nomV;		/* nominal supply voltage */
	int		nomV_flags;
	ulong_t	minV;		/* minimum supply voltage */
	int		minV_flags;
	ulong_t	maxV;		/* maximum supply voltage */
	int		maxV_flags;
	ulong_t	staticI;	/* continuous supply current */
	int		staticI_flags;
		/* max current required averaged over 1 second */
	ulong_t	avgI;
	int		avgI_flags;
	ulong_t	peakI;		/* max current required averaged over 10mS */
	int		peakI_flags;
	ulong_t	pdownI;		/* power down supply curent required */
	int		pdownI_flags;
} cistpl_cftable_entry_pwr_t;

/*
 * Flags for the global power description structure.  These show up in
 *	the flags field of the structure.
 */
#define	CISTPL_CFTABLE_TPCE_FS_PWR_VCC	0x000000001 /* Vcc description valid  */
#define	CISTPL_CFTABLE_TPCE_FS_PWR_VPP1	0x000000002 /* vpp1 description valid */
#define	CISTPL_CFTABLE_TPCE_FS_PWR_VPP2	0x000000004 /* Vpp2 description valid */

typedef struct cistpl_cftable_entry_pd_t {
	int		flags; /* which descriptions are valid */
	struct cistpl_cftable_entry_pwr_t pd_vcc; /* VCC power description */
	struct cistpl_cftable_entry_pwr_t pd_vpp1; /* Vpp1 power description */
	struct cistpl_cftable_entry_pwr_t pd_vpp2; /* Vpp2 power description */
} cistpl_cftable_entry_pd_t;

/*
 * Device speed structure.  Each field is only valid if the
 *	CISTPL_CFTABLE_TPCE_FS_TD flag is set.
 *
 * The following flags describe which timing information is available.
 *	They appear in the flags field of the device speed structure.
 */
		/* WAIT timing exists */
#define	CISTPL_CFTABLE_TPCE_FS_TD_WAIT	0x000000001
		/* RDY/BSY timing exists */
#define	CISTPL_CFTABLE_TPCE_FS_TD_RDY	0x000000002
		/* RSVD timing exists */
#define	CISTPL_CFTABLE_TPCE_FS_TD_RSVD	0x000000004

typedef struct cistpl_cftable_entry_speed_t {
    int		flags;		/* which timing information is present */
    ulong_t	wait;		/* max WAIT time in device speed format */
    ulong_t	nS_wait;	/* max WAIT time in nS */
    ulong_t	rdybsy;		/* max RDY/BSY time in device speed format */
    ulong_t	nS_rdybsy;	/* max RDY/BSY time in nS */
    ulong_t	rsvd;		/* max RSVD time in device speed format */
    ulong_t	nS_rsvd;	/* max RSVD time in nS */
} cistpl_cftable_entry_speed_t;

/*
 * Device I/O range description structures.  Only valid if the
 *	CISTPL_CFTABLE_TPCE_FS_IO flag is set.
 *
 * The following flags describe the IO description information. They
 *	appear in the flags field of the IO space description structure.
 */
#define	CISTPL_CFTABLE_TPCE_FS_IO_BUS	0x060	/* bus width mask */
#define	CISTPL_CFTABLE_TPCE_FS_IO_BUS8	0x020	/* 8-bit flag */
#define	CISTPL_CFTABLE_TPCE_FS_IO_BUS16	0x040	/* 16-bit flag */
#define	CISTPL_CFTABLE_TPCE_FS_IO_RANGE	0x080	/* IO address ranges exist */

typedef struct cistpl_cftable_entry_io_range_t {
    int		addr;		/* I/O start address */
    int		length;		/* I/O register length */
} cistpl_cftable_entry_io_range_t;
typedef struct cistpl_cftable_entry_io_t {
    int		flags;		/* direct copy of TPCE_IO byte in tuple */
    int		addr_lines;	/* number of decoded I/O address lines */
    int		ranges;		/* number of I/O ranges */
    struct cistpl_cftable_entry_io_range_t
	    range[CISTPL_CFTABLE_ENTRY_MAX_IO_RANGES];
} cistpl_cftable_entry_io_t;

/*
 * Device IRQ description structure.  Only valid if the
 *	CISTPL_CFTABLE_TPCE_FS_IRQ flag is set.
 */
typedef struct cistpl_cftable_entry_irq_t {
    int		flags;		/* direct copy of TPCE_IR byte in tuple */
    int		irqs;		/* bit mask for each allowed IRQ */
} cistpl_cftable_entry_irq_t;

/*
 * Device memory space description structure.  Only valid if the
 *	CISTPL_CFTABLE_TPCE_FS_MEM flag is set.
 *
 * The following flags describe the memory description information.  They
 *	appear in the flags field of the memory space description structure.
 */
		/* space descriptors */
#define	CISTPL_CFTABLE_TPCE_FS_MEM3	0x000000001
		/* host_addr=card_addr */
#define	CISTPL_CFTABLE_TPCE_FS_MEM2	0x000000002
		/* card address=0, any host address */
#define	CISTPL_CFTABLE_TPCE_FS_MEM1	0x000000004
		/* if host address is present in MEM3 */
#define	CISTPL_CFTABLE_TPCE_FS_MEM_HOST	0x000000008

typedef struct cistpl_cftable_entry_mem_window_t {
    int		length;		/* length of this window */
    int		card_addr;	/* card address */
    int		host_addr;	/* host address */
} cistpl_cftable_entry_mem_window_t;
typedef struct cistpl_cftable_entry_mem_t {
    int		flags;		/* memory desc type and host addr info */
    int		windows;	/* number of memory space descriptors */
    cistpl_cftable_entry_mem_window_t
	    window[CISTPL_CFTABLE_ENTRY_MAX_MEM_WINDOWS];
} cistpl_cftable_entry_mem_t;

/*
 * Devices misc description structure.  Only valid if the
 *	CISTPL_CFTABLE_TPCE_FS_MISC flag is set.
 */
typedef struct cistpl_cftable_entry_misc_t {
    int		flags;		/* direct copy of TPCE_MI byte in tuple */
} cistpl_cftable_entry_misc_t;

/*
 * cistpl_cftable_entry_t - this is the struct that the caller passes
 *				to the CISTPL_CFTABLE_ENTRY handler
 */
typedef struct cistpl_cftable_entry_t {
    int		flags;		/* which descriptions are valid */
    int		ifc;		/* interface description info */
    u_long	pin;		/* values for PRR */
    int		index;		/* configuration index number */
    struct cistpl_cftable_entry_pd_t	pd; /* power requirements description */
    struct cistpl_cftable_entry_speed_t	speed; /* device speed description */
    struct cistpl_cftable_entry_io_t	io; /* device I/O map */
    struct cistpl_cftable_entry_irq_t	irq; /* device IRQ utilization */
    struct cistpl_cftable_entry_mem_t	mem; /* device memory space */
    struct cistpl_cftable_entry_misc_t	misc; /* misc device features */
} cistpl_cftable_entry_t;

/*
 * CISTPL_LINKTARGET
 *
 * This tuple is used to verify that tuple chains other than the primary
 *	chain which starts at offset 0 in Attribute Memory are valid. All
 *	secondary tuple chains are required to contain this tuple as the
 *	first tuple of the chain.
 * This tuple must have a link field of at least MIN_LINKTARGET_LENGTH and
 *	must contain the byte pattern CISTPL_LINKTARGET_MAGIC.
 * LINKTARGET_AC_HEADER_LENGTH is the number of bytes contained in a
 *	valid CISTPL_LINKTARGET tuple header.
 */
#define	MIN_LINKTARGET_LENGTH		3
#define	CISTPL_LINKTARGET_MAGIC		"CIS"
#define	LINKTARGET_AC_HEADER_LENGTH	2

typedef struct cistpl_linktarget_t {
	int		length;		/* number of bytes in tpltg_tag */
	uchar_t		*tpltg_tag;
} cistpl_linktarget_t;

/*
 * CISTPL_LONGLINK_A and CISTPL_LONGLINK_C
 *
 * Both of these tuples are processed the same way. The target address is
 *	really an offset from the beginning of the specified address space
 *	and is not a virtual address.
 * This tuple must have a link field of at least MIN_LONGLINK_AC_LENGTH.
 */
#define	MIN_LONGLINK_AC_LENGTH		4

typedef struct cistpl_longlink_ac_t {
	int		flags;		/* space flags */
	unsigned	tpll_addr;	/* target address, normalized */
} cistpl_longlink_ac_t;
/*
 * Flags for cistpl_longlink_ac_t->flags
 */
#define	CISTPL_LONGLINK_AC_AM	0x0001	/* longlink to AM */
#define	CISTPL_LONGLINK_AC_CM	0x0002	/* longlink to CM */

/*
 * cisparse_t - the structure that unifies all tuple parsing structures
 */
typedef union cisparse_t {
	cistpl_config_t		config;
	cistpl_device_t		device;
	cistpl_vers_1_t		version_1;
	cistpl_vers_2_t		version_2;
	cistpl_jedec_t		jedec;
	cistpl_format_t		format;
	cistpl_geometry_t	geometry;
	cistpl_byteorder_t	byteorder;
	cistpl_date_t		date;
	cistpl_battery_t	battery;
	cistpl_org_t		org;
	cistpl_manfid_t		manfid;
	cistpl_funcid_t		funcid;
	cistpl_funce_t		funce;
	cistpl_cftable_entry_t	cftable;
	cistpl_linktarget_t	linktarget;
} cisparse_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _CIS_HANDLERS_H */
