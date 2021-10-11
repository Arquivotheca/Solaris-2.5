/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#ifndef _ATTRVALSET_H
#define	_ATTRVALSET_H

#pragma ident	"@(#)AttrValSet.hh	1.3	94/08/04 SMI"

#include <xfn/FN_attrvalue.hh>
#include "Set.hh"

class AttrValSetItem : public SetItem {
 public:
	FN_attrvalue attr_val;

	AttrValSetItem(const FN_attrvalue &);
	~AttrValSetItem();
	SetItem *copy();
	int key_match(const void *key, int case_sense);
	int item_match(SetItem &, int case_sense);
	unsigned long key_hash();
	unsigned long key_hash(const void *);
};

#endif // _ATTRVALSET_H
