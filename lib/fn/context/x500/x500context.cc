/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)x500context.cc	1.1	94/12/05 SMI"


#include <string.h>
#include "x500context.hh"


static const FN_string		slash((const unsigned char *)"/");


CX500Context::CX500Context(
	const FN_ref_addr	&addr,
	const FN_ref		&ref,
	int			&err
) : x500_dua(err)
{
	if (addr.length() > 0)
		context_prefix = new FN_string((unsigned char *)addr.data(),
		    addr.length());
	else
		context_prefix = 0;

	self_reference = new FN_ref(ref);

	x500_debug("[CX500Context:/%s]\n",
	    context_prefix ? (char *)context_prefix->str() : "");
}


CX500Context::~CX500Context(
)
{
	x500_debug("[~CX500Context:/%s]\n",
	    context_prefix ? (char *)context_prefix->str() : "");

	delete self_reference;
	delete context_prefix;
}


/*
 *	C O N T E X T    I N T E R F A C E
 */

FN_ref *
CX500Context::c_lookup(
	const FN_string	&name,
	unsigned int,
	FN_status_csvc	&cs
)
{
	FN_string	*x500_name;
	unsigned int	status;
	FN_ref		*ref = 0;
	int		err;

	x500_debug("CX500Context::c_lookup(\"%s\")\n", name.str());

	if (name.is_empty())
		return (new FN_ref(*self_reference));

	if (context_prefix)
		x500_name = new FN_string(&status, context_prefix, &slash,
		    &name, 0);
	else
		x500_name = new FN_string(&status, &slash, &name, 0);

	if (! x500_name) {
		cs.set_error(FN_E_INSUFFICIENT_RESOURCES, *self_reference,
		    name);
		return (0);
	}

	mutex_lock(&x500_dua_mutex);

	if (ref = x500_dua.lookup(*x500_name, err)) {
		cs.set_success();
	} else {
		cs.set_error(err, *self_reference, name);
	}

	mutex_unlock(&x500_dua_mutex);

	delete x500_name;
	return (ref);
}


FN_ref *
CX500Context::c_lookup_nns(
	const FN_string	&name,
	unsigned int,
	FN_status_csvc	&cs
)
{
	FN_string	*x500_name;
	unsigned int	status;
	FN_ref		*ref;
	int		err;

	x500_debug("CX500Context::c_lookup_nns(\"%s\")\n", name.str());

	if (context_prefix)
		x500_name = new FN_string(&status, context_prefix, &slash,
		    &name, 0);
	else
		x500_name = new FN_string(&status, &slash, &name, 0);

	if (! x500_name) {
		cs.set_error(FN_E_INSUFFICIENT_RESOURCES, *self_reference,
		    name);
		return (0);
	}

	mutex_lock(&x500_dua_mutex);

	if (ref = x500_dua.lookup_next(*x500_name, err)) {
		cs.set_success();
	} else {
		cs.set_error(err, *self_reference, name);
	}

	mutex_unlock(&x500_dua_mutex);

	delete x500_name;
	return (ref);
}

FN_namelist *
CX500Context::c_list_names(
	const FN_string	&name,
	FN_status_csvc	&cs
)
{
	FN_string	*x500_name;
	unsigned int	status;
	FN_namelist	*nl;
	int		err;

	x500_debug("CX500Context::c_list_names(\"%s\")\n", name.str());

	if (context_prefix)
		x500_name = new FN_string(&status, context_prefix, &slash,
		    &name, 0);
	else
		x500_name = new FN_string(&status, &slash, &name, 0);

	if (! x500_name) {
		cs.set_error(FN_E_INSUFFICIENT_RESOURCES, *self_reference,
		    name);
		return (0);
	}

	mutex_lock(&x500_dua_mutex);

	if (nl = x500_dua.list_names(*x500_name, err)) {
		cs.set_success();
	} else {
		cs.set_error(err, *self_reference, name);
	}

	mutex_unlock(&x500_dua_mutex);

	delete x500_name;
	return (nl);
}


