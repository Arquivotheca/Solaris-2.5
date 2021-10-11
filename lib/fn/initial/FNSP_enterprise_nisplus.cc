/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)FNSP_enterprise_nisplus.cc	1.7 94/10/19 SMI"

#include <rpcsvc/nis.h>
#include <rpcsvc/nislib.h>
#include <xfn/xfn.h>
#include <synch.h>
#include <unistd.h>

#include "FNSP_enterprise_nisplus.hh"

#define	PASSWD_BUF_SIZE 256

FNSP_enterprise_nisplus::FNSP_enterprise_nisplus()
{
	root_directory = 0;
	mutex_init(&root_directory_lock, USYNC_THREAD, 0);
	my_address_type = new
	    FN_identifier((const unsigned char *)NISPLUS_ADDRESS_STR);
}

const FN_identifier*
FNSP_enterprise_nisplus::get_addr_type()
{
        return (my_address_type);
}
 
/* *********** functions to deal with NIS+ root **************** */


extern "C" nis_name __nis_local_root();

int
FNSP_enterprise_nisplus::root_orgunit_p(const FN_string &name)
{
	mutex_lock(&root_directory_lock);
	if (root_directory == 0) {
		mutex_unlock(&root_directory_lock);
		return (0);
	} else {
		if (root_directory->compare(name) == 0) {
			mutex_unlock(&root_directory_lock);
			return (1);
		} else {
			mutex_unlock(&root_directory_lock);
			return (0);
		}
	}
}

const FN_string*
FNSP_enterprise_nisplus::set_root_orgunit_name(const FN_string &newroot)
{
	if (root_directory) {
		delete root_directory;
		root_directory = 0;
	}
	root_directory = new FN_string(newroot);
	return (root_directory);
}

const FN_string*
FNSP_enterprise_nisplus::get_root_orgunit_name()
{
	char *rootdir;

	mutex_lock(&root_directory_lock);
	if ((root_directory == 0) && (rootdir = __nis_local_root())) {
		const FN_string *r =
		    set_root_orgunit_name((unsigned char *)rootdir);
		mutex_unlock(&root_directory_lock);
		return (r);
	} else {
		mutex_unlock(&root_directory_lock);
		return (root_directory);
	}
}

/*
 * functions to deal with user related information
 */

// 'UserOrgUnit' is derived from user's NIS+ principal name.
// If user's principal name does not contain a domain name (e.g. 'nobody'),
// the host's domain is used instead.

FN_string*
FNSP_enterprise_nisplus::get_user_orgunit_name(uid_t target_uid)
{
	uid_t saved_uid = geteuid();

	if (saved_uid != target_uid) {
		if (seteuid(target_uid) != 0) {
			// could not set uid to target
			return (0);
		}
	}

	char *principal_name = nis_local_principal();
	char *principal_domain = 0;

	if (principal_name) {
		principal_domain = nis_domain_of(principal_name);
		if (principal_domain && *principal_domain == '.')
			principal_domain = 0;  // invalid domain name
	}
	// If cannot determine principal domain, use machine's directory

	if (principal_domain == 0)
		principal_domain = nis_local_directory();

	FN_string *orgunit = new
	    FN_string((unsigned char *)(principal_domain));

	// restore original uid
	if (saved_uid != target_uid)
		seteuid(saved_uid);

	return (orgunit);
}

#include <string.h>
#include <pwd.h>
// #include <sys/types.h>

static inline int
is_nobody(const char *pname)
{
	return (strcmp(pname, "nobody") == 0);
}

FN_string*
FNSP_enterprise_nisplus::get_user_name(uid_t target_uid)
{
	uid_t saved_uid = geteuid();

	if (saved_uid != target_uid) {
		if (seteuid(target_uid) != 0) {
			// could not set uid to target
			return (0);
		}
	}

	char *principal_name = nis_local_principal();
	const int max_username_len = 64;
	char username[max_username_len];
	username[0] = '\0';

	if (principal_name && !is_nobody(principal_name)) {
		nis_leaf_of_r(principal_name, username, max_username_len);
	}

	// could not get user name from principal name (e.g. nobody)
	// extract from passwd entry
	if (username[0] == '\0') {
		struct passwd pw;
		char buffer[PASSWD_BUF_SIZE];
		if (getpwuid_r(target_uid, &pw, buffer, PASSWD_BUF_SIZE)
		    != NULL && pw.pw_name != NULL)
			strcpy(username, pw.pw_name);
	}

	// restore original uid
	if (saved_uid != target_uid)
		seteuid(saved_uid);

	if (username[0] == '\0')
		return (0);
	else
		return (new FN_string((unsigned char *)username));
}


// ************* functions to determine information related to host ***

FN_string*
FNSP_enterprise_nisplus::get_host_orgunit_name()
{
	char *domainname = nis_local_directory();

	FN_string* hostorgunit_name = new
	    FN_string((unsigned char *)(domainname ? domainname : ""));

	return (hostorgunit_name);
}

FN_string*
FNSP_enterprise_nisplus::get_host_name()
{
	char *nlh = nis_local_host();
	FN_string full_hostname((unsigned char *)(nlh? nlh : ""));

	int first_dot = full_hostname.next_substring((unsigned char *)".");
	FN_string *hostname = new FN_string(full_hostname,
	    FN_STRING_INDEX_FIRST,
	    (first_dot == FN_STRING_INDEX_NONE) ?
	    FN_STRING_INDEX_LAST : first_dot - 1);
	return (hostname);
}
