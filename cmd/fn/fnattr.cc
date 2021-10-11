/*
 * Copyright (c) 1993 - 1994 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)fnattr.cc	1.5 94/12/09 SMI"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <xfn/xfn.hh>
#include <rpcsvc/nis.h>

int option_specified = 0;
enum FNSP_attr_operation {
	FNSP_attr_null_operation = 0,
	FNSP_attr_add_operation,
	FNSP_attr_list_operation,
	FNSP_attr_delete_operation,
	FNSP_attr_modify_operation,
};
FNSP_attr_operation attribute_operation = FNSP_attr_null_operation;
int add_supersede = 0;
int id_dce_uuid = 0;
int id_iso_oid = 0;

const unsigned char *composite_name = 0;
const unsigned char *identifier = 0;
const unsigned char **values = 0;
int number_of_values = 0;

const FN_identifier
attribute_syntax((const unsigned char *) "fn_attr_syntax_ascii");

static void
check_error(FN_status &status, char *msg = 0)
{
	if (!status.is_success()) {
		if (msg)
			fprintf(stderr, "%s\n", msg);
		fprintf(stderr, "Error: %s\n", status.description()->str());
		exit(1);
	}
}

static void
usage(char *cmd, char *msg = 0)
{
	if (msg)
		fprintf(stderr, "%s: %s\n", cmd, msg);
	fprintf(stderr, "Usage:\t%s -a [-s] composite_name [-O|-U] identifier",
	    cmd);
	fprintf(stderr, " [value1 value2 ...]\n");
	fprintf(stderr,
	    "Usage:\t%s -l|-v composite_name [[-O|-U] identifier]\n",
	    cmd);
	fprintf(stderr,
	    "Usage:\t%s -d composite_name [[-O|-U] identifier [value ...]]\n",
	    cmd);
	fprintf(stderr,
	    "Usage:\t%s -m composite_name [-O|-U] identifier value1 value2\n",
	    cmd);
	exit(1);
}

static void
process_cmd_line(int argc, char **argv)
{
	int c;
	while ((c = getopt(argc, argv, "asdlm")) != -1) {
		switch (c) {
		case 'a':
			option_specified++;
			attribute_operation = FNSP_attr_add_operation;
			break;
		case 's':
			add_supersede = 1;
			break;
		case 'l':
			option_specified++;
			attribute_operation = FNSP_attr_list_operation;
			break;
		case 'd':
			option_specified++;
			attribute_operation = FNSP_attr_delete_operation;
			break;
		case 'm':
			option_specified++;
			attribute_operation = FNSP_attr_modify_operation;
			break;
		default:
			usage(argv[0]);
			break;
		}
	}

	if (option_specified != 1)
		usage(argv[0], "Incorrect options specified");

	// Check for composite name
	if (optind >= argc)
		usage(argv[0], "Missing composite name");
	composite_name = (const unsigned char *) argv[optind++];

	// Now check for identifier types, viz., UUID and OID in string format
	while ((c = getopt(argc, argv, "OU")) != -1) {
		switch (c) {
		case 'O':
			id_iso_oid++;
			break;
		case 'U':
			id_dce_uuid++;
			break;
		default:
			usage(argv[0]);
			break;
		}
	}

	if ((id_dce_uuid + id_iso_oid) >= 2)
		usage(argv[0], "Illegal option for the identifier");

	switch (attribute_operation) {
	case FNSP_attr_add_operation:
		if (argc < (optind + 1))
			usage(argv[0], "Missing identifer");
		identifier = (const unsigned char *) argv[optind++];
		number_of_values = argc - optind;
		if (number_of_values > 0)
			values = (const unsigned char **) &argv[optind];
		else
			fprintf(stdout, "Adding empty attribute\n");
		break;
	case FNSP_attr_list_operation:
		if (argc > (optind + 1))
			usage(argv[0], "Too many arguments");
		else if (argc == (optind + 1))
			identifier = (const unsigned char *) argv[optind++];
		break;
	case FNSP_attr_delete_operation:
		if (argc >= (optind + 1)) {
			identifier = (const unsigned char *) argv[optind++];
			if (argc >= (optind + 1)) {
				number_of_values = argc - optind;
				values = (const unsigned char **) &argv[optind];
			}
		}
		break;
	case FNSP_attr_modify_operation:
		if (argc != (optind + 3))
			usage(argv[0], "Incorrect number of parameters");
		identifier = (const unsigned char *) argv[optind++];
		number_of_values = argc - optind;
		values = (const unsigned char **) &argv[optind];
		break;
	case FNSP_attr_null_operation:
	default:
		usage(argv[0]);
		break;
	}
}

static void
print_cmd_line()
{
	fprintf(stdout, "\nCommand line arguments:\n");
	switch (attribute_operation) {
	case FNSP_attr_add_operation:
		fprintf(stdout, "Add attributes\n");
		if (add_supersede)
		fprintf(stdout, "Supersede flag is ON\n");
		break;
	case FNSP_attr_list_operation:
		fprintf(stdout, "List attribute\n");
		break;
	case FNSP_attr_delete_operation:
		fprintf(stdout, "Delete attributes\n");
		break;
	case FNSP_attr_modify_operation:
		fprintf(stdout, "Modify attribute\n");
		break;
	case FNSP_attr_null_operation:
		fprintf(stdout, "Null attribute operation\n");
		break;
	}

	if (composite_name)
		fprintf(stdout, "Composite Name: %s\n", composite_name);
	if (identifier)
		fprintf(stdout, "Identifier: %s\n", identifier);
	if (number_of_values)
		for (int i = 0; i < number_of_values; i++)
			fprintf(stdout, "Values: %s\n", values[i]);
	fprintf(stdout, "\n\n");
}

#include <sys/param.h>
#include <ctype.h>

static char *
convert_to_char(int length, const void *contents)
{
	char *buf, *bp;
	const unsigned char *p;
	int l, i, j, dl;

	l = length;
	if (l > 100)
		dl = 100;
	else
		dl = l;
	int size = 70 + (l / 10) * 80; // formated data
	buf = new char[size];
	bp = buf;
	strcpy(bp, "\n        ");
	bp += 9;
	for (p = (const unsigned char *) (contents), i = 0; i < dl; ) {
		for (j = 0; j < 10 && i+j < dl; j++) {
			sprintf(bp, "0x%.2x ", p[i+j]);
			bp += 5;
		}
		for (; j < 10; j++) {
			strcpy(bp, "     ");
			bp += 5;
		}
		*bp++ = ' ';
		for (j = 0; j < 10 && i < dl; j++, i++)
			*bp++ = (isprint(p[i]))?p[i]:'.';
		if (i < dl) {
			strcpy(bp, "\n         ");
			bp += 9;
		}
	}
	if (dl < l) {
		strcpy(bp, "\n         ...");
		bp += 12;
	}

	*bp++ = '\n';
	*bp = 0;

	return (buf);
}

static void
print_attribute(const FN_attribute *attribute)
{
	void *ip;
	const FN_identifier *syntax;
	const FN_identifier *identifier;
	const FN_attrvalue *attrvalue;

	// An empty line to start printing the output
	fprintf(stdout, "\n");

	// Print the identifier format and contents: flags UUID and OID
	identifier = attribute->identifier();
	unsigned int format = identifier->format();
	switch (format) {
	case FN_ID_STRING:
		// fprintf(stdout, "Identifier Format: FN_ID_STRING\n");
		fprintf(stdout, "Identifer: %s\n", identifier->str());
		break;
	case FN_ID_DCE_UUID:
		fprintf(stdout, "Identifier Format: FN_ID_DCE_UUID\n");
		fprintf(stdout, "Identifier: %s\n", identifier->str());
		break;
	case FN_ID_ISO_OID_STRING:
		fprintf(stdout, "Identifier Format: FN_ID_ISO_OID_STRING\n");
		fprintf(stdout, "Identifier: %s\n", identifier->str());
		break;
	case FN_ID_ISO_OID_BER:
		fprintf(stdout, "Identifier Format: FN_ID_ISO_OID_BER\n");
		fprintf(stdout, "Identifer: %s\n",
		    convert_to_char(identifier->length(),
		    identifier->contents()));
		break;
	default:
		fprintf(stdout, "Unknown Identifier Format\n");
		fprintf(stdout, "Identifer: %s\n",
		    convert_to_char(identifier->length(),
		    identifier->contents()));
		break;
	}

	// Print attribute syntax format and contents: flags UUID or OID
	syntax = attribute->syntax();
	format = syntax->format();
	unsigned int not_ascii_format = 1;
	switch (format) {
	case FN_ID_STRING:
		// fprintf(stdout, "Syntax Format: FN_ID_STRING\n");
		if ((*syntax) != attribute_syntax)
			fprintf(stdout, "Attribute Syntax: %s\n",
			    syntax->str());
		else
			not_ascii_format = 0;
		break;
	case FN_ID_DCE_UUID:
		fprintf(stdout, "Syntax Format: FN_ID_DCE_UUID\n");
		fprintf(stdout, "Attribute Syntax: %s\n", syntax->str());
		break;
	case FN_ID_ISO_OID_STRING:
		fprintf(stdout, "Syntax Format: FN_ID_ISO_OID_STRING\n");
		fprintf(stdout, "Attribute Syntax: %s\n", syntax->str());
		break;
	case FN_ID_ISO_OID_BER:
		fprintf(stdout, "Syntax Format: FN_ID_ISO_OID_BER\n");
		fprintf(stdout, "Attribute Syntax: %s\n",
		    convert_to_char(syntax->length(), syntax->contents()));
		break;
	default:
		fprintf(stdout, "Unknown Syntax Format\n");
		fprintf(stdout, "Attribute Syntax: %s\n",
		    convert_to_char(syntax->length(), syntax->contents()));
		break;
	}

	// Print the attribute values
	attrvalue = attribute->first(ip);
	while (attrvalue) {
		if (not_ascii_format) {
			fprintf(stdout, "Value:   %s\n",
			    convert_to_char(attrvalue->length(),
			    attrvalue->contents()));
		} else {
			fprintf(stdout, "Value: %s\n",
			    attrvalue->string()->str());
		}
		attrvalue = attribute->next(ip);
	}
}

static void
print_attrset(FN_multigetlist *attrset)
{
	FN_status status;
	FN_attribute *attr = attrset->next(status);
	while (attr) {
		print_attribute(attr);
		delete attr;
		attr = attrset->next(status);
	}
}

static void
delete_all_attributes(FN_ctx *init_ctx, FN_composite_name &name)
{
	FN_status status;
	FN_attrset *attrset;
	const FN_attribute *attribute;
	void *ip;

	attrset = init_ctx->attr_get_ids(name, status);
	check_error(status, "Unable to get attribute sets to delete");

	if (!attrset)
		return;
	attribute = attrset->first(ip);
	while (attribute) {
		init_ctx->attr_modify(name, FN_ATTR_OP_REMOVE,
		    *attribute, status);
		check_error(status, "Unable to delete all attribute");
		attribute = attrset->next(ip);
	}
	delete attrset;
}

static void
modify_attributes(FN_ctx *init_ctx, FN_composite_name &name,
    FN_identifier *id)
{
	FN_status status;
	FN_attrvalue *attrvalue;
	FN_attribute *attribute = new FN_attribute(*id, attribute_syntax);
	attrvalue = new FN_attrvalue(values[0]);
	attribute->add(*attrvalue);

	init_ctx->attr_modify(name, FN_ATTR_OP_REMOVE_VALUES,
	    *attribute, status);
	check_error(status, "Unable to modify attribute values");

	attribute->remove(*attrvalue);
	delete attrvalue;
	attrvalue = new FN_attrvalue(values[1]);
	attribute->add(*attrvalue);
	init_ctx->attr_modify(name, FN_ATTR_OP_ADD_VALUES,
	    *attribute, status);
	check_error(status, "Unable to modify attribute values");
	delete attribute;
	delete attrvalue;
}

main(int argc, char **argv)
{
	process_cmd_line(argc, argv);
	/* print_cmd_line(); */

	FN_status status;
	FN_ctx *init_ctx = FN_ctx::from_initial(status);
	check_error(status, "Unable to get initial context");

	// Composite name
	FN_string name_str(composite_name);
	FN_composite_name name_comp(name_str);

	// Identifier
	FN_identifier *id = 0;
	FN_attribute *attribute = 0;
	FN_attrvalue *attrvalue = 0;
	if (identifier) {
		if (id_dce_uuid)
			id = new FN_identifier(FN_ID_DCE_UUID,
			    strlen((char *) identifier),
			    (const void *) identifier);
		else if (id_iso_oid)
			id = new FN_identifier(FN_ID_ISO_OID_STRING,
			    strlen((char *) identifier),
			    (const void *) identifier);
		else
			id = new FN_identifier(identifier);
		if (attribute_operation != FNSP_attr_modify_operation) {
			attribute = new FN_attribute(*id, attribute_syntax);
			for (int i = 0; i < number_of_values; i++) {
				attrvalue = new FN_attrvalue(values[i]);
				attribute->add(*attrvalue);
				delete attrvalue;
			}
		}
	}

	switch (attribute_operation) {
	case FNSP_attr_add_operation:
		if (!add_supersede) {
			init_ctx->attr_modify(name_comp, FN_ATTR_OP_ADD_VALUES,
			    *attribute, status);
			check_error(status, "Unable to add attribute values");
		} else {
			init_ctx->attr_modify(name_comp, FN_ATTR_OP_ADD,
			    *attribute, status);
			check_error(status,
			    "Unable to replace attribute values");
		}
		break;

	case FNSP_attr_list_operation:
		if (identifier) {
			FN_attribute *ret_attribute =
			    init_ctx->attr_get(name_comp, *id, status);
			check_error(status, "Unable to list attribute values");
			if (ret_attribute) {
				fprintf(stdout, "Attributes for: %s\n",
				    composite_name);
				print_attribute(ret_attribute);
			} else
				fprintf(stdout, "No attribute values\n");
			delete ret_attribute;
		} else {
			FN_multigetlist *ret_attrset =
			    init_ctx->attr_multi_get(name_comp, 0, status);
			check_error(status,
			    "Unable to list attribute set values");
			if (ret_attrset) {
				fprintf(stdout, "Attributes for: %s\n",
				    composite_name);
				print_attrset(ret_attrset);
			} else
				fprintf(stdout, "No attributes\n");
			delete ret_attrset;
		}
		break;

	case FNSP_attr_delete_operation:
		if ((identifier) && (number_of_values)) {
			init_ctx->attr_modify(name_comp,
			    FN_ATTR_OP_REMOVE_VALUES,
			    *attribute, status);
			check_error(status, "Unable to delete values");
		} else if (identifier) {
			init_ctx->attr_modify(name_comp, FN_ATTR_OP_REMOVE,
			    *attribute, status);
			check_error(status, "Unable to delete attribute");
		} else
			delete_all_attributes(init_ctx, name_comp);
		break;

	case FNSP_attr_modify_operation:
		modify_attributes(init_ctx, name_comp, id);
		break;

	case FNSP_attr_null_operation:
		break;
	}

	if (id)
		delete id;
	if (attribute)
		delete attribute;
	exit(0);
}
