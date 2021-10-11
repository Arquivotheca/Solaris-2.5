/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident "@(#)FN_ctx_cnsvc_impl.cc	1.5 94/11/20 SMI"

#include <xfn/fn_spi.hh>

static FN_composite_name empty_name((unsigned char *)"");

FN_ctx_cnsvc_impl::~FN_ctx_cnsvc_impl()
{
}

FN_ref*
FN_ctx_cnsvc_impl::p_lookup(const FN_composite_name &n,
    unsigned int lookup_flags, FN_status_psvc &ps)
{
	FN_ref *ret = 0;
	FN_composite_name *fn = 0;
	FN_status_cnsvc bs(ps);
	switch (p_resolve(n, ps, &fn)) {
	case RS_TERMINAL_NNS_COMPONENT:
		// list NNS context
		ret = cn_lookup_nns(*fn, lookup_flags, bs);
		if (!ps.is_success()) {
			if (!ps.is_following_terminal_link())
				ps.append_remaining_name(empty_name);
		} else {
			if (!(lookup_flags&FN_SPI_LEAVE_TERMINAL_LINK) &&
			    ret->is_link()) {
				ps.set_continue_context(*ret, *this, 0);
				delete ret;
				ret = 0;
			}
		}
		break;
	case RS_TERMINAL_COMPONENT:
		// list terminal component
		ret = cn_lookup(n, lookup_flags, bs);
		if (ps.is_success() &&
		    !(lookup_flags&FN_SPI_LEAVE_TERMINAL_LINK) &&
		    ret->is_link()) {
			ps.set_continue_context(*ret, *this, 0);
			delete ret;
			ret = 0;
		}
	}
	delete fn;
	return (ret);
}

FN_namelist*
FN_ctx_cnsvc_impl::p_list_names(const FN_composite_name &n,
    FN_status_psvc &ps)
{
	FN_namelist *ret = 0;
	FN_status_cnsvc rs(ps);
	FN_composite_name *fn = 0;
	switch (p_resolve(n, ps, &fn)) {
	case RS_TERMINAL_NNS_COMPONENT:
		// trailing slash means list NNS context
		ret = cn_list_names_nns(*fn, rs);
		if (!rs.is_success() && !rs.is_following_terminal_link())
			rs.append_remaining_name(empty_name);
		break;
	case RS_TERMINAL_COMPONENT:
		// list terminal component
		ret = cn_list_names(n, rs);
	}
	delete fn;
	return (ret);
}

FN_bindinglist*
FN_ctx_cnsvc_impl::p_list_bindings(const FN_composite_name &n,
    FN_status_psvc &ps)
{
	FN_bindinglist *ret = 0;
	FN_composite_name *fn = 0;
	FN_status_cnsvc rs(ps);
	switch (p_resolve(n, ps, &fn)) {
	case RS_TERMINAL_NNS_COMPONENT:
		// trailing slash means list NNS context
		ret = cn_list_bindings_nns(*fn, rs);
		if (!ps.is_success() && !rs.is_following_terminal_link())
			ps.append_remaining_name(empty_name);
		break;
	case RS_TERMINAL_COMPONENT:
		// list terminal component
		ret = cn_list_bindings(n, rs);
	}
	delete fn;
	return (ret);
}

int
FN_ctx_cnsvc_impl::p_bind(const FN_composite_name &n,
    const FN_ref &r, unsigned f, FN_status_psvc &ps)
{
	int ret = 0;
	FN_composite_name *fn = 0;
	FN_status_cnsvc rs(ps);
	switch (p_resolve(n, ps, &fn)) {
	case RS_TERMINAL_NNS_COMPONENT:
		// trailing slash means bind NNS context
		ret = cn_bind_nns(*fn, r, f, rs);
		if (!ps.is_success() && !ps.is_following_terminal_link())
			ps.append_remaining_name(empty_name);
		break;
	case RS_TERMINAL_COMPONENT:
		// bind terminal component
		ret = cn_bind(n, r, f, rs);
	}
	delete fn;
	return (ret);
}

