/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _CS_H
#define	_CS_H

#pragma ident	"@(#)cs.h	1.40	95/04/24 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * PCMCIA Card Services header file
 */

/*
 * XXX - This define really should be in a global header file
 *	somewhere; we do this stunt here since a lot of
 *	people include this header file but not necessarily
 *	the header file in which this is defined.
 */
#ifndef	_VERSION
#define	_VERSION(major, minor)	((major)<<16|(minor))
#endif

/*
 * Define this version of CS - this should correspond to the PCMCIA
 *	version number specified in the PCMCIA standard.
 * SS checks this version number as all CS clients should.
 */
#define	CS_VERSION	_VERSION(2, 1)

/*
 * typedef for function pointers to quiet lint and cc -v
 */
typedef	int (csfunction_t)(int, ...);	/* for lint - cc -v quieting */

/*
 * Card Services driver initialization macro - we require drivers to call
 *	this macro in their xxx_attach routines to setup a global entry
 *	point to Card Services.  Once CS is part of the base kernel, this
 *	macro and the global entry point will go away.
 * It is expected that the driver will do something like this in their
 *	xxx_attach code:
 *
 *		if (!CardServicesInit)
 *		    {error handling and fail attach}
 *
 * XXX - we should really use the CS_PROP define from Socket Services
 */
#define	CardServicesInit(dip)				\
	(cardservices = (csfunction_t *)ddi_getprop(	\
			DDI_DEV_T_ANY, dip,		\
			(DDI_PROP_CANSLEEP |		\
				DDI_PROP_NOTPROM),	\
			"card-services", NULL))
/*
 * Card Services macro - we use this so that calls to CS look like
 *	regular function calls; that way when the kernel finally has
 *	a CardServices symbol, very littel driver code will have to
 *	change.
 */
#define	CardServices	(*cardservices)

/*
 * Card Services function identifiers - these correspond to the PCMCIA
 *	standard function codes for CS with the exception of a few
 *	private and implementation-specific function identifiers.
 *
 * client services functions
 */
#define	GetCardServicesInfo		0x000b
#define	RegisterClient			0x0010
#define	DeregisterClient		0x0002
#define	GetStatus			0x000c
#define	ResetCard			0x0011
#define	SetEventMask			0x0031
#define	GetEventMask			0x002e
/*
 * reource management functions
 */
#define	RequestIO			0x001f
#define	ReleaseIO			0x001b
#define	RequestIRQ			0x0020
#define	ReleaseIRQ			0x001c
#define	RequestWindow			0x0021
#define	ReleaseWindow			0x001d
#define	ModifyWindow			0x0017
#define	MapMemPage			0x0014
#define	RequestSocketMask		0x0022
#define	ReleaseSocketMask		0x002f
#define	RequestConfiguration		0x0030
#define	GetConfigurationInfo		0x0004
#define	ModifyConfiguration		0x0027
#define	ReleaseConfiguration		0x001e
#define	AccessConfigurationRegister	0x0036
/*
 * bulk memory service functions
 */
#define	OpenMemory			0x0018
#define	ReadMemory			0x0019
#define	WriteMemory			0x0024
#define	CopyMemory			0x0001
#define	RegisterEraseQueue		0x000f
#define	CheckEraseQueue			0x0026
#define	DeregisterEraseQueue		0x0025
#define	CloseMemory			0x0000
/*
 * client utility functions
 */
#define	GetFirstTuple			0x0007
#define	GetNextTuple			0x000a
#define	GetTupleData			0x000d
#define	GetFirstRegion			0x0006
#define	GetNextRegion			0x0009
#define	GetFirstPartition		0x0005
#define	GetNextPartition		0x0008
/*
 * advanced client services functions
 */
