/*
 * Copyright (c) 1990-1994, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)modsysfile.c	1.50	95/10/02 SMI"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bootconf.h>
#include <sys/sysconf.h>
#include <sys/sunddi.h>
#include <sys/esunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/kmem.h>
#include <sys/fs/ufs_fsdir.h>
#include <sys/hwconf.h>
#include <sys/modctl.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <sys/kobj.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/autoconf.h>
#include <sys/bootconf.h>

extern char **syscallnames;

struct hwc_class {
	struct hwc_class *class_next;
	char		*class_exporter;
	char		*class;
};

static struct hwc_class *hcl_head;	/* head of list of classes */

#define	DAFILE		"/etc/driver_aliases"
#define	CLASSFILE	"/etc/driver_classes"

static char *class_file = CLASSFILE;
static char *dafile = DAFILE;

char *systemfile = "etc/system";	/* name of ascii system file */

static struct sysparam *sysparam_hd;	/* head of parameters list */
static struct sysparam *sysparam_tl;	/* tail of parameters list */

struct psm_mach {
	struct psm_mach *m_next;
	char		*m_machname;
};

static struct psm_mach *pmach_head;	/* head of list of classes */

#define	MACHFILE	"/etc/mach"
static char *mach_file = MACHFILE;

static char *rtc_config_file = "/etc/rtc_config";

static void sys_set_var(int, struct sysparam *, void *);
static struct hwc_spec *class_to_hwc(struct hwc_spec *);

static void setparams(int ask);
static int get_string(u_longlong_t *, char *);
static int getvalue(char *, u_longlong_t *);

#ifdef DEBUG
static int parse_debug_on = 0;

/*VARARGS1*/
static void
parse_debug(struct _buf *file, char *fmt, ...)
{
	va_list adx;

	if (parse_debug_on) {
		va_start(adx, fmt);
		vprintf(fmt, adx);
		if (file)
			printf(" on line %d of %s\n", kobj_linenum(file),
				kobj_filename(file));
		va_end(adx);
	}
}
#endif /* DEBUG */

/*VARARGS1*/
static void
file_err(int type,  struct _buf *file, char *fmt, ...)
{
	char buf[256];
	char trailer[256];
	va_list ap;

	va_start(ap, fmt);
	vsprintf(buf, fmt, ap);
	sprintf(trailer, "on line %d of %s", kobj_linenum(file),
		kobj_filename(file));
	cmn_err(type, "%s %s", buf, trailer);
	va_end(ap);
}

#define	isunary(ch)	((ch) == '~' || (ch) == '-')

#define	iswhite(ch)	((ch) == ' ' || (ch) == '\t')

#define	isnewline(ch)	((ch) == '\n' || (ch) == '\r' || (ch) == '\f')

#define	isdigit(ch)	((ch) >= '0' && (ch) <= '9')

#define	isxdigit(ch)	(isdigit(ch) || ((ch) >= 'a' && (ch) <= 'f') || \
			((ch) >= 'A' && (ch) <= 'F'))

#define	isalpha(ch)	(((ch) >= 'a' && (ch) <= 'z') || \
			((ch) >= 'A' && (ch) <= 'Z'))

#define	isalphanum(ch)	(isalpha(ch) || isdigit(ch))

#define	isnamechar(ch)	(isalphanum(ch) || (ch) == '_' || (ch) == '-')

typedef enum {
	EQUALS,
	AMPERSAND,
	BIT_OR,
	STAR,
	POUND,
	COLON,
	SEMICOLON,
	COMMA,
	SLASH,
	WHITE_SPACE,
	NEWLINE,
	EOF,
	STRING,
	HEXVAL,
	DECVAL,
	NAME
} token_t;

#ifdef DEBUG
char *tokennames[] = {
	"EQUALS",
	"AMPERSAND",
	"BIT_OR",
	"STAR",
	"POUND",
	"COLON",
	"SEMICOLON",
	"COMMA",
	"SLASH",
	"WHITE_SPACE",
	"NEWLINE",
	"EOF",
	"STRING",
	"HEXVAL",
	"DECVAL",
	"NAME"
};
#endif /* DEBUG */

static token_t
lex(struct _buf *file, char *val)
{
	register char	*cp;
	register int	ch;
	register token_t token;
	int	oval;
	int	badquote;

	cp = val;
	while ((ch  = kobj_getc(file)) == ' ' || ch == '\t')
		;

	*cp++ = (char)ch;
	switch (ch) {
	case '=':
		token = EQUALS;
		break;
	case '&':
		token = AMPERSAND;
		break;
	case '|':
		token = BIT_OR;
		break;
	case '*':
		token = STAR;
		break;
	case '#':
		token = POUND;
		break;
	case ':':
		token = COLON;
		break;
	case ';':
		token = SEMICOLON;
		break;
	case ',':
		token = COMMA;
		break;
	case '/':
		token = SLASH;
		break;
	case ' ':
	case '\t':
	case '\f':
		while ((ch  = kobj_getc(file)) == ' ' ||
		    ch == '\t' || ch == '\f')
			*cp++ = (char)ch;
		(void) kobj_ungetc(file);
		token = WHITE_SPACE;
		break;
	case '\n':
	case '\r':
		token = NEWLINE;
		break;
	case '"':
		cp--;
		badquote = 0;
		while (!badquote && (ch  = kobj_getc(file)) != '"') {
			switch (ch) {
			case '\n':
			case -1:
				file_err(CE_WARN, file, "Missing \"");
				cp = val;
				*cp++ = '\n';
				badquote = 1;
				/* since we consumed the newline/EOF */
				(void) kobj_ungetc(file);
				break;

			case '\\':
				ch = (char)kobj_getc(file);
				if (!isdigit(ch)) {
					/* escape the character */
					*cp++ = (char)ch;
					break;
				}
				oval = 0;
				while (ch >= '0' && ch <= '7') {
					ch -= '0';
					oval = (oval << 3) + ch;
					ch = (char)kobj_getc(file);
				}
				(void) kobj_ungetc(file);
				/* check for character overflow? */
				if (oval > 127) {
					cmn_err(CE_WARN,
					    "Character "
					    "overflow detected.");
				}
				*cp++ = (char)oval;
				break;
			default:
				*cp++ = (char)ch;
				break;
			}
		}
		token = STRING;
		break;

	default:
		if (ch == -1) {
			token = EOF;
			break;
		}
		if (isunary(ch))
			*cp++ = (char)(ch = kobj_getc(file));

		if (isdigit(ch)) {
			if (ch == '0') {
				if ((ch = kobj_getc(file)) == 'x') {
					*cp++ = (char)ch;
					ch = kobj_getc(file);
					while (isxdigit(ch)) {
						*cp++ = (char)ch;
						ch = kobj_getc(file);
					}
					(void) kobj_ungetc(file);
					token = HEXVAL;
				} else {
					goto digit;
				}
			} else {
				ch = kobj_getc(file);
digit:
				while (isdigit(ch)) {
					*cp++ = (char)ch;
					ch = kobj_getc(file);
				}
				(void) kobj_ungetc(file);
				token = DECVAL;
			}
		} else if (isalpha(ch)) {
			ch = kobj_getc(file);
			while (isnamechar(ch)) {
				*cp++ = (char)ch;
				ch = kobj_getc(file);
			}
			(void) kobj_ungetc(file);
			token = NAME;
		} else {
			return (-1);
		}
		break;
	}
	*cp = '\0';

#ifdef DEBUG
	parse_debug(NULL, "lex: token %s value '%s'\n", tokennames[token], val);
#endif
	return (token);
}

