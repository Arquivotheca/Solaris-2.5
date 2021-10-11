/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)stab.c	1.12	94/06/22 SMI"

/* LINTLIBRARY */

/*
 * This file contains all functions relating to stab processing.  The
 * stab table is compressed by eliminating duplicate include file entries.
 */
#include	<stdio.h>
#include	<string.h>
#include	<stab.h>
#include	<memory.h>
#include	<unistd.h>
#include	<stdlib.h>
#include	<signal.h>
#include	<sys/param.h>
#include	"libld.h"

/*
 * Data structure that holds persistent data that sbfocus_symbol & sbfocus_close
 * needs. Passing in a pointer to this struct makes them re-entrant.
 */
typedef struct sbld_tag {
	FILE *	fd;
	int	failed;
} * Sbld, Sbld_rec;

/*
 * holds information needed by sbfocus_symbol and sbfocus_close.
 */
static	Sbld_rec	sb_data = { NULL, 0 };

/*
 * holds information out the output file being created.
 */
static const char *	out_fname = NULL;
static const char *	in_fname = NULL;	/* current input file */
static Half 		out_e_type = ET_NONE;


static const char
	* stab = ".stab",
	* stabstr = ".stabstr",
	* sbfocus = "sbfocus",
	* stabindex = ".stab.index",
	* stabindexstr = ".stab.indexstr",
	* stabsbfocus = ".stab.sbfocus",
	* stabsbfocusstr = ".stab.sbfocusstr";

/*
 * List of error messages
 */
static const char
	* Errmsg_missing_table =
		"%s: .stab[.index|.sbfocus] found with no matching "
		"stringtable\n",
	* Errmsg_bad_table =
		"%s: .stab[.index|.sbfocus] error when reading stringtable",
	* Errmsg_getscn = "elf_getscn failed for %s\n",
	* Errmsg_getdata = "elf_getdata failed for %s\n",
	* Errmsg_sbfocus = "%s SourceBrowser data will be lost.";


/*
 *  Signal handler is called when a SIGPIPE is encountered.  This would
 *  happen in case `sbfocus' did not exist and `ld' is writing down a
 *  pipe with no reader.  Trap signal and set failed field so that no
 *  more subsequent writes occur.
 */
static void
sigpipe_handler()
{
	sb_data.failed = 1;
}

/*
 * sbfocus_symbol() will write one symbol to a pipe that has the program
 * "sbfocus" at the receiving end. If the program has not been started yet,
 * it is started, and the pipe established. "sbfocus" is started with the
 * function arguments "type" and "name" as its arguments, in that order.
 *
 * sbfocus_symbol() should be called with four arguments:
 *	data	Pointer to a Sbld struct that the caller has allocated in
 *		permanent storage. It must be the same struct for all related
 *		calls to sbfocus_symbol().
 *	name	This is the string name of the library/executable being built.
 *	type	A string, should be one of:
 *			"-x": Building an executable or shared object
 *			"-r": Concatenating object files
 *	symbol	The string that should be written to "sbfocus". If this
 *		argument is NULL "sbfocus" is started, but no symbol is
 *		written to it.
 */
static void
sbfocus_symbol(Sbld data, const char * name, const char * type, char * symbol)
{
	int	fd[2];

	if (data->failed) {
		return;
	}

	(void) signal(SIGPIPE, (void (*)(int)) sigpipe_handler);

	if (data->fd == NULL) {
		data->failed = 0;
		(void) pipe(fd);

		switch (fork()) {
		case -1:
			eprintf(ERR_WARNING, Errmsg_sbfocus,
			    (const char *)"fork() failed.");
			data->failed = 1;
			(void) close(fd[0]);
			(void) close(fd[1]);
			return;

		/*
		 * Child process
		 */
		case 0:
			(void) close(fd[1]);
			(void) dup2(fd[0], fileno(stdin));
			(void) close(fd[0]);
			(void) execlp(sbfocus, sbfocus, type, name, 0);
			eprintf(ERR_WARNING, Errmsg_sbfocus,
			    (const char *)"`sbfocus' would not start.");
			exit(-1);

		/*
		 * Parent process
		 */
		default:
			(void) close(fd[0]);
			data->fd = fdopen(fd[1], "w");
			break;
		}
	}
	if (symbol != NULL) {
		(void) fputs(symbol, data->fd);
		(void) putc('\n', data->fd);
	}
}


