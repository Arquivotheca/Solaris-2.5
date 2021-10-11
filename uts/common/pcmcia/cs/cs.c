/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved
 */

#ident "@(#)cs.c	1.69	95/08/07 SMI"

/*
 * PCMCIA Card Services
 *	The PCMCIA Card Services is a loadable module which
 *	presents the Card Services interface to client device
 *	drivers.
 *
 *	Card Services uses Socket Services-like calls into the
 *	PCMCIA nexus driver to manipulate socket and adapter
 *	resources.
 *
 * Note that a bunch of comments are not indented correctly with the
 *	code that they are commenting on. This is because cstyle is
 *	is inflexible concerning 4-column indenting.
 */

/*
 * _depends_on used to be static, but SC3.0 wants it global
 */
char _depends_on[] = "drv/pcmcia";

#if defined(DEBUG)
#define	CS_DEBUG
#endif

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/buf.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/conf.h>
#include <sys/stat.h>
#include <sys/autoconf.h>
#include <sys/vtoc.h>
#include <sys/dkio.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/varargs.h>
#include <sys/var.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/utsname.h>
#include <sys/vtrace.h>
#include <sys/kstat.h>
#include <sys/kmem.h>
#include <sys/modctl.h>
#include <sys/kobj.h>

#include <sys/pctypes.h>
#include <sys/pcmcia.h>
#include <sys/sservice.h>
#include <pcmcia/sys/cis.h>
#include <pcmcia/sys/cis_handlers.h>
#include <pcmcia/sys/cs_types.h>
#include <pcmcia/sys/cs.h>
#include <pcmcia/sys/cs_priv.h>
/*
 * The cs_strings header file is where all of the major strings that
 *	Card Services uses are located.
 */
#include <pcmcia/sys/cs_strings.h>

#ifdef	CardServices
#undef	CardServices
#endif

/*
 * Function declarations
 *
 * The main Card Services entry point
 */
int CardServices(int function, ...);

/*
 * event handling functions
 */
static int cs_event(event_t, unsigned);
static event_t ss_to_cs_events(cs_socket_t *, event_t);
static event_t cs_cse2sbm(event_t);
static void cs_event_thread(cs_socket_t *);
static int cs_card_insertion(cs_socket_t *, event_t);
static int cs_card_removal(cs_socket_t *);
static void cs_ss_thread(cs_socket_t *);
void cs_ready_timeout(cs_socket_t *);
static int cs_card_for_client(client_t *);
static int cs_request_socket_mask(client_handle_t, sockevent_t *);
static int cs_release_socket_mask(client_handle_t);
static int cs_get_event_mask(client_handle_t, sockevent_t *);
static int cs_set_event_mask(client_handle_t, sockevent_t *);
static int cs_event2text(event2text_t *, int);
static int cs_read_event_status(cs_socket_t *, event_t *, get_ss_status_t *,
									int);
u_int cs_socket_event_softintr();
void cs_event_softintr_timeout();
static int cs_get_status(client_handle_t, get_status_t *);
static u_long cs_sbm2cse(u_long);
static unsigned cs_merge_event_masks(cs_socket_t *);
static int cs_set_socket_event_mask(cs_socket_t *, unsigned);

/*
 * CIS handling functions
 */
static void *(*cis_parser)(int, ...) = NULL;
cistpl_callout_t *cis_cistpl_std_callout;
static int cs_parse_tuple(client_handle_t,  tuple_t *, cisparse_t *, cisdata_t);
static int cs_get_tuple_data(client_handle_t, tuple_t *);
static int cs_validate_cis(client_handle_t, cisinfo_t *);
static int cs_get_firstnext_tuple(client_handle_t, tuple_t *, int);

/*
 * client handling functions
 */
unsigned cs_create_next_client_minor(unsigned, unsigned);
static client_t *cs_find_client(client_handle_t, int *);
static client_handle_t cs_create_client_handle(unsigned, client_t *);
static int cs_destroy_client_handle(client_handle_t);
static int cs_register_client(client_handle_t *, client_reg_t *);
static int cs_deregister_client(client_handle_t);
static int cs_deregister_mtd(client_handle_t);
static void cs_clear_superclient_lock(int);
static int cs_add_client_to_socket(unsigned, client_handle_t *,
						client_reg_t *, int);

/*
 * window handling functions
 */
static int cs_request_window(client_handle_t, window_handle_t *, win_req_t *);
static int cs_release_window(window_handle_t);
static int cs_modify_window(window_handle_t, modify_win_t *);
static int cs_modify_mem_window(window_handle_t, modify_win_t *, win_req_t *,
									int);
static int cs_map_mem_page(window_handle_t, map_mem_page_t *);
static int cs_find_mem_window(u_long, win_req_t *, u_long *);
static int cs_memwin_space_and_map_ok(inquire_window_t *, win_req_t *);
static int cs_valid_window_speed(inquire_window_t *, u_long);
static window_handle_t cs_create_window_handle(u_long);
cs_window_t *cs_find_window(window_handle_t);
static int cs_find_io_win(u_long, iowin_char_t *, u_long *, u_long *);

/*
 * IO, IRQ and configuration handling functions
 */
static int cs_request_io(client_handle_t, io_req_t *);
static int cs_release_io(client_handle_t, io_req_t *);
static int cs_allocate_io_win(u_long, u_long, u_long *);
static int cs_setup_io_win(u_long, u_long, void **, u_long *, u_long, u_long);
static int cs_request_irq(client_handle_t, irq_req_t *);
static int cs_release_irq(client_handle_t, irq_req_t *);
static int cs_request_configuration(client_handle_t, config_req_t *);
static int cs_release_configuration(client_handle_t);
static int cs_modify_configuration(client_handle_t, modify_config_t *);
static int cs_access_configuration_register(client_handle_t,
						access_config_reg_t *);

/*
 * general functions
 */
static int cs_init();
static int cs_deinit();
static u_long cs_get_socket(client_handle_t, u_long *,
					cs_socket_t **, client_t **);
static int cs_convert_speed(convert_speed_t *);
static int cs_convert_size(convert_size_t *);
static char *cs_csfunc2text(int, int);
static int cs_map_log_socket(client_handle_t, map_log_socket_t *);
static int cs_convert_powerlevel(u_long, u_long, u_long, unsigned *);
static int cs_make_device_node(client_handle_t, make_device_node_t *);
static int cs_ddi_info(cs_ddi_info_t *);
caddr_t cs_init_cis_window(cs_socket_t *, u_long, u_long *);

/*
 * global variables
 */
static int cis_module;	/* CIS module id */
static cs_socket_t *cs_sockets = NULL;
static cs_window_t *cs_windows = NULL;
static int cs_max_client_handles = CS_MAX_CLIENTS;
client_t cs_socket_services_client;	/* global SS client */
client_types_t client_types[MAX_CLIENT_TYPES];
cs_globals_t cs_globals;
int cs_reset_timeout_time = RESET_TIMEOUT_TIME;

/*
 * This is the loadable module wrapper.
 * It is essentially boilerplate so isn't documented
 */
extern struct mod_ops mod_miscops;

static struct modlmisc modldrv = {
	&mod_miscops,		/* Type of module. This one is a module */
	"PCMCIA Card Services",	/* Name of the module. */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};

#ifdef	CS_DEBUG
int	cs_debug = 0;
#endif

/*
 * ==== module initialization/deinitialization ====
 */

int
_init()
{

	if (cs_init() != CS_SUCCESS) {
	    if (cs_deinit() != CS_SUCCESS)
		cmn_err(CE_CONT, "CS: _init cs_deinit error\n");
	    return (-1);
	} else {
	    return (mod_install(&modlinkage));
	}
}

/*
 * Set cs_allow_unload to non-zero to allow Card Services
 *	to be unloaded. Note that you must be sure that
 *	no external accesses to this module are in progress
 *	when you unload this module or else you will likely
 *	cause the system to panic. At the moment, unloading
 *	the Card Services module is only really supported
 *	for debugging.
 */
int cs_allow_unload = 0;

int
_fini()
{
	int ret;

	if (cs_allow_unload == 0) {
#ifdef	CS_DEBUG
	    if (cs_debug > 0) {
		cmn_err(CE_CONT, "CS: to allow module to unload set "
					"cs_allow_unload to a non-zero "
								"value\n");
	    }
#endif
	    return (-1);
	}

	if ((ret = mod_remove(&modlinkage)) == 0)
		if (cs_deinit() != CS_SUCCESS)
			ret = -1;
	return (ret);
}

int
_info(struct modinfo *modinfop)

