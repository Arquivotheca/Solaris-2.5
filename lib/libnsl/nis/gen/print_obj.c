/*
 *	print_obj.c
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)print_obj.c	1.15	94/10/15 SMI"

/*
 *
 * print_obj.c
 *
 * This module contains a function for printing objects to standard out.
 */

#include <stdio.h>
#include <string.h>
#include <rpcsvc/nis.h>

#define	ZVAL zattr_val.zattr_val_val
#define	ZLEN zattr_val.zattr_val_len
#define	nilstring(s)	((s) ? (s) : "(nil)")


/*
 * forward function prototypes.
 */

void nis_print_server(nis_server *);

void
nis_print_rights(r)
	u_long	r;
{
	int s;
	for (s = 24; s >= 0; s -= 8) {
		if (r & (NIS_READ_ACC << s))
			printf("r");
		else
			printf("-");
		if (r & (NIS_MODIFY_ACC << s))
			printf("m");
		else
			printf("-");
		if (r & (NIS_CREATE_ACC << s))
			printf("c");
		else
			printf("-");
		if (r & (NIS_DESTROY_ACC << s))
			printf("d");
		else
			printf("-");
	}
}

void
nis_print_server(s)
	nis_server	*s;
{
	int		i;

	printf("\tName       : %s\n", nilstring(s->name));
	printf("\tPublic Key : ");
	switch (s->key_type) {
	case NIS_PK_DH :
		printf("Diffie-Hellman (%d bits)\n",
			strlen(s->pkey.n_bytes) * 4);
		break;
	case NIS_PK_RSA :
		printf("RSA (%d bits)\n", s->pkey.n_len * 4);
		break;
	case NIS_PK_NONE :
		printf("None.\n");
		break;
	default :
		printf("Unknown (type = %d, bits = %d)\n", s->key_type,
					s->pkey.n_len * 4);
	}

	printf("\tUniversal addresses (%d)\n", s->ep.ep_len);
	for (i = 0; i < s->ep.ep_len; i++)
		printf("\t[%d] - %s, %s, %s\n", i+1, s->ep.ep_val[i].proto,
			s->ep.ep_val[i].family, s->ep.ep_val[i].uaddr);
}

void
nis_print_directory(r)
	directory_obj	*r;
{
	int		i;


	printf("Name : '%s'\n", nilstring(r->do_name));
	printf("Type : ");
	switch (r->do_type) {
		case NIS :
			printf("NIS\n");
			break;
		case SUNYP :
			printf("YP\n");
			break;
		case DNS :
			printf("DNS\n");
			break;
		default :
			printf("%d\n", r->do_type);
	}
	for (i = 0; i < r->do_servers.do_servers_len; i++) {
		if (i == 0)
			printf("Master Server :\n");
		else
			printf("Replicate : \n");

		nis_print_server(&(r->do_servers.do_servers_val[i]));
	}
	printf("Time to live : %d:%d:%d\n", r->do_ttl/3600,
			(r->do_ttl - (r->do_ttl/3600)*3600)/60,
			(r->do_ttl % 60));
	printf("Default Access rights :\n");
	for (i = 0; i < r->do_armask.do_armask_len; i++) {
		switch (r->do_armask.do_armask_val[i].oa_otype) {
			case GROUP_OBJ :
				printf("\t\tGROUP Objects     : ");
				break;
			case ENTRY_OBJ :
				printf("\t\tENTRY Objects     : ");
				break;
			case LINK_OBJ :
				printf("\t\tLINK Objects      : ");
				break;
			case DIRECTORY_OBJ :
				printf("\t\tDIRECTORY Objects : ");
				break;
			case TABLE_OBJ :
				printf("\t\tTABLE Objects     : ");
				break;
			case BOGUS_OBJ :
				printf("\t\tBOGUS Objects     : ");
				break;
			default :
				printf("\t\tUnknown Objects   : ");
		}
		nis_print_rights(OARIGHTS(r, i));
		printf("\n");
	}
}

void
nis_print_group(g)
	group_obj	*g;
{
	int		i;

	printf("Group Flags : ");
	if (g->gr_flags & NEGMEM_GROUPS)
		printf("\tNegative Memberships allowed\n");
	if (g->gr_flags & IMPMEM_GROUPS)
		printf("\tImplicit Membership allowed\n");
	if (g->gr_flags & RECURS_GROUPS)
		printf("\tRecursive Memberships allowed\n");
	if (! g->gr_flags)
		printf("\n");

	printf("Group Members :\n");
	for (i = 0; i < g->gr_members.gr_members_len; i++)
		printf("\t%s\n", nilstring(g->gr_members.gr_members_val[i]));
}

