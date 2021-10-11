/*
 * File name: praudit.h
 * praudit.c defines, globals
 */

#pragma	ident	"@(#)praudit.h	1.12	94/10/14 SMI"

/* DEFINES */

/*
 * output value types
 */
#define	PRA_INT 0
#define	PRA_UINT 1
#define	PRA_LONG 2
#define	PRA_ULONG 3
#define	PRA_SHORT 4
#define	PRA_USHORT 5
#define	PRA_CHAR 6
#define	PRA_UCHAR 7
#define	PRA_STRING 8
#define	PRA_HEX 9
#define	PRA_SHEX 10
#define	PRA_OCT 11
#define	PRA_BYTE 12
#define	PRA_OUTREC 13
#define	PRA_LOCT 14

/*
 * output mode - set with values of command line flags
 */
#define	DEFAULTM 0
#define	RAWM  1
#define	SHORTM  2

/*
 * source of audit file names
 */
#define	PIPEMODE 0
#define	FILEMODE 1


/*
 * record header field types
 */
#define	RECORD_TYPE 0
#define	RECORD_EVENT 1
#define	RECORD_TIME 2
#define	RECORD_UID 3
#define	RECORD_AUID 4
#define	RECORD_EUID 5
#define	RECORD_GID 6
#define	RECORD_PID 7
#define	RECORD_ERROR 8
#define	RECORD_RETURN 9
#define	RECORD_LABEL 10

/*
 * used with access()
 */
#define	R_OK  4  /* test for read permission */

/*
 * used to indicate one line output per item
 */
#define	NOINDEX -1

/*
 * max. number of audit file names entered on command line
 */
#define	MAXFILENAMES 100

/*
 * max. number of variable length audit record data fields
 */
#define	MAXFIELDS 30

/*
 * max. size of event strings short or long
 */
#define	MAXEVENTSTR 156

/*
 * max. size of file name
 */
#define	MAXFILELEN MAXPATHLEN+MAXNAMLEN+1

/*
 * max. event
 */
#define		MAXEVENT 11

/*
 * Length of your typical privilege string.
 */
#define	PRA_PRIVSTR_LEN 24

/*
 * GLOBALS
 */

/*
 * indicates source of audit file names
 */
static int	file_mode;

/*
 * initial value for output mode
 */
static int	format = DEFAULTM;

/*
 * output value type
 */
static int	uvaltype;

/*
 * one line output or formatted output
 */
static int	ONELINE = 0;

/*
 * field seperator for one line per record output
 */
#define	SEP_SIZE 4
static char	SEPARATOR[SEP_SIZE] = ",";

/*
 * token id for new praudit record format
 */
static signed char	tokenid = -1;

/*
 * do we cache the entire event file?
 */
static int	CACHE = 0;

/*
 * used to store value to be output
 */
static union u_tag {
	int	int_val;
	u_int uint_val;
	long	long_val;
	u_long ulong_val;
	short	short_val;
	u_short ushort_val;
	char	char_val;
	char	uchar_val;
	char	*string_val;
} uval;

/*
 * pointer to audit file
 */

static adr_t *audit_adr;

/*
 * ---------------------------------------------------------------
 *	Table of token names and praudit routines corresponding to
 *	each token type
 * ---------------------------------------------------------------
 */

/* static int pa_file_header(); */
static int	pa_header_token();
static int	pa_exit_token();
static int	pa_file_token();
static int	pa_trailer_token();
static int	pa_arbitrary_data_token();
static int	pa_SystemV_IPC_token();
static int	pa_path_token();
static int	pa_subject_token();
static int	pa_server_token();
static int	pa_process_token();
static int	pa_return_token();
static int	pa_text_token();
static int	pa_opaque_token();
static int	pa_ip_addr();
static int	pa_ip();
static int	pa_iport();
static int	pa_attribute_token();
static int	pa_SystemV_IPC_perm_token();
static int	pa_group_token();
static int	pa_newgroup_token();
static int	pa_arg_token();
static int	pa_socket_token();
static int	pa_sequence_token();
static int	pa_exec_token();
#ifdef	SunOS_CMW
static int	pa_clearance_token();
static int	pa_ilabel_token();
static int	pa_liaison_token();
static int	pa_privilege_token();
static int	pa_slabel_token();
static int	pa_useofpriv_token();
static int	pa_xatom_token();
static int	pa_xobject_token();
static int	pa_xprotocol_token();
static int	pa_xselection_token();
#endif	/* SunOS_CMW */

void	adrf_start  (adr_t *, FILE *);
int	adrf_char   (adr_t *, char *, int);
int	adrf_short  (adr_t *, short *, int);
int	adrf_long   (adr_t *, long *, int);
int	adrf_string (adr_t *, char *);
int	adrf_ushort (adr_t *, u_int *, int);
int	adrf_opaque (adr_t *, char *);
int	adrf_u_char (adr_t *, u_char *, int);
int	adrf_u_long (adr_t *, u_long *, int);
int	adrf_u_short(adr_t *, u_short *, int);