FN_namelist *
CX500Context::c_list_names_nns(
	const FN_string	&name,
	FN_status_csvc	&cs
)
{
	x500_debug("CX500Context::c_list_names_nns(\"%s\")\n", name.str());

	FN_ref	*ref;

	if (ref = c_lookup_nns(name, 0, cs)) {
		cs.set(FN_E_SPI_CONTINUE, ref, 0, 0);
		delete ref;
	}
	return (0);
}


FN_bindinglist *
CX500Context::c_list_bindings(
	const FN_string	&name,
	FN_status_csvc	&cs
)
{
	FN_string	*x500_name;
	unsigned int	status;
	FN_bindinglist	*bl;
	int		err;

	x500_debug("CX500Context::c_list_bindings(\"%s\")\n", name.str());

	if (context_prefix)
		x500_name = new FN_string(&status, context_prefix, &slash,
		    &name, 0);
	else
		x500_name = new FN_string(&status, &slash, &name, 0);

	if (! x500_name) {
		cs.set_error(FN_E_INSUFFICIENT_RESOURCES, *self_reference,
		    name);
		return (0);
	}

	mutex_lock(&x500_dua_mutex);

	if (bl = x500_dua.list_bindings(*x500_name, err)) {
		cs.set_success();
	} else {
		cs.set_error(err, *self_reference, name);
	}

	mutex_unlock(&x500_dua_mutex);

	delete x500_name;
	return (bl);
}


FN_bindinglist *
CX500Context::c_list_bindings_nns(
	const FN_string	&name,
	FN_status_csvc	&cs
)
{
	x500_debug("CX500Context::c_list_bindings_nns(\"%s\")\n", name.str());

	FN_ref	*ref;

	if (ref = c_lookup_nns(name, 0, cs)) {
		cs.set(FN_E_SPI_CONTINUE, ref, 0, 0);
		delete ref;
	}
	return (0);
}


int
CX500Context::c_bind(
	const FN_string	&name,
	const FN_ref	&,
	unsigned int,
	FN_status_csvc	&cs
)
{
	x500_debug("CX500Context::c_bind(\"%s\")\n", name.str());

	// cannot support in X.500
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}


int
CX500Context::c_bind_nns(
	const FN_string	&name,
	const FN_ref	&ref,
	unsigned int	exclusive,
	FN_status_csvc	&cs
)
{
	FN_string	*x500_name;
	unsigned int	status;
	int		status_code;

	x500_debug("CX500Context::c_bind_nns(\"%s\")\n", name.str());

	if (context_prefix)
		x500_name = new FN_string(&status, context_prefix, &slash,
		    &name, 0);
	else
		x500_name = new FN_string(&status, &slash, &name, 0);

	if (! x500_name) {
		cs.set_error(FN_E_INSUFFICIENT_RESOURCES, *self_reference,
		    name);
		return (0);
	}

	mutex_lock(&x500_dua_mutex);

	if ((status_code = x500_dua.bind_next(*x500_name, ref, exclusive)) ==
	    FN_SUCCESS) {
		cs.set_success();
	} else {
		cs.set_error(status_code, *self_reference, name);
	}

	mutex_unlock(&x500_dua_mutex);

	delete x500_name;
	return ((status_code == FN_SUCCESS) ? 1 : 0);
}


