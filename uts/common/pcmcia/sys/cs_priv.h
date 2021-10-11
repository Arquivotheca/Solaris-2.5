/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _CS_PRIV_H
#define	_CS_PRIV_H

#pragma ident	"@(#)cs_priv.h	1.35	95/08/07 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * PCMCIA Card Services private header file
 */

/*
 * typedef for function pointers to quiet lint and cc -v
 */
typedef	int (f_t)(int, ...);	/* for lint - cc -v quieting */

/*
 * Magic number we use when talking with Socket Services
 */
#define	CS_MAGIC	PCCS_MAGIC

/*
 * CIS_DEFAULT_SPEED is the default speed to use to read the CIS
 *	in AM space.  It is expressed in nS.
 */
#define	CIS_DEFAULT_SPEED	250

/*
 * This is the IO window speed.
 */
#define	IO_WIN_SPEED		250

/*
 * Flags to support various internal first/next functions
 */
#define	CS_GET_FIRST_FLAG	0x0001
#define	CS_GET_NEXT_FLAG	0x0002

/*
 * Macros to manipulate bits - only does up to u_long size
 */
#define	CS_BIT_WORDSIZE		(sizeof (u_long))
#define	CS_BIT_GET(val, bit)	((u_long)(val) & (u_long)(1<<(u_long)(bit)))
#define	CS_BIT_CLEAR(val, bit)	((val) &= (u_long)~(1<<(u_long)(bit)))
#define	CS_BIT_SET(val, bit)	((u_long)(val) |= (u_long)(1<<(u_long)(bit)))

/*
 * Minimum time to wait after socket reset before we are allowed to
 *	access the card.  The PCMCIA specification says at least 20mS
 *	must elapse from the time that the card is reset until the
 *	first access of any kind can be made to the card. This time
 *	value is expressed in mS.
 */
#define	RESET_TIMEOUT_TIME	180

/*
 * Maximum time to wait for card ready after resetting the socket.
 *	We wait for card ready a maximum of 20 seconds after card
 *	reset before considering that we have an error condition.
 * XXX - what does PCMCIA specify as the max time here??
 */
#define	READY_TIMEOUT_TIME	(drv_usectohz(20000000))

/*
 * Time between periodically kicking the soft interrupt handler.
 */
#define	SOFTINT_TIMEOUT_TIME	(drv_usectohz(2000000))

/*
 * Handy macro to do untimeout.
 */
#define	UNTIMEOUT(id)		\
	if ((id)) {		\
	    untimeout((id));	\
	    (id) = 0;		\
	}

/*
 * Macros to enter/exit event thread mutex
 */
#define	EVENT_THREAD_MUTEX_ENTER(sp)			\
	if (!(sp->flags & SOCKET_IN_EVENT_THREAD))	\
	    mutex_enter(&sp->client_lock);
#define	EVENT_THREAD_MUTEX_EXIT(sp)			\
	if (!(sp->flags & SOCKET_IN_EVENT_THREAD))	\
	    mutex_exit(&sp->client_lock);

/*
 * cisregister_t structure is used to support the CISRegister
 *	and the CISUnregister function calls
 */
typedef struct cisregister_t {
	ulong			cis_magic;
	ulong			cis_version;
	void *			(*cis_parser)(int function, ...);
	cistpl_callout_t	*cistpl_std_callout; /* standard callout list */
} cisregister_t;

/*
 * These two defines are to support CISRegister and CISUnregister
 */
#define	CIS_MAGIC	0x20434953
#define	CIS_VERSION	_VERSION(0, 1)

/*
 * CS_SS_CLIENT_HANDLE is a special client handle that Socket Services gets
 *	when it registers with RegisterClient.
 */
#define	CS_SS_CLIENT_HANDLE	0x00000000

/*
 * Client handle, socket number and socket pointer macros.
 */
#define	CLIENT_HANDLE_IS_SS(ch)		(!GET_CLIENT_MINOR((ch)))
#define	CS_MAX_CLIENTS_MASK		0x0ffff
#define	CS_MAX_CLIENTS			(CS_MAX_CLIENTS_MASK - 2)
#define	MAKE_CLIENT_HANDLE(s, m)	(((s)<<16)|((m)&CS_MAX_CLIENTS_MASK))
#define	GET_CLIENT_SOCKET(ch)		(((ch)>>16)&CS_MAX_CLIENTS_MASK)
#define	GET_CLIENT_MINOR(ch)		((ch)&CS_MAX_CLIENTS_MASK)