#define	ReturnSSEntry			0x0023
#define	MapLogSocket			0x0012
#define	MapPhySocket			0x0015
#define	MapLogWindow			0x0013
#define	MapPhyWindow			0x0016
#define	RegisterMTD			0x001a
#define	RegisterTimer			0x0028
#define	SetRegion			0x0029
#define	ValidateCIS			0x002b
#define	RequestExclusive		0x002c
#define	ReleaseExclusive		0x002d
#define	GetFirstClient			0x000e
#define	GetNextClient			0x002a
#define	GetClientInfo			0x0003
#define	AddSocketServices		0x0032
#define	ReplaceSocketServices		0x0033
#define	VendorSpecific			0x0034
#define	AdjustResourceInfo		0x0035
/*
 * private functions - clients should never call these; if they do,
 *	the system will esplode.
 */
#define	CISRegister			0x1000
#define	CISUnregister			0x1001
/*
 * Card Services functions specific to this implementation
 */
#define	ParseTuple		0x2000	/* parses contents of tuples */
#define	MakeDeviceNode		0x2001	/* makes device nodes in fs */
#define	ConvertSpeed		0x2002	/* converts device speeds */
#define	ConvertSize		0x2003	/* converts device sizes */
#define	Event2Text		0x2004	/* return string of event type */
#define	Function2Text		0x2005	/* function or ret code string */
#define	CS_DDI_Info		0x2006	/* set/get DDI info */
#define	CSFuncListEnd		0x8000	/* end of CS function list */

/*
 * Return codes from Card Services - these correspond to the PCMCIA
 *	standard and also include some implementation-specific return
 *	codes.
 */
#define	CS_SUCCESS		0x00	/* Request succeeded */
#define	CS_BAD_ADAPTER		0x01	/* Specified adapter is invalid */
#define	CS_BAD_ATTRIBUTE	0x02	/* Bad attribute value */
#define	CS_BAD_BASE		0x03	/* System base address invalid */
#define	CS_BAD_EDC		0x04	/* EDC generator is invalid */
	/* RESERVED - 0x05 */
#define	CS_BAD_IRQ		0x06	/* Invalid IRQ */
#define	CS_BAD_OFFSET		0x07	/* Card offset invalid */
#define	CS_BAD_PAGE		0x08	/* Card page invalid */
#define	CS_READ_FAILURE		0x09	/* Unable to complete read request */
#define	CS_BAD_SIZE		0x0a	/* Size is invalid */
#define	CS_BAD_SOCKET		0x0b	/* Specified socket is invalid */
	/* RESERVED - 0x0c */
#define	CS_BAD_TYPE		0x0d	/* Window/interface type invalid */
#define	CS_BAD_VCC		0x0e	/* Vcc value/index invalid */
#define	CS_BAD_VPP		0x0f	/* Vpp value/index invalid */
#define	CS_BAD_WINDOW		0x11	/* Specified window is invalid */
#define	CS_WRITE_FAILURE	0x12	/* Unable to complete write request */
	/* RESERVED - 0x13 */
#define	CS_NO_CARD		0x14	/* No PC card in socket */
#define	CS_UNSUPPORTED_FUNCTION	0x15	/* Unsupported function */
#define	CS_UNSUPPORTED_MODE	0x16	/* Unsupported processor mode */
#define	CS_BAD_SPEED		0x17	/* Specified speed is unavailable */
#define	CS_BUSY			0x18	/* CS is busy - try again later */
#define	CS_GENERAL_FAILURE	0x19	/* Undefined error */
#define	CS_WRITE_PROTECTED	0x1a	/* Media is write protected */
#define	CS_BAD_ARG_LENGTH	0x1b	/* Arg length invalid */
#define	CS_BAD_ARGS		0x1c	/* Arg values invalid */
#define	CS_CONFIGURATION_LOCKED	0x1d	/* This configuration is locked */
#define	CS_IN_USE		0x1e	/* Requested resource in use */
#define	CS_NO_MORE_ITEMS	0x1f	/* No more of requested item */
#define	CS_OUT_OF_RESOURCE	0x20	/* Internal CS resources exhausted */
#define	CS_BAD_HANDLE		0x21	/* client or window handle invalid */

/*
 * The following are Solaris-specific extended return codes
 */
#define	CS_NO_CIS		0x80	/* No CIS on card */
#define	CS_BAD_CIS		0x81	/* Bad CIS on card */
#define	CS_UNKNOWN_TUPLE	0x82	/* unknown tuple */
#define	CS_BAD_VERSION		0x83	/* bad CS version */
#define	CS_UNSUPPORTED_EVENT	0x84	/* Unsupported event in client */
#define	CS_ERRORLIST_END	0x8000	/* end of error list */

