/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _CIS_H
#define	_CIS_H

#pragma ident	"@(#)cis.h	1.15	95/07/17 SMI"

/*
 * This is the Card Services Card Information Structure (CIS) interpreter
 *	header file.  CIS information in this file is based on the
 *	Release 2.01 PCMCIA standard.
 */

/* #define	CIS_DEBUG */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The CIS interpreter has a single entry point with a bunch of function
 *	id numbers.
 */
#define	CISP_CIS_SETUP		0x01	/* setup CS address in CIS */
#define	CISP_CIS_LIST_CREATE	0x02	/* create the CIS linked list */
#define	CISP_CIS_LIST_DESTROY	0x03	/* destroy the CIS linked list */
#define	CISP_CIS_GET_LTUPLE	0x04	/* get a tuple */
#define	CISP_CIS_PARSE_TUPLE	0x05	/* parse a tuple */
#define	CISP_CIS_CONV_DEVSPEED	0x06	/* convert devspeed to nS and back */
#define	CISP_CIS_CONV_DEVSIZE	0x07	/* convert device size */

/*
 * define a few macros to make function pointers look like function calls
 */
#define	CIS_PARSER		(*cis_parser)
#define	CIS_CARD_SERVICES	(*cis_card_services)

/*
 * define the tuples that we recognize
 *
 * Layer 1 - Basic Compatability TUples
 */
#define	CISTPL_NULL		0x000	/* null tuple - ignore */
#define	CISTPL_DEVICE		0x001	/* device information */
#define	CISTPL_CHECKSUM		0x010	/* checksum control */
#define	CISTPL_LONGLINK_A	0x011	/* long-link to AM */
#define	CISTPL_LONGLINK_C	0x012	/* long-link to CM */
#define	CISTPL_LINKTARGET	0x013	/* link-target control */
#define	CISTPL_NO_LINK		0x014	/* no-link control */
#define	CISTPL_VERS_1		0x015	/* level 1 version information */
#define	CISTPL_ALTSTR		0x016	/* alternate language string */
#define	CISTPL_DEVICE_A		0x017	/* AM device information */
#define	CISTPL_JEDEC_C		0x018	/* JEDEC programming info for CM */
#define	CISTPL_JEDEC_A		0x019	/* JEDEC programming info for AM */
#define	CISTPL_CONFIG		0x01a	/* configuration */
#define	CISTPL_CFTABLE_ENTRY	0x01b	/* configuration-table-entry */
#define	CISTPL_DEVICE_OC	0x01c	/* other op conditions CM device info */
#define	CISTPL_DEVICE_OA	0x01d	/* other op conditions AM device info */

/*
 * Layer 1 - Extended Tuples
 */
#define	CISTPL_MANFID		0x20 	/* manufacturer identification */
#define	CISTPL_FUNCID		0x21	/* function identification */
#define	CISTPL_FUNCE		0x22	/* function extension */
#define	CISTPL_SWIL		0x23	/* software interleave */

/*
 * Layer 2 - Data Recording Format Tuples
 */
#define	CISTPL_VERS_2		0x040	/* level 2 version information */
#define	CISTPL_FORMAT		0x041	/* format type */
#define	CISTPL_GEOMETRY		0x042	/* geometry */
#define	CISTPL_BYTEORDER	0x043	/* byte order */
#define	CISTPL_DATE		0x044	/* card initialization date */
#define	CISTPL_BATTERY		0x045	/* battery replacement date */

/*
 * Layer 3 - Data Organization Tuples
 */
#define	CISTPL_ORG		0x046	/* organization */

/*
 * Layer 4 - System Specific Standard Tuples
 */
#define	CISTPL_END		0x0ff	/* end-of-list tuple */

/*
 * The GetFirstTuple and GetNextTuple Card Services function calls use
 *	the DesiredTuple member of the tuple_t structure to determine
 *	while tuple type to return; since the CIS parser doesn't ever
 *	return CISTPL_END tuples, we can never ask for those tuples,
 *	so we overload this tuple code to mean that we want the
 *	first (or next) tuple in the chain.
 * XXX - If we ever do return CISTPL_END tuples, we'll have to
 *	re-think this.
 */
#define	RETURN_FIRST_TUPLE	0x0ff	/* return first/next tuple */
#define	RETURN_NEXT_TUPLE	0x0ff	/* return first/next tuple */