/*
 * DIP2SOCKET_NUM(dip) - this macro gets the PCM_DEV_SOCKET property from
 *	the passed dip.  If the property can't be found, then the default
 *	value of cs_globals.num_sockets is returned.
 */
#define	DIP2SOCKET_NUM(dip)		ddi_getprop(DDI_DEV_T_NONE, dip,\
						(DDI_PROP_CANSLEEP |	\
							DDI_PROP_NOTPROM), \
						PCM_DEV_SOCKET,		\
						cs_globals.num_sockets)

/*
 * Range checking macros
 *
 * CHECK_SOCKET_NUM(socket_number, max_sockets) returns 1 if
 *	socket_number is in range
 */
#define	CHECK_SOCKET_NUM(sn, ms)	(((sn) >= (ms))?0:1)

/*
 * window macros
 *
 * Note that WINDOW_FOR_SOCKET expects a socket mask for the wsm
 *	parameter (this is a socket_enum_t type, and NOT just a
 *	plain old u_long)
 */
#define	WINDOW_FOR_SOCKET(wsm, sn)	((wsm)[sn/PR_WORDSIZE] & \
						(1 << ((sn) & PR_MASK)))
#define	WINDOW_AVAILABLE_FOR_MEM(wn)	(!(cs_windows[wn].state))
#define	WINDOW_AVAILABLE_FOR_IO(wn)	\
		(!(cs_windows[wn].state & (CW_CIS | CW_MEM | CW_ALLOCATED)))

/*
 * IO Base and NumPorts address frobnitz macros
 */
#define	IOADDR_FROBNITZ(Base, IOAddrLines)	\
	((caddr_t)((u_long)Base & (ulong)((1 << IOAddrLines) - 1)))
#define	IONUMPORTS_FROBNITZ(np)			(((np)&1)?((np)+1):(np))

/*
 * Structure that contains offsets to the card's configuration registers
 *	as well as copies of the data written to them in RequestConfiguration.
 *	We use an offset per register approach since not all cards have
 *	all registers implemented, and by specifying a NULL register offset,
 *	we know not to try to access that register.
 */
typedef struct config_regs_t {
	cfg_regs_t	cor;		/* Configuration Option Register */
	u_long		cor_p;
	cfg_regs_t	ccsr;		/* Configuration and Status Register */
	u_long		ccsr_p;
	cfg_regs_t	prr;		/* Pin Replacement Register */
	u_long		prr_p;
	cfg_regs_t	scr;		/* Socket and Copy Register */
	u_long		scr_p;
} config_regs_t;

/*
 * Macro to make calling the client's event handler look like a function.
 */
#define	CLIENT_EVENT_CALLBACK(cp, event, pri)		\
	if ((cp)->event_callback_handler) {		\
	    (cp)->event_callback_handler(event, pri,	\
			&(cp)->event_callback_args);	\
	}

/*
 * Macro to return event in PRR - this also clears the changed bit if
 *	the event occured.
 */
#define	PRR_EVENT(prrx, pe, ps, ce, re)	\
	if (prrx & pe) {		\
	    if (prrx & ps)		\
		(re) |= ce;		\
	    prrx &= ~pe;		\
	    prrx |= ps;			\
	}

/*
 * io_alloc_t struct used to keep track of a client's IO window allocation
 */
typedef struct io_alloc_t {
	u_long	Window1;	/* allocated IO window number for set #1 */
	void	*BasePort1;	/* first IO range base address or port num */
	u_long	NumPorts1;	/* first IO range number contiguous ports */
	u_long	Attributes1;	/* first IO range attributes */
	u_long	Window2;	/* allocated IO window number for set #2 */
	void	*BasePort2;	/* second IO range base address or port num */
	u_long	NumPorts2;	/* second IO range number contiguous ports */
	u_long	Attributes2;	/* second IO range attributes */
	u_long	IOAddrLines;	/* number of IO address lines decoded */
} io_alloc_t;

/*
 * irq_alloc_t structure used to keep track of a client's IRQ allocation
 */
typedef struct irq_alloc_t {
	u_long		Attributes;	/* IRQ attribute flags */
	u_long		irq;		/* assigned IRQ number */
	u_long		priority;	/* assigned IRQ priority */
	u_long		handler_id;	/* IRQ handler ID for this IRQ */
	f_t		*irq_handler;
	caddr_t		irq_handler_arg;
} irq_alloc_t;

/*
 * The client data structure
 */
