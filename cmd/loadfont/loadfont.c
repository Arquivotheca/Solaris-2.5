/*
 * Copyrighted as an unpublished work.
 * Copyright (c) 1989, 1990, 1991, 1992, 1993 Sun Microsystems, Inc.
 * All rights reserved.
 *
 * RESTRICTED RIGHTS
 *
 * These programs are supplied under a license.  They may be used,
 * disclosed, and/or copied only as permitted under such license
 * agreement.  Any copy must contain the above copyright notice and
 * this restricted rights notice.  Use, copying, and/or disclosure
 * of the programs is strictly prohibited unless otherwise provided
 * in the license agreement.
 */

#pragma ident "@(#)loadfont.c	1.7	  94/11/10 SMI"


#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h> 
#include <errno.h> 
#include <ctype.h> 
#include <fcntl.h> 
#include <locale.h> 
#include <libintl.h> 
#include <sys/types.h>
#include <stropts.h> 
#include <sys/conf.h>
#include <sys/kd.h>
#include "codesets.h"
#include "fonts.h"

#ifdef MIN
#	undef MIN
#endif
#define MIN(a,b) ((a)>(b)?(b):(a))

#define GLWIDTHBYTESPADDED(bits,nbytes) \
	((nbytes) == 1 ? (((bits)+7)>>3)         \
	:(nbytes) == 2 ? ((((bits)+15)>>3)&~1)   \
	:(nbytes) == 4 ? ((((bits)+31)>>3)&~3)   \
	:(nbytes) == 8 ? ((((bits)+63)>>3)&~7)   \
	: 0)

#define	MAXENCODING		0xFFFF
#define DEFAULTGLPAD            1
#define DEFAULTBITORDER         1
#define DEFAULTBYTEORDER        0
#define DEFAULTSCANUNIT         1
#define MAPSIZE         	4096

static char *progname;			/* this prog's name */
static char *bdf_file = NULL;		/* arg to -f flag */
static int linenum = 0;			/* of input, for error messages */

static void prep_i18n(void);
static void bad_syntax(void);
static void getline(FILE *, char *);
static int prefix(const char *, const char *);
static unsigned char hexbyte(char *);
static font_t *chk_video(font_t *);
static int load_cs(const char *, const font_t *);
static int show_loaded_bdf(const font_t *);
static int switch_mode(const char *);
static int load_bdf(const char *, const font_t *, const int, const int,
		    const int, const int, const int);
static void twobyteinvert(unsigned char *, int);
static void fourbyteinvert(unsigned char *, int);
static void output_bdf(unsigned char *, const font_t *);
static void errmsg(const char *, va_list ap);
static void warning(const char *, ...);
static void fatal(const char *, ...);
static void cfatal(FILE *, unsigned char *, const char *, ...);

int
main(int argc, char *const argv[])
{

	int c;
	int fflg = 0, cflg = 0;		/* -c and -f are mutually exclusive */
	int errflg = 0;
	int retval = 0;			/* 0 -> success, !0 -> failure */
	font_t *font;


	progname = *argv;
	prep_i18n();
	if (argc == 1 || **(argv + 1) != '-')
		bad_syntax(); /* NOTREACHED */

	font = chk_video(fonts);

	while(!errflg && (c = getopt(argc, argv, "f:c:dm:")) != EOF)
		switch(c) {
		case 'f':
			if (!cflg) {
				fflg++;
				bdf_file = optarg;
				retval = load_bdf(bdf_file, font,
						  MAXENCODING,
						  DEFAULTGLPAD,
						  DEFAULTBITORDER,
				  		  DEFAULTBYTEORDER,
						  DEFAULTSCANUNIT);
			} else
				errflg++;
			break;
		case 'c':	/* is really a short hand notation for -f */
			if (!fflg) {
				cflg++;
				retval = load_cs(optarg, font);
			} else
				errflg++;
			break;
		case 'd':
			retval = show_loaded_bdf(font);
			break;
		case 'm':
			retval = switch_mode(optarg);
			break;
		case '?':
			errflg++;
			break;
		}
	if (errflg)
		bad_syntax();	/* NOTREACHED */

	return retval;
}

/*
 * Prepare for internationalization.
 */