/*
 * types for data in CIS and pointers into PC card's CIS space
 *
 * The "last" member is used by the NEXT_CIS_ADDR macro so that
 *	we don't run past the end of the mapped CIS address space.
 */
typedef uchar_t cisdata_t;

typedef struct cisptr_t {
    volatile caddr_t	base;	/* base virtual address of CIS space */
    volatile caddr_t	last;	/* last virtual address mapped in */
    int			offset;	/* byte offset into CIS space */
	/* see flag definitions for cistpl_t structure */
    int			flags;
} cisptr_t;

/*
 * This is the maximum length that the data portion of a tuple can be.
 *	We have to use this since the brain-damaged 2.01 PCMCIA spec
 *	specifies that you can end a CIS chain by putting a CISTPL_END
 *	in the link field of the last VALID tuple.
 */
#define	CIS_MAX_TUPLE_DATA_LEN	254

/*
 * Macros to manipulate addresses and data in various CIS spaces
 *
 * NEXT_CIS_ADDR(cisptr_t *) increments the offset to point to the
 *	next data element in the CIS, based on what space the CIS
 *	we are reading resides in.  If the resulting address would
 *	be past the end of the mapped-in area, we return NULL,
 *	otherwise the new address is returned.
 *
 * GET_CIS_DATA(ptr) returns the data byte at the current CIS location.
 *
 * STORE_CIS_ADDR(tp,ptr) saves the current CIS address and space type
 *	of the beginning of the tuple into the passed linked list element.
 *	Note that this macro will decrement the CIS address by two
 *	elements prior to storing it to the linked list element to point
 *	to the tuple type byte.
 *
 * GET_CIS_ADDR(tp,ptr) returns the virtual address that was saved by a
 *	call to STORE_CIS_ADDR.
 *
 * BAD_CIS_ADDR is a flag that should be returned by callers of NEXT_CIS_ADDR
 *	if that macro returns NULL.  Note that this flag shares the same bit
 *	field definitions as the tuple handler flags defined in cis_handlers.h
 *	so check that file if you make any changes to these flags.
 * XXX - not the best distribution of flags, I'm afraid
 */
#define	NEXT_CIS_ADDR(ptr)	\
			(((ptr->flags&CISTPLF_AM_SPACE)?(ptr->offset += 2): \
				(ptr->offset++)),	\
			((((unsigned)ptr->offset+(unsigned)ptr->base) > \
				(unsigned)ptr->last)?(NULL):(ptr->offset)))
#define	GET_CIS_DATA(ptr)	(*((ptr->base)+(ptr->offset))&0x0ff)
#define	STORE_CIS_ADDR(tp, ptr)	\
	((tp->offset = ((ptr->flags&CISTPLF_AM_SPACE)?(ptr->offset-4): \
			(ptr->offset-2))), \
			(tp->space = ptr->flags))
#define	GET_CIS_ADDR(tp, ptr)	((cisdata_t *)((ptr)->base + \
					(unsigned)(tp)->offset))
#define	BAD_CIS_ADDR	0x080000000 /* read past end of mapped CIS error */

/*
 * CIS_MEM_ALLOC(len) is used to allocate memory for our local linked
 *	CIS list; we use a macro so that the same code can be used in
 *	the kernel as well as in user space
 *
 * CIS_MEM_FREE(ptr) - same comment as CIS_MEM_ALLOC
 */
#if !defined(_KERNEL)
#ifdef	CISMALLOC_DEBUG
#define	CIS_MEM_ALLOC(len)		cis_malloc((int)len)
#define	CIS_MEM_FREE(ptr)		cis_free(ptr)
#else
#define	CIS_MEM_ALLOC(len)		malloc((int)len)
#define	CIS_MEM_FREE(ptr)		free(ptr)
#endif	CISMALLOC_DEBUG
#else
#define	CIS_MEM_ALLOC(len)		cis_malloc((int)len)
#define	CIS_MEM_FREE(ptr)		cis_free(ptr)
#endif

typedef struct cis_u_malloc_tag_t {
	caddr_t	addr;
	int	len;
} cis_u_malloc_tag_t;

/*
 * We keep the tuples in a locally-maintained linked list.  This allows
 *	us to return the tuple information at any time to a client for
 *	those cards that make their CIS inaccessible once the card is
 *	configured.
 */