/*
 * Leave NEWLINE as the next character.
 */

static void
find_eol(struct _buf *file)
{
	register int ch;

	while ((ch = kobj_getc(file)) != -1) {
		if (isnewline(ch)) {
			(void) kobj_ungetc(file);
			break;
		}
	}
}

/*
 * The ascii system file is read and processed.
 *
 * The syntax of commands is as follows:
 *
 * '*' in column 1 is a comment line.
 * <command> : <value>
 *
 * command is EXCLUDE, INCLUDE, FORCELOAD, ROOTDEV, ROOTFS,
 *	SWAPDEV, SWAPFS, MODDIR, SET
 *
 * value is an ascii string meaningful for the command.
 */

/*
 * Table of commands
 */
static struct modcmd modcmd[] = {
	{ "EXCLUDE",	MOD_EXCLUDE	},
	{ "exclude",	MOD_EXCLUDE	},
	{ "INCLUDE",	MOD_INCLUDE	},
	{ "include",	MOD_INCLUDE	},
	{ "FORCELOAD",	MOD_FORCELOAD	},
	{ "forceload",	MOD_FORCELOAD	},
	{ "ROOTDEV",	MOD_ROOTDEV	},
	{ "rootdev",	MOD_ROOTDEV	},
	{ "ROOTFS",	MOD_ROOTFS	},
	{ "rootfs",	MOD_ROOTFS	},
	{ "SWAPDEV",	MOD_SWAPDEV	},
	{ "swapdev",	MOD_SWAPDEV	},
	{ "SWAPFS",	MOD_SWAPFS	},
	{ "swapfs",	MOD_SWAPFS	},
	{ "MODDIR",	MOD_MODDIR	},
	{ "moddir",	MOD_MODDIR	},
	{ "SET",	MOD_SET		},
	{ "set",	MOD_SET		},
	{ NULL,		MOD_UNKNOWN	}
};


static char *bad_op = "illegal operator '%s' used on a string";
static char *colon_err = "A colon (:) must follow the '%s' command";
static char *tok_err = "Unexpected token '%s'";
static char *extra_err = "extraneous input ignored starting at '%s'";

static struct sysparam *
do_sysfile_cmd(struct _buf *file, char *cmd)
{
	register struct sysparam *sysp;
	register struct modcmd *mcp;
	register token_t token, op;
	register char *cp;
	register int ch;
	char tok1[MOD_MAXPATH + 1]; /* used to read the path set by 'moddir' */
	char tok2[64];

	for (mcp = modcmd; mcp->mc_cmdname != NULL; mcp++) {
		if (strcmp(mcp->mc_cmdname, cmd) == 0)
			break;
	}
	sysp = kmem_zalloc(sizeof (struct sysparam), KM_SLEEP);
	sysp->sys_op = SETOP_NONE; /* set op to noop initially */

	switch (sysp->sys_type = mcp->mc_type) {
	case MOD_INCLUDE:
	case MOD_EXCLUDE:
	case MOD_FORCELOAD:
		/*
		 * Are followed by colon.
		 */
	case MOD_ROOTFS:
	case MOD_SWAPFS:
		if ((token = lex(file, tok1)) == COLON) {
			token = lex(file, tok1);
		} else {
			file_err(CE_WARN, file, colon_err, cmd);
		}
		if (token != NAME) {
			file_err(CE_WARN, file, "value expected");
			goto bad;
		}

		cp = tok1 + strlen(tok1);
		do {
			*cp++ = (char)(ch = kobj_getc(file));
		} while (!iswhite(ch) && !isnewline(ch) && (ch != -1));
		*(--cp) = '\0';
		if (ch != -1)
			(void) kobj_ungetc(file);
		if (sysp->sys_type == MOD_INCLUDE) {
			goto not_supported;
		}
		sysp->sys_ptr = (char *)kmem_zalloc(strlen(tok1) + 1, KM_SLEEP);
		(void) strcpy(sysp->sys_ptr, tok1);
		break;
	case MOD_SET:
	{
		register char *var;

		if (lex(file, tok1) != NAME) {
			file_err(CE_WARN, file, "value expected");
			goto bad;
		}

		/*
		 * If the next token is a colon (:),
		 * we have the <modname>:<variable> construct.
		 */
		if ((token = lex(file, tok2)) == COLON) {
			if ((token = lex(file, tok2)) == NAME) {
				var = tok2;
				/*
				 * Save the module name.
				 */
				sysp->sys_modnam = kmem_alloc(strlen(tok1) + 1,
					KM_SLEEP);
				(void) strcpy(sysp->sys_modnam, tok1);
				op = lex(file, tok1);
			} else {
				file_err(CE_WARN, file, "value expected");
				goto bad;
			}
		} else {
			/* otherwise, it was the op */
			var = tok1;
			op = token;
		}
		/*
		 * kernel param - place variable name in sys_ptr.
		 */
		sysp->sys_ptr = kmem_alloc(strlen(var) + 1, KM_SLEEP);
		(void) strcpy(sysp->sys_ptr, var);
		/* set operation */
		switch (op) {
		case EQUALS:
			/* simple assignment */
			sysp->sys_op = SETOP_ASSIGN;
			break;
		case AMPERSAND:
			/* bitwise AND */
			sysp->sys_op = SETOP_AND;
			break;
		case BIT_OR:
			/* bitwise OR */
			sysp->sys_op = SETOP_OR;
			break;
		default:
			/* unsupported operation */
			file_err(CE_WARN, file,
				"unsupported operator %s", tok2);
			goto bad;
		} /* end switch */

		switch (lex(file, tok1)) {
		case STRING:
			/* string variable */
			if (sysp->sys_op != SETOP_ASSIGN) {
				file_err(CE_WARN, file, bad_op, tok1);
				goto bad;
			}
			if (get_string(&sysp->sys_info, tok1) == 0) {
				file_err(CE_WARN, file, "string garbled");
				goto bad;
			}
			break;
		case HEXVAL:
		case DECVAL:
			if (getvalue(tok1, &sysp->sys_info) == -1) {
				file_err(CE_WARN, file, "invalid number '%s'",
					tok1);
				goto bad;
			}
		} /* end switch */
		break;

	}
	case MOD_MODDIR:
		if ((token = lex(file, tok1)) != COLON) {
			file_err(CE_WARN, file, colon_err, cmd);
			goto bad;
		}

		cp = tok1;
		while ((token = lex(file, cp)) != NEWLINE && token != EOF) {
			cp += strlen(cp);
			do {
				*cp++ = (char)(ch = kobj_getc(file));
			} while (!iswhite(ch) && !isnewline(ch) &&
			    ch != ':' && (ch != -1));
			*(cp - 1) = ':';
			if (isnewline(ch))
				(void) kobj_ungetc(file);
		}
		(void) kobj_ungetc(file);
		*(cp-1) = '\0';
		sysp->sys_ptr = kmem_alloc(strlen(tok1) + 1, KM_SLEEP);
		strcpy(sysp->sys_ptr, tok1);
		break;

	case MOD_SWAPDEV:
	case MOD_ROOTDEV:
		if ((token = lex(file, tok1)) != COLON) {
			file_err(CE_WARN, file, colon_err, cmd);
			goto bad;
		}
		while ((ch = kobj_getc(file)) == ' ' || ch == '\t')
			;
		(void) kobj_ungetc(file);
		cp = tok1;
		do {
			*cp++ = (char)(ch = kobj_getc(file));
		} while (!iswhite(ch) && !isnewline(ch) && (ch != -1));
		if (ch != -1)
			(void) kobj_ungetc(file);
		*(cp-1) = '\0';
		sysp->sys_ptr = kmem_alloc(strlen(tok1) + 1, KM_SLEEP);
		strcpy(sysp->sys_ptr, tok1);
		break;

	case MOD_UNKNOWN:
	default:
		file_err(CE_WARN, file, "unknown command '%s'", cmd);
		goto bad;
	}

	return (sysp);

bad:
	find_eol(file);
not_supported:
	if (sysp->sys_ptr)
		kmem_free(sysp->sys_ptr, strlen(sysp->sys_ptr) + 1);
	if (sysp->sys_modnam)
		kmem_free(sysp->sys_modnam, strlen(sysp->sys_modnam) + 1);
	kmem_free(sysp, sizeof (struct sysparam));
	return (NULL);
}