static int
pass1_stabindex(const Elf_Data *s_data, const Elf_Data *str_data,
		const int cwd_len, const int name_len)
{
	struct nlist	* elem,
			* last = NULL;
	int		i,
			str_offset = 0,
			new_size = 0,
			first_object = 1,
			any_obj = 0,
			num_elem;
	/*
	 * The processing of the stab table happens in two passes.
	 *
	 * first pass: calculate if any change is needed and if so,
	 *	       how much the string table needs to be expanded
	 *	       by.
	 */
	num_elem = s_data->d_size / sizeof (struct nlist);
	for (i = 0; i < num_elem; i++) {
		char 	* str;

		elem = (struct nlist *) s_data->d_buf + i;
		switch (elem->n_type) {
		case 0:
			if (last)
				str_offset += last->n_value;
			last = elem;
			break;
		case N_OBJ:
			str = (char *) str_data->d_buf + str_offset +
				elem->n_un.n_strx;
			if ((*str == '\0') && first_object) {
				/*
				 * This is a 'CWD' N_OBJ
				 *
				 * we only record the 'cwd' once in each
				 * stringtable.  so - we only need to add
				 * it's length once to the new_size
				 */
				if (any_obj == 0) {
					any_obj++;
					new_size += cwd_len + 1;
				} /* if */
				first_object = 0;
			} /* if */
			else if (*str == '\0') {
				/*
				 * This is a 'object_name' N_OBJ
				 */
				new_size += name_len + 1;
				first_object = 1;
			} /* else if */
			break;
		default:
			/* no-op */
			break;
		} /* switch */
	} /* for */
	return (new_size);
} /* pass1_stabindex */


static int
pass2_stabindex(Elf_Data *s_data, Elf_Data *str_data, const char *name,
		int name_len, int cwd_pos, int free_pos)
{
	struct nlist	* elem,
			* last = NULL;
	int		i,
			str_offset = 0,
			first_object = 1,
			num_elem;
	/*
	 * The processing of the stab table happens in two passes.
	 *
	 * first pass: calculate if any change is needed and if so,
	 *	       how much the string table needs to be expanded
	 *	       by.
	 */
	num_elem = s_data->d_size / sizeof (struct nlist);
	for (i = 0; i < num_elem; i++) {
		char 	* str;

		elem = (struct nlist *) s_data->d_buf + i;
		switch (elem->n_type) {
		case 0:
			if (last)
				str_offset += last->n_value;
			last = elem;
			break;
		case N_OBJ:
			str = (char *) str_data->d_buf + str_offset +
				elem->n_un.n_strx;
			if ((*str == '\0') && first_object) {
				/*
				 * This is a 'CWD' N_OBJ
				 *
				 * We point it at the CWD entry that we've
				 * already placed in the new string_table.
				 */
				elem->n_un.n_strx = cwd_pos - str_offset;
				first_object = 0;
			} /* if */
			else if (*str == '\0') {
				/*
				 * This is a 'object_name' N_OBJ.
				 *
				 * Append the object name to the string table
				 * and set the elem->n_un.n_strx to point
				 * to it.
				 */
				(void) strcpy((char *)str_data->d_buf +
					free_pos, name);
				elem->n_un.n_strx = free_pos - str_offset;
				free_pos += name_len + 1;
				first_object = 1;
			} /* if */
			break;
		default:
			break;
		} /* switch */
	} /* for */

	last->n_value = str_data->d_size - str_offset;

	return (1);
} /* pass2_stabindex() */


/*
 * find_scn()
 *
 *	Find a section in elf that matches the supplied section name,
 *	type, and flags.
 *
 * Returns:
 *		section number if found
 *		 0 - if no matching section found
 *		-1 - if error
 *
 *	If shdr is a non-null pointer it will be set to the section header
 *	that was found.
 */