/*
 * Card Services event codes - these do NOT correspond to the PCMCIA
 *	standard event codes for CS since these events are encoded as
 *	bit flags, while the PCMCIA standard event codes are encoded
 *	as numerical values.  In practice, this shouldn't be a problem
 *	since no one should be looking at the absolute value of the
 *	event codes; these defines should be used.
 *
 * The numerical value of an event code determines in what order a client
 *	will receive the event if other events are also pending for that
 *	client. XXX - need to make event_t a 64-bit field.
 *
 * Card Services receives these events from Socket Services or by reading
 *	the card's Pin Replacement Register.  In either case, the client
 *	always gets the same type of notification.
 */
#define	CS_EVENT_REGISTRATION_COMPLETE	0x00000001 /* 0x82 */
#define	CS_EVENT_PM_RESUME		0x00000002 /* 0x05 */
#define	CS_EVENT_CARD_INSERTION		0x00000004 /* 0x0c */
#define	CS_EVENT_CARD_READY		0x00000008 /* 0x01 */
#define	CS_EVENT_BATTERY_LOW		0x00000010 /* 0x02 is also BVD2 */
#define	CS_EVENT_BATTERY_DEAD		0x00000020 /* 0x40 is also BVD1 */
#define	CS_EVENT_CARD_LOCK		0x00000040 /* 0x03 */
#define	CS_EVENT_PM_SUSPEND		0x00000080 /* 0x04 */
#define	CS_EVENT_CARD_RESET		0x00000100 /* 0x11 */
#define	CS_EVENT_CARD_UNLOCK		0x00000200 /* 0x06 */
#define	CS_EVENT_EJECTION_COMPLETE	0x00000400 /* 0x07 */
#define	CS_EVENT_EJECTION_REQUEST	0x00000800 /* 0x08 */
#define	CS_EVENT_ERASE_COMPLETE		0x00001000 /* 0x81 */
#define	CS_EVENT_EXCLUSIVE_COMPLETE	0x00002000 /* 0x0d */
#define	CS_EVENT_EXCLUSIVE_REQUEST	0x00004000 /* 0x0e */
#define	CS_EVENT_INSERTION_COMPLETE	0x00008000 /* 0x09 */
#define	CS_EVENT_INSERTION_REQUEST	0x00010000 /* 0x0a */
#define	CS_EVENT_RESET_COMPLETE		0x00020000 /* 0x80 */
#define	CS_EVENT_RESET_PHYSICAL		0x00040000 /* 0x0f */
#define	CS_EVENT_RESET_REQUEST		0x00080000 /* 0x10 */
#define	CS_EVENT_MTD_REQUEST		0x00100000 /* 0x12 */
#define	CS_EVENT_CLIENT_INFO		0x00200000 /* 0x14 */
#define	CS_EVENT_TIMER_EXPIRED		0x00400000 /* 0x15 */
#define	CS_EVENT_WRITE_PROTECT		0x01000000 /* 0x17 */

/*
 * The CS_EVENT_SS_UPDATED event is generated when Socket Services
 *	has completed parsing the CIS and has done any necessary
 *	work to get the client driver loaded and attached.
 */
#define	CS_EVENT_SS_UPDATED		0x00800000 /* 0x16 */

/*
 * The CS_EVENT_STATUS_CHANGE event is generated by a Socket Services
 *	PCE_CARD_STATUS_CHANGE event; this event gets converted to
 *	the appropriate Card Services events when Card Services
 *	reads the PRR.
 */
#define	CS_EVENT_STATUS_CHANGE		0x02000000 /* ?? */

/*
 * The CS_EVENT_CARD_REMOVAL is the last "real" CS event and must
 *	have the highest value of all "real" CS events so that this
 *	event is handed to the client after all other queued events
 *	have been processed.
 * If the client has set the CS_EVENT_CARD_REMOVAL_LOWP flag in
 *	either of their event masks, then they will also receive
 *	a CS_EVENT_CARD_REMOVAL at low (cs_event_thread) priority;
 *	in this low priority removal event, the client can call
 *	many CS functions that they can't call when they recieve
 *	the high priority removal event.
 */