#define	MIN_DEFAULT	8
#define	MAX_DEFAULT	1024
#define	MAX_MAXUSERS	2048

/*
 *  setmaxusers is an ugly hack to scale maxusers with memory size
 *  it actually uses the value of physmem, which if calculated is
 *  shy of actual memory by about 1.5 meg.  The values for maxuser are
 *  is equal to the physical memory in physmegs.
 *
 *  This routine is called by setparams ONLY if maxusers is not
 *  set in /etc/system
 */
static void
setmaxusers(int actual)
{
	extern int maxusers;

	u_int phymegs;

	/*
	 * 2^20 is a meg, so shifting right by 20 - PAGESHIFT
	 * converts pages to megs (without overflowing a u_int
	 * if you have more than 4G of memory, like ptob(physmem)/1M
	 * would).
	 */
	phymegs = physmem >> (20 - PAGESHIFT);
	if (actual != 0)
		phymegs += 2;
	/*
	 * Set "maxusers" = physmegs but within reasonable limits.
	 */

	maxusers = min(max(MIN_DEFAULT, phymegs), MAX_DEFAULT);
}

void
mod_read_system_file(int ask)
{
	register struct sysparam *sp;
	register struct _buf *file;
	register token_t token, last_tok;
	char tokval[MAXLINESIZE];

	auto char unixfile[MAXNAMLEN];
	register char *p, *path, *mod_defpath;

	BOP_GETPROP(bootops, "whoami", unixfile);

	/*
	 * Ensure that the default module loading path has the directory
	 * which contains the 'unix' binary first on the path by prepending
	 * the directory name to the default path.  This can be overridden
	 * with 'boot -a' if desired, but this is most often what is wanted.
	 */
	if (default_path == NULL) {
		mod_defpath = MOD_DEFPATH;
		if ((p = strrchr(unixfile, '/')) == (char *)0 ||
		    p == unixfile) {
			path = kobj_zalloc(2 + strlen(mod_defpath) + 1,
				KM_SLEEP);
			default_path = strcat(strcpy(path, "/ "), mod_defpath);
		} else {
			path = kobj_zalloc(2 + p - unixfile +
				strlen(mod_defpath) + 1, KM_SLEEP);
			/*
			 * Enforce leading '/'
			*/
			if (*unixfile != '/') {
				*path = '/';
				(void) strncpy(path + 1, unixfile,
				    p - unixfile);
			/* don't need to null terminate because zalloced */
			} else
				(void) strncpy(path, unixfile, p - unixfile);
			default_path = strcat(strcat(path, " "), mod_defpath);
		}
	}
	if (ask)
		mod_askparams();

	if (systemfile == NULL)
		return;

	if ((file = kobj_open_file(systemfile)) == (struct _buf *)-1) {
		if (moddebug & MODDEBUG_ERRMSG)
			modprintf("can't open system file %s\n", systemfile);
		return;
	}
	sysparam_tl = (struct sysparam *)&sysparam_hd; /* XXX - Too tricky */

	last_tok = NEWLINE;
	while ((token = lex(file, tokval)) != EOF) {
		switch (token) {
			case STAR:
			case POUND:
				/*
				 * Skip comments.
				 */
				if (last_tok != NEWLINE)
					file_err(CE_WARN, file,
						"line comment ignored");
				find_eol(file);
				break;
			case NEWLINE:
				kobj_newline(file);
				last_tok = NEWLINE;
				break;
			case NAME:
				if (last_tok != NEWLINE) {
					file_err(CE_WARN, file,
						extra_err, tokval);
					find_eol(file);
				} else if ((sp = do_sysfile_cmd(file, tokval))
				    != NULL) {
					sp->sys_next = NULL;
					sysparam_tl->sys_next = sp;
					sysparam_tl = sp;
				}
				last_tok = NAME;
				break;
			default:
				/* Error?? */
				file_err(CE_WARN, file, tok_err, tokval);
				find_eol(file);
				break;
		}
	}
	kobj_close_file(file);
	/* now use the info from the system file */
	/*
	 * if setparams returns zero, maxusers was not set and we should
	 * set it based on physical memory (NOTE: we now set
	 * physmem in setparams to get this to take effect
	 */
	setparams(ask);
}

/*
 * Process the system file commands.
 */
int
mod_sysctl(int fcn, void *p)
{
	static char *wmesg = "forceload of %s failed";
	register struct sysparam *sysp;
	register char *name;
	struct modctl *modp;

	if (sysparam_hd == NULL)
		return (0);

	for (sysp = sysparam_hd; sysp != NULL; sysp = sysp->sys_next) {

		switch (fcn) {

		case SYS_FORCELOAD:
		if (sysp->sys_type == MOD_FORCELOAD) {
			name = sysp->sys_ptr;
			if (strncmp(sysp->sys_ptr, "drv", 3) == 0) {
				if (ddi_install_driver(name + 4) ==
				    DDI_FAILURE) {
					cmn_err(CE_WARN, wmesg, name);
					break;
				}
			} else {
				if (modload(NULL, name) == -1)
					cmn_err(CE_WARN, wmesg, name);
			}
			/*
			 * The following works because it
			 * runs before autounloading is started!!
			 * XXX - Do we need to forceload drivers?
			 */
			modp = mod_find_by_filename(NULL, name);
			if (modp != NULL)
				modp->mod_loadflags |= MOD_NOAUTOUNLOAD;
		}
		break;

		case SYS_SET_KVAR:
		case SYS_SET_MVAR:
			if (sysp->sys_type == MOD_SET)
				sys_set_var(fcn, sysp, p);
			break;

		case SYS_CHECK_EXCLUDE:
			if (sysp->sys_type == MOD_EXCLUDE) {
				if (p == NULL || sysp->sys_ptr == NULL)
					return (0);
				if (strcmp((char *)p, sysp->sys_ptr) == 0)
					return (1);
			}
		}
	}
	param_check();

	return (0);
}

/*
 * Process the system file commands, by type.
 */
