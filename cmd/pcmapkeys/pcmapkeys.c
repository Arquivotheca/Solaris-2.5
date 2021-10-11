/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993 Sun Microsystems, Inc.
 * All Rights Reserved. 
 */

#pragma ident "@(#)pcmapkeys.c	1.10      94/11/10 SMI"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <termio.h>
#include <fcntl.h>
#include <locale.h>
#include <libintl.h> 
#include <sys/types.h>
#include <sys/kd.h>
#include <sys/emap.h>
#include "default.h"

typedef struct {
	struct keymap_flags flags;
	keymap_t ktable;
} keys_t;

int kd_fd;	 			  /* also ref'd in parser.y */
unsigned char emap_buf[EMAP_SIZE];	/* also ref'd in parser.y */
static char *progname;

static void prep_i18n(void);
static void bad_syntax(void);
static void show_emap(void);
static void set_mapfile(const char *const, unsigned char *const);
static int save_maps(const int, strmap_t const,
		     keys_t *const, unsigned char *const);
static void restore_maps(const int, const strmap_t const, const keys_t *const,
			 const unsigned char *const, const int);
static void prepbuf(unsigned char *const);
static int chk_emap(const unsigned char *const);
static void errmsg(const char *, va_list);
static void fatal(const char *, ...);
void warning(const char *, ...);	/* also ref'd in parser.y */
extern int yyparse(void);

int
main(int argc, char *const argv[])
{
	int c;
	unsigned char ch;
	int arg;

	progname = *argv;
	prep_i18n();
        if (argc == 1 || **(argv + 1) != '-')
		bad_syntax(); /* NOTREACHED */

	kd_fd = dup(0);

	if (ioctl(kd_fd, KDGETMODE, &arg) == -1) {
		/* mustn't be a KD console */
		exit(1);
	}
	/*
	 * KDGKBMODE is handled in the char STREAMS module which
	 * is not pushed on the pts streams in the Windows environments.
	 * Therefore, if the following ioctl fails and we don't
	 * know for sure if we are in K_RAW mode or not, still it is fair
	 * to bail out assuming that we are in K_RAW mode.  The char
	 * module is needed for the other ioctl's later on as well.
	 */
	if (ioctl(kd_fd, KDGKBMODE, &ch) == -1 || ch == K_RAW) {
		extern int errno;

		errno = 0;
		fatal(gettext("illegal in keyboard raw scancode mode, "
			      "e.g. Windows environment"));
	}

        while((c = getopt(argc, argv, "f:gnde")) != EOF)
                switch(c) {
                case 'f':
			set_mapfile(optarg, emap_buf);
                        break;
                case 'g':
			show_emap();
                        break;
                case 'n':
			if (ioctl(kd_fd, LDNMAP, NULL) == -1)
				fatal(gettext("failed to dismantle "
					      "keyboard extended map"));
                        break;
                case 'd':
			if (ioctl(kd_fd, LDDMAP, NULL) == -1)
				fatal(gettext("failed to disable "
					      "keyboard extended mapping"));
                        break;
                case 'e':
			if (ioctl(kd_fd, LDEMAP, NULL) == -1)
				fatal(gettext("failed to enable "
					      "keyboard extended mapping"));
                        break;
                case '?':
			bad_syntax();
			/* NOTREACHED */
                }          
	return 0;
}

/*
 * Prepare for internationalization.
 */
static void
prep_i18n(void)
{
	if (setlocale(LC_ALL, "") == NULL)
		warning("failed to set locale");

#if !defined(TEXT_DOMAIN)		       /* Should be defined by cc -D */
#	define TEXT_DOMAIN "SYS_TEST"	       /* Use this only if it weren't */
#endif
	if (textdomain(TEXT_DOMAIN) == NULL)
		warning("failed to set text domain to %s", TEXT_DOMAIN);
	/*
	 * NB.  gettext() cannot be used up until here.
	 */
}

static void
bad_syntax(void)
{

        (void) fprintf(stderr, gettext("Usage: %s -f mapfile | -n | "
				       "-g | -d | -e\n"), progname);
        exit(1);
}

/*
 * Dumps the current emap to stdout as hex values.  Mostly should be
 * used for debugging purposes.
 */
static void
show_emap(void)
{
	register int i;
	unsigned char emap_buf[EMAP_SIZE];
	register unsigned char *cp = emap_buf;
	register emp_t ep = (emp_t) emap_buf;

	if (ioctl(kd_fd, LDGMAP, ep) == -1) {
		extern int errno;

		if (errno == EINVAL)
			fatal(gettext("keyboard extended mapping is "
				      "not enabled"));
		fatal(gettext("failed to get keyboard extended mapping"));
	}

	if (chk_emap(cp))
		fatal(gettext("corrupted keyboard extended map"));

	for (i = 0; i < EMAP_SIZE; i++)
		(void) printf("%s%#2x, ", (i % 8) ? "" : "\n\t", *cp++);
	(void) printf("\n");
} 