typedef struct cistpl_t {
	cisdata_t	type;	/* type of tuple */
	cisdata_t	len;	/* length of tuple data */
	cisdata_t	*data;	/* data in tuple */
	union {
		cisdata_t	*byte;	/* read pointer for GET_BYTE macros */
		u_short		*sword;
	}		read;
	int		flags;	/* misc flags */
	int		offset;	/* CIS address offset for tuple data area */
	int		space;	/* CIS space this tuple is located in */
	struct cistpl_t	*prev;	/* back pointer */
	struct cistpl_t	*next;	/* forward pointer */
} cistpl_t;

/*
 * Flags that are used in the cistpl_t and cisptr_t linked lists
 */
#define	CISTPLF_NOERROR		0x000000000 /* no error return from handler */
#define	CISTPLF_UNKNOWN		0x000000001 /* unknown tuple */
#define	CISTPLF_REGS		0x000000002 /* tuple contains registers */
#define	CISTPLF_COPYOK		0x000000004 /* OK to copy tuple data */
#define	CISTPLF_VALID		0x000000008 /* tuple is valid */
#define	CISTPLF_LINK_INVALID	0x001000000 /* tuple link is invalid */
#define	CISTPLF_PARAMS_INVALID	0x002000000 /* tuple body is invalid */
#define	CISTPLF_AM_SPACE	0x010000000 /* this tuple is in AM space */
#define	CISTPLF_CM_SPACE	0x020000000 /* this tuple is in CM space */
#define	CISTPLF_LM_SPACE	0x040000000 /* this tuple is in local memory */
#define	CISTPLF_MEM_ERR		0x080000000 /* GET_BYTE macros memory error */

/*
 * Macros to walk the local linked CIS list.
 *
 * These macros can take any valid local list tuple pointer.  They return
 *	another tuple pointer or NULL if they fail.
 */
#define	GET_NEXT_LTUPLE(tp)		((tp->next)?tp->next:NULL) /*  */
#define	GET_PREV_LTUPLE(tp)		((tp->prev)?tp->prev:NULL)
#define	GET_FIRST_LTUPLE(tp)		CIS_PARSER(CISP_CIS_GET_LTUPLE, tp, \
							NULL, GET_FIRST_LTUPLEF)
#define	GET_LAST_LTUPLE(tp)		CIS_PARSER(CISP_CIS_GET_LTUPLE, tp, \
							NULL, GET_LAST_LTUPLEF)
#define	FIND_LTUPLE_FWD(tp, tu)		CIS_PARSER(CISP_CIS_GET_LTUPLE, tp, \
							tu, FIND_LTUPLE_FWDF)
#define	FIND_LTUPLE_BACK(tp, tu)	CIS_PARSER(CISP_CIS_GET_LTUPLE, tp, \
							tu, FIND_LTUPLE_BACKF)
#define	FIND_NEXT_LTUPLE(tp, tu)	CIS_PARSER(CISP_CIS_GET_LTUPLE, tp, \
							tu, FIND_NEXT_LTUPLEF)
#define	FIND_PREV_LTUPLE(tp, tu)	CIS_PARSER(CISP_CIS_GET_LTUPLE, tp, \
							tu, FIND_PREV_LTUPLEF)
#define	FIND_FIRST_LTUPLE(tp, tu)	FIND_LTUPLE_FWD(GET_FIRST_LTUPLE(tp), \
							tu)

/*
 * Flags used internally by the above macros on calls to cis_get_tuple.
 *
 * Each of these flags are mutually exclusive, i.e. cis_get_tuple can only
 *	do one operation per call.
 */
#define	GET_FIRST_LTUPLEF	0x000000001 /* return first tuple in list */
#define	GET_LAST_LTUPLEF	0x000000002 /* return last tuple in list */
#define	FIND_LTUPLE_FWDF	0x000000003 /* find tuple, fwd search from tp */
#define	FIND_LTUPLE_BACKF	0x000000004 /* find tuple, backward from tp */
#define	FIND_NEXT_LTUPLEF	0x000000005 /* find tuple, fwd from tp+1 */
#define	FIND_PREV_LTUPLEF	0x000000006 /* find tuple, backward from tp-1 */
#define	GET_NEXT_LTUPLEF	0x000000007 /* return next tuple in list */
#define	GET_PREV_LTUPLEF	0x000000008 /* return prev tuple in list */

