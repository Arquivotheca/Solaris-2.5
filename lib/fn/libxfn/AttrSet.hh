/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#ifndef _ATTRSET_H
#define	_ATTRSET_H

#pragma ident	"@(#)AttrSet.hh	1.3	94/08/04 SMI"

#include <xfn/FN_string.hh>
#include <xfn/FN_attribute.hh>

#include "Set.hh"

class AttrSetItem : public SetItem {
 public:
	FN_attribute	at;		 // %%% rename

	AttrSetItem(const FN_attribute &);
	~AttrSetItem();
	SetItem *copy();
	int key_match(const void *key, int case_sense);
	int item_match(SetItem &, int case_sense);
	unsigned long key_hash();
	unsigned long key_hash(const void *);
};

#endif // _ATTRSET_H
