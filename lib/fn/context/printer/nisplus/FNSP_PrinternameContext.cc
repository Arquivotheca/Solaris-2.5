/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident "@(#)FNSP_PrinternameContext.cc	1.6 95/02/06 SMI"

#include <xfn/fn_p.hh>
#include <xfn/fn_printer_p.hh>
#include "../FNSP_printer_Syntax.hh"
#include "../FNSP_PrinternameContext.hh"

#define	oncPREFIX "onc_"
#define	oncPRELEN (13)
#define	CSIZE 1024
#define	DEFAULT_FILE "/etc/printers.conf"

static const FN_string null_name((unsigned char *)"");
static const FN_string internal_name((unsigned char *) "printers");

FNSP_PrinternameContext::~FNSP_PrinternameContext()
{
	if (my_reference) delete my_reference;
}

FNSP_PrinternameContext::FNSP_PrinternameContext(const FN_ref_addr &,
    const FN_ref& from_ref)
{
	my_reference = new FN_ref(from_ref);
	my_syntax = FNSP_printer_Syntax(
	    FNSP_printername_context_files)->get_syntax_attrs();
}

FNSP_PrinternameContext*
FNSP_PrinternameContext::from_address(const FN_ref_addr& from_addr,
    const FN_ref& from_ref, FN_status& stat)
{
	FNSP_PrinternameContext *answer =
	    new FNSP_PrinternameContext(from_addr, from_ref);

	if ((answer) && (answer->my_reference))
		stat.set_success();
	else {
		if (answer) {
			delete answer;
			answer = 0;
		}
		stat.set(FN_E_INSUFFICIENT_RESOURCES);
	}
	return (answer);
}

FN_ref*
FNSP_PrinternameContext::get_ref(FN_status& stat) const
{
	stat.set_success();
	return (new FN_ref(*my_reference));
}

#ifdef DEBUG
FNSP_PrinternameContext::FNSP_PrinternameContext(const FN_ref &from_ref)
{
	my_reference = new FN_ref(from_ref);
	my_syntax = FNSP_printer_Syntax(
	    FNSP_printername_context_files)->get_syntax_attrs();
}
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

int get_entry(const char *file, const unsigned char *name, char **value);

FN_ref*
FNSP_PrinternameContext::resolve(const FN_string &aname, FN_status_csvc& cstat)
{
	FN_ref* ref = 0;
	char *value;

	if (get_entry(DEFAULT_FILE, aname.str(), &value) == 0) {
		ref = get_service_ref_from_value(internal_name, value);
		free(value);
	}

	if (ref == 0)
		cstat.set_error(FN_E_NAME_NOT_FOUND, *my_reference, aname);
	else
		cstat.set_success();
	return (ref);
}

char * get_name(char *in, char **tok);
file_map(const char *file, char **buffer);
int get_line(char *entry, char *buffer, int pos, int size);

FN_nameset *
FNSP_PrinternameContext::list(FN_status_csvc& cstat)
{
	int	size, pos = 0;
	char	entry[CSIZE];
	char	*buffer, *name, *p, *tmp;

	FN_nameset *ns = new FN_nameset;
	if ((size = file_map(DEFAULT_FILE, &buffer)) < 0) {
		// File not found, hence set success and
		// return empty name set
		cstat.set_success();
		return (ns);
	}

	do {
		pos = get_line(entry, buffer, pos, size);
		tmp = p = strdup(entry);
		while ((p = get_name(p, &name)) || name) {
			ns->add((unsigned char *)name);
			if (p == 0)
				break;
		}
		free(tmp);
	} while (pos <= size);

	(void) munmap(buffer, size);
	cstat.set_success();

	return (ns);
}

