/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)util.c	1.19	94/12/15 SMI"

/* LINTLIBRARY */

/*
 * Utility functions
 */
#include	<stdio.h>
#include	<string.h>
#include	"_libld.h"


/*
 * Append an item to the specified list, and return a pointer to the list
 * node created.
 */
Listnode *
list_append(List * lst, const void * item)
{
	Listnode *	_lnp;

	if ((_lnp = (Listnode *)malloc(sizeof (Listnode))) == 0)
		return (0);

	_lnp->data = (void *)item;
	_lnp->next = NULL;

	if (lst->head == NULL)
		lst->tail = lst->head = _lnp;
	else {
		lst->tail->next = _lnp;
		lst->tail = lst->tail->next;
	}
	return (_lnp);
}

/*
 * Add an item after the specified listnode, and return a pointer to the list
 * node created.
 */
Listnode *
list_insert(List * lst, const void * item, Listnode * lnp)
{
	Listnode *	_lnp;

	if ((_lnp = (Listnode *)malloc(sizeof (Listnode))) == 0)
		return (0);

	_lnp->data = (void *)item;
	_lnp->next = lnp->next;
	if (_lnp->next == NULL)
		lst->tail = _lnp;
	lnp->next = _lnp;
	return (_lnp);
}

/*
 * Prepend an item to the specified list, and return a pointer to the
 * list node created.
 */
Listnode *
list_prepend(List * lst, const void * item)
{
	Listnode *	_lnp;

	if ((_lnp = (Listnode *)malloc(sizeof (Listnode))) == 0)
		return (0);

	_lnp->data = (void *)item;

	if (lst->head == NULL) {
		_lnp->next = NULL;
		lst->tail = lst->head = _lnp;
	} else {
		_lnp->next = lst->head;
		lst->head = _lnp;
	}
	return (_lnp);
}

/*
 * Find out where to insert the node for reordering.  List of insect structures
 * is traversed and the is_txtndx field of the insect structure is examined
 * and that determines where the new input section should be inserted.
 * All input sections which have a non zero is_txtndx value will be placed
 * in ascending order before sections with zero is_txtndx value.  This
 * implies that any section that does not appear in the map file will be
 * placed at the end of this list as it will have a is_txtndx value of 0.
 * Returns:  NULL if the input section should be inserted at beginning
 * of list else A pointer to the entry AFTER which this new section should
 * be inserted.
 */
Listnode *
list_where(List * lst, Word num)
{
	Listnode *	ln, * pln;	/* Temp list node ptr */
	Is_desc	*	isp;		/* Temp Insect structure */
	Word		n;

	/*
	 * No input sections exist, so add at beginning of list
	 */
	if (lst->head == NULL)
		return (NULL);

	for (ln = lst->head, pln = ln; ln != NULL; pln = ln, ln = ln->next) {
		isp = (Is_desc *)ln->data;
		/*
		 *  This should never happen, but if it should we
		 *  try to do the right thing.  Insert at the
		 *  beginning of list if no other items exist, else
		 *  end of already existing list, prior to this null
		 *  item.
		 */
		if (isp == NULL) {
			if (ln == pln) {
				return (NULL);
			} else {
				return (pln);
			}
		}
		/*
		 *  We have reached end of reorderable items.  All
		 *  following items have is_txtndx values of zero
		 *  So insert at end of reorderable items.
		 */
		if ((n = isp->is_txtndx) > num || n == 0) {
			if (ln == pln) {
				return (NULL);
			} else {
				return (pln);
			}
		}
		/*
		 *  We have reached end of list, so insert
		 *  at the end of this list.
		 */
		if ((n != 0) && (ln->next == NULL))
			return (ln);
	}
	return (NULL);
}

/*
 * Determine if a shared object definition structure already exists and if
 * not create one.  These definitions provide for recording information
 * regarding shared objects that are still to be processed.  Once processed
 * shared objects are maintained on the ofl_sos list.  The information
 * recorded in this structure includes:
 *
 *  o	DT_USED requirements.  In these cases definitions are added during
 *	mapfile processing of `-' entries (see map_dash()).
 *
 *  o	implicit NEEDED entries.  As shared objects are processed from the
 *	command line so any of their dependencies are recorded in these
 *	structures for later processing (see process_dynamic()).
 *
 *  o	version requirements.  Any explicit shared objects that have version
 *	dependencies on other objects have their version requirements recorded.
 *	In these cases definitions are added during mapfile processing of `-'
 *	entries (see map_dash()).  Also, shared objects may have versioning
 *	requirements on their NEEDED entries.  These cases are added during
 *	their version processing (see vers_need_process()).
 *
 *	Note: Both process_dynamic() and vers_need_process() may generate the
 *	initial version definition structure because you can't rely on what
 *	section (.dynamic or .SUNW_version) may be processed first from	any
 *	input file.
 */
Sdf_desc *
sdf_find(const char * name, List * lst)
{
	Listnode *	lnp;
	Sdf_desc *	sdf;

	for (LIST_TRAVERSE(lst, lnp, sdf))
		if (strcmp(name, sdf->sdf_name) == 0)
			return (sdf);

	return (0);
}

Sdf_desc *
sdf_desc(const char * name, List * lst)
{
	Sdf_desc *	sdf;

	if (!(sdf = (Sdf_desc *)calloc(sizeof (Sdf_desc), 1)))
		return ((Sdf_desc *)S_ERROR);

	sdf->sdf_name = name;

	if (list_append(lst, sdf) == 0)
		return ((Sdf_desc *)S_ERROR);
	else
		return (sdf);
}

/*
 * Determine a least common multiplier.  Input sections contain an alignment
 * requirement, which elf_update() uses to insure that the section is aligned
 * correctly off of the base of the elf image.  We must also insure that the
 * sections mapping is congruent with this alignment requirement.  For each
 * input section associated with a loadable segment determine whether the
 * segments alignment must be adjusted to compensate for a sections alignment
 * requirements.
 */
Word
lcm(Word a, Word b)
{
	Word	_r, _a, _b;

	if ((_a = a) == 0)
		return (b);
	if ((_b = b) == 0)
		return (a);

	if (_a > _b)
		_a = b, _b = a;
	while ((_r = _b % _a) != 0)
		_b = _a, _a = _r;
	return ((a / _a) * b);
}
