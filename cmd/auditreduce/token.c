#ifndef lint
static char	*sccsid = "@(#)token.c 1.3 92/11/02 SMI";
static char	*cmw_sccsid = "@(#)token.c 2.4 92/08/12 SMI; SunOS CMW";
static char	*bsm_sccsid = "@(#)token.c 4.8.1.1 91/11/12 SMI; BSM Module";
static char	*mls_sccsid = "@(#)token.c 3.2 90/11/13 SMI; SunOS MLS";
#endif

/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

/*
 * Token processing for auditreduce.
 */
#include "auditr.h"

/*
 * Token routines for parsing SunOS 4.1.X C2-BSM audit trails
 */
#ifdef C2_MARAUDER
extern int	old_attribute_token();
extern int	old_path_token();
extern int	old_process_token();
extern int	old_s5_IPC_perm_token();
extern int	old_subject_token();
#endif /* C2_MARAUDER */

/*
 * These tokens are the same in CMW 1.0, 4.1.X C2-BSM and 5.X C2-BSM
 */
extern int	arbitrary_data_token();
extern int	argument_token();
extern int	file_token();
extern int	group_token();
extern int	header_token();
extern int	ip_addr_token();
extern int	ip_token();
extern int	iport_token();
extern int	opaque_token();
extern int	return_value_token();
extern int	s5_IPC_token();
extern int	sequence_token();
extern int	server_token();
extern int	text_token();
extern int	trailer_token();
extern int	attribute_token();
extern int	cmd_token();
extern int	exit_token();
extern int	liaison_token();
extern int	path_token();
extern int	process_token();
extern int	s5_IPC_perm_token();
extern int	socket_token();
extern int	subject_token();
extern int	xatom_token();
extern int	xobj_token();
extern int	xproto_token();
extern int	xselect_token();
extern int	old_header_token();

extern int	newgroup_token();
extern int	exec_args_token();
extern int	exec_env_token();

/*
 * CMW only tokens
 */
#ifdef SunOS_CMW
extern int	clearance_token();
extern int	ilabel_token();
extern int	privilege_token();
extern int	slabel_token();
extern int	useofpriv_token();
#endif /* SunOS_CMW */

static void	anchor_path();
static char	*collapse_path();

struct token_desc {
	char	*t_name;	/* name of the token */
	int	tokenid;	/* token type */
	int	t_fieldcount;	/* number of fields in this token */
	int	(*func)();	/* token processing function */
	char	*t_fields[16];	/* The fields to watch */
};
typedef struct token_desc token_desc_t;

#ifndef C2_MARAUDER

token_desc_t tokentable[] = {
	{ "argument",	AUT_ARG, 	3, 	argument_token, 	},
	{ "attr", 	AUT_ATTR, 	6, 	attribute_token, 	},
	{ "cmd", 	AUT_CMD, 	2, 	cmd_token, 		},
	{ "data", 	AUT_DATA, 	4, 	arbitrary_data_token, 	},
	{ "exec_args", 	AUT_EXEC_ARGS, 	-1, 	exec_args_token, 	},
	{ "exec_env", 	AUT_EXEC_ENV, 	-1, 	exec_env_token, 	},
	{ "exit", 	AUT_EXIT, 	2, 	exit_token, 		},
	{ "groups", 	AUT_GROUPS, 	16, 	group_token, 		},
	{ "header", 	AUT_HEADER, 	4, 	header_token, 		},
	{ "in_addr", 	AUT_IN_ADDR, 	1, 	ip_addr_token, 		},
	{ "ip", 	AUT_IP,		10, 	ip_token,		},
	{ "ipc", 	AUT_IPC, 	1, 	s5_IPC_token, 		},
	{ "ipc_perm", 	AUT_IPC_PERM, 	8, 	s5_IPC_perm_token, 	},
	{ "iport", 	AUT_IPORT, 	1, 	iport_token, 		},
	{ "liaison", 	AUT_LIAISON, 	1, 	liaison_token, 		},
	{ "newgroups", 	AUT_NEWGROUPS, 	-1, 	newgroup_token, 	},
	{ "old_header",	AUT_OHEADER, 	1, 	old_header_token, 	},
	{ "opaque", 	AUT_OPAQUE, 	2, 	opaque_token, 		},
	{ "other_file", AUT_OTHER_FILE,	3, 	file_token, 		},
	{ "path", 	AUT_PATH, 	3, 	path_token, 		},
	{ "process", 	AUT_PROCESS, 	5, 	process_token, 		},
	{ "return", 	AUT_RETURN, 	2, 	return_value_token, 	},
	{ "sequence", 	AUT_SEQ, 	1, 	sequence_token, 	},
	{ "server", 	AUT_SERVER, 	5, 	server_token, 		},
	{ "socket", 	AUT_SOCKET, 	5, 	socket_token, 		},
	{ "subject", 	AUT_SUBJECT, 	5, 	subject_token, 		},
	{ "text", 	AUT_TEXT, 	1, 	text_token, 		},
	{ "trailer", 	AUT_TRAILER, 	2, 	trailer_token, 		},
	{ "xatom", 	AUT_XATOM, 	2, 	xatom_token, 		},
	{ "xobj", 	AUT_XOBJ, 	4, 	xobj_token, 		},
	{ "xproto", 	AUT_XPROTO, 	1, 	xproto_token, 		},
	{ "xselect", 	AUT_XSELECT, 	4, 	xselect_token, 		},
#ifdef SunOS_CMW
	{ "clearance", 	AUT_CLEAR, 	5, 	clearance_token, 	},
	{ "ilabel", 	AUT_ILABEL, 	3, 	ilabel_token, 		},
	{ "privilege", 	AUT_PRIV, 	1, 	cmw_privilege_token, 	},
	{ "slabel", 	AUT_SLABEL, 	2, 	cmw_slabel_token, 	},
	{ "use_of_priv", AUT_UPRIV, 	3, 	cmw_useofpriv_token, 	},
#endif /* SunOS_CMW  */
};


#else /* C2_MARAUDER 4.1.X C2-BSM */