static void
set_mapfile(const char *const mapfile, register unsigned char *const emap_buf)
{
	int fd, got_emap;
	strmap_t strtable;
	keys_t keys;
	unsigned char old_emap_buf[EMAP_SIZE];	/* last map on the system */

	assert(mapfile != NULL);

	prepbuf(old_emap_buf);
	got_emap = save_maps(kd_fd, strtable, &keys, old_emap_buf);

	if ((fd = open(mapfile, O_RDONLY)) == -1)
		fatal(gettext("Could not open %s\n"), mapfile);

	(void) close(0);
	dup(fd);
	prepbuf(emap_buf);

	/*
	 * As the map files are (may be) sparse, it's better to install
	 * the default keys and string maps first.
	 */
	if (ioctl(kd_fd, PIO_STRMAP, (strmap_t *) &default_strtable) == -1)
		fatal(gettext("failed to reset the string key mappings"));

	if (ioctl(kd_fd, PIO_KEYMAP, (keys_t *) &default_keys) == -1)
		fatal(gettext("failed to reset the keyboard mappings"));

	/*
	 * Parser will change the string and key mappings so from now on
	 * make sure you restore the saved keys and string maps which were
	 * installed on the system (NB. may be different from the default)
	 * before calling fatal().
	 */
	if (yyparse()) {
		restore_maps(kd_fd, strtable, &keys, old_emap_buf, got_emap);
		fatal(gettext("bad mapfile syntax"));
	}

	if (chk_emap(emap_buf)) {
		restore_maps(kd_fd, strtable, &keys, old_emap_buf, got_emap);
		fatal(gettext("bad mapfile semantics"));
	}
	if (ioctl(kd_fd, LDSMAP, emap_buf) == -1) {
		restore_maps(kd_fd, strtable, &keys, old_emap_buf, got_emap);
		fatal(gettext("failed to set the keyboard map table"));
	}
	(void) close(fd);
}

static int
save_maps(const int fd, strmap_t const strtable,
	  keys_t *const keys, unsigned char *const emap_buf)
{
	assert(fd >= 0);
	assert(strtable != NULL);
	assert(keys != NULL);
	assert(emap_buf != NULL);

	if (ioctl(fd, GIO_STRMAP, strtable) == -1)
		fatal(gettext("failed to save string key mappings")); 
	if (ioctl(fd, GIO_KEYMAP, keys) == -1)
		fatal(gettext("failed to save keyboard mappings"));
#if 0
	/*
	 * Prefer to leave this piece of code which helped me in
	 * producing the default string and key maps here, in case 
	 * we need to change the default in future.
	 */
	(void) printf("static const unsigned char default_strtable[] = {\n");
	cp = (unsigned char *) strtable;
	for (i = 0; i < STRTABLN; i++)
		(void) printf("%s%#2x, ", (i % 8) ? "" : "\n\t", *cp++);
	(void) printf("\b\b\n};\n");

	(void) printf("static const unsigned char default_keys[] = {\n");
	cp = (unsigned char *) keys;
	for (i = 0; i < sizeof(keys_t); i++)
		(void) printf("%s%#2x, ", (i % 8) ? "" : "\n\t", *cp++);
	(void) printf("\b\b\n};\n");
#endif /* 0 */
	if (ioctl(fd, LDGMAP, emap_buf) == -1) {
		extern int errno;

		if (errno == EINVAL)
			/*
			 * Keyboard extended mapping was not enabled
			 * to begin with.
			 */
			return 0;
		fatal(gettext("failed to save keyboard extended map"));
	} else
		return 1;       /* got the emap info */
	return 0;
}

static void
restore_maps(const int fd, const strmap_t const strtable,
	     const keys_t *const keys,
	     const unsigned char *const emap_buf, const int got_emap)
{
	assert(fd >= 0);
	assert(strtable != NULL);
	assert(keys != NULL);
	assert(emap_buf != NULL);

	if (ioctl(fd, PIO_STRMAP, strtable) == -1)
		fatal(gettext("failed to restore the save "
			      "string key mappings"));
	if (ioctl(fd, PIO_KEYMAP, keys) == -1)
		fatal(gettext("failed to restore the save keyboard mappings"));

	if (got_emap && ioctl(kd_fd, LDSMAP, emap_buf) == -1)
		fatal(gettext("failed to restore the saved "
			      "keyboard extended map"));
}

