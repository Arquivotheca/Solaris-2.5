/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ifndef	_SYS_GPIO_H
#define	_SYS_GPIO_H

#pragma ident	"@(#)gpio.h	1.11	92/07/14 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * General purpose structure for passing info between GP/fb/processes
 */
struct gp1fbinfo {
	int	fb_vmeaddr;	/* physical color board address */
	int	fb_hwwidth;	/* fb board width */
	int	fb_hwheight;	/* fb board height */
	int	addrdelta;	/* phys addr diff between fb and gp */
	caddr_t	fb_ropaddr;	/* cg2 va thru kernelmap */
	int	fbunit;		/* fb unit to use  for a, b, c, d */
};


/*
 * The following ioctl commands allow for the transferring of data
 * between the graphics processor and color boards and processes.
 */

#define	GIOC		('G'<<8)

/* passes information about fb into driver */
#define	GP1IO_PUT_INFO			(GIOC|0)

/* hands out a static block  from the GP */
#define	GP1IO_GET_STATIC_BLOCK		(GIOC|1)

/* frees a static block from the GP */
#define	GP1IO_FREE_STATIC_BLOCK		(GIOC|2)

/* see if this gp/fb combo can see the GP */
#define	GP1IO_GET_GBUFFER_STATE		(GIOC|3)

/* restarts the GP if neccessary */
#define	GP1IO_CHK_GP			(GIOC|4)

/* returns the number of restarts of a gpunit since power on */
/* needed to differentiate SIGXCPU calls in user land */
#define	GP1IO_GET_RESTART_COUNT		(GIOC|5)

/* configures /dev/fb to talk to a gpunit */
#define	GP1IO_REDIRECT_DEVFB		(GIOC|6)

/* returns the requested minor device */
#define	GP1IO_GET_REQDEV		(GIOC|7)

/* returns the true minor device */
#define	GP1IO_GET_TRUMINORDEV		(GIOC|8)

/* see if there is a GB attached to this GP */
#define	GP1IO_CHK_FOR_GBUFFER 		(GIOC|9)

/* set the fb that can use the Graphics BUffer */
#define	GP1IO_SET_USING_GBUFFER		(GIOC|10)

struct static_block_info
{
	int	sbi_count;
	char	*sbi_array;
};

/* inform which blocks are owned by the current process. */
#define	GP1IO_INFO_STATIC_BLOCK		(GIOC|11)

#define	GP1_UNBIND_FBUNIT  0x08000000

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_GPIO_H */