{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * cs_init - Initialize CS internal structures, databases, and state,
 *		and register with SS
 *
 * XXX - Need to make sure that if we fail at any point that we free
 *		any resources that we allocated, as well as kill any
 *		threads that may have been started.
 */
static int
cs_init()
{
	csregister_t csr;
	static f_t *lcis_parser = NULL;
	cs_socket_t *sp;
	int socket_num;
	client_types_t *ct;
	client_t *client;
	inquire_adapter_t inq_adapter;

#if defined(CS_DEBUG)
	if (cs_debug > 1)
	    cmn_err(CE_CONT, "CS: cs_init\n");
#endif

	/*
	 * Initialize the CS global structure
	 */
	mutex_init(&cs_globals.global_lock, "cs_globals.global_lock",
							MUTEX_DRIVER, NULL);
	mutex_init(&cs_globals.window_lock, "cs_globals.window_lock",
							MUTEX_DRIVER, NULL);

	cs_globals.softint_id = NULL;
	cs_globals.init_state = GLOBAL_INIT_STATE_MUTEX;
	cs_globals.flags = 0;
	cs_globals.num_sockets = 0;
	cs_globals.num_windows = 0;
	cs_globals.sotfint_tmo = 0;

	/*
	 * Setup the "super-client" structure
	 */
	cs_globals.sclient_list = (struct sclient_list_t *)
					kmem_zalloc(
						cs_globals.num_sockets *
						sizeof (struct sclient_list_t),
						KM_SLEEP);

	/*
	 * Set up the global Socket Services client, since we're going to
	 *	need it once we register with SS.
	 */
	client = &cs_socket_services_client;
	bzero((caddr_t)client, sizeof (client_t));
	client->client_handle = CS_SS_CLIENT_HANDLE;
	client->flags |= (INFO_SOCKET_SERVICES | CLIENT_CARD_INSERTED);

	/*
	 * Fill out the function for CSRegister and CISGetAddr
	 */
	csr.cs_magic = PCCS_MAGIC;
	csr.cs_version = PCCS_VERSION;
	csr.cs_card_services = CardServices;
	csr.cs_event = NULL;

	/*
	 * Load the CIS interpreter; if this fails, then we
	 *	should fail as well.
	 */
	cis_module = modload("misc", "cis");

	if (cis_module == -1) {
	    cmn_err(CE_CONT, "cs_init: can't load CIS module\n");
	    return (BAD_FUNCTION);
	} else {
	/*
	 * Now, call SS to get the address that CIS dropped in there
	 *	for us; we do this because the CIS module can't do a
	 *	_depends_on this module.
	 */
	    SocketServices(CISGetAddress, &csr);
	    if ((lcis_parser = (f_t *)csr.cs_event) == NULL) {
		cmn_err(CE_CONT, "cs_init: bad CIS entrypoint address\n");
		return (BAD_FUNCTION);
	    }
	/*
	 * Call into CIS and tell it what the Card Services entry point is
	 * This is actually when CIS calls CardServices(CISRegister) to
	 *	setup the address of the CIS entrypoint.  We do it this way
	 *	since some day either _depends_on will work correctly or
	 *	CardServices will be a kernel symbol.
	 */
	    (*lcis_parser)(CISP_CIS_SETUP, &csr);
#if defined(CS_DEBUG)
	    if (cs_debug > 250) {
		cmn_err(CE_CONT, "cs_init: loaded CIS module id %d "
				"cis_parser 0x%x lcis_parser 0x%x\n",
					(int)cis_module, (int)cis_parser,
					(int)lcis_parser);
	    }
#endif
	}

	/*
	 * Find out how many sockets we have and create the socket
	 *	list.  As a side effect, get the capabilities of
	 *	the generic adapter model that the nexus exports
	 *	to us.
	 */
	SocketServices(SS_InquireAdapter, &inq_adapter);

	cs_globals.num_sockets = inq_adapter.NumSockets;
	cs_globals.num_windows = inq_adapter.NumWindows;

	/*
	 * Do some simple sanity checking.
	 */
	if (cs_globals.num_sockets < 1 || cs_globals.num_windows < 1) {
	    cmn_err(CE_CONT,
		"cs_init: adapter has no sockets (%d) or windows (%d)\n",
				(int)cs_globals.num_sockets,
				(int)cs_globals.num_windows);
	    return (BAD_ADAPTER);
	}

	/*
	 * Setup the client type structure - this is used in the socket event
	 *	thread to sequence the delivery of events to all clients on
	 *	the socket.
	 */
	ct = &client_types[0];
	ct->type = INFO_IO_CLIENT;
	ct->order = CLIENT_EVENTS_LIFO;
	ct->next = &client_types[1];

	ct = ct->next;
	ct->type = INFO_MTD_CLIENT;
	ct->order = CLIENT_EVENTS_FIFO;
	ct->next = &client_types[2];

	ct = ct->next;
	ct->type = INFO_MEM_CLIENT;
	ct->order = CLIENT_EVENTS_FIFO;
	ct->next = NULL;

#ifdef	CS_DEBUG
	if (cs_debug > 1) {
	    ct = &client_types[0];
	    while (ct) {
		cmn_err(CE_CONT, "   ct->type 0x%x ct->order 0x%x "
						"ct->next 0x%x\n",
						(int)ct->type,
						(int)ct->order,
						(int)ct->next);
		ct = ct->next;
	    }
	}
#endif

	/*
	 * OK, we've got some sockets and some windows, now create the
	 *	list of sockets and windows and initialize each socket
	 *	and window structure.
	 */
	cs_sockets = (cs_socket_t *)kmem_zalloc((sizeof (cs_socket_t)) *
					cs_globals.num_sockets,
					KM_SLEEP);
	cs_windows = (cs_window_t *)kmem_zalloc((sizeof (cs_window_t)) *
					cs_globals.num_windows,
					KM_SLEEP);

	/*
	 * Walk through the sockets and try to allocate a CIS window for
	 *	each one.  If any socket can't get a CIS window, then
	 *	we fail for all sockets.
	 * Start up event threads for each socket as well.
	 */
	for (socket_num = 0; socket_num < cs_globals.num_sockets;
							socket_num++) {
	    sservice_t sservice;
	    get_cookies_and_dip_t *gcad;
	    win_req_t win_req;
	    convert_speed_t convert_speed;
	    iowin_char_t iowin_char;
	    u_long io_win_num, io_win_size;
	    set_socket_t set_socket;
	    int ret;

	    sp = &cs_sockets[socket_num];
	    sp->socket_num = socket_num;
	    sp->cis = NULL;
	    sp->cis_win_num = cs_globals.num_windows;
	    sp->event_mask = 0;
	    sp->events = 0;

	    mutex_enter(&cs_globals.window_lock);

	/*
	 * Find out if this socket supports memory-mapped IO or not.  If
	 *	it does, create the io_mmap_window structure for this socket
	 *	and allocate this window temporarily so that the search for
	 *	the CIS window won't find it (we'll deallocate it after we
	 *	find a CIS window).
	 */
	    iowin_char.IOWndCaps = (WC_IO_RANGE_PER_WINDOW |
				    WC_8BIT |
				    WC_16BIT);
	    if (cs_find_io_win(sp->socket_num, &iowin_char, &io_win_num,
						&io_win_size) == CS_SUCCESS) {

		sp->io_mmap_window = kmem_zalloc(sizeof (io_mmap_window_t),
								KM_SLEEP);

		/*
		 * Save the window number that we found since we have to
		 *	mark that window as allocated so that the search
		 *	for a CIS window won't find it.
		 * Also save the window size as a hint to RequestIO.
		 */
		sp->io_mmap_window->number = io_win_num;
		sp->io_mmap_window->size = io_win_size;
		cs_windows[sp->io_mmap_window->number].state = CW_ALLOCATED;
#ifdef	CS_DEBUG
		if (cs_debug > 1) {
		    cmn_err(CE_CONT, "cs_init: memory-mapped IO window 0x%x "
					"found for socket %d\n",
					(int)sp->io_mmap_window->number,
					(int)sp->socket_num);
		}
#endif
	    }

	/*
	 * Find a window that we can use for this socket's CIS window.
	 */
	    convert_speed.Attributes = CONVERT_NS_TO_DEVSPEED;
	    convert_speed.nS = CIS_DEFAULT_SPEED;
	    (void) cs_convert_speed(&convert_speed);

	    win_req.AccessSpeed = convert_speed.devspeed;
	    win_req.Attributes = (WIN_MEMORY_TYPE_AM | WIN_DATA_WIDTH_8);
	    win_req.Attributes = (WIN_MEMORY_TYPE_AM | WIN_MEMORY_TYPE_CM);
	    win_req.Base = 0;
	    win_req.Size = 0;

	    if ((ret = cs_find_mem_window(sp->socket_num, &win_req,
					&sp->cis_win_num)) != CS_SUCCESS) {
		sp->cis_win_num = cs_globals.num_windows;
		cmn_err(CE_CONT, "cs_init: socket %d can't get CIS "
						"window - error 0x%x\n",
						sp->socket_num, ret);
		return (BAD_WINDOW);
	    } else {
		cs_window_t *cwp = &cs_windows[sp->cis_win_num];
		inquire_window_t iw;

		iw.window = sp->cis_win_num;
		SocketServices(SS_InquireWindow, &iw);

		/*
		 * If the CIS window is a variable sized window, then use
		 *	the size that cs_find_mem_window returned to us,
		 *	since this will be the minimum size that we can
		 *	set this window to. If the CIS window is a fixed
		 *	sized window, then use the system pagesize as the
		 *	CIS window size.
		 */
		if (iw.mem_win_char.MemWndCaps & WC_SIZE) {
		    sp->cis_win_size = win_req.Size;
		} else {
		    sp->cis_win_size = PAGESIZE;
		}

		cwp->state = (CW_CIS | CW_ALLOCATED);
		cwp->socket_num = sp->socket_num;

	    } /* if (cs_find_mem_window) */

	/*
	 * We've got a CIS window, so now check if we also found a
	 *	memory-mapped IO window, and if so, mark that window as
	 *	available again.
	 */
	    if (sp->io_mmap_window) {
		io_mmap_window_t *iom = sp->io_mmap_window;

		cs_windows[iom->number].state = 0;
	    }

	    mutex_exit(&cs_globals.window_lock);

#if defined(CS_DEBUG)
	    if (cs_debug > 1) {
		cmn_err(CE_CONT, "cs_init: socket %d using CIS window %d "
					"size 0x%x\n", (int)sp->socket_num,
					(int)sp->cis_win_num,
					(int)sp->cis_win_size);
	    }
#endif

	/*
	 * Get the interrupt cookies and dip associated with this socket
	 *	so that we can initialize the mutexes, condition variables,
	 *	and soft interrupt handler.
	 */
	    gcad = &sservice.get_cookies;
	    gcad->socket = socket_num;
	    if (SocketServices(CSGetCookiesAndDip, &sservice) != SUCCESS) {
		cmn_err(CE_CONT,
			"cs_init: socket %d CSGetCookiesAndDip failure\n",
						socket_num);
		return (BAD_FUNCTION);
	    }

	/*
	 * Save the iblock and idev cookies for RegisterClient
	 */
	    sp->iblk = gcad->iblock;
	    sp->idev = gcad->idevice;

	    /* Setup for cs_event and cs_event_thread */
	    mutex_init(&sp->lock, "sp->lock", MUTEX_DRIVER, *(gcad->iblock));
	    mutex_init(&sp->client_lock, "sp->client_lock", MUTEX_DRIVER, NULL);
	    mutex_init(&sp->cis_lock, "sp->cis_lock", MUTEX_DRIVER, NULL);

	    /* Setup for Socket Services work thread */
	    mutex_init(&sp->ss_thread_lock, "sp->ss_thread_lock",
						MUTEX_DRIVER, NULL);

	    sp->init_state |= SOCKET_INIT_STATE_MUTEX;

	    /* Setup for cs_event_thread */
	    cv_init(&sp->thread_cv, "sp->thread_cv", CV_DRIVER, NULL);
	    cv_init(&sp->caller_cv, "sp->caller_cv", CV_DRIVER, NULL);
	    cv_init(&sp->reset_cv, "sp->reset_cv", CV_DRIVER, NULL);

	    /* Setup for Socket Services work thread */
	    cv_init(&sp->ss_thread_cv, "sp->ss_thread_cv", CV_DRIVER, NULL);
	    cv_init(&sp->ss_caller_cv, "sp->ss_caller_cv", CV_DRIVER, NULL);

	    sp->init_state |= SOCKET_INIT_STATE_CV;

	/*
	 * If we haven't installed it yet, then install the soft interrupt
	 *	handler and save away the softint id.
	 */
	    if (!(cs_globals.init_state & GLOBAL_INIT_STATE_SOFTINTR)) {
		if (ddi_add_softintr(gcad->dip, DDI_SOFTINT_HIGH,
						&sp->softint_id,
						NULL, NULL,
						cs_socket_event_softintr,
						(caddr_t)NULL) != DDI_SUCCESS) {
		    cmn_err(CE_CONT, "cs_init: socket %d can't add softintr\n",
							sp->socket_num);
		    return (BAD_FUNCTION);  /* XXX - what to really return?? */
		} /* ddi_add_softintr */
		mutex_enter(&cs_globals.global_lock);
		cs_globals.softint_id = sp->softint_id;
		cs_globals.init_state |= GLOBAL_INIT_STATE_SOFTINTR;
		/* XXX this timer is hokey at best... */
		cs_globals.sotfint_tmo = timeout(cs_event_softintr_timeout,
							(caddr_t)NULL,
							SOFTINT_TIMEOUT_TIME);
		mutex_exit(&cs_globals.global_lock);
	    } else {
		/*
		 * We've already added the soft interrupt handler, so just
		 *	store away the softint id.
		 */
		sp->softint_id = cs_globals.softint_id;
	    } /* if (!GLOBAL_INIT_STATE_SOFTINTR) */

	/*
	 * While this next flag doesn't really describe a per-socket
	 *	resource, we still set it for each socket.  When the soft
	 *	interrupt handler finally gets removed in cs_deinit, this
	 *	flag will get cleared.
	 */
	    sp->init_state |= SOCKET_INIT_STATE_SOFTINTR;

	/*
	 * Socket Services defaults all sockets to power off and
	 *	clears all event masks.  We want to receive at least
	 *	card insertion events, so enable them.  Turn off power
	 *	to the socket as well.  We will turn it on again when
	 *	we get a card insertion event.
	 */
	    sp->event_mask = CS_EVENT_CARD_INSERTION;
	    set_socket.socket = sp->socket_num;
	    set_socket.SCIntMask = SBM_CD;
	    set_socket.IREQRouting = 0;
	    set_socket.IFType = IF_MEMORY;
	    set_socket.CtlInd = 0; /* turn off controls and indicators */
	    set_socket.State = (unsigned)~0;	/* clear latched state bits */

	    (void) cs_convert_powerlevel(sp->socket_num, 0, VCC,
						&set_socket.VccLevel);
	    (void) cs_convert_powerlevel(sp->socket_num, 0, VPP1,
						&set_socket.Vpp1Level);
	    (void) cs_convert_powerlevel(sp->socket_num, 0, VPP2,
						&set_socket.Vpp2Level);

	    if ((ret = SocketServices(SS_SetSocket, &set_socket)) != SUCCESS) {
		cmn_err(CE_CONT,
		    "cs_init: socket %d SS_SetSocket failure %d\n",
				sp->socket_num, ret);
		return (ret);
	    }

	} /* for (socket_num) */

	/*
	 * Now go through all the sockets again and create the per-socket
	 *	event handler thread; if it gets created, then set the flag
	 *	that tells everyone else that the socket is ready.
	 */
	for (socket_num = 0; socket_num < cs_globals.num_sockets;
							socket_num++) {
	    sp = &cs_sockets[socket_num];

	    /*
	     * Create the per-socket event handler thread.
	     */
	    if (!(sp->event_thread = CREATE_SOCKET_EVENT_THREAD(
							cs_event_thread, sp))) {
		cmn_err(CE_CONT,
			"cs_init: socket %d can't create event thread\n",
							sp->socket_num);
		return (BAD_FUNCTION);
	    }

	    mutex_enter(&sp->lock);
	    sp->init_state |= SOCKET_INIT_STATE_THREAD;
	    mutex_exit(&sp->lock);

	    /*
	     * Create the per-socket Socket Services work thread.
	     */
	    if (!(sp->ss_thread = CREATE_SOCKET_EVENT_THREAD(
							cs_ss_thread, sp))) {
		cmn_err(CE_CONT,
			"cs_init: socket %d can't create Socket Services "
					"work thread\n", sp->socket_num);
		return (BAD_FUNCTION);
	    }

	    mutex_enter(&sp->lock);
	    sp->init_state |= (SOCKET_INIT_STATE_SS_THREAD |
						SOCKET_INIT_STATE_READY);
	    sp->event_mask = CS_EVENT_CARD_INSERTION;
	    mutex_exit(&sp->lock);
	}

	/*
	 * OK, we've done all our setup for now; anything else will happen
	 *	at interrupt time or via requests from a client.
	 * Send in a pointer to the CS event handler and tell SS that
	 *	we're here.
	 * At this point, we should be ready to receive and process events
	 *	that SS calls us with.
	 */
	csr.cs_event = (f_t *)cs_event;

	SocketServices(CSRegister, &csr);

	return (CS_SUCCESS);
}

/*
 * cs_deinit - Deinitialize CS
 *
 * This function cleans up any allocated resources, stops any running threads,
 *	destroys any mutexes and condition variables, and finally frees up the
 *	global socket and window structure arrays.
 */
static int
cs_deinit()
{
	cs_socket_t *sp;
	int sn, have_clients = 0, ret;

#if defined(CS_DEBUG)
	if (cs_debug > 1)
	    cmn_err(CE_CONT, "CS: cs_deinit\n");
#endif

	/*
	 * Set the GLOBAL_INIT_STATE_NO_CLIENTS flag to prevent new clients
	 *	from registering.
	 */
	mutex_enter(&cs_globals.global_lock);
	cs_globals.init_state |= GLOBAL_INIT_STATE_NO_CLIENTS;
	mutex_exit(&cs_globals.global_lock);

	/*
	 * Go through each socket and make sure that there are no clients
	 *	on any of the sockets.  If there are, we can't deinit until
	 *	all the clients for every socket are gone.
	 */
	for (sn = 0; sn < cs_globals.num_sockets; sn++) {
	    sp = &cs_sockets[sn];
	    if (sp->client_list) {
		cmn_err(CE_CONT, "cs_deinit: cannot unload module since "
				"socket %d has registered clients\n", sn);
		have_clients++;
	    }
	}

	if (have_clients)
	    return (BAD_FUNCTION);

	/*
	 * First, tell Socket Services that we're leaving, so that we
	 *	don't get any more event callbacks.
	 */
	SocketServices(CSUnregister);

	/*
	 * Wait for the soft int timer to tell us it's done
	 */
	mutex_enter(&cs_globals.global_lock);
	cs_globals.init_state |= GLOBAL_INIT_STATE_UNLOADING;
	mutex_exit(&cs_globals.global_lock);
	UNTIMEOUT(cs_globals.sotfint_tmo);

	/*
	 * Remove the soft interrupt handler.
	 */
	mutex_enter(&cs_globals.global_lock);
	if (cs_globals.init_state & GLOBAL_INIT_STATE_SOFTINTR) {
	    ddi_remove_softintr(cs_globals.softint_id);
	    cs_globals.init_state &= ~GLOBAL_INIT_STATE_SOFTINTR;
	}
	mutex_exit(&cs_globals.global_lock);

	/*
	 * Go through each socket and free any resource allocated to that
	 *	socket, as well as any mutexs and condition variables.
	 */
	for (sn = 0; sn < cs_globals.num_sockets; sn++) {
	    set_socket_t set_socket;

	    sp = &cs_sockets[sn];

	    /*
	     * untimeout possible pending ready/busy timer
	     */
	    UNTIMEOUT(sp->rdybsy_tmo_id);

	    if (sp->init_state & SOCKET_INIT_STATE_MUTEX)
		mutex_enter(&sp->lock);
	    sp->flags = SOCKET_UNLOAD_MODULE;
	    if (sp->init_state & SOCKET_INIT_STATE_SOFTINTR)
		sp->init_state &= ~SOCKET_INIT_STATE_SOFTINTR;
	    if (sp->init_state & SOCKET_INIT_STATE_MUTEX)
		mutex_exit(&sp->lock);

	    if (sp->init_state & SOCKET_INIT_STATE_MUTEX)
		mutex_enter(&sp->cis_lock);
	    if (sp->cis) {
		CIS_PARSER(CISP_CIS_LIST_DESTROY, &sp->cis);
		sp->cis_flags &= ~CW_VALID_CIS;
	    }
	    if (sp->init_state & SOCKET_INIT_STATE_MUTEX)
		mutex_exit(&sp->cis_lock);

	    /*
	     * Tell the event handler thread that we want it to exit, then
	     *	wait around until it tells us that it has exited.
	     */
	    if (sp->init_state & SOCKET_INIT_STATE_MUTEX)
		mutex_enter(&sp->client_lock);
	    if (sp->init_state & SOCKET_INIT_STATE_THREAD) {
		sp->thread_state = SOCKET_THREAD_EXIT;
		cv_broadcast(&sp->thread_cv);
		cv_wait(&sp->caller_cv, &sp->client_lock);
	    }
	    if (sp->init_state & SOCKET_INIT_STATE_MUTEX)
		mutex_exit(&sp->client_lock);

	    /*
	     * Tell the SS work thread that we want it to exit, then
	     *	wait around until it tells us that it has exited.
	     */
	    if (sp->init_state & SOCKET_INIT_STATE_MUTEX)
		mutex_enter(&sp->ss_thread_lock);
	    if (sp->init_state & SOCKET_INIT_STATE_SS_THREAD) {
		sp->ss_thread_state = SOCKET_THREAD_EXIT;
		cv_broadcast(&sp->ss_thread_cv);
		cv_wait(&sp->ss_caller_cv, &sp->ss_thread_lock);
	    }

	    if (sp->init_state & SOCKET_INIT_STATE_MUTEX)
		mutex_exit(&sp->ss_thread_lock);

	    /*
	     * Free the mutexii and condition variables that we used.
	     */
	    if (sp->init_state & SOCKET_INIT_STATE_MUTEX) {
		mutex_destroy(&sp->lock);
		mutex_destroy(&sp->client_lock);
		mutex_destroy(&sp->cis_lock);
		mutex_destroy(&sp->ss_thread_lock);
	    }

	    if (sp->init_state & SOCKET_INIT_STATE_CV) {
		cv_destroy(&sp->thread_cv);
		cv_destroy(&sp->caller_cv);
		cv_destroy(&sp->reset_cv);
		cv_destroy(&sp->ss_thread_cv);
		cv_destroy(&sp->ss_caller_cv);
	    }

	    /*
	     * Free the memory-mapped IO structure if we allocated one.
	     */
	    if (sp->io_mmap_window)
		kmem_free(sp->io_mmap_window, sizeof (io_mmap_window_t));

	    /*
	     * Return the socket to memory-only mode and turn off the
	     *	socket power.
	     */
	    sp->event_mask = 0;
	    set_socket.socket = sp->socket_num;
	    set_socket.SCIntMask = 0;
	    set_socket.IREQRouting = 0;
	    set_socket.IFType = IF_MEMORY;
	    set_socket.CtlInd = 0; /* turn off controls and indicators */
	    set_socket.State = (unsigned)~0;	/* clear latched state bits */

	    (void) cs_convert_powerlevel(sp->socket_num, 0, VCC,
						&set_socket.VccLevel);
	    (void) cs_convert_powerlevel(sp->socket_num, 0, VPP1,
						&set_socket.Vpp1Level);
	    (void) cs_convert_powerlevel(sp->socket_num, 0, VPP2,
						&set_socket.Vpp2Level);

	    /*
	     * If we fail this call, there's not much we can do, so
	     *	just continue with the resource deallocation.
	     */
	    if ((ret = SocketServices(SS_SetSocket, &set_socket)) != SUCCESS) {
		cmn_err(CE_CONT,
		    "cs_deinit: socket %d SS_SetSocket failure %d\n",
				sp->socket_num, ret);
	    }

	} /* for (sn) */

	/*
	 * Destroy the global mutexii.
	 */
	mutex_destroy(&cs_globals.global_lock);
	mutex_destroy(&cs_globals.window_lock);

	/*
	 * Free the global "super-client" structure
	 */
	if (cs_globals.sclient_list)
	    kmem_free(cs_globals.sclient_list,
		(cs_globals.num_sockets * sizeof (struct sclient_list_t)));
	cs_globals.sclient_list = NULL;

	/*
	 * Free the socket and window structures; by the time we get here,
	 *	all of the pointers hanging off of each socket and window
	 *	structure should have already been freed.
	 */
	if (cs_sockets)
	    kmem_free(cs_sockets, (sizeof (cs_socket_t)) *
						cs_globals.num_sockets);
	cs_sockets = NULL;

	if (cs_windows)
	    kmem_free(cs_windows, (sizeof (cs_window_t)) *
						cs_globals.num_windows);
	cs_windows = NULL;

	/*
	 * Unload the CIS module if it was loaded
	 */
	if (cis_module != -1) {
	    modunload(cis_module);
	    cis_module = -1;
	}

	return (CS_SUCCESS);
}

/*
 * ==== drip, drip, drip - the Card Services waterfall :-) ====
 */

/*
 * CardServices - general Card Services entry point for CS clients
 *			and Socket Services; the address of this
 *			function is handed to SS via the CSRegister
 *			SS call
 */
int
CardServices(int function, ...)
{
	va_list arglist;
	int retcode = CS_UNSUPPORTED_FUNCTION;

#ifdef	CS_DEBUG
	if (cs_debug > 127) {
	    cmn_err(CE_CONT, "CardServices: called for function %s (0x%x)\n",
				cs_csfunc2text(function, CSFUN2TEXT_FUNCTION),
				function);
	}
#endif

	va_start(arglist, function);

	/*
	 * Here's the Card Services waterfall
	 */
	switch (function) {
	    case CISRegister: {
		cisregister_t *cisr;

		    cisr = va_arg(arglist, cisregister_t *);

		    if (cisr->cis_magic != PCCS_MAGIC ||
			cisr->cis_version != PCCS_VERSION) {
			    cmn_err(CE_WARN,
				"CS: CISRegister (%x, %x, %x, %x) *ERROR*",
					(int)cisr->cis_magic,
					(int)cisr->cis_version,
					(int)cisr->cis_parser,
					(int)cisr->cistpl_std_callout);
			retcode = CS_BAD_ARGS;
		    } else {
			cis_parser = cisr->cis_parser;
			cis_cistpl_std_callout = cisr->cistpl_std_callout;
			retcode = CS_SUCCESS;
		    }
		}
		break;
	    case CISUnregister:	/* XXX - should we do some more checking */
		/* XXX - need to protect this by a mutex */
		cis_parser = NULL;
		cis_cistpl_std_callout = NULL;
		retcode = CS_SUCCESS;
		break;
	    case GetCardServicesInfo:
		cmn_err(CE_CONT, "CS: GetCardServicesInfo\n");
		break;
	    case RegisterClient:
		retcode = cs_register_client(
				va_arg(arglist, client_handle_t *),
				va_arg(arglist, client_reg_t *));
		break;
	    case DeregisterClient:
		retcode = cs_deregister_client(
				va_arg(arglist, client_handle_t));
		break;
	    case GetStatus:
		retcode = cs_get_status(
				va_arg(arglist, client_handle_t),
				va_arg(arglist, get_status_t *));
		break;
	    case ResetCard:
		cmn_err(CE_CONT, "CS: ResetCard\n");
		break;
	    case SetEventMask:
		retcode = cs_set_event_mask(
				va_arg(arglist, client_handle_t),
				va_arg(arglist, sockevent_t *));
		break;
	    case GetEventMask:
		retcode = cs_get_event_mask(
				va_arg(arglist, client_handle_t),
				va_arg(arglist, sockevent_t *));
		break;
	    case RequestIO:
		retcode = cs_request_io(
				va_arg(arglist, client_handle_t),
				va_arg(arglist, io_req_t *));
		break;
	    case ReleaseIO:
		retcode = cs_release_io(
				va_arg(arglist, client_handle_t),
				va_arg(arglist, io_req_t *));
		break;
	    case RequestIRQ:
		retcode = cs_request_irq(
				va_arg(arglist, client_handle_t),
				va_arg(arglist, irq_req_t *));
		break;
	    case ReleaseIRQ:
		retcode = cs_release_irq(
				va_arg(arglist, client_handle_t),
				va_arg(arglist, irq_req_t *));
		break;
	    case RequestWindow:
		retcode = cs_request_window(
				va_arg(arglist, client_handle_t),
				va_arg(arglist, window_handle_t *),
				va_arg(arglist, win_req_t *));
		break;
	    case ReleaseWindow:
		retcode = cs_release_window(
				va_arg(arglist, window_handle_t));
		break;
	    case ModifyWindow:
		retcode = cs_modify_window(
				va_arg(arglist, window_handle_t),
				va_arg(arglist, modify_win_t *));
		break;
	    case MapMemPage:
		retcode = cs_map_mem_page(
				va_arg(arglist, window_handle_t),
				va_arg(arglist, map_mem_page_t *));
		break;
	    case RequestSocketMask:
		retcode = cs_request_socket_mask(
				va_arg(arglist, client_handle_t),
				va_arg(arglist, sockevent_t *));
		break;
	    case ReleaseSocketMask:
		retcode = cs_release_socket_mask(
				va_arg(arglist, client_handle_t));
		break;
	    case RequestConfiguration:
		retcode = cs_request_configuration(
				va_arg(arglist, client_handle_t),
				va_arg(arglist, config_req_t *));
		break;
	    case GetConfigurationInfo:
		cmn_err(CE_CONT, "CS: GetConfigurationInfo\n");
		break;
	    case ModifyConfiguration:
		retcode = cs_modify_configuration(
				va_arg(arglist, client_handle_t),
				va_arg(arglist, modify_config_t *));
		break;
	    case AccessConfigurationRegister:
		retcode = cs_access_configuration_register(
				va_arg(arglist, client_handle_t),
				va_arg(arglist, access_config_reg_t *));
		break;
	    case ReleaseConfiguration:
		retcode = cs_release_configuration(
				va_arg(arglist, client_handle_t));
		break;
	    case OpenMemory:
		cmn_err(CE_CONT, "CS: OpenMemory\n");
		break;
	    case ReadMemory:
		cmn_err(CE_CONT, "CS: ReadMemory\n");
		break;
	    case WriteMemory:
		cmn_err(CE_CONT, "CS: WriteMemory\n");
		break;
	    case CopyMemory:
		cmn_err(CE_CONT, "CS: CopyMemory\n");
		break;
	    case RegisterEraseQueue:
		cmn_err(CE_CONT, "CS: RegisterEraseQueue\n");
		break;
	    case CheckEraseQueue:
		cmn_err(CE_CONT, "CS: CheckEraseQueue\n");
		break;
	    case DeregisterEraseQueue:
		cmn_err(CE_CONT, "CS: DeregisterEraseQueue\n");
		break;
	    case CloseMemory:
		cmn_err(CE_CONT, "CS: CloseMemory\n");
		break;
	    case GetFirstRegion:
		cmn_err(CE_CONT, "CS: GetFirstRegion\n");
		break;
	    case GetNextRegion:
		cmn_err(CE_CONT, "CS: GetNextRegion\n");
		break;
	    case GetFirstPartition:
		cmn_err(CE_CONT, "CS: GetFirstPartition\n");
		break;
	    case GetNextPartition:
		cmn_err(CE_CONT, "CS: GetNextPartition\n");
		break;
	    case ReturnSSEntry:
		cmn_err(CE_CONT, "CS: ReturnSSEntry\n");
		break;
	    case MapLogSocket:
		retcode = cs_map_log_socket(
				va_arg(arglist, client_handle_t),
				va_arg(arglist, map_log_socket_t *));
		break;
	    case MapPhySocket:
		cmn_err(CE_CONT, "CS: MapPhySocket\n");
		break;
	    case MapLogWindow:
		cmn_err(CE_CONT, "CS: MapLogWindow\n");
		break;
	    case MapPhyWindow:
		cmn_err(CE_CONT, "CS: MapPhyWindow\n");
		break;
	    case RegisterMTD:
		cmn_err(CE_CONT, "CS: RegisterMTD\n");
		break;
	    case RegisterTimer:
		cmn_err(CE_CONT, "CS: RegisterTimer\n");
		break;
	    case SetRegion:
		cmn_err(CE_CONT, "CS: SetRegion\n");
		break;
	    case RequestExclusive:
		cmn_err(CE_CONT, "CS: RequestExclusive\n");
		break;
	    case ReleaseExclusive:
		cmn_err(CE_CONT, "CS: ReleaseExclusive\n");
		break;
	    case GetFirstClient:
		cmn_err(CE_CONT, "CS: GetFirstClient\n");
		break;
	    case GetNextClient:
		cmn_err(CE_CONT, "CS: GetNextClient\n");
		break;
	    case GetClientInfo:
		cmn_err(CE_CONT, "CS: GetClientInfo\n");
		break;
	    case AddSocketServices:
		cmn_err(CE_CONT, "CS: AddSocketServices\n");
		break;
	    case ReplaceSocketServices:
		cmn_err(CE_CONT, "CS: ReplaceSocketServices\n");
		break;
	    case VendorSpecific:
		cmn_err(CE_CONT, "CS: VendorSpecific\n");
		break;
	    case AdjustResourceInfo:
		cmn_err(CE_CONT, "CS: AdjustResourceInfo\n");
		break;
	    case ValidateCIS:
		retcode = cs_validate_cis(
				va_arg(arglist, client_handle_t),
				va_arg(arglist, cisinfo_t *));
		break;
	    case GetFirstTuple:
		retcode = cs_get_firstnext_tuple(
				va_arg(arglist, client_handle_t),
				va_arg(arglist, tuple_t *),
				CS_GET_FIRST_FLAG);
		break;
	    case GetNextTuple:
		retcode = cs_get_firstnext_tuple(
				va_arg(arglist, client_handle_t),
				va_arg(arglist, tuple_t *),
				CS_GET_NEXT_FLAG);
		break;
	    case GetTupleData:
		retcode = cs_get_tuple_data(
				va_arg(arglist, client_handle_t),
				va_arg(arglist, tuple_t *));
		break;
	    case ParseTuple:
		retcode = cs_parse_tuple(
				va_arg(arglist, client_handle_t),
				va_arg(arglist, tuple_t *),
				va_arg(arglist, cisparse_t *),
				va_arg(arglist, cisdata_t));
		break;
	    case MakeDeviceNode:
		retcode = cs_make_device_node(
				va_arg(arglist, client_handle_t),
				va_arg(arglist, make_device_node_t *));
		break;
	    case ConvertSpeed:
		retcode = cs_convert_speed(
				va_arg(arglist, convert_speed_t *));
		break;
	    case ConvertSize:
		retcode = cs_convert_size(
				va_arg(arglist, convert_size_t *));
		break;
	    case Event2Text:
		retcode = cs_event2text(
				va_arg(arglist, event2text_t *), 1);
		break;
	    case Function2Text: {
		cs_csfunc2text_strings_t *cft;

			cft = va_arg(arglist, cs_csfunc2text_strings_t *);
			cft->text =
				cs_csfunc2text(cft->item, CSFUN2TEXT_RETURN);

		retcode = CS_SUCCESS;
		}
		break;
	    case CS_DDI_Info:
		retcode = cs_ddi_info(va_arg(arglist, cs_ddi_info_t *));
		break;
	    default:
		cmn_err(CE_CONT, "CS: {unknown function %d}\n", function);
		break;
	} /* switch(function) */

#ifdef	CS_DEBUG
	if (cs_debug > 127) {
	    cmn_err(CE_CONT, "CardServices: returning %s (0x%x)\n",
				cs_csfunc2text(retcode, CSFUN2TEXT_RETURN),
				retcode);
	}
#endif

	return (retcode);
}

/*
 * ==== tuple and CIS handling section ====
 */

/*
 * cs_parse_tuple - This function supports the CS ParseTuple function call
 */
static int
cs_parse_tuple(client_handle_t client_handle, tuple_t *tuple,
			cisparse_t *cisparse, cisdata_t cisdata)
{
	cs_socket_t *sp;
	client_t *client;
	int ret;

	if ((ret = cs_get_socket(client_handle, &tuple->Socket, &sp, &client))
								!= CS_SUCCESS)
	    return (ret);

	/*
	 * If there's no card in the socket or the card in the socket is not
	 *	for this client, then return an error.
	 */
	if (!(client->flags & CLIENT_CARD_INSERTED))
	    return (CS_NO_CARD);

	mutex_enter(&sp->cis_lock);

	if (sp->cis_flags & CW_VALID_CIS) {
	    if ((unsigned)CIS_PARSER(CISP_CIS_PARSE_TUPLE,
				cis_cistpl_std_callout,
				tuple->CIS_Data, HANDTPL_PARSE_LTUPLE,
				cisparse, cisdata) == CISTPLF_UNKNOWN) {
		mutex_exit(&sp->cis_lock);
		return (CS_UNKNOWN_TUPLE);
	    }
	    ret = CS_SUCCESS;
	} else {
	    ret = CS_NO_CIS;
	} /* if (CW_VALID_CIS) */

	mutex_exit(&sp->cis_lock);

	return (ret); /* XXX - should use the handler's return value */
}

/*
 * cs_get_firstnext_tuple - returns the first/next tuple of the specified type
 *				this is to support the GetFirstTuple and
 *				GetNextTuple function call
 *
 *    flags - CS_GET_FIRST_FLAG causes function to support GetFirstTuple
 *	      CS_GET_NEXT_FLAG causes function to support GetNextTuple
 */
static int
cs_get_firstnext_tuple(client_handle_t client_handle, tuple_t *tuple, int flags)
{
	cs_socket_t *sp;
	client_t *client;
	int ret;

	if ((ret = cs_get_socket(client_handle, &tuple->Socket, &sp, &client))
								!= CS_SUCCESS)
	    return (ret);

	/*
	 * If there's no card in the socket or the card in the socket is not
	 *	for this client, then return an error.
	 */
	if (!(client->flags & CLIENT_CARD_INSERTED))
	    return (CS_NO_CARD);

	mutex_enter(&sp->cis_lock);

	/*
	 * If there's no CIS, then we can't do much.
	 */
	if (!(sp->cis_flags & CW_VALID_CIS)) {
	    mutex_exit(&sp->cis_lock);
	    return (CS_NO_CIS);
	}

	/*
	 * Are we GetFirstTuple or GetNextTuple?
	 */
	if (flags & CS_GET_FIRST_FLAG) {
	/*
	 * Initialize the tuple structure; we need this information when
	 *	we have to process a GetNextTuple or ParseTuple call.
	 */
	    tuple->CIS_Data = sp->cis;
	} else {
	/*
	 * Check to be sure that we have a non-NULL tuple list pointer.
	 *	This is necessary in the case where the caller calls us
	 *	with get next tuple requests but we don't have any more
	 *	tuples to give back.
	 */
	    if (!(tuple->CIS_Data)) {
		mutex_exit(&sp->cis_lock);
		return (CS_NO_MORE_ITEMS);
	    }
	/*
	 * Point to the next tuple in the list.  If we're searching for
	 *	a particular tuple, FIND_LTUPLE_FWD will find it.
	 */
	    if ((tuple->CIS_Data = GET_NEXT_LTUPLE(tuple->CIS_Data)) == NULL) {
		mutex_exit(&sp->cis_lock);
		return (CS_NO_MORE_ITEMS);
	    }
	}

	/*
	 * Check if we want to get the first of a particular type of tuple
	 *	or just the first tuple in the chain.
	 */
	if (tuple->DesiredTuple != RETURN_FIRST_TUPLE) {
	    if (!(tuple->CIS_Data = FIND_LTUPLE_FWD(tuple->CIS_Data,
				tuple->DesiredTuple))) {
		mutex_exit(&sp->cis_lock);
		return (CS_NO_MORE_ITEMS);
	    }
	}

	/*
	 * We've got a tuple, now fill out the rest of the tuple_t
	 *	structure.  Callers can use the flags member to
	 *	determine whether or not the tuple data was copied
	 *	to the linked list or if it's still on the card.
	 */
	tuple->Flags = tuple->CIS_Data->flags;
	tuple->TupleCode = tuple->CIS_Data->type;
	tuple->TupleLink = tuple->CIS_Data->len;

	mutex_exit(&sp->cis_lock);

	return (CS_SUCCESS);
}

/*
 * cs_get_tuple_data - get the data portion of a tuple; this is to
 *	support the GetTupleData function call.
 *
 *    Note that if the data body of a tuple was not read from the CIS,
 *	then this function will return CS_NO_MORE_ITEMS and the flags
 *	member of the tuple_t structure will have the CISTPLF_COPYOK
 *	and CISTPLF_LM_SPACE flags set; these flags are set by the
 *	tuple handler in the CIS parser and copied into the tuple_t
 *	flags member by the GetFirstTuple/GetNextTuple function calls.
 */
static int
cs_get_tuple_data(client_handle_t client_handle, tuple_t *tuple)
{
	cs_socket_t *sp;
	client_t *client;
	int ret, nbytes;
	cisdata_t *tsd, *tdd;

	if ((ret = cs_get_socket(client_handle, &tuple->Socket, &sp, &client))
								!= CS_SUCCESS)
	    return (ret);

	/*
	 * If there's no card in the socket or the card in the socket is not
	 *	for this client, then return an error.
	 */
	if (!(client->flags & CLIENT_CARD_INSERTED))
	    return (CS_NO_CARD);

	mutex_enter(&sp->cis_lock);

	if (sp->cis_flags & CW_VALID_CIS) {

	    /*
	     * Check to be sure that we have a non-NULL pointer to
	     *	a CIS list.
	     */
	    if (!(tuple->CIS_Data)) {
		mutex_exit(&sp->cis_lock);
		return (CS_NO_MORE_ITEMS);
	    }

	    /*
	     * Do we have a copy of the tuple data?  If not, return an error.
	     */
	    if (!(tuple->CIS_Data->flags & CISTPLF_LM_SPACE)) {
		mutex_exit(&sp->cis_lock);
		return (CS_NO_MORE_ITEMS);
	    }

	/*
	 * Make sure the requested offset is not past the end of the
	 *	tuple data body nor past the end of the user-supplied
	 *	buffer;
	 */
	    if ((int)tuple->TupleOffset >= min((int)tuple->TupleLink,
						(int)tuple->TupleDataMax)) {
		mutex_exit(&sp->cis_lock);
		return (CS_BAD_ARGS);
	    }

	    tuple->TupleDataLen = tuple->TupleLink;

	    if ((nbytes = min((int)tuple->TupleDataMax -
						(int)tuple->TupleOffset,
						(int)tuple->TupleDataLen -
						(int)tuple->TupleOffset)) < 1) {
		mutex_exit(&sp->cis_lock);
		return (CS_BAD_ARGS);
	    }

	    tsd = (tuple->CIS_Data->data + (unsigned)tuple->TupleOffset);
	    tdd = (tuple->TupleData + (unsigned)tuple->TupleOffset);

	    while (nbytes--)
		*tdd++ = *tsd++;

	    ret = CS_SUCCESS;
	} else {
	    ret = CS_NO_CIS;
	} /* if (CW_VALID_CIS) */

	mutex_exit(&sp->cis_lock);

	return (ret);
}

/*
 * cs_get_socket - returns the socket number and a pointer to the socket
 *			structure
 *
 * calling:	client_handle_t client_handle - client handle to extract
 *						socket number from
 *		u_long *socket -  pointer to socket number to use if
 *					client_handle is for the SS client;
 *					this value will be filled in on
 *					return with the correct socket
 *					number if we return CS_SUCCESS
 *		cs_socket_t **sp - pointer to a pointer where a pointer
 *					to the socket struct will be
 *					placed if this is non-NULL
 *		client_t **clp - pointer to a pointer where a pointer
 *					to the client struct will be
 *					placed if this is non-NULL
 */
static u_long
cs_get_socket(client_handle_t client_handle, u_long *socket,
			cs_socket_t **csp, client_t **clp)
{
	cs_socket_t *sp;
	client_t *client;
	int ret;

	/*
	 * If this is the Socket Services client, then honor the socket
	 *	argument, otherwise set the socket number to the client's
	 *	socket.
	 */
	if (!(CLIENT_HANDLE_IS_SS(client_handle)))
	    *socket = GET_CLIENT_SOCKET(client_handle);

	/*
	 * Check to be sure that the socket number is in range
	 */
	if (!(CHECK_SOCKET_NUM(*socket, cs_globals.num_sockets)))
	    return (CS_BAD_SOCKET);

	sp = &cs_sockets[*socket];

	/*
	 * If we were given a pointer, then fill it in with a pointer
	 *	to this socket.
	 */
	if (csp)
	    *csp = sp;

	/*
	 * Search for the client; if it's not found, return an error.
	 */
	mutex_enter(&sp->lock);
	if (!(client = cs_find_client(client_handle, &ret))) {
	    mutex_exit(&sp->lock);
	    return (ret);
	}
	mutex_exit(&sp->lock);

	if (clp)
	    *clp = client;

	return (CS_SUCCESS);
}

/*
 * cs_validate_cis - validates the CIS on a card in the given socket; this
 *			is to support the ValidateCIS function call.
 */
static int
cs_validate_cis(client_handle_t client_handle, cisinfo_t *cisinfo)
{
	cs_socket_t *sp;
	client_t *client;
	int ret;

	if ((ret = cs_get_socket(client_handle, &cisinfo->Socket, &sp, &client))
					!= CS_SUCCESS)
	    return (ret);

	/*
	 * If there's no card in the socket or the card in the socket is not
	 *	for this client, then return an error.
	 */
	if (!(client->flags & CLIENT_CARD_INSERTED))
	    return (CS_NO_CARD);

	/*
	 * XXX - This is cheesy way to validate the CIS.  We should find
	 *	a better way to do this.
	 * XXX - There should really be a CIS Parser function to validate
	 *	the CIS for us.
	 */
	mutex_enter(&sp->cis_lock);
	if (sp->cis_flags & CW_VALID_CIS) {
	    cisinfo->Chains = sp->nchains;
	    cisinfo->Tuples = sp->ntuples;
	    ret = CS_SUCCESS;
	} else {
	    cisinfo->Chains = 0;
	    cisinfo->Tuples = 0;
	    ret = CS_NO_CIS;
	}
	mutex_exit(&sp->cis_lock);

	return (ret);
}

/*
 * cs_init_cis_window - initializes the CIS window for the passed socket
 *
 *	calling: *sp - pointer to the per-socket structure
 *		 offset - offset from start of AM space
 *		 *newbase - pointer to optional u_long to store modified
 *				base address in
 *
 *	returns: CS_SUCCESS if CIS window was set up
 *		 CS_BAD_WINDOW if CIS window could not be setup
 *		 CS_GENERAL_FAILURE if socket has a CIS window number
 *					but the window flags are wrong
 *
 *	Note: This function will check to be sure that there is a valid
 *		CIS window allocated to this socket.
 *	      If there is an error in setting up the window hardware, the
 *		CIS window information for this socket is cleared.
 *	      This function is also used by routines that need to get
 *		a pointer to the base of AM space to access the card's
 *		configuration registers.
 *	      The passed offset is the un-window-size-aligned offset.
 */
caddr_t
cs_init_cis_window(cs_socket_t *sp, u_long offset, u_long *newbase)
{
	set_window_t sw;
	get_window_t gw;
	inquire_window_t iw;
	set_page_t set_page;
	cs_window_t *cw;

	/*
	 * Check to be sure that we have a valid CIS window
	 */
	if (!SOCKET_HAS_CIS_WINDOW(sp)) {
	    cmn_err(CE_CONT,
			"cs_init_cis_window: socket %d has no CIS window\n",
				sp->socket_num);
	    return (NULL);
	}

	/*
	 * Check to be sure that this window is allocated for CIS use
	 */
	cw = &cs_windows[sp->cis_win_num];
	if (!(cw->state & CW_CIS)) {
	    cmn_err(CE_CONT,
		"cs_init_cis_window: socket %d invalid CIS window state 0x%x\n",
				sp->socket_num, cw->state);
	    return (NULL);
	}

	/*
	 * Get the characteristics of this window - we use this to
	 *	determine whether we need to re-map the window or
	 *	just move the window offset on the card.
	 */
	iw.window = sp->cis_win_num;
	SocketServices(SS_InquireWindow, &iw);

	/*
	 * We've got a window, now set up the hardware. If we've got
	 *	a variable sized window, then all we need to do is to
	 *	get a valid mapping to the base of the window using
	 *	the current window size; if we've got a fixed-size
	 *	window, then we need to get a mapping to the window
	 *	starting at offset zero of the window.
	 */
	if (iw.mem_win_char.MemWndCaps & WC_SIZE) {
	    sw.WindowSize = sp->cis_win_size;
	    set_page.offset = ((offset / sp->cis_win_size) *
						sp->cis_win_size);
	} else {
	    set_page.offset = ((offset / iw.mem_win_char.MinSize) *
						iw.mem_win_char.MinSize);
	    sw.WindowSize = (((offset & ~(PAGESIZE - 1)) &
					(set_page.offset - 1)) + PAGESIZE);
	}

	/*
	 * If the caller wants us to return a normalized base offset,
	 *	do that here. This takes care of the case where the
	 *	required offset is greater than the window size.
	 */
	if (newbase)
	    *newbase = *newbase & (set_page.offset - 1);

#ifdef	CS_DEBUG
	if (cs_debug > 1)
	    cmn_err(CE_CONT, "cs_init_cis_window: WindowSize 0x%x "
							"offset 0x%x\n",
							(int)sw.WindowSize,
							(int)set_page.offset);
	if ((cs_debug > 1) && newbase)
	    cmn_err(CE_CONT, "cs_init_cis_window: *newbase = 0x%x",
							(int)*newbase);
#endif

	sw.window = sp->cis_win_num;
	sw.socket = sp->socket_num;
	sw.state = (WS_ENABLED | WS_EXACT_MAPIN);
	/*
	 * The PCMCIA SS spec specifies this be expressed in
	 *	a device speed format per 5.2.7.1.3 but
	 *	our implementation of SS_SetWindow uses
	 *	actual nanoseconds.
	 */
	sw.speed = CIS_DEFAULT_SPEED;
	sw.base = 0;
	/*
	 * Set up the window - if this fails, then just set the
	 *	CIS window number back to it's initialized value so
	 *	that we'll fail when we break out of the loop.
	 */
	if (SocketServices(SS_SetWindow, &sw) != SUCCESS) {
	    sp->cis_win_num = cs_globals.num_windows;
	    cw->state = 0; /* XXX do we really want to do this? */
	    return (NULL);
	} else {

		set_page.window = sp->cis_win_num;
		set_page.page = 0;
		set_page.state = (PS_ATTRIBUTE | PS_ENABLED);

		if (SocketServices(SS_SetPage, &set_page) != SUCCESS) {
		    sp->cis_win_num = cs_globals.num_windows;
		    cw->state = 0; /* XXX do we really want to do this? */
		    return (NULL);
		} /* if (SS_SetPage) */
	} /* if (SS_SetWindow) */

	/*
	 * Get the window information for the CIS window for this socket.
	 */
	gw.window = sp->cis_win_num;
	gw.socket = sp->socket_num; /* XXX - SS_GetWindow should set this */
	if (SocketServices(SS_GetWindow, &gw) != SUCCESS)
	    return (NULL);

	return ((caddr_t)gw.base);
}

/*
 * ==== client registration/deregistration section ====
 */

/*
 * cs_register_client - This supports the RegisterClient call.
 *
 * Upon successful registration, the client_handle_t * handle argument will
 *	contain the new client handle and we return CS_SUCCESS.
 */
static int
cs_register_client(client_handle_t *ch, client_reg_t *cr)
{
	u_long sn;
	int super_client = 0;
	sclient_reg_t *scr = cr->private;
	struct sclient_list_t *scli;

	/*
	 * See if we're not supposed to register any new clients.
	 */
	if (cs_globals.init_state & GLOBAL_INIT_STATE_NO_CLIENTS)
	    return (CS_OUT_OF_RESOURCE);

	/*
	 * Do a version check - if the client expects a later version of
	 *	Card Services than what we are, return CS_BAD_VERSION.
	 * XXX - How do we specify just a PARTICULAR version of CS??
	 */
	if (CS_VERSION < cr->Version)
	    return (CS_BAD_VERSION);

	/*
	 * Check to be sure that the client has given us a valid set of
	 *	client type flags.  We also use this opportunity to see
	 *	if the registering client is Socket Services or is a
	 *	"super-client".
	 *
	 * Note that SS can not set any flag in the Attributes field other
	 *	than the INFO_SOCKET_SERVICES flag.
	 *
	 * Valid combinations of cr->Attributes and cr->EventMask flags:
	 *
	 *  for Socket Services:
	 *	cr->Attributes:
	 *	    set:
	 *		INFO_SOCKET_SERVICES
	 *	    clear:
	 *		{all other flags}
	 *	cr->EventMask:
	 *	    don't care:
	 *		{all flags}
	 *
	 *  for regular clients:
	 *	cr->Attributes:
	 *	    only one of:
	 *		INFO_IO_CLIENT
	 *		INFO_MTD_CLIENT
	 *		INFO_MEM_CLIENT
	 *	    don't care:
	 *		INFO_CARD_SHARE
	 *		INFO_CARD_EXCL
	 *	cr->EventMask:
	 *	    clear:
	 *		CS_EVENT_ALL_CLIENTS
	 *	    don't care:
	 *		{all other flags}
	 *
	 *  for "super-clients":
	 *	cr->Attributes:
	 *	    set:
	 *		INFO_IO_CLIENT
	 *		INFO_MTD_CLIENT
	 *		INFO_SOCKET_SERVICES
	 *		INFO_CARD_SHARE
	 *	    clear:
	 *		INFO_MEM_CLIENT
	 *		INFO_CARD_EXCL
	 *	cr->EventMask:
	 *	    don't care:
	 *		{all flags}
	 */
	switch (cr->Attributes & (INFO_SOCKET_SERVICES | INFO_IO_CLIENT |
					INFO_MTD_CLIENT | INFO_MEM_CLIENT)) {
	/*
	 * Check first to see if this is Socket Services registering; if
	 *	so, we don't do anything but return the client handle that is
	 *	in the global SS client.
	 */
	    case INFO_SOCKET_SERVICES:
		*ch = cs_socket_services_client.client_handle;
		return (CS_SUCCESS);
		/* NOTREACHED */
	    /* regular clients */
	    case INFO_IO_CLIENT:
	    case INFO_MTD_CLIENT:
	    case INFO_MEM_CLIENT:
		if (cr->EventMask & CS_EVENT_ALL_CLIENTS)
		    return (CS_BAD_ATTRIBUTE);
		break;
	    /* "super-client" clients */
	    case (INFO_IO_CLIENT | INFO_MTD_CLIENT | INFO_SOCKET_SERVICES):
		if ((!(cr->Attributes & INFO_CARD_SHARE)) ||
				(cr->Attributes & INFO_CARD_EXCL))
		    return (CS_BAD_ATTRIBUTE);
		/*
		 * We only allow one "super-client" per system.
		 */
		mutex_enter(&cs_globals.global_lock);
		if (cs_globals.flags & GLOBAL_SUPER_CLIENT_REGISTERED) {
		    mutex_exit(&cs_globals.global_lock);
		    return (CS_NO_MORE_ITEMS);
		}
		cs_globals.flags |= GLOBAL_SUPER_CLIENT_REGISTERED;
		mutex_exit(&cs_globals.global_lock);
		super_client = CLIENT_SUPER_CLIENT;
		break;
	    default:
		return (CS_BAD_ATTRIBUTE);
	} /* switch (cr->Attributes) */

	/*
	 * Now, actually create the client node on the socket; this will
	 *	also return the new client handle if there were no errors
	 *	creating the client node.
	 */
	if (super_client != CLIENT_SUPER_CLIENT) {
	    if ((sn = DIP2SOCKET_NUM(cr->dip)) == cs_globals.num_sockets) {
		/* XXX */
		cmn_err(CE_CONT, "cs_register_client: !! WARNING !! "
						"DIP2SOCKET_NUM(0x%x) = %d\n",
							(int)cr->dip, (int)sn);
		return (CS_GENERAL_FAILURE);
	    }
#ifdef	CS_DEBUG
	    if (cs_debug > 1) {
		cmn_err(CE_CONT, "cs_register_client: DIP2SOCKET_NUM = 0x%x\n",
								(int)sn);
	    }
#endif

	    return (cs_add_client_to_socket(sn, ch, cr, super_client));
	}

	/*
	 * This registering client is a "super-client", so we create one
	 *	client node for each socket in the system.  We use the
	 *	client_reg_t.private structure member to point to a struct
	 *	that the "super-client" client knows about.  The client
	 *	handle pointer is not used in this case.
	 * We return CS_SUCCESS if at least one client node could be
	 *	created.  The client must check the error codes in the
	 *	error code array to determine which clients could not
	 *	be created on which sockets.
	 * We return CS_BAD_HANDLE if no client nodes could be created.
	 */
	scr->num_clients = 0;
	scr->num_sockets = cs_globals.num_sockets;
	scr->num_windows = cs_globals.num_windows;

	*(scr->sclient_list) = cs_globals.sclient_list;

	for (sn = 0; sn < scr->num_sockets; sn++) {
	    scli = scr->sclient_list[sn];
	    if ((scli->error = cs_add_client_to_socket(sn, &scli->client_handle,
					    cr, super_client)) == CS_SUCCESS) {
		scr->num_clients++;
	    }
	}

	/*
	 * If we couldn't create any client nodes at all, then
	 *	return an error.
	 */
	if (!scr->num_clients) {
	/*
	 * XXX - The global superclient lock now gets
	 * cleared in cs_deregister_client
	 */
	    /* cs_clear_superclient_lock(super_client); */
	    return (CS_BAD_HANDLE);
	}

	return (CS_SUCCESS);
}

/*
 * cs_add_client_to_socket - this function creates the client node on the
 *				requested socket.
 *
 * Note that if we return an error, there is no state that can be cleaned
 *	up.  The only way that we can return an error with allocated resources
 *	would be if one of the client handle functions had an internal error.
 *	Since we wouldn't get a valid client handle in this case anyway, there
 *	would be no way to find out what was allocated and what wasn't.
 */
static int
cs_add_client_to_socket(unsigned sn, client_handle_t *ch,
					client_reg_t *cr, int super_client)
{
	cs_socket_t *sp;
	client_t *client, *cclp;
	int error;

	sp = &cs_sockets[sn];

	EVENT_THREAD_MUTEX_ENTER(sp);

	/*
	 * Run through all of the registered clients and compare the passed
	 *	dip to the dip of each client to make sure that this client
	 *	is not trying to register more than once.  If they are, then
	 *	display a message and return an error.
	 * XXX - we should really check all the sockets in case the client
	 *	manipulates the instance number in the dip.
	 * XXX - if we check each socket, we ned to also check for the
	 *	"super-client" since it will use the same dip for all
	 *	of it's client nodes.
	 */
	mutex_enter(&sp->lock);
	client = sp->client_list;
	while (client) {
	    if (client->dip == cr->dip) {
		mutex_exit(&sp->lock);
		EVENT_THREAD_MUTEX_EXIT(sp);
		cmn_err(CE_CONT, "cs_add_client_to_socket: socket %d client "
					" already registered with client"
					" handle 0x%x\n",
						(int)sn,
						(int)client->client_handle);
		return (CS_BAD_HANDLE);
	    }
	    client = client->next;
	} /* while (client) */
	mutex_exit(&sp->lock);

	/*
	 * Create a unique client handle then make sure that we can find it.
	 *	This has the side effect of getting us a pointer to the
	 *	client structure as well.
	 * Create a client list entry - cs_create_client_handle will use this
	 *	as the new client node.
	 * We do it here so that we can grab the sp->lock mutex for the
	 *	duration of our manipulation of the client list.
	 * If this function fails, then it will not have added the newly
	 *	allocated client node to the clietn list on this socket,
	 *	so we have to free the node that we allocated.
	 */
	cclp = (client_t *)kmem_zalloc(sizeof (client_t), KM_SLEEP);

	mutex_enter(&sp->lock);
	if (!(*ch = cs_create_client_handle(sn, cclp))) {
	    mutex_exit(&sp->lock);
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    kmem_free(cclp, sizeof (client_t));
	    return (CS_OUT_OF_RESOURCE);
	}

	/*
	 *  Make sure that this is a valid client handle.  We should never
	 *	fail this since we just got a valid client handle.
	 * If this fails, then we have an internal error so don't bother
	 *	trying to clean up the allocated client handle since the
	 *	whole system is probably hosed anyway and will shortly
	 *	esplode.
	 * It doesn't make sense to call cs_deregister_client at this point
	 *	to clean up this broken client since the deregistration
	 *	code will also call cs_find_client and most likely fail.
	 */
	if (!(client = cs_find_client(*ch, &error))) {
	    mutex_exit(&sp->lock);
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    cmn_err(CE_CONT, "cs_add_client_to_socket: socket %d invalid "
				"client handle created handle 0x%x\n",
							(int)sn, (int)*ch);
	    return (error);
	}

	/*
	 * Save the DDI information.
	 */
	client->dip = cr->dip;
	client->driver_name = (char *)kmem_zalloc(strlen(cr->driver_name) + 1,
							KM_SLEEP);
	strcpy(client->driver_name, cr->driver_name);
	client->instance = ddi_get_instance(cr->dip);

	/*
	 * Copy over the interesting items that the client gave us.
	 */
	client->flags = cr->Attributes &
			(INFO_IO_CLIENT | INFO_MTD_CLIENT | INFO_MEM_CLIENT);
	client->event_callback_handler = cr->event_handler;
	bcopy((caddr_t)&cr->event_callback_args,
				(caddr_t)&client->event_callback_args,
				sizeof (event_callback_args_t));
	/*
	 * Set the client handle since the client needs a client handle
	 *	when they call us for their event handler.
	 */
	client->event_callback_args.client_handle = *ch;

	/*
	 * Initialize the IO window numbers; if an IO window number is equal
	 *	to cs_globals.num_windows it means that IO range is not in
	 *	use.
	 */
	client->io_alloc.Window1 = cs_globals.num_windows;
	client->io_alloc.Window2 = cs_globals.num_windows;

	/*
	 * Give the client the iblock and idevice cookies to use in
	 *	the client's event handler high priority mutex.
	 */
	cr->iblk_cookie = sp->iblk;
	cr->idev_cookie = sp->idev;

	/*
	 * Set up the global event mask information; we copy this directly
	 *	from the client; since we are the only source of events,
	 *	any bogus bits that the client puts in here won't matter
	 *	because we'll never look at them.
	 */
	client->global_mask = cr->EventMask;

	/*
	 * If this client registered as a "super-client" set the appropriate
	 *	flag in the client's flags area.
	 */
	if (super_client == CLIENT_SUPER_CLIENT)
	    client->flags |= CLIENT_SUPER_CLIENT;

	/*
	 * Determine if we should give artificial card insertion events and
	 *	a registration complete event. Since we don't differentiate
	 *	between sharable and exclusive use cards when giving clients
	 *	event notification, we modify the definition of the share/excl
	 *	flags as follows:
	 *
	 *	    If either INFO_CARD_SHARE or INFO_CARD_EXCL is set,
	 *	    the client will receive artificial card insertion
	 *	    events (if the client's card is currently in the
	 *	    socket) and a registration complete event.
	 *
	 *	    If neither of the INFO_CARD_SHARE or INFO_CARD_EXCL is
	 *	    set, the client will not receive an artificial card
	 *	    insertion event nor a registration complete event
	 *	    due to the client's call to register client.
	 *
	 *	    The client's event mask is not affected by the setting
	 *	    of these two bits.
	 */
	if (cr->Attributes & (INFO_CARD_SHARE | INFO_CARD_EXCL)) {
	    client->pending_events = (CS_EVENT_CARD_INSERTION |
				CS_EVENT_REGISTRATION_COMPLETE);
	}

	/*
	 * Check to see if the card for this client is currently in
	 *	the socket. If it is, then set CLIENT_CARD_INSERTED
	 *	since clients that are calling GetStatus at attach
	 *	time will typically check to see if their card is
	 *	currently installed.
	 */
	if (cs_card_for_client(client))
	    client->flags |= CLIENT_CARD_INSERTED;

	mutex_exit(&sp->lock);
	EVENT_THREAD_MUTEX_EXIT(sp);

	return (CS_SUCCESS);
}

/*
 * cs_deregister_client - This supports the DeregisterClient call.
 */
static int
cs_deregister_client(client_handle_t client_handle)
{
	cs_socket_t *sp;
	client_t *client;
	int error, super_client = 0;

	/*
	 * Check to see if this is the Socket Services client handle; if it
	 *	is, we don't do anything except for return success.
	 */
	if (CLIENT_HANDLE_IS_SS(client_handle))
	    return (CS_SUCCESS);

	/*
	 * Get a pointer to this client's socket structure.
	 */
	sp = &cs_sockets[GET_CLIENT_SOCKET(client_handle)];

	EVENT_THREAD_MUTEX_ENTER(sp);

	/*
	 *  Make sure that this is a valid client handle.
	 */
	if (!(client = cs_find_client(client_handle, &error))) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (error);
	}

	/*
	 * Make sure that any resources allocated by this client are
	 *	not still allocated, and that if this is an MTD that
	 *	no MTD operations are still in progress.
	 */
	if (client->flags &    (CLIENT_IO_ALLOCATED	|
				CLIENT_IRQ_ALLOCATED	|
				CLIENT_WIN_ALLOCATED	|
				REQ_CONFIGURATION_DONE	|
				REQ_SOCKET_MASK_DONE	|
				REQ_IO_DONE		|
				REQ_IRQ_DONE)) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_BUSY);
	}

	if (client->flags & CLIENT_MTD_IN_PROGRESS) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_IN_USE);
	}

	/*
	 * Any previously allocated resources are not allocated anymore, and
	 *	no MTD operations are in progress, so if this is an MTD client
	 *	then do any MTD-specific client deregistration, and then
	 *	nuke this client.
	 * We expect cs_deregister_mtd to never fail.
	 */
	if (client->flags & INFO_MTD_CLIENT)
	    (void) cs_deregister_mtd(client_handle);

	if (client->flags & CLIENT_SUPER_CLIENT)
	    super_client = CLIENT_SUPER_CLIENT;

	kmem_free(client->driver_name, strlen(client->driver_name) + 1);

	error = cs_destroy_client_handle(client_handle);

	EVENT_THREAD_MUTEX_EXIT(sp);

	/*
	 * If this was the "super-client" deregistering, then this
	 *	will clear the global "super-client" lock.
	 * XXX - move this outside the per-socket code.
	 */
	cs_clear_superclient_lock(super_client);

	return (error);
}

