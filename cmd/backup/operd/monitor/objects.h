/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

/*	@(#)objects.h 1.0 91/01/28 SMI */

/*	@(#)objects.h 1.8 91/12/20 */

struct	object {
	char	*obj_name;		/* name or prompt string */
	int	obj_x;			/* x (column) position */
	int	obj_y;			/* y (row) position */
	caddr_t	obj_val;		/* address of value */
	void	(*obj_func)();		/* rendering function */
};

#ifdef __STDC__
static void scr_server(struct object *);
static void scr_qstatus(struct object *);
static void scr_string(struct object *);
static void scr_clock(struct object *);
static void scr_filter(struct object *);
static void scr_msg(struct msg_cache *, int);
#else
static void scr_server();
static void scr_qstatus();
static void scr_string();
static void scr_clock();
static void scr_filter();
static void scr_msg();
#endif

static struct object objects[] = {	/* XXX don't reorder this list! */
	/*
	 * Names are initialized in scr_config()
	 */
	{	/* Current operator daemon server */
		NULL,
		0,	0,
		(caddr_t)&connected,
		scr_server
	},
	{	/* Clock */
		NULL,
		-1,	0,
		(caddr_t)&current_time,
		scr_clock
	},
	{	/* queue status (above display) */
		NULL,
		0,	1,
		(caddr_t)&msgs_above,
		scr_qstatus
	},
	{	/* queue status (below display) */
		NULL,
		0,	-1,
		(caddr_t)&msgs_below,
		scr_qstatus
	},
	{	/* Status info */
		NULL,
		0,	-1,
		(caddr_t)&current_status,
		scr_string
	},
	{	/* Filter status */
		NULL,
		0,	-1,
		(caddr_t)&current_filter,
		scr_filter
	},
	{	/* Prompt */
		NULL,
		0,	-1,
		(caddr_t)&current_prompt,
		scr_string
	},
	{	/* User input */
		NULL,
		0,	-1,
		(caddr_t)&current_input,
		scr_string
	}
};
static int  nobjects = sizeof (objects)/sizeof (struct object);

#define	OBJ_SERVER	0
#define	OBJ_CLOCK	1
#define	OBJ_ABOVE	2
#define	OBJ_BELOW	3
#define	OBJ_STATUS	4
#define	OBJ_FILTER	5
#define	OBJ_PROMPT	6	/* do these last so cursor looks nice */
#define	OBJ_INPUT	7