static void
prep_i18n(void)
{
	register text_mode_t *tm = text_modes;

	if (setlocale(LC_ALL, "") == NULL)
		warning("failed to set locale");

#if !defined(TEXT_DOMAIN)                      /* Should be defined by cc -D */ 
#       define TEXT_DOMAIN "SYS_TEST"	       /* Use this only if it weren't */
#endif 
        if (textdomain(TEXT_DOMAIN) == NULL)
                warning("failed to set text domain to %s", TEXT_DOMAIN);
	/*
	 * NB.  gettext() cannot be used up until here.
	 */
	while (tm->switchfunc) {
		switch (tm->switchfunc) {
		case SW_VGAC40x25:
			tm->name = gettext("V40x25");
			break;
		case SW_VGAC80x25:
			tm->name = gettext("V80x25");
			break;
#if 0	/* XXX currently not required */
		case SW_VGA_C132x25:
			tm->name = gettext("V132x25");
			break;
		case SW_VGA_C132x43:
			tm->name = gettext("V132x43");
			break;
#endif /* 0 */
#if _EGA
		case SW_ENHC40x25:
			tm->name = gettext("E40x25");
			break;
		case SW_ENHC80x25:
			tm->name = gettext("E80x25");
			break;
		case SW_ENHC80x43:
			tm->name = gettext("E80x43");
			break;
#endif /* _EGA */
		default:
			fatal(gettext("bad video card mode"));
		}
		tm++;
	}
}

static void
bad_syntax(void)
{

	(void) fprintf(stderr, gettext("Usage:\t%s [-m mode] [-d] "
				       "[-c codeset | -f BDF_file]\n"),
		       progname);
	exit(1);
}

/*
 * Read the next line and keep a count for error messages
 *
 * NB. This function is used ONLY to read BDF files.
 *     Hence, no need to have internationalization considerations here.
 */
static void
getline(register FILE *fp, register char *s)
{
	s = fgets(s, 80, fp);
	linenum++;
	while (s) {
		int len = strlen(s);

		if (len && s[len - 1] == '\n')
			s[--len] = '\0';
		if (len && s[len - 1] == '\015')
			s[--len] = '\0';
		if ((len==0) || prefix(s, "COMMENT")) {
			s = fgets(s, 80, fp);
			linenum++;
		} else
			break;
	}
}

/*
 * Return 1 if str is a prefix of buf
 *
 * NB. This function is used ONLY on tokens in BDF files. 
 *     Hence, no need to have internationalization considerations here.
 */
static int
prefix(const char *buf, const char *str)
{
	return strncmp(buf, str, strlen(str))? 0 : 1;
}

/*
 * Make a byte from the first two hex characters in s
 *
 * NB. This function is used ONLY on hex numbers in BDF files.
 *     Hence, no need to have internationalization considerations here.
 */
static unsigned char
hexbyte(register char *s)
{
	unsigned char b = 0;
	register char c;
	int i;

	for (i=2; i; i--) {
		c = *s++;
		if ((c >= '0') && (c <= '9'))
			b = (b<<4) + (c - '0');
		else if ((c >= 'A') && (c <= 'F'))
			b = (b<<4) + 10 + (c - 'A');
		else if ((c >= 'a') && (c <= 'f'))
			b = (b<<4) + 10 + (c - 'a');
		else
			fatal(gettext("bad hex char '%c'"), c);
	} 
	return b;
}

static font_t *
chk_video(register font_t *f)
{
	int mode;
	int arg;

	if (ioctl(0, KDGETMODE, &arg) == -1) {
		/* mustn't be a KD console */
		exit(1);
	}

	if (ioctl(0, CONS_GET, &mode) == -1)
		fatal(gettext("failed to get the display mode setting"
		              ", perhaps not in text mode"));

	while (f->video_mode) {
		if (f->video_mode == mode)
			return f;
		f++;
	}
	fatal(gettext("not in text mode or video card is not supported"));
	/*NOTREACHED*/
}


/*
 * Load a given codeset.  From the codeset name the font file name is
 * deduced and then treated as if -f option was used.
 */
static int
load_cs(const char *cs_name, const font_t *f)
{
	register codeset_t *cs = f->cs;

	if (!strcmp(cs_name, "?")) {
		(void) printf(gettext("Valid codesets:\n"));
		while (cs->name != NULL) {
			(void) printf("\t%s\n", cs->name);
			cs++;
		}
		(void) printf(gettext("For a list of supported codesets, "
				      "please refer to the system manuals.\n"));

		return 0;
	}

	while (cs->name != NULL && strcmp(cs->name, cs_name))
		cs++;
	if (cs->name == NULL)
		fatal (gettext("%s is not a valid codeset"), cs_name);

	return load_bdf(cs->bdf_file, f, MAXENCODING, DEFAULTGLPAD,
			DEFAULTBITORDER, DEFAULTBYTEORDER, DEFAULTSCANUNIT);
}