/*
 * cs_create_next_client_minor - returns the next available client minor
 *					number or 0 if none available
 *
 * Note that cs_find_client will always return a valid pointer to the
 *	global Socket Services client which has a client minor number
 *	of 0; this means that this function can never return a 0 as the
 *	next valid available client minor number.
 */
unsigned
cs_create_next_client_minor(unsigned socket_num, unsigned next_minor)
{
	unsigned max_client_handles = cs_max_client_handles;

	do {
	    next_minor &= CS_MAX_CLIENTS_MASK;
	    if (!cs_find_client(MAKE_CLIENT_HANDLE(socket_num, next_minor),
						NULL)) {
		return (next_minor);
	    }
	    next_minor++;
	} while (max_client_handles--);

	return (0);
}

/*
 * cs_find_client - finds the client pointer associated with the client handle
 *			or NULL if client not found
 *
 * returns:	(client_t *)NULL - if client not found or an error occured
 *					If the error argument is not NULL,
 *					it is set to:
 *			CS_BAD_SOCKET - socket number in client_handle_t is
 *						invalid
 *			CS_BAD_HANDLE - client not found
 *		(client_t *) - pointer to client_t structure
 *
 * Note that each socket always has a pseudo client with a client minor number
 *	of 0; this client minor number is used for Socket Services access to
 *	Card Services functions. The client pointer returned for client minor
 *	number 0 is the global Socket Services client pointer.
 */
static client_t *
cs_find_client(client_handle_t client_handle, int *error)
{
	cs_socket_t *sp;
	client_t *clp;

	/*
	 * If we are being asked to see if a client with a minor number
	 *	of 0 exists, always return a pointer to the global Socket
	 *	Services client, since this client always exists, and is
	 *	only for use by Socket Services.  There is no socket
	 *	associated with this special client handle.
	 */
	if (CLIENT_HANDLE_IS_SS(client_handle))
	    return (&cs_socket_services_client);

	/*
	 * Check to be sure that the socket number is in range
	 */
	if (!(CHECK_SOCKET_NUM(GET_CLIENT_SOCKET(client_handle),
					cs_globals.num_sockets))) {
	    if (error)
		*error = CS_BAD_SOCKET;
	    return (NULL);
	}

	sp = &cs_sockets[GET_CLIENT_SOCKET(client_handle)];

	clp = sp->client_list;

	while (clp) {
	    if (clp->client_handle == client_handle)
		return (clp);
	    clp = clp->next;
	}

	if (error)
	    *error = CS_BAD_HANDLE;

	return (NULL);
}

/*
 * cs_destroy_client_handle - destroys client handle and client structure of
 *				passed client handle
 *
 * returns:	CS_SUCCESS - if client handle sucessfully destroyed
 *		CS_BAD_HANDLE - if client handle is invalid or if trying
 *					to destroy global SS client
 *		{other errors} - other errors from cs_find_client()
 */
static int
cs_destroy_client_handle(client_handle_t client_handle)
{
	client_t *clp;
	cs_socket_t *sp;
	int error = CS_BAD_HANDLE;

	/*
	 * See if we were passed a valid client handle or if we're being asked
	 *	to destroy the Socket Services client
	 */
	if ((!(clp = cs_find_client(client_handle, &error))) ||
			(CLIENT_HANDLE_IS_SS(client_handle)))
	    return (error);

	sp = &cs_sockets[GET_CLIENT_SOCKET(client_handle)];

	/*
	 * Recycle this client's minor number.  This will most likely
	 *	be the next client minor number we use, but it is also
	 *	a hint to cs_create_client_handle, and that function
	 *	may actually create a new client handle using a minor
	 *	number different that this number.
	 */
	mutex_enter(&sp->lock);
	sp->next_cl_minor = GET_CLIENT_MINOR(client_handle);

	/*
	 * See if we're the first or not in the client list; if we're
	 *	not first, then just adjust the client behind us to
	 *	point to the client ahead of us; this could be NULL
	 *	if we're the last client in the list.
	 */
	if (clp->prev) {
	    clp->prev->next = clp->next;
	} else {
	/*
	 * We are first, so adjust the client list head pointer
	 *	in the socket to point to the client structure that
	 *	follows us; this could turn out to be NULL if we're
	 *	the only client on this socket.
	 */
	    sp->client_list = clp->next;
	}

	/*
	 * If we're not the last client in the list, point the next
	 *	client to the client behind us; this could turn out
	 *	to be NULL if we're the first client on this socket.
	 */
	if (clp->next)
	    clp->next->prev = clp->prev;
	mutex_exit(&sp->lock);

	/*
	 * Free this client's memory.
	 */
	kmem_free(clp, sizeof (client_t));

	return (CS_SUCCESS);
}

/*
 * cs_create_client_handle - create a new client handle for the passed socket
 *
 * returns:	0 -  if can't create client for some reason
 *		client_handle_t - new client handle
 */
static client_handle_t
cs_create_client_handle(unsigned socket_num, client_t *cclp)
{
	client_t *clp;
	cs_socket_t *sp;
	unsigned next_minor;
	client_handle_t client_handle;

	sp = &cs_sockets[socket_num];

	/*
	 * Get the next available minor number that we can use.  We use the
	 *	next_cl_minor number as a hint to cs_create_next_client_minor
	 *	and in most cases this will be the minor number we get back.
	 * If for some reason we can't get a minor number, return an error.
	 *	The only way we could get an error would be if there are
	 *	already the maximum number of clients for this socket. Since
	 *	the maximum number of clients per socket is pretty large,
	 *	this error is unlikely to occur.
	 */
	if (!(next_minor =
		cs_create_next_client_minor(socket_num, sp->next_cl_minor)))
	    return (0);

	/*
	 * Got a new client minor number, now create a new client handle.
	 */
	client_handle = MAKE_CLIENT_HANDLE(socket_num, next_minor);

	/*
	 * If this client handle exists, then we have an internal
	 *	error; this should never happen, BTW.  This is really
	 *	a double-check on the cs_create_next_client_minor
	 *	function, which also calls cs_find_client.
	 */
	if (cs_find_client(client_handle, NULL)) {
	    cmn_err(CE_CONT,
		"cs_create_client_handle: duplicate client handle 0x%x\n",
							(int)client_handle);
	    return (0);
	}

	/*
	 * If we don't have any clients on this socket yet, create
	 *	a new client and hang it on the socket client list.
	 */
	if (!sp->client_list) {
	    sp->client_list = cclp;
	    clp = sp->client_list;
	} else {
	/*
	 * There are other clients on this socket, so look for
	 *	the last client and add our new client after it.
	 */
	    clp = sp->client_list;
	    while (clp->next) {
		clp = clp->next;
	    }

	    clp->next = cclp;
	    clp->next->prev = clp;
	    clp = clp->next;
	} /* if (!sp->client_list) */

	/*
	 * Assign the new client handle to this new client structure.
	 */
	clp->client_handle = client_handle;

	/*
	 * Create the next available client minor number for this socket
	 *	and save it away.
	 */
	sp->next_cl_minor =
		cs_create_next_client_minor(socket_num, sp->next_cl_minor);

	return (client_handle);
}

/*
 * cs_clear_superclient_lock - clears the global "super-client" lock
 *
 * Note: this function uses the cs_globals.global_lock so observe proper
 *		nexting of locks!!
 */
static void
cs_clear_superclient_lock(int super_client)
{

	/*
	 * If this was a "super-client" registering then we need
	 *	to clear the GLOBAL_SUPER_CLIENT_REGISTERED flag
	 *	so that other "super-clients" can register.
	 */
	if (super_client == CLIENT_SUPER_CLIENT) {
	    mutex_enter(&cs_globals.global_lock);
	    cs_globals.flags &= ~GLOBAL_SUPER_CLIENT_REGISTERED;
	    mutex_exit(&cs_globals.global_lock);
	}
}

/*
 * ==== event handling section ====
 */

/*
 * cs_event - CS event hi-priority callback handler
 *
 *	This function gets called by SS and is passed the event type in
 *		the "event" argument, and the socket number in the "info"
 *		argument.
 *
 *	This function is called at high-priority interrupt time, and the only
 *		event that it handles directly is the CS_EVENT_CARD_REMOVAL
 *		event, which gets shuttled right into the client's event
 *		handler.  All other events are just queued up and the socket
 *		event thread is woken up via the soft interrupt handler.
 *	Note that CS_EVENT_CARD_INSERTION events are not set in the clients'
 *		event field, since the CS card insertion/card ready processing
 *		code is responsible for setting this event in a client's
 *		event field.
 */
static int
cs_event(event_t event, unsigned sn)
{
	client_t *client;
	cs_socket_t *sp;
	client_types_t *ct;

	sp = &cs_sockets[sn];

	/*
	 * Check to see if CS wants to unload - we do this since it's possible
	 *	to disable certain sockets.  Do NOT acquire any locks yet.
	 */
	if (sp->flags & SOCKET_UNLOAD_MODULE) {
	    if (event == PCE_CARD_INSERT)
		cmn_err(CE_CONT, "PCMCIA: socket %d disabled - please "
							"remove card\n", sn);
	    return (0);
	}

	mutex_enter(&sp->lock);

#ifdef	CS_DEBUG
	if (cs_debug > 1) {
	    event2text_t event2text;

	    event2text.Attributes = CONVERT_EVENT_TO_TEXT;
	    event2text.event = event;
	    (void) cs_event2text(&event2text, 0);
	    cmn_err(CE_CONT, "cs_event: event=%s (x%x), socket=0x%x\n",
				event2text.text, (int)event, (int)sn);
	}
#endif

	/*
	 * Convert SS events to CS events; handle the PRR if necessary.
	 */
	sp->events |= ss_to_cs_events(sp, event);

	/*
	 * We want to maintain the required event dispatching order as
	 *	specified in the PCMCIA spec, so we cycle through all
	 *	clients on this socket to make sure that they are
	 *	notified in the correct order of any high-priority
	 *	events.
	 */
	ct = &client_types[0];
	while (ct) {
	/*
	 * Point to the head of the client list for this socket, and go
	 *	through each client to set up the client events as well as
	 *	call the client's event handler directly if we have a high
	 *	priority event that we need to tell the client about.
	 */
	    client = sp->client_list;

	    if (ct->order & CLIENT_EVENTS_LIFO) {
		client_t *clp = NULL;

		while (client) {
		    clp = client;
		    client = client->next;
		}
		client = clp;
	    }

	    while (client) {
		client->events |= ((sp->events & ~CS_EVENT_CARD_INSERTION) &
				    (client->event_mask | client->global_mask));
		if (client->flags & ct->type) {
#ifdef	CS_DEBUG
		    if (cs_debug > 1) {
			cmn_err(CE_CONT, "cs_event: socket %d client [%s] "
						"events 0x%x flags 0x%x\n",
						sn, client->driver_name,
						(int)client->events,
						(int)client->flags);
		    }
#endif

		/*
		 * Handle the suspend and card removal events
		 *	specially here so that the client can receive
		 *	these events at high-priority.
		 */
		    if (client->events & CS_EVENT_PM_SUSPEND) {
			if (client->flags & CLIENT_CARD_INSERTED) {
			    CLIENT_EVENT_CALLBACK(client, CS_EVENT_PM_SUSPEND,
							CS_EVENT_PRI_HIGH);
			} /* if (CLIENT_CARD_INSERTED) */
			client->events &= ~CS_EVENT_PM_SUSPEND;
		    } /* if (CS_EVENT_PM_SUSPEND) */

		    if (client->events & CS_EVENT_CARD_REMOVAL) {
			if (client->flags & CLIENT_CARD_INSERTED) {
			    client->flags &= ~(CLIENT_CARD_INSERTED |
						CLIENT_SENT_INSERTION);
			    CLIENT_EVENT_CALLBACK(client,
							CS_EVENT_CARD_REMOVAL,
							CS_EVENT_PRI_HIGH);
			/*
			 * Check to see if the client wants low priority
			 *	removal events as well.
			 */
			    if ((client->event_mask | client->global_mask) &
						CS_EVENT_CARD_REMOVAL_LOWP) {
				client->events |= CS_EVENT_CARD_REMOVAL_LOWP;
			    }
			} /* if (CLIENT_CARD_INSERTED) */
			client->events &= ~CS_EVENT_CARD_REMOVAL;
		    } /* if (CS_EVENT_CARD_REMOVAL) */

		} /* if (ct->type) */
		if (ct->order & CLIENT_EVENTS_LIFO) {
		    client = client->prev;
		} else {
		    client = client->next;
		}
	    } /* while (client) */

	    ct = ct->next;
	} /* while (ct) */

	/*
	 * Set the SOCKET_NEEDS_THREAD flag so that the soft interrupt
	 *	handler will wakeup this socket's event thread.
	 */
	if (sp->events)
	    sp->flags |= SOCKET_NEEDS_THREAD;

	/*
	 * Fire off a soft interrupt that will cause the socket thread
	 *	to be woken up and any remaining events to be sent to
	 *	the clients on this socket.
	 */
	if ((sp->init_state & SOCKET_INIT_STATE_SOFTINTR) &&
			!(cs_globals.init_state & GLOBAL_INIT_STATE_UNLOADING))
	    ddi_trigger_softintr(sp->softint_id);

	mutex_exit(&sp->lock);

	return (0); /* XXX - who looks at this return value ?? */
}

/*
 * cs_card_insertion - handle card insertion and card ready events
 *
 * We read the CIS, if present, and store it away, then tell SS that
 *	we have read the CIS and it's ready to be parsed.  Since card
 *	insertion and card ready events are pretty closely intertwined,
 *	we handle both here.  For card ready events that are not the
 *	result of a card insertion event, we expect that the caller has
 *	already done the appropriate processing and that we will not be
 *	called unless we received a card ready event right after a card
 *	insertion event, i.e. that the SOCKET_WAIT_FOR_READY flag in
 *	sp->thread_state was set or if we get a CARD_READY event right
 *	after a CARD_INSERTION event.
 *
 *    calling:	sp - pointer to socket structure
 *		event - event to handle, one of:
 *				CS_EVENT_CARD_INSERTION
 *				CS_EVENT_CARD_READY
 *				CS_EVENT_SS_UPDATED
 */