FN_bindingset *
FNSP_PrinternameContext::list_bs(FN_status_csvc& cstat)
{
	int	size, pos = 0;
	char	entry[BUFSIZ];
	char	*buffer, *name, *p, *tmp;

	FN_bindingset* bs = new FN_bindingset;
	if ((size = file_map(DEFAULT_FILE, &buffer)) < 0) {
		// File not found, hence set as success
		// and return empty set
		cstat.set_success();
		return (bs);
	}

	FN_ref *ref;
	do {
		pos = get_line(entry, buffer, pos, size);
		tmp = p = strdup(entry);
		while ((p = get_name(p, &name)) || name) {
			char *in;
			in = strdup(entry);
			ref = get_service_ref_from_value(internal_name, in);
			if (ref) {
				bs->add((unsigned char *)name, *ref);
				delete ref;
			}
			free(in);
			if (p == 0)
				break;
		}
		free(tmp);
	} while (pos <= size);

	(void) munmap(buffer, size);
	cstat.set_success();

	return (bs);
}

FN_ref *
FNSP_PrinternameContext::c_lookup(const FN_string &name, unsigned int,
    FN_status_csvc &cstat)
{
	if (name.is_empty()) {
		FN_ref *answer = 0;

		// No name was given; resolves to current reference of context
		answer = new FN_ref(*my_reference);
		if (answer)
			cstat.set_success();
		else
			cstat.set_error(FN_E_INSUFFICIENT_RESOURCES,
			    *my_reference, null_name);
		return (answer);
	} else
		return (resolve(name, cstat));
}

FN_namelist *
FNSP_PrinternameContext::c_list_names(const FN_string &name,
    FN_status_csvc &cstat)
{
	if (name.is_empty()) {
		cstat.set_success();
		FN_nameset* ns = list(cstat);
		if (ns)
			return (new FN_namelist_svc(ns));
		else
			return (0);
	}

	cstat.set_error(FN_E_NAME_NOT_FOUND, *my_reference, name);
	return (0);
}

FN_bindinglist *
FNSP_PrinternameContext::c_list_bindings(const FN_string &name,
    FN_status_csvc &cstat)
{
	if (name.is_empty()) {
		cstat.set_success();
		FN_bindingset *bs = list_bs(cstat);
		if (bs)
			return (new FN_bindinglist_svc(bs));
		else
			return (0);
	}

	cstat.set_error(FN_E_NAME_NOT_FOUND, *my_reference, name);
	return (0);
}

