/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1991 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)sgs.h	1.18	94/08/18 SMI"

/*
 * Global include file for all sgs.
 */
#ifndef	SGS_DOT_H
#define	SGS_DOT_H


/*
 * Software identification.
 */
#define	SGS		""
#define	SGU_PKG		"Software Generation Utilities"
#define	SGU_REL		"(SGU) SunOS/ELF (LK-2.0 (S/I) - versioning)"


#ifndef	_ASM

#include	<stdlib.h>
#include	<libelf.h>
#include	<sys/types.h>

/*
 * Macro to round to next double word boundary.
 */
#define	S_DROUND(x)	(((x) + sizeof (double) - 1) & \
				~(sizeof (double) - 1))

/*
 * General align and round macros.
 */
#define	S_ALIGN(x, a)	((int)(x) & ~(((int)(a) ? (int)(a) : 1) - 1))
#define	S_ROUND(x, a)   ((int)(x) + (((int)(a) ? (int)(a) : 1) - 1) & \
				~(((int)(a) ? (int)(a) : 1) - 1))

/*
 * Bit manipulation macros; generic bit mask and is `v' in the range
 * supportable in `n' bits?
 */
#define	S_MASK(n)	((1 << (n)) -1)
#define	S_INRANGE(v, n)	(((-(1 << (n))) < (v)) && ((v) < (1 << (n))))
#define	S_ABS(x)	(((int)(x) < 0) ? -((int)(x)) : (x))


/*
 * General typedefs.
 */
typedef enum {
	FALSE = 0,
	TRUE = 1
} Boolean;

/*
 * Types of errors (used by eprintf()), together with a generic error return
 * value.
 */
typedef enum {
	ERR_NONE,
	ERR_WARNING,
	ERR_FATAL,
	ERR_ELF
} Error;

#define	S_ERROR		(~(unsigned int)0)

/*
 * LIST_TRAVERSE() is used as the only "argument" of a "for" loop to
 * traverse a linked list. The node pointer `node' is set to each node in
 * turn and the corresponding data pointer is copied to `data'.  The macro
 * is used as in
 * 	for (LIST_TRAVERSE(List * list, Listnode * node, void * data)) {
 *		process(data);
 *	}
 */
#define	LIST_TRAVERSE(L, N, D) \
	(void) (((N) = (L)->head) != NULL && ((D) = (N)->data) != NULL); \
	(N) != NULL; \
	(void) (((N) = (N)->next) != NULL && ((D) = (N)->data) != NULL)

typedef	struct listnode	Listnode;
typedef	struct list	List;

struct	listnode {			/* a node on a linked list */
	void *		data;		/* the data item */
	Listnode *	next;		/* the next element */
};

struct	list {				/* a linked list */
	Listnode *	head;		/* the first element */
	Listnode *	tail;		/* the last element */
};

/*
 * Permission structure.  Used to define access with ld.so.1 link maps.
 * (maintained here as liblddbg needs to know this also).
 */
typedef struct permit {
	unsigned long	p_cnt;		/* No. of p_value entries of which */
	unsigned long	p_value[1];	/* there may be more than one */
} Permit;


/*
 * Data structures (defined in libld.h).
 */
typedef struct ent_desc		Ent_desc;
typedef struct ifl_desc		Ifl_desc;
typedef struct is_desc		Is_desc;
typedef struct ofl_desc		Ofl_desc;
typedef struct os_desc		Os_desc;
typedef	struct rel_cache	Rel_cache;
typedef	struct sdf_desc		Sdf_desc;
typedef	struct sdv_desc		Sdv_desc;
typedef struct sg_desc		Sg_desc;
typedef struct sec_order	Sec_order;
typedef struct sym_desc		Sym_desc;
typedef struct sym_aux		Sym_aux;
typedef struct sym_cache	Sym_cache;
typedef struct sym_names	Sym_names;
typedef struct ver_desc		Ver_desc;
typedef struct ver_index	Ver_index;

/*
 * Data structures defined in machrel.h.
 */
typedef struct rel_desc	Rel_desc;


#endif
#endif