static int
cs_card_insertion(cs_socket_t *sp, event_t event)
{
	int ret;

	/*
	 * Since we're only called while waiting for the card insertion
	 *	and card ready sequence to occur, we may have a pending
	 *	card ready timer that hasn't gone off yet if we got a
	 *	real card ready event.
	 */
	UNTIMEOUT(sp->rdybsy_tmo_id);

#ifdef	CS_DEBUG
	if (cs_debug > 1) {
	    cmn_err(CE_CONT, "cs_card_insertion: event=0x%x, socket=0x%x\n",
						(int)event, sp->socket_num);
	}
#endif

	/*
	 * Handle card insertion processing
	 */
	if (event & CS_EVENT_CARD_INSERTION) {
	    set_socket_t set_socket;
	    get_ss_status_t gs;

	/*
	 * Check to be sure that we have a valid CIS window
	 */
	    if (!SOCKET_HAS_CIS_WINDOW(sp)) {
		cmn_err(CE_CONT,
			"cs_card_insertion: socket %d has no "
							"CIS window\n",
				sp->socket_num);
		return (CS_GENERAL_FAILURE);
	    }

	/*
	 * Apply power to the socket, enable card detect and card ready
	 *	events, then reset the socket.
	 */
	    mutex_enter(&sp->lock);
	    sp->event_mask =   (CS_EVENT_CARD_REMOVAL   |
				CS_EVENT_CARD_READY);
	    mutex_exit(&sp->lock);
	    set_socket.socket = sp->socket_num;
	    set_socket.SCIntMask = (SBM_CD | SBM_RDYBSY);
	    set_socket.IREQRouting = 0;
	    set_socket.IFType = IF_MEMORY;
	    set_socket.CtlInd = 0; /* turn off controls and indicators */
	    set_socket.State = (unsigned)~0;	/* clear latched state bits */

	    (void) cs_convert_powerlevel(sp->socket_num, 50, VCC,
						&set_socket.VccLevel);
	    (void) cs_convert_powerlevel(sp->socket_num, 0, VPP1,
						&set_socket.Vpp1Level);
	    (void) cs_convert_powerlevel(sp->socket_num, 0, VPP2,
						&set_socket.Vpp2Level);

	    if ((ret = SocketServices(SS_SetSocket, &set_socket)) != SUCCESS) {
		cmn_err(CE_CONT,
		    "cs_card_insertion: socket %d SS_SetSocket failure %d\n",
				sp->socket_num, ret);
		return (ret);
	    }

	/*
	 * Clear the ready and ready_timeout events since they are now
	 *	bogus since we're about to reset the socket.
	 * XXX - should these be cleared right after the RESET??
	 */
	    mutex_enter(&sp->lock);

	    sp->events &= ~(CS_EVENT_CARD_READY | CS_EVENT_READY_TIMEOUT);
	    mutex_exit(&sp->lock);

	    SocketServices(SS_ResetSocket, sp->socket_num,
						RESET_MODE_CARD_ONLY);

	/*
	 * We are required by the PCMCIA spec to wait some number of
	 *	milliseconds after reset before we access the card, so
	 *	we set up a timer here that will wake us up and allow us
	 *	to continue with our card initialization.
	 */
	    mutex_enter(&sp->lock);
	    sp->thread_state |= SOCKET_RESET_TIMER;
	    timeout(cs_ready_timeout, (caddr_t)sp,
				drv_usectohz(cs_reset_timeout_time * 1000));
	    cv_wait(&sp->reset_cv, &sp->lock);
	    sp->thread_state &= ~SOCKET_RESET_TIMER;
	    mutex_exit(&sp->lock);

#ifdef	CS_DEBUG
	    if (cs_debug > 2) {
		cmn_err(CE_CONT, "cs_card_insertion: socket %d out of RESET "
							"for %d mS "
							"sp->events 0x%x\n",
							sp->socket_num,
							cs_reset_timeout_time,
							(int)sp->events);
	    }
#endif

	    /*
	     * If we have a pending CS_EVENT_CARD_REMOVAL event it
	     *	means that we likely got CD line bounce on the
	     *	insertion, so terminate this processing.
	     */
	    if (sp->events & CS_EVENT_CARD_REMOVAL) {
#ifdef	CS_DEBUG
		if (cs_debug > 0) {
		    cmn_err(CE_CONT, "cs_card_insertion: socket %d "
						"CS_EVENT_CARD_REMOVAL event "
						"terminating insertion "
						"processing\n",
							sp->socket_num);
		}
#endif
	    return (CS_SUCCESS);
	    } /* if (CS_EVENT_CARD_REMOVAL) */

	    /*
	     * If we got a card ready event after the reset, then don't
	     *	bother setting up a card ready timer, since we'll blast
	     *	right on through to the card ready processing.
	     * Get the current card status to see if it's ready; if it
	     *	is, we probably won't get a card ready event.
	     */
	    gs.socket = sp->socket_num;
	    gs.CardState = 0;
	    if ((ret = SocketServices(SS_GetStatus, &gs)) != SUCCESS) {
		cmn_err(CE_CONT,
		    "cs_card_insertion: socket %d SS_GetStatus failure %d\n",
				sp->socket_num, ret);
		return (ret);
	    }

	    mutex_enter(&sp->lock);
	    if ((sp->events & CS_EVENT_CARD_READY) ||
					(gs.CardState & SBM_RDYBSY)) {
		event = CS_EVENT_CARD_READY;
#ifdef	CS_DEBUG
		if (cs_debug > 1) {
		    cmn_err(CE_CONT, "cs_card_insertion: socket %d card "
						"READY\n", sp->socket_num);
		}
#endif

	    } else {
#ifdef	CS_DEBUG
		if (cs_debug > 1) {
		    cmn_err(CE_CONT, "cs_card_insertion: socket %d setting "
					"READY timer\n", sp->socket_num);
		}
#endif

		sp->rdybsy_tmo_id = timeout(cs_ready_timeout, (caddr_t)sp,
							READY_TIMEOUT_TIME);
		sp->thread_state |= SOCKET_WAIT_FOR_READY;

	    } /* if (CS_EVENT_CARD_READY) */

	    mutex_exit(&sp->lock);

	} /* if (CS_EVENT_CARD_INSERTION) */

	/*
	 * Handle card ready processing.  This is only card ready processing
	 *	for card ready events in conjunction with a card insertion.
	 */
	if (event == CS_EVENT_CARD_READY) {
	    cisptr_t cisptr;
	    get_socket_t get_socket;
	    set_socket_t set_socket;
	    int tpcnt = 0;
	    caddr_t cis_base;

	/*
	 * The only events that we want to see now are card removal
	 *	events.
	 */
	    mutex_enter(&sp->lock);
	    sp->event_mask = CS_EVENT_CARD_REMOVAL;
	    mutex_exit(&sp->lock);
	    get_socket.socket = sp->socket_num;
	    if (SocketServices(SS_GetSocket, &get_socket) != SUCCESS) {
		cmn_err(CE_CONT,
			"cs_card_insertion: socket %d SS_GetSocket failed\n",
							sp->socket_num);
		return (CS_BAD_SOCKET);
	    }

	    set_socket.socket = sp->socket_num;
	    set_socket.SCIntMask = SBM_CD;
	    set_socket.VccLevel = get_socket.VccLevel;
	    set_socket.Vpp1Level = get_socket.Vpp1Level;
	    set_socket.Vpp2Level = get_socket.Vpp2Level;
	    set_socket.IREQRouting = get_socket.IRQRouting;
	    set_socket.IFType = get_socket.IFType;
	    set_socket.CtlInd = get_socket.CtlInd;
	    /* XXX (is ~0 correct here?) to reset latched values */
	    set_socket.State = (unsigned)~0;

	    if (SocketServices(SS_SetSocket, &set_socket) != SUCCESS) {
		cmn_err(CE_CONT,
			"cs_card_insertion: socket %d SS_SetSocket failed\n",
							sp->socket_num);

		return (CS_BAD_SOCKET);
	    }

	/*
	 * First, do some checks for things that should have been set up
	 *	correctly previously - if any of these fail, then something
	 *	went wrong with the logic of this whole system.  We don't need
	 *	to grab any mutexii for these checks since this should all be
	 *	static data.
	 * Check to be sure that we can actually get to the CIS interpreter
	 *	and that we have access to a tuple parser callout structure.
	 * We do this here because the CIS module may have been unloaded.
	 */
	    if (!cis_parser || !cis_cistpl_std_callout) {
		cmn_err(CE_CONT,
			"cs_card_insertion: socket %d cis_parser 0x%x/"
			"callout 0x%x pointer error\n",
				sp->socket_num, (int)cis_parser,
				(int)cis_cistpl_std_callout);
		return (CS_GENERAL_FAILURE);
	    }

		/*
		 * Grab the cis_lock mutex to protect the CIS-to-be and
		 *	the CIS window, then fire off the CIS parser to
		 *	create a local copy of the card's CIS.
		 */
		mutex_enter(&sp->cis_lock);

		/*
		 * Make sure that we have a valid CIS window (we should)
		 *	and initialize it if necessary.
		 */
		if (!(cis_base = cs_init_cis_window(sp, 0, NULL))) {
		    mutex_exit(&sp->cis_lock);
		    cmn_err(CE_CONT,
			"cs_card_insertion: socket %d can't init CIS window\n",
				sp->socket_num);
		    return (CS_GENERAL_FAILURE);
		}

#ifdef	CS_DEBUG
		if (cs_debug > 3) {
		    cmn_err(CE_CONT, "cs_card_insertion: socket %d CARD_READY "
					"calling CISP_CIS_LIST_CREATE\n",
						sp->socket_num);
		}
#endif

		/*
		 * Set the cis pointer to NULL so that CIS_LIST_CREATE will
		 *	allocate memory for a new list, then set up the
		 *	parameters that are used to access the CIS.
		 */
		sp->cis = NULL;

		cisptr.base = (caddr_t)cis_base;
		cisptr.last = (caddr_t)((unsigned)cisptr.base +
					((unsigned)sp->cis_win_size - 1));

		cisptr.offset = 0;
		cisptr.flags = CISTPLF_AM_SPACE;

		/*
		 * Read the CIS, then check for any errors returned from the
		 *	CIS list create.  If we don't see any tuples, this
		 *	could be a memory card.
		 * XXX - We could return a BAD_CIS_ADDRESS if we try to parse
		 *	a card without a CIS, so we may have to just forego
		 *	the BAD_CIS_ADDR, or else call a CIS validate function
		 *	before we call CISP_CIS_LIST_CREATE.
		 */
		if ((unsigned)(tpcnt = (int)CIS_PARSER(CISP_CIS_LIST_CREATE,
			cis_cistpl_std_callout,
			&cisptr, &sp->cis)) & BAD_CIS_ADDR) {
		    mutex_exit(&sp->cis_lock);
		    cmn_err(CE_CONT, "cs_card_insertion: socket %d "
							"BAD_CIS_ADDR\n",
							sp->socket_num);
		    return (CS_BAD_OFFSET);
		} /* if BAD_CIS_ADDR */

		/*
		 * If one of the tuple handlers returned an error, we need to
		 *	catch that here as well.
		 * XXX - need to do something more than display a meesage here
		 */
		if (tpcnt & HANDTPL_ERROR) {
		    cmn_err(CE_CONT, "cs_card_insertion: socket %d "
							"HANDTPL_ERROR\n",
							sp->socket_num);
		    tpcnt &= ~HANDTPL_ERROR;
		} /* if HANDTPL_ERROR */

		/*
		 * XXX - This is cheesy way to validate the CIS.  We should
		 *	find a better way to do this.
		 * XXX - Should really call the CS ValidateCIS function rather
		 *	than trying to do this here, however the ValidateCIS
		 *	function depends on sp->cis_flags, sp->ntuples and
		 *	sp->nchains being setup, which we do here.
		 *	Catch-22. Yuk.
		 */
		if ((sp->ntuples = tpcnt) > 0) {
		    sp->cis_flags |= CW_VALID_CIS;
		    sp->nchains = 1; /* XXX - eventually figure this out */
		}

		mutex_exit(&sp->cis_lock);

		/*
		 * If we have a pending CS_EVENT_CARD_REMOVAL event it
		 *	means that we likely got CD line bounce on the
		 *	insertion, so destroy the CIS and terminate this
		 *	processing. We'll get called back to handle the
		 *	insertion again later.
		 */
		if (sp->events & CS_EVENT_CARD_REMOVAL) {
		    mutex_enter(&sp->cis_lock);
		    CIS_PARSER(CISP_CIS_LIST_DESTROY, &sp->cis);
		    sp->cis_flags &= ~CW_VALID_CIS;
		    mutex_exit(&sp->cis_lock);
#ifdef	CS_DEBUG
		    if (cs_debug > 0) {
			cmn_err(CE_CONT, "cs_card_insertion: socket %d "
						"CS_EVENT_CARD_REMOVAL event "
						"terminating READY "
						"processing\n",
							sp->socket_num);
		    }
#endif
		} else {

#ifdef	CS_DEBUG
		    if (cs_debug > 0) {
			cmn_err(CE_CONT, "cs_card_insertion: socket %d "
						"CARD_READY scheduling "
						"cs_card_ss_updated "
						"with %d tuples found\n",
							sp->socket_num,
							tpcnt);
		    }
#endif

		    /*
		     * Schedule the call to the Socket Services work thread.
		     */
		    mutex_enter(&sp->ss_thread_lock);
		    sp->ss_thread_state |= SOCKET_THREAD_CSCISInit;
		    mutex_exit(&sp->ss_thread_lock);
		    cv_broadcast(&sp->ss_thread_cv);

		} /* if (CS_EVENT_CARD_REMOVAL) */
	} /* if (CS_EVENT_CARD_READY) */

	/*
	 * Socket Services has parsed the CIS and has done any other
	 *	work to get the client driver loaded and attached if
	 *	necessary, so setup the per-client state.
	 */
	if (event == CS_EVENT_SS_UPDATED) {
	    client_t *client;

	    /*
	     * Now that we and SS are done handling the card insertion
	     *	semantics, go through each client on this socket and set
	     *	the CS_EVENT_CARD_INSERTION event in each client's event
	     *	field.  We do this here instead of in cs_event so that
	     *	when a client gets a CS_EVENT_CARD_INSERTION event, the
	     *	card insertion and ready processing has already been done
	     *	and SocketServices has had a chance to create a dip for
	     *	the card in this socket.
	     */
	    mutex_enter(&sp->lock);
	    client = sp->client_list;
	    while (client) {
		client->events |= (CS_EVENT_CARD_INSERTION &
				(client->event_mask | client->global_mask));
		client = client->next;
	    } /* while (client) */

	    mutex_exit(&sp->lock);

	} /* if (CS_EVENT_SS_UPDATED) */

	return (CS_SUCCESS);
}

/*
 * cs_card_removal - handle card removal events
 *
 * Destroy the CIS.
 *
 *    calling:	sp - pointer to socket structure
 *
 */
static int
cs_card_removal(cs_socket_t *sp)
{
	set_socket_t set_socket;
	int ret;

#ifdef	CS_DEBUG
	if (cs_debug > 0) {
	    cmn_err(CE_CONT, "cs_card_removal: socket %d\n", sp->socket_num);
	}
#endif

	/*
	 * Remove any pending card ready timer
	 */
	UNTIMEOUT(sp->rdybsy_tmo_id);

	/*
	 * Clear various flags so that everyone else knows that there's
	 *	nothing on this socket anymore.  Note that we clear the
	 *	SOCKET_CARD_INSERTED and SOCKET_IS_IO flags in the
	 *	ss_to_cs_events event mapping function.
	 */
	mutex_enter(&sp->lock);
	sp->thread_state &= ~(SOCKET_WAIT_FOR_READY | SOCKET_RESET_TIMER);

	/*
	 * Turn off socket power and set the socket back to memory mode.
	 * Disable all socket events except for CARD_INSERTION events.
	 */
	sp->event_mask = CS_EVENT_CARD_INSERTION;
	mutex_exit(&sp->lock);
	set_socket.socket = sp->socket_num;
	set_socket.SCIntMask = SBM_CD;
	set_socket.IREQRouting = 0;
	set_socket.IFType = IF_MEMORY;
	set_socket.CtlInd = 0; /* turn off controls and indicators */
	set_socket.State = (unsigned)~0;	/* clear latched state bits */

	(void) cs_convert_powerlevel(sp->socket_num, 0, VCC,
					&set_socket.VccLevel);
	(void) cs_convert_powerlevel(sp->socket_num, 0, VPP1,
					&set_socket.Vpp1Level);
	(void) cs_convert_powerlevel(sp->socket_num, 0, VPP2,
					&set_socket.Vpp2Level);

	if ((ret = SocketServices(SS_SetSocket, &set_socket)) != SUCCESS) {
	    cmn_err(CE_CONT,
		"cs_card_removal: socket %d SS_SetSocket failure %d\n",
				sp->socket_num, ret);
	    return (ret);
	}

#ifdef	CS_DEBUG
	if (cs_debug > 2) {
	    cmn_err(CE_CONT, "cs_card_removal: calling "
					"CISP_CIS_LIST_DESTROY\n");
	}
#endif

	/*
	 * Destroy the CIS and tell Socket Services that we're done
	 *	handling the card removal event.
	 */
	mutex_enter(&sp->cis_lock);
	CIS_PARSER(CISP_CIS_LIST_DESTROY, &sp->cis);
	sp->cis_flags &= ~CW_VALID_CIS;
	mutex_exit(&sp->cis_lock);

#ifdef	CS_DEBUG
	if (cs_debug > 2) {
	    cmn_err(CE_CONT, "cs_card_removal: calling CSCardRemoved\n");
	}
#endif

	SocketServices(CSCardRemoved, sp->socket_num);

	return (CS_SUCCESS);
}

/*
 * ss_to_cs_events - convert Socket Services events to Card Services event
 *			masks; this function will not read the PRR if the
 *			socket is in IO mode; this happens in cs_event_thread
 *
 * This function returns a bit mask of events.
 *
 * Note that we do some simple hysterious on card insertion and card removal
 *	events to prevent spurious insertion and removal events from being
 *	propogated down the chain.
 */
static event_t
ss_to_cs_events(cs_socket_t *sp, event_t event)
{
	event_t revent = 0;

	switch (event) {
	    case PCE_CARD_STATUS_CHANGE:
		revent |= CS_EVENT_STATUS_CHANGE;
		break;
	    case PCE_CARD_REMOVAL:
		if (sp->flags & SOCKET_CARD_INSERTED) {
		    sp->flags &= ~(SOCKET_CARD_INSERTED | SOCKET_IS_IO);
		    revent |= CS_EVENT_CARD_REMOVAL;
		/*
		 * If we're processing a removal event, it makes
		 *	no sense to keep any insertion or ready events,
		 *	so nuke them here.  This will not clear any
		 *	insertion events in the per-client event field.
		 */
		    sp->events &= ~(CS_EVENT_CARD_INSERTION |
				    CS_EVENT_CARD_READY |
				    CS_EVENT_READY_TIMEOUT);

		/*
		 * We also don't need to wait for READY anymore since
		 *	it probably won't show up, or if it does, it will
		 *	be a bogus READY event as the card is sliding out
		 *	of the socket.  Since we never do a cv_wait on the
		 *	card ready timer, it's OK for that timer to either
		 *	never go off (via an UNTIMEOUT in cs_card_removal)
		 *	or to go off but not do a cv_broadcast (since the
		 *	SOCKET_WAIT_FOR_READY flag is cleared here).
		 */
		    sp->thread_state &= ~SOCKET_WAIT_FOR_READY;

		/*
		 * Clear the config register pointers; when we process
		 *	the card insertion event, we'll read the CIS and get
		 *	the new config register addresses.
		 */
		    bzero((char *)&sp->config_regs, sizeof (config_regs_t));
		}
		break;
	    case PCE_CARD_INSERT:
		if (!(sp->flags & SOCKET_CARD_INSERTED)) {
		    sp->flags |= SOCKET_CARD_INSERTED;
		    revent |= CS_EVENT_CARD_INSERTION;
		}
		break;
	    case PCE_CARD_READY:
		if (sp->flags & SOCKET_CARD_INSERTED)
		    revent |= CS_EVENT_CARD_READY;
		break;
	    case PCE_CARD_BATTERY_WARN:
		if (sp->flags & SOCKET_CARD_INSERTED)
		    revent |= CS_EVENT_BATTERY_LOW;
		break;
	    case PCE_CARD_BATTERY_DEAD:
		if (sp->flags & SOCKET_CARD_INSERTED)
		    revent |= CS_EVENT_BATTERY_DEAD;
		break;
	    case PCE_CARD_WRITE_PROTECT:
		if (sp->flags & SOCKET_CARD_INSERTED)
		    revent |= CS_EVENT_WRITE_PROTECT;
		break;
	    case PCE_PM_RESUME:
		revent |= CS_EVENT_PM_RESUME;
		break;
	    case PCE_PM_SUSPEND:
		revent |= CS_EVENT_PM_SUSPEND;
		break;
	    default:
		cmn_err(CE_CONT, "ss_to_cs_events: unknown event 0x%x\n",
								(int)event);
		break;
	} /* switch(event) */

	return (revent);
}

/*
 * cs_ready_timeout - general purpose READY/BUSY and RESET timer
 *
 * Note that we really only expect one of the two events to be asserted when
 *	we are called.  XXX - Perhaps this might be a problem later on??
 *
 *	There is also the problem of cv_broadcast dropping the interrupt
 *	priority, even though we have our high-priority mutex held.  If
 *	we hold our high-priority mutex (sp->lock) over a cv_broadcast, and
 *	we get a high-priority interrupt during this time, the system will
 *	deadlock or panic.  Thanks to Andy Banta for finding this out in
 *	the SPC/S (stc.c) driver.
 *
 * This callback routine can not grab the sp->client_lock mutex or deadlock
 *	will result.
 */
void
cs_ready_timeout(cs_socket_t *sp)
{
	kcondvar_t *cvp = NULL;

	mutex_enter(&sp->lock);

	if (sp->thread_state & SOCKET_RESET_TIMER) {
#ifdef	CS_DEBUG
	if (cs_debug > 1) {
	    cmn_err(CE_CONT, "cs_ready_timeout: SOCKET_RESET_TIMER socket %d\n",
							sp->socket_num);
	}
#endif

	    cvp = &sp->reset_cv;
	}

	if (sp->thread_state & SOCKET_WAIT_FOR_READY) {
	    sp->events |= CS_EVENT_READY_TIMEOUT;
	    cvp = &sp->thread_cv;

#ifdef	CS_DEBUG
	    if (cs_debug > 1) {
		cmn_err(CE_CONT, "cs_ready_timeout: SOCKET_WAIT_FOR_READY "
						"socket %d\n", sp->socket_num);
	    }
#endif

	}

	mutex_exit(&sp->lock);

	if (cvp)
	    cv_broadcast(cvp);
}

/*
 * cs_event_softintr_timeout - wrapper function to call cs_socket_event_softintr
 */
void
cs_event_softintr_timeout()
{

	/*
	 * If we're trying to unload this module, then don't do
	 *	anything but exit.
	 * We acquire the cs_globals.global_lock mutex here so that
	 *	we can correctly synchronize with cs_deinit when it
	 *	is telling us to shut down. XXX - is this bogus??
	 */
	mutex_enter(&cs_globals.global_lock);
	if (!(cs_globals.init_state & GLOBAL_INIT_STATE_UNLOADING)) {
	    mutex_exit(&cs_globals.global_lock);
	    (void) cs_socket_event_softintr();
	    cs_globals.sotfint_tmo = timeout(cs_event_softintr_timeout,
						(caddr_t)NULL,
						SOFTINT_TIMEOUT_TIME);
	} else {
	    mutex_exit(&cs_globals.global_lock);
	}
}

/*
 * cs_socket_event_softintr - This function just does a cv_broadcast on behalf
 *				of the high-priority interrupt handler.
 *
 *	Note: There is no calling argument.
 */
u_int
cs_socket_event_softintr()
{
	cs_socket_t *sp;
	u_long sn;
	int ret = DDI_INTR_UNCLAIMED;

	/*
	 * If the module is on it's way out, then don't bother
	 *	to do anything else except return.
	 */
	mutex_enter(&cs_globals.global_lock);
	if ((cs_globals.init_state & GLOBAL_INIT_STATE_UNLOADING) ||
				(cs_globals.init_state & GLOBAL_IN_SOFTINTR)) {
		mutex_exit(&cs_globals.global_lock);

		/*
		 * Note that we return DDI_INTR_UNCLAIMED here
		 *	since we don't want to be constantly
		 *	called back.
		 */
		return (ret);
	} else {
	    cs_globals.init_state |= GLOBAL_IN_SOFTINTR;
	    mutex_exit(&cs_globals.global_lock);
	}

	/*
	 * Go through each socket and dispatch the appropriate events.
	 *	We have to funnel everything through this one routine because
	 *	we can't do a cv_broadcast from a high level interrupt handler
	 *	and we also can't have more than one soft interrupt handler
	 *	on a single dip and using the same handler address.
	 */
	for (sn = 0; sn < cs_globals.num_sockets; sn++) {
	    sp = &cs_sockets[sn];

	    if (sp->init_state & SOCKET_INIT_STATE_READY) {
		/*
		 * If we're being asked to unload CS, then don't bother
		 *	waking up the socket event thread handler.
		 */
		if (!(sp->flags & SOCKET_UNLOAD_MODULE) &&
					(sp->flags & SOCKET_NEEDS_THREAD)) {
		    ret = DDI_INTR_CLAIMED;
		    cv_broadcast(&sp->thread_cv);
		} /* if (SOCKET_NEEDS_THREAD) */
	    } /* if (SOCKET_INIT_STATE_READY) */
	} /* for (sn) */

	mutex_enter(&cs_globals.global_lock);
	cs_globals.init_state &= ~GLOBAL_IN_SOFTINTR;
	mutex_exit(&cs_globals.global_lock);

	return (ret);
}

/*
 * cs_event_thread - This is the per-socket event thread.
 */
static void
cs_event_thread(cs_socket_t *sp)
{
	client_t	*client;
	client_types_t	*ct;

#ifdef	CS_DEBUG
	if (cs_debug > 1) {
	    cmn_err(CE_CONT, "cs_event_thread: socket %d thread started\n",
								sp->socket_num);
	}
#endif

	mutex_enter(&sp->client_lock);

	for (;;) {

	    mutex_enter(&sp->lock);
	    sp->flags &= ~SOCKET_IN_EVENT_THREAD;
	    mutex_exit(&sp->lock);

	    cv_wait(&sp->thread_cv, &sp->client_lock);

	    mutex_enter(&sp->lock);
	    sp->flags &= ~SOCKET_NEEDS_THREAD;
	    sp->flags |= SOCKET_IN_EVENT_THREAD;
	    mutex_exit(&sp->lock);

	/*
	 * Check to see if there are any special thread operations that
	 *	we are being asked to perform.
	 */
	    if (sp->thread_state & SOCKET_THREAD_EXIT) {
#ifdef	CS_DEBUG
		if (cs_debug > 1) {
		    cmn_err(CE_CONT, "cs_event_thread: socket %d "
							"SOCKET_THREAD_EXIT\n",
							sp->socket_num);
		}
#endif
		mutex_enter(&sp->lock);
		sp->flags &= ~SOCKET_IN_EVENT_THREAD;
		mutex_exit(&sp->lock);
		mutex_exit(&sp->client_lock);
		cv_broadcast(&sp->caller_cv);	/* wakes up cs_deinit */
		return;
	    } /* if (SOCKET_THREAD_EXIT) */

#ifdef	CS_DEBUG
	    if (cs_debug > 1) {
		cmn_err(CE_CONT, "cs_event_thread: socket %d sp->events 0x%x\n",
							sp->socket_num,
							(int)sp->events);
	    }
#endif

	    /*
	     * Handle CS_EVENT_CARD_INSERTION events
	     */
	    if (sp->events & CS_EVENT_CARD_INSERTION) {
		mutex_enter(&sp->lock);
		sp->events &= ~CS_EVENT_CARD_INSERTION;
		mutex_exit(&sp->lock);

		/*
		 * If we have a pending CS_EVENT_CARD_REMOVAL event it
		 *	means that we likely got CD line bounce on the
		 *	insertion, so terminate this processing.
		 */
		if (sp->events & CS_EVENT_CARD_REMOVAL) {
#ifdef	CS_DEBUG
		    if (cs_debug > 0) {
			cmn_err(CE_CONT, "cs_event_thread: socket %d "
						"CS_EVENT_CARD_REMOVAL event "
						"terminating "
						"CS_EVENT_CARD_INSERTION "
						"processing\n",
							sp->socket_num);
		    }
#endif
		} else {
		    (void) cs_card_insertion(sp, CS_EVENT_CARD_INSERTION);
		} /* if (CS_EVENT_CARD_REMOVAL) */
	    } /* if (CS_EVENT_CARD_INSERTION) */

	    /*
	     * Handle CS_EVENT_CARD_READY and CS_EVENT_READY_TIMEOUT events
	     */
	    if (sp->events & (CS_EVENT_CARD_READY | CS_EVENT_READY_TIMEOUT)) {
		mutex_enter(&sp->lock);
		sp->events &= ~(CS_EVENT_CARD_READY | CS_EVENT_READY_TIMEOUT);
		mutex_exit(&sp->lock);
		if (sp->thread_state & SOCKET_WAIT_FOR_READY) {
		    mutex_enter(&sp->lock);
		    sp->thread_state &= ~SOCKET_WAIT_FOR_READY;
		    mutex_exit(&sp->lock);
		    (void) cs_card_insertion(sp, CS_EVENT_CARD_READY);
		} /* if (SOCKET_WAIT_FOR_READY) */
	    } /* if (CS_EVENT_CARD_READY) */

	    /*
	     * Handle CS_EVENT_SS_UPDATED events
	     */
	    if (sp->events & CS_EVENT_SS_UPDATED) {
		mutex_enter(&sp->lock);
		sp->events &= ~CS_EVENT_SS_UPDATED;
		mutex_exit(&sp->lock);
		(void) cs_card_insertion(sp, CS_EVENT_SS_UPDATED);
	    } /* if (CS_EVENT_SS_UPDATED) */

	    /*
	     * Handle CS_EVENT_STATUS_CHANGE events
	     */
	    if (sp->events & CS_EVENT_STATUS_CHANGE) {
		event_t revent;

		mutex_enter(&sp->cis_lock);
		mutex_enter(&sp->lock);
		sp->events &= ~CS_EVENT_STATUS_CHANGE;
		mutex_exit(&sp->lock);

		/*
		 * Read the PRR (if it exists) and check for any events.
		 *	The PRR will only be read if the socket is in IO
		 *	mode, if there is a card in the socket, and if there
		 *	is a PRR.
		 * We don't have to clear revent before we call the
		 *	cs_read_event_status function since it will
		 *	clear it before adding any current events.
		 */
		(void) cs_read_event_status(sp, &revent, NULL, 0);

		/*
		 * Go through each client and add any events that we saw to
		 *	the client's event list if the client has that event
		 *	enabled in their event mask.
		 */
		mutex_enter(&sp->lock);
		client = sp->client_list;

		while (client) {
		    client->events |= (revent &
				(client->event_mask | client->global_mask));
		    client = client->next;
		} /* while (client) */

		mutex_exit(&sp->lock);
		mutex_exit(&sp->cis_lock);
	    } /* if (CS_EVENT_STATUS_CHANGE) */

	/*
	 * We want to maintain the required event dispatching order as
	 *	specified in the PCMCIA spec, so we cycle through all
	 *	clients on this socket to make sure that they are
	 *	notified in the correct order of any high-priority
	 *	events.
	 */
	    ct = &client_types[0];
	    while (ct) {
		/*
		 * Point to the head of the client list for this socket, and go
		 *	through each client to set up the client events as well
		 *	as call the client's event handler directly if we have
		 *	a high priority event that we need to tell the client
		 *	about.
		 */
		client = sp->client_list;

		if (ct->order & CLIENT_EVENTS_LIFO) {
		    client_t *clp = NULL;

		    while (client) {
			clp = client;
			client = client->next;
		    }
		    client = clp;
		}

		while (client) {
		    if (client->flags & ct->type) {
			    u_long bit = 0;
			    event_t event;

			while (client->events) {

			    switch (event = CS_BIT_GET(client->events, bit)) {
				/*
				 * Clients always receive registration complete
				 *	events, even if there is no card of
				 *	their type currently in the socket.
				 */
				case CS_EVENT_REGISTRATION_COMPLETE:
				    CLIENT_EVENT_CALLBACK(client, event,
							CS_EVENT_PRI_LOW);
				    break;
				/*
				 * The client only gets a card insertion event
				 *	if there is currently a card in the
				 *	socket that the client can control.
				 *	The nexus determines this. We also
				 *	prevent the client from receiving
				 *	multiple CS_EVENT_CARD_INSERTION
				 *	events without receiving intervening
				 *	CS_EVENT_CARD_REMOVAL events.
				 */
				case CS_EVENT_CARD_INSERTION:
				    if (cs_card_for_client(client)) {
					int send_insertion;

					mutex_enter(&sp->lock);
					send_insertion = client->flags;
					client->flags |=
						(CLIENT_CARD_INSERTED |
						CLIENT_SENT_INSERTION);
					mutex_exit(&sp->lock);
					if (!(send_insertion &
						    CLIENT_SENT_INSERTION)) {
					    CLIENT_EVENT_CALLBACK(client,
						event, CS_EVENT_PRI_LOW);
					} /* if (!CLIENT_SENT_INSERTION) */
				    }
				    break;
				/*
				 * The CS_EVENT_CARD_REMOVAL_LOWP is a low
				 *	priority CS_EVENT_CARD_REMOVAL event.
				 */
				case CS_EVENT_CARD_REMOVAL_LOWP:
				    mutex_enter(&sp->lock);
				    client->flags &= ~CLIENT_SENT_INSERTION;
				    mutex_exit(&sp->lock);
				    CLIENT_EVENT_CALLBACK(client,
							CS_EVENT_CARD_REMOVAL,
							CS_EVENT_PRI_LOW);
				    break;
				/*
				 * The hardware card removal events are handed
				 *	to the client in cs_event at high
				 *	priority interrupt time; this card
				 *	removal event is a software-generated
				 *	event.
				 */
				case CS_EVENT_CARD_REMOVAL:
				    if (client->flags & CLIENT_CARD_INSERTED) {
					mutex_enter(&sp->lock);
					client->flags &=
						~(CLIENT_CARD_INSERTED |
						CLIENT_SENT_INSERTION);
					mutex_exit(&sp->lock);
					CLIENT_EVENT_CALLBACK(client, event,
							CS_EVENT_PRI_LOW);
				    }
				    break;
				/*
				 * Write protect events require the info field
				 *	of the client's event callback args to
				 *	be zero if the card is not write
				 *	protected and one if it is.
				 */
				case CS_EVENT_WRITE_PROTECT:
				    if (client->flags & CLIENT_CARD_INSERTED) {
					get_ss_status_t gs;

					mutex_enter(&sp->cis_lock);
					mutex_enter(&sp->lock);
					(void) cs_read_event_status(sp, NULL,
									&gs, 0);
					if (gs.CardState & SBM_WP) {
					    client->event_callback_args.info =
								    (void *)1;
					} else {
					    client->event_callback_args.info =
								    (void *)0;
					}
					mutex_exit(&sp->lock);
					mutex_exit(&sp->cis_lock);
					CLIENT_EVENT_CALLBACK(client, event,
							CS_EVENT_PRI_LOW);
				    } /* if (CLIENT_CARD_INSERTED) */
				    break;
				case 0:
				    break;
				default:
				    if (client->flags & CLIENT_CARD_INSERTED) {
					CLIENT_EVENT_CALLBACK(client, event,
							CS_EVENT_PRI_LOW);
				    }
				    break;
			    } /* switch */
			    mutex_enter(&sp->lock);
			    CS_BIT_CLEAR(client->events, bit);
			    mutex_exit(&sp->lock);
			    bit++;
			} /* while (client->events) */
		    } /* if (ct->type) */
		    if (ct->order & CLIENT_EVENTS_LIFO) {
			client = client->prev;
		    } else {
			client = client->next;
		    }
		} /* while (client) */

		ct = ct->next;
	    } /* while (ct) */

	/*
	 * Handle CS_EVENT_CARD_REMOVAL events
	 */
	    if (sp->events & CS_EVENT_CARD_REMOVAL) {
		mutex_enter(&sp->lock);
		sp->events &= ~CS_EVENT_CARD_REMOVAL;
		mutex_exit(&sp->lock);
		(void) cs_card_removal(sp);
	    } /* if (CS_EVENT_CARD_REMOVAL) */

	} /* for (;;) */
}