static void
print_column(n, col)
	int		n;
	table_col 	*col;
{

	printf("\t[%d]\tName          : ", n);
	printf("%s\n", nilstring(col->tc_name));
	printf("\t\tAttributes    : (");
	if (col->tc_flags & TA_SEARCHABLE)
		printf("SEARCHABLE, ");
	if ((col->tc_flags & TA_BINARY) == 0) {
		printf("TEXTUAL DATA");
		if (col->tc_flags & TA_SEARCHABLE) {
			if (col->tc_flags & TA_CASE)
				printf(", CASE INSENSITIVE");
			else
				printf(", CASE SENSITIVE");
		}
	} else {
		printf("BINARY DATA");
		if (col->tc_flags & TA_XDR)
			printf(", XDR ENCODED");
		if (col->tc_flags & TA_ASN1)
			printf(", ASN.1 ENCODED");
	}
	printf(")\n");
	printf("\t\tAccess Rights : ");
	nis_print_rights(col->tc_rights);
	printf("\n");
}

void
nis_print_table(t)
	table_obj	*t;
{
	int		i;

	printf("Table Type          : %s\n", nilstring(t->ta_type));
	printf("Number of Columns   : %d\n", t->ta_maxcol);
	printf("Character Separator : %c\n", t->ta_sep);
	printf("Search Path         : %s\n", nilstring(t->ta_path));
	printf("Columns             :\n");
	for (i = 0; i < t->ta_cols.ta_cols_len; i++) {
		print_column(i, &(t->ta_cols.ta_cols_val[i]));
	}
}

void
nis_print_link(l)
	link_obj	*l;
{
	int		i;
	printf("Linked Object Type : ");
	switch (l->li_rtype) {
		case DIRECTORY_OBJ :
			printf("DIRECTORY\n");
			break;
		case TABLE_OBJ :
			printf("TABLE\n");
			break;
		case ENTRY_OBJ :
			printf("ENTRY\n");
			break;
		case GROUP_OBJ :
			printf("GROUP\n");
			break;
		case LINK_OBJ :
			printf("LINK\n");
			break;
		case PRIVATE_OBJ :
			printf("PRIVATE\n");
			break;
		default :
			printf("(UNKNOWN) [%d]\n", l->li_rtype);
			break;
	}
	printf("Link to : ");
	if (l->li_attrs.li_attrs_len) {
		printf("[");
		for (i = 0; i < (l->li_attrs.li_attrs_len-1); i++)
			printf("%s=%s, ",
			    nilstring(l->li_attrs.li_attrs_val[i].zattr_ndx),
			    nilstring(l->li_attrs.li_attrs_val[i].ZVAL));
		printf("%s=%s ] ",
			nilstring(l->li_attrs.li_attrs_val[i].zattr_ndx),
			nilstring(l->li_attrs.li_attrs_val[i].ZVAL));
	}
	printf("%s\n", nilstring(l->li_name));
}

void
nis_print_entry(edata)
	entry_obj	*edata;
{
	int		i, j;
	entry_col	*col;

	printf("\tEntry data of type %s\n", nilstring(edata->en_type));
	for (i = 0, col = edata->en_cols.en_cols_val;
			i < edata->en_cols.en_cols_len; i++, col++) {
		printf("\t[%d] - [%d bytes] ", i,
				col->ec_value.ec_value_len);
		if (col->ec_flags & EN_CRYPT) {
			printf("Encrypted data\n");
			continue;
		}
		if (col->ec_flags & EN_XDR) {
			printf("XDR'd Data\n");
			continue;
		}
		if (col->ec_flags & EN_BINARY) {
			for (j = 0; j < col->ec_value.ec_value_len; j++) {
				if (((j % 8) == 0) && (j != 0)) {
					printf("\n\t      ");
				}
				printf("0x%02x ",
				    (u_char) *(col->ec_value.ec_value_val+j));
			}
			printf("\n");
			continue;
		} else {
			printf("'%s'\n", nilstring(col->ec_value.ec_value_val));
			continue;
		}
	}
}

