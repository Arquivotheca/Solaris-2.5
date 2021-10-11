/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#ifndef _NAMESET_HH
#define	_NAMESET_HH

#pragma ident	"@(#)NameSet.hh	1.4	94/08/04 SMI"

#include <xfn/FN_string.hh>
#include "Set.hh"

class NameSetItem : public SetItem {
 public:
	FN_string name;

	NameSetItem(const FN_string &);
	~NameSetItem();
	SetItem *copy();
	int key_match(const void *key, int case_sense);
	int item_match(SetItem &, int case_sense);
	unsigned long key_hash();
	unsigned long key_hash(const void *key);
};

#endif // _NAMESET_HH