/*
 * cs_card_for_client - checks to see if a card that the client can control
 *			is currently inserted in the socket.  Socket Services
 *			has to tell us if this is the case.
 */
static int
cs_card_for_client(client_t *client)
{

	/*
	 * If the client has set the CS_EVENT_ALL_CLIENTS it means that they
	 *	want to get all events for all clients, irrespective of
	 *	whether or not there is a card in the socket.  Such clients
	 *	have to be very careful if they touch the card hardware in
	 *	any way to prevent causing problems for other clients on the
	 *	same socket.  This flag will typically only be set by a
	 *	"super-client" type of client that wishes to get information
	 *	on other clients or cards in the system.
	 * Note that the CS_EVENT_ALL_CLIENTS must be set in both the
	 *	client's socket event mask as well as it's global event mask.
	 * The client must also have registered as a "super-client" for this
	 *	socket.
	 */
	if ((client->flags & CLIENT_SUPER_CLIENT) &&
			(client->event_mask & CS_EVENT_ALL_CLIENTS) &&
			(client->global_mask & CS_EVENT_ALL_CLIENTS))
	    return (1);

	/*
	 * Look for the PCM_DEV_ACTIVE property on this client's dip; if
	 *	it's found, it means that this client can control the card
	 *	that is currently in the socket.  This is a boolean
	 *	property managed by Socket Services.
	 */
	if (ddi_getprop(DDI_DEV_T_ANY, client->dip,    (DDI_PROP_CANSLEEP |
							DDI_PROP_NOTPROM),
							PCM_DEV_ACTIVE, NULL)) {
#ifdef	CS_DEBUG
	    if (cs_debug > 1) {
		cmn_err(CE_CONT, "cs_card_for_client: client handle 0x%x "
					"driver [%s] says %s found\n",
						(int)client->client_handle,
						client->driver_name,
						PCM_DEV_ACTIVE);
	    }
#endif
	    return (1);
	}

	return (0);
}

/*
 * cs_ss_thread - This is the Socket Services work thread. We fire off
 *			any calls to Socket Services here that we want
 *			to run on a thread that is seperate from the
 *			per-socket event thread.
 */
static void
cs_ss_thread(cs_socket_t *sp)
{

	mutex_enter(&sp->ss_thread_lock);

	for (;;) {

	    cv_wait(&sp->ss_thread_cv, &sp->ss_thread_lock);

	    /*
	     * Check to see if there are any special thread operations that
	     *	we are being asked to perform.
	     */
	    if (sp->ss_thread_state & SOCKET_THREAD_EXIT) {
#ifdef	CS_DEBUG
		if (cs_debug > 1) {
		    cmn_err(CE_CONT, "cs_ss_thread: socket %d "
					"SOCKET_THREAD_EXIT\n",
						sp->socket_num);
		}
#endif
		mutex_exit(&sp->ss_thread_lock);
		cv_broadcast(&sp->ss_caller_cv);	/* wake up cs_deinit */
		return;
	    } /* if (SOCKET_THREAD_EXIT) */

#ifdef	CS_DEBUG
	    if (cs_debug > 1) {
		cmn_err(CE_CONT, "cs_ss_thread: socket %d "
					"ss_thread_state = 0x%x\n",
						(int)sp->socket_num,
						(int)sp->ss_thread_state);
	    }
#endif

	    /*
	     * Call SocketServices(CSCISInit) to have SS parse the
	     *	CIS and load/attach any client drivers necessary.
	     */
	    if (sp->ss_thread_state & SOCKET_THREAD_CSCISInit) {

		sp->ss_thread_state &= ~SOCKET_THREAD_CSCISInit;

		if (!(sp->flags & SOCKET_CARD_INSERTED)) {
		    cmn_err(CE_CONT, "cs_ss_thread %d "
					"card NOT inserted\n",
					sp->socket_num);
		}

#ifdef	CS_DEBUG
		if (cs_debug > 1) {
		    cmn_err(CE_CONT, "cs_ss_thread: socket %d calling "
						"CSCISInit\n", sp->socket_num);
		}
#endif

		/*
		 * Tell SS that we have a complete CIS and that it can now
		 *	be parsed.
		 * Note that in some cases the client driver may block in
		 *	their attach routine, causing this call to block until
		 *	the client completes their attach.
		 */
		SocketServices(CSCISInit, sp->socket_num);

		/*
		 * Set the CS_EVENT_SS_UPDATED event for this socket so that the
		 *	event thread can continue any card insertion processing
		 *	that it has to do.
		 */
		mutex_enter(&sp->lock);
		sp->events |= CS_EVENT_SS_UPDATED;
		mutex_exit(&sp->lock);

		/*
		 * Wake up this socket's event thread so that clients can
		 *	continue any card insertion or attach processing
		 *	that they need to do.
		 */
		cv_broadcast(&sp->thread_cv);
	    } /* if ST_CSCISInit */

	} /* for (;;) */
}

/*
 * cs_request_socket_mask - set the client's event mask as well as causes
 *				any events pending from RegisterClient to
 *				be scheduled to be sent to the client
 */
static int
cs_request_socket_mask(client_handle_t client_handle, sockevent_t *se)
{
	cs_socket_t *sp;
	client_t *client;
	int error;

	/*
	 * Check to see if this is the Socket Services client handle; if it
	 *	is, we don't do anything except for return success.
	 */
	if (CLIENT_HANDLE_IS_SS(client_handle))
	    return (CS_SUCCESS);

	/*
	 * Get a pointer to this client's socket structure.
	 */
	sp = &cs_sockets[GET_CLIENT_SOCKET(client_handle)];

	EVENT_THREAD_MUTEX_ENTER(sp);

	/*
	 *  Make sure that this is a valid client handle.
	 */
	if (!(client = cs_find_client(client_handle, &error))) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (error);
	}

	mutex_enter(&sp->lock);

	/*
	 * If this client has already done a RequestSocketMask without
	 *	a corresponding ReleaseSocketMask, then return an error.
	 */
	if (client->flags & REQ_SOCKET_MASK_DONE) {
	    mutex_exit(&sp->lock);
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_IN_USE);
	}

	/*
	 * Set up the event mask information; we copy this directly from
	 *	the client; since we are the only source of events, any
	 *	bogus bits that the client puts in here won't matter
	 *	because we'll never look at them.
	 */
	client->event_mask = se->EventMask;

	/*
	 * If RegisterClient left us some events to process, set these
	 *	events up here.
	 */
	if (client->pending_events) {
	    client->events |= client->pending_events;
	    client->pending_events = 0;
#ifdef	CS_DEBUG
	    if (cs_debug > 1) {
		cmn_err(CE_CONT, "cs_request_socket_mask: client_handle = 0x%x "
				"driver_name = [%s] events = 0x%x\n",
					(int)client->client_handle,
					client->driver_name,
					(int)client->events);
	    }
#endif
	}

	client->flags |= REQ_SOCKET_MASK_DONE;

	/*
	 * Merge all the clients' event masks and set the socket
	 *	to generate the appropriate events.
	 */
	(void) cs_set_socket_event_mask(sp, cs_merge_event_masks(sp));

	mutex_exit(&sp->lock);
	EVENT_THREAD_MUTEX_EXIT(sp);

	/*
	 * Wakeup the event thread if there are any client events to process.
	 */
	if (client->events) {
	    cv_broadcast(&sp->thread_cv);
#ifdef	CS_DEBUG
	    if (cs_debug > 1) {
		cmn_err(CE_CONT, "cs_request_socket_mask: did cv_broadcast for "
				"client_handle = 0x%x "
				"driver_name = [%s] events = 0x%x\n",
					(int)client->client_handle,
					client->driver_name,
					(int)client->events);
	    }
#endif

	}

	return (CS_SUCCESS);
}

/*
 * cs_release_socket_mask - clear the client's event mask
 */
static int
cs_release_socket_mask(client_handle_t client_handle)
{
	cs_socket_t *sp;
	client_t *client;
	int error;

	/*
	 * Check to see if this is the Socket Services client handle; if it
	 *	is, we don't do anything except for return success.
	 */
	if (CLIENT_HANDLE_IS_SS(client_handle))
	    return (CS_SUCCESS);

	/*
	 * Get a pointer to this client's socket structure.
	 */
	sp = &cs_sockets[GET_CLIENT_SOCKET(client_handle)];

	EVENT_THREAD_MUTEX_ENTER(sp);

	/*
	 *  Make sure that this is a valid client handle.
	 */
	if (!(client = cs_find_client(client_handle, &error))) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (error);
	}

	mutex_enter(&sp->lock);

	/*
	 * If this client has already done a RequestSocketMask without
	 *	a corresponding ReleaseSocketMask, then return an error.
	 */
	if (!(client->flags & REQ_SOCKET_MASK_DONE)) {
	    mutex_exit(&sp->lock);
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_BAD_SOCKET);
	}

	client->event_mask = 0;
	client->flags &= ~REQ_SOCKET_MASK_DONE;

	/*
	 * Merge all the clients' event masks and set the socket
	 *	to generate the appropriate events.
	 */
	(void) cs_set_socket_event_mask(sp, cs_merge_event_masks(sp));

	mutex_exit(&sp->lock);
	EVENT_THREAD_MUTEX_EXIT(sp);

	return (CS_SUCCESS);
}

/*
 * cs_get_event_mask - return the event mask for this client
 */
static int
cs_get_event_mask(client_handle_t client_handle, sockevent_t *se)
{
	cs_socket_t *sp;
	client_t *client;
	int error;

	/*
	 * Check to see if this is the Socket Services client handle; if it
	 *	is, we don't do anything except for return success.
	 */
	if (CLIENT_HANDLE_IS_SS(client_handle))
	    return (CS_SUCCESS);

	/*
	 * Get a pointer to this client's socket structure.
	 */
	sp = &cs_sockets[GET_CLIENT_SOCKET(client_handle)];

	EVENT_THREAD_MUTEX_ENTER(sp);

	/*
	 *  Make sure that this is a valid client handle.
	 */
	if (!(client = cs_find_client(client_handle, &error))) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (error);
	}

	mutex_enter(&sp->lock);

#ifdef	XXX
	/*
	 * If there's no card in the socket or the card in the socket is not
	 *	for this client, then return an error.
	 * XXX - how can a client get their event masks if their card
	 *	goes away?
	 */
	if (!(client->flags & CLIENT_CARD_INSERTED)) {
	    mutex_exit(&sp->lock);
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_NO_CARD);
	}
#endif

	/*
	 * We are only allowed to get the client event mask if a
	 *	RequestSocketMask has been called previously.  We
	 *	are allowed to get the global event mask at any
	 *	time.
	 * The global event mask is initially set by the client
	 *	in the call to RegisterClient.  The client event
	 *	mask is set by the client in calls to SetEventMask
	 *	and RequestSocketMask and gotten in calls to
	 *	GetEventMask.
	 */
	if (se->Attributes & CONF_EVENT_MASK_CLIENT) {
	    if (!(client->flags & REQ_SOCKET_MASK_DONE)) {
		mutex_exit(&sp->lock);
		EVENT_THREAD_MUTEX_EXIT(sp);
		return (CS_BAD_SOCKET);
	    }
	    se->EventMask = client->event_mask;
	} else {
	    se->EventMask = client->global_mask;
	}

	mutex_exit(&sp->lock);
	EVENT_THREAD_MUTEX_EXIT(sp);

	return (CS_SUCCESS);
}

/*
 * cs_set_event_mask - set the event mask for this client
 */
static int
cs_set_event_mask(client_handle_t client_handle, sockevent_t *se)
{
	cs_socket_t *sp;
	client_t *client;
	int error;

	/*
	 * Check to see if this is the Socket Services client handle; if it
	 *	is, we don't do anything except for return success.
	 */
	if (CLIENT_HANDLE_IS_SS(client_handle))
	    return (CS_SUCCESS);

	/*
	 * Get a pointer to this client's socket structure.
	 */
	sp = &cs_sockets[GET_CLIENT_SOCKET(client_handle)];

	EVENT_THREAD_MUTEX_ENTER(sp);

	/*
	 *  Make sure that this is a valid client handle.
	 */
	if (!(client = cs_find_client(client_handle, &error))) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (error);
	}

	mutex_enter(&sp->lock);

#ifdef	XXX
	/*
	 * If there's no card in the socket or the card in the socket is not
	 *	for this client, then return an error.
	 */
	if (!(client->flags & CLIENT_CARD_INSERTED)) {
	    mutex_exit(&sp->lock);
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_NO_CARD);
	}
#endif

	/*
	 * We are only allowed to set the client event mask if a
	 *	RequestSocketMask has been called previously.  We
	 *	are allowed to set the global event mask at any
	 *	time.
	 * The global event mask is initially set by the client
	 *	in the call to RegisterClient.  The client event
	 *	mask is set by the client in calls to SetEventMask
	 *	and RequestSocketMask and gotten in calls to
	 *	GetEventMask.
	 */
	if (se->Attributes & CONF_EVENT_MASK_CLIENT) {
	    if (!(client->flags & REQ_SOCKET_MASK_DONE)) {
		mutex_exit(&sp->lock);
		EVENT_THREAD_MUTEX_EXIT(sp);
		return (CS_BAD_SOCKET);
	    }
	    client->event_mask = se->EventMask;
	} else {
	    client->global_mask = se->EventMask;
	}

	/*
	 * Merge all the clients' event masks and set the socket
	 *	to generate the appropriate events.
	 */
	(void) cs_set_socket_event_mask(sp, cs_merge_event_masks(sp));

	mutex_exit(&sp->lock);
	EVENT_THREAD_MUTEX_EXIT(sp);

	return (CS_SUCCESS);
}

/*
 * cs_read_event_status - handles PRR events and returns card status
 *
 *	calling: *sp - socket struct point
 *		 *revent - pointer to event mask to update; if NULL, will
 *				not be updated, if non-NULL, will be updated
 *				with CS-format events; it is NOT necessary
 *				to clear this value before calling this
 *				function
 *		 *gs - pointer to a get_ss_status_t used for the SS GetStatus
 *				call; it is not necessary to initialize any
 *				members in this structure; set to NULL if
 *				not used
 *		flags - if CS_RES_IGNORE_NO_CARD is set, the check for a
 *				card present will not be done
 *
 *	returns: CS_SUCCESS
 *		 CS_NO_CARD - if no card is in the socket and the flags arg
 *				is not set to CS_RES_IGNORE_NO_CARD
 *		 CS_BAD_SOCKET - if the SS_GetStatus function returned an
 *					error
 *
 *	Note that if the client that configured this socket has told us that
 *		the READY pin in the PRR isn't valid and the socket is in IO
 *		mode, we always return that the card is READY.
 *
 *	Note that if gs is not NULL, the current card state will be returned
 *		in the gs->CardState member; this will always reflect the
 *		current card state and the state will come from both the
 *		SS_GetStatus call and the PRR, whichever is appropriate for
 *		the mode that the socket is currently in.
 */
static int
cs_read_event_status(cs_socket_t *sp, event_t *revent, get_ss_status_t *gs,
								int flags)
{
	volatile cfg_regs_t *prr, prrd = 0;

	/*
	 * SOCKET_IS_IO will only be set if a RequestConfiguration
	 *	has been done by at least one client on this socket.
	 * If there isn't a card in the socket or the caller wants to ignore
	 *	whether the card is in the socket or not, get the current
	 *	card status.
	 */
	if ((sp->flags & SOCKET_CARD_INSERTED) ||
					(flags & CS_RES_IGNORE_NO_CARD)) {
	    if (sp->flags & SOCKET_IS_IO) {
		if (sp->present & CONFIG_PINREPL_REG_PRESENT) {
		    caddr_t cis_base;

		/*
		 * Get a pointer to the CIS window
		 */
		    if (!(cis_base = cs_init_cis_window(sp,
						sp->config_regs_offset,
								NULL))) {
			cmn_err(CE_CONT, "cs_read_event_status: socket %d "
					    "can't init CIS window\n",
							sp->socket_num);
			return (CS_GENERAL_FAILURE);
		    } /* cs_init_cis_window */

		/*
		 * Create the address for the config register that
		 *	the client wants to access.
		 */
		    prr = (cfg_regs_t *)(sp->config_regs.prr_p + cis_base);

		    prrd = *prr;
		    prrd &= sp->pin;

#ifdef	CS_DEBUG
		    if (cs_debug > 1) {
			cmn_err(CE_CONT, "cs_read_event_status: prr 0x%x "
						"prrd 0x%x sp->pin 0x%x\n",
								(int)prr,
								(int)prrd,
								sp->pin);
			cmn_err(CE_CONT, "PRR(1) = [%s%s%s%s%s%s%s%s]\n",
						((prrd & PRR_WP_STATUS)?
							"PRR_WP_STATUS ":""),
						((prrd & PRR_READY_STATUS)?
							"PRR_READY_STATUS ":""),
						((prrd & PRR_BVD2_STATUS)?
							"PRR_BVD2_STATUS ":""),
						((prrd & PRR_BVD1_STATUS)?
							"PRR_BVD1_STATUS ":""),
						((prrd & PRR_WP_EVENT)?
							"PRR_WP_EVENT ":""),
						((prrd & PRR_READY_EVENT)?
							"PRR_READY_EVENT ":""),
						((prrd & PRR_BVD2_EVENT)?
							"PRR_BVD2_EVENT ":""),
						((prrd & PRR_BVD1_EVENT)?
							"PRR_BVD1_EVENT ":""));
		    }
#endif

		/*
		 * The caller wants the event changes sent back and the PRR
		 *	event change bits cleared.
		 */
		    if (revent) {
			get_socket_t get_socket;
			set_socket_t set_socket;

			/*
			 * Bug ID: 1193636 - Card Services sends bogus
			 *	events on CS_EVENT_STATUS_CHANGE events
			 * Clear this before we OR-in any values.
			 */
			*revent = 0;

			PRR_EVENT(prrd, PRR_WP_EVENT, PRR_WP_STATUS,
					CS_EVENT_WRITE_PROTECT, *revent);

			PRR_EVENT(prrd, PRR_READY_EVENT, PRR_READY_STATUS,
					CS_EVENT_CARD_READY, *revent);

			PRR_EVENT(prrd, PRR_BVD2_EVENT, PRR_BVD2_STATUS,
					CS_EVENT_BATTERY_LOW, *revent);

			PRR_EVENT(prrd, PRR_BVD1_EVENT, PRR_BVD1_STATUS,
					CS_EVENT_BATTERY_DEAD, *revent);


#ifdef	CS_DEBUG
			if (cs_debug > 1) {

			    cmn_err(CE_CONT, "PRR() = [%s%s%s%s%s%s%s%s]\n",
						((prrd & PRR_WP_STATUS)?
							"PRR_WP_STATUS ":""),
						((prrd & PRR_READY_STATUS)?
							"PRR_READY_STATUS ":""),
						((prrd & PRR_BVD2_STATUS)?
							"PRR_BVD2_STATUS ":""),
						((prrd & PRR_BVD1_STATUS)?
							"PRR_BVD1_STATUS ":""),
						((prrd & PRR_WP_EVENT)?
							"PRR_WP_EVENT ":""),
						((prrd & PRR_READY_EVENT)?
							"PRR_READY_EVENT ":""),
						((prrd & PRR_BVD2_EVENT)?
							"PRR_BVD2_EVENT ":""),
						((prrd & PRR_BVD1_EVENT)?
							"PRR_BVD1_EVENT ":""));
			}
#endif

			if (prrd)
			    *prr = prrd;

			/*
			 * We now have to reenable the status change interrupts
			 *	if there are any valid bits in the PRR. Since
			 *	the BVD1 signal becomes the STATUS_CHANGE
			 *	signal when the socket is in IO mode, we just
			 *	have to set the SBM_BVD1 enable bit in the
			 *	event mask.
			 */
			if (sp->pin) {
			    get_socket.socket = sp->socket_num;
			    SocketServices(SS_GetSocket, &get_socket);
			    set_socket.socket = sp->socket_num;
			    set_socket.SCIntMask =
					get_socket.SCIntMask | SBM_BVD1;
			    set_socket.VccLevel = get_socket.VccLevel;
			    set_socket.Vpp1Level = get_socket.Vpp1Level;
			    set_socket.Vpp2Level = get_socket.Vpp2Level;
			    set_socket.IREQRouting = get_socket.IRQRouting;
			    set_socket.IFType = get_socket.IFType;
			    set_socket.CtlInd = get_socket.CtlInd;
			    set_socket.State = get_socket.state;
			    SocketServices(SS_SetSocket, &set_socket);
			} /* if (sp->pin) */
		    } /* if (revent) */

		} /* if (CONFIG_PINREPL_REG_PRESENT) */
	    } /* if (SOCKET_IS_IO) */

	/*
	 * The caller wants the current card state; we just read
	 *	it and return a copy of it but do not clear any of
	 *	the event changed bits (if we're reading the PRR).
	 */
	    if (gs) {
		gs->socket = sp->socket_num;
		gs->CardState = 0;
		if (SocketServices(SS_GetStatus, gs) != SUCCESS)
		    return (CS_BAD_SOCKET);
		if (sp->flags & SOCKET_IS_IO) {
		/*
		 * If the socket is in IO mode, then clear the
		 *	gs->CardState bits that are now in the PRR
		 */
		    gs->CardState &= ~(SBM_WP | SBM_BVD1 |
						SBM_BVD2 | SBM_RDYBSY);

		/*
		 * Convert PRR status to SS_GetStatus status
		 */
		    if (prrd & PRR_WP_STATUS)
			gs->CardState |= SBM_WP;
		    if (prrd & PRR_BVD2_STATUS)
			gs->CardState |= SBM_BVD2;
		    if (prrd & PRR_BVD1_STATUS)
			gs->CardState |= SBM_BVD1;

		/*
		 * If the client has indicated that there is no
		 *	PRR or that the READY bit in the PRR isn't
		 *	valid, then we simulate the READY bit by
		 *	always returning READY.
		 */
		    if (!(sp->present & CONFIG_PINREPL_REG_PRESENT) ||
			((sp->present & CONFIG_PINREPL_REG_PRESENT) &&
			!((sp->pin & (PRR_READY_STATUS | PRR_READY_EVENT)) ==
				(PRR_READY_STATUS | PRR_READY_EVENT))) ||
				(prrd & PRR_READY_STATUS))
			gs->CardState |= SBM_RDYBSY;

#ifdef	CS_DEBUG
			if (cs_debug > 1) {
			    cmn_err(CE_CONT, "cs_read_event_status: prrd 0x%x "
						"sp->pin 0x%x "
						"gs->CardState 0x%x\n",
						prrd, sp->pin, gs->CardState);
			}
#endif

		} /* if (SOCKET_IS_IO) */
	    } /* if (gs) */
	    return (CS_SUCCESS);
	} /* if (SOCKET_CARD_INSERTED) */

	return (CS_NO_CARD);
}

/*
 * cs_get_status - gets live card status and latched card status changes
 *			supports the GetStatus CS call
 *
 *	returns: CS_SUCCESS
 *		 CS_BAD_HANDLE if the passed client handle is invalid
 *
 *	Note: This function resets the latched status values maintained
 *		by Socket Services
 */
static int
cs_get_status(client_handle_t client_handle, get_status_t *gs)
{
	cs_socket_t *sp;
	client_t *client;
	get_ss_status_t get_ss_status;
	get_socket_t get_socket;
	set_socket_t set_socket;
	int error;

	/*
	 * Check to see if this is the Socket Services client handle; if it
	 *	is, we don't do anything except for return success.
	 */
	if (CLIENT_HANDLE_IS_SS(client_handle))
	    return (CS_SUCCESS);

	/*
	 * Get a pointer to this client's socket structure.
	 */
	sp = &cs_sockets[GET_CLIENT_SOCKET(client_handle)];

	EVENT_THREAD_MUTEX_ENTER(sp);

	/*
	 *  Make sure that this is a valid client handle.
	 */
	if (!(client = cs_find_client(client_handle, &error))) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (error);
	}

	/*
	 * Get the current card status as well as the latched card
	 *	state.  Set the CS_RES_IGNORE_NO_CARD so that even
	 *	if there is no card in the socket we'll still get
	 *	a valid status.
	 * Note that it is not necessary to initialize any values
	 *	in the get_ss_status structure.
	 */
	mutex_enter(&sp->cis_lock);
	if ((error = cs_read_event_status(sp, NULL, &get_ss_status,
					CS_RES_IGNORE_NO_CARD)) != CS_SUCCESS) {
	    mutex_exit(&sp->cis_lock);
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (error);
	}

	mutex_exit(&sp->cis_lock);

	gs->raw_CardState = cs_sbm2cse(get_ss_status.CardState);

	/*
	 * Assign the "live" card state to the "real" card state. If there's
	 *	no card in the socket or the card in the socket is not
	 *	for this client, then we lie and tell the caller that the
	 *	card is not inserted.
	 */
	gs->CardState = gs->raw_CardState;
	if (!(client->flags & CLIENT_CARD_INSERTED))
	    gs->CardState &= ~CS_EVENT_CARD_INSERTION;

	EVENT_THREAD_MUTEX_EXIT(sp);

	get_socket.socket = sp->socket_num;
	if (SocketServices(SS_GetSocket, &get_socket) != SUCCESS)
	    return (CS_BAD_SOCKET);

	gs->SocketState = cs_sbm2cse(get_socket.state);

	set_socket.socket = sp->socket_num;
	set_socket.SCIntMask = get_socket.SCIntMask;
	set_socket.VccLevel = get_socket.VccLevel;
	set_socket.Vpp1Level = get_socket.Vpp1Level;
	set_socket.Vpp2Level = get_socket.Vpp2Level;
	set_socket.IREQRouting = get_socket.IRQRouting;
	set_socket.IFType = get_socket.IFType;
	set_socket.CtlInd = get_socket.CtlInd;
	/* XXX (is ~0 correct here?) reset latched values */
	set_socket.State = (unsigned)~0;

	if (SocketServices(SS_SetSocket, &set_socket) != SUCCESS)
	    return (CS_BAD_SOCKET);

	return (CS_SUCCESS);
}

/*
 * cs_cse2sbm - converts a CS event mask to an SS (SBM_XXX) event mask
 */
static event_t
cs_cse2sbm(event_t event_mask)
{
	event_t sbm_event = 0;

	/*
	 * XXX - we need to handle PM_CHANGE and RESET here as well
	 */
	if (event_mask & CS_EVENT_WRITE_PROTECT)
	    sbm_event |= SBM_WP;
	if (event_mask & CS_EVENT_BATTERY_DEAD)
	    sbm_event |= SBM_BVD1;
	if (event_mask & CS_EVENT_BATTERY_LOW)
	    sbm_event |= SBM_BVD2;
	if (event_mask & CS_EVENT_CARD_READY)
	    sbm_event |= SBM_RDYBSY;
	if (event_mask & CS_EVENT_CARD_LOCK)
	    sbm_event |= SBM_LOCKED;
	if (event_mask & CS_EVENT_EJECTION_REQUEST)
	    sbm_event |= SBM_EJECT;
	if (event_mask & CS_EVENT_INSERTION_REQUEST)
	    sbm_event |= SBM_INSERT;
	if (event_mask & (CS_EVENT_CARD_INSERTION | CS_EVENT_CARD_REMOVAL))
	    sbm_event |= SBM_CD;

	return (sbm_event);
}

/*
 * cs_sbm2cse - converts SBM_xxx state to CS event bits
 */
static u_long
cs_sbm2cse(u_long state)
{
	u_long rstate = 0;

	/*
	 * XXX - we need to handle PM_CHANGE and RESET here as well
	 */
	if (state & SBM_WP)
	    rstate |= CS_EVENT_WRITE_PROTECT;
	if (state & SBM_BVD1)
	    rstate |= CS_EVENT_BATTERY_DEAD;
	if (state & SBM_BVD2)
	    rstate |= CS_EVENT_BATTERY_LOW;
	if (state & SBM_RDYBSY)
	    rstate |= CS_EVENT_CARD_READY;
	if (state & SBM_LOCKED)
	    rstate |= CS_EVENT_CARD_LOCK;
	if (state & SBM_EJECT)
	    rstate |= CS_EVENT_EJECTION_REQUEST;
	if (state & SBM_INSERT)
	    rstate |= CS_EVENT_INSERTION_REQUEST;
	if (state & SBM_CD)
	    rstate |= CS_EVENT_CARD_INSERTION;

	return (rstate);
}

/*
 * cs_merge_event_masks - merge the CS global socket event mask with all the
 *				clients' event masks
 */
