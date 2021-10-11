/*
 * Copyright (c) 1992 Sun Microsystems, Inc.
 */

#include "synonyms.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <nss_dbdefs.h>

#pragma ident	"@(#)nss_dbdefs.c	1.7	94/02/02 SMI"

/*
 * XXX is there an official definition of this?  (what we want is an alignment
 * big enough for any structure
 */
#define	ALIGN(x) ((((long)(x)) + 3) & ~3)

nss_XbyY_buf_t *
_nss_XbyY_buf_alloc(struct_size, buffer_size)
	int		struct_size;
	int		buffer_size;
{
	nss_XbyY_buf_t	*b;

	/* Use one malloc for dbargs, result struct and buffer */
	b = (nss_XbyY_buf_t *)
		malloc(ALIGN(sizeof (*b)) + struct_size + buffer_size);
	if (b == 0) {
		return (0);
	}
	b->result = (void *)ALIGN(&b[1]);
	b->buffer = (char *)(b->result) + struct_size;
	b->buflen = buffer_size;
	return (b);
}

void
_nss_XbyY_buf_free(b)
	nss_XbyY_buf_t	*b;
{
	if (b != 0) {
		free(b);
	}
}

/* === Comment:  used by fget{gr,pw,sp}ent */
/* ==== Should do ye olde syslog()ing of suspiciously long lines */
#define	BUFCONST	512	/* <=== manifest constant */
void
_nss_XbyY_fgets(f, b)
	FILE		*f;
	nss_XbyY_args_t	*b;
{
	char		buf[BUFCONST];
	int		len, parsestat;

	if (fgets(buf, BUFCONST, f) == 0) {
		/* End of file */
		b->returnval = 0;
		b->erange    = 0;
		return;
	}
	len = strlen(buf) - 1;
	/* len >= 0 (otherwise we would have got EOF) */
	if (buf[len] != '\n') {
		/* Line too long for buffer; too bad */
		while (fgets(buf, BUFCONST, f) != 0 &&
		    buf[strlen(buf) - 1] != '\n') {
			;
		}
		b->returnval = 0;
		b->erange    = 1;
		return;
	}
	parsestat = (*b->str2ent)(buf, len, b->buf.result, b->buf.buffer,
		b->buf.buflen);
	if (parsestat == NSS_STR_PARSE_SUCCESS)
		b->returnval = b->buf.result;
}

/* Power-of-two alignments only... */
#define	ROUND_DOWN(n, align)	(((long)n) & ~((align) - 1))
#define	ROUND_UP(n, align)	ROUND_DOWN(((long)n) + (align) - 1, (align))

/*
 * parse the aliases string into the buffer and if successful return
 * a char ** pointer to the beginning of the aliases.
 *
 * CAUTION: (instr, instr+lenstr) and (buffer, buffer+buflen) are
 * non-intersecting memory areas. Since this is an internal interface,
 * we should be able to live with that.
 */
char **
_nss_netdb_aliases(instr, lenstr, buffer, buflen)
	const char	*instr; /* beginning of the aliases string */
	int		lenstr;
	char		*buffer; /* return val for success */
	int		buflen; /* length of the buffer available for aliases */
{

	/*
	 * Build the alias-list in the start of the buffer, and copy
	 * the strings to the end of the buffer.
	 */
	const char
		*instr_limit	= instr + lenstr;
	char	*copyptr	= buffer + buflen;
	char	**aliasp	= (char **) ROUND_UP(buffer, sizeof (*aliasp));
	char	**alias_start	= aliasp;
	int	nstrings	= 0;

	while (1) {
		const char	*str_start;
		int		str_len;

		while (instr < instr_limit && isspace(*instr)) {
			instr++;
		}
		if (instr >= instr_limit || *instr == '#') {
			break;
		}
		str_start = instr;
		while (instr < instr_limit && !isspace(*instr)) {
			instr++;
		}

		++nstrings;

		str_len = instr - str_start;
		copyptr -= str_len + 1;
		if (copyptr <= (char *)(&aliasp[nstrings + 1])) {
			/* Has to be room for the pointer to */
			/* the alias we're about to add,   */
			/* as well as the final NULL ptr.  */
			return (0);
		}
		*aliasp++ = copyptr;
		memcpy(copyptr, str_start, str_len);
		copyptr[str_len] = '\0';
	}
	*aliasp++ = 0;
	return (alias_start);
}
