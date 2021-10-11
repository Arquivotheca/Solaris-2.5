/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#ifndef _FNSP_CONTEXT_HH
#define	_FNSP_CONTEXT_HH

#pragma ident "@(#)FNSP_Context.hh	1.4 94/09/15 SMI"


#include <xfn/xfn.hh>

class FNSP_Context : public virtual FN_ctx {
protected:
	virtual FN_ref *n_create_subcontext(const FN_string& atomic_name,
	    unsigned context_type,
	    unsigned representation_type,
	    const FN_identifier *ref_type,
	    FN_status& stat) = 0;

	virtual FN_ref *n_create_subcontext_nns(const FN_string& atomic_name,
	    unsigned context_type,
	    unsigned representation_type,
	    const FN_identifier *ref_type,
	    FN_status& stat) = 0;

public:
	static FNSP_Context *from_ref(const FN_ref& ref, FN_status& stat);

	FN_ref *create_fnsp_subcontext(const FN_composite_name& name,
	    unsigned context_type,
	    FN_status&,
	    unsigned representation_type = 0,
	    const FN_identifier *ref_type = 0);
};

#endif // _FNSP_CONTEXT_HH
