/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident "@(#)fncreate_printer.cc	1.8 95/02/02 SMI"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <strings.h>
#include <locale.h>
#include <libintl.h>
#include <sys/wait.h>

#include <xfn/xfn.hh>
#include <xfn/fn_p.hh>
#include <xfn/fn_printer_p.hh>
#include <fnsp_printer_internal.hh>
#include <FNSP_printer_Address.hh>

#include <rpc/types.h>
#include <rpc/xdr.h>
#include <sys/mman.h>

#define	BUFSIZE 1024
#define	oncPREFIX "onc_"
#define	oncPREFIX1 "onc_printers"
#define	oncPRELEN 4

static const
FN_string empty_string((const unsigned char *) "");

static const
FN_string service_string((const unsigned char *) "service");

static const
FN_string printer_string((const unsigned char *) "printer");

static const
FN_string internal_name((const unsigned char *) "printers");

// Global variables from command line
static int check_files = 0;
static int check_command = 0;
static int verbose = 0;
static int supercede = 0;

#define	DEFAULT_FILE_NAME "/etc/printers.conf"
static char *file_name;
static char *printer_ctx_name;
static char *printer_name;
static const unsigned char **printer_address;
static int num_printer_address;

static char *fncreate_path = "/usr/sbin/fncreate";

// external functions
extern FN_ref*
get_service_ref_from_value(const FN_string &, char *);

extern int
file_map(const char *file, char **buffer);

extern int
get_line(char *entry, char *buffer, int pos, int size);

extern char *
get_name(char *in, char **tok);

extern int
get_entry(const char *file, const unsigned char *name, char **value);

static void
check_error(FN_status &status, char *msg = 0)
{
	char *message = gettext(msg);
	if (!status.is_success()) {
		if (msg)
			printf("%s\n", msg);
		fprintf(stderr, gettext("Error: %s\n"),
		    status.description()->str());
		exit(1);
	}
}

static void
usage(char *cmd, char *msg = 0)
{
	if (msg) {
		char *message = gettext(msg);
		fprintf(stderr, "%s: %s\n", cmd, msg);
	}
	fprintf(stderr,
	    gettext("Usage:\t%s [-vs] compositename printername "), cmd);
	fprintf(stderr, gettext("printeraddr [printeraddr ...]\n"));
	fprintf(stderr, gettext("Usage:\t%s [-vs] [-f filename] compositename\n"),
	    cmd);
	exit(1);
}

static void
process_cmd_line(int argc, char **argv)
{
	int c;
	while ((c = getopt(argc, argv, "f:vs")) != -1) {
		switch (c) {
		case 'v' :
			verbose = 1;
			break;
		case 's' :
		supercede = 1;
			break;
		case 'f':
			check_files = 1;
			file_name = optarg;
			break;
		case '?':
			default :
			usage(argv[0], "invalid option");
		}
	}

	if (optind >= argc)
		usage(argv[0], "Insufficient command line arguments");

	if (check_files) {
		if (argc == (optind + 1)) {
			printer_ctx_name = strdup(argv[optind++]);
			return;
		} else
			usage(argv[0], "Too many arguments");
	}

	if (argc == (optind + 1)) {
		check_files = 1;
		file_name = DEFAULT_FILE_NAME;
		printer_ctx_name = strdup(argv[optind++]);
		return;
	}

	if (argc == (optind + 2)) {
		usage(argv[0], "Insufficient command line arguments");
	}

	check_command = 1;
	printer_ctx_name = strdup(argv[optind++]); 
	printer_name = strdup(argv[optind++]);
	printer_address = (const unsigned char **) &argv[optind];
	num_printer_address = argc - optind;
}

static void
print_command_line()
{
	if (check_files)
		fprintf(stdout,
		    gettext("Bindings read form file: %s\n"),
		    file_name);

	if (check_command) {
		fprintf(stdout, gettext("Bindings from command line\n"));
		fprintf(stdout, gettext("Printer name: %s\n"),
		    printer_name);
		fprintf(stdout, gettext("Number of address: %d\n"),
		    num_printer_address);
		for (int i = 0; i < num_printer_address; i++)
			fprintf(stdout,
			    gettext("Printer address: %s\n"),
			    printer_address[i]);
	}
	fprintf(stdout, gettext("Printer context: %s\n"),
	    printer_ctx_name);
	if (verbose)
		fprintf(stdout, gettext("Verbose flag turned ON\n"));
	if (supercede)
		fprintf(stdout, gettext("Supercede flag turned ON\n"));
}