#define	_dot(c)	(isprint(c) ? c : '.')

static void
nis_print_private(u)
	objdata	*u;
{
	char	pbuf[80],
		buf1[5],
		buf2[20],
		buf3[20],
		buf4[20];
	u_char	*data;
	int	len, i, j;

	/*
	 * dump private data as a formatted dump using format :
	 * "1234: 0011223344556677 8899aabbccddeeff ................\n"
	 */
	data = (u_char *)(u->objdata_u.po_data.po_data_val);
	len  = u->objdata_u.po_data.po_data_len;

	for (i = 0; (i+15) < len; i += 16) {
		sprintf(buf1, "%04x", (u_long) (i));
		sprintf(buf2, "%02x%02x%02x%02x%02x%02x%02x%02x",
			*(data+i), *(data+i+1), *(data+i+2), *(data+i+3),
			*(data+i+4), *(data+i+5), *(data+i+6), *(data+i+7));
		sprintf(buf3, "%02x%02x%02x%02x%02x%02x%02x%02x",
			*(data+i+8), *(data+i+9), *(data+i+10), *(data+i+11),
			*(data+i+12), *(data+i+13), *(data+i+14), *(data+i+15));
		sprintf(buf4, "%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c",
			_dot(*(data+i)), _dot(*(data+i+1)), _dot(*(data+i+2)),
			_dot(*(data+i+3)), _dot(*(data+i+4)),
			_dot(*(data+i+5)), _dot(*(data+i+6)), _dot(*(data+i+7)),
			_dot(*(data+i+8)), _dot(*(data+i+9)),
			_dot(*(data+i+10)), _dot(*(data+i+11)),
			_dot(*(data+i+12)), _dot(*(data+i+13)),
			_dot(*(data+i+14)), _dot(*(data+i+15)));
		printf("\t%s: %s %s %s\n", buf1, buf2, buf3, buf4);
	}
	if (i < len) {
		sprintf(pbuf, "%04x: ", (u_long)(i));
		buf4[0] = '\0';
		for (j = 0; j < 16; j++) {
			if (i+j < len) {
				sprintf(buf3, "%02x", *(data+i+j));
				strcat(pbuf, buf3);
				if (j == 7)
					strcat(pbuf, " ");
				sprintf(buf3, "%c", _dot(*(data+i+j)));
				strcat(buf4, buf3);
			} else {
				strcat(pbuf, "  ");
				if (j == 8)
					strcat(pbuf, " ");
			}

		}
		printf("\t%s %s\n", pbuf, buf4);
	}
}

void
nis_print_object(o)
	nis_object	*o;
{
	printf("Object Name   : %s\n", nilstring(o->zo_name));
	printf("Directory     : %s\n", nilstring(o->zo_domain));
	printf("Owner         : %s\n", nilstring(o->zo_owner));
	printf("Group	      : %s\n", nilstring(o->zo_group));
	printf("Access Rights : ");
	nis_print_rights(o->zo_access);
	printf("\n");
	printf("Time to Live  : %d:%d:%d\n", o->zo_ttl/3600,
			(o->zo_ttl - (o->zo_ttl/3600)*3600)/60,
			(o->zo_ttl % 60));
	printf("Creation Time : %s", ctime((time_t *)&(o->zo_oid.ctime)));
	printf("Mod. Time     : %s", ctime((time_t *)&(o->zo_oid.mtime)));
	printf("Object Type   : ");
	switch (__type_of(o)) {
		case NO_OBJ :
			printf("NONE\n");
			break;
		case DIRECTORY_OBJ :
			printf("DIRECTORY\n");
			nis_print_directory(&(o->DI_data));
			break;
		case TABLE_OBJ :
			printf("TABLE\n");
			nis_print_table(&(o->TA_data));
			break;
		case ENTRY_OBJ :
			printf("ENTRY\n");
			nis_print_entry(&(o->EN_data));
			break;
		case GROUP_OBJ :
			printf("GROUP\n");
			nis_print_group(&(o->GR_data));
			break;
		case LINK_OBJ :
			printf("LINK\n");
			nis_print_link(&(o->LI_data));
			break;
		case PRIVATE_OBJ :
			printf("PRIVATE\n");
			nis_print_private(&(o->zo_data));
			break;
		default :
			printf("(UNKNOWN) [%d]\n", __type_of(o));
			break;
	}
}
