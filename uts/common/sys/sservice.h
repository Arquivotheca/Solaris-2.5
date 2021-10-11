/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef	_SSERVICE_H
#define	_SSERVICE_H

#pragma ident	"@(#)sservice.h	1.28	95/01/26 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

typedef int(f_tt)(int, ...);	/* for lint - cc -v quieting */

/*
 * identifiers for all SS functions implemented
 */
#define	SS_GetAdapter		0
#define	SS_GetPage		1
#define	SS_GetSocket		2
#define	SS_GetStatus		3
#define	SS_GetWindow		4
#define	SS_InquireAdapter	5
#define	SS_InquireSocket	6
#define	SS_InquireWindow	7
#define	SS_ResetSocket		8
#define	SS_SetPage		9
#define	SS_SetAdapter		10
#define	SS_SetSocket		11
#define	SS_SetWindow		12
#define	SS_SetIRQHandler	13
#define	SS_ClearIRQHandler	14
#define	CSGetActiveDip		98
#define	CSInitDev		99
#define	CSRegister		100
#define	CSCISInit		101
#define	CSUnregister		102

/*
 * XXX
 */
#define	CISGetAddress		103
#define	CISSetAddress		104
#define	CSCardRemoved		105
#define	CSGetCookiesAndDip	106

/*
 * returns a la Socket Services
 */

#define	SUCCESS		0x00
#define	BAD_ADAPTER		0x01
#define	BAD_ATTRIBUTE	0x02
#define	BAD_BASE		0x03
#define	BAD_EDC		0x04
#define	BAD_IRQ		0x06
#define	BAD_OFFSET		0x07
#define	BAD_PAGE		0x08
#define	READ_FAILURE		0x09
#define	BAD_SIZE		0x0a
#define	BAD_SOCKET		0x0b
#define	BAD_TYPE		0x0d
#define	BAD_VCC		0x0e
#define	BAD_VPP		0x0f
#define	BAD_WINDOW		0x11
#define	WRITE_FAILURE	0x12
#define	NO_CARD		0x14
#define	BAD_FUNCTION		0x15
#define	BAD_MODE		0x16
#define	BAD_SPEED		0x17
#define	BUSY			0x18
#define	NO_RESOURCE		0x20

/*
 * The following structure is to support CSRegister
 */
typedef struct csregister {
	ulong		cs_magic;		/* magic number */
	ulong		cs_version;		/* CS version number */
						/* CS entry point */
	int		(*cs_card_services)(int, ...);
						/* CS event entry point */
	f_tt		*cs_event;
} csregister_t;

/* GetAdapter(get_adapter_t) */

typedef struct get_adapter {
	unsigned	state;		/* adapter hardware state */
	irq_t		SCRouting;	/* status change IRQ routing */
} get_adapter_t;

/* IRQ definitions */
#define	IRQ_ENABLE	0x8000
#define	IRQ_HIGH	0x4000
#define	IRQ_PRIORITY	0x2000

/* GetPage(get_page_t) */

typedef struct get_page {
	unsigned	window;		/* window number */
	unsigned	page;		/* page number within window */
	unsigned	state;		/* page state: */
					/*
					 * PS_ATTRIBUTE
					 * PS_COMMON
					 * PS_IO (for DoRight?)
					 * PS_ENABLED
					 * PS_WP
					 */
	off_t		offset;		/* PC card's memory offset */
} get_page_t;

/*
 * PS flags
 */

#define	PS_ATTRIBUTE	0x01
#define	PS_ENABLED	0x02
#define	PS_WP		0x04
#define	PS_IO		0x08	/* needed? for DoRight */

/* GetSocket(get_socket_t) */

typedef struct get_socket {
	unsigned	socket;		/* socket number */
	unsigned	SCIntMask;	/* status change interrupt mask */
	unsigned	VccLevel;	/* VCC voltage in 1/10 volt */
	unsigned	Vpp1Level;	/* VPP1 voltage in 1/10 volt */
	unsigned	Vpp2Level;	/* VPP2 voltage in 1/10 volt */
	unsigned	state;		/* latched status change signals */
	unsigned	CtlInd;		/* controls and indicators */
	irq_t		IRQRouting;	/* I/O IRQ routing */
	unsigned	IFType;		/* memory-only or memory & I/O */
} get_socket_t;

/* GetStatus(get_ss_status_t) */

typedef struct get_ss_status {
	unsigned	socket;		/* socket number */
	unsigned	CardState;	/* real-time card state */
	unsigned	SocketState;	/* latched status change signals */
	unsigned	CtlInd;		/* controls and indicators */
	irq_t		IRQRouting;	/* I/O IRQ routing */
	unsigned	IFType;		/* memory-only or memory & I/O */
} get_ss_status_t;

/*
 * Socket specific flags and capabilities
 */

#define	SBM_WP		0x01
#define	SBM_LOCKED	0x02
#define	SBM_EJECT	0x04
#define	SBM_INSERT	0x08
#define	SBM_BVD1	0x10
#define	SBM_BVD2	0x20
#define	SBM_RDYBSY	0x40
#define	SBM_CD		0x80

				/* capabilities only */