/*
 * Preparing the buffer as if no extended mapping is done,
 * i.e.  creating a raw buffer.
 */
static void
prepbuf(unsigned char *const emap_buf)
{
	register int  i = 0;

	assert(emap_buf != NULL);

	while (i < 256) {
		emap_buf[i] = (unsigned char) i;
		emap_buf[i + 256] = (unsigned char) i;
		emap_buf[i + 512] = 0;
		emap_buf[i + 512 + 256] = 0;
		i++;
	}
}

/* 
 * Sanity check of the map table.  Semantics is the same as
 * emap_chkmap() in emap STREAMS module in kernel.
 *
 * Returns 0 on success and never returns on failure.
 */
static int
chk_emap(const unsigned char *const bp)
{
	register int n;
	int ndind, ncind, ndcout, nsind, nschar;
	register emp_t ep = (emp_t) bp;
	register emip_t eip;

	assert(bp != NULL);

	/*
	 * Check table offsets
	 */

	n = ep->e_cind - E_DIND;   	/* must be after deadkey indices */
	if (n < 0) {
		warning(gettext("bad position of compose indexes"));
		return -1;
	}
	if (n % sizeof(struct emind)) {
		warning(gettext("bad dead key indexes"));
		return -1;
	}
	if (ep->e_cind > (EMAP_SIZE - 2 * sizeof(struct emind))) {
		warning(gettext("compose indexes offset is too large"));
		return -1;
	}
	ndind = n / sizeof(struct emind);

	n = ep->e_dctab - ep->e_cind;
	if (n < 0) {
		warning(gettext("bad position of dead/compose table"));
		return -1;
	}
	if (n % sizeof(struct emind)) {
		warning(gettext("bad compose indexes"));
		return -1;
	}
	if (ep->e_dctab > (EMAP_SIZE - sizeof(struct emind))) {
		warning(gettext("dead/compose table offset too large"));
		return -1;
	}
	ncind = n / sizeof(struct emind);

	n = ep->e_sind - ep->e_dctab;
	if (n < 0) {
		warning(gettext("bad position of string indexes"));
		return -1;
	}
	if (n % sizeof(struct emout)) {
		warning(gettext("bad dead/compose table size"));
		return -1;
	}
	if (ep->e_sind > EMAP_SIZE) {
		warning(gettext("offset of string indexes is too large"));
		return -1;
	}
	ndcout = n / sizeof(struct emout);

	n = ep->e_stab - ep->e_sind;
	if (n < 0) {
		warning(gettext("bad position of string table"));
		return -1;
	}
	if (n % sizeof(struct emind)) {
		warning(gettext("bad string indexes"));
		return -1;
	}
	nschar = EMAP_SIZE - ep->e_stab;
	if (nschar < 0) {
		warning(gettext("string table offset is too large"));
		return -1;
	}
	nsind = n / sizeof(struct emind);

	/* 
	 * Check dead/compose indices
	 */
	eip = &ep->e_dind[0];
	n = ndind + ncind;
	while (--n > 0) {
		if (eip[1].e_ind < eip[0].e_ind) {
			warning(gettext("dead key indexes not in "
				        "ascending order"));
			return -1;
		}
		++eip;
	}
	if ((n == 0) && (eip->e_ind > ndcout)) {
		warning(gettext("index > size of dead/compose table"));
		return -1;
	}
	/*
	 * Check string indices
	 */
	eip = (emip_t) ((unsigned char *)ep + ep->e_sind);
	n = nsind;
	while (--n > 0) {
		if (eip[1].e_ind < eip[0].e_ind) {
			warning(gettext("string indexes not in "
					"ascending order"));
			return -1;
		}
		++eip;
	}
	if ((n == 0) && (eip->e_ind > nschar)) {
		warning(gettext("index > size of string table"));
		return -1;
	}
	
	return 0;
}

static void
errmsg(const char *msg, va_list ap)
{
        assert(msg != NULL);

	if (errno)
		perror(progname);
	(void) fprintf(stderr, "%s: ", progname);
	(void) vfprintf(stderr, msg, ap);
	(void) fprintf(stderr, "\n");
}

/*VARARGS1*/
void
warning(const char *msg, ...)
{
        va_list ap;

	va_start(ap, msg);
	errmsg(msg, ap);
	va_end(ap);
}

/*
 * Fatal error. Exits with failure.
 */
/*VARARGS1*/
static void
fatal(const char *msg, ...)
{
        va_list ap;

	va_start(ap, msg);
	errmsg(msg, ap);
	va_end(ap);
	exit(1);
}