token_desc_t tokentable[] = {
	{ "attr", 	AUT_ATTR, 	6, 	old_attribute_token, 	},
	{ "data", 	AUT_DATA, 	4, 	arbitrary_data_token, 	},
	{ "groups", 	AUT_GROUPS, 	16, 	group_token, 		},
	{ "header", 	AUT_OHEADER, 	4, 	old_header_token, 	},
	{ "in_addr", 	AUT_IN_ADDR, 	1, 	ip_addr_token, 		},
	{ "ip",		AUT_IP, 	10, 	ip_token, 		},
	{ "ipc", 	AUT_IPC, 	1, 	s5_IPC_token, 		},
	{ "ipc_perm", 	AUT_IPC_PERM, 	8, 	old_s5_IPC_perm_token, 	},
	{ "iport", 	AUT_IPORT, 	1, 	iport_token, 		},
	{ "argument", 	AUT_ARG, 	3, 	argument_token, 	},
	{ "opaque", 	AUT_OPAQUE, 	2, 	opaque_token, 		},
	{ "other_file",	AUT_OTHER_FILE,	3, 	file_token, 		},
	{ "path", 	AUT_PATH, 	3, 	old_path_token, 	},
	{ "process", 	AUT_PROCESS, 	5, 	old_process_token, 	},
	{ "return", 	AUT_RETURN, 	2, 	return_value_token, 	},
	{ "server", 	AUT_SERVER, 	5, 	server_token, 		},
	{ "sequence", 	AUT_SEQ, 	1, 	sequence_token, 	},
	{ "subject", 	AUT_SUBJECT, 	5, 	old_subject_token, 	},
	{ "text", 	AUT_TEXT, 	1, 	text_token, 		},
	{ "trailer", 	AUT_TRAILER, 	2, 	trailer_token, 		},
};


#endif /* SunOS_CMW */

token_desc_t	*tokenp;
int	numtokenentries = sizeof (tokentable) / sizeof (token_desc_t);
/*
 * List of the interesting tokens
 */
static token_desc_t *interesting_tokens;

#ifdef DUMP
/*
 * Translate a -T 'option' argument
 */

proc_token(spec)
char	*spec;
{
	token_desc_t * tp;	/* list processing */
	char	*values;		/* values for the token to contain */
	int	i;			/* an integer */
	char	*p;

	/*
	 * Separate the token name from the values.
	 */
	if ((values = strchr(spec, '=')) == NULL)
		return (1);
	*values++ = '\0';

	/*
	 * Find the token name.
	 */
	for (i = 0; i < numtokenentries; i++) {
		if (strcmp(spec, tp->t_name) == 0) {
			dasht = 1;
			tp = tokenp = &tokentable[i];
			break;
		}
	}
	if (i == numtokenentries)
		return (1);

	/*
	 * Read the token values
	 */
	i = 0;
	while ((p = strchr(values, ',')) != NULL) {
		tp->t_fields[i] = values;
		*p++ = '\0';
		values = p;
		if (++i > 16)
			return (1);
	}
	/* don't forget the last one */
	if (i < 16)
		tp->t_fields[i] = values;
	/* clean up the rest of the fields */
	for (i++; i < 16; i++)
		tp->t_fields[i] = NULL;
	return (0);
}


#endif

/*
 *  Process a token in a record to determine whether the record
 *  is interesting.
 */

int
token_processing(adr, tokenid)
adr_t	*adr;
int	tokenid;
{
	int	i;
	token_desc_t t;
	token_desc_t * k;
	int	rc;

	for (i = 0; i < numtokenentries; i++) {
		k = &(tokentable[i]);
		if ((k->tokenid) == tokenid) {
			rc = (*tokentable[i].func)(adr);
			return (rc);
		}
	}
	/* here if token id is not in table */
	return (-2);
}


/*
 * There should not be any file or header tokens in the middle of
 * a record
 */

file_token(adr)
adr_t	*adr;
{
	return (-2);
}


int
header_token(adr)
adr_t	*adr;
{
	return (-2);
}


int
old_header_token(adr)
adr_t	*adr;
{
	return (-2);
}


/*
 * ======================================================
 *  The following token processing routines return
 *  -1: if the record is not interesting
 *  -2: if an error is found
 * ======================================================
 */

int
trailer_token(adr)
adr_t	*adr;
{
	int	returnstat = 0;
	int	i;
	short	magic_number;
	unsigned long bytes;

	adrm_u_short(adr, (u_short *) & magic_number, 1);
	if (magic_number != AUT_TRAILER_MAGIC) {
		fprintf(stderr, "auditreduce: Bad trailer token\n");
		return (-2);
	}
	adrm_u_long(adr, &bytes, 1);

	return (-1);
}


/*
 * Format of arbitrary data token:
 *	arbitrary data token id	adr char
 * 	how to print		adr_char
 *	basic unit		adr_char
 *	unit count		adr_char, specifying number of units of
 *	data items		depends on basic unit
 *
 */
int
arbitrary_data_token(adr)
adr_t	*adr;
{
	int	i;
	char	c1;
	short	c2;
	long	c3;
	char	how_to_print, basic_unit, unit_count;

	/* get how_to_print, basic_unit, and unit_count */
	adrm_char(adr, &how_to_print, 1);
	adrm_char(adr, &basic_unit, 1);
	adrm_char(adr, &unit_count, 1);
	for (i = 0; i < unit_count; i++) {
		switch (basic_unit) {
			/* case AUR_BYTE: has same value as AUR_CHAR */
		case AUR_CHAR:
			adrm_char(adr, &c1, 1);
			break;
		case AUR_SHORT:
			adrm_short(adr, &c2, 1);
			break;
		case AUR_INT:
			adrm_int(adr, (int *) & c3, 1);
			break;
		case AUR_LONG:
			adrm_long(adr, (long *) & c3, 1);
			break;
		default:
			return (-2);
			break;
		}
	}
#ifdef DUMP
	/* check "-T" : only the first three fields are checked */
	if (dasht && (tokenp->tokenid == AUT_DATA)) {
		if (check_field(tokenp->t_fields[0], how_to_print) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[1], basic_unit) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[2], unit_count) != 0)
			return (-1);
	}
