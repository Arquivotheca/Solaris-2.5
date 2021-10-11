/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#ifndef _XFN_FN_BINDINGLIST_SVC_HH
#define	_XFN_FN_BINDINGLIST_SVC_HH

#pragma ident "@(#)FN_bindinglist_svc.hh	1.3 94/08/13 SMI"

#include <xfn/xfn.hh>
#include <xfn/FN_bindingset.hh>

class FN_bindinglist_svc : public FN_bindinglist {
	FN_bindingset *bindings;
	void *iter_pos;
	unsigned int iter_status;
public:
	FN_bindinglist_svc(FN_bindingset *);
	~FN_bindinglist_svc();
	FN_string *next(FN_ref **, FN_status &);
};

#endif /* _XFN_FN_BINDINGLIST_SVC_HH */