static unsigned
cs_merge_event_masks(cs_socket_t *sp)
{
	client_t *client;
	unsigned SCIntMask;

	/*
	 * We always want to see card detect and status change events.
	 */
	SCIntMask = SBM_CD;

	client = sp->client_list;

	while (client) {
	    u_long event_mask;

	    event_mask = client->event_mask | client->global_mask |
							sp->event_mask;

	    if (!(sp->flags & SOCKET_IS_IO)) {
		SCIntMask |= cs_cse2sbm(event_mask);
	    } else {
		/*
		 * If the socket is in IO mode and there is a PRR present,
		 *	then we may need to enable PCE_CARD_STATUS_CHANGE
		 *	events.
		 */
		if (sp->present & CONFIG_PINREPL_REG_PRESENT) {

		    SCIntMask |= (cs_cse2sbm(event_mask) &
				~(SBM_WP | SBM_BVD1 | SBM_BVD2 | SBM_RDYBSY));

		    if ((sp->pin & (PRR_WP_STATUS | PRR_WP_EVENT)) ==
					(PRR_WP_STATUS | PRR_WP_EVENT))
			if (event_mask & CS_EVENT_WRITE_PROTECT)
			    SCIntMask |= SBM_BVD1;

		    if ((sp->pin & (PRR_READY_STATUS | PRR_READY_EVENT)) ==
					(PRR_READY_STATUS | PRR_READY_EVENT))
			if (event_mask & CS_EVENT_CARD_READY)
			    SCIntMask |= SBM_BVD1;

		    if ((sp->pin & (PRR_BVD2_STATUS | PRR_BVD2_EVENT)) ==
					(PRR_BVD2_STATUS | PRR_BVD2_EVENT))
			if (event_mask & CS_EVENT_BATTERY_LOW)
			    SCIntMask |= SBM_BVD1;

		    if ((sp->pin & (PRR_BVD1_STATUS | PRR_BVD1_EVENT)) ==
					(PRR_BVD1_STATUS | PRR_BVD1_EVENT))
			if (event_mask & CS_EVENT_BATTERY_DEAD)
			    SCIntMask |= SBM_BVD1;
		} /* if (CONFIG_PINREPL_REG_PRESENT) */
	    } /* if (!SOCKET_IS_IO) */

	    client = client->next;
	} /* while (client) */

	return (SCIntMask);
}

/*
 * cs_set_socket_event_mask - set the event mask for the socket
 */
static int
cs_set_socket_event_mask(cs_socket_t *sp, unsigned event_mask)
{
	get_socket_t get_socket;
	set_socket_t set_socket;

	get_socket.socket = sp->socket_num;
	if (SocketServices(SS_GetSocket, &get_socket) != SUCCESS)
	    return (CS_BAD_SOCKET);

	set_socket.socket = sp->socket_num;
	set_socket.SCIntMask = event_mask;
	set_socket.VccLevel = get_socket.VccLevel;
	set_socket.Vpp1Level = get_socket.Vpp1Level;
	set_socket.Vpp2Level = get_socket.Vpp2Level;
	set_socket.IREQRouting = get_socket.IRQRouting;
	set_socket.IFType = get_socket.IFType;
	set_socket.CtlInd = get_socket.CtlInd;
	/* XXX (is ~0 correct here?) reset latched values */
	set_socket.State = (unsigned)~0;

	if (SocketServices(SS_SetSocket, &set_socket) != SUCCESS)
	    return (CS_BAD_SOCKET);

	return (CS_SUCCESS);
}

/*
 * ==== MTD handling section ====
 */
static int
cs_deregister_mtd(client_handle_t client_handle)
{

	cmn_err(CE_CONT, "cs_deregister_mtd: client_handle 0x%x\n",
							(int)client_handle);

	return (CS_SUCCESS);
}

/*
 * ==== memory window handling section ====
 */

/*
 * cs_request_window  - searches through window list for the socket to find a
 *			memory window that matches the requested criteria;
 *			this is RequestWindow
 *
 * calling:  cs_request_window(client_handle_t, *window_handle_t, win_req_t *)
 *
 *	On sucessful return, the window_handle_t * pointed to will
 *		contain a valid window handle for this window.
 *
 *	returns: CS_SUCCESS - if window found
 *		 CS_OUT_OF_RESOURCE - if no windows match requirements
 *		 CS_BAD_HANDLE - client handle is invalid
 *		 CS_BAD_SIZE - if requested size can not be met
 *		 CS_BAD_WINDOW - if an internal error occured
 *		 CS_UNSUPPORTED_FUNCTION - if SS is trying to call us
 *		 CS_NO_CARD - if no card is in socket
 *		 CS_BAD_ATTRIBUTE - if any of the unsupported Attrbute
 *					flags are set
 */
static int
cs_request_window(client_handle_t client_handle,
				window_handle_t *wh,
				win_req_t *rw)
{
	cs_socket_t *sp;
	cs_window_t *cw;
	client_t *client;
	modify_win_t mw;
	inquire_window_t iw;
	u_long aw;
	int error;

	/*
	 * Check to see if this is the Socket Services client handle; if it
	 *	is, we don't support SS using this call.
	 */
	if (CLIENT_HANDLE_IS_SS(client_handle))
	    return (CS_UNSUPPORTED_FUNCTION);

	/*
	 * Make sure that none of the unsupported flags are set.
	 */
	if (rw->Attributes &   (WIN_PAGED |
				WIN_SHARED |
				WIN_FIRST_SHARED |
				WIN_BINDING_SPECIFIC))
	    return (CS_BAD_ATTRIBUTE);

	mutex_enter(&cs_globals.window_lock);

	/*
	 * Get a pointer to this client's socket structure.
	 */
	sp = &cs_sockets[GET_CLIENT_SOCKET(client_handle)];

	EVENT_THREAD_MUTEX_ENTER(sp);

	/*
	 *  Make sure that this is a valid client handle.
	 */
	if (!(client = cs_find_client(client_handle, &error))) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    mutex_exit(&cs_globals.window_lock);
	    return (error);
	}

	mutex_enter(&sp->lock);

	/*
	 * If there's no card in the socket or the card in the socket is not
	 *	for this client, then return an error.
	 */
	if (!(client->flags & CLIENT_CARD_INSERTED)) {
	    mutex_exit(&sp->lock);
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    mutex_exit(&cs_globals.window_lock);
	    return (CS_NO_CARD);
	}

	mutex_exit(&sp->lock);

	/*
	 * See if we can find a window that matches the caller's criteria.
	 *	If we can't, then thre's not much more that we can do except
	 *	for return an error.
	 */
	if ((error = cs_find_mem_window(sp->socket_num, rw, &aw)) !=
								CS_SUCCESS) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    mutex_exit(&cs_globals.window_lock);
	    return (error);
	}

	/*
	 * We got a window, now synthesize a new window handle for this
	 *	client and get a pointer to the global window structs
	 *	and assign this window to this client.
	 * We don't have to check for errors from cs_create_window_handle
	 *	since that function always returns a valid window handle
	 *	if it is given a valid window number.
	 */
	*wh = cs_create_window_handle(aw);
	cw = &cs_windows[aw];

	cw->window_handle = *wh;
	cw->client_handle = client_handle;
	cw->socket_num = sp->socket_num;
	cw->state |= (CW_ALLOCATED | CW_MEM);

	mw.Attributes = (
				rw->Attributes |
				WIN_DATA_PATH_VALID |
				WIN_ACCESS_SPEED_VALID);
	mw.AccessSpeed = rw->AccessSpeed;

	if ((error = cs_modify_mem_window(*wh, &mw, rw, sp->socket_num)) !=
								CS_SUCCESS) {
	    cw->state = 0;
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    mutex_exit(&cs_globals.window_lock);
	    return (error);
	}

	/*
	 * Get any required card offset and pass it back to the client.
	 *	This is not defined in the current PCMCIA spec.  It is
	 *	an aid to clients that want to use it to generate an
	 *	optimum card offset.
	 */
	iw.window = GET_WINDOW_NUMBER(*wh);
	SocketServices(SS_InquireWindow, &iw);

	if (iw.mem_win_char.MemWndCaps & WC_CALIGN)
	    rw->ReqOffset = rw->Size;
	else
	    rw->ReqOffset = iw.mem_win_char.ReqOffset;

	/*
	 * Increment the client's memory window count; this is how we know
	 *	when a client has any allocated memory windows.
	 */
	client->memwin_count++;

	EVENT_THREAD_MUTEX_EXIT(sp);
	mutex_exit(&cs_globals.window_lock);

	return (CS_SUCCESS);
}

/*
 * cs_release_window - deallocates the window associated with the passed
 *			window handle; this is ReleaseWindow
 *
 *	returns: CS_SUCCESS if window handle is valid and window was
 *			sucessfully deallocated
 *		 CS_BAD_HANDLE if window handle is invalid or if window
 *			handle is valid but window is not allocated
 */
static int
cs_release_window(window_handle_t wh)
{
	cs_socket_t *sp;
	cs_window_t *cw;
	client_t *client;
	int error;

	mutex_enter(&cs_globals.window_lock);

	if (!(cw = cs_find_window(wh))) {
	    mutex_exit(&cs_globals.window_lock);
	    return (CS_BAD_HANDLE);
	}

	/*
	 * Check to see if this is the Socket Services client handle; if it
	 *	is, we don't support SS using this call.
	 */
	if (CLIENT_HANDLE_IS_SS(cw->client_handle)) {
	    mutex_exit(&cs_globals.window_lock);
	    return (CS_UNSUPPORTED_FUNCTION);
	}

	/*
	 * Get a pointer to this client's socket structure.
	 */
	sp = &cs_sockets[GET_CLIENT_SOCKET(cw->client_handle)];

	EVENT_THREAD_MUTEX_ENTER(sp);

	/*
	 *  Make sure that this is a valid client handle.
	 */
	if (!(client = cs_find_client(cw->client_handle, &error))) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    mutex_exit(&cs_globals.window_lock);
	    return (error);
	}

	/*
	 * Mark this window as not in use anymore.
	 */
	cw->state = 0;

	/*
	 * Decrement the client's memory window count; this is how we know
	 *	when a client has any allocated memory windows.
	 */
	if (!(--(client->memwin_count)))
	    client->flags &= ~CLIENT_WIN_ALLOCATED;

	EVENT_THREAD_MUTEX_EXIT(sp);
	mutex_exit(&cs_globals.window_lock);

	return (CS_SUCCESS);
}

/*
 * cs_modify_window - modifies a window's characteristics; this is ModifyWindow
 */
static int
cs_modify_window(window_handle_t wh, modify_win_t *mw)
{
	cs_socket_t *sp;
	cs_window_t *cw;
	client_t *client;
	int error;

	mutex_enter(&cs_globals.window_lock);

	/*
	 * Do some sanity checking - make sure that we can find a pointer
	 *	to the window structure, and if we can, get the client that
	 *	has allocated that window.
	 */
	if (!(cw = cs_find_window(wh))) {
	    mutex_exit(&cs_globals.window_lock);
	    return (CS_BAD_HANDLE);
	}

	/*
	 * Get a pointer to this client's socket structure.
	 */
	sp = &cs_sockets[GET_CLIENT_SOCKET(cw->client_handle)];

	EVENT_THREAD_MUTEX_ENTER(sp);

	if (!(client = cs_find_client(cw->client_handle, &error))) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    mutex_exit(&cs_globals.window_lock);
	    return (error);
	}

	mutex_enter(&sp->lock);

	/*
	 * If there's no card in the socket or the card in the socket is not
	 *	for this client, then return an error.
	 */
	if (!(client->flags & CLIENT_CARD_INSERTED)) {
	    mutex_exit(&sp->lock);
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    mutex_exit(&cs_globals.window_lock);
	    return (CS_NO_CARD);
	}

	mutex_exit(&sp->lock);

	mw->Attributes &= (
				WIN_MEMORY_TYPE_AM |
				WIN_ENABLE |
				WIN_ACCESS_SPEED_VALID);

	mw->Attributes &= ~WIN_DATA_PATH_VALID;

	if ((error = cs_modify_mem_window(wh, mw, NULL, NULL)) != CS_SUCCESS) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    mutex_exit(&cs_globals.window_lock);
	    return (error);
	}

	EVENT_THREAD_MUTEX_EXIT(sp);
	mutex_exit(&cs_globals.window_lock);

	return (CS_SUCCESS);
}

/*
 * cs_modify_mem_window - modifies a window's characteristics; used internally
 *				by Card Services
 *
 *    If *wr is NULL, it means that we're being called by ModifyWindow
 *    If *wr is non-NULL, it means that we are being called by RequestWindow
 *	and so we can't use SS_GetWindow.
 */
static int
cs_modify_mem_window(window_handle_t wh, modify_win_t *mw, win_req_t *wr,
									int sn)
{
	get_window_t gw;
	set_window_t sw;
	set_page_t set_page;
	get_page_t get_page;

	/*
	 * If the win_req_t struct pointer is NULL, it means that
	 *	we're being called by ModifyWindow, so get the
	 *	current window characteristics.
	 */
	if (!wr) {
	    gw.window = GET_WINDOW_NUMBER(wh);
	    if (SocketServices(SS_GetWindow, &gw) != SUCCESS)
		return (CS_BAD_WINDOW);
	    sw.state = gw.state;
	    sw.socket = gw.socket;
	    sw.WindowSize = gw.size;
	} else {
	    sw.state = 0;
	    sw.socket = sn;
	    sw.WindowSize = wr->Size;
	}

	/*
	 * If we're being called by RequestWindow, we must always have
	 *	WIN_ACCESS_SPEED_VALID set since get_window_t is not
	 *	defined.
	 */
	if (mw->Attributes & WIN_ACCESS_SPEED_VALID) {
	    convert_speed_t convert_speed;

	    convert_speed.Attributes = CONVERT_DEVSPEED_TO_NS;
	    convert_speed.devspeed = mw->AccessSpeed;

	    if (cs_convert_speed(&convert_speed) != CS_SUCCESS)
		return (CS_BAD_SPEED);

	    sw.speed = convert_speed.nS;
	} else {
	    sw.speed = gw.speed;
	}

	if (!wr) {
	    get_page.window = GET_WINDOW_NUMBER(wh);
	    get_page.page = 0;
	    if (SocketServices(SS_GetPage, &get_page) != SUCCESS)
		return (CS_BAD_WINDOW);
	    set_page.state = get_page.state;
	    set_page.offset = get_page.offset;
	} else {
	    set_page.state = 0;
	    set_page.offset = 0;
	}

	if (mw->Attributes & WIN_ENABLE) {
	    sw.state |= WS_ENABLED;
	    set_page.state |= PS_ENABLED;
	} else {
	    sw.state &= ~WS_ENABLED;
	    set_page.state &= ~PS_ENABLED;
	}

	if (mw->Attributes & WIN_DATA_PATH_VALID) {
	    if (mw->Attributes & WIN_DATA_WIDTH_16)
		sw.state |= WS_16BIT;
	    else
		sw.state &= ~WS_16BIT;
	}

	sw.window = GET_WINDOW_NUMBER(wh);
	sw.base = 0;

	if (SocketServices(SS_SetWindow, &sw) != SUCCESS)
	    return (CS_BAD_WINDOW);

	if (mw->Attributes & WIN_MEMORY_TYPE_AM)
	    set_page.state |= PS_ATTRIBUTE;
	else
	    set_page.state &= ~PS_ATTRIBUTE;

	set_page.window = GET_WINDOW_NUMBER(wh);
	set_page.page = 0;
	if (SocketServices(SS_SetPage, &set_page) != SUCCESS)
	    return (CS_BAD_OFFSET);

	/*
	 * Return the current base address of this window
	 */
	gw.window = GET_WINDOW_NUMBER(wh);
	if (SocketServices(SS_GetWindow, &gw) != SUCCESS)
	    return (CS_BAD_WINDOW);
	wr->Base = (caddr_t)gw.base;

	return (CS_SUCCESS);
}

/*
 * cs_map_mem_page - sets the card offset of the mapped window
 */
static int
cs_map_mem_page(window_handle_t wh, map_mem_page_t *mmp)
{
	cs_socket_t *sp;
	cs_window_t *cw;
	client_t *client;
	inquire_window_t iw;
	get_window_t gw;
	set_page_t set_page;
	get_page_t get_page;
	int error;
	u_long size;

	/*
	 * We don't support paged windows, so never allow a page number
	 *	of other than 0
	 */
	if (mmp->Page)
	    return (CS_BAD_PAGE);

	mutex_enter(&cs_globals.window_lock);

	/*
	 * Do some sanity checking - make sure that we can find a pointer
	 *	to the window structure, and if we can, get the client that
	 *	has allocated that window.
	 */
	if (!(cw = cs_find_window(wh))) {
	    mutex_exit(&cs_globals.window_lock);
	    return (CS_BAD_HANDLE);
	}

	/*
	 * Get a pointer to this client's socket structure.
	 */
	sp = &cs_sockets[GET_CLIENT_SOCKET(cw->client_handle)];

	EVENT_THREAD_MUTEX_ENTER(sp);

	if (!(client = cs_find_client(cw->client_handle, &error))) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    mutex_exit(&cs_globals.window_lock);
	    return (error);
	}

	mutex_enter(&sp->lock);

	/*
	 * If there's no card in the socket or the card in the socket is not
	 *	for this client, then return an error.
	 */
	if (!(client->flags & CLIENT_CARD_INSERTED)) {
	    mutex_exit(&sp->lock);
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    mutex_exit(&cs_globals.window_lock);
	    return (CS_NO_CARD);
	}

	mutex_exit(&sp->lock);

	gw.window = GET_WINDOW_NUMBER(wh);
	SocketServices(SS_GetWindow, &gw);

	iw.window = GET_WINDOW_NUMBER(wh);
	SocketServices(SS_InquireWindow, &iw);

	if (iw.mem_win_char.MemWndCaps & WC_CALIGN)
	    size = gw.size;
	else
	    size = iw.mem_win_char.ReqOffset;

	if (((mmp->Offset/size)*size) != mmp->Offset) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    mutex_exit(&cs_globals.window_lock);
	    return (CS_BAD_OFFSET);
	}

	get_page.window = GET_WINDOW_NUMBER(wh);
	get_page.page = 0;
	SocketServices(SS_GetPage, &get_page);

	set_page.window = GET_WINDOW_NUMBER(wh);
	set_page.page = 0;
	set_page.state = get_page.state;
	set_page.offset = mmp->Offset;
	if (SocketServices(SS_SetPage, &set_page) != SUCCESS) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    mutex_exit(&cs_globals.window_lock);
	    return (CS_BAD_OFFSET);
	}

	EVENT_THREAD_MUTEX_EXIT(sp);
	mutex_exit(&cs_globals.window_lock);

	return (CS_SUCCESS);
}

/*
 * cs_find_window - finds the window associated with the passed window
 *			handle; if the window handle is invalid or no
 *			windows match the passed window handle, NULL
 *			is returned.  Note that the window must be
 *			allocated for this function to return a valid
 *			window pointer.
 *
 *	returns: cs_window_t * pointer to the found window
 *		 NULL if window handle invalid or window not allocated
 */
cs_window_t *
cs_find_window(window_handle_t wh)
{
	cs_window_t *cw;

	if ((GET_WINDOW_NUMBER(wh) >= cs_globals.num_windows) ||
			(GET_WINDOW_MAGIC(wh) != WINDOW_HANDLE_MAGIC))
	    return ((cs_window_t *)NULL);

	cw = &cs_windows[GET_WINDOW_NUMBER(wh)];

	if ((cw->state & CW_ALLOCATED) && (cw->state & CW_MEM))
	    return (cw);

	return ((cs_window_t *)NULL);
}

/*
 * cs_create_window_handle - creates a unique window handle based on the
 *				passed window number.
 */
static window_handle_t
cs_create_window_handle(u_long aw)
{
	return (WINDOW_HANDLE_MAGIC | (aw & WINDOW_HANDLE_MASK));
}

/*
 * cs_find_mem_window - tries to find a memory window matching the caller's
 *			criteria
 *
 *	returns: CS_SUCCESS - if memory window found
 *		 CS_OUT_OF_RESOURCE - if no windows match requirements
 *		 CS_BAD_SIZE - if requested size can not be met
 *		 CS_BAD_WINDOW - if an internal error occured
 */
static int
cs_find_mem_window(u_long sn, win_req_t *rw, u_long *assigned_window)
{
	u_long wn;
	int error = CS_OUT_OF_RESOURCE;
	u_long window_num = cs_globals.num_windows;
	/* LINTED C used to be so much fun before all the rules... */
	u_long min_size = (u_long)~0;
	inquire_window_t inquire_window, *iw;
	u_long MinSize, MaxSize, ReqGran, MemWndCaps, WndCaps;
	u_long tws;

	iw = &inquire_window;

	for (wn = 0; wn < cs_globals.num_windows; wn++) {
	    iw->window = wn;
	    SocketServices(SS_InquireWindow, iw);
	    MinSize = iw->mem_win_char.MinSize;
	    MaxSize = iw->mem_win_char.MaxSize;
	    ReqGran = iw->mem_win_char.ReqGran;
	    MemWndCaps = iw->mem_win_char.MemWndCaps;
	    WndCaps = iw->WndCaps;

	    if (WINDOW_FOR_SOCKET(iw->Sockets, sn) &&
					WINDOW_AVAILABLE_FOR_MEM(wn) &&
					WndCaps & (WC_COMMON|WC_ATTRIBUTE)) {
		if ((error = cs_valid_window_speed(iw, rw->AccessSpeed)) ==
					CS_SUCCESS) {
		    error = CS_OUT_OF_RESOURCE;
		    if (cs_memwin_space_and_map_ok(iw, rw)) {
			error = CS_BAD_SIZE;
			if (!rw->Size) {
			    min_size = min(min_size, MinSize);
			    window_num = wn;
			} else {
			    if (!(MemWndCaps & WC_SIZE)) {
				if (rw->Size == MinSize) {
				    min_size = MinSize;
				    window_num = wn;
				    goto found_window;
				}
			    } else { /* WC_SIZE */
	/* CSTYLED */
			      if (!ReqGran) {
				printf("RequestWindow: Window %d ReqGran "
							"is 0\n", (int)wn);
				error = CS_BAD_WINDOW;
	/* CSTYLED */
			      } else {
				if ((rw->Size >= MinSize) &&
							(rw->Size <= MaxSize)) {
				    if (MemWndCaps & WC_POW2) {
	/* CSTYLED */
				      unsigned rg = ReqGran;
					for (tws = MinSize; tws <= MaxSize;
								rg = (rg<<1)) {
					    if (rw->Size == tws) {
						min_size = tws;
						window_num = wn;
						goto found_window;
					    }
					    tws += rg;
	/* CSTYLED */
					  } /* for (tws) */
				    } else {
					for (tws = MinSize; tws <= MaxSize;
							tws += ReqGran) {
					    if (rw->Size == tws) {
						min_size = tws;
						window_num = wn;
						goto found_window;
					    }
	/* CSTYLED */
					  } /* for (tws) */
				    } /* if (!WC_POW2) */
				} /* if (Size >= MinSize) */
	/* CSTYLED */
			      } /* if (!ReqGran) */
			    } /* if (WC_SIZE) */
			} /* if (rw->Size) */
		    } /* if (cs_space_and_map_ok) */
		} /* if (cs_valid_window_speed) */
	    } /* if (WINDOW_FOR_SOCKET) */
	} /* for (wn) */

	/*
	 * If we got here and the window_num wasn't set by any window
	 *	 matches in the above code, it means that we didn't
	 *	find a window matching the caller's criteria.
	 * If the error is CS_BAD_TYPE, it means that the last reason
	 *	that we couldn't match a window was because the caller's
	 *	requested speed was out of range of the last window that
	 *	we checked.  We convert this error code to CS_OUT_OF_RESOURCE
	 *	to conform to the RequestWindow section of the PCMCIA
	 *	Card Services spec.
	 */
	if (window_num >= cs_globals.num_windows) {
	    if (error == CS_BAD_TYPE)
		error = CS_OUT_OF_RESOURCE;
	    return (error);
	}

found_window:
	rw->Size = min_size;
	*assigned_window = window_num;
	iw->window = window_num;
	SocketServices(SS_InquireWindow, iw);
	MemWndCaps = iw->mem_win_char.MemWndCaps;

	if (MemWndCaps & WC_CALIGN)
	    rw->Attributes |= WIN_OFFSET_SIZE;
	else
	    rw->Attributes &= ~WIN_OFFSET_SIZE;
	return (CS_SUCCESS);
}

/*
 * cs_memwin_space_and_map_ok - checks to see if the passed window mapping
 *				capabilities and window speeds are in the
 *				range of the passed window.
 *
 *	returns: 0 - if the capabilities are out of range
 *		 1 - if the capabilities are in range
 */
static int
cs_memwin_space_and_map_ok(inquire_window_t *iw, win_req_t *rw)
{

#ifdef	CS_DEBUG
	if (cs_debug > 240)
	    printf("-> s&m_ok: Attributes 0x%x AccessSpeed 0x%x "
					"WndCaps 0x%x MemWndCaps 0x%x\n",
					(int)rw->Attributes,
					(int)rw->AccessSpeed,
					iw->WndCaps,
					iw->mem_win_char.MemWndCaps);
#endif

	if (rw->AccessSpeed & WIN_USE_WAIT) {
	    if (!(iw->WndCaps & WC_WAIT))
		return (0);
	}

	if (rw->Attributes & WIN_DATA_WIDTH_16) {
	    if (!(iw->mem_win_char.MemWndCaps & WC_16BIT))
		return (0);
	} else {
	    if (!(iw->mem_win_char.MemWndCaps & WC_8BIT))
		return (0);
	}

	if (rw->Attributes & WIN_MEMORY_TYPE_AM) {
	    if (!(iw->WndCaps & WC_ATTRIBUTE))
		return (0);
	}

	if (rw->Attributes & WIN_MEMORY_TYPE_CM) {
	    if (!(iw->WndCaps & WC_COMMON))
		return (0);
	}

	return (1);
}

/*
 * cs_valid_window_speed - checks to see if requested window speed
 *				is in range of passed window
 *
 *	The inquire_window_t struct gives us speeds in nS, and we
 *	get speeds in the AccessSpeed variable as a devspeed code.
 *
 *	returns: CS_BAD_SPEED - if AccessSpeed is invalid devspeed code
 *		 CS_BAD_TYPE -	if AccessSpeed is not in range of valid
 *				speed for this window
 *		 CS_SUCCESS -	if window speed is in range
 */
static int
cs_valid_window_speed(inquire_window_t *iw, u_long AccessSpeed)
{
	convert_speed_t convert_speed, *cs;

	cs = &convert_speed;

	cs->Attributes = CONVERT_DEVSPEED_TO_NS;
	cs->devspeed = AccessSpeed;

	if (cs_convert_speed(cs) != CS_SUCCESS)
	    return (CS_BAD_SPEED);

	if ((cs->nS < iw->mem_win_char.Fastest) ||
		(cs->nS > iw->mem_win_char.Slowest))
	    return (CS_BAD_TYPE);

	return (CS_SUCCESS);
}

/*
 * ==== IO window handling section ====
 */

/*
 * cs_request_io - provides IO resources for clients; this is RequestIO
 *
 *	calling: cs_request_io(client_handle_t, io_req_t *)
 *
 *	returns: CS_SUCCESS - if IO resources available for client
 *		 CS_OUT_OF_RESOURCE - if no windows match requirements
 *		 CS_BAD_HANDLE - client handle is invalid
 *		 CS_UNSUPPORTED_FUNCTION - if SS is trying to call us
 *		 CS_NO_CARD - if no card is in socket
 *		 CS_BAD_ATTRIBUTE - if any of the unsupported Attribute
 *					flags are set
 *		 CS_BAD_BASE - if either or both base port addresses
 *					are invalid or out of range
 *		 CS_CONFIGURATION_LOCKED - a RequestConfiguration has
 *					already been done
 *		 CS_IN_USE - IO ports already in use or function has
 *					already been called
 *		 CS_BAD_WINDOW - if failure while trying to set window
 *					characteristics
 */