#endif
	return (-1);
}


/*
 * Format of opaque token:
 *	opaque token id		adr_char
 *	size			adr_short
 *	data			adr_char, size times
 *
 */
int
opaque_token(adr)
adr_t	*adr;
{
	int	i;
	short	size;
	char	*charp;

	adrm_short(adr, &size, 1);
	/* try to allocate memory for the character string */
	charp = a_calloc(1, (size_t) size);

	adrm_char(adr, charp, size);

	free(charp);
#ifdef DUMP
	if (dasht && (tokenp->tokenid == AUT_OPAQUE)) {
		if (tokenp->t_fields[1] && *tokenp->t_fields[1]) {
			if (strcmp(tokenp->t_fields[1], charp))
				return (-1);
		}
	}
#endif
	return (-1);
}



/*
 * Format of return value token:
 * 	return value token id	adr_char
 *	error number		adr_char
 *	return value		adr_u_int
 *
 */
int
return_value_token(adr)
adr_t	*adr;
{
	char	errnum;
	unsigned int	value;

	adrm_char(adr, &errnum, 1);
	adrm_u_int(adr, &value, 1);
	if ((flags & M_SORF) &&
		((global_class & mask.am_success) && (errnum == 0)) ||
		((global_class & mask.am_failure) && (errnum != 0))) {
			checkflags |= M_SORF;
	}
#ifdef DUMP
	/* check -T option */
	if (dasht && (tokenp->tokenid == AUT_RETURN)) {
		if (check_field(tokenp->t_fields[0], errnum) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[1], value) != 0)
			return (-1);
	}
#endif
	return (-1);
}


/*
 * Format of sequence token:
 *	sequence token id	adr_char
 *	audit_count		long
 *
 */
int
sequence_token(adr)
adr_t	*adr;
{
	long	audit_count;

	adrm_long(adr, &audit_count, 1);
	return (-1);
}


/*
 * Format of server token:
 *	server token id		adr_char
 *	auid			adr_u_short
 *	euid			adr_u_short
 *	ruid			adr_u_short
 *	egid			adr_u_short
 *	pid			adr_u_short
 *
 */
int
server_token(adr)
adr_t	*adr;
{
	unsigned short	auid, euid, ruid, egid, pid;

	adrm_u_short(adr, &auid, 1);
	adrm_u_short(adr, &euid, 1);
	adrm_u_short(adr, &ruid, 1);
	adrm_u_short(adr, &egid, 1);
	adrm_u_short(adr, &pid, 1);

	if (flags & M_USERA) {
		if (m_usera == auid)
			checkflags = checkflags | M_USERA;
	}
	if (flags & M_USERE) {
		if (m_usere == euid)
			checkflags = checkflags | M_USERE;
	}
	if (flags & M_USERR) {
		if (m_userr == ruid)
			checkflags = checkflags | M_USERR;
	}
	if (flags & M_GROUPE) {
		if (m_groupe == egid)
			checkflags = checkflags | M_GROUPE;
	}
#ifdef DUMP
	/* check -T option */
	if (dasht && (tokenp->tokenid == AUT_SERVER)) {
		if (check_field(tokenp->t_fields[0], auid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[1], euid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[2], ruid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[3], egid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[4], pid) != 0)
			return (-1);
	}
#endif
	return (-1);
}


/*
 * Format of text token:
 *	text token id		adr_char
 * 	text			adr_string
 *
 */
int
text_token(adr)
adr_t	*adr;
{
	char	text[MAXFILELEN];

	text[0] = '\0';
	adrm_string(adr, text);
#ifdef DUMP
	/* check -T */
	if (dasht && (tokenp->tokenid == AUT_TEXT)) {
		if (check_field_str(tokenp->t_fields[0], text, 1) != 0)
			return (-1);
	}
#endif
	return (-1);
}


/*
 * Format of ip_addr token:
 *	ip token id	adr_char
 *	address		adr_long
 *
 */
int
ip_addr_token(adr)
adr_t	*adr;
{
	long	address;

	adrm_char(adr, (char *) &address, 4);
#ifdef DUMP
	/* check -T */
	if (dasht && (tokenp->tokenid == AUT_IN_ADDR)) {
		if (check_field(tokenp->t_fields[0], address) != 0)
			return (-1);
	}
#endif
	return (-1);
}


/*
 * Format of ip token:
 *	ip header token id	adr_char
 *	version			adr_char
 *	type of service		adr_char
 *	length			adr_short
 *	id			adr_u_short
 *	offset			adr_u_short
 *	ttl			adr_char
 *	protocol		adr_char
 *	checksum		adr_u_short
 *	source address		adr_long
 *	destination address	adr_long
 *
 */
int
ip_token(adr)
adr_t	*adr;
{
	char	version;
	char	type;
	short	len;
	unsigned short	id, offset, checksum;
	char	ttl, protocol;
	long	src, dest;

	adrm_char(adr, &version, 1);
	adrm_char(adr, &type, 1);
	adrm_short(adr, &len, 1);
	adrm_u_short(adr, &id, 1);
	adrm_u_short(adr, &offset, 1);
	adrm_char(adr, &ttl, 1);
	adrm_char(adr, &protocol, 1);
	adrm_u_short(adr, &checksum, 1);
	adrm_char(adr, (char *) &src, 4);
	adrm_char(adr, (char *) &dest, 4);
#ifdef DUMP
	/* check -T */
	if (dasht && (tokenp->tokenid == AUT_IP)) {
		if (check_field(tokenp->t_fields[0], version) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[1], type) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[2], len) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[3], id) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[4], offset) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[5], ttl) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[6], protocol) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[7], checksum) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[8], src) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[9], dest) != 0)
			return (-1);
	}
#endif
	return (-1);
}


/*
 * Format of iport token:
 *	ip port address token id	adr_char
 *	port address			adr_short
 *
 */
int
iport_token(adr)
adr_t	*adr;
{
	short	address;

	adrm_short(adr, &address, 1);
#ifdef DUMP
	if (dasht && (tokenp->tokenid == AUT_IPORT)) {
		if (check_field(tokenp->t_fields[0], address) != 0)
			return (-1);
	}
#endif
	return (-1);
}