typedef struct client_t {
	client_handle_t	client_handle;	/* this client's client handle */
	unsigned	flags;		/* client flags */
	/* resource control */
	u_long		memwin_count;	/* number of mem windows allocated */
	io_alloc_t	io_alloc;	/* IO resource allocations */
	irq_alloc_t	irq_alloc;	/* IRQ resource allocations */
	/* event support */
	u_long		event_mask;	/* client event mask */
	u_long		global_mask;	/* client global event mask */
	u_long		events;		/* current events pending */
	u_long		pending_events;	/* events pending in RegisterClient */
	csfunction_t	*event_callback_handler;
	event_callback_args_t	event_callback_args;
	/* DDI support */
	dev_info_t	*dip;		/* this client's dip */
	char		*driver_name;	/* client's driver name */
	int		instance;	/* client's driver instance */
	/* list control */
	struct client_t	*next;		/* next client pointer */
	struct client_t	*prev;		/* previous client pointer */
} client_t;

/*
 * Flags for client structure - note that we share the client_t->flags
 *	member with the definitions in cs.h that are used by the
 *	RegisterClient function.
 *
 * We can start our flags from 0x00001000 and on up.
 */
#define	REQ_CONFIGURATION_DONE	0x00001000	/* RequestConfiguration done */
#define	REQ_SOCKET_MASK_DONE	0x00002000	/* RequestSocketMask done */
#define	REQ_IO_DONE		0x00004000	/* RequestIO done */
#define	REQ_IRQ_DONE		0x00008000	/* RequestIRQ done */
#define	CLIENT_SUPER_CLIENT	0x00010000	/* "super-client" client */
#define	CLIENT_CARD_INSERTED	0x00100000	/* current card for client */
#define	CLIENT_SENT_INSERTION	0x00200000	/* send CARD_INSERTION */
#define	CLIENT_MTD_IN_PROGRESS	0x01000000	/* MTD op in progress */
#define	CLIENT_IO_ALLOCATED	0x02000000	/* IO resources allocated */
#define	CLIENT_IRQ_ALLOCATED	0x04000000	/* IRQ resources allocated */
#define	CLIENT_WIN_ALLOCATED	0x08000000	/* window resources allocated */

/*
 * io_mmap_window_t structure that describes the memory-mapped IO
 *	window on this socket
 */
typedef struct io_mmap_window_t {
	u_long		flags;		/* window flags */
	u_long		number;		/* IO window number */
	u_long		size;		/* size of mapped IO window */
	caddr_t		base;		/* window mapped base address */
	u_long		count;		/* referance count */
} io_mmap_window_t;

/*
 * The per-socket structure.
 */
typedef struct cs_socket_t {
	unsigned	socket_num;	/* socket number */
	u_long		flags;		/* socket flags */
	u_long		init_state;	/* cs_init state */
	/* socket thread control and status */
	kthread_t	*event_thread;	/* per-socket work thread */
	u_long		thread_state;	/* socket thread state flags */
	kmutex_t	lock;		/* protects events and clients */
	kcondvar_t	thread_cv;	/* event handling synchronization */
	kcondvar_t	caller_cv;	/* event handling synchronization */
	kcondvar_t	reset_cv;	/* for use after card RESET */
	u_long		events;		/* socket events */
	u_long		event_mask;	/* socket event mask */
	ddi_softintr_t	softint_id;	/* soft interrupt handler ID */
	int		rdybsy_tmo_id;	/* timer ID for READY/BUSY timer */
	ddi_iblock_cookie_t	*iblk;	/* event iblk cookie */
	ddi_idevice_cookie_t	*idev;	/* event idev cookie */
	/* config registers support */
	config_regs_t	config_regs;	/* pointers to config registers */
	u_long		config_regs_offset; /* offset from start of AM */
	unsigned	pin;		/* valid bits in PRR */
	u_long		present;	/* which config registers present */
	/* client management */
	client_t	*client_list;	/* clients on this socket */
	unsigned	next_cl_minor;	/* next available client minor num */
	kmutex_t	client_lock;	/* protects client list */
	/* CIS support */
	u_long		cis_flags;	/* CIS-specific flags */
	cistpl_t	*cis;		/* CIS linked list */
	u_long		cis_win_num;	/* CIS window number */
	unsigned	cis_win_size;	/* CIS window size */
	int		nchains;	/* number of tuple chains in CIS */
	int		ntuples;	/* number of tuples in CIS */
	kmutex_t	cis_lock;	/* protects CIS */
	/* memory mapped IO window support */
	io_mmap_window_t *io_mmap_window;
	/* Socket Services work thread control and status */
	kthread_t	*ss_thread;	/* SS work thread */
	u_long		ss_thread_state; /* SS work thread state */
	kcondvar_t	ss_thread_cv;	/* SS work thread synchronization */
	kcondvar_t	ss_caller_cv;	/* SS work thread synchronization */
	kmutex_t	ss_thread_lock;	/* protects SS work thread state */
} cs_socket_t;