static int
cs_request_io(client_handle_t client_handle, io_req_t *ior)
{
	cs_socket_t *sp;
	client_t *client;
	int error;

	/*
	 * Check to see if this is the Socket Services client handle; if it
	 *	is, we don't support SS using this call.
	 */
	if (CLIENT_HANDLE_IS_SS(client_handle))
	    return (CS_UNSUPPORTED_FUNCTION);

	/*
	 * If the client has only requested one IO range, then make sure
	 *	that the Attributes2 filed is clear.
	 */
	if (!ior->NumPorts2)
	    ior->Attributes2 = 0;

	/*
	 * Make sure that none of the unsupported or reserved flags are set.
	 */
	if ((ior->Attributes1 | ior->Attributes2) &    (IO_SHARED |
							IO_FIRST_SHARED |
							IO_FORCE_ALIAS_ACCESS |
							IO_DEALLOCATE_WINDOW |
							IO_DISABLE_WINDOW))
	    return (CS_BAD_ATTRIBUTE);

	/*
	 * Make sure that we have a port count for the first region.
	 */
	if (!ior->NumPorts1)
	    return (CS_BAD_BASE);

	/*
	 * If we're being asked for multiple IO ranges, then both base port
	 *	members must be non-zero.
	 */
	if ((ior->NumPorts2) && !(ior->BasePort1 && ior->BasePort2))
	    return (CS_BAD_BASE);

	mutex_enter(&cs_globals.window_lock);

	/*
	 * Get a pointer to this client's socket structure.
	 */
	sp = &cs_sockets[GET_CLIENT_SOCKET(client_handle)];

	EVENT_THREAD_MUTEX_ENTER(sp);

	/*
	 *  Make sure that this is a valid client handle.
	 */
	if (!(client = cs_find_client(client_handle, &error))) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    mutex_exit(&cs_globals.window_lock);
	    return (error);
	}

	/*
	 * If RequestConfiguration has already been done, we don't allow
	 *	this call.
	 */
	if (client->flags & REQ_CONFIGURATION_DONE) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    mutex_exit(&cs_globals.window_lock);
	    return (CS_CONFIGURATION_LOCKED);
	}

	/*
	 * If RequestIO has already been done, we don't allow this call.
	 */
	if (client->flags & REQ_IO_DONE) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    mutex_exit(&cs_globals.window_lock);
	    return (CS_IN_USE);
	}

	mutex_enter(&sp->lock);

	/*
	 * If there's no card in the socket or the card in the socket is not
	 *	for this client, then return an error.
	 */
	if (!(client->flags & CLIENT_CARD_INSERTED)) {
	    mutex_exit(&sp->lock);
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    mutex_exit(&cs_globals.window_lock);
	    return (CS_NO_CARD);
	}

	mutex_exit(&sp->lock);

	/*
	 * If we're only being asked for one IO range, then set BasePort2 to
	 *	zero, since we use it later on.
	 */
	if (!ior->NumPorts2)
	    ior->BasePort2 = 0;

	/*
	 * See if we can allow Card Services to select the base address
	 *	value for this card; if the client has specified a non-zero
	 *	base IO address but the card doesn't decode enough IO
	 *	address lines to uniquely use that address, then we have
	 *	the flexibility to choose an alternative base address.
	 * Note that if the client specifies that the card decodes zero
	 *	IO address lines, then we have to use the NumPortsX
	 *	values to figure out how many address lines the card
	 *	actually decodes, and we have to round the NumPortsX
	 *	values up to the closest power of two.
	 */
	if (ior->IOAddrLines) {
	    ior->BasePort1 = IOADDR_FROBNITZ(ior->BasePort1, ior->IOAddrLines);
	    ior->BasePort2 = IOADDR_FROBNITZ(ior->BasePort2, ior->IOAddrLines);
	} else {
	    ior->BasePort1 = (caddr_t)((u_long)ior->BasePort1 &
				((IONUMPORTS_FROBNITZ(ior->NumPorts1) +
				IONUMPORTS_FROBNITZ(ior->NumPorts2)) - 1));
	    ior->BasePort2 = (caddr_t)((u_long)ior->BasePort2 &
				((IONUMPORTS_FROBNITZ(ior->NumPorts1) +
				IONUMPORTS_FROBNITZ(ior->NumPorts2)) - 1));
	}

	/*
	 * Here is where the code diverges, depending on the type of IO windows
	 *	that this socket supports.  If this socket supportes memory
	 *	mapped IO windows, as determined by cs_init allocating an
	 *	io_mmap_window_t structure on the socket structure, then we
	 *	use one IO window for all the clients on this socket.  We can
	 *	do this safely since a memory mapped IO window implies that
	 *	only this socket shares the complete IO space of the card.
	 * See the next major block of code for a description of what we do
	 *	if a socket doesn't support memory mapped IO windows.
	 */
	if (sp->io_mmap_window) {
	    io_mmap_window_t *imw = sp->io_mmap_window;

	/*
	 * If we haven't allocated an IO window yet, do it now.  Try to
	 *	allocate the IO window that cs_init found for us; if that
	 *	fails, then call cs_find_io_win to find a window.
	 */
	    if (!imw->count) {
		set_window_t set_window;

		if (!WINDOW_AVAILABLE_FOR_IO(imw->number)) {
		    iowin_char_t iowin_char;

		    iowin_char.IOWndCaps = (WC_IO_RANGE_PER_WINDOW |
					    WC_8BIT |
					    WC_16BIT);
		    if ((error = cs_find_io_win(sp->socket_num, &iowin_char,
				    &imw->number, &imw->size)) != CS_SUCCESS) {
			EVENT_THREAD_MUTEX_EXIT(sp);
			mutex_exit(&cs_globals.window_lock);
		    } /* cs_find_io_win */
		} /* if (!WINDOW_AVAILABLE_FOR_IO) */

		set_window.socket = sp->socket_num;
		set_window.window = imw->number;
		set_window.speed = IO_WIN_SPEED;
		set_window.base = 0;
		set_window.WindowSize = imw->size;
		set_window.state = (WS_ENABLED | WS_16BIT |
				    WS_EXACT_MAPIN | WS_IO);

		if (SocketServices(SS_SetWindow, &set_window) != SUCCESS) {
		    (void) cs_setup_io_win(sp->socket_num, imw->number,
						NULL, NULL, NULL,
						(IO_DEALLOCATE_WINDOW |
						IO_DISABLE_WINDOW));
		    EVENT_THREAD_MUTEX_EXIT(sp);
		    mutex_exit(&cs_globals.window_lock);
		    return (CS_BAD_WINDOW);
		}

		imw->base = set_window.base;
		imw->size = set_window.WindowSize;

		/*
		 * Check the caller's port requirements to be sure that they
		 *	fit within our found IO window.
		 */
		if ((
			(u_long)(ior->BasePort1) + (u_long)(ior->NumPorts1) +
			(u_long)(ior->BasePort2) + (u_long)(ior->NumPorts2)) >
							(u_long)(imw->size)) {
		    EVENT_THREAD_MUTEX_EXIT(sp);
		    mutex_exit(&cs_globals.window_lock);
		    return (CS_BAD_BASE);
		}

		cs_windows[imw->number].state |= (CW_ALLOCATED | CW_IO);

	    } /* if (!imw->count) */
	    imw->count++;

	    ior->BasePort1 = (void *)((u_long)ior->BasePort1 +
						(u_long)imw->base);

	    ior->BasePort2 = (void *)((u_long)ior->BasePort2 +
						(u_long)imw->base);

	/*
	 * We don't really use these two values if we've got a memory
	 *	mapped IO window since the assigned window number is stored
	 *	in imw->number.
	 */
	    client->io_alloc.Window1 = imw->number;
	    client->io_alloc.Window2 = cs_globals.num_windows;

	/*
	 * This socket supports only IO port IO windows.
	 */
	} else {
	    if ((error = cs_allocate_io_win(sp->socket_num, ior->Attributes1,
						&client->io_alloc.Window1)) !=
								CS_SUCCESS) {

		EVENT_THREAD_MUTEX_EXIT(sp);
		mutex_exit(&cs_globals.window_lock);
		return (error);
	    } /* if (cs_allocate_io_win(1)) */

	/*
	 * Setup the window hardware; if this fails, then we need to
	 *	deallocate the previously allocated window.
	 */
	    if ((error = cs_setup_io_win(sp->socket_num,
						client->io_alloc.Window1,
						&ior->BasePort1,
						&ior->NumPorts1,
						ior->IOAddrLines,
						ior->Attributes1)) !=
								CS_SUCCESS) {
		(void) cs_setup_io_win(sp->socket_num, client->io_alloc.Window1,
					NULL, NULL, NULL,
					(
						IO_DEALLOCATE_WINDOW |
						IO_DISABLE_WINDOW));

		EVENT_THREAD_MUTEX_EXIT(sp);
		mutex_exit(&cs_globals.window_lock);
		return (error);
	    } /* if (cs_setup_io_win(1)) */

	/*
	 * See if the client wants two IO ranges.
	 */
	    if (ior->NumPorts2) {
		/*
		 * If we fail to allocate this window, then we must deallocate
		 *	the previous IO window that is already allocated.
		 */
		if ((error = cs_allocate_io_win(sp->socket_num,
						ior->Attributes2,
						&client->io_alloc.Window2)) !=
								CS_SUCCESS) {
		    (void) cs_setup_io_win(sp->socket_num,
						client->io_alloc.Window2,
						NULL, NULL, NULL,
						(
							IO_DEALLOCATE_WINDOW |
							IO_DISABLE_WINDOW));
		    EVENT_THREAD_MUTEX_EXIT(sp);
		    mutex_exit(&cs_globals.window_lock);
		    return (error);
		} /* if (cs_allocate_io_win(2)) */
		/*
		 * Setup the window hardware; if this fails, then we need to
		 *	deallocate the previously allocated window.
		 */
		if ((error = cs_setup_io_win(sp->socket_num,
						client->io_alloc.Window2,
						&ior->BasePort2,
						&ior->NumPorts2,
						ior->IOAddrLines,
						ior->Attributes2)) !=
								CS_SUCCESS) {
		    (void) cs_setup_io_win(sp->socket_num,
						client->io_alloc.Window1,
						NULL, NULL, NULL,
						(
							IO_DEALLOCATE_WINDOW |
							IO_DISABLE_WINDOW));
		    (void) cs_setup_io_win(sp->socket_num,
						client->io_alloc.Window2,
						NULL, NULL, NULL,
						(
							IO_DEALLOCATE_WINDOW |
							IO_DISABLE_WINDOW));
		    EVENT_THREAD_MUTEX_EXIT(sp);
		    mutex_exit(&cs_globals.window_lock);
		    return (error);
		} /* if (cs_setup_io_win(2)) */
	    } else {
		client->io_alloc.Window2 = cs_globals.num_windows;
	    } /* if (ior->NumPorts2) */
	} /* if (sp->io_mmap_window) */

	/*
	 * Save a copy of the client's port information so that we
	 *	can verify it in the ReleaseIO call.  We set the IO
	 *	window number(s) allocated in the respective section
	 *	of code, above.
	 */
	client->io_alloc.BasePort1 = ior->BasePort1;
	client->io_alloc.NumPorts1 = ior->NumPorts1;
	client->io_alloc.Attributes1 = ior->Attributes1;
	client->io_alloc.BasePort2 = ior->BasePort2;
	client->io_alloc.NumPorts2 = ior->NumPorts2;
	client->io_alloc.Attributes2 = ior->Attributes2;
	client->io_alloc.IOAddrLines = ior->IOAddrLines;

	/*
	 * Mark this client as having done a successful RequestIO call.
	 */
	client->flags |= (REQ_IO_DONE | CLIENT_IO_ALLOCATED);

	EVENT_THREAD_MUTEX_EXIT(sp);
	mutex_exit(&cs_globals.window_lock);

	return (CS_SUCCESS);
}

/*
 * cs_release_io - releases IO resources allocated by RequestIO; this is
 *			ReleaseIO
 *
 *	calling: cs_release_io(client_handle_t, io_req_t *)
 *
 *	returns: CS_SUCCESS - if IO resources sucessfully deallocated
 *		 CS_BAD_HANDLE - client handle is invalid
 *		 CS_UNSUPPORTED_FUNCTION - if SS is trying to call us
 *		 CS_CONFIGURATION_LOCKED - a RequestConfiguration has been
 *				done without a ReleaseConfiguration
 *		 CS_IN_USE - no RequestIO has been done
 */
static int
cs_release_io(client_handle_t client_handle, io_req_t *ior)
{
	cs_socket_t *sp;
	client_t *client;
	int error;

#ifdef	lint
	ior = NULL;
#endif

	/*
	 * Check to see if this is the Socket Services client handle; if it
	 *	is, we don't support SS using this call.
	 */
	if (CLIENT_HANDLE_IS_SS(client_handle))
	    return (CS_UNSUPPORTED_FUNCTION);

	mutex_enter(&cs_globals.window_lock);

	/*
	 * Get a pointer to this client's socket structure.
	 */
	sp = &cs_sockets[GET_CLIENT_SOCKET(client_handle)];

	EVENT_THREAD_MUTEX_ENTER(sp);

	/*
	 *  Make sure that this is a valid client handle.
	 */
	if (!(client = cs_find_client(client_handle, &error))) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    mutex_exit(&cs_globals.window_lock);
	    return (error);
	}

	/*
	 * If RequestConfiguration has already been done, we don't allow
	 *	this call.
	 */
	if (client->flags & REQ_CONFIGURATION_DONE) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    mutex_exit(&cs_globals.window_lock);
	    return (CS_CONFIGURATION_LOCKED);
	}

	/*
	 * If RequestIO has not been done, we don't allow this call.
	 */
	if (!(client->flags & REQ_IO_DONE)) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    mutex_exit(&cs_globals.window_lock);
	    return (CS_IN_USE);
	}

#ifdef	XXX
	/*
	 * Check the passed IO allocation with the stored allocation; if
	 *	they don't match, then return an error.
	 */
	if ((client->io_alloc.BasePort1 != ior->BasePort1) ||
	    (client->io_alloc.NumPorts1 != ior->NumPorts1) ||
	    (client->io_alloc.Attributes1 != ior->Attributes1) ||
	    (client->io_alloc.BasePort2 != ior->BasePort2) ||
	    (client->io_alloc.NumPorts2 != ior->NumPorts2) ||
	    (client->io_alloc.Attributes2 != ior->Attributes2) ||
	    (client->io_alloc.IOAddrLines != ior->IOAddrLines)) {
		EVENT_THREAD_MUTEX_EXIT(sp);
		mutex_exit(&cs_globals.window_lock);
		return (CS_BAD_ARGS);
	}
#endif

	/*
	 * The code diverges here depending on if this socket supports
	 *	memory mapped IO windows or not.  See comments in the
	 *	cs_request_io function for a description of what's
	 *	going on here.
	 */
	if (sp->io_mmap_window) {
	    io_mmap_window_t *imw = sp->io_mmap_window;

	/*
	 * We should never see this; if we do, it's an internal
	 *	consistency error.
	 */
	    if (!imw->count) {
		cmn_err(CE_CONT, "cs_release_io: socket %d !imw->count\n",
							    sp->socket_num);
		EVENT_THREAD_MUTEX_EXIT(sp);
		mutex_exit(&cs_globals.window_lock);
		return (CS_GENERAL_FAILURE);
	    }

	/*
	 * If the IO window referance count is zero, then deallocate
	 *	and disable this window.
	 */
	    if (!--(imw->count)) {
		(void) cs_setup_io_win(sp->socket_num, imw->number, NULL,
								NULL, NULL,
						(
							IO_DEALLOCATE_WINDOW |
							IO_DISABLE_WINDOW));
	    } /* if (imw->count) */
	} else {
	    (void) cs_setup_io_win(sp->socket_num, client->io_alloc.Window1,
						NULL, NULL, NULL,
						(
							IO_DEALLOCATE_WINDOW |
							IO_DISABLE_WINDOW));
	    if (client->io_alloc.Window2 != cs_globals.num_windows)
		(void) cs_setup_io_win(sp->socket_num, client->io_alloc.Window2,
						NULL, NULL, NULL,
						(
							IO_DEALLOCATE_WINDOW |
							IO_DISABLE_WINDOW));
	} /* if (sp->io_mmap_window) */

	/*
	 * Mark the client as not having any IO resources allocated.
	 */
	client->flags &= ~(REQ_IO_DONE | CLIENT_IO_ALLOCATED);

	EVENT_THREAD_MUTEX_EXIT(sp);
	mutex_exit(&cs_globals.window_lock);
	return (CS_SUCCESS);
}

/*
 * cs_find_io_win - finds an IO window that matches the parameters specified
 *			in the flags argument
 *
 *	calling: sn - socket number to look for IO window on
 *		 *iwc - other window characteristics to match
 *		 *assigned_window - pointer to where we return the assigned
 *					window number if we found a window or
 *					undefined otherwise
 *		 *size - if non-NULL, the found window size will be stored here
 *
 *	returns: CS_SUCCESS - if IO window found
 *		 CS_OUT_OF_RESOURCE - if no windows match requirements
 */
static int
cs_find_io_win(u_long sn, iowin_char_t *iwc, u_long *assigned_window,
								u_long *size)
{
	inquire_window_t inquire_window, *iw;
	unsigned wn;

	iw = &inquire_window;

	for (wn = 0; wn < cs_globals.num_windows; wn++) {
	    iowin_char_t *iowc;

	    iw->window = wn;
	    SocketServices(SS_InquireWindow, iw);

	    iowc = &iw->iowin_char;

	    if (WINDOW_FOR_SOCKET(iw->Sockets, sn) &&
		WINDOW_AVAILABLE_FOR_IO(wn) &&
		(iw->WndCaps & WC_IO) &&
		((iowc->IOWndCaps & iwc->IOWndCaps) == iwc->IOWndCaps)) {

		    *assigned_window = wn;
		    if (size)
			*size = iw->iowin_char.ReqGran;
		    return (CS_SUCCESS);
		}
	}

	return (CS_OUT_OF_RESOURCE);
}

/*
 * cs_allocate_io_win - finds and allocates an IO window
 *
 *	calling: sn - socket number to look for window on
 *		 Attributes - window attributes in io_req_t.Attributes format
 *		 *assigned_window - pointer to return assigned window number
 *
 *	returns: CS_SUCCESS - IO window found and allocated
 *		 CS_OUT_OF_RESOURCE - if cs_find_io_win couldn't find a
 *				window that matches the passed criteria
 *
 * Note: This fucntion will find and allocate an IO window.  The caller is
 *	responsible for deallocating the window.
 */
static int
cs_allocate_io_win(u_long sn, u_long Attributes, u_long *assigned_window)
{
	iowin_char_t iowin_char;

	iowin_char.IOWndCaps =
		((Attributes & IO_DATA_PATH_WIDTH_16)?WC_16BIT:WC_8BIT);

	if (cs_find_io_win(sn, &iowin_char, assigned_window, NULL) ==
								CS_SUCCESS) {
	    cs_windows[*assigned_window].state = (CW_ALLOCATED | CW_IO);
	    return (CS_SUCCESS);
	}

	return (CS_OUT_OF_RESOURCE);
}

/*
 * cs_setup_io_win - setup and destroy an IO window
 *
 *	calling: sn - socket number
 *		 wn - window number
 *		 **Base - pointer to base address to return
 *		 *NumPorts - pointer to number of allocated ports to return
 *		 IOAddrLines - number of IO address lines decoded by this card
 *		 Attributes - either io_req_t attributes, or a combination of
 *				the following flags:
 *				    IO_DEALLOCATE_WINDOW - deallocate the window
 *				    IO_DISABLE_WINDOW - disable the window
 *				When either of these two flags are set, *Base
 *				    and NumPorts should be NULL.
 *
 *	returns: CS_SUCCESS - if no failure
 *		 CS_BAD_WINDOW - if error while trying to configure window
 *
 * Note: We use the IOAddrLines value to determine what base address to pass
 *		to Socket Services.
 */
static int
cs_setup_io_win(u_long sn, u_long wn, void **Base, u_long *NumPorts,
					u_long IOAddrLines, u_long Attributes)
{
	set_window_t set_window;

#ifdef	lint
	if (IOAddrLines != 0)
	    panic("lint panic");
#endif

	if (Attributes & (IO_DEALLOCATE_WINDOW | IO_DISABLE_WINDOW)) {

	    if (Attributes & IO_DEALLOCATE_WINDOW)
		cs_windows[wn].state = 0;

	    if (Attributes & IO_DISABLE_WINDOW) {
		get_window_t get_window;

		get_window.window = wn;

		SocketServices(SS_GetWindow, &get_window);

		set_window.socket = get_window.socket;
		set_window.window = get_window.window;
		set_window.speed = get_window.speed;
		set_window.base = 0;
		set_window.WindowSize = get_window.size;
		set_window.state = get_window.state & ~WS_ENABLED;

		SocketServices(SS_SetWindow, &set_window);
	    }
	    return (CS_SUCCESS);
	} /* if (IO_DEALLOCATE_WINDOW | IO_DISABLE_WINDOW) */

#ifdef	XXX
	/*
	 * See if we can allow Socket Services to select the base address
	 *	value for this card; if the client has specified a non-zero
	 *	base IO address but the card doesn't decode enough IO
	 *	address lines to uniquely use that address, then we have
	 *	the flexibility to choose an alternative base address.
	 */
	if (!IOAddrLines)
	    *Base = 0;
	else
	    *Base = IOADDR_FROBNITZ(*Base, IOAddrLines);
#endif

	set_window.socket = sn;
	set_window.window = wn;
	set_window.speed = IO_WIN_SPEED;
	set_window.base = *Base;
	set_window.WindowSize = *NumPorts;
	set_window.state = (WS_ENABLED | WS_IO |
			((Attributes & IO_DATA_PATH_WIDTH_16)?WS_16BIT:0));

	if (SocketServices(SS_SetWindow, &set_window) != SUCCESS)
	    return (CS_BAD_WINDOW);

	*Base = set_window.base;
	*NumPorts = set_window.WindowSize;

	return (CS_SUCCESS);
}

/*
 * ==== IRQ handling functions ====
 */

/*
 * cs_request_irq - add's client's IRQ handler; supports RequestIRQ
 *
 *	calling: irq_req_t.Attributes - must have the IRQ_TYPE_EXCLUSIVE
 *			flag set, and all other flags clear, or
 *			CS_BAD_ATTRIBUTE will be returned
 *
 *	returns: CS_SUCCESS - if IRQ resources available for client
 *		 CS_BAD_IRQ - if IRQ can not be allocated
 *		 CS_BAD_HANDLE - client handle is invalid
 *		 CS_UNSUPPORTED_FUNCTION - if SS is trying to call us
 *		 CS_NO_CARD - if no card is in socket
 *		 CS_BAD_ATTRIBUTE - if any of the unsupported Attribute
 *					flags are set
 *		 CS_CONFIGURATION_LOCKED - a RequestConfiguration has
 *					already been done
 *		 CS_IN_USE - IRQ ports already in use or function has
 *					already been called
 *
 * Note: We only allow level-mode interrupts.
 */
static int
cs_request_irq(client_handle_t client_handle, irq_req_t *irqr)
{
	cs_socket_t *sp;
	client_t *client;
	set_irq_handler_t set_irq_handler;
	int error;

	/*
	 * Check to see if this is the Socket Services client handle; if it
	 *	is, we don't support SS using this call.
	 */
	if (CLIENT_HANDLE_IS_SS(client_handle))
	    return (CS_UNSUPPORTED_FUNCTION);

	/*
	 * Make sure that none of the unsupported or reserved flags are set.
	 */
	if ((irqr->Attributes &	(IRQ_TYPE_TIME | IRQ_TYPE_DYNAMIC_SHARING |
				IRQ_FIRST_SHARED | IRQ_PULSE_ALLOCATED |
				IRQ_FORCED_PULSE)) ||
		!(irqr->Attributes & IRQ_TYPE_EXCLUSIVE))
	    return (CS_BAD_ATTRIBUTE);

	/*
	 * Get a pointer to this client's socket structure.
	 */
	sp = &cs_sockets[GET_CLIENT_SOCKET(client_handle)];

	EVENT_THREAD_MUTEX_ENTER(sp);

	/*
	 *  Make sure that this is a valid client handle.
	 */
	if (!(client = cs_find_client(client_handle, &error))) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (error);
	}

	/*
	 * If RequestConfiguration has already been done, we don't allow
	 *	this call.
	 */
	if (client->flags & REQ_CONFIGURATION_DONE) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_CONFIGURATION_LOCKED);
	}

	/*
	 * If RequestIRQ has already been done, we don't allow this call.
	 */
	if (client->flags & REQ_IRQ_DONE) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_IN_USE);
	}

	/*
	 * If there's no card in the socket or the card in the socket is not
	 *	for this client, then return an error.
	 */
	if (!(client->flags & CLIENT_CARD_INSERTED)) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_NO_CARD);
	}

	/*
	 * Set up the parameters and ask Socket Services to give us an IRQ
	 *	for this client.  We don't really do much, since the IRQ
	 *	resources are managed by SS and the kernel.  We also don't
	 *	care which IRQ level we are given.
	 */
	set_irq_handler.socket = sp->socket_num;
	set_irq_handler.irq = IRQ_ANY;

	if (irqr->Attributes & IRQ_PRIORITY_HIGH)
	    set_irq_handler.priority = PRIORITY_HIGH;
	else
	    set_irq_handler.priority = PRIORITY_LOW;

	set_irq_handler.handler_id = client_handle;
	set_irq_handler.handler = (f_t *)irqr->irq_handler;
	set_irq_handler.arg = irqr->irq_handler_arg;

	if ((error = SocketServices(SS_SetIRQHandler,
					&set_irq_handler)) != SUCCESS) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_BAD_IRQ);
	}

	irqr->iblk_cookie = set_irq_handler.iblk_cookie;
	irqr->idev_cookie = set_irq_handler.idev_cookie;

	/*
	 * Save the allocated IRQ information for this client.
	 */
	client->irq_alloc.Attributes = irqr->Attributes;
	client->irq_alloc.irq = set_irq_handler.irq;
	client->irq_alloc.priority = set_irq_handler.priority;
	client->irq_alloc.handler_id = set_irq_handler.handler_id;
	client->irq_alloc.irq_handler = (f_t *)set_irq_handler.handler;
	client->irq_alloc.irq_handler_arg = set_irq_handler.arg;

#ifdef	CS_DEBUG
	if (cs_debug > 0)
	    cmn_err(CE_CONT, "cs_request_irq: socket %d irqr->Attributes 0x%x "
						"set_irq_handler.irq 0x%x\n",
						sp->socket_num,
						(int)irqr->Attributes,
						set_irq_handler.irq);
#endif

	/*
	 * Mark this client as having done a successful RequestIRQ call.
	 */
	client->flags |= (REQ_IRQ_DONE | CLIENT_IRQ_ALLOCATED);

	EVENT_THREAD_MUTEX_EXIT(sp);
	return (CS_SUCCESS);
}

/*
 * cs_release_irq - releases IRQ resources allocated by RequestIRQ; this is
 *			ReleaseIRQ
 *
 *	calling: cs_release_irq(client_handle_t, irq_req_t *)
 *
 *	returns: CS_SUCCESS - if IRQ resources sucessfully deallocated
 *		 CS_BAD_IRQ - if IRQ can not be deallocated
 *		 CS_BAD_HANDLE - client handle is invalid
 *		 CS_UNSUPPORTED_FUNCTION - if SS is trying to call us
 *		 CS_CONFIGURATION_LOCKED - a RequestConfiguration has been
 *				done without a ReleaseConfiguration
 *		 CS_IN_USE - no RequestIRQ has been done
 */
static int
cs_release_irq(client_handle_t client_handle, irq_req_t *irqr)
{
	cs_socket_t *sp;
	client_t *client;
	clear_irq_handler_t clear_irq_handler;
	int error;

#ifdef	lint
	irqr = NULL;
#endif

	/*
	 * Check to see if this is the Socket Services client handle; if it
	 *	is, we don't support SS using this call.
	 */
	if (CLIENT_HANDLE_IS_SS(client_handle))
	    return (CS_UNSUPPORTED_FUNCTION);

	/*
	 * Get a pointer to this client's socket structure.
	 */
	sp = &cs_sockets[GET_CLIENT_SOCKET(client_handle)];

	EVENT_THREAD_MUTEX_ENTER(sp);

	/*
	 *  Make sure that this is a valid client handle.
	 */
	if (!(client = cs_find_client(client_handle, &error))) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (error);
	}

	/*
	 * If RequestConfiguration has already been done, we don't allow
	 *	this call.
	 */
	if (client->flags & REQ_CONFIGURATION_DONE) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_CONFIGURATION_LOCKED);
	}

	/*
	 * If RequestIRQ has not been done, we don't allow this call.
	 */
	if (!(client->flags & REQ_IRQ_DONE)) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_IN_USE);
	}

	/*
	 * Tell Socket Services that we want to deregister this client's
	 *	IRQ handler.
	 */
	clear_irq_handler.socket = sp->socket_num;
	clear_irq_handler.handler_id = client->irq_alloc.handler_id;
	clear_irq_handler.handler = (f_t *)client->irq_alloc.irq_handler;

	/*
	 * At this point, we should never fail this SS call; if we do, it
	 *	means that there is an internal consistancy error in either
	 *	Card Services or Socket Services.
	 */
	if ((error = SocketServices(SS_ClearIRQHandler, &clear_irq_handler)) !=
								SUCCESS) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_BAD_IRQ);
	}

	/*
	 * Mark the client as not having any IRQ resources allocated.
	 */
	client->flags &= ~(REQ_IRQ_DONE | CLIENT_IRQ_ALLOCATED);

	EVENT_THREAD_MUTEX_EXIT(sp);
	return (CS_SUCCESS);
}

/*
 * ==== configuration handling functions ====
 */

/*
 * cs_request_configuration - sets up socket and card configuration on behalf
 *		of the client; this is RequestConfiguration
 *
 *	returns: CS_SUCCESS - if configuration sucessfully set
 *		 CS_BAD_SOCKET - if Socket Services returns an error
 *		 CS_UNSUPPORTED_FUNCTION - if SS is trying to call us
 *		 CS_BAD_ATTRIBUTE - if any unsupported or reserved flags
 *					are set
 *		 CS_BAD_TYPE - if the socket doesn't support a mem and IO
 *				interface (SOCKET_INTERFACE_MEMORY_AND_IO set)
 *		 CS_CONFIGURATION_LOCKED - a RequestConfiguration has
 *					already been done
 *		 CS_BAD_VCC - if Vcc value is not supported by socket
 *		 CS_BAD_VPP1 - if Vpp1 value is not supported by socket
 *		 CS_BAD_VPP2 - if Vpp2 value is not supported by socket
 *
 * Bug ID: 1193637 - Card Services RequestConfiguration does not conform
 *	to PCMCIA standard
 * We allow clients to do a RequestConfiguration even if they haven't
 *	done a RequestIO or RequestIRQ.
 */
static int
cs_request_configuration(client_handle_t client_handle, config_req_t *cr)
{
	cs_socket_t *sp;
	client_t *client;
	volatile config_regs_t *crt;
	set_socket_t set_socket;
	get_socket_t get_socket;
	caddr_t cis_base;
	u_long newbase;
	int error;

	/*
	 * Check to see if this is the Socket Services client handle; if it
	 *	is, we don't support SS using this call.
	 */
	if (CLIENT_HANDLE_IS_SS(client_handle))
	    return (CS_UNSUPPORTED_FUNCTION);

#ifdef	XXX
	/*
	 * If the client specifies Vcc = 0 and any non-zero value for
	 *	either of the Vpp members, that's an illegal condition.
	 */
	if (!(cr->Vcc) && (cr->Vpp1 || cr->Vpp2))
	    return (CS_BAD_VCC);
#endif

	/*
	 * Get a pointer to this client's socket structure.
	 */
	sp = &cs_sockets[GET_CLIENT_SOCKET(client_handle)];

	/*
	 * If the client is asking for a memory and IO interface on this
	 *	socket, then check the socket capabilities to be sure that
	 *	this socket supports this configuration.
	 */
	if (cr->IntType & SOCKET_INTERFACE_MEMORY_AND_IO) {
	    inquire_socket_t inquire_socket;

	    inquire_socket.socket = sp->socket_num;

	    if (SocketServices(SS_InquireSocket, &inquire_socket) != SUCCESS)
		return (CS_BAD_SOCKET);

	    if (!(inquire_socket.SocketCaps & IF_IO))
		return (CS_BAD_TYPE);

	} /* if (SOCKET_INTERFACE_MEMORY_AND_IO) */

	EVENT_THREAD_MUTEX_ENTER(sp);

	/*
	 *  Make sure that this is a valid client handle.
	 */
	if (!(client = cs_find_client(client_handle, &error))) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (error);
	}

	/*
	 * If RequestConfiguration has already been done, we don't allow
	 *	this call.
	 */
	if (client->flags & REQ_CONFIGURATION_DONE) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_CONFIGURATION_LOCKED);
	}

	/*
	 * If there's no card in the socket or the card in the socket is not
	 *	for this client, then return an error.
	 */
	if (!(client->flags & CLIENT_CARD_INSERTED)) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_NO_CARD);
	}

	/*
	 * At this point, most of the client's calling parameters have been
	 *	validated, so we can go ahead and configure the socket and
	 *	the card.
	 */
	mutex_enter(&sp->cis_lock);

	/*
	 * Configure the socket with the interface type and voltages requested
	 *	by the client.
	 */
	get_socket.socket = sp->socket_num;

	if (SocketServices(SS_GetSocket, &get_socket) != SUCCESS) {
	    mutex_exit(&sp->cis_lock);
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_BAD_SOCKET);
	}

#ifdef	CS_DEBUG
	if (cs_debug > 0)
	    cmn_err(CE_CONT, "cs_request_configuration: socket %d "
					"client->irq_alloc.irq 0x%x "
					"get_socket.IRQRouting 0x%x\n",
						sp->socket_num,
						(int)client->irq_alloc.irq,
						get_socket.IRQRouting);