int
mod_sysctl_type(int type, int (*func)(struct sysparam *, void *), void *p)
{
	register struct sysparam *sysp;
	int	err;

	for (sysp = sysparam_hd; sysp != NULL; sysp = sysp->sys_next)
		if (sysp->sys_type == type)
			if (err = (*(func))(sysp, p))
				return (err);
	return (0);
}


static char *seterr = "Symbol %s has size of 0 in symbol table. %s";
static char *assumption = "Assuming it is a 'long'";
static char *defmsg = "Trying to set a variable that is of size %d";

static void set_char_var(u_int, struct sysparam *);
static void set_short_var(u_int, struct sysparam *);
static void set_long_var(u_int, struct sysparam *);
static void set_llong_var(u_int, struct sysparam *);

static void
sys_set_var(int fcn, struct sysparam *sysp, void *p)
{
	register int symaddr;
	int size;

	if (fcn == SYS_SET_KVAR && sysp->sys_modnam == NULL) {
		symaddr = (kobj_getelfsym(sysp->sys_ptr, NULL, &size));
	} else if (fcn == SYS_SET_MVAR) {
		if (sysp->sys_modnam == (char *)NULL ||
			strcmp(((struct modctl *)p)->mod_modname,
			    sysp->sys_modnam) != 0)
				return;
		symaddr = kobj_getelfsym(sysp->sys_ptr,
		    ((struct modctl *)p)->mod_mp, &size);
	} else
		return;

	if (symaddr != NULL) {
		switch (size) {
		case 1:
			set_char_var(symaddr, sysp);
			break;
		case 2:
			set_short_var(symaddr, sysp);
			break;
		case 0:
			cmn_err(CE_WARN, seterr, sysp->sys_ptr, assumption);
			/*FALLTHROUGH*/
		case 4:
			set_long_var(symaddr, sysp);
			break;
		case 8:
			set_llong_var(symaddr, sysp);
			break;
		default:
			cmn_err(CE_WARN, defmsg, size);
			break;
		}
	} else {
		modprintf("sorry, variable '%s' is not defined in the '%s' ",
		    sysp->sys_ptr,
		    sysp->sys_modnam ? sysp->sys_modnam : "kernel");
		if (sysp->sys_modnam)
			modprintf("module");
		modprintf("\n");
	}
}

static void
set_char_var(u_int symaddr, struct sysparam *sysp)
{
	char c;

	if (moddebug & MODDEBUG_LOADMSG)
		modprintf("OP: %x: param '%s' was '0x%x' in module: '%s'.\n",
		    sysp->sys_op, sysp->sys_ptr, *(char *)symaddr,
		    sysp->sys_modnam);

	switch (sysp->sys_op) {
	case SETOP_ASSIGN:
	    *(char *)symaddr = sysp->sys_info;
	    break;
	case SETOP_AND:
	    /* *(char *)symaddr &= sysp->sys_info; */
	    c = sysp->sys_info;
	    *(char *)symaddr &= c;
	    break;
	case SETOP_OR:
	    /* *(char *)symaddr |= sysp->sys_info; */
	    c = sysp->sys_info;
	    *(char *)symaddr |= c;
	    break;
	}

	if (moddebug & MODDEBUG_LOADMSG)
		modprintf("now it is set to '0x%x'.\n", *(char *)symaddr);
}

static void
set_short_var(u_int symaddr, struct sysparam *sysp)
{
	short sh;

	if (moddebug & MODDEBUG_LOADMSG)
		modprintf("OP: %x: param '%s' was '0x%x' in module: '%s'.\n",
		    sysp->sys_op, sysp->sys_ptr, *(short *)symaddr,
		    sysp->sys_modnam);

	switch (sysp->sys_op) {
	case SETOP_ASSIGN:
	    *(short *)symaddr = sysp->sys_info;
	    break;
	case SETOP_AND:
	    /* *(short *)symaddr &= sysp->sys_info; */
	    sh = sysp->sys_info;
	    *(short *)symaddr &= sh;

	    break;
	case SETOP_OR:
	    /* *(short *)symaddr |= sysp->sys_info; */
	    sh = sysp->sys_info;
	    *(short *)symaddr |= sh;
	    break;
	}

	if (moddebug & MODDEBUG_LOADMSG)
		modprintf("now it is set to '0x%x'.\n", *(short *)symaddr);
}

static void
set_long_var(u_int symaddr, struct sysparam *sysp)
{
	if (moddebug & MODDEBUG_LOADMSG)
		modprintf("OP: %x: param '%s' was '0x%x' in module: '%s'.\n",
		    sysp->sys_op, sysp->sys_ptr, *(u_long *)symaddr,
		    sysp->sys_modnam);

	switch (sysp->sys_op) {
	case SETOP_ASSIGN:
	    *(u_long *)symaddr = sysp->sys_info;
	    break;
	case SETOP_AND:
	    *(u_long *)symaddr &= sysp->sys_info;
	    break;
	case SETOP_OR:
	    *(u_long *)symaddr |= sysp->sys_info;
	    break;
	}

	if (moddebug & MODDEBUG_LOADMSG)
		modprintf("now it is set to '0x%x'.\n", *(u_long *)symaddr);
}

static void
set_llong_var(u_int symaddr, struct sysparam *sysp)
{
	if (moddebug & MODDEBUG_LOADMSG)
		modprintf("OP: %x: param '%s' was '0x%x' in module: '%s'.\n",
		    sysp->sys_op, sysp->sys_ptr, *(u_longlong_t *)symaddr,
		    sysp->sys_modnam);

	switch (sysp->sys_op) {
	case SETOP_ASSIGN:
	    *(u_longlong_t *)symaddr = sysp->sys_info;
	    break;
	case SETOP_AND:
	    *(u_longlong_t *)symaddr &= sysp->sys_info;
	    break;
	case SETOP_OR:
	    *(u_longlong_t *)symaddr |= sysp->sys_info;
	    break;
	}

	if (moddebug & MODDEBUG_LOADMSG)
		modprintf("now it is set to '0x%x'.\n",
		    *(u_longlong_t *)symaddr);
}

/*
 * The next item on the line is a string value. Allocate memory for
 * it and copy the string. Return 1, and set arg ptr to newly allocated
 * and initialized buffer, or NULL if an error occurs.
 */
static int
get_string(u_longlong_t *llptr, char *tchar)
{
	register char *cp;
	register char *start = (char *)0;
	register int len = 0;

	len = strlen(tchar);
	start = tchar;
	/* copy string */
	cp = kmem_zalloc(len + 1, KM_SLEEP);
	*llptr = (u_longlong_t)cp;
	for (; len > 0; len--) {
		/* convert some common escape sequences */
		if (*start == '\\') {
			switch (*(start + 1)) {
			case 't':
				/* tab */
				*cp++ = '\t';
				len--;
				start += 2;
				break;
			case 'n':
				/* new line */
				*cp++ = '\n';
				len--;
				start += 2;
				break;
			case 'b':
				/* back space */
				*cp++ = '\b';
				len--;
				start += 2;
				break;
			default:
				/* simply copy it */
				*cp++ = *start++;
				break;
			}
		} else
			*cp++ = *start++;
	}
	*cp = '\0';
	return (1);
}

/*
 * get a decimal octal or hex number. Handle '~' for one's complement.
 */