static int
show_loaded_bdf(const font_t *f)
{
	unsigned char bitmap[MAPSIZE];

	if (ioctl(0, f->get_cmd, bitmap) == -1)
		fatal(gettext("cannot read currently loaded fonts"));

	output_bdf(bitmap, f);

	return 0;
}

static int
switch_mode(const char *mode)
{
	text_mode_t *tm = text_modes;

	if (!strcmp(mode, "?")) {
		(void) printf(gettext("Valid modes:\n"));
		while (tm->name != NULL) {
			(void) printf("\t%s\n", tm->name);
			tm++;
		}
		return 0;
	}

	while (tm->name != NULL && strcmp(tm->name, mode))
		tm++;
	if (tm->name == NULL)
		fatal (gettext("text mode %s not supported"), mode);

	if (ioctl (0, tm->switchfunc, NULL) == -1)
		fatal(gettext("the video card cannot switch to %s"), mode);
	return 0;
}

static int
load_bdf(const char *fileName, const font_t *f, const int max_encoding,
	 const int glyphPad, const int bitorder, const int byteorder,
	 const int scanunit)
{
	int bytesGlUsed = 0;
	int nGl = 0;
	int nchars;
	int bytesGlAlloced = 1024;	/* amount now allocated for */
	unsigned int attributes;
	char linebuf[BUFSIZ];
	char namebuf[100];
	char font_name[100];
	unsigned char *pGl = (unsigned char *) malloc((unsigned)bytesGlAlloced);
	FILE *fp;

	/*
	 * NB.  No gettext() needed for tokens in the BDF file.
	 */
	if ((fp = fopen(fileName, "r")) == NULL)
		cfatal(fp, pGl, gettext("could not open file"));
	getline(fp,linebuf);
	if ((sscanf(linebuf, "STARTFONT %s", namebuf) != 1) ||
	    strcmp(namebuf, "2.1"))
		cfatal(fp, pGl, gettext("bad 'STARTFONT'"));
	getline(fp,linebuf);
	if (sscanf(linebuf, "FONT %[^\n]", font_name) != 1)
		cfatal(fp, pGl, gettext("bad 'FONT'"));
	if (strcmp(f->name, font_name))
		cfatal(fp, pGl, gettext("'FONT' is not %s"), f->name);
	getline(fp,linebuf);
	if (!prefix(linebuf, "SIZE"))
		cfatal(fp, pGl, gettext("missing 'SIZE'"));
	getline(fp,linebuf);
	if (!prefix(linebuf, "FONTBOUNDINGBOX"))
		cfatal(fp, pGl, gettext("missing 'FONTBOUNDINGBOX'"));
	getline(fp,linebuf);

	if (prefix(linebuf, "STARTPROPERTIES")) {
		int nprops;

		if (sscanf(linebuf, "STARTPROPERTIES %d", &nprops) != 1)
			cfatal(fp, pGl, gettext("bad 'STARTPROPERTIES'"));
		getline(fp,linebuf);
		while((nprops-- > 0) && !prefix(linebuf, "ENDPROPERTIES"))
			getline(fp,linebuf);
		if (!prefix(linebuf, "ENDPROPERTIES"))
			cfatal(fp, pGl, gettext("missing 'ENDPROPERTIES'"));
		if (nprops != -1)
			cfatal(fp, pGl, gettext("%d too few properties"),
			       nprops + 1);
		getline(fp,linebuf);
	} else if (!prefix(linebuf, "CHARS")) /* properties are optional */
		cfatal(fp, pGl, gettext("bad input file, expected "
					"'STARTPROPERTIES' or 'CHARS'"));

	if (sscanf(linebuf, "CHARS %d", &nchars) != 1)
		cfatal(fp, pGl, gettext("bad 'CHARS'"));
	if (nchars < 1)
		cfatal(fp, pGl, gettext("invalid number of CHARS"));
	getline(fp,linebuf);

	while ((nchars-- > 0) && prefix(linebuf, "STARTCHAR"))  {
		int	t;
		int	ix;	/* counts bytes in a glyph */
		int	bw;	/* bounding-box width */
		int	bh;	/* bounding-box height */
		int	bl;	/* bounding-box left */
		int	bb;	/* bounding-box bottom */
		int	enc, enc2;	/* encoding */
		char	*p;	/* temp pointer into linebuf */
		int	 bytesperrow, row, hexperrow, perrow;
		char	charName[100];

		if (sscanf(linebuf, "STARTCHAR %s", charName) != 1)
			cfatal(fp, pGl, gettext("bad character name"));
		getline(fp, linebuf);
		if ((t=sscanf(linebuf, "ENCODING %d %d", &enc, &enc2)) < 1)
			cfatal(fp, pGl, gettext("bad 'ENCODING'"));
		if ((enc < -1) || ((t == 2) && (enc2 < -1)))
			cfatal(fp, pGl, gettext("bad ENCODING value"));
		if (t == 2 && enc == -1)
			enc = enc2;
		if (enc == -1) {
			warning(gettext("character '%s' ignored"), charName);
			do {
				getline(fp,linebuf);
				if (linebuf == NULL)
					cfatal(fp, pGl,
					       gettext("Unexpected EOF"));
			} while (!prefix(linebuf, "ENDCHAR"));
			getline(fp,linebuf);
			continue;
		}
		if (enc > max_encoding)
			cfatal(fp, pGl, gettext("encoding %d for character "
						"'%s' exceeds max"),
			       charName, enc);
		getline(fp, linebuf);
		getline(fp, linebuf);
		getline(fp, linebuf);
		if (sscanf(linebuf, "BBX %d %d %d %d", &bw, &bh, &bl, &bb) != 4)
			cfatal(fp, pGl, gettext("bad 'BBX'"));
		if ((bh < 0) || (bw < 0))
			cfatal(fp, pGl, gettext("character '%s' has a negative "
						"sized bitmap, %dx%d"),
			       charName, bw, bh);
		getline(fp,linebuf);
		/*
		 * attributes variable is never used, bad it has to
		 * exist in the BDF file.
		 */
		if (prefix(linebuf, "ATTRIBUTES")) {
			for (p = linebuf + strlen("ATTRIBUTES ");
			     (*p == ' ') || (*p == '\t');
			     p++)
				;		/* empty for loop */
			attributes = hexbyte(p)<< 8 + hexbyte(p + 2);
			/*
			 * Set up for BITMAP which follows
			 */
			getline(fp, linebuf);
		} else
			attributes = 0;

		if (!prefix(linebuf, "BITMAP"))
			cfatal(fp, pGl, gettext("missing 'BITMAP'"));
		bytesperrow = GLWIDTHBYTESPADDED(bw,glyphPad);
		hexperrow = (bw + 7) >> 3;
		if (hexperrow == 0)
			hexperrow = 1;
		for (row=0; row < bh; row++) {
			getline(fp,linebuf);
			p = linebuf;
			t = strlen(p);
			if (t & 1)
				cfatal(fp, pGl, gettext("odd number of "
				                        "characters in "
							"hex encoding"));
			t >>= 1;
			if ((bytesGlUsed + bytesperrow) >= bytesGlAlloced) {
				bytesGlAlloced = (bytesGlUsed+bytesperrow) * 2;
				pGl = (unsigned char *) realloc((char *)pGl,
						   (unsigned)bytesGlAlloced);
			}
			perrow = MIN(hexperrow, t);
			for (ix = 0; ix < perrow; ix++, p+=2, bytesGlUsed++)
				pGl[bytesGlUsed] = hexbyte(p);
			if (perrow && (hexperrow <= t) && (bw & 7) &&
			    (ix = (pGl[bytesGlUsed - 1] & (0xff >> (bw & 7)))))
				pGl[bytesGlUsed - 1] &= ~ix;
			for (ix=perrow; ix < bytesperrow; ix++, bytesGlUsed++)
				pGl[bytesGlUsed] = 0;
			/*
			 *  Now pad the glyph row our pad boundary.
			 */
			bytesGlUsed = GLWIDTHBYTESPADDED(bytesGlUsed<<3,
							 glyphPad);
		}
		getline(fp,linebuf);
		if (!prefix(linebuf, "ENDCHAR"))
			cfatal(fp, pGl, gettext("missing 'ENDCHAR'"));
		nGl++;
		getline(fp,linebuf);	/* get STARTCHAR or ENDFONT */
	}
	if (nchars != -1)
		cfatal(fp, pGl, gettext("%d too few characters"), nchars + 1);
	if (prefix(linebuf, "STARTCHAR"))
		cfatal(fp, pGl, gettext("more characters than specified"));
	if (!prefix(linebuf, "ENDFONT"))
		cfatal(fp, pGl, gettext("missing 'ENDFONT'"));
	if (nGl == 0)
		cfatal(fp, pGl, gettext("No characters with valid encodings"));
	if (bitorder != byteorder) {
		if (scanunit == 2)
			twobyteinvert(pGl, bytesGlUsed);
		else if (scanunit == 4)
			fourbyteinvert(pGl, bytesGlUsed);
	}

	if (ioctl(0, f->put_cmd, (char *) pGl) == -1)
		cfatal(fp, pGl, gettext("failed to download fonts"));

	free(pGl);
	return 0;
}