/*
 * Format of groups token:
 *	group token id		adr_char
 *	group list		adr_long, 16 times
 *
 */
int
group_token(adr)
adr_t	*adr;
{
	int	gid[16];
	int	i;
	int	flag = 0;

	for (i = 0; i < 16; i++) {
		adrm_long(adr, (long *) & gid[i], 1);
		if (flags & M_GROUPR) {
			if ((unsigned short)m_groupr == gid[i])
				flag = 1;
		}
	}

	if (flags & M_GROUPR) {
		if (flag)
			checkflags = checkflags | M_GROUPR;
	}
#ifdef DUMP
	/* check -T */
	if (dasht && (tokenp->tokenid == AUT_GROUPS)) {
		for (i = 0; i < 16; i++) {
			if (check_field(tokenp->t_fields[i], gid[i]) !=
			    0)
				return (-1);
		}
	}
#endif
	return (-1);
}

/*
 * Format of newgroups token:
 *	group token id		adr_char
 *	number of groups	adr_short
 *	group list		adr_long, "number" times
 *
 */
int
newgroup_token(adr)
adr_t	*adr;
{
	int	gid[NGROUPS_MAX];
	int	i;
	short int   number;
	int	flag = 0;

	adrm_short(adr, &number, 1);

	for (i = 0; i < number; i++) {
		adrm_long(adr, (long *) & gid[i], 1);
		if (flags & M_GROUPR) {
			if ((unsigned short)m_groupr == gid[i])
				flag = 1;
		}
	}

	if (flags & M_GROUPR) {
		if (flag)
			checkflags = checkflags | M_GROUPR;
	}
#ifdef DUMP
	/* check -T */
	if (dasht && (tokenp->tokenid == AUT_GROUPS)) {
		for (i = 0; i < 16; i++) {
			if (check_field(tokenp->t_fields[i], gid[i]) !=
			    0)
				return (-1);
		}
	}
#endif
	return (-1);
}

/*
 * Format of argument token:
 *	argument token id	adr_char
 *	argument number		adr_char
 *	argument value		adr_long
 *	argument description	adr_string
 *
 */
int
argument_token(adr)
adr_t	*adr;
{
	char	arg_num;
	long	arg_val;
	char	text[MAXFILELEN];

	adrm_char(adr, &arg_num, 1);
	adrm_long(adr, &arg_val, 1);
	text[0] = '\0';
	adrm_string(adr, text);

#ifdef DUMP
	/* check -T option */
	if (dasht && (tokenp->tokenid == AUT_ARG)) {
		if (check_field(tokenp->t_fields[0], arg_num) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[1], arg_val) != 0)
			return (-1);
		if (check_field_str(tokenp->t_fields[0], text, 1) != 0)
			return (-1);
	}
#endif
	return (-1);
}


/*
 * Format of attribute token:
 *	attribute token id	adr_char
 * 	mode			adr_long (printed in octal)
 *	uid			adr_long
 *	gid			adr_long
 *	file system id		adr_long
 *	node id			adr_long
 *	device			adr_long
 *
 */
int
attribute_token(adr)
adr_t	*adr;
{
	long	dev;
	long	file_sysid;
	long	gid;
	long	mode;
	long	nodeid;
	long	uid;

	adrm_long(adr, &mode, 1);
	adrm_long(adr, &uid, 1);
	adrm_long(adr, &gid, 1);
	adrm_long(adr, &file_sysid, 1);
	adrm_long(adr, &nodeid, 1);
	adrm_long(adr, &dev, 1);

	if (flags & M_USERE) {
		if (m_usere == uid)
			checkflags = checkflags | M_USERE;
	}
	if (flags & M_GROUPR) {
		if (m_groupr == gid)
			checkflags = checkflags | M_GROUPR;
	}
#ifdef DUMP
	/* check -T */
	if (dasht && (tokenp->tokenid == AUT_ATTR)) {
		if (check_field(tokenp->t_fields[0], mode) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[1], uid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[2], gid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[3], file_sysid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[4], nodeid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[5], dev) != 0)
			return (-1);
	}
#endif
	return (-1);
}


/*
 * Format of command token:
 *	attribute token id	adr_char
 *	argc			adr_short
 *	argv len		adr_short	variable amount of argv len
 *	argv text		argv len	and text
 *	.
 *	.
 *	.
 *	envp count		adr_short	variable amount of envp len
 *	envp len		adr_short	and text
 *	envp text		envp		len
 *	.
 *	.
 *	.
 *
 */
int
cmd_token(adr)
adr_t	*adr;
{
	short	cnt;
	short	i;

	char	s[2048];

	adrm_short(adr, &cnt, 1);

	for (i = 0; i < cnt; i++)
		adrm_string(adr, s);

	adrm_short(adr, &cnt, 1);

	for (i = 0; i < cnt; i++)
		adrm_string(adr, s);

	return (-1);
}


/*
 * Format of exit token:
 *	attribute token id	adr_char
 *	return value		adr_long
 *	errno			adr_long
 *
 */
int
exit_token(adr)
adr_t	*adr;
{
	long	retval;
	long	errno;

	adrm_long(adr, &retval, 1);
	adrm_long(adr, &errno, 1);
	return (-1);
}

/*
 * Format of exec_args token:
 *	attribute token id	adr_char
 *	count value		adr_long
 *	strings			null terminated strings
 *
 */
int
exec_args_token(adr)
adr_t *adr;
{
	long count, i;
	char c;

	adrm_long(adr, &count, 1);
	for (i = 1; i <= count; i++) {
		adrm_char(adr, &c, 1);
		while (c != (char) 0)
			adrm_char(adr, &c, 1);
	}
	/* no dump option here, since we will have variable length fields */
	return (-1);
}

/*
 * Format of exec_env token:
 *	attribute token id	adr_char
 *	count value		adr_long
 *	strings			null terminated strings
 *
 */
