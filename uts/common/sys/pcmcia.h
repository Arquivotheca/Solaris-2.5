/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

/*
 * PCMCIA nexus
 */

#ifndef _PCMCIA_H
#define	_PCMCIA_H

#pragma ident	"@(#)pcmcia.h	1.28	95/01/25 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#if defined(DEBUG)
#define	PCMCIA_DEBUG
#endif

#include <sys/modctl.h>

#define	PCMCIA_MAX_ADAPTERS	8 /* maximum distinct adapters */
#define	PCMCIA_MAX_SOCKETS	64 /* maximum distinct sockets */
#define	PCMCIA_MAX_WIN_ADAPT	40
#define	PCMCIA_MAX_WINDOWS	(PCMCIA_MAX_ADAPTERS*PCMCIA_MAX_WIN_ADAPT)
#define	PCMCIA_MAX_POWER	16 /* maximum power table entries */

#define	_VERSION(major, minor)	((major)<<16|(minor))

/*
 * DDI/Nexus stuff
 */

#define	PCMCIA_NEXUS_NAME	"pcmcia"
#define	PCMCIA_ADAPTER_NODE	"ddi_pcmcia:adapter"
#define	PCMCIA_SOCKET_NODE	"ddi_pcmcia:socket"
#define	PCMCIA_PCCARD_NODE	"ddi_pcmcia:pccard"

/*
 * private interface between nexus and adapter specific driver
 * This is only an "ops" type structure
 */

typedef struct pcmcia_if {
	ulong	  pcif_magic;	/* magic number to verify correct scructure */
	ulong	  pcif_version;
	int	(*pcif_set_callback)();
	int	(*pcif_get_adapter)();
	int	(*pcif_get_page)();
	int	(*pcif_get_socket)();
	int	(*pcif_get_status)();
	int	(*pcif_get_window)();
	int	(*pcif_inquire_adapter)();
	int	(*pcif_inquire_socket)();
	int	(*pcif_inquire_window)();
	int	(*pcif_reset_socket)();
	int	(*pcif_set_page)();
	int	(*pcif_set_window)();
	int	(*pcif_set_socket)();
	int	(*pcif_set_interrupt)();
	int	(*pcif_clr_interrupt)();
	int	(*pcic_init_dev)();
} pcmcia_if_t;

/*
 * magic number and version information to identify
 * variant of the PCMCIA nexus.
 */
#define	PCIF_MAGIC 0x50434946
#define	PCIF_VERSION	_VERSION(0, 1)
#define	PCIF_MIN_VERSION _VERSION(0, 1)
#define	DEFAULT_CS_NAME	"cs"

/*
 * all adapter drivers use a commonly defined structure for
 * their private data.  This structure must be filled in
 * and set.  The an_private member is for the driver writer's
 * use and is not looked at by the nexus.
 */
struct pcmcia_adapter_nexus_private {
	dev_info_t	*an_dip;
	pcmcia_if_t	*an_if;
	void		*an_private;
	ddi_iblock_cookie_t *an_iblock;	/* high priority handler cookies */
	ddi_idevice_cookie_t *an_idev;
};

/*
 * macros to make indirect functions easier
 * and shorter (makes cstyle happier)
 */

#define	GET_SOCKET_STATUS(f, dip, sock, stat)\
			(*(f)->pcif_get_socket_status)(dip, sock, stat)
#define	SET_CALLBACK(f, dip, callback, sock)\
			(*(f)->pcif_set_callback)(dip, callback, sock)

#define	GET_ADAPTER(f, dip, conf) (*(f)->pcif_get_adapter) (dip, conf)
#define	GET_SOCKET(f, dip, sock) (*(f)->pcif_get_socket)(dip, sock)
#define	GET_STATUS(f, dip, status) (*(f)->pcif_get_status)(dip, status)
#define	GET_WINDOW(f, dip, window) (*(f)->pcif_get_window)(dip, window)
#define	INQUIRE_ADAPTER(f, dip, inquire) (*(f)->pcif_inquire_adapter)(dip,\
						inquire)
#define	GET_CONFIG(f, dip, conf) INQUIRE_ADAPTER(f, dip, conf)
#define	INQUIRE_SOCKET(f, dip, sock) (*(f)->pcif_inquire_socket)(dip, \
						sock)
#define	GET_PAGE(f, dip, page) (*(f)->pcif_get_page)(dip, page)
#define	INQUIRE_WINDOW(f, dip, window) (*(f)->pcif_inquire_window)(dip, window)
#define	RESET_SOCKET(f, dip, socket, mode) \
			(*(f)->pcif_reset_socket)(dip, socket, mode)
#define	SET_PAGE(f, dip, page) (*(f)->pcif_set_page)(dip, page)
#define	SET_WINDOW(f, dip, window) (*(f)->pcif_set_window)(dip, window)
#define	SET_SOCKET(f, dip, socket) (*(f)->pcif_set_socket)(dip, socket)
#define	SET_IRQ(f, dip, handler) (*(f)->pcif_set_interrupt)(dip, handler)
#define	CLEAR_IRQ(f, dip, handler) (*(f)->pcif_clr_interrupt)(dip, handler)