static size_t
find_scn(Elf *elf, const char *elf_strtab, const char *name,
	const Word sh_type, const Word sh_flags, Elf_Scn **ret_scn)
{
	Elf_Scn *	scn = NULL;
	Shdr *		scn_shdr;

	while ((scn = elf_nextscn(elf, scn)) != 0) {
		if ((scn_shdr = elf_getshdr(scn)) == NULL)
			return ((size_t)-1);
		if ((scn_shdr->sh_type == sh_type) &&
		    (scn_shdr->sh_flags == sh_flags) &&
		    (strcmp(elf_strtab + scn_shdr->sh_name, name) == 0)) {
			size_t scn_ndx;
			/*
			 * we've got a match
			 */
			if ((scn_ndx = elf_ndxscn(scn)) == SHN_UNDEF)
				return ((size_t)-1);
			if (ret_scn)
				*ret_scn = scn;
			return (scn_ndx);
		} /* if */
	} /* while */

	/*
	 * no match found
	 */
	return (0);
} /* find_scn() */


static Elf_Data *
get_str_data(Elf *elf, const char *elf_strtab, const char *strtab_name,
		Shdr *shdr, const char *in_name)
{
	Elf_Scn		* str_scn;
	Elf_Data	* str_data;

	/*
	 * The stab's string table can be found through the
	 * shdr->sh_link value.
	 */
	if (shdr->sh_link == 0) {
		/*
		 * Normally the sh_link field should point to the
		 * required strtab.  But if it's not filled in (which
		 * means something goofed somewhere) we will try to look
		 * it up from the elf file itself.
		 */
		size_t	strscn_ndx;

		strscn_ndx = find_scn(elf, elf_strtab, strtab_name,
			SHT_STRTAB, shdr->sh_flags, &str_scn);
		if (strscn_ndx == 0) {
			eprintf(ERR_FATAL, Errmsg_missing_table, in_name);
			return ((Elf_Data *)S_ERROR);
		} else if (strscn_ndx == (size_t) -1) {
			eprintf(ERR_FATAL, Errmsg_bad_table, in_name);
			return ((Elf_Data *)S_ERROR);
		}
	} else {
		if ((str_scn = elf_getscn(elf, shdr->sh_link)) == NULL) {
			eprintf(ERR_FATAL, Errmsg_getscn, in_name);
			return ((Elf_Data *)S_ERROR);
		}
	}

	if ((str_data = elf_getdata(str_scn, NULL)) == NULL) {
		eprintf(ERR_FATAL, Errmsg_getdata, in_name);
		return ((Elf_Data *)S_ERROR);
	}

	return (str_data);
}




/*
 * We examine all the stab's looking for pairs of N_OBJ's who's
 * string pointers (elem->n_un.n_strx) points to a null string.
 * When we find a pair we set the first string pointing to the
 * CWD and we set the second string to the file object name (*name).
 *
 * The stab's string table will have to be expanded to hold
 * these new enties.
 */
static int
process_stabindex(Elf *elf, const char *elf_strtab, const char *strtab_name,
			Shdr *shdr, Elf_Data *s_data, const char *name)
{
	Elf_Data	* str_data;
	static char	* cwd = NULL;
	static int 	cwd_len;
	int 		new_size,
			cwd_pos,
			name_len;
	Elf_Void	* data;

	if ((str_data = get_str_data(elf, elf_strtab, strtab_name,
					shdr, name)) == (Elf_Data *)S_ERROR)
		return (S_ERROR);

	if (cwd == NULL) {
		if ((cwd = getcwd(NULL, MAXPATHLEN)) == NULL) {
			eprintf(ERR_FATAL, "getcwd call failed");
			return (S_ERROR);
		} /* if */
		cwd_len = strlen(cwd);
	} /* if */
	name_len = strlen(name);

	new_size = pass1_stabindex(s_data, str_data, cwd_len, name_len);

	if (new_size == 0)
		/* no changes are needed */
		return (1);
	/*
	 * The .stab.index data buffer must be updated so a new copy is
	 * allocated.  The original is read-only.
	 */
	if ((data = (Elf_Void *)malloc(s_data->d_size)) == 0)
		return (S_ERROR);
	(void) memcpy(data, s_data->d_buf, s_data->d_size);
	s_data->d_buf = data;

	/*
	 * Allocate a new .stab.indexstr that is big enough to hold the new
	 * entries that we will need to place into it.
	 *
	 * Then append the 'cwd' onto the end of the current data.
	 */
	if ((data = (Elf_Void *)malloc(str_data->d_size + new_size)) == 0)
		return (S_ERROR);
	(void) memcpy(data, str_data->d_buf, str_data->d_size);
	cwd_pos = str_data->d_size;
	(void) strcpy((char *)data + cwd_pos, cwd);
	str_data->d_buf = data;
	str_data->d_size = str_data->d_size + new_size;

	(void) pass2_stabindex(s_data, str_data, name, name_len, cwd_pos,
			cwd_pos + cwd_len + 1);

	return (1);
}