static FN_ref *
check_printer_name_context(FN_ctx *initial, FN_status &status)
{
	FN_ref *answer;

	FN_string printer_str((unsigned char *) printer_ctx_name);
	FN_composite_name fullname(printer_str);
	FN_composite_name *append_to_printer = 0;
	FN_composite_name *printer_comp = 0;

	// Parse the composite name and remove the service and
	// printer contexts if the exist
	// First remove the empty space if present
	void *ip;
	const FN_string *last_comp = fullname.last(ip);
	if ((last_comp) && (!last_comp->compare(empty_string))) {
		fullname.next(ip); // points after the last comp
		fullname.delete_comp(ip); // deletes the last comp
		last_comp = fullname.last(ip);
	}

	// If the last component is "service" remove it
	if ((last_comp) && (!last_comp->compare(service_string)))
		printer_comp = fullname.prefix(ip);

	// Search for "service/printer"
	if (!printer_comp) { 
		// Service name not found, look for service/printer
		while ((last_comp) && (last_comp->compare(printer_string))) {
			if (!append_to_printer)
				append_to_printer = new FN_composite_name;
			append_to_printer->prepend_comp(*last_comp);
			last_comp = fullname.prev(ip);
		}
		last_comp = fullname.prev(ip);
		if ((last_comp) && (!last_comp->compare(service_string))) {
			// Found the combination service/printer
			printer_comp = fullname.prefix(ip);
		} else {
			delete append_to_printer;
			append_to_printer = 0;
		}
	}

	// Reset the printer_name_ctx to the current value
	if (printer_comp) {
		// Construct the new printer context name
		const unsigned char *ctx_name = printer_comp->string()->str();
		// Delete the original printer context name
		if (printer_ctx_name)
			free(printer_ctx_name);
		printer_ctx_name = strdup((char *) ctx_name);
		if (append_to_printer) {
			char *old_printer_name = printer_name;
			if (old_printer_name)
				printer_name = new char[strlen(old_printer_name) +
				    append_to_printer->string()->bytecount() + 2];
			else
				printer_name = new char[
				    append_to_printer->string()->bytecount() + 2];
			strcpy(printer_name,
			    (char *) append_to_printer->string()->str());
			if (old_printer_name) {
				strcat(printer_name, "/");
				strcat(printer_name, old_printer_name);
				free(old_printer_name);
			}
			delete append_to_printer;
		}	
	} else
		printer_comp = new FN_composite_name(fullname);

	// If the fisrt component of the printer_comp is org,
	// then we need to add "/" at the end in order to get the
	// correct context. Hence as a default "/" is added before the
	// lookup. Bug Fix #190005

	printer_comp->append_comp(empty_string);

	if (verbose)
		printf(gettext("Lookup performed on %s\n"),
		    printer_comp->string()->str());

	answer = initial->lookup(*printer_comp, status);
	if (!status.is_success()) {
		printf(gettext("Context \"%s\" does not exist\n"),
		    printer_comp->string()->str());
		printf(
		    gettext("Use fncreate command to create\n"));
		check_error(status);
	}
	delete printer_comp;
	return (answer);
}

static void
print_service_error()
{
	fprintf(stderr,
	    gettext("Unable to create service context\n"));
	fprintf(stderr,
	    gettext("Use fncreate command to create service context\n"));
}
	

static FN_ref*
append_service_context(FN_ctx *initial_ctx)
{
	FN_status status;
	FN_ref *answer;
	FN_composite_name service_comp(service_string);

	answer = initial_ctx->lookup(service_comp, status);
	if (!status.is_success()) {
		// Fork a child process and to create the service context
		// Parent should wait until the child exits, and do
		// lookup again.
		pid_t child;
		if ((child = fork()) == -1) {
			// Error in forking
			print_service_error();
			check_error(status);
		}
		// Fork successful
		if (child == 0) {
			char *composite_name = new char[
			    strlen((char *) printer_ctx_name) +
			    strlen((char *) service_string.str()) + 2];
			strcpy(composite_name, (char *) printer_ctx_name);
			strcat(composite_name, "/");
			strcat(composite_name, (char *) service_string.str());
			execl(fncreate_path, "fncreate", "-t", "service",
			    composite_name, (char *) 0);
			// Should not return, hence
			print_service_error();
		} else {
			// Wait for the child process to terminate
			int child_status;
			wait(&child_status);

			// Do a lookup on service context
			answer = initial_ctx->lookup(service_comp, status);

			// If in error again, giveup
			if (!status.is_success()) {
				print_service_error();
				check_error(status);
			}
		}
	}
	return (answer);
}

