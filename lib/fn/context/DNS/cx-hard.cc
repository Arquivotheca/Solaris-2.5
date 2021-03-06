/*
 * Copyright (c) 1993 - 1994 by Sun Microsystems, Inc.
 *
 * Here, we define context ops which are considered to be "hard" to implement
 * for DNS.  They return the FN_E_OPERATION_NOT_SUPPORTED status.
 */

#pragma ident "@(#)cx-hard.cc	1.7 94/11/20 SMI"

#include <stdio.h>

#include "cx.hh"


int
DNS_ctx::c_bind(
	const FN_string &name,
	const FN_ref & /* r */,
	unsigned /* bind_flags */,
	FN_status_csvc &cs)
{
	if (trace)
		fprintf(stderr, "DNS_ctx::c_bind() call\n");

	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}

int
DNS_ctx::c_unbind(const FN_string &name, FN_status_csvc &cs)
{
	if (trace)
		fprintf(stderr, "DNS_ctx::c_unbind() call\n");

	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}

FN_ref *
DNS_ctx::c_create_subcontext(const FN_string &name, FN_status_csvc &cs)
{
	if (trace)
		fprintf(stderr, "DNS_ctx::c_create_subcontext() call\n");

	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}

int
DNS_ctx::c_destroy_subcontext(const FN_string &name, FN_status_csvc &cs)
{
	if (trace)
		fprintf(stderr, "DNS_ctx::c_destroy_subcontext() call\n");

	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}

int
DNS_ctx::c_unbind_nns(const FN_string &name, FN_status_csvc &cs)
{
	if (trace)
		fprintf(stderr, "DNS_ctx::c_unbind_nns() call\n");

	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}

FN_ref *
DNS_ctx::c_create_subcontext_nns(const FN_string &name, FN_status_csvc &cs)
{
	if (trace)
		fprintf(stderr, "DNS_ctx::c_create_subcontext_nns() call\n");

	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}

int
DNS_ctx::c_destroy_subcontext_nns(const FN_string &name, FN_status_csvc &cs)
{
	if (trace)
		fprintf(stderr, "DNS_ctx::c_destroy_subcontext_nns() call\n");

	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}

int
DNS_ctx::c_attr_modify_nns(
	const FN_string &name,
	unsigned int,
	const FN_attribute &,
	FN_status_csvc &cs)
{
	if (trace)
		fprintf(stderr, "DNS_ctx::c_attr_modify_nns() call\n");

	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}

int
DNS_ctx::c_attr_multi_modify_nns(
	const FN_string &name,
	const FN_attrmodlist &,
	FN_attrmodlist **,
	FN_status_csvc &cs)
{
	if (trace)
		fprintf(stderr, "DNS_ctx::c_attr_mmulti_modify_nns() call\n");

	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}

int
DNS_ctx::c_rename_nns(
	const FN_string &oldname,
	const FN_composite_name &/*newname*/,
	unsigned int /*exclusive*/,
	FN_status_csvc &cs)
{
	if (trace)
		fprintf(stderr, "DNS_ctx::c_rename_nns() call\n");

	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, oldname);
	return (0);
}

int
DNS_ctx::c_rename(
	const FN_string &oldname,
	const FN_composite_name &/*newname*/,
	unsigned int /*exclusive*/,
	FN_status_csvc &cs)
{
	if (trace)
		fprintf(stderr, "DNS_ctx::c_rename() call\n");

	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, oldname);
	return (0);
}

int
DNS_ctx::c_attr_multi_modify(
	const FN_string &name,
	const FN_attrmodlist &,
	FN_attrmodlist **,
	FN_status_csvc &cs)
{
	if (trace)
		fprintf(stderr, "DNS_ctx::c_attr_multi_modify() call\n");

	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}

int
DNS_ctx::c_attr_modify(
	const FN_string &name,
	unsigned int,
	const FN_attribute &,
	FN_status_csvc &cs)
{
	if (trace)
		fprintf(stderr, "DNS_ctx::c_attr_modify() call\n");

	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}

int
DNS_ctx::c_bind_nns(
	const FN_string &name,
	const FN_ref & /* r */,
	unsigned /* bind_flags */,
	FN_status_csvc &cs)
{
	if (trace)
		fprintf(stderr, "DNS_ctx::c_blind_nns() call\n");

	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}

FN_attrset *
DNS_ctx::c_get_attrs(const FN_string &name, FN_status_csvc &cs)
{
	if (trace)
		fprintf(stderr, "DNS_ctx::c_get_attrs(\"%s\") call\n", (const char *)name.str());

	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}

FN_attribute *
DNS_ctx::c_attr_get_nns(
	const FN_string &name,
	const FN_identifier &,
	FN_status_csvc &cs)
{
	if (trace)
		fprintf(stderr, "DNS_ctx::c_attr_get_nns() call\n");

	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}

FN_valuelist *
DNS_ctx::c_attr_get_values_nns(
	const FN_string &name,
	const FN_identifier &,
	FN_status_csvc &cs)
{
	if (trace)
		fprintf(stderr, "DNS_ctx::c_attr_get_values_nns() call\n");

	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}

FN_attrset *
DNS_ctx::c_attr_get_ids_nns(
	const FN_string &name,
	FN_status_csvc &cs)
{
	if (trace)
		fprintf(stderr, "DNS_ctx::c_attr_get_ids_nns() call\n");

	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}

FN_multigetlist *
DNS_ctx::c_attr_multi_get_nns(
	const FN_string &name,
	const FN_attrset *,
	FN_status_csvc &cs)
{
	if (trace)
		fprintf(stderr, "DNS_ctx::c_attr_multi_get_nns() call\n");

	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}

FN_valuelist *
DNS_ctx::c_attr_get_values(
	const FN_string &name,
	const FN_identifier &,
	FN_status_csvc &cs)
{
	if (trace)
		fprintf(stderr, "DNS_ctx::c_attr_get_values() call\n");

	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}

FN_attribute *
DNS_ctx::c_attr_get(
	const FN_string &name,
	const FN_identifier &,
	FN_status_csvc &cs)
{
	if (trace)
		fprintf(stderr, "DNS_ctx::c_attr_get() call\n");

	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}

FN_attrset *
DNS_ctx::c_get_attrs_nns(const FN_string &name, FN_status_csvc &cs)
{
	if (trace)
		fprintf(stderr, "DNS_ctx::c_get_attrs_nns() call\n");

	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}

FN_attrset *
DNS_ctx::c_attr_get_ids(const FN_string &name, FN_status_csvc &cs)
{
	if (trace)
		fprintf(stderr, "DNS_ctx::c_attr_get_ids() call\n");

	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}

FN_multigetlist *
DNS_ctx::c_attr_multi_get(
	const FN_string &name,
	const FN_attrset *,
	FN_status_csvc &cs)
{
	if (trace)
		fprintf(stderr, "DNS_ctx::c_attr_multi_get() call\n");

	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *self_reference, name);
	return (0);
}