int
exec_env_token(adr)
adr_t *adr;
{
	long count, i;
	char c;

	adrm_long(adr, &count, 1);
	for (i = 1; i <= count; i++) {
		adrm_char(adr, &c, 1);
		while (c != (char) 0)
			adrm_char(adr, &c, 1);
	}
	/* no dump option here, since we will have variable length fields */
	return (-1);
}

/*
 * Format of liaison token:
 */
int
liaison_token(adr)
adr_t	*adr;
{
	long	li;

	adrm_long(adr, &li, 1);
	return (-1);
}


/*
 * Format of path token:
 *	path				adr_string
 */
int
path_token(adr)
adr_t 	*adr;
{
	static char	path[MAXFILELEN+1];

	adrm_string(adr, path);
	if ((flags & M_OBJECT) && (obj_flag == OBJ_PATH)) {
		if (path[0] != '/')
			/*
			 * anchor the path. user apps may not do it.
			 */
			anchor_path(path);
		/*
		 * match against the collapsed path. that is what user sees.
		 */
		if (re_exec2(collapse_path(path)) == 1)
			checkflags = checkflags | M_OBJECT;
	}
#ifdef DUMP
	/* check path token: if interesting, returns 0 */
	if (dasht && (tokenp->tokenid == AUT_PATH)) {
		/* check path */
		if (check_field_str(tokenp->t_fields[2], path, 0) != 0)
			return (-1);
	}
#endif
	return (-1);
}


/*
 * Format of System V IPC permission token:
 *	System V IPC permission token id	adr_char
 * 	uid					adr_long
 *	gid					adr_long
 *	cuid					adr_long
 *	cgid					adr_long
 *	mode					adr_long
 *	seq					adr_long
 *	key					adr_long
 *	label					adr_opaque, sizeof (bslabel_t)
 *							    bytes
 */
s5_IPC_perm_token(adr)
adr_t	*adr;
{
	long	uid, gid, cuid, cgid, mode, seq;
	long	key;

	adrm_long(adr, &uid, 1);
	adrm_long(adr, &gid, 1);
	adrm_long(adr, &cuid, 1);
	adrm_long(adr, &cgid, 1);
	adrm_long(adr, &mode, 1);
	adrm_long(adr, &seq, 1);
	adrm_long(adr, &key, 1);

	if (flags & M_USERE) {
		if (m_usere == uid)
			checkflags = checkflags | M_USERE;
	}

	if (flags & M_USERE) {
		if (m_usere == cuid)
			checkflags = checkflags | M_USERE;
	}

	if (flags & M_GROUPR) {
		if ((long)m_groupr == gid)
			checkflags = checkflags | M_GROUPR;
	}
#ifdef DUMP
	/* check -T */
	if (dasht && (tokenp->tokenid == AUT_IPC_PERM)) {
		if (check_field(tokenp->t_fields[0], uid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[1], gid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[2], cuid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[3], cgid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[4], mode) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[5], seq) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[6], key) != 0)
			return (-1);
	}
#endif
	return (-1);
}


/*
 * Format of process token:
 *	process token id	adr_char
 *	auid			adr_long
 *	euid			adr_long
 *	egid 			adr_long
 * 	ruid			adr_long
 *	rgid			adr_long
 * 	pid			adr_long
 * 	sid			adr_long
 * 	termid			adr_long*2
 *
 */
int
process_token(adr)
adr_t	*adr;
{
	long	auid, euid, egid, ruid, rgid, pid;
	long	sid;
	au_termid_t termid;

	adrm_long(adr, &auid, 1);
	adrm_long(adr, &euid, 1);
	adrm_long(adr, &egid, 1);
	adrm_long(adr, &ruid, 1);
	adrm_long(adr, &rgid, 1);
	adrm_long(adr, &pid, 1);
	adrm_long(adr, &sid, 1);
	adrm_long(adr, (long *) & termid.port, 1);
	adrm_char(adr, (char *) & termid.machine, 4);

	if (flags & M_OBJECT && obj_flag == OBJ_PROC) {
		if (obj_id == pid)
			checkflags = checkflags | M_OBJECT;
	}
	if (flags & M_USERA) {
		if (m_usera == auid)
			checkflags = checkflags | M_USERA;
	}
	if (flags & M_USERE) {
		if (m_usere == euid)
			checkflags = checkflags | M_USERE;
	}
	if (flags & M_USERR) {
		if (m_userr == ruid)
			checkflags = checkflags | M_USERR;
	}
	if (flags & M_GROUPR) {
		if (m_groupr == egid)
			checkflags = checkflags | M_GROUPR;
	}
	if (flags & M_GROUPE) {
		if (m_groupe == egid)
			checkflags = checkflags | M_GROUPE;
	}
#ifdef DUMP
	/* check -T */
	if (dasht && (tokenp->tokenid == AUT_SUBJECT)) {
		if (check_field(tokenp->t_fields[0], auid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[1], euid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[2], ruid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[3], egid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[4], pid) != 0)
			return (-1);
	}
#endif
	return (-1);
}


/*
 * Format of System V IPC token:
 *	System V IPC token id	adr_char
 *	object id		adr_long
 *
 */
int
s5_IPC_token(adr)
adr_t	*adr;
{
	char	ipc_type;
	long	ipc_id;

	adrm_char(adr, &ipc_type, 1);
	adrm_long(adr, &ipc_id, 1);

	if ((flags & M_OBJECT) &&
	    ipc_type_match(obj_flag, ipc_type) &&
	    (obj_id == ipc_id))
		checkflags = checkflags | M_OBJECT;

#ifdef DUMP
	if (dasht && (tokenp->tokenid == AUT_IPC)) {
		if (check_field(tokenp->t_fields[0], id) != 0)
			return (-1);
	}
#endif /* DUMP */
	return (-1);
}


/*
 * Format of socket token:
 *	socket_type		adrm_short
 *	local_port		adrm_short
 *	local_inaddr		adrm_long
 *	remote_port		adrm_short
 *	remote_inaddr		adrm_long
 *
 */