/*
 * Invert byte order within each 16-bits of an array.
 *
 * NB. This function is used ONLY on hex numbers in BDF files.
 *     Hence, no need to have internationalization considerations here.
 */
static void
twobyteinvert(register unsigned char *buf, register int nbytes)
{
	register unsigned char c;

	for (; nbytes > 0; nbytes -= 2, buf += 2) {
		c = *buf;
		*buf = *(buf + 1);
		*(buf + 1) = c;
	}
}

/*
 * Invert byte order within each 32-bits of an array.
 *
 * NB. This function is used ONLY on hex numbers in BDF files.
 *     Hence, no need to have internationalization considerations here.
 */
static void
fourbyteinvert(register unsigned char *buf, register int nbytes)
{
	register unsigned char c;

	for (; nbytes > 0; nbytes -= 4, buf += 4) {
		c = *buf;
		*buf = *(buf + 3);
		*(buf + 3) = c;
		c = *(buf + 1);
		*(buf + 1) = *(buf + 2);
		*(buf + 2) = c;
	}
}

/* NB.  gettext() should NOT be used for the messages in this 
 *      function as the BDF file should contain the exact
 *      strings output here.
 */
static void
output_bdf(register unsigned char *bitmap, const font_t *f)
{
	register int c, i;

	/*
	 * Print basic font info
	 */
	(void) printf("STARTFONT 2.1\nFONT %s\nSIZE %u 75 75\n"
		      "FONTBOUNDINGBOX %u %u 0 %d\n"
		      "STARTPROPERTIES 3\nFONT_DESCENT %u\n"
		      "FONT_ASCENT %u\nDEFAULT_CHAR 0\nENDPROPERTIES\n"
		      "CHARS %d\n",
		       f->name, f->height, f->width, f->height,
		       -(f->descent), f->descent,
		       f->height - f->descent, f->nchars);

	/*
	 * Print out each character
	 */
	for (c = 0; c < f->nchars; c++) {
		(void) printf("STARTCHAR C%.4x\n"
		              "ENCODING %u\nSWIDTH 666 0\n"
			      "DWIDTH %u 0\nBBX %u %u 0 %d\nBITMAP\n",
			       c, c, f->width, f->width, f->height,
			       -(f->descent));
		for (i = f->height; i != 0; i--)
			(void) printf("%.2x\n", *bitmap++);
		(void) printf("ENDCHAR\n");
	}
	(void) printf("ENDFONT\n");
}

static void
errmsg(const char *msg, va_list ap)
{
        if (msg == NULL) {
		(void) fprintf(stderr,
			       gettext("Internal error: bad error message.\n"));
		exit(1);
	}


	if (errno)
		perror(progname);

	(void) fprintf(stderr, "%s: %s%s", progname,
		       (bdf_file != NULL) ? bdf_file : "",
		       (bdf_file != NULL) ? ": " : "");

	(void) vfprintf(stderr, msg, ap);

	if (linenum)
		(void) fprintf(stderr, gettext(" (line %d).\n"), linenum);
	else
		(void) fprintf(stderr, ".\n");
}

/*VARARGS1*/
static void
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

/*
 * Clean up and then do as fatal().
 */
/*VARARGS3*/
static void
cfatal(register FILE *fp, register unsigned char *ptr, const char *msg, ...)
{
        va_list ap;

	if (fp != NULL)
		(void) fclose(fp);
	free(ptr);
	va_start(ap, msg);
	errmsg(msg, ap);
	va_end(ap);
	exit(1);
}