int
FN_ctx_cnsvc_impl::p_unbind(const FN_composite_name &n,
    FN_status_psvc &ps)
{
	int ret = 0;
	FN_composite_name *fn = 0;
	FN_status_cnsvc rs(ps);
	switch (p_resolve(n, ps, &fn)) {
	case RS_TERMINAL_NNS_COMPONENT:
		// trailing slash means unbind NNS context
		ret = cn_unbind_nns(*fn, rs);
		if (!ps.is_success() && !ps.is_following_terminal_link())
			ps.append_remaining_name(empty_name);
		break;
	case RS_TERMINAL_COMPONENT:
		// unbind terminal component
		ret = cn_unbind(n, rs);
	}
	delete fn;
	return (ret);
}

FN_ref*
FN_ctx_cnsvc_impl::p_create_subcontext(const FN_composite_name &n,
    FN_status_psvc &ps)
{
	FN_ref *ret = 0;
	FN_composite_name *fn = 0;
	FN_status_cnsvc rs(ps);
	switch (p_resolve(n, ps, &fn)) {
	case RS_TERMINAL_NNS_COMPONENT:
		// trailing slash means create NNS context
		ret = cn_create_subcontext_nns(*fn, rs);
		if (!ps.is_success() && !ps.is_following_terminal_link())
			ps.append_remaining_name(empty_name);
		break;
	case RS_TERMINAL_COMPONENT:
		// create terminal component
		ret = cn_create_subcontext(n, rs);
	}
	delete fn;
	return (ret);
}

int
FN_ctx_cnsvc_impl::p_destroy_subcontext(const FN_composite_name &n,
    FN_status_psvc &ps)
{
	int ret = 0;
	FN_composite_name *fn = 0;
	FN_status_cnsvc rs(ps);
	switch (p_resolve(n, ps, &fn)) {
	case RS_TERMINAL_NNS_COMPONENT:
		// trailing slash means destroy_subcontext of NNS context
		ret = cn_destroy_subcontext_nns(*fn, rs);
		if (!ps.is_success() && !ps.is_following_terminal_link())
			ps.append_remaining_name(empty_name);
		break;
	case RS_TERMINAL_COMPONENT:
		// destroy_subcontext of terminal component
		ret = cn_destroy_subcontext(n, rs);
	}
	delete fn;
	return (ret);
}

int
FN_ctx_cnsvc_impl::p_rename(const FN_composite_name &n,
    const FN_composite_name &newn, unsigned int f, FN_status_psvc &ps)
{
	int ret = 0;
	FN_composite_name *fn = 0;
	FN_status_cnsvc rs(ps);
	switch (p_resolve(n, ps, &fn)) {
	case RS_TERMINAL_NNS_COMPONENT:
		// trailing slash means rename NNS context
		ret = cn_rename_nns(*fn, newn, f, rs);
		if (!ps.is_success() && !ps.is_following_terminal_link())
			ps.append_remaining_name(empty_name);
		break;
	case RS_TERMINAL_COMPONENT:
		// rename terminal component
		ret = cn_rename(n, newn, f, rs);
	}
	delete fn;
	return (ret);
}


FN_attrset*
FN_ctx_cnsvc_impl::p_get_syntax_attrs(const FN_composite_name &n,
    FN_status_psvc &ps)
{
	FN_attrset *ret = 0;
	FN_composite_name *fn = 0;
	FN_status_cnsvc rs(ps);
	switch (p_resolve(n, ps, &fn)) {
	case RS_TERMINAL_NNS_COMPONENT:
		// trailing slash means get attrs of NNS context
		ret = cn_get_syntax_attrs_nns(*fn, rs);
		if (!ps.is_success() && !ps.is_following_terminal_link())
			ps.append_remaining_name(empty_name);
		break;
	case RS_TERMINAL_COMPONENT:
		// get attrs of terminal component
		ret = cn_get_syntax_attrs(n, rs);
	}
	delete fn;
	return (ret);
}

FN_attribute*
FN_ctx_cnsvc_impl::p_attr_get(const FN_composite_name &n,
    const FN_identifier &id, FN_status_psvc &ps)
{
	FN_attribute *ret = 0;
	FN_composite_name *fn = 0;
	FN_status_cnsvc rs(ps);
	switch (p_resolve(n, ps, &fn)) {
	case RS_TERMINAL_NNS_COMPONENT:
		// trailing slash means get attrs of NNS context
		ret = cn_attr_get_nns(*fn, id, rs);
		if (!ps.is_success() && !ps.is_following_terminal_link())
			ps.append_remaining_name(empty_name);
		break;
	case RS_TERMINAL_COMPONENT:
		// get attrs of terminal component
		ret = cn_attr_get(n, id, rs);
	}
	delete fn;
	return (ret);
}