#endif

	set_socket.socket = sp->socket_num;
	set_socket.IREQRouting = (client->irq_alloc.irq &
				~(IRQ_HIGH | IRQ_ENABLE | IRQ_PRIORITY));

	if (client->irq_alloc.Attributes & IRQ_PRIORITY_HIGH)
	    set_socket.IREQRouting |= IRQ_PRIORITY;

	if (cr->Attributes & CONF_ENABLE_IRQ_STEERING)
	    set_socket.IREQRouting |= IRQ_ENABLE;

	set_socket.CtlInd = get_socket.CtlInd;
	set_socket.State = 0;	/* don't reset latched values */

	if (cs_convert_powerlevel(sp->socket_num, cr->Vcc, VCC,
					&set_socket.VccLevel) != CS_SUCCESS) {
	    mutex_exit(&sp->cis_lock);
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_BAD_VCC);
	}

	if (cs_convert_powerlevel(sp->socket_num, cr->Vpp1, VPP1,
					&set_socket.Vpp1Level) != CS_SUCCESS) {
	    mutex_exit(&sp->cis_lock);
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_BAD_VPP);
	}

	if (cs_convert_powerlevel(sp->socket_num, cr->Vpp2, VPP2,
					&set_socket.Vpp2Level) != CS_SUCCESS) {
	    mutex_exit(&sp->cis_lock);
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_BAD_VPP);
	}

	if (cr->IntType & SOCKET_INTERFACE_MEMORY_AND_IO)
	    set_socket.IFType = IF_IO;
	else
	    set_socket.IFType = IF_MEMORY;

	/*
	 * Get a pointer to a window that contains the configuration
	 *	registers.
	 */
	mutex_enter(&sp->lock);
	sp->config_regs_offset = cr->ConfigBase;
	mutex_exit(&sp->lock);
	newbase = cr->ConfigBase;
	if (!(cis_base = cs_init_cis_window(sp, sp->config_regs_offset,
								&newbase))) {
	    mutex_exit(&sp->cis_lock);
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    cmn_err(CE_CONT, "cs_request_configuration: socket %d can't init "
				"CIS window\n", sp->socket_num);
	    return (CS_GENERAL_FAILURE);
	}

	/*
	 * Setup the config register pointers.  These pointers have already
	 *	been cleared both in cs_init and in ss_to_cs_events, so if
	 *	the client tells us that we don't have a particular register,
	 *	we don't have to explictly clear that register's pointer.
	 * Note that these pointers are not the complete virtual address;
	 *	the complete address is constructed each time the registers
	 *	are accessed.
	 */
	mutex_enter(&sp->lock);
	crt = &sp->config_regs;
	sp->present = cr->Present;

	/* Configuration Option Register */
	if (sp->present & CONFIG_OPTION_REG_PRESENT)
	    crt->cor_p = (newbase + CONFIG_OPTION_REG_OFFSET);

	/* Configuration and Status Register */
	if (sp->present & CONFIG_STATUS_REG_PRESENT)
	    crt->ccsr_p = (newbase + CONFIG_STATUS_REG_OFFSET);

	/* Pin Replacement Register */
	if (sp->present & CONFIG_PINREPL_REG_PRESENT)
	    crt->prr_p = (newbase + CONFIG_PINREPL_REG_OFFSET);

	/* Socket and Copy Register */
	if (sp->present & CONFIG_COPY_REG_PRESENT)
	    crt->scr_p = (newbase + CONFIG_COPY_REG_OFFSET);

	/*
	 * Setup the bits in the PRR mask that are valid; this is easy, just
	 *	copy the Pin value that the client gave us.  Note that for
	 *	this to work, the client must set both of the XXX_STATUS
	 *	and the XXX_EVENT bits in the Pin member.
	 */
	sp->pin = cr->Pin;

#ifdef	CS_DEBUG
	if (cs_debug > 128)
	    cmn_err(CE_CONT, "cs_request_configuration: sp->pin 0x%x "
				"newbase 0x%x cor_p 0x%x ccsr_p 0x%x "
				"prr_p 0x%x scr_p 0x%x\n",
				sp->pin, (int)newbase, (int)crt->cor_p,
				(int)crt->ccsr_p, (int)crt->prr_p,
				(int)crt->scr_p);
#endif

	/*
	 * Write any configuration registers that the client tells us are
	 *	present to the card; save a copy of what we wrote so that we
	 *	can return them if the client calls GetConfigurationInfo.
	 * The order in which we write the configuration registers is
	 *	specified by the PCMCIA spec; we must write the socket/copy
	 *	register first (if it exists), and then we can write the
	 *	registers in any arbitrary order.
	 */
	/* Socket and Copy Register */
	if (sp->present & CONFIG_COPY_REG_PRESENT) {
	    crt->scr = cr->Copy;
	    *((volatile cfg_regs_t *)((crt->scr_p) + cis_base)) = crt->scr;
	}

#ifdef	XXX
	/* Pin Replacement Register */
	if (sp->present & CONFIG_PINREPL_REG_PRESENT) {
	    crt->prr = cr->Pin;
	    *((volatile cfg_regs_t *)((crt->prr_p) + cis_base)) = crt->prr;
	}
#endif

	/* Configuration and Status Register */
	if (sp->present & CONFIG_STATUS_REG_PRESENT) {
	    crt->ccsr = cr->Status;
	    *((volatile cfg_regs_t *)((crt->ccsr_p) + cis_base)) = crt->ccsr;
	}

	/*
	 * Mark the socket as being in IO mode.
	 */
	if (cr->IntType & SOCKET_INTERFACE_MEMORY_AND_IO)
	    sp->flags |= SOCKET_IS_IO;

	mutex_exit(&sp->lock);

	/*
	 * Now that we know if the PRR is present and if it is, which
	 *	bits in the PRR are valid, we can construct the correct
	 *	socket event mask.
	 */
	set_socket.SCIntMask = cs_merge_event_masks(sp);

	/*
	 * Set the socket to the parameters that the client requested.
	 */
	if (SocketServices(SS_SetSocket, &set_socket) != SUCCESS) {
	    if (sp->present & CONFIG_OPTION_REG_PRESENT) {
		crt->cor = 0; /* XXX is 0 the right thing here? */
		*((volatile cfg_regs_t *)((crt->cor_p) + cis_base)) = crt->cor;
	    }
	    sp->flags &= ~SOCKET_IS_IO;
	    mutex_exit(&sp->cis_lock);
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_BAD_SOCKET);
	}

	/*
	 * Configuration Option Register - we handle this specially since
	 *	we don't allow the client to manipulate the RESET or
	 *	INTERRUPT bits (although a client can manipulate these
	 *	bits via an AccessConfigurationRegister call - explain
	 *	THAT logic to me).
	 * XXX - we force level-mode interrupts (COR_LEVEL_IRQ)
	 */
	if (sp->present & CONFIG_OPTION_REG_PRESENT) {
	    crt->cor = ((cr->ConfigIndex | COR_LEVEL_IRQ) & ~COR_SOFT_RESET);
	    *((volatile cfg_regs_t *)((crt->cor_p) + cis_base)) = crt->cor;
	}

	/*
	 * Mark this client as having done a successful RequestConfiguration
	 *	call.
	 */
	client->flags |= REQ_CONFIGURATION_DONE;

	mutex_exit(&sp->cis_lock);
	EVENT_THREAD_MUTEX_EXIT(sp);

	return (CS_SUCCESS);
}

/*
 * cs_release_configuration - releases configuration previously set via the
 *		RequestConfiguration call; this is ReleaseConfiguration
 *
 *	returns: CS_SUCCESS - if configuration sucessfully released
 *		 CS_UNSUPPORTED_FUNCTION - if SS is trying to call us
 *		 CS_BAD_SOCKET - if Socket Services returns an error
 *		 CS_BAD_HANDLE - a RequestConfiguration has not been done
 */
static int
cs_release_configuration(client_handle_t client_handle)
{
	cs_socket_t *sp;
	client_t *client;
	volatile config_regs_t *crt;
	set_socket_t set_socket;
	get_socket_t get_socket;
	caddr_t cis_base;
	int error;

	/*
	 * Check to see if this is the Socket Services client handle; if it
	 *	is, we don't support SS using this call.
	 */
	if (CLIENT_HANDLE_IS_SS(client_handle))
	    return (CS_UNSUPPORTED_FUNCTION);

	/*
	 * Get a pointer to this client's socket structure.
	 */
	sp = &cs_sockets[GET_CLIENT_SOCKET(client_handle)];

	EVENT_THREAD_MUTEX_ENTER(sp);

	/*
	 *  Make sure that this is a valid client handle.
	 */
	if (!(client = cs_find_client(client_handle, &error))) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (error);
	}

	/*
	 * If RequestConfiguration has not been done, we don't allow
	 *	this call.
	 */
	if (!(client->flags & REQ_CONFIGURATION_DONE)) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_BAD_HANDLE);
	}

	mutex_enter(&sp->cis_lock);

	/*
	 * Set the card back to a memory-only interface byte writing a zero
	 *	to the COR.  Note that we don't update our soft copy of the
	 *	COR state since the PCMCIA spec only requires us to maintain
	 *	the last value that was written to that register during a
	 *	call to RequestConfiguration.
	 */
	crt = &sp->config_regs;

	if (!(cis_base = cs_init_cis_window(sp, sp->config_regs_offset,
								NULL))) {
	    mutex_exit(&sp->cis_lock);
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    cmn_err(CE_CONT, "cs_release_configuration: socket %d can't init "
				"CIS window\n", sp->socket_num);
	    return (CS_GENERAL_FAILURE);
	}

	if (sp->present & CONFIG_OPTION_REG_PRESENT)
	    *((volatile cfg_regs_t *)((crt->cor_p) + cis_base)) = 0;

	/*
	 * Set the socket back to a memory-only interface; don't change
	 *	any other parameter of the socket.
	 */
	get_socket.socket = sp->socket_num;

	if (SocketServices(SS_GetSocket, &get_socket) != SUCCESS) {
	    mutex_exit(&sp->cis_lock);
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_BAD_SOCKET);
	}

	mutex_enter(&sp->lock);
	sp->flags &= ~SOCKET_IS_IO;
	set_socket.SCIntMask = cs_merge_event_masks(sp);
	mutex_exit(&sp->lock);

	set_socket.socket = sp->socket_num;
	set_socket.IREQRouting = 0;
	set_socket.CtlInd = get_socket.CtlInd;
	set_socket.State = 0;	/* don't reset latched values */
	set_socket.VccLevel = get_socket.VccLevel;
	set_socket.Vpp1Level = get_socket.Vpp1Level;
	set_socket.Vpp2Level = get_socket.Vpp2Level;
	set_socket.IFType = IF_MEMORY;

	if (SocketServices(SS_SetSocket, &set_socket) != SUCCESS) {
	    mutex_exit(&sp->cis_lock);
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_BAD_SOCKET);
	}

	/*
	 * Mark this client as not having a configuration.
	 */
	client->flags &= ~REQ_CONFIGURATION_DONE;

	mutex_exit(&sp->cis_lock);
	EVENT_THREAD_MUTEX_EXIT(sp);

	return (CS_SUCCESS);
}

/*
 * cs_modify_configuration - modifies a configuration established by
 *		RequestConfiguration; this is ModifyConfiguration
 *
 *	returns: CS_SUCCESS - if configuration sucessfully modified
 *		 CS_BAD_SOCKET - if Socket Services returns an error
 *		 CS_UNSUPPORTED_FUNCTION - if SS is trying to call us
 *		 CS_BAD_HANDLE - a RequestConfiguration has not been done
 *		 CS_NO_CARD - if no card in socket
 *		 CS_BAD_ATTRIBUTE - if any unsupported or reserved flags
 *					are set
 *		 CS_BAD_VCC - if Vcc value is not supported by socket
 *		 CS_BAD_VPP1 - if Vpp1 value is not supported by socket
 *		 CS_BAD_VPP2 - if Vpp2 value is not supported by socket
 */
static int
cs_modify_configuration(client_handle_t client_handle, modify_config_t *mc)
{
	cs_socket_t *sp;
	client_t *client;
	set_socket_t set_socket;
	get_socket_t get_socket;
	int error;

	/*
	 * Check to see if this is the Socket Services client handle; if it
	 *	is, we don't support SS using this call.
	 */
	if (CLIENT_HANDLE_IS_SS(client_handle))
	    return (CS_UNSUPPORTED_FUNCTION);

	/*
	 * Get a pointer to this client's socket structure.
	 */
	sp = &cs_sockets[GET_CLIENT_SOCKET(client_handle)];

	EVENT_THREAD_MUTEX_ENTER(sp);

	/*
	 *  Make sure that this is a valid client handle.
	 */
	if (!(client = cs_find_client(client_handle, &error))) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (error);
	}

	/*
	 * If RequestConfiguration has not been done, we don't allow
	 *	this call.
	 */
	if (!(client->flags & REQ_CONFIGURATION_DONE)) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_BAD_HANDLE);
	}

	/*
	 * If there's no card in the socket or the card in the socket is not
	 *	for this client, then return an error.
	 */
	if (!(client->flags & CLIENT_CARD_INSERTED)) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_NO_CARD);
	}

	/*
	 * Get the current socket parameters so that we can modify them.
	 */
	get_socket.socket = sp->socket_num;

	if (SocketServices(SS_GetSocket, &get_socket) != SUCCESS) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_BAD_SOCKET);
	}

#ifdef	CS_DEBUG
	if (cs_debug > 0)
	    cmn_err(CE_CONT, "cs_modify_configuration: socket %d "
				"client->irq_alloc.irq 0x%x "
				"get_socket.IRQRouting 0x%x\n",
				sp->socket_num, (int)client->irq_alloc.irq,
				get_socket.IRQRouting);
#endif

	set_socket.socket = sp->socket_num;
	set_socket.SCIntMask = get_socket.SCIntMask;
	set_socket.CtlInd = get_socket.CtlInd;
	set_socket.State = 0;	/* don't reset latched values */
	set_socket.IFType = get_socket.IFType;

	set_socket.IREQRouting = get_socket.IRQRouting;

	/*
	 * Modify the IRQ routing if the client wants it modified.
	 */
	if (mc->Attributes & CONF_IRQ_CHANGE_VALID) {
	    set_socket.IREQRouting &= ~(IRQ_HIGH | IRQ_ENABLE | IRQ_PRIORITY);
	    if (mc->Attributes & CONF_ENABLE_IRQ_STEERING)
		set_socket.IREQRouting |= IRQ_ENABLE;
	    if (client->irq_alloc.Attributes & IRQ_PRIORITY_HIGH)
		set_socket.IREQRouting |= IRQ_PRIORITY;
	} /* CONF_IRQ_CHANGE_VALID */

	/*
	 * Modify the voltage levels that the client specifies.
	 */
	if (mc->Attributes & CONF_VCC_CHANGE_VALID) {
	    if (cs_convert_powerlevel(sp->socket_num, mc->Vcc, VCC,
					&set_socket.VccLevel) != CS_SUCCESS) {
		EVENT_THREAD_MUTEX_EXIT(sp);
		return (CS_BAD_VCC);
	    }
	} else {
	    set_socket.VccLevel = get_socket.VccLevel;
	}

	if (mc->Attributes & CONF_VPP1_CHANGE_VALID) {
	    if (cs_convert_powerlevel(sp->socket_num, mc->Vpp1, VPP1,
					&set_socket.Vpp1Level) != CS_SUCCESS) {
		EVENT_THREAD_MUTEX_EXIT(sp);
		return (CS_BAD_VPP);
	    }
	} else {
	    set_socket.Vpp1Level = get_socket.Vpp1Level;
	}

	if (mc->Attributes & CONF_VPP2_CHANGE_VALID) {
	    if (cs_convert_powerlevel(sp->socket_num, mc->Vpp2, VPP2,
					&set_socket.Vpp2Level) != CS_SUCCESS) {
		EVENT_THREAD_MUTEX_EXIT(sp);
		return (CS_BAD_VPP);
	    }
	} else {
	    set_socket.Vpp2Level = get_socket.Vpp2Level;
	}

	/*
	 * Setup the modified socket configuration.
	 */
	if (SocketServices(SS_SetSocket, &set_socket) != SUCCESS) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_BAD_SOCKET);
	}

	EVENT_THREAD_MUTEX_EXIT(sp);
	return (CS_SUCCESS);
}

/*
 * cs_access_configuration_register - provides a client access to the card's
 *		configuration registers; this is AccessConfigurationRegister
 *
 *	returns: CS_SUCCESS - if register accessed successfully
 *		 CS_UNSUPPORTED_FUNCTION - if SS is trying to call us
 *		 CS_BAD_ARGS - if arguments are out of range
 *		 CS_NO_CARD - if no card in socket
 *		 CS_BAD_BASE - if no config registers base address
 *		 CS_UNSUPPORTED_MODE - if no RequestConfiguration has
 *				been done yet
 */
static int
cs_access_configuration_register(client_handle_t client_handle,
						access_config_reg_t *acr)
{
	volatile cfg_regs_t *reg;
	cs_socket_t *sp;
	client_t *client;
	caddr_t cis_base;
	int error;

	/*
	 * Check to see if this is the Socket Services client handle; if it
	 *	is, we don't support SS using this call.
	 */
	if (CLIENT_HANDLE_IS_SS(client_handle))
	    return (CS_UNSUPPORTED_FUNCTION);

	/*
	 * Make sure that the specifed offset is in range.
	 */
	if (acr->Offset > ((CISTPL_CONFIG_MAX_CONFIG_REGS * 2) - 2))
	    return (CS_BAD_ARGS);

	/*
	 * Get a pointer to this client's socket structure.
	 */
	sp = &cs_sockets[GET_CLIENT_SOCKET(client_handle)];

	EVENT_THREAD_MUTEX_ENTER(sp);

	/*
	 *  Make sure that this is a valid client handle.
	 */
	if (!(client = cs_find_client(client_handle, &error))) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (error);
	}

	/*
	 * If there's no card in the socket or the card in the socket is not
	 *	for this client, then return an error.
	 */
	if (!(client->flags & CLIENT_CARD_INSERTED)) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_NO_CARD);
	}

	/*
	 * If RequestConfiguration has not been done, we don't allow
	 *	this call.
	 */
	if (!(client->flags & REQ_CONFIGURATION_DONE)) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_UNSUPPORTED_MODE);
	}

	mutex_enter(&sp->cis_lock);

	/*
	 * Get a pointer to the CIS window
	 */
	if (!(cis_base = cs_init_cis_window(sp, sp->config_regs_offset,
								NULL))) {
	    mutex_exit(&sp->cis_lock);
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    cmn_err(CE_CONT, "cs_ACR: socket %d can't init CIS window\n",
							sp->socket_num);
	    return (CS_GENERAL_FAILURE);
	}

	/*
	 * Create the address for the config register that the client
	 *	wants to access.
	 */
	mutex_enter(&sp->lock);
	reg = (cfg_regs_t *)(sp->config_regs_offset + acr->Offset + cis_base);

#ifdef	CS_DEBUG
	if (cs_debug > 1) {
	    cmn_err(CE_CONT, "cs_ACR: reg 0x%x config_regs_offset 0x%x "
							"Offset 0x%x\n",
						(int)reg,
						(int)sp->config_regs_offset,
						(int)acr->Offset);
}
#endif

	/*
	 * Determine what the client wants us to do.  The client is
	 *	allowed to specify any valid offset, even if it would
	 *	cause an unimplemented configuration register to be
	 *	accessed.
	 */
	error = CS_SUCCESS;
	switch (acr->Action) {
	    case CONFIG_REG_READ:
		acr->Value = *reg;
		break;
	    case CONFIG_REG_WRITE:
		*reg = acr->Value;
		break;
	    default:
		error = CS_BAD_ARGS;
		break;
	} /* switch */

	mutex_exit(&sp->lock);
	mutex_exit(&sp->cis_lock);
	EVENT_THREAD_MUTEX_EXIT(sp);

	return (error);
}

/*
 * ==== general functions ====
 */

/*
 * cs_map_log_socket - takes a client_handle_t and returns the physical
 *			socket number; supports the MapLogSocket call
 *
 * Note: We provide this function since the instance number of a client
 *		driver doesn't necessary correspond to the physical
 *		socket number (thanks, DDI team).
 *
 *	returns: CS_SUCCESS
 *
 * XXX - should we check to be sure that the client handle belongs to
 *		a valid client?
 */
static int
cs_map_log_socket(client_handle_t ch, map_log_socket_t *mls)
{

	mls->Socket = GET_CLIENT_SOCKET(ch);

	return (CS_SUCCESS);
}

/*
 * cs_convert_speed - convers nS to devspeed and devspeed to nS
 *
 * The actual function is is in the CIS parser module; this
 *	is only a wrapper.
 */
static int
cs_convert_speed(convert_speed_t *cs)
{
	return ((int)CIS_PARSER(CISP_CIS_CONV_DEVSPEED, cs));
}

/*
 * cs_convert_size - converts a devsize value to a size in bytes value
 *			or a size in bytes value to a devsize value
 *
 * The actual function is is in the CIS parser module; this
 *	is only a wrapper.
 */
static int
cs_convert_size(convert_size_t *cs)
{
	return ((int)CIS_PARSER(CISP_CIS_CONV_DEVSIZE, cs));
}

/*
 * cs_convert_powerlevel - converts a power level in tenths of a volt
 *			to a power table entry for the specified socket
 *
 *	returns: CS_SUCCESS - if volts converted to a valid power level
 *		 CS_BAD_ADAPTER - if SS_InquireAdapter fails
 *		 CS_BAD_ARGS - if volts are not supported on this socket
 *				and adapter
 */
static int
cs_convert_powerlevel(u_long sn, u_long volts, u_long flags, unsigned *pl)
{
	inquire_adapter_t inquire_adapter;
	int i;

#ifdef	lint
	if (sn == 0)
	    panic("lint panic");
#endif

	*pl = 0;

	if (SocketServices(SS_InquireAdapter, &inquire_adapter) != SUCCESS)
	    return (CS_BAD_ADAPTER);

	for (i = 0; (i < inquire_adapter.NumPower); i++) {
	    if ((inquire_adapter.power_entry[i].ValidSignals & flags) &&
		(inquire_adapter.power_entry[i].PowerLevel == volts)) {
		*pl = i;
		return (CS_SUCCESS);
	    }
	}

	return (CS_BAD_ARGS);
}

/*
 * cs_event2text - returns text string(s) associated with the event; this
 *			function supports the Event2Text CS call.
 *
 *	calling: event2text_t * - pointer to event2text struct
 *			The text buffer must be allocated by the caller
 *			and be at least MAX_CS_EVENT_BUFSIZE bytes.
 *		 int event_source - specifies event type in event2text_t:
 *					0 - SS event
 *					1 - CS event
 *
 *	returns: CS_SUCCESS
 *		 CS_OUT_OF_RESOURCE if NULL text buffer passed in for
 *			CS events
 */
static int
cs_event2text(event2text_t *e2t, int event_source)
{
	event_t event;
	char *sepchar = "|";

	/*
	 * If the caller just wants to find out the required buffer
	 *	size, give it to them and return.
	 */
	if (e2t->Attributes & GET_CS_EVENT_BUFSIZE) {
	    e2t->BufSize = MAX_CS_EVENT_BUFSIZE;
	    return (CS_SUCCESS);
	}

	/*
	 * If the caller is expecting to get multiple events at
	 *	one time (such as SS or CS), they will need a
	 *	much larger buffer.  This will return the size
	 *	of the largest buffer necessary for these callers.
	 */
	if (e2t->Attributes & GET_MULTI_EVENT_BUFSIZE) {
	    e2t->BufSize = MAX_MULTI_EVENT_BUFSIZE;
	    return (CS_SUCCESS);
	}

	/*
	 * If event_source is 0, this is a SS event
	 */
	if (!event_source) {
	    for (event = 0; event < MAX_SS_EVENTS; event++) {
		if (cs_ss_event_text[event].ss_event == e2t->event) {
		    e2t->text = cs_ss_event_text[event].text;
		    return (CS_SUCCESS);
		}
	    }
	    e2t->text = cs_ss_event_text[MAX_CS_EVENTS].text;
	    return (CS_SUCCESS);
	} else {
	/*
	 * This is a CS event
	 */
	    if (!e2t->text) {
		cmn_err(CE_CONT, "cs_event2text: NULL text buffer\n");
		return (CS_OUT_OF_RESOURCE);
	    }
	    *e2t->text = '\0';
	    for (event = 0; event < MAX_CS_EVENTS; event++) {
		if (cs_ss_event_text[event].cs_event & e2t->event) {
		    strcat(e2t->text, cs_ss_event_text[event].text);
		    strcat(e2t->text, sepchar);
		} /* if (cs_ss_event_text) */
	    } /* for (event) */
	    if (*e2t->text)
		e2t->text[strlen(e2t->text)-1] = NULL;
	} /* if (!event_source) */

	return (CS_SUCCESS);
}

/*
 * cs_csfunc2text - returns a pointer to a text string containing the name
 *			of the passed Card Services function or return code
 *
 *	This function supports the CSFunction2Text CS call.
 */
static char *
cs_csfunc2text(int function, int type)
{
	cs_csfunc2text_strings_t *cfs;
	int end_marker;

	if (type == CSFUN2TEXT_FUNCTION) {
	    cfs = cs_csfunc2text_funcstrings;
	    end_marker = CSFuncListEnd;
	} else {
	    cfs = cs_csfunc2text_returnstrings;
	    end_marker = CS_ERRORLIST_END;
	}

	while (cfs->item != end_marker) {
	    if (cfs->item == function)
		return (cfs->text);
	    cfs++;
	}

	return (cfs->text);
}

/*
 * cs_make_device_node - creates/removes device nodes on a client's behalf;
 *					this is MakeDeviceNode
 *
 *	returns: CS_SUCCESS - if all device nodes successfully created/removed
 *		 CS_BAD_ATTRIBUTE - if NumDevNodes is not zero when Action
 *				is REMOVAL_ALL_DEVICES
 *		 CS_BAD_ARGS - if an invalid Action code is specified
 *		 CS_UNSUPPORTED_FUNCTION - if SS is trying to call us
 *		 CS_OUT_OF_RESOURCE - if can't create/remove device node
 */
static int
cs_make_device_node(client_handle_t client_handle, make_device_node_t *mdn)
{
	cs_socket_t *sp;
	client_t *client;
	ss_make_device_node_t ss_make_device_node;
	int error, i;

	/*
	 * Check to see if this is the Socket Services client handle; if it
	 *	is, we don't support SS using this call.
	 */
	if (CLIENT_HANDLE_IS_SS(client_handle))
	    return (CS_UNSUPPORTED_FUNCTION);

	/*
	 * Get a pointer to this client's socket structure.
	 */
	sp = &cs_sockets[GET_CLIENT_SOCKET(client_handle)];

	EVENT_THREAD_MUTEX_ENTER(sp);

	/*
	 *  Make sure that this is a valid client handle.
	 */
	if (!(client = cs_find_client(client_handle, &error))) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (error);
	}

#ifdef	XXX
	/*
	 * If there's no card in the socket or the card in the socket is not
	 *	for this client, then return an error.
	 */
	if (!(client->flags & CLIENT_CARD_INSERTED)) {
	    EVENT_THREAD_MUTEX_EXIT(sp);
	    return (CS_NO_CARD);
	}
#endif

	/*
	 * Setup the client's dip, since we use it later on.
	 */
	ss_make_device_node.dip = client->dip;

	/*
	 * Make sure that we're being given a valid Action.  Set the default
	 *	error code as well.
	 */
	error = CS_BAD_ARGS;	/* for default case */
	switch (mdn->Action) {
	    case CREATE_DEVICE_NODE:
	    case REMOVE_DEVICE_NODE:
		break;
	    case REMOVAL_ALL_DEVICE_NODES:
		if (mdn->NumDevNodes) {
		    error = CS_BAD_ATTRIBUTE;
		} else {
		    ss_make_device_node.flags = SS_CSINITDEV_REMOVE_DEVICE;
		    ss_make_device_node.name = NULL;
		    SocketServices(CSInitDev, &ss_make_device_node);
		    error = CS_SUCCESS;
		}
		/* fall-through case */
	    default:
		EVENT_THREAD_MUTEX_EXIT(sp);
		return (error);
		/* NOTREACHED */
	} /* switch */

	/*
	 * Loop through the device node descriptions and create or destroy
	 *	the device node.
	 */
	for (i = 0; i < mdn->NumDevNodes; i++) {
	    struct devnode_desc *devnode_desc = &mdn->devnode_desc[i];

	    ss_make_device_node.name = devnode_desc->name;
	    ss_make_device_node.spec_type = devnode_desc->spec_type;
	    ss_make_device_node.minor_num = devnode_desc->minor_num;
	    ss_make_device_node.node_type = devnode_desc->node_type;

	/*
	 * Set the appropriate flag for the action that we want
	 *	SS to perform. Note that if we ever OR-in the flag
	 *	here, we need to be sure to clear the flags member
	 *	since we sometimes OR-in other flags below.
	 */
	    if (mdn->Action == CREATE_DEVICE_NODE) {
		ss_make_device_node.flags = SS_CSINITDEV_CREATE_DEVICE;
	    } else {
		ss_make_device_node.flags = SS_CSINITDEV_REMOVE_DEVICE;
	    }

	/*
	 * If this is not the last device to process, then we need
	 *	to tell SS that more device process requests are on
	 *	their way after this one.
	 */
	    if (i < (mdn->NumDevNodes - 1))
		ss_make_device_node.flags |= SS_CSINITDEV_MORE_DEVICES;

	    if (SocketServices(CSInitDev, &ss_make_device_node) != SUCCESS) {
		EVENT_THREAD_MUTEX_EXIT(sp);
		return (CS_OUT_OF_RESOURCE);
	    } /* CSInitDev */
	} /* for (mdn->NumDevNodes) */

	EVENT_THREAD_MUTEX_EXIT(sp);
	return (CS_SUCCESS);
}

/*
 * cs_ddi_info - this function is used by clients that need to support
 *			the xxx_getinfo function; this is CS_DDI_Info
 */
static int
cs_ddi_info(cs_ddi_info_t *cdi)
{
	cs_socket_t *sp;
	client_t *client;

#ifdef	CS_DEBUG
	if (cs_debug > 0) {
	    cmn_err(CE_CONT, "cs_ddi_info: socket %d client [%s]\n",
					(int)cdi->Socket, cdi->driver_name);
	}
#endif

	/*
	 * Check to see if the socket number is in range - the system
	 *	framework may cause a client driver to call us with
	 *	a socket number that used to be present but isn't
	 *	anymore. This is not a bug, and it's OK to return
	 *	an error if the socket number is out of range.
	 */
	if (!CHECK_SOCKET_NUM(cdi->Socket, cs_globals.num_sockets)) {

#ifdef	CS_DEBUG
	    if (cs_debug > 0) {
		cmn_err(CE_CONT, "cs_ddi_info: socket %d client [%s] "
						"SOCKET IS OUT OF RANGE\n",
							(int)cdi->Socket,
							cdi->driver_name);
	    }
#endif

	    return (CS_BAD_SOCKET);
	} /* if (!CHECK_SOCKET_NUM) */

	/*
	 * Get a pointer to this client's socket structure.
	 */
	sp = &cs_sockets[cdi->Socket];

	EVENT_THREAD_MUTEX_ENTER(sp);

	client = sp->client_list;
	while (client) {

#ifdef	CS_DEBUG
	if (cs_debug > 0) {
	    cmn_err(CE_CONT, "cs_ddi_info: socket %d checking client [%s] "
							"handle 0x%x\n",
						(int)cdi->Socket,
						client->driver_name,
						(int)client->client_handle);
	}
#endif

	    if (!(strcmp(client->driver_name, cdi->driver_name))) {
		cdi->dip = client->dip;
		cdi->instance = client->instance;

#ifdef	CS_DEBUG
		if (cs_debug > 0) {
		    cmn_err(CE_CONT, "cs_ddi_info: found client [%s] "
						"instance %d handle 0x%x\n",
					client->driver_name, client->instance,
					(int)client->client_handle);
		}
#endif

		EVENT_THREAD_MUTEX_EXIT(sp);
		return (CS_SUCCESS);
	    }
	    client = client->next;
	} /* while (client) */

	EVENT_THREAD_MUTEX_EXIT(sp);
	return (CS_BAD_SOCKET);
}