#define	SBM_LOCK	0x10
#define	SBM_BATT	0x20
#define	SBM_BUSY	0x40
#define	SBM_XID		0x80

/* GetWindow(get_window_t) */
typedef unsigned long speed_t;	/* memory speed in nanoseconds */

typedef struct get_window {
	unsigned	window;		/* window number */
	unsigned	socket;		/* socket this window is assigned to */
	unsigned	size;		/* size in bytes */
	unsigned	state;		/* current state of window hardware */
	speed_t		speed;		/* speed in nanoseconds */
	baseaddr_t	base;		/* base address in host address map */
} get_window_t;

/*
 * window flags (state and capabilities)
 */

#define	WS_IO		0x01
#define	WS_ENABLED	0x02
#define	WS_16BIT	0x04
#define	WS_PAGED	0x80
#define	WS_EISA		0x10
#define	WS_CENABLE	0x20
#define	WS_EXACT_MAPIN	0x40	/* map exactly what's asked for */

/* Inquire Adapter(inquire_adapter_t) */

typedef struct inquire_adapter {
	unsigned	NumSockets;	/* number of sockets */
	unsigned	NumWindows;	/* number of windows */
	unsigned	NumEDCs;	/* number of EDCs */

	unsigned	AdpCaps;	/* adapter power capabilities */
	irq_t		ActiveHigh;	/* active high status change IRQ */
	irq_t		ActiveLow;	/* active low status change IRQ */
	int		NumPower;	/* number of power entries */
	struct power_entry {
		unsigned	PowerLevel;	/* voltage in 1/10 volt */
		unsigned	ValidSignals;	/* voltage is valid for: */
						/*
						 * VCC
						 * VPP1
						 * VPP2
						 * if none are set, this is end
						 * of list
						 */
	} *power_entry;
} inquire_adapter_t;

#define	VCC	0x80
#define	VPP1	0x40
#define	VPP2	0x20
#define	V_MASK	(VCC|VPP1|VPP2)

/* InquireSocket(inquire_socket_t) */

typedef struct inquire_socket {
	unsigned	socket;		/* socket number */
	unsigned	SCIntCaps;	/* status change interrupt events */
	unsigned	SCRptCaps;	/* reportable status change events */
	unsigned	CtlIndCaps;	/* controls and indicators */
	unsigned	SocketCaps;	/* socket capabilities */
	irq_t		ActiveHigh;	/* active high status change IRQ */
	irq_t		ActiveLow;	/* active low status change IRQ */
} inquire_socket_t;

/* InquireWindow(inquire_window_t) */

typedef struct memwin_char {
	unsigned	MemWndCaps;	/* memory window characteristcs */
	baseaddr_t	FirstByte;	/* first byte in host space */
	baseaddr_t	LastByte;	/* last byte in host space */
	unsigned	MinSize;	/* minimum window size */
	unsigned	MaxSize;	/* maximum window size */
	unsigned	ReqGran;	/* window size constraints */
	unsigned	ReqBase;	/* base address alignment boundry */
	unsigned	ReqOffset;	/* offset alignment boundry */
	unsigned	Slowest;	/* slowest speed in nanoseconds */
	unsigned	Fastest;	/* fastest speed in nanoseconds */
} mem_win_char_t;

typedef struct iowin_char {
	unsigned	IOWndCaps;	/* I/O window characteristcs */
	baseaddr_t	FirstByte;	/* first byte in host space */
	baseaddr_t	LastByte;	/* last byte in host space */
	unsigned	MinSize;	/* minimum window size */
	unsigned	MaxSize;	/* maximum window size */
	unsigned	ReqGran;	/* window size constraints */
	unsigned	AddrLines;	/* number of address lines decoded */
	unsigned	EISASlot;	/* EISA I/O address decoding */
} iowin_char_t;

typedef struct inquire_window {
	unsigned	window;		/* window number */
	unsigned	WndCaps;	/* window capabilities */
	socket_enum_t	Sockets;	/* window<->socket assignment mask */
	/* note that we always declare both forms */
	mem_win_char_t	mem_win_char;
	iowin_char_t	iowin_char;
} inquire_window_t;


/* interface definitions */
#define	IF_IO		0x01
#define	IF_MEMORY	0x02


#define	PC_PAGESIZE	0x4000	/* 16K page size */

/* window capabilities */
				/* generic */
#define	WC_IO		0x0004
#define	WC_WAIT		0x0080
#define	WC_COMMON	0x0001
#define	WC_ATTRIBUTE	0x0002
				/* I/O and memory */
#define	WC_BASE		0x0001
#define	WC_SIZE		0x0002
#define	WC_WENABLE	0x0004
#define	WC_8BIT		0x0008
#define	WC_16BIT	0x0010
#define	WC_BALIGN	0x0020
#define	WC_POW2		0x0040
				/* memory only */
#define	WC_CALIGN	0x0080
#define	WC_PAVAIL	0x0100
#define	WC_PSHARED	0x0200
#define	WC_PENABLE	0x0400
#define	WC_WP		0x0800
				/* I/O only */
#define	WC_INPACK	0x0080
#define	WC_EISA		0x0100
#define	WC_CENABLE	0x0200
				/* Solaris/SPARC */