int
FN_ctx_cnsvc_impl::p_attr_modify(const FN_composite_name &n,
    unsigned int i, const FN_attribute &a, FN_status_psvc &ps)
{
	int ret = 0;
	FN_composite_name *fn = 0;
	FN_status_cnsvc rs(ps);
	switch (p_resolve(n, ps, &fn)) {
	case RS_TERMINAL_NNS_COMPONENT:
		// trailing slash means get attrs of NNS context
		ret = cn_attr_modify_nns(*fn, i, a, rs);
		if (!ps.is_success() && !ps.is_following_terminal_link())
			ps.append_remaining_name(empty_name);
		break;
	case RS_TERMINAL_COMPONENT:
		// get attrs of terminal component
		ret = cn_attr_modify(n, i, a, rs);
	}
	delete fn;
	return (ret);
}

FN_valuelist*
FN_ctx_cnsvc_impl::p_attr_get_values(const FN_composite_name &n,
    const FN_identifier &i, FN_status_psvc &ps)
{
	FN_valuelist *ret = 0;
	FN_composite_name *fn = 0;
	FN_status_cnsvc rs(ps);
	switch (p_resolve(n, ps, &fn)) {
	case RS_TERMINAL_NNS_COMPONENT:
		// trailing slash means get attrs of NNS context
		ret = cn_attr_get_values_nns(*fn, i, rs);
		if (!ps.is_success() && !ps.is_following_terminal_link())
			ps.append_remaining_name(empty_name);
		break;
	case RS_TERMINAL_COMPONENT:
		// get attrs of terminal component
		ret = cn_attr_get_values(n, i, rs);
	}
	delete fn;
	return (ret);
}

FN_attrset*
FN_ctx_cnsvc_impl::p_attr_get_ids(const FN_composite_name &n,
    FN_status_psvc &ps)
{
	FN_attrset *ret = 0;
	FN_composite_name *fn = 0;
	FN_status_cnsvc rs(ps);
	switch (p_resolve(n, ps, &fn)) {
	case RS_TERMINAL_NNS_COMPONENT:
		// trailing slash means get attrs of NNS context
		ret = cn_attr_get_ids_nns(*fn, rs);
		if (!ps.is_success() && !ps.is_following_terminal_link())
			ps.append_remaining_name(empty_name);
		break;
	case RS_TERMINAL_COMPONENT:
		// get attrs of terminal component
		ret = cn_attr_get_ids(n, rs);
	}
	delete fn;
	return (ret);
}

FN_multigetlist*
FN_ctx_cnsvc_impl::p_attr_multi_get(const FN_composite_name &n,
    const FN_attrset *a, FN_status_psvc &ps)
{
	FN_multigetlist *ret = 0;
	FN_composite_name *fn = 0;
	FN_status_cnsvc rs(ps);
	switch (p_resolve(n, ps, &fn)) {
	case RS_TERMINAL_NNS_COMPONENT:
		// trailing slash means get attrs of NNS context
		ret = cn_attr_multi_get_nns(*fn, a, rs);
		if (!ps.is_success() && !ps.is_following_terminal_link())
			ps.append_remaining_name(empty_name);
		break;
	case RS_TERMINAL_COMPONENT:
		// get attrs of terminal component
		ret = cn_attr_multi_get(n, a, rs);
	}
	delete fn;
	return (ret);
}

int
FN_ctx_cnsvc_impl::p_attr_multi_modify(const FN_composite_name &n,
    const FN_attrmodlist &l, FN_attrmodlist **a, FN_status_psvc &ps)
{
	int ret = 0;
	FN_composite_name *fn = 0;
	FN_status_cnsvc rs(ps);
	switch (p_resolve(n, ps, &fn)) {
	case RS_TERMINAL_NNS_COMPONENT:
		// trailing slash means get attrs of NNS context
		ret = cn_attr_multi_modify_nns(*fn, l, a, rs);
		if (!ps.is_success() && !ps.is_following_terminal_link())
			ps.append_remaining_name(empty_name);
		break;
	case RS_TERMINAL_COMPONENT:
		// get attrs of terminal component
		ret = cn_attr_multi_modify(n, l, a, rs);
	}
	delete fn;
	return (ret);
}