static int
getvalue(char *token, u_longlong_t *valuep)
{
	register int radix;
	register u_longlong_t retval = 0;
	register int onescompl = 0;
	register int negate = 0;
	register char c;

	if (*token == '~') {
		onescompl++; /* perform one's complement on result */
		token++;
	} else if (*token == '-') {
		negate++;
		token++;
	}
	if (*token == '0') {
		token++;
		c = *token;

		if (c == '\0') {
			*valuep = 0;	/* value is 0 */
			return (0);
		}

		if (c == 'x' || c == 'X') {
			radix = 16;
			token++;
		} else
			radix = 8;
	} else
		radix = 10;

	while ((c = *token++)) {
		switch (radix) {
		case 8:
			if (c >= '0' && c <= '7')
				c -= '0';
			else
				return (-1);	/* invalid number */
			retval = (retval << 3) + c;
			break;
		case 10:
			if (c >= '0' && c <= '9')
				c -= '0';
			else
				return (-1);	/* invalid number */
			retval = (retval * 10) + c;
			break;
		case 16:
			if (c >= 'a' && c <= 'f')
				c = c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				c = c - 'A' + 10;
			else if (c >= '0' && c <= '9')
				c -= '0';
			else
				return (-1);	/* invalid number */
			retval = (retval << 4) + c;
			break;
		}
	}
	if (onescompl)
		retval = ~retval;
	if (negate)
		retval = -retval;
	*valuep = retval;
	return (0);
}

/*
 * set parameters that can be set early during initialization.
 */
static void
setparams(int ask)
{
	register struct sysparam *sysp;
	register struct bootobj *bootobjp;
	int maxuset = 0;
	int physmset = 0;

	extern maxusers;

	if (sysparam_hd == NULL) {
		setmaxusers(0);
		return;
	}

	/*
	 * in order to correctly scale the default maxusers
	 * we need to take in physmem now
	 */
	for (sysp = sysparam_hd; sysp != NULL; sysp = sysp->sys_next) {

		if (ask && (sysp->sys_type != MOD_SET ||
		    (strcmp(sysp->sys_ptr, "maxusers") != 0 &&
		    strcmp(sysp->sys_ptr, "physmem") != 0)))
			continue;

		if (sysp->sys_type == MOD_SET) {
			if (strcmp(sysp->sys_ptr, "physmem") == 0) {
				switch (sysp->sys_op) {
				case SETOP_ASSIGN:
					physmem = sysp->sys_info;
					break;
				case SETOP_AND:
					physmem &= sysp->sys_info;
					break;
				case SETOP_OR:
					physmem |= sysp->sys_info;
					break;
				}
				physmset++;
				continue;
			} else if (strcmp(sysp->sys_ptr, "maxusers") == 0) {
				switch (sysp->sys_op) {
				case SETOP_ASSIGN:
					maxusers = sysp->sys_info;
					break;
				case SETOP_AND:
					maxusers &= sysp->sys_info;
					break;
				case SETOP_OR:
					maxusers |= sysp->sys_info;
					break;
				}
				maxuset++;
				continue;
			}
		}

		if (sysp->sys_type == MOD_MODDIR) {
			default_path = sysp->sys_ptr;
			continue;
		}

		if (sysp->sys_type == MOD_ROOTDEV ||
		    sysp->sys_type == MOD_ROOTFS)
			bootobjp = &rootfs;
		else
			bootobjp = &swapfile;

		switch (sysp->sys_type) {
		case MOD_ROOTDEV:
		case MOD_SWAPDEV:
			bootobjp->bo_flags |= BO_VALID;
			copystr(sysp->sys_ptr, bootobjp->bo_name,
			    BO_MAXOBJNAME, NULL);
			break;
		case MOD_ROOTFS:
		case MOD_SWAPFS:
			bootobjp->bo_flags |= BO_VALID;
			copystr(sysp->sys_ptr, bootobjp->bo_fstype,
			    BO_MAXOBJNAME, NULL);
			break;
		}
	}
	if (maxuset == 0) {
		setmaxusers(physmset);
	} else {
		/*
		 *	keep maxusers within known bounds
		 */
		if (maxusers > MAX_MAXUSERS) {
			maxusers = MAX_MAXUSERS;
			cmn_err(CE_NOTE, "maxusers limited to %d",
				MAX_MAXUSERS);
		}

	}
}

/*
 * clean up after an error.
 */

void
hwc_free(struct hwc_spec *hwcp)
{
	register char *name;

	if ((name = hwcp->hwc_parent_name) != NULL)
		kmem_free(name, strlen(name) + 1);
	if ((name = ddi_get_name(hwcp->hwc_proto)) != NULL)
		kmem_free(name, strlen(name) + 1);
	e_ddi_prop_remove_all(hwcp->hwc_proto);
	kmem_free(hwcp->hwc_proto, sizeof (struct dev_info));
	kmem_free(hwcp, sizeof (struct hwc_spec));
}

struct val_list {
	struct val_list *val_next;
	int		val_type;
	int		val_size;
	union {
		char *string;
		int integer;
	} val;
};

static void
add_val(struct val_list **val_listp, int val_type, caddr_t val)
{
	register struct val_list *new_val, *listp = *val_listp;

	new_val = kmem_alloc(sizeof (struct val_list), KM_SLEEP);
	new_val->val_next = NULL;
	if ((new_val->val_type = val_type) == 0) {
		new_val->val_size = strlen((char *)val) + 1;
		new_val->val.string = (char *)val;
	} else {
		new_val->val_size = sizeof (int);
		new_val->val.integer = (int)val;
	}

	if (listp) {
		while (listp->val_next) {
			listp = listp->val_next;
		}
		listp->val_next = new_val;
	} else {
		*val_listp = new_val;
	}
}

static void
make_prop(struct _buf *file, dev_info_t *devi, char *name, struct val_list *val)
{
	register int proplen = 0, val_type;
	register struct val_list *vl, *tvl;
	caddr_t valbuf = NULL;
	register char *valsp;
	register int *valip;

	if (name == NULL)
		return;

#ifdef DEBUG
	parse_debug(NULL, "%s", name);
#endif
	if (val) {
		for (vl = val, val_type = vl->val_type; vl; vl = vl->val_next) {
			if (val_type != vl->val_type) {
				cmn_err(CE_WARN, "Mixed types in value list");
				return;
			}
			proplen += vl->val_size;
		}
		valbuf = kmem_alloc(proplen, KM_SLEEP);
		valsp = (char *)valbuf;
		valip = (int *)valbuf;
		vl = val;
		while (vl) {
			tvl = vl;
			vl = vl->val_next;
			if (val_type == 0) {
#ifdef DEBUG
				parse_debug(NULL, " %s", tvl->val.string);
#endif
				strcpy(valsp, tvl->val.string);
				valsp += tvl->val_size;
				kmem_free(tvl->val.string, tvl->val_size);
			} else {
#ifdef DEBUG
				parse_debug(NULL, " %x",  tvl->val.integer);
#endif
				*valip++ = tvl->val.integer;
			}
			kmem_free(tvl, sizeof (struct val_list));
		}
	}
#ifdef DEBUG
	parse_debug(NULL, "\n");
#endif
	if (e_ddi_prop_create(DDI_DEV_T_NONE, devi, 0, name, valbuf,
	    proplen) != DDI_PROP_SUCCESS) {
		file_err(CE_WARN, file, "cannot create property %s", name);
	}
	kmem_free(name, strlen(name) + 1);
	if (valbuf)
		kmem_free(valbuf, proplen);
}