#define	CS_EVENT_CARD_REMOVAL		0x10000000 /* 0x0b */
#define	CS_EVENT_CARD_REMOVAL_LOWP	0x20000000 /* ?? */
/*
 * The following are not events but they share the event flags field
 *	and are used internally by CS.  These bit patterns will never
 *	be seen by clients.  If a client sets any of these bits in
 *	their event masks, the system will panic.
 */
#define	CS_EVENT_ALL_CLIENTS		0x40000000 /* ?? */
#define	CS_EVENT_READY_TIMEOUT		0x80000000 /* ?? */

/*
 * The client event callback argument structure - this is passed in to
 *	the client event handler.  MOst of these arguments are identical
 *	to the PCMCIA-specified arguments.
 */
typedef struct event_callback_args_t {
	client_handle_t	client_handle;
	void		*info;
	void		*mtdrequest;
	void		*buffer;
	void		*misc;
	void		*client_data;
} event_callback_args_t;

/*
 * Event priority flag passed to the client's event handler; the client
 *	uses this priority to determine which mutex to use.
 */
#define	CS_EVENT_PRI_LOW	0x0001
#define	CS_EVENT_PRI_HIGH	0x0002
#define	CS_EVENT_PRI_NONE	0x0004

/*
 * io_req_t structure used for RequestIO and ReleaseIO
 */
typedef struct io_req_t {
	void	*BasePort1;	/* first IO range base address or port num */
	u_long	NumPorts1;	/* first IO range number contiguous ports */
	u_long	Attributes1;	/* first IO range attributes */
	void	*BasePort2;	/* second IO range base address or port num */
	u_long	NumPorts2;	/* second IO range number contiguous ports */
	u_long	Attributes2;	/* second IO range attributes */
	u_long	IOAddrLines;	/* number of IO address lines decoded */
} io_req_t;
/*
 * Flags for RequestIO and ReleaseIO
 */
#define	IO_DATA_PATH_WIDTH	0x00000001	/* 16 bit data path */
#define	IO_DATA_PATH_WIDTH_8	0x00000000	/* 8 bit data path */
#define	IO_DATA_PATH_WIDTH_16	0x00000001	/* 16 bit data path */
/*
 * The following flags are included for compatability with other versions of
 *	Card Services, but they are not implemented in this version.  They
 *	are assigned values as placeholders only.  If any of these flags
 *	are set on a call to RequestIO, CS_BAD_ATTRIBUTE is returned.
 */
#define	IO_SHARED		0x00010000	/* for compatability only */
#define	IO_FIRST_SHARED		0x00020000	/* for compatability only */
#define	IO_FORCE_ALIAS_ACCESS	0x00040000	/* for compatability only */
/*
 * The following flags are private to Card Services and should never be set
 *	by a client.  Doing so will cause the system to take a supervisor
 *	trap at level twenty-nine.
 */
#define	IO_DEALLOCATE_WINDOW	0x10000000	/* CS private */
#define	IO_DISABLE_WINDOW	0x20000000	/* CS private */

/*
 * client_reg_t structure for RegisterClient
 */
typedef struct client_reg_t {
	ddi_iblock_cookie_t	*iblk_cookie;	/* event iblk cookie */
	ddi_idevice_cookie_t	*idev_cookie;	/* event idev cookie */
	u_long			Attributes;
	u_long			EventMask;
	csfunction_t		*event_handler;
	event_callback_args_t	event_callback_args;
	u_long			Version;	/* CS version to expect */
	/* DDI support */
	dev_info_t		*dip;		/* client's dip */
	char			*driver_name;	/* client's driver name */
	/* CS private */
	void			*private;	/* CS private data */
} client_reg_t;