int
socket_token(adr)
adr_t	*adr;
{
	short	socket_type;
	short	local_port;
	long	local_inaddr;
	short	remote_port;
	long	remote_inaddr;

	adrm_short(adr, &socket_type, 1);
	adrm_short(adr, &local_port, 1);
	adrm_char(adr, (char *) &local_inaddr, 4);
	adrm_short(adr, &remote_port, 1);
	adrm_char(adr, (char *) &remote_inaddr, 4);

	if ((flags & M_OBJECT) && (obj_flag == OBJ_SOCK)) {
		if (socket_flag == SOCKFLG_MACHINE) {
			if (local_inaddr  == obj_id ||
			    remote_inaddr == obj_id)
				checkflags = checkflags | M_OBJECT;
		} else if (socket_flag == SOCKFLG_PORT) {
			if ((long)local_port  == obj_id ||
			    (long)remote_port == obj_id)
				checkflags = checkflags | M_OBJECT;
		}
	}
	return (-1);
}


/*
 * Format of cmw subject token:
 *	subject token id	adr_char
 *	auid			adr_long
 *	euid			adr_long
 *	egid 			adr_long
 * 	ruid			adr_long
 *	rgid			adr_long
 * 	pid			adr_long
 * 	sid			adr_long
 * 	termid			adr_long*2
 *
 */
int
subject_token(adr)
adr_t	*adr;
{
	long	auid, euid, egid, ruid, rgid, pid;
	long	sid;
	au_termid_t termid;

	adrm_long(adr, &auid, 1);
	adrm_long(adr, &euid, 1);
	adrm_long(adr, &egid, 1);
	adrm_long(adr, &ruid, 1);
	adrm_long(adr, &rgid, 1);
	adrm_long(adr, &pid, 1);
	adrm_long(adr, &sid, 1);
	adrm_long(adr, (long *) & termid.port, 1);
	adrm_char(adr, (char *) & termid.machine, 4);

	if (flags & M_SUBJECT) {
		if (subj_id == pid)
			checkflags = checkflags | M_SUBJECT;
	}
	if (flags & M_USERA) {
		if (m_usera == auid)
			checkflags = checkflags | M_USERA;
	}
	if (flags & M_USERE) {
		if (m_usere == euid)
			checkflags = checkflags | M_USERE;
	}
	if (flags & M_USERR) {
		if (m_userr == ruid)
			checkflags = checkflags | M_USERR;
	}
	if (flags & M_GROUPR) {
		if (m_groupr == egid)
			checkflags = checkflags | M_GROUPR;
	}
	if (flags & M_GROUPE) {
		if (m_groupe == egid)
			checkflags = checkflags | M_GROUPE;
	}
#ifdef DUMP
	/* check -T */
	if (dasht && (tokenp->tokenid == AUT_SUBJECT)) {
		if (check_field(tokenp->t_fields[0], auid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[1], euid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[2], ruid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[3], egid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[4], pid) != 0)
			return (-1);
	}
#endif
	return (-1);
}


/*
 * Format of xatom token:
 */
int
xatom_token(adr)
adr_t	*adr;
{
	u_short alen;
	char	*atom;

	adrm_short(adr, (short *) & alen, 1);
	atom = a_calloc(1, (size_t)alen);
	adrm_char(adr, atom, alen);
	free(atom);

	return (-1);
}


/*
 * Format of xobj token:
 */
int
xobj_token(adr)
adr_t	*adr;
{
	long	oid, xid, cuid;

	adrm_long(adr, &oid, 1);
	adrm_long(adr, &xid, 1);
	adrm_long(adr, &cuid, 1);

	return (-1);
}


/*
 * Format of xproto token:
 */
int
xproto_token(adr)
adr_t	*adr;
{
	long	pid;
	adrm_long(adr, &pid, 1);
	return (-1);
}


/*
 * Format of xselect token:
 */
int
xselect_token(adr)
adr_t	*adr;
{
	short	len;
	char	*pstring;
	char	*type;
	char	*data;

	adrm_short(adr, &len, 1);
	pstring = a_calloc(1, (size_t)len);
	adrm_char(adr, pstring, len);
	adrm_short(adr, &len, 1);
	type = a_calloc(1, (size_t)len);
	adrm_char(adr, type, len);
	adrm_short(adr, &len, 1);
	data = a_calloc(1, (size_t)len);
	adrm_char(adr, data, len);
	free(pstring);
	free(type);
	free(data);

	return (-1);
}


#ifdef C2_MARAUDER

/*
 * Format of attribute token:
 *	attribute token id	adr_char
 * 	mode			adr_u_short (printed in octal)
 *	uid			adr_u_short
 *	gid			adr_u_short
 *	file system id		adr_long
 *	node id			adr_long
 *	device			adr_u_short
 *
 */
int
old_attribute_token(adr)
adr_t	*adr;
{
	unsigned short	mode;
	unsigned short	uid, gid;
	long	file_sysid, nodeid;
	unsigned short	dev;

	adrm_u_short(adr, &mode, 1);
	adrm_u_short(adr, &uid, 1);
	adrm_u_short(adr, &gid, 1);
	adrm_long(adr, &file_sysid, 1);
	adrm_long(adr, &nodeid, 1);
	adrm_u_short(adr, &dev, 1);

	if (flags & M_USERE) {
		if (m_usere == uid)
			checkflags = checkflags | M_USERE;
	}
	if (flags & M_GROUPR) {
		if (m_groupr == gid)
			checkflags = checkflags | M_GROUPR;
	}
#ifdef DUMP
	/* check -T */
	if (dasht && (tokenp->tokenid == AUT_ATTR)) {
		if (check_field(tokenp->t_fields[0], mode) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[1], uid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[2], gid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[3], file_sysid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[4], nodeid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[5], dev) != 0)
			return (-1);
	}
#endif
	return (-1);
}


/*
 * Format of path token:
 *	current directory token id	adr_char
 *	root				adr_string
 *	directory			adr_string
 *	path				adr_string
 *
 */
