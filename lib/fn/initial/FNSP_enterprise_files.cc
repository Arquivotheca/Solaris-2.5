/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident "@(#)FNSP_enterprise_files.cc	1.3 94/11/08 SMI"

#include <xfn/xfn.hh>
#include "FNSP_enterprise_files.hh"

#define	FILES_ADDRESS_STR "onc_fn_files"
static const FN_identifier
my_addr_type_str((const unsigned char *)FILES_ADDRESS_STR);

const FN_identifier*
FNSP_enterprise_files::get_addr_type()
{
	return (&my_addr_type_str);
}


FNSP_enterprise_files::FNSP_enterprise_files(const FN_string &domain)
{
	root_directory = new FN_string(domain);
}

FNSP_enterprise_files::~FNSP_enterprise_files()
{
	delete root_directory;
}

const FN_string*
FNSP_enterprise_files::get_root_orgunit_name()
{
	return (root_directory);
}

FN_string*
FNSP_enterprise_files::get_user_orgunit_name(uid_t)
{
	FN_string *orgunit = new FN_string(*root_directory);
	return (orgunit);
}

FN_string*
FNSP_enterprise_files::get_user_name(uid_t)
{
	return (0);
}

FN_string*
FNSP_enterprise_files::get_host_orgunit_name()
{
	FN_string *orgunit = new FN_string(*root_directory);
	return (orgunit);
}

FN_string*
FNSP_enterprise_files::get_host_name()
{
	return (0);
}