int
CX500Context::c_unbind(
	const FN_string	&name,
	FN_status_csvc	&cs
)
{
	FN_string	*x500_name;
	unsigned int	status;
	int		status_code;

	x500_debug("CX500Context::c_unbind(\"%s\")\n", name.str());

	if (context_prefix)
		x500_name = new FN_string(&status, context_prefix, &slash,
		    &name, 0);
	else
		x500_name = new FN_string(&status, &slash, &name, 0);

	if (! x500_name) {
		cs.set_error(FN_E_INSUFFICIENT_RESOURCES, *self_reference,
		    name);
		return (0);
	}

	mutex_lock(&x500_dua_mutex);

	if ((status_code = x500_dua.unbind(*x500_name)) == FN_SUCCESS) {
		cs.set_success();
	} else {
		cs.set_error(status_code, *self_reference, name);
	}

	mutex_unlock(&x500_dua_mutex);

	delete x500_name;
	return ((status_code == FN_SUCCESS) ? 1 : 0);
}


int
CX500Context::c_unbind_nns(
	const FN_string	&name,
	FN_status_csvc	&cs
)
{
	FN_string	*x500_name;
	unsigned int	status;
	int		status_code;

	x500_debug("CX500Context::c_unbind_nns(\"%s\")\n", name.str());

	if (context_prefix)
		x500_name = new FN_string(&status, context_prefix, &slash,
		    &name, 0);
	else
		x500_name = new FN_string(&status, &slash, &name, 0);

	if (! x500_name) {
		cs.set_error(FN_E_INSUFFICIENT_RESOURCES, *self_reference,
		    name);
		return (0);
	}

	mutex_lock(&x500_dua_mutex);

	if ((status_code = x500_dua.unbind_next(*x500_name)) == FN_SUCCESS) {
		cs.set_success();
	} else {
		cs.set_error(status_code, *self_reference, name);
	}

	mutex_unlock(&x500_dua_mutex);

	delete x500_name;
	return ((status_code == FN_SUCCESS) ? 1 : 0);
}


int
CX500Context::c_rename(
	const FN_string		&oldname,
	const FN_composite_name	&newname,
	unsigned int		exclusive,
	FN_status_csvc		&cs
)
{
	FN_string	*x500_name;
	unsigned int	status;
	int		status_code;

	x500_debug("CX500Context::c_rename(\"%s\")\n", oldname.str());

	// X.500 only supports rename of leaf entries
	if (newname.count() != 1) {
		cs.set_error(FN_E_ILLEGAL_NAME, *self_reference, oldname);
		return (0);
	}

	if (context_prefix)
		x500_name = new FN_string(&status, context_prefix, &slash,
		    &oldname, 0);
	else
		x500_name = new FN_string(&status, &slash, &oldname, 0);

	if (! x500_name) {
		cs.set_error(FN_E_INSUFFICIENT_RESOURCES, *self_reference,
		    oldname);
		return (0);
	}

	mutex_lock(&x500_dua_mutex);

	FN_string	*name_string = newname.string();

	if ((status_code = x500_dua.rename(*x500_name, name_string,
	    exclusive)) == FN_SUCCESS) {
		cs.set_success();
	} else {
		cs.set_error(status_code, *self_reference, oldname);
	}
	delete name_string;

	mutex_unlock(&x500_dua_mutex);

	delete x500_name;
	return ((status_code == FN_SUCCESS) ? 1 : 0);
}


int
CX500Context::c_rename_nns(
	const FN_string		&oldname,
	const FN_composite_name	&,
	unsigned int,
	FN_status_csvc		&cs
)
{
	x500_debug("CX500Context::c_rename_nns(\"%s\")\n", oldname.str());

	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, oldname);
	return (0);
}


FN_ref *
CX500Context::c_create_subcontext(
	const FN_string	&name,
	FN_status_csvc	&cs
)
{
	x500_debug("CX500Context::c_create_subcontext(\"%s\")\n", name.str());

	// cannot support in X.500
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}


FN_ref *
CX500Context::c_create_subcontext_nns(
	const FN_string	&name,
	FN_status_csvc	&cs
)
{
	x500_debug("CX500Context::c_create_subcontext_nns(\"%s\")\n",
	    name.str());

	// cannot support in X.500
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}