/*
 * Flags for RegisterClient - some of these flags are also used internally
 *	by CS to sequence the order of event callbacks and to allow Socket
 *	Services to register as a "super" client.
 *
 * The client_reg_t->Attributes structure member uses these flags.
 *
 * The client_types_t->type and client_t->flags structure members use these
 *	flags as well.
 *
 * Client types - mutually exclusive.
 */
#define	INFO_SOCKET_SERVICES	0x00000001
#define	INFO_IO_CLIENT		0x00000002
#define	INFO_MTD_CLIENT		0x00000004
#define	INFO_MEM_CLIENT		0x00000008
#define	MAX_CLIENT_TYPES	3	/* doesn't include SS client type */

/*
 * The following two are for backwards-compatability with the PCMCIA spec.
 *	We will give the client CARD_INSERTION and REGISTRATION_COMPLETE
 *	if either of these two bits are set.  Normally, all IO and MEM
 *	clients should set both of these bits.
 */
#define	INFO_CARD_SHARE		0x00000010
#define	INFO_CARD_EXCL		0x00000020

/*
 * tuple_t struct used for GetFirstTuple, GetNextTuple, and ParseTuple
 *
 * Note that the values for DesiredTuple are defined in the cis.h header
 *	file.
 */
typedef struct tuple_t {
	u_long		Socket;		/* socket number to get tuple from */
	u_long		Attributes;	/* tuple return attributes */
	cisdata_t	DesiredTuple;	/* tuple to search for or flags */
	u_long		Flags;		/* CS private */
	cistpl_t	*CIS_Data;	/* CS private */
	cisdata_t	TupleCode;	/* tuple type code */
	cisdata_t	TupleLink;	/* tuple data body size */
	cisdata_t	TupleOffset;	/* offset in tuple data body */
	cisdata_t	TupleDataMax;	/* max size of tuple data area */
	cisdata_t	TupleDataLen;	/* actual size of tuple data area */
	cisdata_t	*TupleData;	/* pointer to tuple body data */
} tuple_t;

/*
 * Flags for CS tuple functions
 */
#define	TUPLE_RETURN_LINK		0x00000002

/*
 * cisinfo_t structure used for ValidateCIS
 */
typedef struct cisinfo_t {
	u_long		Socket;		/* socket number to validate CIS on */
	long		Chains;		/* number of tuple chains in CIS */
	long		Tuples;		/* total number of tuples in CIS */
} cisinfo_t;

/*
 * win_req_t structure used for RequestWindow
 *
 * Note that the ReqOffset member is not defined in the current PCMCIA
 *	spec but is included here to aid clients in determining the
 *	optimum offset to give to MapMemPage.
 */
typedef struct win_req_t {
	u_long		Attributes;	/* window flags */
	caddr_t		Base;		/* returned VA for base of window */
	u_long		Size;		/* window size requested/granted */
	u_long		AccessSpeed;	/* window access speed */
	u_long		ReqOffset;	/* required window offest */
} win_req_t;

/*
 * modify_win_t structure used for ModifyWindow
 */
typedef struct modify_win_t {
	u_long		Attributes;	/* window flags */
	u_long		AccessSpeed;	/* window access speed */
} modify_win_t;

/*
 * Flags for RequestWindow and ModifyWindow
 */
#define	WIN_MEMORY_TYPE		0x00000001	/* window points to AM */
#define	WIN_MEMORY_TYPE_CM	0x00000000	/* window points to CM */
#define	WIN_MEMORY_TYPE_AM	0x00000001	/* window points to AM */
#define	WIN_DATA_WIDTH		0x00000002	/* 16-bit data path */
#define	WIN_DATA_WIDTH_8	0x00000000	/* 8-bit data path */
#define	WIN_DATA_WIDTH_16	0x00000002	/* 16-bit data path */
#define	WIN_ENABLE		0x00000004	/* enable/disable window */
#define	WIN_OFFSET_SIZE		0x00000008	/* card offsets window sized */
#define	WIN_ACCESS_SPEED_VALID	0x00000010	/* speed valid (ModifyWindow) */
#define	WIN_DATA_PATH_VALID	0x00000020	/* data path valid */
/*
 * The following flags are included for compatability with other versions of
 *	Card Services, but they are not implemented in this version.  They
 *	are assigned values as placeholders only.  If any of these flags
 *	are set on a call to RequestWindow, CS_BAD_ATTRIBUTE is returned.
 */