static char *omit_err = "(maybe ';' omitted on previous spec!";
static char *prnt_err = "'parent' attr already specified";
static char *nm_err = "'name' attr already specified";
static char *class_err = "'class' attr already specified";

typedef enum {
	hwc_begin, parent, drvname, drvclass, prop,
	parent_equals, name_equals, drvclass_equals,
	parent_equals_string, name_equals_string,
	drvclass_equals_string,
	prop_equals, prop_equals_string, prop_equals_integer,
	prop_equals_string_comma, prop_equals_integer_comma
} hwc_state_t;

static struct hwc_spec *
get_hwc_spec(struct _buf *file, char *tokbuf)
{
	register char *prop_name, *string;
	register token_t token;
	register struct hwc_spec *hwcp;
	register struct dev_info *devi;
	struct val_list *val_list;
	hwc_state_t state;
	u_longlong_t ival;

	hwcp = kmem_zalloc(sizeof (*hwcp), KM_SLEEP);
	devi = kmem_zalloc(sizeof (*devi), KM_SLEEP);
	hwcp->hwc_proto = (dev_info_t *)devi;

	state = hwc_begin;
	token = NAME;
	prop_name = NULL;
	val_list = NULL;
	do {
		switch (token) {
		case NAME:
			switch (state) {
			case prop:
			case prop_equals_string:
			case prop_equals_integer:
				make_prop(file, (dev_info_t *)devi,
				    prop_name, val_list);
				prop_name = NULL;
				val_list = NULL;
				/*FALLTHROUGH*/
			case hwc_begin:
				if (strcmp(tokbuf, "PARENT") == 0 ||
				    strcmp(tokbuf, "parent") == 0) {
					state = parent;
				} else if (strcmp(tokbuf, "NAME") == 0 ||
				    strcmp(tokbuf, "name") == 0) {
					state = drvname;
				} else if (strcmp(tokbuf, "CLASS") == 0 ||
				    strcmp(tokbuf, "class") == 0) {
					state = drvclass;
				} else {
					state = prop;
					prop_name = kmem_alloc(strlen(tokbuf) +
						1, KM_SLEEP);
					strcpy(prop_name, tokbuf);
				}
				break;
			default:
				file_err(CE_WARN, file, tok_err, tokbuf);
			}
			break;
		case EQUALS:
			switch (state) {
			case drvname:
				state = name_equals;
				break;
			case parent:
				state = parent_equals;
				break;
			case drvclass:
				state = drvclass_equals;
				break;
			case prop:
				state = prop_equals;
				break;
			default:
				file_err(CE_WARN, file, tok_err, tokbuf);
			}
			break;
		case STRING:
			string = kmem_alloc(strlen(tokbuf) + 1, KM_SLEEP);
			strcpy(string, tokbuf);
			switch (state) {
			case name_equals:
				if (ddi_get_name(hwcp->hwc_proto)) {
					file_err(CE_WARN, file, "%s %s",
						nm_err, omit_err);
					hwc_free(hwcp);
					return (NULL);
				}
				devi->devi_name = string;
				state = hwc_begin;
				break;
			case parent_equals:
				if (hwcp->hwc_parent_name) {
					file_err(CE_WARN, file, "%s %s",
						prnt_err, omit_err);
					hwc_free(hwcp);
					return (NULL);
				}
				hwcp->hwc_parent_name = string;
				state = hwc_begin;
				break;
			case drvclass_equals:
				if (hwcp->hwc_class_name) {
					file_err(CE_WARN, file, class_err);
					hwc_free(hwcp);
					return (NULL);
				}
				hwcp->hwc_class_name = string;
				state = hwc_begin;
				break;
			case prop_equals:
			case prop_equals_string_comma:
				add_val(&val_list, 0, string);
				state = prop_equals_string;
				break;
			default:
				file_err(CE_WARN, file, tok_err, tokbuf);
			}
			break;
		case HEXVAL:
		case DECVAL:
			switch (state) {
			case prop_equals:
			case prop_equals_integer_comma:
				(void) getvalue(tokbuf, &ival);
				add_val(&val_list, 1, (caddr_t)ival);
				state = prop_equals_integer;
				break;
			default:
				file_err(CE_WARN, file, tok_err, tokbuf);
			}
			break;
		case COMMA:
			switch (state) {
			case prop_equals_string:
				state = prop_equals_string_comma;
				break;
			case prop_equals_integer:
				state = prop_equals_integer_comma;
				break;
			default:
				file_err(CE_WARN, file, tok_err, tokbuf);
			}
			break;
		case NEWLINE:
			kobj_newline(file);
			break;
		case POUND:
			find_eol(file);
			break;
		case EOF:
			file_err(CE_WARN, file, "Unexpected EOF");
			hwc_free(hwcp);
			return (NULL);
		default:
			file_err(CE_WARN, file, tok_err, tokbuf);
			hwc_free(hwcp);
			return (NULL);
		}
	} while ((token = lex(file, tokbuf)) != SEMICOLON);

	switch (state) {
	case prop:
	case prop_equals_string:
	case prop_equals_integer:
		make_prop(file, (dev_info_t *)devi,
			prop_name, val_list);
		break;

	case hwc_begin:
		break;
	default:
		file_err(CE_WARN, file, "Unexpected end of line");
		break;
	}
	return (hwcp);
}

static char *no_class = "hwc_parse: No expansion for class %s";

/*
 * hwc_parse -- parse an hwconf file.  Ignore error lines and parse
 * as much as possible.
 */
struct hwc_spec *
hwc_parse(register char *fname)
{
	register struct _buf *file;
	register struct hwc_spec *hwcp, *hwcp1;
	register struct hwc_spec *hwcp_head, *hwcp_tail;
	register char *tokval;
	register token_t token;

	if ((file = kobj_open_path(fname, 1)) == (struct _buf *)-1) {
		if (moddebug & MODDEBUG_ERRMSG)
			cmn_err(CE_WARN, "Cannot open %s", fname);
		return (NULL);
	}

	/*
	 * Initialize variables
	 */
	tokval = kmem_alloc(MAX_HWC_LINESIZE, KM_SLEEP);
	hwcp_head = NULL;
	hwcp_tail = NULL;

	while ((token = lex(file, tokval)) != EOF) {
		switch (token) {
		case POUND:
			/*
			 * Skip comments.
			 */
			find_eol(file);
			break;
		case NAME:
			if (hwcp = get_hwc_spec(file, tokval)) {
				/*
				 * No name, class, and parent indicates
				 * that this really a global property
				 * entry, which will be processed later.
				 * However, it still an error to specify
				 * a name and no parent/class, or a
				 * parent/class and no name.
				 */
				if ((hwcp->hwc_parent_name == NULL &&
				    hwcp->hwc_class_name == NULL) &&
				    ddi_get_name(hwcp->hwc_proto) != NULL) {
					file_err(CE_WARN, file,
					    "missing parent or class "
					    "attribute");
					hwc_free(hwcp);
					continue;
				} else if ((hwcp->hwc_parent_name != NULL ||
				    hwcp->hwc_class_name != NULL) &&
				    ddi_get_name(hwcp->hwc_proto) == NULL) {
					file_err(CE_WARN, file,
					    "missing name attribute");
					hwc_free(hwcp);
					continue;
				} else if (hwcp->hwc_class_name != NULL) {
					hwcp1 = class_to_hwc(hwcp);
					if (hwcp1 == NULL) {
						cmn_err(CE_WARN, no_class,
						    hwcp->hwc_class_name);
						hwc_free(hwcp);
						continue;
					} else {
						hwcp = hwcp1;
					}
				}
				/*
				 * link this hwcp into the list of the hwc_specs
				 */
				if (hwcp_head == NULL)
					hwcp_head = hwcp;
				else
					hwcp_tail->hwc_next = hwcp;

				for (hwcp1 = hwcp; hwcp1->hwc_next != NULL;
				    hwcp1 = hwcp1->hwc_next)
					;
				hwcp_tail = hwcp1;
			}
			break;
		case NEWLINE:
			kobj_newline(file);
			break;
		default:
			/* Error? */
			break;
		}
	}
	/*
	 * XXX - Check for clean termination.
	 */
	kmem_free(tokval, MAX_HWC_LINESIZE);
	kobj_close_file(file);
	return (hwcp_head);
}