int
old_path_token(adr)
adr_t 	*adr;
{
	int	i;
	char	root[MAXFILELEN+1];
	char	dir[MAXFILELEN+1];
	char	path[MAXFILELEN+1];
	char	*complete_path;

	root[0] = dir[0] = path[0] = '\0';

	adrm_string(adr, root);
	adrm_string(adr, dir);
	adrm_string(adr, path);

	complete_path = a_calloc(1, strlen(root) + strlen(dir) + strlen(path)
	    + 2);

	(void) strcat(complete_path, root);
	(void) strcat(complete_path, dir);
	(void) strcat(complete_path, path);

	if ((flags & M_OBJECT) && (obj_flag == OBJ_PATH)) {
		if (complete_path[0] != '/')
			/*
			 * anchor the path. user apps may not do it.
			 */
			anchor_path(complete_path);
		/*
		 * match against the collapsed path. that is what user sees.
		 */
		if (regex(path_re, collapse_path(complete_path)) !=
		    (char *)NULL) {
			free(complete_path);
			checkflags = checkflags | M_OBJECT;
		}
	}

#ifdef DUMP
	/* check path token: if interesting, returns 0 */
	if (dasht && (tokenp->tokenid == AUT_PATH)) {
		/* check root */
		if (check_field_str(tokenp->t_fields[0], root, 0) != 0)
			return (-1);
		/* check directory */
		if (check_field_str(tokenp->t_fields[1], dir, 0) != 0)
			return (-1);
		/* check path */
		if (check_field_str(tokenp->t_fields[2], path, 0) != 0)
			return (-1);
	}
#endif /* DUMP */
	free(complete_path);
	return (-1);
}


/*
 * Format of process token:
 *	process token id	adr_char
 *	auid			adr_u_short
 *	euid			adr_u_short
 *	ruid			adr_u_short
 *	egid			adr_u_short
 *	pid			adr_u_short
 */
int
old_process_token(adr)
adr_t	*adr;
{
	unsigned short	auid, euid, ruid, egid, pid;

	adrm_u_short(adr, &auid, 1);
	adrm_u_short(adr, &euid, 1);
	adrm_u_short(adr, &ruid, 1);
	adrm_u_short(adr, &egid, 1);
	adrm_u_short(adr, &pid, 1);

	if ((flags & M_OBJECT) && (obj_flag == OBJ_PROC)) {
		if (obj_id == pid)
			checkflags = checkflags | M_OBJECT;
	}
	if (flags & M_USERA) {
		if (m_usera == auid)
			checkflags = checkflags | M_USERA;
	}
	if (flags & M_USERR) {
		if (m_userr == ruid)
			checkflags = checkflags | M_USERR;
	}
	if (flags & M_USERE) {
		if (m_usere == euid)
			checkflags = checkflags | M_USERE;
	}
	if (flags & M_GROUPR) {
		if (m_groupr == egid)
			checkflags = checkflags | M_GROUPR;
	}
#ifdef DUMP
	/* check -T option */
	if (dasht && (tokenp->tokenid == AUT_PROCESS)) {
		if (check_field(tokenp->t_fields[0], auid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[1], euid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[2], ruid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[3], egid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[4], pid) != 0)
			return (-1);
	}
#endif /* DUMP */
	return (-1);
}


/*
 * Format of System V IPC permission token:
 *	System V IPC permission token id	adr_char
 * 	uid					adr_u_short
 *	gid					adr_u_short
 *	cuid					adr_u_short
 *	cgid					adr_u_short
 *	mode					adr_u_short
 *	seq					adr_u_short
 *	key					adr_long
 *	label					adr_opaque, sizeof (bslabel_t)
 *							    bytes
 */
old_s5_IPC_perm_token(adr)
adr_t	*adr;
{
	unsigned short	uid, gid, cuid, cgid, mode, seq;
	long	key;

	adrm_u_short(adr, &uid, 1);
	adrm_u_short(adr, &gid, 1);
	adrm_u_short(adr, &cuid, 1);
	adrm_u_short(adr, &cgid, 1);
	adrm_u_short(adr, &mode, 1);
	adrm_u_short(adr, &seq, 1);
	adrm_long(adr, &key, 1);

	if (flags & M_USERR) {
		if (m_userr == uid || m_userr == cuid)
			checkflags = checkflags | M_USERR;
	}

	if (flags & M_GROUPR) {
		if (m_groupr == gid || m_groupr == cgid)
			checkflags = checkflags | M_GROUPR;
	}
#ifdef DUMP
	/* check -T */
	if (dasht && (tokenp->tokenid == AUT_IPC_PERM)) {
		if (check_field(tokenp->t_fields[0], uid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[1], gid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[2], cuid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[3], cgid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[4], mode) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[5], seq) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[6], key) != 0)
			return (-1);
	}
#endif /* DUMP */
	return (-1);
}


/*
 * Format of subject token:
 *	subject token id	adr_char
 *	auid			adr_u_short
 *	euid			adr_u_short
 * 	ruid			adr_u_short
 *	egid			adr_u_short
 * 	pid			adr_u_short
 */
int
old_subject_token(adr)
adr_t	*adr;
{
	unsigned short	auid, euid, ruid, egid, pid;

	adrm_u_short(adr, &auid, 1);
	adrm_u_short(adr, &euid, 1);
	adrm_u_short(adr, &ruid, 1);
	adrm_u_short(adr, &egid, 1);
	adrm_u_short(adr, &pid, 1);

	if (flags & M_SUBJECT) {
		if (subj_id == pid)
			checkflags = checkflags | M_SUBJECT;
	}
	if (flags & M_USERA) {
		if (m_usera == auid)
			checkflags = checkflags | M_USERA;
	}
	if (flags & M_USERE) {
		if (m_usere == euid)
			checkflags = checkflags | M_USERE;
	}
	if (flags & M_USERR) {
		if (m_userr == ruid)
			checkflags = checkflags | M_USERR;
	}
	if (flags & M_GROUPE) {
		if (m_groupe == egid)
			checkflags = checkflags | M_GROUPE;
	}
#ifdef DUMP
	/* check -T */
	if (dasht && (tokenp->tokenid == AUT_SUBJECT)) {
		if (check_field(tokenp->t_fields[0], auid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[1], euid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[2], ruid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[3], egid) != 0)
			return (-1);
		if (check_field(tokenp->t_fields[4], pid) != 0)
			return (-1);
	}
#endif /* DUMP */
	return (-1);
}


