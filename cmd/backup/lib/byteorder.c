/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#pragma ident	"@(#)byteorder.c	1.8	94/07/18 SMI"

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <locale.h>
#include <stdlib.h>
#include <sys/fs/ufs_inode.h>
#include <sys/fs/ufs_fs.h>
#include <sys/fs/ufs_fsdir.h>
#include <sys/fs/ufs_acl.h>
#include <protocols/dumprestore.h>
#include <byteorder.h>

struct byteorder_ctx *
byteorder_create(void)
{
	struct byteorder_ctx *rc;

	if ((rc = (struct byteorder_ctx *) calloc(1, sizeof (*rc))) == NULL)
		return (NULL);
	return (rc);
}

void
byteorder_destroy(struct byteorder_ctx *ctx)
{
	if (ctx != NULL)
		(void) free((char *) ctx);
}

void
byteorder_banner(struct byteorder_ctx *ctx, FILE *filep)
{
	if ((! ctx->initialized) || (filep == NULL))
		return;

	if (ctx->Bcvt)
		(void) fprintf(filep, gettext("Note: doing byte swapping\n"));
}

void
swabst(char *cp, char *sp)
{
	int n = 0;
	char c;

	while (*cp) {
		switch (*cp) {
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			n = (n * 10) + (*cp++ - '0');
			continue;

		case 's': case 'w': case 'h':
			c = sp[0]; sp[0] = sp[1]; sp[1] = c;
			sp++;
			break;

		case 'l':
			c = sp[0]; sp[0] = sp[3]; sp[3] = c;
			c = sp[2]; sp[2] = sp[1]; sp[1] = c;
			sp += 3;
		}
		/* Any other character, like 'b' counts as byte. */
		sp++;
		if (n <= 1) {
			n = 0; cp++;
		} else
			n--;
	}
}

long
swabl(long x)
{
	long l = x;

	swabst("l", (char *)&l);
	return (l);
}

static int
checksum(struct byteorder_ctx *ctx, int *b)
{
	register int i, j;

	if (! ctx->initialized)
		return (-1);
	j = sizeof (union u_spcl) / sizeof (int);
	i = 0;
	if (!ctx->Bcvt) {
		do
			i += *b++;
		while (--j);
	} else {
		/*
		 * What happens if we want to read restore tapes
		 * for a 16bit int machine???
		 */
		do
			i += swabl(*b++);
		while (--j);
	}

	return (i != CHECKSUM);
}

/*
 * normspcl() checks that a spclrec is valid.  it does byte/quad
 * swapping if necessary, and checks the checksum.  it does NOT convert
 * from the old filesystem format; gethead() in tape.c does that.
 *
 * ctx is the context for this package
 * sp is a pointer to a current-format spclrec, that may need to be
 *	byteswapped.
 * cs is a pointer to the thing we want to checksum.  if we're
 *	converting from the old filesystem format, it might be different
 *	from sp.
 * magic is the magic number we compare against.
 */

int
normspcl(struct byteorder_ctx *ctx, struct s_spcl *sp, int *cs, int magic)
{
	long *sv;

	if ((! ctx->initialized) && (sp->c_magic != magic)) {
		if (swabl(sp->c_magic) != magic)
			return (-1);
		ctx->Bcvt = 1;
	}
	ctx->initialized = 1;

	if (checksum(ctx, cs))
		return (-1);

	/* handle byte swapping */
	if (ctx->Bcvt) {
		char buffy[BUFSIZ];

		/*
		 * byteswap
		 *	c_type, c_date, c_ddate, c_volume, c_tapea, c_inumber,
		 *	c_magic, c_checksum,
		 *	all of c_dinode, and c_count.
		 */

		swabst("8l4s31l", (char *) sp);

		/*
		 * byteswap
		 *	c_flags, c_firstrec, and c_spare.
		 */

		swabst("34l", (char *) sp->c_label);

		/* byteswap the inodes if necessary. */

		if (sp->c_flags & DR_INODEINFO) {
			sprintf(buffy, "%dl", TP_NINOS);
			swabst(buffy, (char *) sp->c_data.s_inos);
		}

		/* if no metadata, byteswap the level */

		if (! (sp->c_flags & DR_HASMETA))
			swabst("1l", (char *) sp->c_level);
	}

	/* handle quad swapping (note -- we no longer perform this check */
	/*	we now do quad swapping iff we're doing byte swapping.)  */

	sv = sp->c_dinode.di_ic.ic_size.val;
#if 0
	if ((sp->c_dinode.di_size == 0) &&
	    ((sp->c_dinode.di_mode & IFMT) == IFDIR) &&
	    (! ctx->Qcvt) &&
	    ((sv[0] | sv[1]) != 0))
		ctx->Qcvt = 1;
#endif

	if (ctx->Bcvt) {
		long foo;

		foo = sv[1];
		sv[1] = sv[0];
		sv[0] = foo;
	}

	if (sp->c_magic != magic)
		return (-1);
	return (0);
}

/* assert(byteorder_ctx has been initialized) ! */
void
normdirect(ctx, d)
	struct byteorder_ctx *ctx;
	struct direct *d;
{
	if (ctx->Bcvt)
		swabst("l2s", (char *)d);
}

/* assert(byteorder_ctx has been initialized) ! */
void
normacls(struct byteorder_ctx *ctx, ufs_acl_t *acl, int n)
{
	int i;

	if (! ctx->Bcvt)
		return;

	for (i = 0; i < n; i++) {
		swabst("1s", (char *) &(acl[i].acl_tag));  /* u_short  */
		swabst("1s", (char *) &(acl[i].acl_perm)); /* o_mode_t */
		swabst("1l", (char *) &(acl[i].acl_who));  /* uid_t    */
	}
}