typedef struct pcmcia_cs {
	ulong	pccs_magic;	/* magic number of verify correct structure */
	ulong	pccs_version;
	int   (*pccs_callback)();
	int   (*pccs_getconfig)();
} pcmcia_cs_t;

#define	PCCS_MAGIC	0x50434353
#define	PCCS_VERSION	_VERSION(2, 1)

/* properties used by the nexus for setup */
#define	ADAPT_PROP	"adapters"	/* property used to find adapter list */
#define	CS_PROP		"card-services"	/* property specifying Card Services */
#define	DEF_DRV_PROP	"default-driver" /* default driver to load if no CIS */

/*
 * per adapter structure
 * this structure defines everything necessary for the
 * the nexus to interact with the adapter specific driver
 */

struct pcmcia_adapter {
	int		pca_module;
	int		pca_unit;
	struct dev_ops	*pca_ops;
	dev_info_t	*pca_dip;
	pcmcia_if_t	*pca_if;
	void		*pca_power;
	ddi_iblock_cookie_t *pca_iblock;
	ddi_idevice_cookie_t *pca_idev;
	int		pca_numpower;
	int		pca_numsockets;
	char		pca_name[MODMAXNAMELEN];
};

typedef struct pcmcia_logical_socket {
	int			 ls_socket; /* adapter's socket number */
	struct pcmcia_adapter	*ls_adapter;
	pcmcia_if_t		*ls_if;
	ulong			 ls_status;
	dev_info_t		*ls_dip; /* currently associated driver */
	ulong			 ls_cs_events;
} pcmcia_logical_socket_t;

#define	PCS_CARD_PRESENT	0x0001 /* card in socket */
#define	PCS_MULTI_FUNCTION	0x0002 /* indicates dip is multifunction */

typedef struct pcmcia_logical_window {
	int			lw_window; /* window number */
	int			lw_socket; /* logical socket number assigned */
	struct pcmcia_adapter	*lw_adapter;
	pcmcia_if_t		*lw_if;
	ulong			lw_status;
	baseaddr_t		lw_base;
	int			lw_len;
} pcmcia_logical_window_t;

#define	PCS_ENABLED		0x0002 /* window is enabled */

/*
 * management interface hook
 */
#define	EM_EVENTSIZE	4
struct pcmcia_mif {
	struct pcmcia_mif *mif_next;
	void		(*mif_function)();
	u_long		  mif_id;
	u_char		  mif_events[EM_EVENTSIZE]; /* events registered for */
};

#define	PR_WORDSIZE	8	/* bits in word */
#define	PR_MASK		0x7
#define	PR_GET(map, bit)	(((u_char *)(map))[(bit)/PR_WORDSIZE] &\
					(1 << ((bit) & PR_MASK)))
#define	PR_SET(map, bit)	(((u_char *)(map))[(bit)/PR_WORDSIZE] |=\
					(1 << ((bit) & PR_MASK)))
#define	PR_CLEAR(map, bit)	(((u_char *)(map))[(bit)/PR_WORDSIZE] &=\
					~(1 << ((bit) & PR_MASK)))
#define	PR_ADDR(map, bit)	(((u_char *)(map)) + ((bit)/PR_WORDSIZE))
#define	PR_ZERO(map)		bzero((caddr_t)map, sizeof (map))

/* socket bit map */
typedef u_char socket_enum_t[PCMCIA_MAX_SOCKETS/PR_WORDSIZE];

#if defined(i86)
#define	PR_MAX_IO_LEN		128
#define	PR_MAX_IO_RANGES	8
#define	PR_MAX_MEM_LEN		32 /* pages or 128K bytes */
#define	PR_MAX_MEM_RANGES	4

struct pcmcia_resources {
	int pr_io_ranges;
	struct pr_io {
		int	pr_iobase;
		int	pr_iolen;
		u_char	pr_iomap[PR_MAX_IO_LEN/PR_WORDSIZE];
	} pr_io[PR_MAX_IO_RANGES];
	int pr_mem_ranges;
	struct pr_mem {
		caddr_t pr_membase;
		int	pr_mempages;
		u_char	pr_memmap[PR_MAX_MEM_LEN/PR_WORDSIZE];
	} pr_mem[PR_MAX_MEM_RANGES];
	u_int pr_irq;
	u_int pr_irq_valid;
	u_int pr_numirq;
};

#endif

/*
 * structures and definitions used in the private interface
 */

/* general values */
#define	PC_SUCCESS	1
#define	PC_FAILURE	0

/* set_mem() */
#define	PC_MEM_AM	0
#define	PC_MEM_CM	1

/* events for callback */
				/* card related events */
