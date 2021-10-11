/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ident "@(#)cis.c	1.20	95/07/17 SMI"

/*
 * This is a collection of routines that make up the Card Information
 *	Structure (CIS) interpreter.  The algorigthms used are based
 *	on the Release 2.01 PCMCIA standard.
 *
 * Note that a bunch of comments are not indented correctly with the
 *	code that they are commenting on. This is because cstyle is
 *	inflexible concerning 4-column indenting.
 */

/*
 * _depends_on used to be static, but SC3.0 wants it global
 */
/* char _depends_on[] = "misc/cs"; */
/*
 * XXX - we should be able to _depends_on a misc module as well as
 *		a driver
 */
char _depends_on[] = "drv/pcmcia";

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
#include <sys/kstat.h>
#include <sys/kmem.h>
#include <sys/modctl.h>
#include <sys/kobj.h>

#include <pcmcia/sys/cis.h>
#include <pcmcia/sys/cis_handlers.h>
#include <pcmcia/sys/cs_types.h>
#include <pcmcia/sys/cs.h>
#include <pcmcia/sys/cs_priv.h>
#include <pcmcia/sys/cis_protos.h>
#include <sys/pctypes.h>
#include <sys/pcmcia.h>
#include <sys/sservice.h>

/*
 * Function declarations
 */
void *CISParser(int function, ...);
static int (*cis_card_services)(int, ...) = NULL;
static void cisp_init();
static void cis_deinit();

extern cistpl_callout_t cistpl_std_callout[];
extern cistpl_devspeed_struct_t cistpl_devspeed_struct;

/*
 * This is the loadable module wrapper.
 */
extern struct mod_ops mod_miscops;

static struct modlmisc modldrv = {
	&mod_miscops,			/* Type of module. */
	"PCMCIA CIS Interpreter",	/* Name of the module. */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};

#ifdef	CIS_DEBUG
int	cis_debug = 0;
#endif

#ifdef	_KERNEL

int
_init()
{

	/*
	 * Initialize
	 */
	cisp_init();

	return (mod_install(&modlinkage));
}

int cis_module_can_be_unloaded = 0;