static struct tokentable {
	int	tokenid;		/* token id */
	char	*tokentype;	/* token type, in ASCII */
	int	(*func)();		/* pointer to function call */
};

/*
 * Note to C2/audit developers:
 *	If you change the tokentab, you will also have to update the
 *	structures.po file, in order to keep the internationalization
 *	tables up to date.  The name of each audit token type will
 *	be translated by the gettext() call in pa_gettokenstring().
 */

static struct tokentable tokentab[] = {
		{ AUT_ARG, "argument", pa_arg_token		},
		{ AUT_ATTR, "attribute", pa_attribute_token	},
		{ AUT_DATA, "arbitrary", pa_arbitrary_data_token},
		{ AUT_EXIT, "exit", pa_exit_token		},
		{ AUT_EXEC_ARGS, "exec_args", pa_exec_token 	},
		{ AUT_EXEC_ENV, "exec_env", pa_exec_token	},
		{ AUT_GROUPS, "group", pa_group_token		},
		{ AUT_NEWGROUPS, "group", pa_newgroup_token	},
		{ AUT_HEADER, "header", pa_header_token		},
		{ AUT_IN_ADDR, "ip address", pa_ip_addr		},
		{ AUT_IP, "ip", pa_ip				},
		{ AUT_IPC, "IPC", pa_SystemV_IPC_token		},
		{ AUT_IPC_PERM, "IPC perm", pa_SystemV_IPC_perm_token },
		{ AUT_IPORT, "ip port", pa_iport		},
		{ AUT_OPAQUE, "opaque", pa_opaque_token		},
		{ AUT_OTHER_FILE, "file", pa_file_token		},
		{ AUT_PATH, "path", pa_path_token		},
		{ AUT_PROCESS, "process", pa_process_token	},
		{ AUT_RETURN, "return", pa_return_token		},
		{ AUT_SEQ, "sequence", pa_sequence_token	},
		{ AUT_SERVER, "server", pa_server_token		},
		{ AUT_SOCKET, "socket", pa_socket_token		},
		{ AUT_SUBJECT, "subject", pa_subject_token	},
		{ AUT_TEXT, "text", pa_text_token		},
		{ AUT_TRAILER, "trailer", pa_trailer_token	},
	#ifdef SunOS_CMW
		{ AUT_CLEAR, "clearance", pa_clearance_token	},
		{ AUT_ILABEL, "ilabel", pa_ilabel_token		},
		{ AUT_LIAISON, "liaison", pa_liaison_token	},
		{ AUT_PRIV, "privilege", pa_privilege_token	},
		{ AUT_SLABEL, "slabel", pa_slabel_token		},
		{ AUT_UPRIV, "useofpriv", pa_useofpriv_token	},
		{ AUT_XATOM, "xatom", pa_xatom_token		},
		{ AUT_XOBJ, "xobject", pa_xobject_token		},
		{ AUT_XPROTO, "xprotocol", pa_xprotocol_token	},
		{ AUT_XSELECT, "xselection", pa_xselection_token},
	#endif	/* SunOS_CMW */
	 };


static int	numtokenentries = sizeof (tokentab)
			/ sizeof (struct tokentable);

/*
 * ------------------------------------------------------
 * field widths for arbitrary data token type
 * ------------------------------------------------------
 */
static struct fw {
	char	basic_unit;
	struct {
		char	print_base;
		int	field_width;
	} pwidth[5];
} fwidth[] = {
	/* character data type, 8 bits */
		AUR_CHAR, { AUP_BINARY, 12 },
				{ AUP_OCTAL, 6 },
				{ AUP_DECIMAL, 6 },
				{ AUP_HEX, 6 },
				{ AUP_STRING, 4 },
		AUR_BYTE, { AUP_BINARY, 12 },
				{ AUP_OCTAL, 6 },
				{ AUP_DECIMAL, 6 },
				{ AUP_HEX, 6 },
				{ AUP_STRING, 4 },
		AUR_SHORT, { AUP_BINARY, 20 },
				{ AUP_OCTAL, 10 },
				{ AUP_DECIMAL, 10 },
				{ AUP_HEX, 8 },
				{ AUP_STRING, 6 },
		AUR_INT, { AUP_BINARY, 36 },
				{ AUP_OCTAL, 18 },
				{ AUP_DECIMAL, 18 },
				{ AUP_HEX, 12 },
				{ AUP_STRING, 10 },
		AUR_LONG, { AUP_BINARY, 36 },
				{ AUP_OCTAL, 18 },
				{ AUP_DECIMAL, 18 },
				{ AUP_HEX, 12 },
				{ AUP_STRING, 10 } };


static int	numwidthentries = sizeof (fwidth)
			/ sizeof (struct fw);