/*
 * cs_socket_t->flags flags
 */
#define	SOCKET_CARD_INSERTED		0x00000001	/* card is inserted */
#define	SOCKET_IS_IO			0x00000002	/* socket in IO mode */
#define	SOCKET_UNLOAD_MODULE		0x00000004	/* want to unload CS */
#define	SOCKET_NEEDS_THREAD		0x00000008	/* wake event thread */
#define	SOCKET_IN_EVENT_THREAD		0x00000010	/* in event thread */

/*
 * cs_socket_t->thread_state and cs_socket_t->ss_thread_state flags
 */

/* generic for all threads */
#define	SOCKET_THREAD_EXIT		0x00000001	/* exit event thread */

/* only used for per-socket event thread */
#define	SOCKET_WAIT_FOR_READY		0x00001000	/* waiting for READY */
#define	SOCKET_RESET_TIMER		0x00002000	/* RESET timer */

/* only used for Socket Services work thread */
#define	SOCKET_THREAD_CSCISInit		0x00100000	/* call CSCISInit */

/*
 * cs_socket_t->cis_flags flags
 */
#define	CW_VALID_CIS			0x00000001	/* valid CIS */

/*
 * macro to test for a valid CIS window on a socket
 */
#define	SOCKET_HAS_CIS_WINDOW(sp)	(sp->cis_win_num !=	\
						cs_globals.num_windows)

/*
 * cs_socket_t->init_state flags - these flags are used to keep track of what
 *	was allocated in cs_init so that things can be deallocated properly
 *	in cs_deinit.
 */
#define	SOCKET_INIT_STATE_MUTEX		0x00000001	/* mutexii are OK */
#define	SOCKET_INIT_STATE_CV		0x00000002	/* cvii are OK */
#define	SOCKET_INIT_STATE_THREAD	0x00000004	/* thread OK */
#define	SOCKET_INIT_STATE_READY		0x00000008	/* socket OK */
#define	SOCKET_INIT_STATE_SS_THREAD	0x00000010	/* SS thread OK */
/*
 * While this next flag doesn't really describe a per-socket resource,
 *	we still set it for each socket.  When the soft interrupt handler
 *	finally gets removed in cs_deinit, this flag will get cleared.
 *	The value of this flag should follow the previous SOCKET_INIT
 *	flag values.
 */
#define	SOCKET_INIT_STATE_SOFTINTR	0x00000020	/* softintr handler */

/*
 * Macro to create a socket event thread.
 */
#define	CS_THREAD_PRIORITY		(v.v_maxsyspri - 4)
#define	CREATE_SOCKET_EVENT_THREAD(eh, csp)			\
	thread_create((caddr_t)NULL, 0, eh, (caddr_t)csp,	\
	0, &p0, TS_RUN, CS_THREAD_PRIORITY)

/*
 * The per-window structure.
 */
typedef struct cs_window_t {
	window_handle_t	window_handle;	/* unique window handle */
	client_handle_t	client_handle;	/* owner of this window */
	unsigned	socket_num;	/* socket number */
	unsigned	state;		/* window state flags */
} cs_window_t;

/*
 * Window structure state flags - if none of these are set, then it
 *	means that this window is available and not being used by
 *	anyone.
 * Setting the CW_ALLOCATED will prevent the window from being found
 *	as an available window for memory or IO; since memory windows
 *	are not shared between clients, RequestWindow will always set
 *	the CW_ALLOCATED flag when it has assigned a memory window to
 *	a client.  Since we can sometimes share IO windows, RequestIO
 *	will only set the CW_ALLOCATED flag if it doesn't want the IO
 *	window to be used by other calls to RequestIO.
 */
#define	CW_ALLOCATED	0x00000001	/* window is allocated  */
#define	CW_CIS		0x00000002	/* window being used as CIS window */
#define	CW_MEM		0x00000004	/* window being used as mem window */
#define	CW_IO		0x00000008	/* window being used as IO window */

/*
 * window handle defines - the WINDOW_HANDLE_MASK implies the maximum number
 *	of windows allowed
 */
