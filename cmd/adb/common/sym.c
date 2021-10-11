/*
 * Copyright (c) 1986 - 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

/*
 * adb - symbol table routines
 */

#ident	"@(#)sym.c	1.65	95/09/11 SMI"

#include "adb.h"
#include <stdio.h>
#include "fio.h"
#include "symtab.h"
#ifdef __ELF
#include <link.h>
#include <sys/types.h>
#include <sys/auxv.h>
#if	!defined(KADB)
#include <unistd.h>
#endif
#else
#include "link_sunos.h"
#endif
#include <setjmp.h>
#if defined(KADB)
#include <sys/sysmacros.h>
#endif

extern int adb_debug;

#ifdef KADB

#define	KREAD(addr,buf,size) kobj_safe_read(addr, buf, size)
#define KREAD_STR(addr, buf, size) kobj_safe_getstring(addr, buf, size)
#ifdef KRTLD_DEBUG
extern struct module *krtld_module;
#endif /* KRTLD_DEBUG */
#endif

#define INFINITE 0x7fffffff
static int SHLIB_OFFSET;
int use_shlib = 0;
int	address_invalid = 1;
static char *get_all();
static jmp_buf shliberr_jmpbuff;
#ifdef __ELF
Elf32_Dyn dynam;           /* root data structure for dl info */
Elf32_Dyn *dynam_addr;     /* its addr in target process */
struct r_debug *ld_debug_addr; /* the substructure that hold debug info */
struct r_debug ld_debug_struct; /* and its addr*/    
extern char *rtld_path;    /* path name to which kernel hands control */
addr_t exec_entry;         /* addr that receives control after rtld's done*/
addr_t rtld_state_addr;    /*in case of adb support for dlopen/dlclose */
#else
struct	ld_debug ld_debug_struct;
addr_t	ld_debug_addr = 0;
struct	link_dynamic dynam;
addr_t *dynam_addr;
#endif

struct	asym *curfunc, *curcommon;
struct  asym *nextglob;	/* next available global symbol slot */
struct  asym *global_segment; /* current segment of global symbol table */
#define GSEGSIZE 150	/* allocate global symbol buckets 150 per shot */
int	curfile;

struct	asym *enter();
struct	afield *field();
struct	asym * lookup_base();
static unsigned int stubs_base_addr;
static unsigned int stubs_end_addr;
static int elf_stripped = 1; /* by default executables are stripped */

#define	HSIZE	255
struct	asym *symhash[HSIZE];
struct	afield *fieldhash[HSIZE];

int	pclcmp(), lpccmp();
int	symvcmp();

sort_globals(flag)
{
	int i;

	db_printf(5, "sort_globals: flag=%D", flag);
#if 0
	if (nlinepc){
		qsort((char *)linepc, nlinepc, sizeof (struct linepc),lpccmp );
		pcline = (struct linepc*) calloc((nlinepc == 0 ? 1 : nlinepc),
			sizeof(struct linepc));
		if (pcline == 0)
			outofmem();
		for (i = 0; i < nlinepc; i++)
			pcline[i] = linepc[i];
		qsort((char *)pcline, nlinepc, sizeof (struct linepc),pclcmp );
	}
#endif
	if (nglobals == 0)
		return;
	globals = (struct asym **)
	   	    realloc((char *)globals, nglobals * sizeof (struct asym *));
	if (globals == 0)
		outofmem();
	/* arrange the globals in ascending value order, for findsym()*/
	qsort((char *)globals, nglobals, sizeof(struct asym *),symvcmp);
	/* prepare the free space for shared libraries */
	if(flag == AOUT)
		globals = (struct asym **)
		    realloc((char*)globals, NGLOBALS * sizeof(struct asym*));
}