#endif /* C2_MARAUDER */

/*
 * CMW only tokens
 */

#ifdef SunOS_CMW
/*
 * Format of CMW clearance token:
 *	attribute token id	adr_char
 *	clearance		sizeof (bclear_t)
 */
int
clearance_token(adr)
adr_t	*adr;
{
	bclear_t clearance;

	adrm_char(adr, (char *) & clearance, sizeof (bclear_t));
	return (-1);
}


/*
 * Format of ilabel token:
 *	ilabel token id		adr_char
 *	ilabel			adr_char, sizeof (bilabel_t) bytes
 *
 */
ilabel_token(adr)
adr_t	*adr;
{
	bilabel_t ilabel;
	brange_t range;

	adrm_char(adr, &ilabel, sizeof (ilabel));
	if (flags & M_ILABEL) {
		if (exactilab) {
			if (bilequal(&ilow, &ilabel))
				checkflags = checkflags | M_ILABEL;
		} else {
			/* check label range */

			range.lower_bound = ilow.binformation_level;
			range.upper_bound = ihigh.binformation_level;

			if (blinrange(&ilabel.binformation_level, &range))
				checkflags = checkflags | M_ILABEL;
		}
	}
	return (-1);
}


/*
 * Format of CMW privilege token:
 */
int
privilege_token(adr)
adr_t	*adr;
{
	priv_set_t privset;
	adrm_char(adr, privset, sizeof (priv_set_t));
	return (-1);
}


/*
 * Format of slabel token:
 *	slabel token id		adr_char
 *	slabel			adr_char, sizeof (bslabel_t) bytes
 */
slabel_token(adr)
adr_t	*adr;
{
	bslabel_t slabel;
	brange_t range;

	adrm_char(adr, &slabel, sizeof (slabel));
	if (flags & M_SLABEL) {
		if (exactslab) {
			if (blequal(&slow, &slabel))
				checkflags = checkflags | M_SLABEL;
		} else {
			/* check label range */

			range.lower_bound = slow;
			range.upper_bound = shigh;

			if (blinrange(&slabel, &range))
				checkflags = checkflags | M_SLABEL;
		}
	}
#ifdef DUMP
	/* check -T */
	if (dasht && (tokenp->tokenid == AUT_LABEL)) {
		if (check_field_str(tokenp->t_fields[6], bltos(slabel),
		    0) != 0)
			return (-1);
	}
#endif
	return (-1);
}


/*
 * Format of CMW useofpriv token:
 */
int
useofpriv_token(adr)
adr_t	*adr;
{
	char	flag;
	priv_t priv;

	adrm_char(adr, &flag, 1);
	adrm_long(adr, &priv, 1);
	return (-1);
}


#endif /* SunOS_CMW */

/*
 * anchor a path name with a slash
 * assume we have enough space
 */
static void
anchor_path(path)
char	*path;
{
	(void) memmove((void *)(path + 1), (void *)path, strlen(path) + 1);
	*path = '/';
}


/*
 * copy path to collapsed path.
 * collapsed path does not contain:
 *	successive slashes
 *	instances of dot-slash
 *	instances of dot-dot-slash
 * passed path must be anchored with a '/'
 */
static char	*
collapse_path(s)
char	*s; /* source path */
{
	int	id;	/* index of where we are in destination string */
	int	is;	/* index of where we are in source string */
	int	slashseen;	/* have we seen a slash */
	int	ls;		/* length of source string */

	ls = strlen(s) + 1;

	slashseen = 0;
	for (is = 0, id = 0; is < ls; is++) {
		/* thats all folks, we've reached the end of input */
		if (s[is] == '\0') {
			if (id > 1 && s[id-1] == '/') {
				--id;
			}
			s[id++] = '\0';
			break;
		}
		/* previous character was a / */
		if (slashseen) {
			if (s[is] == '/')
				continue;	/* another slash, ignore it */
		} else if (s[is] == '/') {
			/* we see a /, just copy it and try again */
			slashseen = 1;
			s[id++] = '/';
			continue;
		}
		/* /./ seen */
		if (s[is] == '.' && s[is+1] == '/') {
			is += 1;
			continue;
		}
		/* XXX/. seen */
		if (s[is] == '.' && s[is+1] == '\0') {
			if (id > 1)
				id--;
			continue;
		}
		/* XXX/.. seen */
		if (s[is] == '.' && s[is+1] == '.' && s[is+2] == '\0') {
			is += 1;
			if (id > 0)
				id--;
			while (id > 0 && s[--id] != '/');
			id++;
			continue;
		}
		/* XXX/../ seen */
		if (s[is] == '.' && s[is+1] == '.' && s[is+2] == '/') {
			is += 2;
			if (id > 0)
				id--;
			while (id > 0 && s[--id] != '/');
			id++;
			continue;
		}
		while (is < ls && (s[id++] = s[is++]) != '/');
		is--;
	}
	return (s);
}


static int
ipc_type_match(flag, type)
int	flag;
char	type;
{
	if (flag == OBJ_SEM && type == AT_IPC_SEM)
		return (1);

	if (flag == OBJ_MSG && type == AT_IPC_MSG)
		return (1);

	if (flag == OBJ_SHM && type == AT_IPC_SHM)
		return (1);

	return (0);
}


#ifdef DUMP
int
check_field(field, value)
char	*field;
int	value;
{
	if (field && *field) {
		if ((*field != '*') && (value != atoi(field)))
			return (-1);
	}
	return (-1);
}


int
check_field_str(field, value, substr)
char	*field;
char	*value;
int	substr;
{
	if (field && *field) {
		if (substr) {
			if ((*field != '*') && (!substring(field, value)))
				return (-1);
		} else {
			if ((*field != '*') && (strcmp(field, value)))
				return (-1);
		}
	}
	return (0);
}


substring(s1, s2)
char	*s1;
char	*s2;
{
	char	*p;

	p = s1;
	while ((p = strchr(p, s2[0])) != NULL) {
		if (strncmp(p, s2, strlen(s2)) != 0)
			return (1);
		p++;
	}
	return (0);
}


#endif
