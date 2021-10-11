/*
 * Copyright (c) 1993 - 1994 by Sun Microsystems, Inc.
 */

#ifndef _XFN_FN_CTX_HH
#define	_XFN_FN_CTX_HH

#pragma ident	"@(#)FN_ctx.hh	1.4	94/11/20 SMI"

#include <xfn/FN_ctx.h>
#include <xfn/FN_ref.hh>
#include <xfn/FN_composite_name.hh>
#include <xfn/FN_status.hh>
#include <xfn/FN_attrset.hh>
#include <xfn/FN_attrmodlist.hh>

class FN_namelist {
public:
    virtual ~FN_namelist();
    virtual FN_string* next(FN_status&) = 0;
	
};

class FN_bindinglist {
public:
    virtual ~FN_bindinglist();
    virtual FN_string* next(FN_ref** ref, FN_status&) = 0;
};


class FN_valuelist {
public:
    virtual ~FN_valuelist();
    virtual FN_attrvalue *next(FN_identifier **,
	FN_status&) = 0;
};

class FN_multigetlist {
public:
    virtual ~FN_multigetlist();
    virtual FN_attribute *next(FN_status&) = 0;
};

class FN_ctx {
 public:
	virtual ~FN_ctx();

	// construct handle to initial context
	static FN_ctx* from_initial(FN_status&);

	// construct context from a reference
	static FN_ctx* from_ref(const FN_ref&, FN_status&);

	// get reference for this context
	virtual FN_ref* get_ref(FN_status&) const = 0;

	// look up the binding of a name
	virtual FN_ref* lookup(const FN_composite_name&, FN_status&) = 0;

	// list all the (atomic) names bound in the named context
	virtual FN_namelist* list_names(const FN_composite_name&,
					FN_status&) = 0;

	// list all the bindings in the named context
	virtual FN_bindinglist* list_bindings(const FN_composite_name&,
					      FN_status&) = 0;

	// bind a name to a reference
	virtual int bind(const FN_composite_name&, 
			 const FN_ref&,
			 unsigned int exclusive, 
			 FN_status&) = 0;

	// unbind a name
	virtual int unbind(const FN_composite_name&, FN_status&) = 0;

	virtual int rename(const FN_composite_name &oldname,
			   const FN_composite_name &newname,
			   unsigned int exclusive,
			   FN_status &status) = 0;

	// create a subcontext
	virtual FN_ref* create_subcontext(const FN_composite_name&,
					   FN_status&) = 0;

	// destroy a subcontext
	virtual int destroy_subcontext(const FN_composite_name&, FN_status&) = 0;

	virtual FN_ref *lookup_link(const FN_composite_name &name,
			FN_status &status) = 0;

	// get the syntax attributes of the named context
	virtual FN_attrset* get_syntax_attrs(const FN_composite_name&, 
					      FN_status&) = 0;


	// Attribute operations

	// To obtain a single attribute
	virtual FN_attribute *attr_get(const FN_composite_name&,
				       const FN_identifier&,
				       FN_status&) = 0;

	// To modifiy a single attribute
	virtual int attr_modify(const FN_composite_name&,
			        unsigned int,
				const FN_attribute&,
				FN_status&) = 0;

	// Obtain multiple attribute values
	virtual FN_valuelist *attr_get_values(const FN_composite_name&,
					      const FN_identifier&,
					      FN_status&) = 0;

	// Obtain the set of attributes 
	virtual FN_attrset *attr_get_ids(const FN_composite_name&,
					 FN_status&) = 0;

	// Attribute operations for multiple attribute list
	virtual FN_multigetlist *attr_multi_get(const FN_composite_name&,
						const FN_attrset *,
						FN_status&) = 0;

	// Operations to modify multiple attributes
	virtual int attr_multi_modify(const FN_composite_name&,
				      const FN_attrmodlist&,
				      FN_attrmodlist**,
				      FN_status&) = 0;

	// other attribute operations to follow
};

#endif // _XFN_FN_CTX_HH