int
_fini()
{
	int ret;

	if (cis_module_can_be_unloaded == 0)
		return (-1);

	if ((ret = mod_remove(&modlinkage)) == 0)
		cis_deinit();

	return (ret);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

#endif

/*
 * cisp_init - initialize the CIS parser
 */
static void
cisp_init()
{
	csregister_t csr;

	/*
	 * Fill out the function for CISSetAddress
	 */
	csr.cs_magic = PCCS_MAGIC;
	csr.cs_version = PCCS_VERSION;
	csr.cs_event = (f_t *)CISParser;

	/*
	 * We have to call SS instead of CS to register because we
	 *	can't do a _depends_on for CS
	 */
	SocketServices(CISSetAddress, &csr);

}

/*
 * cis_deinit - deinitialize the CIS parser
 */
static void
cis_deinit()
{

	/*
	 * Tell CS that we're gone.
	 */
	if (cis_card_services)
	    CIS_CARD_SERVICES(CISUnregister);

	return;

}

/*
 * CISParser - this is the entrypoint for all of the CIS Interpreter
 *		functions
 */
void *
CISParser(int function, ...)
{
	va_list arglist;
	void *retcode = (void *)CS_UNSUPPORTED_FUNCTION;

#if defined(CIS_DEBUG)
	if (cis_debug > 1) {
	    cmn_err(CE_CONT, "CISParser: called with function 0x%x\n",
				function);
	}
#endif

	va_start(arglist, function);

	/*
	 * ...and here's the CIS Interpreter waterfall
	 */
	switch (function) {
	    case CISP_CIS_SETUP: {
		csregister_t *csr;
		cisregister_t cisr;

		    csr = va_arg(arglist, csregister_t *);
		    cis_card_services = csr->cs_card_services;

		    cisr.cis_magic = PCCS_MAGIC;
		    cisr.cis_version = PCCS_VERSION;
		    cisr.cis_parser = CISParser;
		    cisr.cistpl_std_callout = cistpl_std_callout;

		/*
		 * Tell CS that we're here and what our entrypoint
		 *	address is.
		 */
		    CIS_CARD_SERVICES(CISRegister, &cisr);

		}
		break;
	    case CISP_CIS_LIST_CREATE: {
		cistpl_callout_t *cistpl_callout;
		cisptr_t *cisptr;
		cistpl_t **cistplbase;

		    cistpl_callout = va_arg(arglist, cistpl_callout_t *);
		    cisptr = va_arg(arglist, cisptr_t *);
		    cistplbase = va_arg(arglist, cistpl_t **);

		    retcode = (void *)cis_list_create(cistpl_callout, cisptr,
							cistplbase);
		}
		break;
	    case CISP_CIS_LIST_DESTROY: {
		cistpl_t **cistplbase;

		    cistplbase = va_arg(arglist, cistpl_t **);

		    retcode = (void *)cis_list_destroy(cistplbase);
		}
		break;
	    case CISP_CIS_GET_LTUPLE: {
		cistpl_t *tp;
		cisdata_t type;
		int flags;

		    tp = va_arg(arglist, cistpl_t *);
		    type = va_arg(arglist, cisdata_t);
		    flags = va_arg(arglist, int);

		    retcode = (void *)cis_get_ltuple(tp, type, flags);
		}
		break;

	    case CISP_CIS_PARSE_TUPLE: {
		cistpl_callout_t *co;
		cistpl_t *tp;
		int flags;
		void *arg;
		cisdata_t subtype;

		    co = va_arg(arglist, cistpl_callout_t *);
		    tp = va_arg(arglist, cistpl_t *);
		    flags = va_arg(arglist, int);
		    arg = va_arg(arglist, void *);
		    subtype = va_arg(arglist, cisdata_t);

		    retcode = (void *)cis_tuple_handler(co, tp,
					flags, arg, subtype);
		}
		break;

	    case CISP_CIS_CONV_DEVSPEED:
		retcode = (void *)cis_convert_devspeed(
				va_arg(arglist, convert_speed_t *));
		break;

	    case CISP_CIS_CONV_DEVSIZE:
		retcode = (void *)cis_convert_devsize(
				va_arg(arglist, convert_size_t *));
		break;

	    default:
		break;
	}

	return (retcode);
}

/*
 * cis_list_create - read a PC card's CIS and create a local linked CIS list
 *
 *	cistpl_callout_t *cistpl_callout - pointer to callout structure
 *				array to use to find tuples.
 *	cisptr_t cisptr - pointer to a structure containing the VA and
 *				offset from where we should start reading
 *				CIS bytes as well as misc flags.
 *	cistpl_t **cistplbase - pointer to a pointer to the base of a
 *				local linked CIS list; pass this as a
 *				pointer to a NULL pointer if you want
 *				to create a new list.
 *
 * We return the a count of the number of tuples that we saw, not including
 *	any CISTPL_END or CISTPL_NULL tuples if there were no problems
 *	processing the CIS.  If a tuple handler returns an error, we
 *	immediately return with the error code from the handler. An
 *	error return code will always have the HANDTPL_ERROR bit set
 *	to allow the caller to distinguish an error from a valid tuple
 *	count.
 * XXX need to add CISTPL_END and CISTPL_NULL tuples to the list, and need
 *	to be sure that the tuple count reflects these tuples
 *
 * If we attempt to read beyond the end of the mapped in CIS address space,
 *	the BAD_CIS_ADDR error code is returned.
 *
 * This function only interprets the CISTPL_END and CISTPL_NULL tuples as
 *	well as any tuple with a link field of CISTPL_END.
 *
 * Tuples of type CISTPL_END or CISTPL_NULL are not added to the list.
 *
 * To append tuples to end of a local linked CIS list, pass a pointer to the
 *	address of the last element in the list that you want tuples appended
 *	to. This pointer should be passed in **cistplbase.
 *
 * To process tuple chains with any long link targets, call this routine
 *	for each tuple chain you want to process using the list append method
 *	described above.  The caller is responsible for determining whether
 *	or not any long link tuples have been encountered, as well as
 *	vaildating any long link targets to be sure that they contain a valid
 *	CIS structure.
 */
int
cis_list_create(cistpl_callout_t *cistpl_callout, volatile cisptr_t *cisptr,
			cistpl_t **cistplbase)
{
	cistpl_t *cp, *tp = NULL;
	cisdata_t tl, td, *dp;
	int done = 0, tpcnt = 0, err;

	/*
	 * If we were passed a non-NULL list base, that means that we should
	 *	parse the CIS and add any tuples we find to the end of the list
	 *	we were handed a pointer to.
	 */
	if (*cistplbase) {
		tp = *cistplbase;
	} else {
		/*
		 * since this is the first time, check to see if one
		 * of the legitimate first tuples.
		 */
		if ((td = GET_CIS_DATA(cisptr)) != (cisdata_t)CISTPL_DEVICE &&
		    td != CISTPL_DEVICE_A) {
			return (0);
		}
	}

	/*
	 * The main tuple processing loop.  We'll exit this loop when either
	 *	a tuple's link field is CISTPL_END or we've seen a tuple type
	 *	field of CISTPL_END.
	 *
	 * Note that we also silently throw away CISTPL_NULL tuples, and don't
	 *	include them in the tuple count that we return.
	 */
	while (!done && ((td = GET_CIS_DATA(cisptr)) !=
						(cisdata_t)CISTPL_END)) {
		/*
		 * Ignore CISTPL_NULL tuples
		 */
		if (td != (cisdata_t)CISTPL_NULL) {
			/*
			 * point to tuple link field and get the link value
			 */
			if (!NEXT_CIS_ADDR(cisptr))
			    return (BAD_CIS_ADDR);
			tl = GET_CIS_DATA(cisptr);
		/*
		 * This is an ugly PCMCIA hack - ugh! since the standard allows
		 *	a link byte of CISTPL_END to signify that this is the
		 *	last tuple.  The problem is that this tuple might
		 *	actually contain useful information, but we don't know
		 *	the size of it.
		 * We do know that it can't be more than CIS_MAX_TUPLE_DATA_LEN
		 *	bytes in length, however.  So, we pretend that the link
		 *	byte is CIS_MAX_TUPLE_DATA_LEN and also set a flag so
		 *	that when we're done processing this tuple, we will
		 *	break out of the while loop.
		 */
			if (tl == (cisdata_t)CISTPL_END) {
				tl = CIS_MAX_TUPLE_DATA_LEN;
				done = 1;
			}
		/*
		 * point to first byte of tuple data, allocate a new list
		 *	element and diddle with the list base and list
		 *	control pointers
		 */
			if (!NEXT_CIS_ADDR(cisptr))
			    return (BAD_CIS_ADDR);
			cp = (cistpl_t *)CIS_MEM_ALLOC(sizeof (cistpl_t));
			cp->next = NULL;
			/*
			 * if we're not the first in the list, point to our
			 *	next
			 */
			if (tp)
				tp->next = cp;
			/*
			 * will be NULL if we're the first element of the
			 *	list
			 */
			cp->prev = tp;
			tp = cp;
			/*
			 * if this is the first element, save it's address
			 */
			if (!*cistplbase)
				*cistplbase = tp;
			tp->type = td;
			tp->len = tl;

			/*
			 * Save the address in CIS space that this tuple
			 *	begins at, as well as set a flag to indicate
			 *	whether the tuple is in AM or CM space.
			 */
			STORE_CIS_ADDR(tp, cisptr);

			/*
			 * If this tuple has tuple data, we might need to
			 *	copy it.
			 * Note that the tuple data pointer (tp->data) will
			 *	be set to NULL for a tuple with no data.
			 */
#ifdef	XXX
			if (tl) {
#endif
			/*
			 * Read the data in the tuple and store it
			 *	away locally if we're allowed to. If
			 *	the CISTPLF_COPYOK flag is set, it means
			 *	that it's OK to touch the data portion
			 *	of the tuple.
			 *
			 * We need to make this check since some
			 *	tuples might contain active registers
			 *	that can alter the device state if they
			 *	are read before the card is correctly
			 *	initialized.  What a stupid thing to
			 *	allow in a standard, BTW.
			 *
			 * We first give the tuple handler a chance
			 *	to set any tuple flags that it wants
			 *	to, then we (optionally) do the data
			 *	copy, and give the tuple handler another
			 *	shot at the tuple.
			 *
			 * ref. PC Card Standard Release 2.01 in the
			 *	Card Metaformat section, section 5.2.6,
			 *	page 5-12.
			 */
			if ((err = cis_tuple_handler(cistpl_callout, tp,
						HANDTPL_SET_FLAGS, NULL, 0)) &
								HANDTPL_ERROR)
			    return (err);

			if (tl > (unsigned)0) {

				/*
				 * if we're supposed to make a local copy of
				 *	the tuple data, allocate space for it,
				 *	otherwise just record the PC card
				 *	starting address of this tuple.  The
				 *	address was saved by the STORE_CIS_ADDR
				 *	macro.
				 */
				if (tp->flags & CISTPLF_COPYOK) {
				    tp->data = (cisdata_t *)CIS_MEM_ALLOC(tl);
				    dp = tp->data;
				} else {
				    tp->data = GET_CIS_ADDR(tp, cisptr);
				}

				while (tl--) {
				    if (tp->flags & CISTPLF_COPYOK)
					*dp++ = GET_CIS_DATA(cisptr);
				    if (!NEXT_CIS_ADDR(cisptr))
					return (BAD_CIS_ADDR);
				}

				/*
				 * If we made a local copy of the tuple data,
				 *	then clear the AM and CM flags; if the
				 *	tuple data is still on the card, then
				 *	leave the flags alone
				 */
				if (tp->flags & CISTPLF_COPYOK) {
				    tp->flags &= ~(
							CISTPLF_AM_SPACE |
							CISTPLF_CM_SPACE);
				    tp->flags |= CISTPLF_LM_SPACE;
				}

			/*
			 * This is a tuple with no data in it's body, so
			 *	we just set the data pointer to NULL.
			 */
			} else {

			    tp->data = NULL;
			    /* tp->flags |= XXX; XXX - what to set here? */

			} /* if (tl > 0) */

			/*
			 * The main idea behind this call is to give
			 *	the handler a chance to validate the
			 *	tuple.
			 */
			if ((err = cis_tuple_handler(cistpl_callout, tp,
						HANDTPL_COPY_DONE, NULL, 0)) &
								HANDTPL_ERROR)
			    return (err);

#ifdef	XXX
			} else { /* if (tl) */
			    tp->data = NULL;
			}
#endif
			tpcnt++;
		} else { /* if (td == CISTPL_NULL) */
			/*
			 * If we're a CISTPL_NULL we need to skip to
			 *	the beginning of the next tuple.
			 */
			if (!NEXT_CIS_ADDR(cisptr))
			    return (BAD_CIS_ADDR);
		}
	} /* while (!done && !CISTPL_END) */

	return (tpcnt);
}

/*
 * cis_list_destroy - function to destroy a linked tuple list
 *
 *	cistpl_t **cistplbase - pointer to a pointer to the base of a
 *				local linked CIS list to destroy; the
 *				data that this pointer points to is
 *				also destroyed
 *
 * Once this function returns, cistplbase is set to NULL.
 */
int
cis_list_destroy(cistpl_t **cistplbase)
{
	cistpl_t *cp, *tp;
	int tpcnt = 0;

	/*
	 * First, check to see if we've got a
	 *	non-NULL list pointer.
	 */
	if ((tp = *cistplbase) == NULL)
	    return (0);

	while (tp) {
	/*
	 * Free any data that may be allocated
	 */
	    if ((tp->flags & CISTPLF_COPYOK) &&
			(tp->flags & CISTPLF_LM_SPACE) &&
						(tp->data))
		CIS_MEM_FREE((caddr_t)tp->data);

	    cp = tp->next;

	/*
	 * Free this tuple
	 */
	    CIS_MEM_FREE((caddr_t)tp);

	    tp = cp;

	    tpcnt++;
	}

	/*
	 * Now clear the pointer to the non-existant
	 *	linked list.
	 */
	*cistplbase = NULL;

	return (tpcnt);

}

/*
 * cis_get_ltuple - function to walk local linked CIS list and return
 *			a tuple based on various criteria
 *
 *	cistpl_t *tp - pointer to any valid tuple in the list
 *	cisdata_t type - type of tuple to search for
 *	int flags - type of action to perform (each is mutually exclusive)
 *		GET_FIRST_LTUPLEF, GET_LAST_LTUPLEF:
 *		    Returns the {first|last} tuple in the list.
 *		FIND_LTUPLE_FWDF, FIND_LTUPLE_BACKF:
 *		FIND_NEXT_LTUPLEF, FIND_PREV_LTUPLEF:
 *		    Returns the first tuple that matches the passed tuple type,
 *			searching the list {forward|backward}.
 *		GET_NEXT_LTUPLEF, GET_PREV_LTUPLEF:
 *		    Returns the {next|previous} tuple in the list.
 *
 * Note on searching:
 *	When using the FIND_LTUPLE_FWDF and FIND_LTUPLE_BACKF flags,
 *	the search starts at the passed tuple.  Continually calling this
 *	function with a tuple that is the same type as the passed type will
 *	continually return the same tuple.
 *
 *	When using the FIND_NEXT_LTUPLEF and FIND_PREV_LTUPLEF flags,
 *	the search starts at the {next|previous} tuple from the passed tuple.
 *
 * returns:
 *	cistpl_t * - pointer to tuple in list
 *	NULL - if error while processing list or tuple not found
 */
cistpl_t *
cis_get_ltuple(cistpl_t *tp, cisdata_t type, int flags)
{
	cistpl_t *ltp = NULL;

	if (!tp)
		return (NULL);

	switch (flags) {
	case GET_FIRST_LTUPLEF:	/* return first tuple in list */
		do {
			ltp = tp;
		} while ((tp = GET_PREV_LTUPLE(tp)) != NULL);
		break;
	case GET_LAST_LTUPLEF:	/* return last tuple in list */
		do {
			ltp = tp;
		} while ((tp = GET_NEXT_LTUPLE(tp)) != NULL);
		break;
	case FIND_LTUPLE_FWDF:	/* find tuple, fwd search from tp */
		do {
			if (tp->type == type)
				return (tp);	/* note return here */
		} while ((tp = GET_NEXT_LTUPLE(tp)) != NULL);
		break;
	case FIND_LTUPLE_BACKF:	/* find tuple, backward search from tp */
		do {
			if (tp->type == type)
				return (tp);	/* note return here */
		} while ((tp = GET_PREV_LTUPLE(tp)) != NULL);
		break;
	case FIND_NEXT_LTUPLEF:	/* find tuple, fwd search from tp+1 */
		while ((tp = GET_NEXT_LTUPLE(tp)) != NULL)
			if (tp->type == type)
				return (tp);	/* note return here */
		break;
	case FIND_PREV_LTUPLEF:	/* find tuple, backward search from tp-1 */
		while ((tp = GET_PREV_LTUPLE(tp)) != NULL)
			if (tp->type == type)
				return (tp);	/* note return here */
		break;
	case GET_NEXT_LTUPLEF:	/* return next tuple in list */
		ltp = GET_NEXT_LTUPLE(tp);
		break;
	case GET_PREV_LTUPLEF:	/* return prev tuple in list */
		ltp = GET_PREV_LTUPLE(tp);
		break;
	default:	/* ltp is already NULL in the initialization */
		break;
	}

	return (ltp);
}

/*
 * cis_convert_devspeed - converts a devspeed value to nS or nS
 *				to a devspeed entry
 */
int
cis_convert_devspeed(convert_speed_t *cs)
{
	cistpl_devspeed_struct_t *cd = &cistpl_devspeed_struct;
	unsigned exponent = 0, mantissa;

	/*
	 * Convert nS to a devspeed value
	 */
	if (cs->Attributes & CONVERT_NS_TO_DEVSPEED) {
	    unsigned tnS, tmanv = 0, i;

	/*
	 * There is no device speed code for 0nS
	 */
	    if (!cs->nS)
		return (CS_BAD_SPEED);

	/*
	 * Handle any nS value below 10nS specially since the code
	 *	below only works for nS values >= 10.  Now, why anyone
	 *	would want to specify a nS value less than 10 is
	 *	certainly questionable, but it is allowed by the spec.
	 */
	    if (cs->nS < 10) {
		tmanv = cs->nS * 10;
		mantissa = CISTPL_DEVSPEED_MAX_MAN;
	    }

	    /* find the exponent */
	    for (i = 0; i < CISTPL_DEVSPEED_MAX_EXP; i++) {
		if ((!(tnS = ((cs->nS)/10))) ||
				(mantissa == CISTPL_DEVSPEED_MAX_MAN)) {
		    /* find the mantissa */
		    for (mantissa = 0; mantissa < CISTPL_DEVSPEED_MAX_MAN;
								mantissa++) {
			if (cd->mantissa[mantissa] == tmanv) {
			    cs->devspeed = ((((mantissa<<3) |
				(exponent & (CISTPL_DEVSPEED_MAX_EXP - 1)))));
			    return (CS_SUCCESS);
			}
		    } /* for (mantissa<CISTPL_DEVSPEED_MAX_MAN) */
		} else {
		    exponent = i + 1;
		    tmanv = cs->nS;
		    cs->nS = tnS;
		} /* if (!tnS) */
	    } /* for (i<CISTPL_DEVSPEED_MAX_EXP) */
	/*
	 * Convert a devspeed value to nS
	 */
	} else if (cs->Attributes & CONVERT_DEVSPEED_TO_NS) {
	    exponent = (cs->devspeed & (CISTPL_DEVSPEED_MAX_TBL - 1));
	    if ((mantissa = (((cs->devspeed)>>3) &
				(CISTPL_DEVSPEED_MAX_MAN - 1))) == NULL) {
		if ((cs->nS = cd->table[exponent]) == NULL)
		    return (CS_BAD_SPEED);
		return (CS_SUCCESS);
	    } else {
		if ((cs->nS = ((cd->mantissa[mantissa] *
					cd->exponent[exponent]) / 10)) == NULL)
		    return (CS_BAD_SPEED);
		return (CS_SUCCESS);
	    }
	} else {
	    return (CS_BAD_ATTRIBUTE);
	}

	return (CS_BAD_SPEED);
}

/*
 * This array is for the cis_convert_devsize function.
 */
static u_long cistpl_device_size[8] =
	{ 512, 2*1024, 8*1024, 32*1024, 128*1024, 512*1024, 2*1024*1024, 0 };

/*
 * cis_convert_devsize - converts a devsize value to a size in bytes value
 *				or a size in bytes value to a devsize value
 */
int
cis_convert_devsize(convert_size_t *cs)
{
	int i;

	if (cs->Attributes & CONVERT_BYTES_TO_DEVSIZE) {
	    if ((cs->bytes < cistpl_device_size[0]) ||
				(cs->bytes > (cistpl_device_size[6] * 32)))
	    return (CS_BAD_SIZE);

	    for (i = 6; i >= 0; i--)
		if (cs->bytes >= cistpl_device_size[i])
		    break;

	    cs->devsize = ((((cs->bytes/cistpl_device_size[i]) - 1) << 3) |
								(i & 7));
	}

	if (cs->Attributes & CONVERT_DEVSIZE_TO_BYTES) {
	    if ((cs->devsize & 7) == 7)
		return (CS_BAD_SIZE);
	    cs->bytes =
		cistpl_device_size[cs->devsize & 7] * ((cs->devsize >> 3) + 1);
	}

	return (CS_SUCCESS);
}
