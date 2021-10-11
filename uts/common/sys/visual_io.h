/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#ifndef _SYS_VISUAL_IO_H
#define	_SYS_VISUAL_IO_H

#pragma ident	"@(#)visual_io.h	1.4	93/07/14 SMI"

#ifdef __cplusplus
extern "C" {
#endif

#define	VIOC	('V' << 8)
#define	VIOCF	('F' << 8)


/*
 * Device Identification
 *
 * VIS_GETIDENTIFIER returns an identifier string to uniquely identify
 * a device type used in the Solaris VISUAL environment.  The identifier
 * must be unique.  We suggest the convention:
 *
 *	<companysymbol><devicetype>
 *
 * for example: SUNWcg6
 */

#define	VIS_MAXNAMELEN 128

struct vis_identifier {
	char name[VIS_MAXNAMELEN];	/* <companysymbol><devicename>	*/
};

#define	VIS_GETIDENTIFIER	(VIOC | 0)



/*
 * Hardware Cursor Control
 *
 * Devices with hardware cursors may implement these ioctls in their
 * kernel device drivers.
 */


struct vis_cursorpos {
	short x;		/* cursor x coordinate	*/
	short y;		/* cursor y coordinate	*/
};

struct vis_cursorcmap {
	int		version;	/* version			*/
	int		reserved;
	unsigned char	*red;		/* red color map elements	*/
	unsigned char	*green;		/* green color map elements	*/
	unsigned char	*blue;		/* blue color map elements	*/
};


/*
 * These ioctls fetch and set various cursor attributes, using the
 * vis_cursor struct.
 */

#define	VIS_SETCURSOR	(VIOCF|24)
#define	VIS_GETCURSOR	(VIOCF|25)

struct vis_cursor {
	short			set;		/* what to set		*/
	short			enable;		/* cursor on/off	*/
	struct vis_cursorpos	pos;		/* cursor position	*/
	struct vis_cursorpos	hot;		/* cursor hot spot	*/
	struct vis_cursorcmap	cmap;		/* color map info	*/
	struct vis_cursorpos	size;		/* cursor bit map size	*/
	char			*image;		/* cursor image bits	*/
	char			*mask;		/* cursor mask bits	*/
};

#define	VIS_CURSOR_SETCURSOR	0x01		/* set cursor		*/
#define	VIS_CURSOR_SETPOSITION	0x02		/* set cursor position	*/
#define	VIS_CURSOR_SETHOTSPOT	0x04		/* set cursor hot spot	*/
#define	VIS_CURSOR_SETCOLORMAP	0x08		/* set cursor colormap	*/
#define	VIS_CURSOR_SETSHAPE	0x10		/* set cursor shape	*/

#define	VIS_CURSOR_SETALL	(VIS_CURSOR_SETCURSOR | \
    VIS_CURSOR_SETPOSITION	| \
    VIS_CURSOR_SETHOTSPOT	| \
    VIS_CURSOR_SETCOLORMAP	| \
    VIS_CURSOR_SETSHAPE)


/*
 * These ioctls fetch and move the current cursor position, using the
 * vis_cursorposition struct.
 */

#define	VIS_MOVECURSOR		(VIOCF|26)
#define	VIS_GETCURSORPOS	(VIOCF|27)



#ifdef __cplusplus
}
#endif

#endif	/* !_SYS_VISUAL_IO_H */