static FN_ref*
append_printer_context(FN_ctx *service_ctx,
    FN_ref *default_printer)
{
	FN_status status;
	FN_ref *answer;
	int create_printer_context = 0;
	if (verbose)
		printf(gettext("Lookup performed on printer context\n"));
	FN_composite_name printer_comp(printer_string);
	answer = service_ctx->lookup(printer_comp, status);
	if (!status.is_success())
		create_printer_context = 1;
	else if (FNSP_printer_context_exists(*answer) == FN_E_NOT_A_CONTEXT) {
		service_ctx->unbind(printer_comp, status);
		check_error(status, "Unable to create printer context");
		create_printer_context = 1;
	}
	if (create_printer_context) {
		if (verbose)
			fprintf(stdout,
			    gettext("Printer context does not exist...creating\n"));
		// Obtain the reference of the service context
		FN_ref *service_ref = service_ctx->get_ref(status);
		check_error(status,
		    "Unable to obtain reference to service context");

		// Obtain the printer_Address format for service reference
		FNSP_printer_Address *printer_service_address = new
		    FNSP_printer_Address(
		    *FNSP_reference_to_internal_name(*service_ref),
		    FNSP_printername_context);
		delete service_ref;

		unsigned service_status;
		answer = FNSP_printer_create_and_bind(*printer_service_address,
		    printer_string, FNSP_printername_context, FNSP_normal_repr,
		    service_status, 1,
		    FNSP_printer_reftype_from_ctxtype(
		    FNSP_printername_context));
		status.set_code(service_status);
		check_error(status, "Unable to create printer name context");

		// Append the default printers
		void *ip;
		const FN_ref_addr *default_addr	=
			default_printer->first(ip);
		while (default_addr) {
			answer->append_addr(*default_addr);
			default_addr = default_printer->next(ip);
		}

		// Add the new reference to the printer bindings
		service_status = FNSP_printer_add_binding(
		    *printer_service_address, printer_string, *answer, 0);
		status.set_code(service_status);
		check_error(status, "Unable to bind printer object");
		delete printer_service_address;
	}
	return (answer);
}

FN_ref_addr*
get_printer_addr_from_value(const unsigned char *value)
{
	char	*typestr, *p;
	XDR	xdr;
	FN_ref_addr *answer = 0;

	char *addrstr, addrt[BUFSIZE];
	u_char buffer[BUFSIZE];

	p = (char *) value;
	typestr = p;
	do {
		if (*p == '=') {
			*p++ = '\0';
			addrstr = p;

			// Create the address identifier
			if ((strlen(oncPREFIX1) + strlen(typestr)) >= BUFSIZE)
				return (0);
			sprintf(addrt, "%s_%s", oncPREFIX1, typestr);
			FN_identifier addrtype((unsigned char *)addrt);

			// XDR the adddress
			xdrmem_create(&xdr, (caddr_t)buffer, BUFSIZE,
			    XDR_ENCODE);
			if (xdr_string(&xdr, &addrstr, ~0) == FALSE)
				return (0);

			// Create the FN_ref_addr
			answer = new FN_ref_addr(addrtype, xdr_getpos(&xdr),
			    (void *) buffer);
			xdr_destroy(&xdr);

			return (answer);
		} else
			p++;
	} while (*p != '\0');
	return (0);
}

FN_ref *
obtain_printer_object_ref()
{
	FN_ref_addr *printer_addr;
	FN_ref *answer = new FN_ref((unsigned char *) oncPREFIX1);

	for (int i = 0; i < num_printer_address; i++) {
		printer_addr =
		    get_printer_addr_from_value(printer_address[i]);
		if (!printer_addr) {
			fprintf(stderr,
			    gettext("Incorrect printer address: %s\n"),
			    printer_address[i]);
			exit(1);
		}
		answer->append_addr(*printer_addr);
		delete printer_addr;
	}
	return (answer);
}

FN_ref *
create_inter_printers(const FN_string *printer_name, FN_ctx *ctx,
    FN_ref *printer_ref)
{
	FN_status status;
	FN_ref *answer;

	if (verbose)
		fprintf(stdout, gettext("Lookup performed on %s\n"),
		    printer_name->str());
	answer = ctx->lookup(*printer_name, status);
	if (!answer) {
		if (verbose)
			fprintf(stdout,
			    gettext("Printer %s does not exist...creating\n"),
			    printer_name->str());
		answer = ctx->create_subcontext(*printer_name, status);
		check_error(status, "Unable to create printer object context");

		// Append the default printers
		void *ip;
		const FN_ref_addr *default_addr	= printer_ref->first(ip);
		while (default_addr) {
			answer->append_addr(*default_addr);
			default_addr = printer_ref->next(ip);
		}
		ctx->bind(*printer_name, *answer, 0, status);
		check_error(status, "Unable to bind printer object");
	}
	return (answer);
}