int
CX500Context::c_destroy_subcontext(
	const FN_string	&name,
	FN_status_csvc	&cs
)
{
	x500_debug("CX500Context::c_destroy_subcontext(\"%s\")\n", name.str());

	// consistent with c_create_subcontext()
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}


int
CX500Context::c_destroy_subcontext_nns(
	const FN_string	&name,
	FN_status_csvc	&cs
)
{
	x500_debug("CX500Context::c_destroy_subcontext_nns(\"%s\")\n",
	    name.str());

	// consistent with c_create_subcontext_nns()
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}


/*
 *	A T T R I B U T E    I N T E R F A C E
 */

FN_attribute *
CX500Context::c_attr_get(
	const FN_string		&name,
	const FN_identifier	&id,
	FN_status_csvc		&cs
)
{
	FN_string	*x500_name;
	unsigned int	status;
	int		err;
	FN_attribute	*attr;

	x500_debug("CX500Context::c_attr_get(\"%s\")\n", name.str());

	if (context_prefix)
		x500_name = new FN_string(&status, context_prefix, &slash,
		    &name, 0);
	else
		x500_name = new FN_string(&status, &slash, &name, 0);

	if (! x500_name) {
		cs.set_error(FN_E_INSUFFICIENT_RESOURCES, *self_reference,
		    name);
		return (0);
	}

	mutex_lock(&x500_dua_mutex);

	if (attr = x500_dua.get_attr(*x500_name, id, err))
		cs.set_success();
	else
		cs.set_error(err, *self_reference, name);

	mutex_unlock(&x500_dua_mutex);

	delete x500_name;
	return (attr);
}


FN_attribute *
CX500Context::c_attr_get_nns(
	const FN_string		&name,
	const FN_identifier	&,
	FN_status_csvc		&cs
)
{
	x500_debug("CX500Context::c_attr_get_nns(\"%s\")\n", name.str());

	FN_ref	*ref;

	if (ref = c_lookup_nns(name, 0, cs)) {
		cs.set(FN_E_SPI_CONTINUE, ref, 0, 0);
		delete ref;
	}
	return (0);
}


FN_attrset *
CX500Context::c_attr_get_ids(
	const FN_string	&name,
	FN_status_csvc	&cs
)
{
	FN_string	*x500_name;
	unsigned int	status;
	int		err;
	FN_attrset	*attrs;

	x500_debug("CX500Context::c_attr_get_ids(\"%s\")\n", name.str());

	if (context_prefix)
		x500_name = new FN_string(&status, context_prefix, &slash,
		    &name, 0);
	else
		x500_name = new FN_string(&status, &slash, &name, 0);

	if (! x500_name) {
		cs.set_error(FN_E_INSUFFICIENT_RESOURCES, *self_reference,
		    name);
		return (0);
	}

	mutex_lock(&x500_dua_mutex);

	if (attrs = x500_dua.get_attr_ids(*x500_name, err)) {
		cs.set_success();
	} else {
		cs.set_error(err, *self_reference, name);
	}

	mutex_unlock(&x500_dua_mutex);

	delete x500_name;
	return (attrs);
}


FN_attrset *
CX500Context::c_attr_get_ids_nns(
	const FN_string	&name,
	FN_status_csvc	&cs
)
{
	x500_debug("CX500Context::c_attr_get_ids_nns(\"%s\")\n", name.str());

	FN_ref	*ref;

	if (ref = c_lookup_nns(name, 0, cs)) {
		cs.set(FN_E_SPI_CONTINUE, ref, 0, 0);
		delete ref;
	}
	return (0);
}


FN_valuelist *
CX500Context::c_attr_get_values(
	const FN_string		&name,
	const FN_identifier	&,
	FN_status_csvc		&cs
)
{
	x500_debug("CX500Context::c_attr_get_values(\"%s\")\n", name.str());

	// unsupported, for now
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}