int
make_devname(char *name, int major)
{
	struct devnames *dn;

	dn = &devnamesp[major];
	if (dn->dn_name && strcmp(dn->dn_name, name) == 0) {
		return (0);	/* Adding same driver */
	} else if (dn->dn_name && dn->dn_flags != 0)
		return (EINVAL);	/* Another driver already here! */

	if (dn->dn_name)
		kmem_free(dn->dn_name, strlen(dn->dn_name) + 1);
	dn->dn_name = kmem_alloc(strlen(name) + 1, KM_SLEEP);
	strcpy(dn->dn_name, name);
	dn->dn_flags = 0;
	return (0);
}

void
make_syscallname(char *name, int sysno)
{
	register char **cp;

	cp = &syscallnames[sysno];
	if ((*cp = kmem_alloc(strlen(name) + 1, KM_NOSLEEP)) == NULL) {
		cmn_err(CE_PANIC, "Not enough memory for syscallnames");
	}
	(void) strcpy(*cp, name);
}

typedef enum {
	new_alias, driver_name_, driver_name_comma_, alias_, alias_comma_
} alias_state_t;

void
make_aliases(struct bind **bhead)
{
	register struct _buf *file;
	char tokbuf[MAXNAMELEN];
	char drvbuf[MAXNAMELEN];
	register token_t token;
	register alias_state_t state;
	register major_t major;

	if ((file = kobj_open_file(dafile)) == (struct _buf *)-1)
		return;

	state = new_alias;
	major = (major_t)-1;
	while ((token = lex(file, tokbuf)) != EOF) {
		switch (token) {
		case POUND:
			state = new_alias;
			find_eol(file);
			break;
		case NAME:
		case STRING:
			switch (state) {
			case new_alias:
				strcpy(drvbuf, tokbuf);
				state = driver_name_;
				break;
			case driver_name_comma_:
				strcat(drvbuf, tokbuf);
				state = driver_name_;
				break;
			case alias_comma_:
				strcat(drvbuf, tokbuf);
				state = alias_;
				break;
			case driver_name_:
				major = ddi_name_to_major(drvbuf);
				if (major == (major_t)-1) {
					find_eol(file);
					state = new_alias;
				} else {
					strcpy(drvbuf, tokbuf);
					state = alias_;
				}
				break;
			case alias_:
				make_mbind(drvbuf, major, bhead);
				break;
			}
			break;
		case COMMA:
			strcat(drvbuf, tokbuf);
			switch (state) {
			case driver_name_:
				state = driver_name_comma_;
				break;
			case alias_:
				state = alias_comma_;
				break;
			default:
				file_err(CE_WARN, file, tok_err, tokbuf);
			}
			break;
		case NEWLINE:
			if (state == alias_)
				make_mbind(drvbuf, major, bhead);
			else if (state != new_alias)
				file_err(CE_WARN, file, "Missing alias for %s",
					drvbuf);
			kobj_newline(file);
			state = new_alias;
			major = (major_t)-1;
			break;
		default:
			file_err(CE_WARN, file, tok_err, tokbuf);
		}
	}

	if (state == alias_)
		make_mbind(drvbuf, major, bhead);

	kobj_close_file(file);
}

static char *mem_err = "Not enough memory for binding";
static char *num_err = "Missing number on preceding line?";

typedef enum {
	new_bind, name_
} bind_state_t;

int
read_binding_file(char *bindfile, struct bind **bhead)
{
	struct _buf *file;
	register struct bind *bp, *bp1;
	char tokbuf[MAXNAMELEN];
	register char *name;
	register token_t token;
	register bind_state_t state;
	register int maxnum = 0;
	u_longlong_t val;

	if (*bhead != NULL) {
		bp = *bhead;
		while (bp != NULL) {
			kmem_free(bp->b_name, strlen(bp->b_name) + 1);
			bp1 = bp;
			bp = bp->b_next;
			kmem_free(bp1, sizeof (struct bind));
		}
		*bhead = NULL;
	}

	if ((file = kobj_open_file(bindfile)) == (struct _buf *)-1) {
		/*
		 * ZZZ This shouldn't panic!
		 */
		cmn_err(CE_PANIC, "read_binding_file: %s file not found",
		    bindfile);
	}
	state = new_bind;

	while ((token = lex(file, tokbuf)) != EOF) {
		switch (token) {
		case POUND:
			state = new_bind;
			find_eol(file);
			break;
		case NAME:
		case STRING:
			if (state != new_bind) {
				file_err(CE_WARN, file, num_err);
			}
			if ((name = kmem_alloc(strlen(tokbuf) + 1,
			    KM_NOSLEEP)) == NULL) {
				file_err(CE_PANIC, file, mem_err);
			}
			strcpy(name, tokbuf);
			state = name_;
			break;
		case HEXVAL:
		case DECVAL:
			if (state != name_) {
				file_err(CE_WARN, file, "Missing name?");
				state = new_bind;
				continue;
			}
			(void) getvalue(tokbuf, &val);
			make_mbind(name, (int)val, bhead);
			if ((int)val > maxnum)
				maxnum = (int)val;
			state = new_bind;
			break;
		case NEWLINE:
			kobj_newline(file);
			break;
		default:
			/* Error */
			file_err(CE_WARN, file, "Missing name/number?");
			break;
		}
	}
	kobj_close_file(file);
	return (maxnum);
}

void
add_class(char *exporter, char *class)
{
	register struct hwc_class *hcl;

	hcl = (struct hwc_class *)kmem_zalloc(sizeof (struct hwc_class),
	    KM_NOSLEEP);
	hcl->class_exporter = kmem_zalloc(strlen(exporter) + 1, KM_NOSLEEP);
	hcl->class = kmem_zalloc(strlen(class) + 1, KM_NOSLEEP);
	if (hcl == NULL || hcl->class_exporter == NULL || hcl->class == NULL) {
		cmn_err(CE_PANIC, "Not enough memory for class file");
	}
	strcpy(hcl->class_exporter, exporter);
	strcpy(hcl->class, class);
	hcl->class_next = hcl_head;
	hcl_head = hcl;
}

typedef enum {
	class_begin, new_class, exporter_
} class_state_t;

