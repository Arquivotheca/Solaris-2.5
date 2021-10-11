/*
 * Copyrighted as an unpublished work.
 * Copyright (c) 1993 Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident "@(#)fonts.h	1.2	  93/03/17 SMI"

typedef struct {
	const int video_mode;		/* video card mode */
	const char name[10];
	const unsigned short width;
	const unsigned short height;
	const unsigned short descent;
	const unsigned short nchars;	/* # of chars/fonts */
	codeset_t *cs;
	const int get_cmd;		/* ioctl cmd to get the fonts */
	const int put_cmd;		/* ioctl cmd to put the fonts */
} font_t;

/*
 * Should not use gettext() for fonts[].name as they are used
 * in the BDF files.
 */
static font_t fonts[] = {	/* indexed on video_mode */
#if _EGA
	{ M_B40x25,
		"8x8", 8, 8, 2, 256, chars8x8, GIO_FONT8x8, PIO_FONT8x8 },
	{ M_C40x25,
		"8x8", 8, 8, 2, 256, chars8x8, GIO_FONT8x8, PIO_FONT8x8 },
	{ M_B80x25,
		"8x8", 8, 8, 2, 256, chars8x8, GIO_FONT8x8, PIO_FONT8x8 },
	{ M_C80x25,
		"8x8", 8, 8, 2, 256, chars8x8, GIO_FONT8x8, PIO_FONT8x8 },
	{ M_ENH_B80x43,
		"8x8", 8, 8, 2, 256, chars8x8, GIO_FONT8x8, PIO_FONT8x8 },
	{ M_ENH_C80x43,
		"8x8", 8, 8, 2, 256, chars8x8, GIO_FONT8x8, PIO_FONT8x8 },
	{ M_ENH_B40x25,
		"8x14", 8, 14, 3, 256, chars8x14, GIO_FONT8x14, PIO_FONT8x14 },
	{ M_ENH_C40x25,
		"8x14", 8, 14, 3, 256, chars8x14, GIO_FONT8x14, PIO_FONT8x14 },
	{ M_ENH_B80x25,
		"8x14", 8, 14, 3, 256, chars8x14, GIO_FONT8x14, PIO_FONT8x14 },
	{ M_ENH_C80x25,
		"8x14", 8, 14, 3, 256, chars8x14, GIO_FONT8x14, PIO_FONT8x14 },
	{ M_EGAMONO80x25,
		"8x14m", 8, 14, 3, 256, chars8x14m, GIO_FONT8x14, PIO_FONT8x14},
#endif /* _EGA */

#if 0   	/* XXX currently not required */ 
	{ DM_VGA_C132x25, ? },
	{ DM_VGA_C132x43, ? },
#endif /* 0 */

	{ DM_VGA_C40x25,
		"8x16", 8, 16, 4, 256, chars8x16, GIO_FONT8x16, PIO_FONT8x16 },
	{ DM_VGA_C80x25,
		"8x16", 8, 16, 4, 256, chars8x16, GIO_FONT8x16, PIO_FONT8x16 },
	{ DM_VGAMONO80x25,
		"8x16", 8, 16, 4, 256, chars8x16, GIO_FONT8x16, PIO_FONT8x16 },
	{ DM_VGA_B40x25,
		"8x16", 8, 16, 4, 256, chars8x16, GIO_FONT8x16, PIO_FONT8x16 },
	{ DM_VGA_B80x25,
		"8x16", 8, 16, 4, 256, chars8x16, GIO_FONT8x16, PIO_FONT8x16 },
	{ 0 }
};

typedef struct {
	char *name;
	const int switchfunc;
} text_mode_t;

static text_mode_t text_modes[] = {
	{ NULL, SW_VGAC40x25 },
	{ NULL, SW_VGAC80x25 },
#if 0   	/* XXX currently not required */ 
	{ NULL, SW_VGA_C132x25 },
	{ NULL, SW_VGA_C132x43 },
#endif /* 0 */
#if _EGA
	{ NULL, SW_ENHC40x25  },
	{ NULL, SW_ENHC80x25  },
	{ NULL, SW_ENHC80x43  },
#endif /* _EGA */
	{ NULL, 0}
};