/*
 * macros for getting various data types out of a tuple
 * Note that due to the modem tuple using a few big-endian values,
 * we have to support both big and little endian macros
 *
 * Common Memory Specific macros - these will also work for tuples in
 *	local memory
 */
#define	GET_CM_BYTE(tp)	(((unsigned)(tp)->len >= \
				((unsigned)(tp)->read.byte - \
					(unsigned)(tp)->data)) ? \
			 *(tp)->read.byte++ : (tp->flags |= CISTPLF_MEM_ERR))
#define	GET_CM_SHORT(tp)	(((unsigned)(tp)->len >= \
					(((unsigned)(tp)->read.byte - \
						(unsigned)(tp)->data) + 1)) ? \
				(*(tp)->read.byte++ | \
					(*(tp)->read.byte++ << 8)) : \
					(tp->flags |= CISTPLF_MEM_ERR))
#define	GET_CM_BE_SHORT(tp) (((unsigned)(tp)->len >= \
				(((unsigned)(tp)->read.byte - \
					(unsigned)(tp)->data) + 1)) ? \
				((*(tp)->read.byte++ << 8) | \
					*(tp)->read.byte++) : \
				(tp->flags |= CISTPLF_MEM_ERR))
#define	GET_CM_INT24(tp)	(GET_CM_SHORT(tp) | (GET_CM_BYTE(tp)<<16))
#define	GET_CM_LONG(tp)	(GET_CM_SHORT(tp) | (GET_CM_SHORT(tp) << 16))
#define	GET_CM_LEN(tp)	((unsigned)tp->len - \
				((unsigned)(tp)->read.byte - \
				(unsigned)(tp)->data))

/* Attribute Memory Specific macros */
#define	GET_AM_BYTE(tp)	(((unsigned)(tp)->len >= \
				(((unsigned)(tp)->read.byte - \
					(unsigned)(tp)->data))>>1) ? \
			 *(cisdata_t *)(tp)->read.sword++ : \
				(tp->flags |= CISTPLF_MEM_ERR))
#define	GET_AM_SHORT(tp) (GET_AM_BYTE(tp) | (GET_AM_BYTE(tp) << 8))
#define	GET_AM_BE_SHORT(tp)  ((GET_AM_BYTE(tp) << 8) | GET_AM_BYTE(tp))
#define	GET_AM_INT24(tp) (GET_AM_SHORT(tp) | (GET_AM_BYTE(tp)<<16))
#define	GET_AM_LONG(tp)  (GET_AM_SHORT(tp) | (GET_AM_SHORT(tp) << 16))
#define	GET_AM_LEN(tp)	((unsigned)tp->len - (((unsigned)(tp)->read.byte - \
				(unsigned)(tp)->data) >> 1))

/* generic macros */
#define	RESET_TP(tp)	(tp)->read.byte = (tp)->data
#define	LOOK_BYTE(tp)	*(tp)->read.byte
#define	GET_BYTE_ADDR(tp) (tp)->read.byte

#define	GET_BYTE(tp)	(((tp)->flags & CISTPLF_AM_SPACE) ? \
				GET_AM_BYTE(tp) : GET_CM_BYTE(tp))
#define	GET_SHORT(tp) 	(((tp)->flags & CISTPLF_AM_SPACE) ? \
				GET_AM_SHORT(tp) : GET_CM_SHORT(tp))
#define	GET_BE_SHORT(tp) (((tp)->flags & CISTPLF_AM_SPACE) ? \
				GET_AM_BE_SHORT(tp) : GET_CM_BE_SHORT(tp))
#define	GET_INT24(tp)	(((tp)->flags & CISTPLF_AM_SPACE) ? \
				GET_AM_INT24(tp) : GET_CM_INT24(tp))
#define	GET_LONG(tp)	(((tp)->flags & CISTPLF_AM_SPACE) ? \
				GET_AM_LONG(tp) : GET_CM_LONG(tp))
#define	GET_LEN(tp)	(((tp)->flags & CISTPLF_AM_SPACE) ? \
				GET_AM_LEN(tp) : GET_CM_LEN(tp))

#ifdef	__cplusplus
}
#endif

#endif	/* _CIS_H */