FN_valuelist *
CX500Context::c_attr_get_values_nns(
	const FN_string		&name,
	const FN_identifier	&,
	FN_status_csvc		&cs
)
{
	x500_debug("CX500Context::c_attr_get_values_nns(\"%s\")\n", name.str());

	FN_ref	*ref;

	if (ref = c_lookup_nns(name, 0, cs)) {
		cs.set(FN_E_SPI_CONTINUE, ref, 0, 0);
		delete ref;
	}
	return (0);
}


FN_multigetlist *
CX500Context::c_attr_multi_get(
	const FN_string		&name,
	const FN_attrset	*ids,
	FN_status_csvc		&cs
)
{
	FN_string	*x500_name;
	unsigned int	status;
	int		err;
	FN_multigetlist	*attrs;

	x500_debug("CX500Context::c_attr_multi_get(\"%s\")\n", name.str());

	if (context_prefix)
		x500_name = new FN_string(&status, context_prefix, &slash,
		    &name, 0);
	else
		x500_name = new FN_string(&status, &slash, &name, 0);

	if (! x500_name) {
		cs.set_error(FN_E_INSUFFICIENT_RESOURCES, *self_reference,
		    name);
		return (0);
	}

	mutex_lock(&x500_dua_mutex);

	if (attrs = x500_dua.get_attrs(*x500_name, ids, err)) {
		cs.set_success();
	} else {
		cs.set_error(err, *self_reference, name);
	}

	mutex_unlock(&x500_dua_mutex);

	delete x500_name;
	return (attrs);
}


FN_multigetlist *
CX500Context::c_attr_multi_get_nns(
	const FN_string		&name,
	const FN_attrset	*,
	FN_status_csvc		&cs
)
{
	x500_debug("CX500Context::c_attr_multi_get_nns(\"%s\")\n", name.str());

	FN_ref	*ref;

	if (ref = c_lookup_nns(name, 0, cs)) {
		cs.set(FN_E_SPI_CONTINUE, ref, 0, 0);
		delete ref;
	}
	return (0);
}


int
CX500Context::c_attr_modify(
	const FN_string		&name,
	unsigned int		mod_op,
	const FN_attribute	&attr,
	FN_status_csvc		&cs
)
{
	FN_string	*x500_name;
	unsigned int	status;
	int		status_code;

	x500_debug("CX500Context::c_attr_modify(\"%s\")\n", name.str());

	if (context_prefix)
		x500_name = new FN_string(&status, context_prefix, &slash,
		    &name, 0);
	else
		x500_name = new FN_string(&status, &slash, &name, 0);

	if (! x500_name) {
		cs.set_error(FN_E_INSUFFICIENT_RESOURCES, *self_reference,
		    name);
		return (0);
	}

	mutex_lock(&x500_dua_mutex);

	if ((status_code = x500_dua.modify_attr(*x500_name, mod_op, attr)) ==
	    FN_SUCCESS) {

		cs.set_success();
	} else {
		cs.set_error(status_code, *self_reference, name);
	}

	mutex_unlock(&x500_dua_mutex);

	delete x500_name;
	return ((status_code == FN_SUCCESS) ? 1 : 0);
}


int
CX500Context::c_attr_modify_nns(
	const FN_string		&name,
	unsigned int,
	const FN_attribute	&,
	FN_status_csvc		&cs
)
{
	x500_debug("CX500Context::c_attr_modify_nns(\"%s\")\n", name.str());

	FN_ref	*ref;

	if (ref = c_lookup_nns(name, 0, cs)) {
		cs.set(FN_E_SPI_CONTINUE, ref, 0, 0);
		delete ref;
	}
	return (0);
}


int
CX500Context::c_attr_multi_modify(
	const FN_string		&name,
	const FN_attrmodlist	&,
	FN_attrmodlist		**,
	FN_status_csvc		&cs
)
{
	x500_debug("CX500Context::c_attr_multi_modify(\"%s\")\n", name.str());

	// unsupported, for now
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}


