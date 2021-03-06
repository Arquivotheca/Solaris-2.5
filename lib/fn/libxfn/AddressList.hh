/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#ifndef _ADDRESSLIST_H
#define	_ADDRESSLIST_H

#pragma ident	"@(#)AddressList.hh	1.2	94/08/02 SMI"


#include <xfn/FN_ref_addr.hh>

#include "List.hh"

class AddressListItem : public ListItem {
 public:
	FN_ref_addr addr;

	AddressListItem(const FN_ref_addr& a);
	~AddressListItem();
	ListItem* copy();
};

#endif // _ADDRESSLIST_H