#define	WC_IO_RANGE_PER_WINDOW	0x8000 /* I/O range unique for each window */

/* SetPage(set_page_t *) */
typedef struct set_page {
	unsigned	window;	/* window number */
	unsigned	page;	/* page number */
	unsigned	state;	/* page state */
	off_t		offset;	/* offset in PC card space */
} set_page_t;

/* SetSocket(set_socket_t) */

typedef struct set_socket {
	unsigned	socket;	/* socket number */
	unsigned	SCIntMask; /* status change enables */
	unsigned	VccLevel; /* Vcc power index level */
	unsigned	Vpp1Level; /* Vpp1 power index level */
	unsigned	Vpp2Level; /* Vpp2 power index level */
	unsigned	State;
	unsigned	CtlInd;	/* control and indicator bits */
	irq_t		IREQRouting; /* I/O IRQ routing */
	unsigned	IFType;	/* interface type (mem/IO) */
} set_socket_t;

/* SetIRQHandler(set_irq_handler_t) */

typedef struct set_irq_handler {
	unsigned	socket;	/* associate with a socket for now */
	unsigned	irq;
	unsigned	priority;
	unsigned	handler_id;	/* ID of this client's handler */
	f_tt		*handler;	/* client IO IRQ handler entry point */
	void		*arg;		/* arg to call client handler with */
	ddi_iblock_cookie_t	*iblk_cookie;	/* iblk cookie pointer */
	ddi_idevice_cookie_t	*idev_cookie;	/* idev cookie pointer */
} set_irq_handler_t;

#define	IRQ_ANY		0x0

/* interrupt priority levels */
#define	PRIORITY_LOW	0x00
#define	PRIORITY_HIGH	0x10

/* ClearIRQHandler(clear_irq_handler_t) */

typedef struct clear_irq_handler {
	unsigned	socket;
	unsigned	handler_id;	/* client handler ID to remove */
	f_tt		*handler;	/* client IO IRQ handler entry point */
} clear_irq_handler_t;

/* SetWindow(set_window_t) */

typedef struct set_window {
	unsigned	window;			/* window number */
	unsigned	socket;			/* socket number */
	unsigned	WindowSize;		/* window size in bytes */
	unsigned	state;			/* window state */
	unsigned	speed;			/* window speed, nanoseconds */
	caddr_t		base;			/* base addr in host space */
} set_window_t;

/* CSInitDev */
typedef
struct ss_make_device_node {
	u_long		flags;		/* operation flags */
	dev_info_t	*dip;		/* dip for this client */
	char		*name;		/* device node path and name */
	char		*slot;		/* slot name string */
	char		*busaddr;	/* bus addr name string */
	int		spec_type;	/* dev special type (block/char) */
	int		minor_num;	/* device node minor number */
	char		*node_type;	/* device node type */
} ss_make_device_node_t;

#define	SS_CSINITDEV_CREATE_DEVICE	0x01	/* create device node */
#define	SS_CSINITDEV_REMOVE_DEVICE	0x02	/* remove device node */
#define	SS_CSINITDEV_USE_SLOT		0x04	/* use slot name from caller */
#define	SS_CSINITDEV_USE_BUSADDR	0x08	/* use bus addr from caller */
#define	SS_CSINITDEV_MORE_DEVICES	0x10	/* send PCE_INIT_DEV */
#define	SS_CSINITDEV_SEND_DEV_EVENT	0x10	/* send PCE_INIT_DEV */

/* CSGetCookiesAndDip */
typedef struct get_cookies_and_dip_t {
	unsigned		socket;		/* socket number */
	dev_info_t		*dip;		/* adapter instance dip */
	ddi_iblock_cookie_t	*iblock;	/* for event handler */
	ddi_idevice_cookie_t	*idevice;	/* for event handler */
} get_cookies_and_dip_t;

/* ResetSocket */
#define	RESET_MODE_FULL		0 /* Reset to SocketServices Specification */
#define	RESET_MODE_CARD_ONLY	1 /* only reset the card itself */

/* union of all exported functions functions */
typedef
union sservice {
	get_adapter_t	get_adapter;
	get_page_t	get_page;
	get_socket_t	get_socket;
	get_window_t	get_window;
	get_ss_status_t	get_ss_status;
	inquire_adapter_t	inquire_adapter;
	inquire_socket_t	inquire_socket;
	inquire_window_t	inquire_window;
	set_page_t	set_page;
	set_socket_t	set_socket;
	set_irq_handler_t	set_irq_handler;
	set_window_t	set_window;
	get_cookies_and_dip_t get_cookies;
	ss_make_device_node_t make_device;
} sservice_t;

/* event manager structures */
struct pcm_make_dev {
	int	socket;
	int	flags;
	int	op;
	dev_t	dev;
	int	type;
	char	path[MAXPATHLEN];
};

#define	PCM_EVENT_MORE		0x0001	/* more events of this type coming */

/* Entrypoint for SocketServices users */
int SocketServices(int, ...);

#ifdef	__cplusplus
}
#endif

#endif	/* _SSERVICE_H */