#define	WINDOW_HANDLE_MAGIC	0x574d0000
#define	WINDOW_HANDLE_MASK	0x0000ffff
#define	GET_WINDOW_NUMBER(wh)	((wh) & WINDOW_HANDLE_MASK)
#define	GET_WINDOW_MAGIC(wh)	((wh) & ~WINDOW_HANDLE_MASK)

/*
 * The client type structures, used to sequence events to clients on a
 *	socket. The "type" flags are the same as are used for the
 *	RegisterClient function.
 */
typedef struct client_types_t {
	u_long			type;
	u_long			order;
	struct client_types_t	*next;
} client_types_t;

/*
 * Flags that specify the order of client event notifications for the
 *	client_types_t structure.
 */
#define	CLIENT_EVENTS_LIFO	0x00000001
#define	CLIENT_EVENTS_FIFO	0x00000002

/*
 * This is a global structure that CS uses to keep items that are shared
 *	among many functions.
 */
typedef struct cs_globals_t {
	kmutex_t	global_lock;	/* protects this struct */
	kmutex_t	window_lock;	/* protects cs_windows */
	ddi_softintr_t	softint_id;	/* soft interrupt handler id */
	int		sotfint_tmo;	/* soft interrupt handler timeout id */
	u_long		init_state;	/* flags set in cs_init */
	u_long		flags;		/* general global flags */
	u_long		num_sockets;
	u_long		num_windows;
	struct sclient_list_t	*sclient_list;
} cs_globals_t;

/*
 * Flags for cs_globals_t->init_state
 */
#define	GLOBAL_INIT_STATE_SOFTINTR	0x00010000	/* softintr handler */
#define	GLOBAL_INIT_STATE_MUTEX		0x00020000	/* global mutex init */
#define	GLOBAL_INIT_STATE_NO_CLIENTS	0x00040000	/* no new clients */
#define	GLOBAL_INIT_STATE_UNLOADING	0x00080000	/* cs_deinit running */
/*
 * Flags for cs_globals_t->flags
 */
#define	GLOBAL_SUPER_CLIENT_REGISTERED	0x00000001	/* "super-client" reg */
#define	GLOBAL_IN_SOFTINTR		0x00000002	/* in soft int code */

/*
 * sclient_reg_t struct for RegisterClient when a "super-client" is
 *	registering.
 * This structure is actually hung off of the client_reg_t.private
 *	structure member.  Since we don't make public how to write
 *	a "super-client", the actualt structure that the client uses
 *	is defined in this private header file.
 */
typedef struct sclient_reg_t {
	u_long			num_sockets;
	u_long			num_windows;
	u_long			num_clients;
	struct sclient_list_t {
		client_handle_t	client_handle;
		int		error;
	} **sclient_list;
} sclient_reg_t;

/*
 * structure for event text used for cs_ss_event_text
 */
typedef struct cs_ss_event_text_t {
	event_t		ss_event;	/* SS event code */
	event_t		cs_event;	/* CS event code */
	char		*text;
} cs_ss_event_text_t;

/*
 * Flags for Event2Text - not used by clients; the struct is defined
 *	in the cs.h header file.  These values depend on the size
 *	of the cs_ss_event_text_t array in the cs_strings.h header
 *	file.
 *
 * MAX_CS_EVENT_BUFSIZE is used by callers to Event2Text as a minimum
 *	text buffer size.  The caller gets this value when calling
 *	Event2Text with the Attributes flag GET_CS_EVENT_BUFSIZE;
 *	we do it this way so that this value can change between
 *	different releases of CS.
 * MAX_MULTI_EVENT_BUFSIZE is used by callers that may get several
 *	events in their event fields (usually only CS or SS uses
 *	this).
 * XXX - this should be determined automatically
 */
#define	MAX_CS_EVENT_BUFSIZE		64	/* single event */
#define	MAX_MULTI_EVENT_BUFSIZE		512	/* all events */

/*
 * Flags for cs_read_event_status
 */
#define	CS_RES_IGNORE_NO_CARD		0x0001	/* don't check for card */

/*
 * Flags for Function2Text - not used by clients; the struct is defined
 *	in the cs.h header file.
 */
#define	CSFUN2TEXT_FUNCTION	0x0001	/* return text of CS function code */
#define	CSFUN2TEXT_RETURN	0x0002	/* return text of CS return code */

#ifdef	__cplusplus
}
#endif

#endif	/* _CS_PRIV_H */