#define	PCE_CARD_REMOVAL	0 /* card removed */
#define	PCE_CARD_INSERT		1 /* card inserted */
#define	PCE_CARD_READY		2 /* ready state changed */
#define	PCE_CARD_BATTERY_WARN	3 /* battery is getting low */
#define	PCE_CARD_BATTERY_DEAD	4 /* battery is dead */
#define	PCE_CARD_STATUS_CHANGE	5 /* card status change for I/O card */
#define	PCE_CARD_WRITE_PROTECT	6 /* card write protect status change */
#define	PCE_CARD_RESET		7 /* client requested reset complete */
#define	PCE_CARD_UNLOCK		8 /* lock has been unlocked (opt) */
#define	PCE_CLIENT_INFO		9 /* someone wants client information */
#define	PCE_EJECTION_COMPLETE	10 /* Motor has finished ejecting card */
#define	PCE_EJECTION_REQUEST	11 /* request to eject card */
#define	PCE_ERASE_COMPLETE	12 /* a Flash Erase request completed */
#define	PCE_EXCLUSIVE_COMPLETE	13
#define	PCE_EXCLUSIVE_REQUEST	14
#define	PCE_INSERTION_COMPLETE	15
#define	PCE_INSERTION_REQUEST	16
#define	PCE_REGISTRATION_COMPLETE	17
#define	PCE_RESET_COMPLETE	18
#define	PCE_RESET_PHYSICAL	19
#define	PCE_RESET_REQUEST	20
#define	PCE_TIMER_EXPIRED	21

/* added for SPARC CPR support */
#define	PCE_PM_RESUME		22
#define	PCE_PM_SUSPEND		23

#define	PCE_DEV_IDENT		30 /* The nexus has identified the device */
#define	PCE_INIT_DEV		31 /* asking for a device */

#define	PCE_E2M(event)		(1 << (event))

/* event callback uses an indirect call -- make it look like a function */
#define	CS_EVENT(event, socket)	(*pcmcia_cs_event) (event, socket)

/* device classes */
#define	PCC_MULTI	0
#define	PCC_MEMORY	1
#define	PCC_SERIAL	2
#define	PCC_PARALLEL	3
#define	PCC_FIXED_DISK	4
#define	PCC_VIDEO	5
#define	PCC_LAN		6

/*
 * device information structure information
 * this is what is used for initial construction of a device node
 */

struct pcm_device_info {
	int	pd_socket;
	uint	pd_nodeid;
	int	pd_type;
	ulong	pd_handle;
	ulong	pd_tuples;
};

#define	PCM_DEFAULT_NODEID		(-1)
#define	PCM_DEV_MODEL	"model"
#define	PCM_DEV_ACTIVE	"card-active"
#define	PCM_DEV_SOCKET	"socket"
#define	PCM_DEV_TYPE	"r2card?"

typedef
struct init_dev {
	int	socket;
} init_dev_t;

/*
 * device descriptions
 * used to determine what driver to associate with a PC Card
 * so that automatic creation of device information trees can
 * be supported.
 */

typedef
struct pcm_device_node {
	struct pcm_device_node *pd_next;
	dev_info_t *pd_dip;	/* proto device info */
	char	pd_name[16];
	int	pd_flags;
	int	pd_devtype;	/* from device tuple */
	int	pd_funcid;
	int	pd_manfid;
	int	pd_manmask;
} pcm_dev_node_t;

#define	PCMD_DEVTYPE	0x0001	/* match device type */
#define	PCMD_FUNCID	0x0002	/* match function ID */
#define	PCMD_MANFID	0x0004	/* match manufacturer ID */
#define	PCMD_FUNCE	0x0008	/* match function extension */
#define	PCMD_VERS1	0x0010	/* match VERSION_1 string(s) */
#define	PCMD_JEDEC	0x0020	/* JEDEC ID */

#define	PCMDEV_PREFIX	"PC,"

/* property names */
#define	PCM_PROP_DEVICE	"device"
#define	PCM_PROP_FUNCID "funcid"

/* basic device types */

#define	PCM_TYPE_MULTI		0
#define	PCM_TYPE_MEMORY		1
#define	PCM_TYPE_SERIAL		2
#define	PCM_TYPE_PARALLEL	3
#define	PCM_TYPE_FIXED		4
#define	PCM_TYPE_VIDEO		5
#define	PCM_TYPE_LAN		6


typedef
struct string_to_int {
	char *sti_str;
	u_int sti_int;
} str_int_t;

/*
 * PCMCIA nexus/adapter specific ioctl commands
 */

#define	PCIOC	('P' << 8)
/* SS is temporary until design done */
#define	PC_SS_CMD(cmd)		(PCIOC|(cmd))

/* stuff that used to be in obpdefs.h but no longer */
#define	PCM_DEVICETYPE	"device_type"

#ifdef	__cplusplus
}
#endif

#endif	/* _PCMCIA_H */