void
read_class_file(void)
{
	struct _buf *file;
	struct hwc_class *hcl, *hcl1;
	char tokbuf[MAXNAMELEN];
	register class_state_t state;
	register token_t token;
	register char *exporter = NULL, *class = NULL, *name = NULL;

	if (hcl_head != NULL) {
		hcl = hcl_head;
		while (hcl != NULL) {
			kmem_free(hcl->class_exporter,
			    strlen(hcl->class_exporter) + 1);
			hcl1 = hcl;
			hcl = hcl->class_next;
			kmem_free(hcl1, sizeof (struct hwc_class));
		}
		hcl_head = NULL;
	}

	if ((file = kobj_open_file(class_file)) == (struct _buf *)-1)
		return;

	state = class_begin;
	while ((token = lex(file, tokbuf)) != EOF) {
		switch (token) {
		case POUND:
			find_eol(file);
			break;
		case NAME:
		case STRING:
			name = kmem_alloc(strlen(tokbuf) + 1, KM_SLEEP);
			strcpy(name, tokbuf);
			switch (state) {
			case class_begin:
				exporter = name;
				name = NULL;
				state = exporter_;
				break;
			case exporter_:
				class = name;
				name = NULL;
				add_class(exporter, class);
				kmem_free(exporter, strlen(exporter) + 1);
				exporter = NULL;
				kmem_free(class, strlen(class) + 1);
				class = NULL;
				break;
			} /* End Switch */
			break;
		case NEWLINE:
			kobj_newline(file);
			state = class_begin;
			if (name)
				kmem_free(name, strlen(name) + 1);
			if (exporter)
				kmem_free(exporter, strlen(exporter) + 1);
			if (class)
				kmem_free(class, strlen(class) + 1);
			break;
		default:
			/* Error */
			break;
		}
	}
	kobj_close_file(file);
}

static struct hwc_spec *
class_to_hwc(struct hwc_spec *hwcp)
{
	struct hwc_spec *hs1;
	struct hwc_class *hcl;
	struct hwc_spec *hwctail = hwcp;

	for (hcl = hcl_head; hcl != NULL; hcl = hcl->class_next) {
		if (strcmp(hwcp->hwc_class_name, hcl->class) == 0) {
			if (hwcp->hwc_parent_name != NULL) {
				/*
				 * create new spec
				 */
				hs1 = (struct hwc_spec *)
				    kmem_zalloc(sizeof (struct hwc_spec),
				    KM_NOSLEEP);
				if (hs1 == NULL) {
					goto nohwcmem;
					/*NOTREACHED*/
				}

				hs1->hwc_proto = (dev_info_t *)
				    kmem_zalloc(sizeof (struct dev_info),
				    KM_NOSLEEP);
				if (hs1->hwc_proto == NULL) {
					goto nohwcmem;
					/*NOTREACHED*/
				}

				DEVI(hs1->hwc_proto)->devi_name =
				    kmem_zalloc(
				    strlen(DEVI(hwcp->hwc_proto)->devi_name)
				    + 1, KM_NOSLEEP);
				if (DEVI(hs1->hwc_proto)->devi_name == NULL) {
					goto nohwcmem;
					/*NOTREACHED*/
				}
				strcpy(DEVI(hs1->hwc_proto)->devi_name,
				    DEVI(hwcp->hwc_proto)->devi_name);

				hs1->hwc_parent_name =
				    kmem_zalloc(strlen(hcl->class_exporter) + 1,
				    KM_NOSLEEP);
				if (hs1->hwc_parent_name == NULL) {
					goto nohwcmem;
					/*NOTREACHED*/
				}
				strcpy(hs1->hwc_parent_name,
				    hcl->class_exporter);
				copy_prop(
				    DEVI(hwcp->hwc_proto)->devi_drv_prop_ptr,
				    &(DEVI(hs1->hwc_proto)->devi_drv_prop_ptr));
				copy_prop(
				    DEVI(hwcp->hwc_proto)->devi_sys_prop_ptr,
				    &(DEVI(hs1->hwc_proto)->devi_sys_prop_ptr));
				hwctail->hwc_next = hs1;
				hwctail = hs1;
			} else {
				hwcp->hwc_parent_name =
				    kmem_zalloc(strlen(hcl->class_exporter) + 1,
				    KM_NOSLEEP);
				if (hwcp->hwc_parent_name == NULL) {
					goto nohwcmem;
					/*NOTREACHED*/
				}
				strcpy(hwcp->hwc_parent_name,
				    hcl->class_exporter);
			}
		}
	}
	if (hwcp->hwc_parent_name == NULL)
		return ((struct hwc_spec *)0);
	else
		return (hwcp);

nohwcmem:
	/*
	 * XXX	This is a bit stupid.
	 */
	cmn_err(CE_PANIC, "No memory for hwconf structure");
}

void
open_mach_list(void)
{
	struct _buf *file;
	char tokbuf[MAXNAMELEN];
	register token_t token;
	struct psm_mach *machp;

	if ((file = kobj_open_file(mach_file)) == (struct _buf *)-1)
		return;

	while ((token = lex(file, tokbuf)) != EOF) {
		switch (token) {
		case POUND:
			find_eol(file);
			break;
		case NAME:
		case STRING:
			machp = kmem_alloc((sizeof (struct psm_mach) +
				strlen(tokbuf) + 1), KM_SLEEP);
			machp->m_next = pmach_head;
			machp->m_machname = (char *)(machp + 1);
			strcpy(machp->m_machname, tokbuf);
			pmach_head = machp;
			break;
		case NEWLINE:
			kobj_newline(file);
			break;
		default:
			/* Error */
			break;
		}
	}
	kobj_close_file(file);
}

void *
get_next_mach(void *handle, char *buf)
{
	struct psm_mach *machp;

	machp = (struct psm_mach *)handle;
	if (machp)
		machp = machp->m_next;
	else
		machp = pmach_head;
	if (machp)
		strcpy(buf, machp->m_machname);
	return (machp);
}

void
close_mach_list(void)
{
	struct psm_mach *machp;

	while (pmach_head) {
		machp = pmach_head;
		pmach_head = machp->m_next;
		kmem_free((caddr_t)machp, (sizeof (struct psm_mach) +
			strlen(machp->m_machname) + 1));
	}
}

/*
 *	Read in the 'zone_lag' value from the rtc configuration file,
 *	and return the value to the caller.
 */

long
process_rtc_config_file(void)
{
	struct _buf *file;
	char tokbuf[MAXNAMELEN];
	register token_t token;
	long zone_lag = 0;
	int state = 0;
	u_longlong_t tmp;

	if ((file = kobj_open_file(rtc_config_file)) == (struct _buf *)-1)
		return (0);

	while ((token = lex(file, tokbuf)) != EOF) {
		switch (token) {
		case POUND:
			find_eol(file);
			break;
		case NAME:
		case STRING:
			if (strcmp(tokbuf, "zone_lag") == 0)
				state = 1;	/* look for '=' */
			else {
				find_eol(file);
				state = 0;
			}
			break;
		case EQUALS:
			if (state == 1)
				state = 2;	/* look for zone_lag */
			else {
				find_eol(file);
				state = 0;
			}
			break;
		case DECVAL:
			if (state == 2) {
				getvalue(tokbuf, &tmp);
				zone_lag = (long)tmp;
			} else {
				find_eol(file);
				state = 0;
			}
			break;
		case NEWLINE:
			kobj_newline(file);
			break;
		default:
			/* Error */
			break;
		}
	}
	kobj_close_file(file);
	return (zone_lag);
}