#define	WIN_PAGED		0x00010000	/* for compatability only */
#define	WIN_SHARED		0x00020000	/* for compatability only */
#define	WIN_FIRST_SHARED	0x00040000	/* for compatability only */
#define	WIN_BINDING_SPECIFIC	0x00080000	/* for compatability only */

/*
 * The following flag is actually part of the AccessSpeed member
 */
#define	WIN_USE_WAIT		0x80	/* use window that supports WAIT */

/*
 * map_mem_page_t structure used for MapMemPage
 */
typedef struct map_mem_page_t {
	u_long		Offset;		/* card offset */
	u_long		Page;		/* page number */
} map_mem_page_t;

/*
 * sockevent_t structure used for RequestSocketMask, GetEventMask
 *	and SetEventMask
 */
typedef struct sockevent_t {
	u_long		EventMask;	/* event mask to set or return */
	u_long		Attributes;	/* attribute flags for call */
	client_handle_t	ClientHandle;	/* client handle if necessary */
	u_long		Socket;		/* socket number if necessary */
} sockevent_t;

/*
 * Flags for GetEventMask and SetEventMask
 */
#define	CONF_EVENT_MASK_GLOBAL	0x00000000	/* global event mask */
#define	CONF_EVENT_MASK_CLIENT	0x00000001	/* client event mask */
#define	CONF_EVENT_MASK_VALID	0x00000001	/* client event mask */

/*
 * convert_speed_t structure used for ConvertSpeed
 */
typedef struct convert_speed_t {
	u_long		Attributes;
	u_long		nS;
	u_long		devspeed;
} convert_speed_t;

/*
 * Flags for ConvertSpeed
 */
#define	CONVERT_NS_TO_DEVSPEED	0x00000001
#define	CONVERT_DEVSPEED_TO_NS	0x00000002

/*
 * convert_size_t structure used for ConvertSize
 */
typedef struct convert_size_t {
	u_long		Attributes;
	u_long		bytes;
	u_long		devsize;
} convert_size_t;

/*
 * Flags for ConvertSize
 */
#define	CONVERT_BYTES_TO_DEVSIZE	0x00000001
#define	CONVERT_DEVSIZE_TO_BYTES	0x00000002

/*
 * event2text_t structure used for Event2Text
 */
typedef struct event2text_t {
	u_long		Attributes;
	u_long		BufSize;	/* size of text buffer */
	event_t		event;		/* events */
	char		*text;		/* buffer to return text strings */
} event2text_t;

/*
 * GET_CS_EVENT_BUFSIZE is used by callers to Event2Text to get
 *	the required text buffer size.
 */
#define	CONVERT_EVENT_TO_TEXT	0x0001	/* convert passed event(s) to text */
#define	GET_CS_EVENT_BUFSIZE	0x0002	/* return required buffer size */
#define	GET_MULTI_EVENT_BUFSIZE	0x0004	/* multi-event buffer size */

/*
 * cs_csfunc2text_strings_t structure used for Function2Text
 */
typedef struct cs_csfunc2text_strings_t {
	int		item;
	char		*text;
} cs_csfunc2text_strings_t;

/*
 * get_status_t structure used for GetStatus
 *
 * The values in the status members are the same as the CS_EVENT_XXX values.
 */
typedef struct get_status_t {
	u_long		CardState;	/* "live" card status for this client */
	u_long		SocketState;	/* latched socket values */
	u_long		raw_CardState;	/* raw live card status */
} get_status_t;

/*
 * map_log_socket_t structure used for MapLogSocket
 */
typedef struct map_log_socket_t {
	u_long		Socket;		/* physical socket */
} map_log_socket_t;

/*
 * irq_req_t structure used for RequestIRQ and ReleaseIRQ
 */
typedef struct irq_req_t {
	u_long			Attributes;	/* IRQ attribute flags */
	csfunction_t		*irq_handler;
	caddr_t			irq_handler_arg;
	ddi_iblock_cookie_t	*iblk_cookie;	/* event iblk cookie */
	ddi_idevice_cookie_t	*idev_cookie;	/* event idev cookie */
} irq_req_t;