FN_ref *
create_final_printers(const FN_string *printer_name, FN_ctx *ctx,
    FN_ref *printer_ref)
{
	FN_status status;
	FN_ref *answer;

	if (verbose)
		fprintf(stdout, gettext("Lookup performed on %s\n"),
		    printer_name->str());
	answer = ctx->lookup(*printer_name, status);
	if (!answer) {
		if (verbose)
			fprintf(stdout,
			    gettext("Printer: %s not present...creating\n"),
			    printer_name->str());
		answer = ctx->create_subcontext(*printer_name, status);
		check_error(status, "Unable to create printer object context");

		// Append the default printers
		void *ip;
		const FN_ref_addr *default_addr	=
			printer_ref->first(ip);
		while (default_addr) {
			answer->append_addr(*default_addr);
			default_addr = printer_ref->next(ip);
		}
		ctx->bind(*printer_name, *answer, 0, status);
		check_error(status, "Unable to bind printer object");
	} else {
		// Check if the same address is present.
		// If present check for supercede flag
		fprintf(stdout, gettext("Printer: %s already exits\n"),
		    printer_name->str());
		if (verbose) {
	fprintf(stdout, gettext("Checking for same address type binding\n"));
		}
		void *ref_ip, *prt_ip;
		const FN_ref_addr *ref_address, *prt_address;
		const FN_identifier *ref_id, *prt_id;
		prt_address = printer_ref->first(prt_ip);

		while (prt_address) {
			prt_id = prt_address->type();
			ref_address = answer->first(ref_ip);
			while (ref_address) {
				ref_id = ref_address->type();
				if ((*ref_id) == (*prt_id)) {
					if (verbose)
	fprintf(stdout, gettext("Binding with \"%s\" address type exits\n"),
	    ref_address->type()->str());
					if (supercede) {
						if (verbose)
	fprintf(stdout, gettext("Overwritting\n"));
						answer->delete_addr(ref_ip);
						answer->insert_addr(ref_ip,
						    *prt_address);
					} else {
	fprintf(stderr, gettext("Use -s option to over-write\n"));
					}
					break;
				}
				ref_address = answer->next(ref_ip);
			}
			if (!ref_address)
				answer->append_addr(*prt_address);
			prt_address = printer_ref->next(prt_ip);
		}
		ctx->bind(*printer_name, *answer, 0, status);
		check_error(status, "Unable to bind printer object");
	}
	return (answer);
}

void static
create_printers(FN_ref *printer_ref, FN_status &status,
    FN_ref *printer_object_ref)
{
	FN_ctx *printer_ctx = FN_ctx::from_ref(*printer_ref, status);
	check_error(status, "Unable to obtain printer name context");

	// Obtain the printer object composite name
	// and traverse the components
	FN_string printer_string((unsigned char *) printer_name);
	FN_composite_name printer_comp(printer_string);
	const FN_string *prt_comp_string;
	const FN_string *prt_comp_string_next;
	FN_ref *prt_ref;
	void *ip;

	prt_comp_string = printer_comp.first(ip);
	prt_comp_string_next = printer_comp.next(ip);
	while (prt_comp_string) {
		if (prt_comp_string_next) {
			prt_ref =
				create_inter_printers(prt_comp_string,
				    printer_ctx, printer_object_ref);
			delete printer_ctx;
			printer_ctx =
			    FN_ctx::from_ref(*prt_ref, status);
			check_error(status, "Unable to obtain printer context");
			delete prt_ref;
		} else {
			prt_ref = create_final_printers(prt_comp_string,
			    printer_ctx, printer_object_ref);
			delete printer_ctx;
			delete prt_ref;
			exit(0);
		}
		prt_comp_string = prt_comp_string_next;
		prt_comp_string_next = printer_comp.next(ip);
	}
}

FN_ref *
create_files_inter_printers(FN_ref *printer_ref, FN_status &status,
    FN_ref *printer_object_ref)
{
	FN_ref *answer = new FN_ref(*printer_ref);

	// If printer_name is NULL return the reference
	if (!printer_name)
		return (answer);

	// Obtain the printer object composite name
	// and traverse the components
	FN_string printer_string((unsigned char *) printer_name);
	FN_composite_name printer_comp(printer_string);

	FN_ctx *printer_ctx;
	const FN_string *prt_comp_string;
	void *ip;

	prt_comp_string = printer_comp.first(ip);
	if (prt_comp_string) {
		printer_ctx = FN_ctx::from_ref(*answer, status);
		check_error(status, "Unable to obtain printer name context");
		delete answer;
	}
	while (prt_comp_string) {
		answer = create_inter_printers(prt_comp_string,
		    printer_ctx, printer_object_ref);
		delete printer_ctx;
		printer_ctx = FN_ctx::from_ref(*answer, status);
		check_error(status, "Unable to obtain printer context");
		prt_comp_string = printer_comp.next(ip);
		if (prt_comp_string)
			delete answer;
	}
	return (answer);
}

