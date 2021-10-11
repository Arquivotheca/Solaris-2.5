/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#ifndef _NAMELIST_HH
#define	_NAMELIST_HH

#pragma ident	"@(#)NameList.hh	1.3	94/08/03 SMI"

#include <xfn/FN_string.hh>
#include "List.hh"

class NameListItem : public ListItem {
 public:
	FN_string name;

	NameListItem(const FN_string&);
	NameListItem(const unsigned char*);
	~NameListItem();
	ListItem* copy();
};

#endif // _NAMELIST_HH