static int
process_stabsbfocus(Elf *elf, const char *elf_strtab, const char *strtab_name,
		Shdr *shdr, Elf_Data *s_data, const char *in_name,
		const char *out_name, Half etype)
{
	Elf_Data	* str_data;
	struct nlist	* elem,
			* last = NULL;
	int		i,
			str_offset = 0,
			num_elem;

	if ((str_data = get_str_data(elf, elf_strtab, strtab_name,
				shdr, in_name)) == (Elf_Data *)S_ERROR)
		return (S_ERROR);

	num_elem = s_data->d_size / sizeof (struct nlist);
	for (i = 0; i < num_elem; i++) {
		char 	* type,
			* str;
		elem = (struct nlist *) s_data->d_buf + i;
		switch (elem->n_type) {
		case 0:
			if (last)
				str_offset += last->n_value;
			last = elem;
			break;
		case N_BROWS:
			str = (char *) str_data->d_buf + elem->n_un.n_strx +
						str_offset;
			if (etype == ET_REL)
				type = "-r";
			else
				type = "-x";
			sbfocus_symbol(&sb_data, out_name, type, str);
			break;
		default:
			/* no-op */
			break;
		} /* switch */
	} /* for */
	return (1);
} /* process_stabsbfocus() */


/* ARGSUSED */
void
ld_start(const char * out_name, const Half etype, const char * caller)
{
	out_fname = out_name;
	out_e_type = etype;
}


/* ARGSUSED */
void
ld_file(const char * name, const Elf_Kind kind, int flags, Elf * elf)
{
	in_fname = name;
}


/*
 * ld_section()
 *
 * Args:
 *	name	- pointer to name of current section being processed.
 *	shdr	- pointer to Section Header of current in-file being
 *		  processed.
 *	s_data	- pointer to Section Data structure of current in-file
 *		  being processed.
 *	elf	- pointer to elf structure for current in-file being
 *		  processed
 */
/* ARGSUSED */
void
ld_section(const char *scn_name, Elf32_Shdr *shdr, Elf32_Word scnndx,
	Elf_Data *s_data, Elf *elf)
{
	Ehdr		* ehdr;
	Elf_Data	* str_data;
	Elf_Scn		* str_scn;
	char		* strtab;

	ehdr = elf_getehdr(elf);
	if ((ehdr->e_type != ET_DYN) && (shdr->sh_type == SHT_PROGBITS)) {
		/*
		 * this is a minor optimization for speed.  If it's not a
		 * stab string we aren't going to strcmp() it.
		 */
		if ((scn_name[1] == 's') &&
		    (scn_name[2] == 't') &&
		    (scn_name[3] == 'a') &&
		    (scn_name[4] == 'b')) {

			str_scn = elf_getscn(elf, ehdr->e_shstrndx);
			str_data = elf_getdata(str_scn, NULL);
			strtab = str_data->d_buf;

			if (strcmp(scn_name, stab) == 0) {
				/*
				 * process .stab
				 */
				process_stabsbfocus(elf, strtab, stabstr,
					shdr, s_data, in_fname, out_fname,
					out_e_type);
			} else if (strcmp(scn_name, stabindex) == 0) {
				/* process .stab.index */
				process_stabindex(elf, strtab, stabindexstr,
						shdr, s_data, in_fname);
			} else if (strcmp(scn_name, stabsbfocus) == 0) {
				/* process .stab.sbfocus */
				process_stabsbfocus(elf, strtab,
					stabsbfocusstr, shdr, s_data,
					in_fname, out_fname, out_e_type);
			}
		}
	}
}
