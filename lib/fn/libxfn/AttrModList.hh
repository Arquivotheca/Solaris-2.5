/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#ifndef _ATTRMODLIST_H
#define	_ATTRMODLIST_H

#pragma ident	"@(#)AttrModList.hh	1.2	94/08/02 SMI"


#include <xfn/FN_string.hh>
#include <xfn/FN_attribute.hh>

#include "List.hh"

class AttrModListItem : public ListItem {
 public:
	FN_attribute	attribute;	// Attribute
	unsigned int    attr_mod_op;    // operation to be performed

	AttrModListItem(const FN_attribute &,
		   	unsigned int);
	~AttrModListItem();
	ListItem* copy();
};

#endif // _ATTRMODLIST_H