#ifdef	__ELF
stinit(fsym, sh, nsect, flag)
	int fsym;		/* handle for symbol file */
	Elf32_Shdr *sh;		/* ptr to section headers */
	int nsect;		/* number of sections */
	int flag;		/* what kind of executable */
{
	int sym_sect;		/* index of symbol section's header */
	int str_sect;		/* index of string section's header */
	char *strtab;		/* ptr to string table copy */
	Elf32_Sym elfsym[BUFSIZ/sizeof(Elf32_Sym)];
				 /* symbol table entry recepticle */
	Elf32_Sym *es;		/* ptr to ELF symbol */
	int ntogo;		/* number of symbols */
	int ninbuf = 0;		/* number of unconsumed of symbols in buffer */

	/* Look for the symbol section header. */
	for (sym_sect = 0; sym_sect < nsect; sym_sect++)
		if (sh[sym_sect].sh_type == SHT_SYMTAB) {
			elf_stripped = 0;	
			break;
		}
	/*
	 * If executable has been stripped then the symbol table is gone
	 * and in that case, use the symbol table that is reserved for
	 * the runtime loader
	 */
	if (sym_sect == nsect) {
		for (sym_sect = 0; sym_sect < nsect; sym_sect++)
			if (sh[sym_sect].sh_type == SHT_DYNSYM)
				break;
		if (sym_sect == nsect)
			return(-1);	/* No symbol section there */
	}

	/* Check the associated string table. */
	str_sect = sh[sym_sect].sh_link;
	if (str_sect == nsect || sh[str_sect].sh_size == 0) {
#ifdef	KADB
		printf("Warning - empty string table; no symbols.\n");
#else	/* KADB */
		fprintf(stderr, "Warning - empty string table; no symbols.\n");
		fflush(stderr);
#endif	/* KADB */
		return(-1);
	}

	/* Get space for a copy of the string table. */
	strtab = (char *) malloc(sh[str_sect].sh_size +
		sizeof(sh[str_sect].sh_size) + 8);
	if (strtab == 0)
		outofmem();
	*(int *) strtab = sh[str_sect].sh_size + sizeof(sh[str_sect].sh_size);

	/* Read the string table. */
	(void) lseek(fsym, (long) sh[str_sect].sh_offset, 0);
	if (read(fsym, strtab + sizeof(sh[str_sect].sh_size),
		sh[str_sect].sh_size) != sh[str_sect].sh_size)
		goto readerr;

	/* Read and process all symbols */
	(void) lseek(fsym, (long)sh[sym_sect].sh_offset, 0);
	ntogo = sh[sym_sect].sh_size / sh[sym_sect].sh_entsize;

	if (ntogo < 1) {
		db_printf(4, "stinit: no symbols?");
		return -1;
	}
	while (ntogo) {
		if (ninbuf == 0) {	/* more in buffer? */
			int nread = ntogo, cc;
			if (nread > BUFSIZ / sizeof (Elf32_Sym))
				nread = BUFSIZ / sizeof (Elf32_Sym);
			cc = read(fsym, (char *) elfsym,
				nread * sizeof (Elf32_Sym));
			if (cc != nread * sizeof (Elf32_Sym))
				goto readerr;
			ninbuf = nread;
			es = elfsym;
		}
		if (es->st_name)
			es->st_name = (int) strtab + es->st_name +
				sizeof(sh[str_sect].sh_size);
		dosym(es++, flag);
		ninbuf--;
		ntogo--;
	}
	sort_globals(AOUT);

#else	/* ELSE */
stinit(fsym, hp, flag)
	int fsym;
	struct exec *hp;
{
	struct nlist nl[BUFSIZ / sizeof (struct nlist)];
	register struct nlist *np;
	register struct asym *s;
	int ntogo = hp->a_syms / sizeof (struct nlist), ninbuf = 0;
	off_t loc; int ssiz; char *strtab;
	register int i;

	if (hp->a_syms == 0)
		return(-1);
	loc = N_SYMOFF(*hp);

	/* allocate and read string table */
	(void) lseek(fsym, (long)(loc + hp->a_syms), 0);
	if (read(fsym, (char *)&ssiz, sizeof (ssiz)) != sizeof (ssiz)) {
		printf("old format a.out - no string table\n");
		return(-1);
	}
	if (ssiz == 0) {	/* check whether there is a string table */
#ifdef	KADB
		printf("Warning -- empty string table. No symbols.\n");
#else	/* KADB */
		fprintf(stderr, "Warning -- empty string table. No symbols.\n");
		fflush(stderr);
#endif	/* KADB */
		return(-1);
	}
	strtab = (char *)malloc(ssiz + 8);	/* TEMP!  kluge to work around kernel bug */
	if (strtab == 0)
		outofmem();
	*(int *)strtab = ssiz;
	ssiz -= sizeof (ssiz);
	if (read(fsym, strtab + sizeof (ssiz), ssiz) != ssiz)
		goto readerr;
	/* read and process symbols */
	(void) lseek(fsym, (long)loc, 0);
	while (ntogo) {
		if (ninbuf == 0) {
			int nread = ntogo, cc;
			if (nread > BUFSIZ / sizeof (struct nlist))
				nread = BUFSIZ / sizeof (struct nlist);
			cc = read(fsym,(char *)nl,nread*sizeof (struct nlist));
			if (cc != nread * sizeof (struct nlist))
				goto readerr;
			ninbuf = nread;
			np = nl;
		}
		if (np->n_un.n_strx)
			np->n_un.n_name = strtab + np->n_un.n_strx;
		dosym(np++, flag); ninbuf--; ntogo--;
	}
	if (flag == AOUT){
		/* make 0 pctofilex entries? */
		sort_globals(flag);
	}
#endif	/* __ELF */
	return(0);
readerr:
	if (!elf_stripped)
		printf("error reading symbol or string table\n");
#if	defined(KADB)
	exit(1, 1);
#else
	exit(1);
#endif
}

#ifdef	__ELF
static
dosym(es, flag)
	Elf32_Sym *es;
	int flag;
{
	char *cp;		/* scratch ptr */
	struct asym *s;		/* debugger symbol ptr */

	db_printf(5, "dosym: es=%X, es->st_name='%s', es->st_shndx=%D, flag=%D",
		es, (es->st_name == 0L) ? "NULL" : (char *) es->st_name,
							es->st_shndx, flag);
	/* ignore undefine symbols */
	if(es->st_shndx == SHN_UNDEF)
	    return;
	/*
	 * Discard symbols containing a ":" -- they
	 * are dbx symbols, and will confuse us.
	 * Same with the "-lg" symbol and nameless symbols
	 */
	if (cp = (char *) es->st_name) {
		if (strcmp("-lg", cp) == 0) {
			db_printf(7, "dosym: discarded -lg");
			return;
		}
		/*
		 * XXX - Disregard symbols starting with a ".".
		 */
		if (*cp == '.')
			return;
		for (; *cp; cp++)
			if (*cp == ':') {
				db_printf(7, "dosym: discarded %s, it has ':'\n",
					  es->st_name);
				return;		/* must be dbx stab */
			}
	} else {
		db_printf(7, "dosym: discarded, nameless symbol\n");
		return;				/* it's nameless */
	}

	db_printf(7, "dosym: ELF32_ST_TYPE(es->st_info)=%D,\n\t"
		     "ELF32_ST_BIND(es->st_info)=%D",
		  ELF32_ST_TYPE(es->st_info), ELF32_ST_BIND(es->st_info));

	switch (ELF32_ST_TYPE(es->st_info)) {
	case STT_FUNC:
		/*
		 * disgard symbols containing a .
		 * this may be a short-term kludge, to keep
		 * us from printing "routine" names like
		 * "scan.o" in stack traces, rather that
		 * real routine names like "_scanner".
		 */
		 for (cp = (char *) es->st_name; *cp; cp++)
		     if (*cp == '.') return;
		/* fall thru */
	case STT_OBJECT:
	case STT_NOTYPE:
		s = lookup_base(es->st_name);
		if (s) {
#if defined(i386) && !defined(KADB)
			if (ELF32_ST_BIND (es->st_info) == STB_LOCAL) {
				db_printf(4,
					 "dosym: threw away LOCAL symbol %s",
					  s->s_name);
				return;
			}
			if (ELF32_ST_BIND (es->st_info) == STB_WEAK &&
			    (s->s_bind == STB_GLOBAL || s->s_bind == STB_WEAK)){
				db_printf(4,
					 "dosym: threw away new WEAK symbol %s",
					  s->s_name);
				return;
			} 
			if (ELF32_ST_BIND (es->st_info) == STB_GLOBAL &&
			    s->s_bind == STB_GLOBAL) {
				db_printf(4,
					  "dosym: threw away duplicate GLOBAL symbol %s, kept old one",
				       	  es->st_name);
				/*
				 * These most definitely come from
				 * ld.so.1 and libc.so.1
				 */
				return;
			} 
			if (ELF32_ST_BIND (es->st_info) == STB_GLOBAL &&
			    s->s_bind == STB_WEAK) {
				/*
				 * replace the old with the current one
				 */
				db_printf(4, "dosym: replacing WEAK symbol %s with its GLOBAL", s->s_name);
				s->s_f = NULL;
				s->s_fcnt = s->s_falloc = s->s_fileloc = 0;
#	if  __ELF
				s->s_type = ELF32_ST_TYPE(es->st_info);
				s->s_bind = ELF32_ST_BIND(es->st_info);
				s->s_flag = flag;
				if (flag == AOUT)
					s->s_value = es->st_value;
				else {
					/*
					 * XXX - FIXME - XXX
					 * if (s->s_type != (N_EXT | N_UNDF))
					 * flag == SHLIB and not COMMON
					 */
					s->s_value = 
						es->st_value + SHLIB_OFFSET;
				}
				db_printf(5, "dosym: use_shlib=%D, name='%s'",
                                          use_shlib, s->s_name);
				if(use_shlib &&
				   strcmp(s->s_name, "_DYNAMIC") == 0) {
					dynam_addr = (Elf32_Dyn *) (s->s_value);
					db_printf(2,
						  "dosym: new _DYNAMIC at %X",
						  dynam_addr);
				}
				if(use_shlib && strcmp((char*)s->s_name,
					               "_r_debug_state") == 0) {
					rtld_state_addr = (addr_t) (s->s_value);
					db_printf(2, "dosym: new _r_debug_state at %X",
                                                  rtld_state_addr);
				}
#	else   /* !__ELF */
				s->s_type = es->n_type;
				s->s_flag = flag;
				if (flag == AOUT)
					s->s_value = es->n_value;
				else if (s->s_type != (N_EXT | N_UNDF))
					/* flag == SHLIB and not COMMON */
				s->s_value = es->n_value + SHLIB_OFFSET;
				db_printf(2,
					  "dosym: use_shlib=%D, s->s_name='%s'",
                                          use_shlib, s->s_name);
				if(!use_shlib &&
				   strcmp(s->s_name, "__DYNAMIC") == 0) {
					dynam_addr = (addr_t) s->s_value;
					if (dynam_addr)
						use_shlib++;
				}
#	endif  /* !__ELF */
			}
#endif	/* defined(i386) && !defined(KADB) */
			if (flag == AOUT)
				mergedefs(s, es);
		 	else
				re_enter(s, es, flag);
		} else
			s = enter(es, flag);

		db_printf(7, "dosym: ELF32_ST_TYPE(es->st_info) %s STT_FUNC",
		       (ELF32_ST_TYPE(es->st_info) == STT_FUNC) ? "==" : "!=");

		if (ELF32_ST_TYPE(es->st_info) == STT_FUNC) {
			if (curfunc && curfunc->s_f) {
				curfunc->s_f = (struct afield *)
				    realloc((char *)curfunc->s_f,
					curfunc->s_fcnt * 
					  sizeof (struct afield)); 
				if (curfunc->s_f == 0)
					outofmem();
				curfunc->s_falloc = curfunc->s_fcnt;
			}
			curfunc = s;
			db_printf(7, "dosym: curfunc=%X", curfunc);
		}
		break;
	case STT_FILE:
		curfile = fenter(es->st_name);
		db_printf(7, "dosym: curfile=%D", curfile);
		break;
	default:
		break;
	}
}
#else	/* __ELF */
static
dosym(np, flag)
	register struct nlist *np;
{
	register struct asym *s;
	register struct afield *f;
	register char *cp;
	int h;

	/*
	 * Discard symbols containing a ":" -- they
	 * are dbx symbols, and will confuse us.
	 * Same with the "-lg" symbol and nameless symbols
	 */
	if (cp=np->n_un.n_name) {
		if( strcmp( "-lg", cp ) == 0  )
			return;
		for (; *cp; cp++)
			if (*cp == ':')
				return;
	} else {
		return;
	}

	switch (np->n_type) {

	case N_PC:
		/*
		 * disregard N_PC symbols; these are emitted
		 * by pc to detect inconsistent definitions
		 * at link time.
		 */
		return;

	case N_TEXT:
		/*
		 * disgard symbols containing a .
		 * this may be a short-term kludge, to keep
		 * us from printing "routine" names like
		 * "scan.o" in stack traces, rather that
		 * real routine names like "_scanner".
		 */
		 for (cp=np->n_un.n_name; *cp; cp++)
		     if (*cp == '.') return;
		/* fall thru */
	case N_TEXT|N_EXT:
	case N_DATA:
	case N_DATA|N_EXT:
	case N_BSS:
	case N_BSS|N_EXT:
	case N_ABS:
	case N_ABS|N_EXT:
	case N_UNDF:
	case N_UNDF|N_EXT:
	case N_GSYM:
	case N_FNAME:
	case N_FUN:
	case N_STSYM:
	case N_LCSYM:
	case N_ENTRY:
		s = lookup_base(np->n_un.n_name);
		if (s)
			if(flag == AOUT)
				mergedef(s, np);
			else 
				re_enter(s, np, flag);
		else
			s = enter(np, flag);
		if (np->n_type == N_FUN) {
			if (curfunc && curfunc->s_f) {
				curfunc->s_f = (struct afield *)
				    realloc((char *)curfunc->s_f,
					curfunc->s_fcnt * 
					  sizeof (struct afield)); 
				if (curfunc->s_f == 0)
					outofmem();
				curfunc->s_falloc = curfunc->s_fcnt;
			}
			curfunc = s;
		}
		switch (np->n_type) {

		case N_FUN:
		case N_ENTRY:
		case N_GSYM:
		case N_STSYM:
		case N_LCSYM:
			s->s_fileloc = FILEX(curfile, np->n_desc);
			goto sline;
		}
		break;

	case N_RSYM:
	case N_LSYM:
	case N_PSYM:
		s = curfunc;
		if(s) (void) field(np, &s->s_f, &s->s_fcnt, &s->s_falloc);
		break;

	case N_SO:
	case N_SOL:
		curfile = fenter(np->n_un.n_name);
		break;

	case N_SSYM:
		if (s = curcommon) {
			(void) field(np, &s->s_f, &s->s_fcnt, &s->s_falloc);
			break;
		}
		f = globalfield(np->n_un.n_name);
		if (f == 0) {
			h = hashval(np->n_un.n_name);
			/*
			  We must allocate storage here ourselves, as
			  field() will attempt to do a realloc(), and
			  we cannot tolerate that as we are hashing.
			*/
			if (nfields >= NFIELDS) {
				NFIELDS += 50; /* up the ante by 50 each time */
				if ((fields = (struct afield *)calloc(NFIELDS, sizeof(struct afield))) == NULL)
					outofmem();
				nfields = 0;
			}
			f = field(np, &fields, &nfields, &NFIELDS);
			f->f_link = fieldhash[h];
			fieldhash[h] = f;
		}
		break;

	case N_LBRAC:
	case N_RBRAC:
	case N_LENG:
		break;

	case N_BCOMM:
		curcommon = enter(np, flag);		/* name ? */
		break;

	case N_ECOMM:
	case N_ECOML:
		curcommon = 0;
		break;


sline:
	case N_SLINE:
		lenter(FILEX(curfile, (unsigned short)np->n_desc), (int)np->n_value);
		break;
	}
}
#endif	/* __ELF */

#ifdef	__ELF
static
mergedefs(s, es)
	struct asym *s;
	Elf32_Sym *es;
{
	db_printf(6, "mergedefs: ELF32_ST_BIND(es->st_info) %s STB_GLOBAL\n",
		     (ELF32_ST_BIND(es->st_info) == STB_GLOBAL) ? "==" : "!=");
	if (ELF32_ST_BIND(es->st_info) == STB_GLOBAL) {
		s->s_name = (char *) es->st_name;
		s->s_value = es->st_value;
		db_printf(7, "mergedefs: s->s_name=%s, s->s_value=%X\n",
			(s->s_name == NULL) ? "NULL" : s->s_name, s->s_value);
	}
	else {
		
		s->s_type = ELF32_ST_TYPE(es->st_info);
		s->s_bind = ELF32_ST_BIND(es->st_info);
	}

	db_printf(7, "mergedefs: s->s_type=%D, s->s_bind=%D\n",
		  s->s_type, s->s_bind);
}
#else	/* __ELF */
static
mergedef(s, np)
	struct asym *s;
	struct nlist *np;
{

	switch (s->s_type) {

	case N_TEXT:
	case N_TEXT|N_EXT:
	case N_DATA:
	case N_DATA|N_EXT:
	case N_BSS:
	case N_BSS|N_EXT:
		/* merging fake symbol into real symbol -- very unlikely */
		s->s_type = np->n_type;
		break;
	case N_GSYM:
	case N_FUN:
		/* merging real symbol into fake symbol -- more likely */
		s->s_name = np->n_un.n_name; /* want _name, really */
		s->s_value = np->n_value;
		break;
	}
}
#endif	/* __ELF */

static
symvcmp(s1, s2)
	struct asym **s1, **s2;
{
	return ((*s1)->s_value > (*s2)->s_value) ? 1 :
			(((*s1)->s_value == (*s2)->s_value) ? 0 : -1);
}

static
hashval(cp)
	register char *cp;
{
	register int h = 0;

#if defined(KADB)
	while (*cp == '_')
		cp++;
	while (*cp && (*cp != '_' || cp[1])) {
#else /* defined(KADB) */
	while (*cp) {
#endif /* defined(KADB) */
		h *= 2;
		h += *cp++;
	}
	h %= HSIZE;
	if (h < 0)
		h += HSIZE;
	return (h);
}

struct asym *
enter(np, flag)
#ifdef	__ELF
	Elf32_Sym *np;
#else	/* __ELF */
	struct nlist *np;
#endif	/* __ELF */
{
	register struct asym *s;
	register int h;

	db_printf(5, "enter: np=%X, flag=%D, NGLOBALS=%D\n",
							np, flag, NGLOBALS);
	/*
	 * if this is the first global entry,
	 * allocate the global symbol spaces
	 */
	if (NGLOBALS == 0) {
		NGLOBALS = GSEGSIZE;
		globals = (struct asym **)
		    malloc(GSEGSIZE * sizeof (struct asym *));
		if (globals == 0)
			outofmem();
		global_segment = nextglob = (struct asym *) 
		    malloc(GSEGSIZE * sizeof(struct asym));
		if (nextglob == 0)
			outofmem();
	}
	/*
	 * if we're full up, reallocate the pointer array,
	 * and give us a new symbol segment
	 */
	if (nglobals == NGLOBALS) {
		NGLOBALS += GSEGSIZE;
		globals = (struct asym **)
		    realloc((char *)globals, NGLOBALS*sizeof (struct asym *));
		if (globals == 0)
			outofmem();
		global_segment = nextglob = (struct asym *)
		    malloc( GSEGSIZE*sizeof (struct asym));
		if (nextglob == 0)
			outofmem();
	}
	globals[nglobals++] = s = nextglob++;
	s->s_f = 0; s->s_fcnt = 0; s->s_falloc = 0;
	s->s_fileloc = 0;
#ifdef	__ELF
	s->s_name = (char *) np->st_name;
	s->s_type = ELF32_ST_TYPE(np->st_info);
	s->s_bind = ELF32_ST_BIND(np->st_info);
	s->s_flag = flag;
	if (flag == AOUT)
		s->s_value = np->st_value;
	else /* if (s->s_type != (N_EXT | N_UNDF)) XXXX FIXME XXXX*/
             /* flag == SHLIB and not COMMON */
		s->s_value = np->st_value + SHLIB_OFFSET;
	db_printf(5, "enter: use_shlib=%D, new entry='%s'\n", 
						use_shlib, s->s_name);
	if(use_shlib && strcmp((char*)s->s_name, "_DYNAMIC")==0){
		dynam_addr = (Elf32_Dyn*)(s->s_value);
		db_printf(2, "enter: found _DYNAMIC at %X\n", dynam_addr);
	}
	if(use_shlib && strcmp((char*)s->s_name, "_r_debug_state")==0){
		rtld_state_addr = (addr_t)(s->s_value);
		db_printf(2, "enter: found _r_debug_state at %X\n",
							rtld_state_addr);
	}
	h = hashval((char *) np->st_name);
#else	/* __ELF */
	s->s_name = np->n_un.n_name;
	s->s_type = np->n_type;
	s->s_flag = flag;
	if (flag == AOUT)
		s->s_value = np->n_value;
	else if (s->s_type != (N_EXT | N_UNDF))
		/* flag == SHLIB and not COMMON */
		s->s_value = np->n_value + SHLIB_OFFSET;
	db_printf(2, "enter: use_shlib=%D, s->s_name='%s'\n", 
						use_shlib, s->s_name);
	if(!use_shlib && strcmp(s->s_name, "__DYNAMIC")==0){
		dynam_addr = (addr_t)s->s_value;
		if (dynam_addr){
			use_shlib++;
		}
	}
	h = hashval(np->n_un.n_name);
#endif	/* __ELF */
	s->s_link = symhash[h];
	symhash[h] = s;
	db_printf(5, "enter: '%s', value %X hashed at %D\n",
						s->s_name, s->s_value, h);
	return (s);
}

static
char *
get_all(addr, leng)
int *addr;
{
	int *copy, *save;
	char *saveflg = errflg;

	db_printf(5, "get_all: addr=%X, leng=%D\n", addr, leng);
	errflg =0;
	/* allocate 4 more, let get to clobber it */
	save = copy = (int*)malloc(leng+4);
	for(; leng > 0; addr++, copy++){
			*copy = get(addr,DSP);
			leng = leng - 4;
			db_printf(2, "get_all: *copy=%D, leng=%D\n", *copy, leng);
	}
	if(errflg) {
		printf("error while reading shared library: %s\n",errflg);
		db_printf(3, "get_all: '%s'\n", errflg);
		errflg = saveflg;
		db_printf(2, "get_all: jumping to shliberr_jmpbuff\n");
		longjmp(shliberr_jmpbuff,1);
	}
	errflg = saveflg;
	db_printf(5, "get_all: returns %X\n", (char *) save);
	return (char*)save;
}

static char*
get_string(addr)
int *addr;
{
	int *copy;
	char *c, *save = 0;
	int done = 0;
	char *saveflg = errflg;

	if (addr == NULL){
		printf("nil address found while reading shared library\n");
		return 0;
	}
	db_printf(2, "get_string: reading shared library at addr=%X\n", addr);
	/* assume the max path of shlib is 1024 */
	copy = (int*)malloc(1024);
	if (copy == 0)
		outofmem();
	save = (char*)copy;
	errflg =0;
	while (!done){
		*copy = get(addr, DSP);
		c = (char*)copy;
		if(*c == '\0' || *c++ == '\0' || *c++ == '\0' || *c++ == '\0')
			done++;
		copy++;
		addr++;
	}
	if(errflg) {
		printf("error while reading shared library:%s\n", errflg);
		db_printf(3, "get_string: '%s'\n", errflg);
		errflg = saveflg;
		db_printf(2, "get_string: jumping to shliberr_jmpbuff\n");
		longjmp(shliberr_jmpbuff,1);
	}
	errflg = saveflg;
	db_printf(5, "get_string: returns %X\n", save);
	return save;
}

#ifndef	__ELF
/*
 * whenever rerun the program, refresh the sysmbols defined in the
 * shared library to reflect the true address
 */
re_enter(s, np, flag)
	struct asym *s;
	struct nlist *np;
{
	if (flag != AOUT && s->s_value == 0 && s->s_type != (N_EXT | N_UNDF))
		s->s_value = np->n_value + SHLIB_OFFSET;
}

/*
 * read the ld_debug structure
 */
char *
read_ld_debug()
{
	char *rvp = NULL;

	jmp_buf save_jmpbuff;
	memcpy((char *)save_jmpbuff, (char *)shliberr_jmpbuff,
	    sizeof(save_jmpbuff));

	if(setjmp(shliberr_jmpbuff)) {
		goto out;
	}
	if (dynam_addr == 0){
		printf("__DYNAMIC is not defined for shlib\n");
		goto out;
	}
	dynam = *(struct link_dynamic *)get_all(dynam_addr, sizeof(struct link_dynamic));
	ld_debug_addr = (addr_t)dynam.ldd;
	rvp = get_all(ld_debug_addr, sizeof(struct ld_debug));
out:
	memcpy((char *)shliberr_jmpbuff, (char *)save_jmpbuff,
	    sizeof(save_jmpbuff));
	return (rvp);
}

/* extend the ? map by adding new ranges */
extend_text_map()
{
	struct	ld_debug *check;
	db_printf(5, "extend_text_map: called\n");
	check = (struct ld_debug*)read_ld_debug();
	db_printf(7, "extend_text_map: check=%X\n", check);
#ifndef KADB
		read_in_shlib();
#endif
}

#ifndef KADB
/*
 * before refresh the symbol table, zero the values of those symbols
 * defined in the shared lib, to avoid multiply defined symbols in
 * different lib
 */
static
clean_up()
{
	register struct asym **p, *q;
	register int i;

	db_printf(6, "clean_up: called\n");
	for (p = globals, i = nglobals; i > 0; p++, i--){
		if ((q = *p)->s_flag != AOUT)
			q->s_value = 0;
	}
	free_shlib_map_ranges(&txtmap);
	free_shlib_map_ranges(&datmap);
}

/*
 * read in the symbol table information from the shared lib 
 */
read_in_shlib(void)
{
	struct link_map *lp;
	struct link_map entry;
	struct rtc_symb *cp, *p;
	struct nlist *np;
	struct asym *s;
	struct	ld_debug *check;
	void add_map_range(struct map *, const int, const int, const int,
			   char *);

	jmp_buf save_jmpbuff;
	memcpy((char *)save_jmpbuff, (char *)shliberr_jmpbuff,
	    sizeof(save_jmpbuff));

	if(setjmp(shliberr_jmpbuff)) {
		goto out;
	}
	clean_up();
	/* get the mapping between the address and shlib */
	if(check = (struct ld_debug*)read_ld_debug())
		ld_debug_struct = *check;
	lp = ((struct link_dynamic_1*)get_all(dynam.ld_un.ld_1, sizeof(struct link_dynamic_1)))->ld_loaded;
	for ( ; lp; lp = entry.lm_next){
		struct exec exec;
		int file;
		char *lib;

		entry = *(struct link_map*)get_all(lp, sizeof(struct link_map));
		lib = get_string(entry.lm_name);
		if(!lib)
			goto out;
		file = getfile(lib, INFINITE);
		if(read(file, (char*)&exec, sizeof exec) != sizeof exec
		   || N_BADMAG(exec)){
			printf("can't open shared library, %s\n", lib);
			(void) close(file);
			goto out;
		}
		SHLIB_OFFSET = (int)entry.lm_addr;
		stinit(file, &exec, SHLIB);
		if(!pid) {
			add_map_range(&txtmap, 
				SHLIB_OFFSET, SHLIB_OFFSET+exec.a_text, N_TXTOFF(exec),
				lib);
		}
		(void) close(file);
	}
	cp = ld_debug_struct.ldd_cp;
	while (cp){
		char *sp;

		p = (struct rtc_symb*)get_all(cp, sizeof(struct rtc_symb));
		np = (struct nlist*)get_all(p->rtc_sp, sizeof(struct nlist));
		sp = get_string(np->n_un.n_name);
		if(!sp)
			return;
		s = lookup_base(sp);
		if (!s) {
			s = enter(np, SHLIB);
			s->s_value = np->n_value;
		} else if (s->s_type == (N_EXT|N_UNDF) || s->s_type == N_COMM){
			s->s_type = np->n_type;
			s->s_value = np->n_value;
		} else {
			break;
		}
		cp = p->rtc_next;
	}
	sort_globals(SHLIB);
	address_invalid = 0;
out:
	memcpy((char *)shliberr_jmpbuff, (char *)save_jmpbuff,
	    sizeof(save_jmpbuff));
}
#endif !KADB
#else /* __ELF variant */

re_enter(s, es, flag)
	struct asym *s;
	Elf32_Sym *es;
        int flag;
{
	db_printf(4, "re_enter: s->s_value=%X, flag=%D\n", s->s_value, flag);

#if defined(KADB)
	if (flag != AOUT && s->s_value == 0)
#else
	if (flag == SHLIB)
#endif
		s->s_value = es->st_value + SHLIB_OFFSET;

	db_printf(4, "re_enter: s->s_value=%X\n", s->s_value);
}

#ifndef	KADB
/* Navigate the stack layout that accompanies an exec() of an ELF with
 * PT_INTERP set (see eg rt_boot.s in the rtld code). Find AT_BASE and
 * return it.
 * On sparc:
 *     sp->(window save, argc, argv,0,envp,0,auxv,0,info_block)
 * On i386:
 *     sp->(argc, argv,0,envp,0,auxv,0,info_block)
 */
find_rtldbase(void)
{
#if old_stuff
    int sp = readreg(Reg_SP);
    int *env_ptr, env;
#if defined(i386)
    int *argc_ptr = (int *) get_all(sp, sizeof(int));
    addr_t env_addr = (addr_t)(sp + sizeof(*argc_ptr) + ((*argc_ptr) * sizeof(int)));
#elif defined(sparc)
    int *argc_ptr = (int *) get_all(sp + 64, sizeof(int));
    addr_t env_addr = sp + 68 + ((*argc_ptr) * sizeof(int));
#endif
    auxv_t *auxv;
    addr_t auxv_addr;
    int base = 0;

    db_printf(4, "find_rtldbase: called\n");
    free(argc_ptr);
    do {
	    /* advance over null word between argv and env */
	env_addr  = (addr_t) ((char*)env_addr + sizeof(int));
	env_ptr = (int *) get_all(env_addr, sizeof(int));
	env = *env_ptr;
	free(env_ptr);
    } while(env);

    auxv_addr = (addr_t) ((char *) env_addr + sizeof(int));
    do {
	auxv = (auxv_t *) get_all(auxv_addr, sizeof(auxv_t));
        db_printf(2, "find_rtldbase: auxv->a_type=%D\n", auxv->a_type);
	switch(auxv->a_type) {
	    case AT_EXECFD:
	    case AT_PHDR:
	    case AT_PHENT:
	    case AT_PHNUM:
	    case AT_PAGESZ:
	    case AT_FLAGS:
		break;

	    case AT_BASE:
		base = auxv->a_un.a_val;
		break;

	    case AT_ENTRY:
		exec_entry = (addr_t) auxv->a_un.a_val;
		break;
	}
	free(auxv);
	auxv_addr = (addr_t)((char*)auxv_addr + sizeof(auxv_t));
    } while(auxv->a_type != AT_NULL);
#else /* !old_stuff */
    int base = 0;
    auxv_t *auxv;
    extern auxv_t *FetchAuxv();

    for (auxv = FetchAuxv(); auxv && auxv->a_type != AT_NULL; auxv++) {
	switch(auxv->a_type) {
	    case AT_BASE:
		base = auxv->a_un.a_val;
		break;

	    case AT_ENTRY:
		exec_entry = (addr_t) auxv->a_un.a_val;
		break;
	}
    }
#endif /* !old_stuff */
    if (base) {
        db_printf(2, "find_rtldbase: base of the rtld=%X\n", base);
	return base;
    } else {
	(void) printf("error rtld auxiliary vector: AT_BASE not defined\n");
	exit(1);
    }
}

read_in_shlib(fname, base)
    char *fname;
    int base;
{
	off_t loc;
	int file;
	Elf32_Ehdr    hdr;
	Elf32_Shdr    *secthdr;
	Elf32_Phdr    *proghdr;
	void add_map_range(struct map *, const int, const int, const int,
			   char *);

	db_printf(4, "read_in_shlib: fname='%s', base=%X\n",
				(fname == NULL) ? "NULL" : fname, base);
	file = getfile(fname, INFINITE);
	if (file == -1) {
		(void) printf("Unable to open shared library %s\n", fname);
		return;
	}
	    
	if (read(file, (char *) &hdr, sizeof hdr) != sizeof hdr ||
	    hdr.e_ident[EI_MAG0] != ELFMAG0 ||
	    hdr.e_ident[EI_MAG1] != ELFMAG1 ||
	    hdr.e_ident[EI_MAG2] != ELFMAG2 ||
	    hdr.e_ident[EI_MAG3] != ELFMAG3 ||
	    hdr.e_type != ET_DYN || hdr.e_version != EV_CURRENT) {
		(void) printf("invalid ELF header for %s\n", fname);
		(void) close(file);
		return;
	}
	db_printf(2, "read_in_shlib: read the ELF header for %s\n",
					(fname == NULL) ? "NULL" : fname);

	if (hdr.e_phnum == 0) {
		(void) printf("No rtld program header for %s\n", fname);
		(void) close(file);
		return;
	}
	/* Get space for a copy of the program header. */
	proghdr = (Elf32_Phdr *) malloc(hdr.e_phentsize * hdr.e_phnum);
	if (proghdr == NULL) {
		printf("Unable to allocate program header for %s\n", fname);
		(void) close(file);
		outofmem();
	}
	/* Seek to program header table and read it. */
	if ((loc = lseek(file, hdr.e_phoff, SEEK_SET) != hdr.e_phoff) ||
	    (read(file, (char *)proghdr,
		  hdr.e_phentsize * hdr.e_phnum) !=
	     hdr.e_phentsize * hdr.e_phnum)) {
		(void) printf("Unable to read program header for %s\n", fname);
		(void) close(file);
		return;
	}
	db_printf(2, "read_in_shlib: read the program header for %s\n",
					(fname == NULL) ? "NULL" : fname);
	/* Get space for the section header. */
	secthdr = (Elf32_Shdr *) malloc (hdr.e_shentsize * hdr.e_shnum);
	if (secthdr == NULL) {
		(void) printf("Unable to allocate section header for %s\n",
			      fname);
		(void) close(file);
		outofmem();
	}
	/* Seek to section header and read it. */
	if ((loc = lseek(file, hdr.e_shoff, SEEK_SET) == -1) ||
	    (read(file, (char *)secthdr, hdr.e_shentsize * hdr.e_shnum) !=
					    hdr.e_shentsize * hdr.e_shnum)) {
		(void) printf("Unable to read section header for %s\n", fname);
		(void) close(file);
		return;
	}
	db_printf(2, "read_in_shlib: read the section header for %s\n",
					(fname == NULL) ? "NULL" : fname);
	SHLIB_OFFSET = base;
	db_printf(2, "read_in_shlib: SHLIB_OFFSET=%X\n", SHLIB_OFFSET);

	if (!stinit(file, secthdr, hdr.e_shnum, SHLIB)) {
		register int i;
		char *strtab = NULL;
		off_t loc;

		/*
		 * Find the string table section of the shared object.
		 */
		for (i = 0; i < (int) hdr.e_shnum; i++)
                        if (secthdr[i].sh_type == SHT_STRTAB) {
                                if ((strtab =
				     malloc(secthdr[i].sh_size)) == NULL) {
                                        (void) printf("Unable to allocate"
						      " section name table.\n");
					(void) close(file);
                                        outofmem();
                                }
                                if ((loc = lseek(file,
						 secthdr[i].sh_offset,
						 0) == -1) ||
                                    (read(file, strtab, secthdr[i].
                                          sh_size) != secthdr[i].sh_size)) {
                                        (void) printf("Unable to read section"
						      " names.\n");
					(void) close(file);
					return;
                                }
                                loc = (off_t) ((int) strtab +
					       (int) secthdr[i].sh_name);
                                if (!strcmp((char *) loc, ".shstrtab"))
                                        break;          /* found it */
                        }
		if (i == (int) hdr.e_shnum) {
			(void) close(file);
			return;
		}

		/*
		 * Add the text and data sections of the shared objects
		 * to txtmap and datmap lists respectively.
		 */
		for (i = 0; i < (int) hdr.e_shnum; i++)
			if (secthdr[i].sh_type == SHT_PROGBITS &&
			    secthdr[i].sh_flags & SHF_EXECINSTR) {
				register struct map *map;

                                loc = (off_t) ((int) strtab +
                                        (int) secthdr[i].sh_name);
                                if (!strcmp((char *) loc, ".text"))
					map = &txtmap;
				else if (!strcmp((char *) loc, ".data"))
					map = &datmap;
				else 
					continue;
				add_map_range(map, 
					      SHLIB_OFFSET + secthdr[i].sh_addr,
					      SHLIB_OFFSET + secthdr[i].sh_addr
					    		   + secthdr[i].sh_size,
					      secthdr[i].sh_offset,
					      fname);
			}

	}
	(void) close(file);
	return;
}

read_in_rtld(void)
{
	struct bkpt *bkptr;
	extern struct bkpt *get_bkpt();

	db_printf(4, "read_in_rtld: called\n");
	read_in_shlib(rtld_path, find_rtldbase());
	/*
	 * set a bkpt at the executable's entry point, aka, start 
	 */
	db_printf(2, "read_in_rtld: exec_entry=%X\n", exec_entry);
	bkptr = get_bkpt(exec_entry);
	bkptr->flag = BKPTSET;
	bkptr->count = bkptr->initcnt = 1;
	bkptr->comm[0] = '\n';
	bkptr->comm[1] = '\0';
	db_printf(4, "read_in_rtld: bkptr=%X\n", bkptr);
}

/*
 * read the ld_debug structure
 */
char *
read_ld_debug(void)
{
	char *rvp = NULL;
        Elf32_Dyn *tmp1, *tmp2; /* pointers in debugger and target space*/

	jmp_buf save_jmpbuff;
	memcpy((char *)save_jmpbuff, (char *)shliberr_jmpbuff,
	    sizeof(save_jmpbuff));

	if(setjmp(shliberr_jmpbuff)) {
		goto out;
	}
	if (dynam_addr == 0){
		printf("_DYNAMIC is not defined \n");
		goto out;
	}
	tmp2 = dynam_addr;
	for(;;) {
	    tmp1 = (Elf32_Dyn *)get_all(tmp2, sizeof(Elf32_Dyn));
	    if (tmp1->d_tag == DT_NULL) {
		goto out;
	    }
	    if(tmp1->d_tag == DT_DEBUG) {
		break;
	    }
	    free(tmp1);
	    tmp2++;
	}
	ld_debug_addr = (struct r_debug*) tmp1->d_un.d_ptr;
	rvp = get_all(ld_debug_addr, sizeof(struct r_debug));
out:
	memcpy((char *)shliberr_jmpbuff, (char *)save_jmpbuff,
	    sizeof(save_jmpbuff));
	return (rvp);
}

scan_linkmap(void)
{
    struct r_debug *dptr;
    addr_t lmp_addr;
    struct link_map *lmp;
    char *name;

    db_printf(5, "scan_linkmap: called\n");
    dptr = (struct r_debug *) read_ld_debug();
    db_printf(2, "scan_linkmap: dptr=%X\n", dptr);
    if(dptr == NULL)
	return;
    for(lmp_addr = (addr_t) dptr->r_map; lmp_addr;
					 lmp_addr = (addr_t) lmp->l_next) {
	lmp = (struct link_map *) get_all(lmp_addr, sizeof(struct link_map));
        if(lmp->l_name) {
	    name = get_string(lmp->l_name);
	    db_printf(2, "scan_linkmap: name='%s'\n",
					(name == NULL) ? "NULL" : name);
	    if(strcmp(name, rtld_path) && strcmp(name, symfil)) {
		/* now we must make shure that the symfile was not entered
		 * on the user line relative to the current directory.
		 * If so the above test will alway pass since the core file
		 * will always have the full path.
		 */
		char *no_sl_nam = (char *)strrchr(name, '/');
		char *no_sl_sym = (char *)strrchr(symfil, '/');
		if ( no_sl_sym++ == 0)
			no_sl_sym = symfil;
		if ( no_sl_nam++ == 0)
			no_sl_nam = name;
		if (strcmp( no_sl_nam, no_sl_sym) != 0)
		read_in_shlib(name, lmp->l_addr);
	    }
        }
        if(lmp->l_name)
            db_printf(5, "scan_linkmap: %s at %X\n", 
				(name == NULL) ? "NULL" : name, lmp->l_addr);
	else
            db_printf(5, "scan_linkmap: (noname) at %X\n", lmp->l_addr);
    }
}


#endif	/* !KADB */
#endif	/* !__ELF */

struct asym *
lookup_base(cp)
	char *cp;
{
	register struct asym *s;
	register int h;

	char *kcp;

	db_printf(4, "lookup_base: cp='%s'", (cp == NULL) ? "NULL" : cp);
	h = hashval(cp);
	errflg = 0;
	for (s = symhash[h]; s; s = s->s_link)
		if (!strcmp(s->s_name, cp)) {
			db_printf(4, "lookup_base: returns %X", s);
			cursym = s;
			return (s);
		}
	cursym = 0;
	db_printf(4, "lookup_base: returns 0");
	return (0);
}

/*
 * Note that for adb, lookup() and lookup_base() are identical.  They very
 * under kadb in the order of symbol lookup and the building of symbol
 * table entries.
 */
struct asym *
lookup(cp)
	char *cp;
{
	register struct asym *s;
#ifdef KADB
	int any_mod;
	int i;
	int val;
	static char namebuf[256];
	static struct asym asym;
	char *p;
#endif	/* KADB */

	db_printf(4, "lookup: cp='%s'", (cp == NULL) ? "NULL" : cp);

#ifdef KADB
	/* if _curmod == 0, then precedence is:
	 *
	 * 	base symbols
	 *	module symbol in reverse-load order
	 *
	 *
	 * if _curmod == -1, then precedence is:
	 *	base symbols
	 *
	 * otherwise, _curmod contains the id of the module
	 * to search first.
	 *
	 *	symbols from selected module
	 *	base symbols
	 *	other module symbols in reverse-load order
	 */

	any_mod = 1;
	if ((s = lookup_base(cp)) != 0) {
		if (stubs_base_addr && stubs_end_addr &&
		   (s->s_value < stubs_base_addr ||
		    s->s_value >= stubs_end_addr))
			any_mod = 0;
		db_printf(2, "lookup: any_mod set to 0");
	}

	if ((val = dbg_kobj_getsymvalue(cp, any_mod)) != 0) {
		p = (char *)&asym;
		for (i = 0; i < sizeof (asym); i++)
			*p++ = 0;
		strcpy (namebuf, cp);
		asym.s_name = namebuf;
		asym.s_type = N_GSYM;
		asym.s_flag = AOUT;
		asym.s_value = val;
		s = &asym;
	}

#else	/* KADB */

	s = lookup_base(cp);

#endif

	errflg = 0;
	cursym = s;
	db_printf(4, "lookup: returns %X", cursym);
	return (cursym);
}


/*
 * The type given to findsym should be an enum { NSYM, ISYM, or DSYM },
 * indicating which instruction symbol space we're looking in (no space,
 * instruction space, or data space).  On VAXen, 68k's and SPARCs,
 * ISYM==DSYM, so that distinction is vestigial (from the PDP-11).
 */
findsym(val, type)
	int val, type;
{
	register struct asym *s;
	register int i, j, k;
	u_int offset;
	char *p;
	char *dbg_kobj_getsymname ();
	static char namebuf[256];
	static struct asym asym;

	db_printf(6, "findsym: val=%X, type=%D", val, type);
	cursym = 0;
	if (type == NSYM)
		return (MAXINT);

	for (i = 0, p = (char *)&asym; i < sizeof asym; i++)
		*p++ = 0;

#ifdef KADB
	if ((p = dbg_kobj_getsymname(val, &offset)) != NULL) {
		db_printf(6, "findsym: offset=%X", offset);
		strcpy (namebuf, p);
		asym.s_name = namebuf;
		asym.s_type = N_GSYM;
		asym.s_flag = AOUT;
		asym.s_value = val - offset;
		s = &asym;
		goto found;
	}
#endif
	i = 0; j = nglobals - 1;
	while (i <= j) {
		k = (i + j) / 2;
		s = globals[k];
		if (s->s_value == val)
			goto found;
		if (s->s_value > val)
			j = k - 1;
		else
			i = k + 1;
	}
	if (j < 0)
		return (MAXINT);
	s = globals[j];
found:
	/*
	 * If addr is zero, fail.  Otherwise, *any*
	 * value will come out as [symbol + offset]
	 */
	if (s->s_value == 0)
		return (MAXINT);
	errflg = 0;
	db_printf(4, "findsym: s_name='%s', s_value=%X, s_bind=%d, returns %X", 
		  (s->s_name == NULL) ? "NULL" : s->s_name,
		  s->s_value, s->s_bind, val - s->s_value);
	cursym = s;
	return (val - s->s_value );
}

/*
 * Given a value v, of type type, find out whether it's "close" enough
 * to any symbol; if so, print the symbol and offset.  The third
 * argument is a format element to follow the symbol, e.g., ":%16t".
 * SEE ALSO:  ssymoff, below.
 */
psymoff(v, type, s)
	int v, type;
	char *s;
{
	int w;

	if (v) 
		w = findsym(v, type);

	if (v == 0 || w >= maxoff) {
		printf("%Z", v);
	} else {
		printf("%s", cursym->s_name);
		if (w)
			printf("+%Z", w);
	}
	printf(s);
}


/*
 * ssymoff is like psymoff, but uses sprintf instead of printf.
 * It's copied & slightly modified from psymoff.
 *
 * ssymoff returns the offset, so the caller can decide whether
 * or not to print anything.
 *
 * NOTE:  Because adb's own printf doesn't provide sprintf, we
 * must use the system's sprintf, which lacks adb's special "%Z"
 * and "%t" format effectors.
 */
ssymoff(v, type, buf)
	int v, type;
	char *buf;
{
	int w, len_sofar;

	db_printf(6, "ssymoff: v=%X, type=%D", v, type);
	if (v) 
		w = findsym(v, type);

	len_sofar = 0;
	if (v != 0 && w < maxoff) {
		sprintf( buf, "%s", cursym->s_name);
		if( w == 0 )
			return w;
		len_sofar = strlen( buf );
		strcpy( &buf[ len_sofar ], " + " );
		len_sofar += 3;
		v = w;
	}
	if( v < 10 )	sprintf( &buf[ len_sofar ], "%x", v );
	else		sprintf( &buf[ len_sofar ], "0x%x", v );
	db_printf(6, "ssymoff: buf='%s', returns %X", buf, w);
	return w;
}


struct afield *
field(np, fpp, fnp, fap)
#ifdef	__ELF
	Elf32_Sym *np;
#else	/* __ELF */
	struct nlist *np;
#endif	/* __ELF */
	struct afield **fpp;
	int *fnp, *fap;
{
	register struct afield *f;

	if (*fap == 0) {
		*fpp = (struct afield *)
		    calloc(10, sizeof (struct afield)); 
		if (*fpp == 0)
			outofmem();
		*fap = 10;
	}
	if (*fnp == *fap) {
		*fap *= 2;
		*fpp = (struct afield *)
		    realloc((char *)*fpp, *fap * sizeof (struct afield));
		if (*fpp == 0)
			outofmem();
	}
	f = *fpp + *fnp; (*fnp)++;
#ifdef	__ELF
	f->f_name = (char *) np->st_name;
	f->f_type = ELF32_ST_TYPE(np->st_info);
	f->f_offset = np->st_value;
#else	/* __ELF */
	f->f_name = np->n_un.n_name;
	f->f_type = np->n_type;
	f->f_offset = np->n_value;
#endif	/* __ELF */
	return (f);
}

#if 0
struct afield *
fieldlookup(cp, f, nftab)
	char *cp;
	register struct afield *f;
	int nftab;
{

	while (nftab > 0) {
		if (eqsym(f->f_name, cp, '_'))
			return (f);
		f++; nftab--;
	}
	return (0);
}

struct afield *
globalfield(cp)
	char *cp;
{
	register int h;
	register struct afield *f;

	h = hashval(cp);
	for (f = fieldhash[h]; f; f = f->f_link)
		if (!strcmp(cp, f->f_name))
			return (f);
	return (0);
}

static
lenter(afilex, apc)
	unsigned afilex, apc;
{

	if (NLINEPC == 0) {
		NLINEPC = 1000;
		linepc = (struct linepc *)
		    malloc(NLINEPC * sizeof (struct linepc));
		linepclast = linepc;
	}
	if (nlinepc == NLINEPC) {
		NLINEPC += 1000;
		linepc = (struct linepc *)
		    realloc((char *)linepc, NLINEPC * sizeof (struct linepc));
		linepclast = linepc+nlinepc;
	}
	if( XLINE(afilex) == 0xffff ) afilex = 0; /* magic */
	linepclast->l_fileloc = afilex;
	linepclast->l_pc = apc;
	linepclast++; nlinepc++;
}
#endif

/* 
 * implement the $p directive by munging throught the global
 * symbol table and printing the names of any N_FUNs we find
 */
printfuns()
{
	register struct asym **p, *q;
	register int i;

	for (p = globals, i = nglobals; i > 0; p++, i--) {
		if ((q = *p)->s_type == N_FUN)
			printf("\t%s\n", q->s_name);
	}
}

#if 0
static
pclcmp(l1, l2)
	struct linepc *l1, *l2;
{

	return (l1->l_pc - l2->l_pc);
}

static
lpccmp(l1, l2)
	struct linepc *l1, *l2;
{

	return (l1->l_fileloc - l2->l_fileloc);
}

pctofilex(apc)
	int apc;
{
	register int i, j, k;
	register struct linepc *lp;

	i = 0; j = nlinepc - 1;
	while (i <= j) {
		k = (i + j) / 2;
		lp = pcline + k;
		if (lp->l_pc == apc) {
			j = k;
			goto found;
		}
		if (lp->l_pc > apc)
			j = k - 1;
		else
			i = k + 1;
	}
	if (j < 0) {
		filex = 0;
		return (-1);
	}
found:
	lp = pcline + j;
	filex = lp->l_fileloc;
	if (filex == 0 )
		return (-1);		/* how do we get these? */
					/* from libg.a...       */
	return (apc - lp->l_pc);
}

filextopc(afilex)
	int afilex;
{
	register int i, j, k;
	register struct linepc *lp;

	i = 0; j = nlinepc - 1;
	while (i <= j) {
		k = (i + j) / 2;
		lp = linepc + k;
		if (lp->l_fileloc == afilex)
			return (lp->l_pc);
		if (lp->l_fileloc > afilex)
			j = k - 1;
		else
			i = k + 1;
	}
	if (j < 0 || i >= nlinepc)
		return (0);
	lp = linepc + i;
	if (XFILE(lp->l_fileloc) != XFILE(afilex))
		return (0);
	return (lp->l_pc);
}
#endif

outofmem()
{

	printf("ran out of memory for symbol table.\n");
#if	defined(KADB)
	exit(1, 1);
#else
	exit(1);
#endif
}

#ifdef KADB

/* pace's stuff below here ... */

/* this file can be compiled into kadb, adb, etc
 *
 * The entry points are:
 *
 * unsigned int dbg_kobj_getsymvalue (char *name);
 *  Returns the value of the given symbol, or 0.  It does not look
 *  in the base symbol table, only the tables for the modules.
 *  If the symbol appears in multiple modules, the most recently loaded
 *  takes precedance.
 *
 * char *dbg_kobj_getsymname(unsigned int value, unsigned int *offset)
 *  Converts value to a symbol name, or NULL.  The difference between
 *  value and the real value of the symbol is stored in *offset.  The
 *  returned value is a static buffer which is overwritten with each call.
 *
 *
 * this file assumes the existance of the two functions:
 *
 *  safe_read (remote_adr, local_adr, size)
 *    Read size bytes from remote_adr in the core file, or in the
 *    active memory.  Put the data in local_adr, which will be
 *    a normal variable in this program.  Returns 0 on success,
 *    -1 for non-existant memory or other error.
 *
 * safe_getstring (remote_adr, local_adr, size)
 *   Read a string from remote_adr in the core file, or active memory.
 *   Put the result in local_adr, which is a buffer that is size big.
 *   This function may be the same thing as safe_read, or it
 *   may stop as soon as it has copied a null byte. Returns 0 on
 *   success, -1 for non-existant memory or other error.
 *
 */

#ifdef sparc
#define ELF_TARGET_SPARC
#endif
#include <sys/elf.h>
#include <sys/kobj.h>

int strcmp ();

static int kobj_safe_read ();
static int kobj_safe_getstring ();

static unsigned int
kobj_hash_name (p)
char *p;
{
	register unsigned long g;
	unsigned int hval;
	
	hval = 0;
	while (*p) {
		hval = (hval << 4) + *p++;
		if ((g = (hval & 0xf0000000)) != 0)
			hval ^= g >> 24;
		hval &= ~g;
	}
	return (hval);
}

static int kobj_initted;
static unsigned int kobj_symbol_info_adr;
static unsigned int _curmod_adr;

static void
init_kobj ()
{
	unsigned int val;
	struct asym *asym;

	db_printf(4, "init_kobj: called");
	kobj_initted = 1;

	/* the variable kobj_symbol_info should be in the base symbol
	 * table, and it is a pointer to a list of kobj_symbol_info
	 * structures for all the currently loaded modules
	 */
	if ((asym = lookup_base("kobj_symbol_info")) == NULL)
		return;
	db_printf(2, "init_kobj: asym=%X\t'kobj_symbol_info'", asym);
	kobj_symbol_info_adr = asym->s_value;
	if ((asym = lookup_base("_curmod")) == NULL)
		return;
	db_printf(2, "init_kobj: asym=%X\t'_curmod'", asym);
	_curmod_adr = asym->s_value;
	asym = lookup_base("stubs_base");
	stubs_base_addr = asym->s_value;
	asym = lookup_base("stubs_end");
	stubs_end_addr = asym->s_value;

#ifdef KRTLD_DEBUG
{
	register struct module *mp = krtld_module;
	Elf32_Shdr *shp = mp->symhdr;
	register int i;
	register Elf32_Sym *sp;
	char *symname;
	u_int *ip;
	
	for (i = 1; i < mp->nsyms; i++) {

		sp = (Elf32_Sym *)(mp->symtbl + (i * mp->symhdr->sh_entsize));
		if (sp->st_shndx < SHN_LORESERVE) {
			shp = (Elf32_Shdr *)
			    (mp->shdrs + sp->st_shndx * mp->hdr.e_shentsize);
			sp->st_value += shp->sh_addr;
		}
		if (sp->st_name == 0 || sp->st_shndx == SHN_UNDEF)
			continue;

		symname = mp->strings + sp->st_name;
		/*
		 * Add to hash table.
		 */
		for (ip = &mp->buckets[kobj_hash_name(symname) % mp->hashsize];
		    *ip; ip = &mp->chains[*ip]) 
			;
		*ip = i;
	}
}
#endif /* KRTLD_DEBUG */

	return;
}

#ifdef KRTLD_DEBUG
/*
 * Do local lookups of the linker module.
 */
int
dbg_kobj_lookup(name)
char *name;
{
	register struct module *mp = krtld_module;
	register Elf32_Sym *sp;
	register u_int *ip;
	u_int hval;
	char *name1;

	hval = kobj_hash_name(name);

	for (ip = &mp->buckets[hval % mp->hashsize];
	    *ip; ip = &mp->chains[*ip]) {
		sp = (Elf32_Sym *)
		    (mp->symtbl + mp->symhdr->sh_entsize * *ip);
		name1 = mp->strings + sp->st_name;
		if (strcmp(name, name1) == 0 &&
		    ELF32_ST_TYPE(sp->st_info) != STT_FILE &&
		    sp->st_shndx != SHN_UNDEF &&
		    sp->st_shndx != SHN_COMMON)
			return (sp->st_value);

	}
	return (NULL);
}

char *
dbg_kobj_getname(value, offset)
	u_int value;
	u_int *offset;
{
	struct module *mp = krtld_module;
	u_int curval = NULL;
	Elf32_Sym *cursym = NULL;
	Elf32_Sym *sp;
	register int i;

	*offset = (u_int)-1;

	/*
	 * Check if it's in range.
	 */
	if ((value >= (u_int)mp->text &&
	    value < (u_int)mp->text + mp->text_size) ||
	    (value >= (u_int)mp->data &&
	    value < (u_int)mp->data + mp->data_size)) {

		for (i = 1; i < mp->nsyms; i++) {
			sp = (Elf32_Sym *)(mp->symtbl +
			    (i * mp->symhdr->sh_entsize));

			if ((ELF32_ST_TYPE(sp->st_info) != STT_OBJECT &&
			    ELF32_ST_TYPE(sp->st_info) != STT_FUNC) ||
			    sp->st_shndx == SHN_COMMON)
				continue;

			curval = sp->st_value;
			if (curval > value)
				continue;
			if (value - curval < *offset) {
				*offset = value - curval;
				cursym = sp;
			}
		}
	}
	return (cursym ? mp->strings + cursym->st_name : NULL);
}
#endif /* KRTLD_DEBUG */

/* look for name in all modules */
int
dbg_kobj_getsymvalue(name, any_mod)
char *name;
int any_mod;
{
	unsigned int hval;
	static char name1[100];
	struct kobj_symbol_info syminfo;
	int idx;
	Elf32_Sym sym;
	int si;
	int retval;
	int desired_mod;


	if (kobj_initted == 0)
		init_kobj();
	
	db_printf(2, "dbg_kobj_getsymvalue: kobj_symbol_info_adr=%X", 
							kobj_symbol_info_adr);
	if (kobj_symbol_info_adr == 0)
		return (0);

	if (_curmod_adr) {
		desired_mod = get(_curmod_adr, DSP);
		if (errflg)
			desired_mod = 0;
	} else
		desired_mod = 0;

	db_printf(2, "dbg_kobj_getsymvalue: desired_mod=%D", desired_mod);
	if (desired_mod == -1 || (desired_mod == 0 && any_mod == 0))
		return (0);

	if (KREAD (kobj_symbol_info_adr, &si, sizeof si) < 0)
		return (0);
	/* skip kernel table */
	db_printf(2, "dbg_kobj_getsymvalue: skip kernel table");


#ifdef KRTLD_DEBUG
	/*
	 * kobj_symbol_info is not yet set, so we use
	 * a private copy of the linker symbol table.
	 */
	if (si == NULL)
		return (dbg_kobj_lookup(name));
#endif /* KRTLD_DEBUG */

	if (KREAD (si, &syminfo, sizeof syminfo) < 0)
		return (0);
	si = (int)syminfo.next;

	retval = 0;
	while (si != 0) {
		if (KREAD (si, &syminfo, sizeof syminfo) < 0)
			return (0);
		hval = kobj_hash_name (name) % syminfo.hash_size;
		if (KREAD ((int)syminfo.buckets
				    + hval * 4, &idx, 4) < 0)
			return (0);
		while (idx != 0) {
			if (KREAD ((int)syminfo.symtbl
					    + syminfo.symsize * idx,
					    &sym, sizeof sym) < 0)
				return (0);
			if (KREAD_STR (syminfo.strings + sym.st_name,
					    name1,
					    sizeof name1) < 0)
				return (0);
			name1[sizeof name1 - 1] = 0;
			if (strcmp (name, name1) == 0 &&
			    ELF32_ST_TYPE(sym.st_info) != STT_FILE) {
				if (desired_mod && desired_mod == syminfo.id)
					return (sym.st_value);
				if (sym.st_shndx == SHN_ABS) {
					retval = sym.st_value;
					break;
				}
				if (desired_mod == 0 && any_mod)
					return (sym.st_value);
				if (retval == 0 && any_mod) {
					retval = sym.st_value;
					break;
				}
			}
			if (KREAD ((int)syminfo.chains + idx * 4, &idx, 4) < 0)
				return (0);
		}
		si = (int)syminfo.next;
	}
	db_printf(2, "dbg_kobj_getsymvalue: returns %D", retval);
	return (retval);
}

/* look for a symbol near value. */
char *
dbg_kobj_getsymname_old(value, offset)
	unsigned int value;
	unsigned int *offset;
{
	static char name[100];
	Elf32_Sym *sym;
	int si;
	struct kobj_symbol_info syminfo;
	int symadr;
	int syms_to_go;
	unsigned int bestval;
	/* this is static just to keep it off the stack */
	static char symbuf[1024];
	int syms_this_time;
	int i;
	int beststr;
	static Elf32_Sym symsave;
	
	db_printf(5, "dbg_kobj_getsymname_old: value=%D, *offset=%D",
							value, *offset);
	if (kobj_initted == 0)
		init_kobj ();
	
	db_printf(2, "dbg_kobj_getsymname_old: kobj_symbol_info_adr=%X", 
							kobj_symbol_info_adr);
	if (kobj_symbol_info_adr == 0)
		return (0);

	/* first, find the module that covers this address */
	if (KREAD (kobj_symbol_info_adr, &si, sizeof si) < 0)
		return (0);
	while (si != 0) {
		if (KREAD (si, &syminfo, sizeof syminfo) < 0)
			return (0);
		
		if ((syminfo.base1 <= value
		     && value < syminfo.base1 + syminfo.len1)
		    || (syminfo.base2 <= value
			&& value < syminfo.base2 + syminfo.len2)
		    || (syminfo.base3 <= value
			&& value < syminfo.base3 + syminfo.len3))
			break;

		si = (int)syminfo.next;
	}

	if (si == 0)
		return (0);

	/* now go through the symbol table.  There is some hair here
	 * to do buffering ... I think it will be useful to have,
	 * but I'm only guessing.
	 */
	symadr = (int)syminfo.symtbl;
	syms_to_go = syminfo.nsyms;
	bestval = 0;
	while (syms_to_go > 0) {
		/* its ok if sizeof symbuf is not a multiple of
		 * syminfo.symsize ... we'll just ignore the
		 * extra bytes at the end
		 */
		if (KREAD (symadr, symbuf, sizeof symbuf) < 0)
			return (0);
		syms_this_time = sizeof symbuf / syminfo.symsize;
		if (syms_this_time > syms_to_go)
			syms_this_time = syms_to_go;
		for (i = 0, sym = (Elf32_Sym *)symbuf;
		     i < syms_this_time;
		     i++, sym = (Elf32_Sym *)((char *)sym + syminfo.symsize)) {
			if (sym->st_name == 0L)
				continue;
			if (ELF32_ST_BIND (sym->st_info) == STB_GLOBAL
			    || (ELF32_ST_BIND (sym->st_info) == STB_LOCAL
				&& (ELF32_ST_TYPE(sym->st_info) == STT_OBJECT ||
				    ELF32_ST_TYPE(sym->st_info) == STT_FUNC))) {
				if (sym->st_value <= value
				    && sym->st_value > bestval) {
					symsave = *sym;
					bestval = sym->st_value;
					beststr = sym->st_name;
					if (bestval == value)
						break;
				}
			}
		}
		syms_to_go -= syms_this_time;
		symadr += syms_this_time * syminfo.symsize;
	}

	db_printf(5, "dbg_kobj_getsymname_old: bestval=%D", bestval);
	if (bestval == 0)
		return (0);

	if (KREAD_STR(syminfo.strings+beststr, name, sizeof name) < 0)
		return (0);
	name[sizeof name - 1] = 0;
	*offset = value - bestval;
	db_printf(5, "dbg_kobj_getsymname_old: returns '%s'",
					(name == NULL) ? "NULL" : name);
	return (name);
}

char *
dbg_kobj_getsymname(value, offset)
	unsigned int value;
	unsigned int *offset;
{
	static char name[100];
	Elf32_Sym sym;
	int si;
	struct kobj_symbol_info syminfo;
	int i, j, k;
	int symidx;
	int baseval;


	if (kobj_initted == 0)
		init_kobj ();
	
	if (kobj_symbol_info_adr == 0)
		return (0);

	if (_curmod_adr == 0)
		return (dbg_kobj_getsymname_old (value, offset));

	if (get (_curmod_adr, DSP) == -1)
		return (0);

	/*
	 * Get the pointer for the head of the linked list of modules.
	 */
	if (KREAD (kobj_symbol_info_adr, &si, sizeof si) < 0) {
		db_printf(2, "dbg_kobj_getsymname: no kobj_symbol_info_adr");
		return (0);
	}
#ifdef KRTLD_DEBUG
	/*
	 * kobj_symbol_info is not yet set, so we use
	 * a private copy of the linker symbol table.
	 */
	if (si == NULL)
		return (dbg_kobj_getname(value, offset));
#endif /* KRTLD_DEBUG */

	/*
	 * Walk the linked list of per-module syminfo structures. 
	 */
	while (si != 0) {
		if (KREAD (si, &syminfo, sizeof syminfo) < 0)
			return (0);
		if ((syminfo.base1 <= value
		     && value < syminfo.base1 + syminfo.len1)
		    || (syminfo.base2 <= value
			&& value < syminfo.base2 + syminfo.len2)
		    || (syminfo.base3 <= value
			&& value < syminfo.base3 + syminfo.len3))
			break;

		si = (int)syminfo.next;
	}

	if (si == 0)
		return (0);

	db_printf(2, "dbg_kobj_getsymname: si=%X", si);

	baseval = 0;
	i = 1; j = syminfo.nsyms - 1;
	while (i <= j) {
		k = (i + j) / 2;
		symidx = get (syminfo.byaddr + k, DSP); /* byaddr is (int *) */
		db_printf(5, "dbg_kobj_getsymname: i=%D, j=%D, k=%D",
								i, j, symidx);
		if (KREAD (syminfo.symtbl + symidx * syminfo.symsize,
				    (char *)&sym, sizeof sym) < 0) {
			db_printf(2, "dbg_kobj_getsymname: {1}");
			return (0);
		}
		if (sym.st_value == value) {
			baseval = sym.st_value;
			j = k;
			break;
		}
		if (sym.st_value > value) {
			j = k - 1;
		} else {
			baseval = sym.st_value;
			i = k + 1;
		}
	}
	if (j < 1) {
		db_printf(2, "dbg_kobj_getsymname: {2}");
		return (0);
	}

	/* the symbol at j has the value we want, but it may have 
	 * neighbors with the same value, and one of the neighbors
	 * may have a better type.  Therefore, search backward
	 * to find the beginning of the run of values, then search
	 * forward for an acceptable type
	 */

	while (1) {
		j--;
		if (j <= 1)
			break;
		symidx = get (syminfo.byaddr + j, DSP);
		KREAD (syminfo.symtbl + symidx * syminfo.symsize,
				(char *)&sym, sizeof sym);
		if (sym.st_value != baseval)
			break;
	}
	while (1) {
		j++;
		if (j >= syminfo.nsyms)
			return (0);
		symidx = get (syminfo.byaddr + j, DSP);
		KREAD (syminfo.symtbl + symidx * syminfo.symsize,
				(char *)&sym, sizeof sym);
		if (sym.st_value != baseval)
			return (0);
		if (ELF32_ST_BIND (sym.st_info) == STB_GLOBAL
		    || (ELF32_ST_BIND (sym.st_info) == STB_LOCAL
			&& (ELF32_ST_TYPE(sym.st_info) == STT_OBJECT ||
			    ELF32_ST_TYPE(sym.st_info) == STT_FUNC)))
			break;
	}

	if (KREAD_STR(syminfo.strings+sym.st_name,
				name, sizeof name) < 0) {
		db_printf(2, "dbg_kobj_getsymname: KREAD_STR failed at %X",
		    (int)(syminfo.strings+sym.st_name));
		return (0);
	}
	name[sizeof name - 1] = 0;
	*offset = value - sym.st_value;
	return (name);
}

static int
kobj_safe_read (remote_adr, adr, size)
unsigned int remote_adr;
char *adr;
int size;
{
	int val;
	int thistime;
	char *p, *q;
	int i;

	db_printf(3, "kobj_safe_read: remote_adr=%X, size=%D",
	    remote_adr, size);
	while (size > 0) {
		val = get (remote_adr, DSP);
		if (errflg){
			db_printf(3, "kobj_safe_read: errflg=%s", errflg);
			goto bad;
		}
		thistime = size;
		if (thistime > 4)
			thistime = 4;
		p = (char *)&val;
		q = adr;
		for (i = 0; i < thistime; i++)
			*q++ = *p++;
		remote_adr += thistime;
		adr += thistime;
		size -= thistime;
	}
	db_printf(6, "kobj_safe_read: returns 0");
	return (0);
 bad:
	db_printf(6, "kobj_safe_read: returns -1");
	return (-1);
}

static int
kobj_safe_getstring (remote_adr, adr, size)
unsigned int remote_adr;
char *adr;
int size;
{
	int val;
	int thistime;
	int i;
	char *p, *q;

	db_printf(6, "kobj_safe_getstring: remote_adr=%X, adr='%s', size=%D",
	    remote_adr, adr, size);
	while (size > 0) {
		val = get (remote_adr, DSP);
		if (errflg != NULL) {
			db_printf(3, "kobj_safe_getstring: errflg=%s", errflg);
			goto bad;
		}
		thistime = size;
		if (thistime > 4)
			thistime = 4;
		p = (char *)&val;
		q = adr;
		for (i = 0; i < thistime; i++) {
			if ((*q++ = *p++) == 0)
				return (0);
		}
		remote_adr += thistime;
		adr += thistime;
		size -= thistime;
	}
	db_printf(6, "kobj_safe_getstring: returns 0");
	return (0);
 bad:
	db_printf(6, "kobj_safe_getstring: returns -1");
	return (-1);
}

#endif /* KADB */