/*
 * Flags for RequestIRQ and ReleaseIRQ
 */
#define	IRQ_PRIORITY_LOW		0x00000000
#define	IRQ_PRIORITY_HIGH		0x00000001
#define	IRQ_TYPE_EXCLUSIVE		0x00000002
/*
 * The following flags are included for compatability with other versions of
 *	Card Services, but they are not implemented in this version.  They
 *	are assigned values as placeholders only.  If any of these flags
 *	are set on a call to RequestIRQ, CS_BAD_ATTRIBUTE is returned.
 */
#define	IRQ_FORCED_PULSE		0x00010000
#define	IRQ_TYPE_TIME			0x00020000
#define	IRQ_TYPE_DYNAMIC_SHARING	0x00040000
#define	IRQ_FIRST_SHARED		0x00080000
#define	IRQ_PULSE_ALLOCATED		0x00100000

/*
 * config_req_t structure used for RequestConfiguration
 */
typedef struct config_req_t {
	u_long		Attributes;	/* configuration attributes */
	u_long		Vcc;		/* Vcc value */
	u_long		Vpp1;		/* Vpp1 value */
	u_long		Vpp2;		/* Vpp2 value */
	u_long		IntType;	/* socket interface type - mem or IO */
	u_long		ConfigBase;	/* offset from start of AM space */
	u_long		Status;		/* value to write to STATUS register */
	u_long		Pin;		/* value to write to PRR */
	u_long		Copy;		/* value to write to COPY register */
	u_long		ConfigIndex;	/* value to write to COR */
	u_long		Present;	/* which config registers present */
} config_req_t;

/*
 * Flags for RequestConfiguration - note that the CONF_ENABLE_IRQ_STEERING
 *	flag shares the same bit field as the Attributes flags for
 *	ModifyConfiguration.
 */
#define	CONF_ENABLE_IRQ_STEERING	0x00010000
/*
 * The following flags are used for the IntType member to specify which
 *	type of socket interface the client wants.
 */
#define	SOCKET_INTERFACE_MEMORY		0x00000001
#define	SOCKET_INTERFACE_MEMORY_AND_IO	0x00000002
/*
 * The following flags are used for the Present member to specify which
 *	configuration registers are present.  They may also be used by
 *	clients for their internal state.
 */
#define	CONFIG_OPTION_REG_PRESENT	0x00000001	/* COR present */
#define	CONFIG_STATUS_REG_PRESENT	0x00000002	/* STAT reg present */
#define	CONFIG_PINREPL_REG_PRESENT	0x00000004	/* PRR present */
#define	CONFIG_COPY_REG_PRESENT		0x00000008	/* COPY reg present */

/*
 * Bit definitions for configuration registers.
 *
 * Pin Replacement Register (PRR) bits - these are used for calls to
 *	RequestConfiguration, AccessConfigurationRegister and
 *	GetConfigurationInfo, as well as internally by clients
 *	and Card Services.
 * To inform Card Services that a particular bit in the PRR is valid on
 *	a call to RequestConfiguration, both the XXX_STATUS and the
 *	XXX_EVENT bits must be set.
 */
#define	PRR_WP_STATUS		0x01	/* R-WP state W-write WP Cbit */
#define	PRR_READY_STATUS	0x02	/* R-READY state W-write READY Cbit */
#define	PRR_BVD2_STATUS		0x04	/* R-BVD2 state W-write BVD2 Cbit */
#define	PRR_BVD1_STATUS		0x08	/* R-BVD1 state W-write BVD1 Cbit */
#define	PRR_WP_EVENT		0x10	/* WP changed */
#define	PRR_READY_EVENT		0x20	/* READY changed */
#define	PRR_BVD2_EVENT		0x40	/* BVD2 changed */
#define	PRR_BVD1_EVENT		0x80	/* BVD1 changed */
/*
 * Configuration Option Register (COR) bits
 */