int
CX500Context::c_attr_multi_modify_nns(
	const FN_string		&name,
	const FN_attrmodlist	&,
	FN_attrmodlist		**,
	FN_status_csvc		&cs
)
{
	x500_debug("CX500Context::c_attr_multi_modify_nns(\"%s\")\n",
	    name.str());

	FN_ref	*ref;

	if (ref = c_lookup_nns(name, 0, cs)) {
		cs.set(FN_E_SPI_CONTINUE, ref, 0, 0);
		delete ref;
	}
	return (0);
}


FN_ref *
CX500Context::get_ref(
	FN_status	&s
) const
{
	x500_debug("CX500Context::get_ref()\n");

	FN_ref	*ref;

	if ((ref = new FN_ref(*self_reference)) == 0) {
		s.set_code(FN_E_INSUFFICIENT_RESOURCES);
		return (0);
	}

	s.set_success();
	return (ref);
}


/*
 * X.500 DN syntax: slash-separated, left-to-right, case-insensitive
 * e.g.  /C=US/O=Wiz/OU=Sales,L=West/CN=Manager
 */
FN_attrset *
CX500Context::c_get_syntax_attrs(
	const FN_string	&name,
	FN_status_csvc	&cs
)
{
	x500_debug("CX500Context::c_get_syntax_attrs(\"%s\")\n", name.str());

	static const FN_string	separator((unsigned char *)"/");
	static const FN_string	begin_quote((unsigned char *)"\"");
	static const FN_string	end_quote((unsigned char *)"\"");
	static const FN_string	escape((unsigned char *)"\\");
	static const FN_string	type_value_separator((unsigned char *)"=");
	static const FN_string	ava_separator((unsigned char *)",");

	static const FN_syntax_standard	x500_syntax(
	    FN_SYNTAX_STANDARD_DIRECTION_LTR, FN_STRING_CASE_INSENSITIVE,
	    &separator, &begin_quote, &end_quote, &escape,
	    &type_value_separator, &ava_separator);

	FN_attrset		*syntax_attrs = x500_syntax.get_syntax_attrs();

	if (syntax_attrs)
		cs.set_success();
	else
		cs.set_error(FN_E_INSUFFICIENT_RESOURCES, *self_reference,
		    name);

	return (syntax_attrs);
}


FN_attrset *
CX500Context::c_get_syntax_attrs_nns(
	const FN_string	&name,
	FN_status_csvc	&cs
)
{
	x500_debug("CX500Context::c_get_syntax_attrs_nns(\"%s\")\n",
	    name.str());

	FN_ref	*ref;

	if (ref = c_lookup_nns(name, 0, cs)) {
		cs.set(FN_E_SPI_CONTINUE, ref, 0, 0);
		delete ref;
	}
	return (0);
}


/*
 * extract X.500 distinguished name
 * rule: extract first sequence of components containing '='
 */
FN_composite_name *
CX500Context::p_component_parser(
	const FN_composite_name	&name,
	FN_composite_name	**rest,
	FN_status_psvc		&s
)
{
#ifdef DEBUG
	FN_string	*temp = name.string();

	x500_debug("CX500Context::p_component_parser(\"%s\")\n", temp->str());

	delete temp;
#endif

	s.set_success();
	void			*position;
	FN_composite_name	*dn = new FN_composite_name();
	const FN_string		*component = name.first(position);

	while (component && strchr((const char *)component->str(), '=')) {

		x500_debug("CX500Context::p_component_parser: %s\n",
		    component->str());

		dn->append_comp(*component);
		component = name.next(position);
	}

	if (rest) {
		if (component)
			name.prev(position);

		*rest = name.suffix(position);

#ifdef DEBUG
		if (*rest) {
			FN_string	*temp = (*rest)->string();

			x500_debug("CX500Context::p_component_parser: %s: %s\n",
			    "rest", temp->str());

			delete temp;
		}
#endif
	}

	return (dn);
}