int
FNSP_PrinternameContext::c_bind(const FN_string &name, const FN_ref&,
    unsigned, FN_status_csvc &cstat)
{
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_PrinternameContext::c_unbind(const FN_string &name, FN_status_csvc &cstat)
{
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_PrinternameContext::c_rename(const FN_string &name,
    const FN_composite_name&,
    unsigned, FN_status_csvc &cstat)
{
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_ref*
FNSP_PrinternameContext::c_create_subcontext(const FN_string &name,
    FN_status_csvc &cstat)
{
	// Not supported for files/nis
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_PrinternameContext::c_destroy_subcontext(const FN_string &name,
    FN_status_csvc &cstat)
{
	// Not supported for files/nis
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_attrset*
FNSP_PrinternameContext::c_get_syntax_attrs(const FN_string &name,
    FN_status_csvc &cstat)
{
	FN_attrset* answer;
	if (name.is_empty()) {
		answer = FNSP_printer_Syntax(
		    FNSP_printername_context_files)->get_syntax_attrs();

		if (answer) {
			cstat.set_success();
			return (answer);
		} else
			cstat.set_error(FN_E_INSUFFICIENT_RESOURCES,
			    *my_reference, name);
	} else {
		FN_ref *ref = resolve(name, cstat);

		if ((cstat.is_success()) || (name.is_empty())) {
			FN_attrset* answer =
			    FNSP_printer_Syntax(
			    FNSP_printername_context_files)->get_syntax_attrs();
			delete ref;
			if (answer)
				return (answer);
			cstat.set_error(FN_E_INSUFFICIENT_RESOURCES, *my_reference,
			    name);
		}
	}
	return (0);
}


FN_attribute*
FNSP_PrinternameContext::c_attr_get(const FN_string &name, const FN_identifier&,
    FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_PrinternameContext::c_attr_modify(const FN_string &name,
    unsigned int, const FN_attribute&, FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_valuelist*
FNSP_PrinternameContext::c_attr_get_values(const FN_string &name,
    const FN_identifier&, FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_attrset*
FNSP_PrinternameContext::c_attr_get_ids(const FN_string &name,
    FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_multigetlist*
FNSP_PrinternameContext::c_attr_multi_get(const FN_string &name,
    const FN_attrset *, FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_PrinternameContext::c_attr_multi_modify(const FN_string &name,
    const FN_attrmodlist&, FN_attrmodlist **, FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

// == Lookup (name:)
FN_ref*
FNSP_PrinternameContext::c_lookup_nns(const FN_string &name,
    unsigned int, FN_status_csvc &cstat)
{
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_namelist*
FNSP_PrinternameContext::c_list_names_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_bindinglist *
FNSP_PrinternameContext::c_list_bindings_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}


// Does it make sense to allow bind_nns, given that we hardwire
// where its contexts are stored (under org_ctx_dir)?  Probably not.
int
FNSP_PrinternameContext::c_bind_nns(const FN_string &name,
    const FN_ref &, unsigned, FN_status_csvc &cstat)
{
	// Not supported for files/nis
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_PrinternameContext::c_unbind_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	// Not supported for files/nis
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_PrinternameContext::c_rename_nns(const FN_string &name,
    const FN_composite_name &, unsigned, FN_status_csvc &cstat)
{
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_ref*
FNSP_PrinternameContext::c_create_subcontext_nns(const FN_string &name,
    FN_status_csvc &cstat)

{
	// Not supported for files/nis
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}


int
FNSP_PrinternameContext::c_destroy_subcontext_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	// Not supported for files/nis
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (cstat.is_success());
}

FN_attrset*
FNSP_PrinternameContext::c_get_syntax_attrs_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	FN_ref* nns_ref = c_lookup_nns(name, 0, cstat);

	if (cstat.is_success()) {
		FN_attrset* answer =
		    FNSP_printer_Syntax(
		    FNSP_printername_context_files)->get_syntax_attrs();
		delete nns_ref;
		if (answer)
			return (answer);
		cstat.set(FN_E_INSUFFICIENT_RESOURCES);
	}
	return (0);
}

FN_attribute*
FNSP_PrinternameContext::c_attr_get_nns(const FN_string &name,
    const FN_identifier &, FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_PrinternameContext::c_attr_modify_nns(const FN_string &name,
    unsigned int, const FN_attribute &, FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_valuelist*
FNSP_PrinternameContext::c_attr_get_values_nns(const FN_string &name,
    const FN_identifier&, FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_attrset*
FNSP_PrinternameContext::c_attr_get_ids_nns(const FN_string &name,
    FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_multigetlist*
FNSP_PrinternameContext::c_attr_multi_get_nns(const FN_string &name,
    const FN_attrset *, FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_PrinternameContext::c_attr_multi_modify_nns(const FN_string &name,
    const FN_attrmodlist &, FN_attrmodlist **, FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

#include <rpc/types.h>
#include <rpc/xdr.h>

FN_ref*
get_service_ref_from_value(const FN_string &intname, char *value)
{
	int	eqfound = 0, endofline = 0;
	char reft[CSIZE];
	char	*typestr, *p = value;
	FN_ref	*ref = 0;
	XDR	xdr;

	if ((intname.charcount() + oncPRELEN) >= CSIZE)
		return (ref);
	sprintf(reft, "%s%s", oncPREFIX, intname.str());

	const FN_identifier *reftype = new FN_identifier((unsigned char *)reft);
	ref = new FN_ref(*reftype);
	delete reftype;
	if (ref == 0)
		return (0);

	while (*p != ':' && *p != '\0' && *p != '\n') // skip aliases
		p++;
	if (*p == '\0' || *p == '\n') {
		delete ref;
		return (0);
	}

	typestr = ++p;
	do {
		char *addrstr, addrt[CSIZE];
		u_char buffer[CSIZE];

		if (*p == '=') {
			if (eqfound) // error: no : since last =
				return (0);
			eqfound = 1;
			*p++ = '\0';
			addrstr = p;
			if ((strlen(reft) + strlen(typestr) + 1) >= CSIZE)
				return (0);
			sprintf(addrt, "%s_%s", reft, typestr);
		} else if (*p == ':' || *p == '\0' || *p == '\n') {
			if (eqfound == 0) {
				// end without =, empty address
				if (*p == ':') {
					p++;
					typestr = p;
				} else
					return (ref);
			} else {
				if (*p == '\0' || *p == '\n')
					endofline++;
				else
					*p++ = '\0'; // found end of an address

				xdrmem_create(&xdr, (caddr_t)buffer, CSIZE,
				    XDR_ENCODE);
				if (xdr_string(&xdr, &addrstr, ~0) == FALSE)
					return (0);
				FN_identifier addrtype((unsigned char *)addrt);
				FN_ref_addr addr(addrtype, xdr_getpos(&xdr),
						 (void *)buffer);
				ref->append_addr(addr);
				xdr_destroy(&xdr);

				if (endofline)
					return (ref);
				eqfound = 0;
				typestr = p;
			}
		} else
			p++;
	} while (1);
}

/*
 * name_match will attempt to match a name in the name list of a printer
 * entry.  If the name is found, a copy of the entry is returned.
 * If the name is not found, NULL is returned.  A printer entry is
 * assumed to look like name1|name2|...:options.
 */
static char *
name_match(char *entry, const unsigned char *key)
{
	char	*s, *tmp, *last, *ret = 0;
	int good = 0, found = 0;

	if ((tmp = s = strdup(entry)) == 0)
		return (ret);

	last = s;
	while (*s != '\0' && *s != ':') {
		if (*s == '|') {
			if (found) {
				s++;
				continue;
			}
			*s = '\0';
			if (strcmp(last, (const char *)key) == 0) {
				found++;
				ret = tmp;
				*s++ = '|';
			} else {
				*s++ = '|';
				last = s;
			}
			continue;
		}
		s++;
	}
	if (*s == '\0') {
		free(tmp);
		return (0); // ":" not found;
	}
	if (*s == ':' && found == 0) {
		*s = '\0';
		if (strcmp(last, (const char *)key) == 0) {
			ret = tmp;
			*s = ':';
		} else
			free(tmp);
	}
	return (ret);
}

char *
get_name(char *in, char **tok)
{
	char *p = in;

	while (*p != '|' && *p != '\0' && *p != ':')
		p++;
	switch (*p) {
	case '|':
		*p = '\0';
		*tok = in;
		return (++p);
	case ':':
		*p = '\0';
		*tok = in;
		return (0);
	case '\0':
		*tok = 0;
		return (0);
	}
}

int
file_map(const char *file, char **buffer)
{
	int fd;
	struct stat st;

	if ((fd = open(file, O_RDONLY)) < 0)
		return (-1);

	if (fstat(fd, &st) < 0) {
		(void) close(fd);
		return (-1);
	}

	if ((*buffer = mmap((caddr_t)0, (size_t)st.st_size, PROT_READ,
		(MAP_PRIVATE|MAP_NORESERVE), fd, (off_t)0)) == MAP_FAILED) {
		(void) close(fd);
		return (-1);
	}

	(void) close(fd);
	return (st.st_size);
}

int
get_line(char *entry, char *buffer, int pos, int size)
{
	int busy = 1;
	int entrypos = 0;

	while ((pos <= size) && (entrypos < BUFSIZ - 1) && (busy))
		switch (buffer[pos]) {
		case '#':
			while (buffer[pos] != '\n' && pos <= size)
				pos++;
			break;
		case '\\':		/* XXX backslash in value? */
			pos += 2;
			break;
		case ' ':
		case '\t':
			pos++;
			break;
		case '\n':
			pos++;
			busy = 0;
			break;
		default:
			entry[entrypos++] = buffer[pos++];
		}
	entry[entrypos] = 0;
	return (pos);
}

/*
 * get_entry gets the printer entry from the file that matches the key
 * passed in.  The file is assumed to be printers format.
 */
int
get_entry(const char *file, const unsigned char *name, char **value)
{
	int	size, pos = 0;
	char	entry[BUFSIZ];
	char	*buffer;

	if ((size = file_map(file, &buffer)) < 0)
		return (1);

	do {
		pos = get_line(entry, buffer, pos, size);
	} while ((*value = name_match(entry, name)) == NULL && pos <= size);

	(void) munmap(buffer, size);

	if (*value == NULL)
		return (1);
	return (0);
}