#define	COR_LEVEL_IRQ		0x40	/* set to enable level interrupts */
#define	COR_SOFT_RESET		0x80	/* soft reset bit */
/*
 * Card Configuration Status Register (CCSR)
 */
#define	CCSR_INTR		0x02	/* interrupt pending */
#define	CCSR_POWER_DOWN		0x04	/* power down card */
#define	CCSR_AUDIO		0x08	/* enable Audio signal */
#define	CCSR_IO_IS_8		0x20	/* only 8-bit IO data path */
#define	CCSR_SIG_CHG		0x40	/* enable status changes */
#define	CCSR_CHANGED		0x80	/* one of the PRR bits has changed */
/*
 * Macros to manipulate the Socket and Copy Register (SCR) values
 */
#define	SCR_GET_SOCKET(r)		((r)&0x0f)
#define	SCR_GET_COPY(r)			(((r)>>4)&7)
#define	SCR_SET_SOCKET(s)		((s)&0x0f)
#define	SCR_SET_COPY(c)			(((c)&7)<<4)
#define	SCR_SET_SOCKET_COPY(s, c)	(((s)&0x0f) | (((c)&7)<<4))

/*
 * modify_config_t structure used for ModifyConfiguration
 */
typedef struct modify_config_t {
	u_long		Attributes;	/* attributes to modify */
	u_long		Vcc;		/* Vcc value */
	u_long		Vpp1;		/* Vpp1 value */
	u_long		Vpp2;		/* Vpp2 value */
} modify_config_t;

/*
 * Flags for ModifyConfiguration - note that the CONF_ENABLE_IRQ_STEERING
 *	flag used with RequestConfiguration shares this bit field.
 */
#define	CONF_VCC_CHANGE_VALID		0x00000001	/* Vcc is valid */
#define	CONF_VPP1_CHANGE_VALID		0x00000002	/* Vpp1 is valid */
#define	CONF_VPP2_CHANGE_VALID		0x00000004	/* Vpp2 is valid */
#define	CONF_IRQ_CHANGE_VALID		0x00000008	/* IRQ is valid */

/*
 * access_config_reg_t structure used for AccessConfigurationRegister
 */
typedef struct access_config_reg_t {
	u_long		Action;		/* register access operation */
	u_long		Offset;		/* config register offset */
	u_long		Value;		/* value read or written */
} access_config_reg_t;
/*
 * Flags for AccessConfigurationRegister
 */
#define	CONFIG_REG_READ		0x00000001	/* read config register */
#define	CONFIG_REG_WRITE	0x00000002	/* write config register */
/*
 * The following offsets are used to specify the configuration register
 *	offset to AccessConfigurationRegister
 */
#define	CONFIG_OPTION_REG_OFFSET	0x00	/* COR offset */
#define	CONFIG_STATUS_REG_OFFSET	0x02	/* STAT reg offset */
#define	CONFIG_PINREPL_REG_OFFSET	0x04	/* PRR offset */
#define	CONFIG_COPY_REG_OFFSET		0x06	/* COPY reg offset */

/*
 * make_device_node_t structure used for MakeDeviceNode
 */
typedef struct make_device_node_t {
	u_long		Action;		/* device operation */
	u_long		NumDevNodes;	/* number of nodes to create/destroy */
	struct devnode_desc {
	    char	*name;		/* device node path and name */
	    int		spec_type;	/* dev special type (block or char) */
	    int		minor_num;	/* device node minor number */
	    char	*node_type;	/* device node type */
	} *devnode_desc;
} make_device_node_t;
/*
 * Action values for MakeDeviceNode
 */
#define	CREATE_DEVICE_NODE		0x01	/* create device node */
#define	REMOVE_DEVICE_NODE		0x02	/* remove device node */
#define	REMOVAL_ALL_DEVICE_NODES	0x03	/* remove all device nodes */

/*
 * cs_ddi_info_t for CS_DDI_Info
 */
typedef struct cs_ddi_info_t {
	u_long		Socket;		/* socket number */
	char		*driver_name;	/* unique driver name */
	dev_info_t	*dip;		/* dip */
	int		instance;	/* instance */
} cs_ddi_info_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _CS_H */