#include <rpcsvc/nis.h>

main(int argc, char **argv)
{
	// Internationalization
	setlocale(LC_ALL, "");
	textdomain("fncreate_printer");

	process_cmd_line(argc, argv);
	// print_command_line();

	char *local_dir = nis_local_directory();
	// Check for NIS+
	nis_result *res;
	char nis_domain[NIS_MAXNAMELEN+1];
	sprintf(nis_domain, "org_dir.%s", local_dir);
	res = nis_lookup(nis_domain, NO_AUTHINFO | USE_DGRAM);
	if (res->status != NIS_SUCCESS) {
		fprintf(stderr, gettext("NIS+ is not installed\n"));
		fprintf(stderr,
		    gettext("Cannot use this command in this environment\n"));
		exit(1);
	}

	// Check for ctx_dir
	sprintf(nis_domain, "ctx_dir.%s", local_dir);
	res = nis_lookup(nis_domain, NO_AUTHINFO | USE_DGRAM);
	if (res->status != NIS_SUCCESS) {
		fprintf(stderr, gettext("FNS not installed\n"));
		fprintf(stderr,
		    gettext("Use: fncreate -t org command to install\n"));
		exit(1);
	}

	FN_status status;
	FN_ctx *init_ctx = FN_ctx::from_initial(status);
	check_error(status, "Unable to get initial context");

	const FN_composite_name *remaining_name = 0;
	FN_ref *context_ref = check_printer_name_context(init_ctx, status);
	delete init_ctx;

	// Obtain the context in which the service/printer to be
	// created
	FN_ctx *context_ctx = FN_ctx::from_ref(*context_ref, status);
	check_error(status, "Unable to get printer context context");
	delete context_ref;

	// Check for the existence of the service context
	// If not present create one
	FN_ref *service_ref = append_service_context(context_ctx);
	delete context_ctx;

	// Obtain service context
	FN_ctx *service_ctx = FN_ctx::from_ref(*service_ref, status);
	check_error(status, "Unable to obtain service context");
	delete service_ref;

	FN_ref *printer_ref = 0;
	FN_ctx *printer_ctx = 0;
	if (check_command) {
		// Obtain the printer object reference
		FN_ref *printer_object_ref = obtain_printer_object_ref();

		// Check for printer context, if not present
		// create one and append the default printer
		printer_ref = append_printer_context(service_ctx,
		    printer_object_ref);
		delete service_ctx;
		create_printers(printer_ref, status, printer_object_ref);
		check_error(status, "Unable to create printers");
		exit(0);
	}


	// Files implementation
	int size, pos = 0;
	char entry[BUFSIZ];
	char *buffer, *name, *p, *tmp;
	FN_ref *ref;
	FN_string *name_str;

	if ((size = file_map(file_name, &buffer)) < 0) {
		fprintf(stderr, gettext("Insufficient resources (or) "));
		fprintf(stderr, gettext("File not found: "));
		fprintf(stderr, "%s\n", file_name);
		exit(1);
	}

	do {
		pos = get_line(entry, buffer, pos, size);
		tmp = p = strdup(entry);
		while ((p = get_name(p, &name)) || name) {
			char *in;
			in = strdup(entry);
			ref = get_service_ref_from_value(internal_name, in);

			if (ref) {
				if (!printer_ctx) {
					FN_ref *inter_ref = append_printer_context(
					    service_ctx, ref);
					delete service_ctx;

					// Update the intermediate printers
					printer_ref = create_files_inter_printers(
					    inter_ref, status, ref);
					delete inter_ref;

					// Obtain the final printer context
					printer_ctx = FN_ctx::from_ref(
					    *printer_ref, status);
					check_error(status,
					    "No printer name context");
					delete printer_ref;
				}
				name_str = new
				    FN_string((const unsigned char *) name);
				printer_ref = create_final_printers(name_str,
				    printer_ctx, ref);
				delete name_str;
			}
			free(in);
			if (p == 0)
				break;
		}
		free(tmp);
	} while (pos <= size);

	(void) munmap(buffer, size);
	exit(0);
}
