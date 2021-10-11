/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

/*	@(#)defs.h 1.0 91/01/28 SMI */

/*	@(#)defs.h 1.19 92/03/25 */

#include <config.h>
#include <operator.h>

/*
 * BSD to USG signal translations
 */
#ifdef USG
#define	sigvec		sigaction
#define	sv_flags	sa_flags
#define	sv_handler	sa_handler
#define	setjmp(b)	sigsetjmp((b), 1)
#define	longjmp		siglongjmp
#define	jmp_buf		sigjmp_buf
#endif

/*
 * User input and tag characters.  These should be of a type
 * compatible with single character input and display (i.e.,
 * wide characters are OK).
 */
typedef	u_int	cmd_t;
typedef	u_int	tag_t;

#define	NOTAG	' '
#define	STRTOTAG(str)	((tag_t)(str)[0])

/*
 * The message cache structure.  All messages received by the monitor
 * are cached.  The cache structure is actually two separate lists:
 * a hashed list for quick look-up and a time ordered list.
 */
struct msg_cache {
	time_t		mc_rcvd;	/* time received */
	msg_t		mc_msg;		/* the message proper */
	tag_t		mc_tag;		/* the message's name/tag */
	u_short		mc_status;	/* message status flags */
	int		mc_nlines;	/* number of display lines required */
	struct msg_cache *mc_nexthash;	/* next in this bucket */
	struct msg_cache *mc_prevhash;	/* previous in this bucket */
	struct msg_cache *mc_nextrcvd;	/* next in time order */
	struct msg_cache *mc_prevrcvd;	/* previous in time order */
};

/*
 * Status flags
 */
#define	DISPLAY		0x1		/* currently being displayed */
#define	EXPIRED		0x2		/* expired but not yet removed */
#define	REVERSE		0x4		/* displayed in reverse video */
#define	FORMAT		0x8		/* text re-formatted for display */
#define	NEEDREPLY	0x10		/* message needs a reply */
#define	SENTREPLY	0x20		/* have sent a reply */
#define	GOTACK		0x40		/* reply was ACKed */
#define	GOTNACK		0x80		/* reply was NACKed */

#define	DEFAULT_PAGER	"/usr/bin/more"
#define	OPGRENT		"operator"
#define	HOLDSCREEN	10		/* duration of screen holds */
#define	TABCOLS		8

/*
 * These two defines control display of dates and
 * times.  There are two separate formats:  one for
 * the clock at the top of the display and one for
 * the info that gets prepended to each message.
 * See strftime(3) for details.
 * NB:  if localizing the program, these formats are
 * gettext'ed within the program and can be changed
 * in the catalog.
 */
#define	CLOCKFMT	"%a %b %e %R"	/* passed to strftime() */
#define	MSGDATEFMT	"%m/%d %R"	/* passed to strftime() */

#ifdef DEBUG
#define	NPIDS		0x10			/* must be power of 2 */
#define	NSEQ		0x10			/* must be power of 2 */
#else
#define	NPIDS		0x100			/* must be power of 2 */
#define	NSEQ		0x100			/* must be power of 2 */
#endif
#define	PIDMASK		NPIDS-1
#define	SEQMASK		NSEQ-1
#define	HASHMSG(id)	\
		msgcache[((id)->mid_pid)&PIDMASK][((id)->mid_seq)&SEQMASK]

struct msg_cache *msgcache[NPIDS][NSEQ];	/* message cache */
struct msg_cache	timeorder;		/* time ordered list */
struct msg_cache	*top;			/* first displayed message */
struct msg_cache	*bottom;		/* last displayed message */
int maxcache;					/* maximum cache size */

int	killch;					/* line kill character */
int	erasech;				/* back space character */

char	*progname;				/* our name */
char	*connected;				/* connected to server? */
char	opserver[BCHOSTNAMELEN];		/* current message server */
char	*current_prompt;			/* current prompt text */
char	*current_status;			/* current status text */
char	*current_input;				/* current user input text */
char	*current_filter;			/* current filter expression */
time_t	current_time;				/* current time */
time_t	screen_hold;				/* hold updates until time */
time_t	suspend;				/* screen retagging suspend */
int	doinghelp;				/* in help: no screen updates */
int	mainprompt;				/* displaying main prompt */

int	msgs_above;				/* number msgs above screen */
int	msgs_below;				/* number msgs below screen */

#ifdef __STDC__
extern void cmd_init(void);
extern void cmd_dispatch(cmd_t);
extern void bell(void);
extern void cmd_exit(void);
extern void Exit(int);
extern char *strerror(int);
extern void msg_init(void);
extern int msg_reply(tag_t tag);
extern void msg_format(struct msg_cache *, int, int);
extern int msg_filter(struct msg_cache *);
extern void msg_dispatch(msg_t *);
extern struct msg_cache *getmsgbytag(tag_t tag);
extern void expire_all(void);
extern void msg_reset(void);
extern char *mreg_comp(char *);
extern int mreg_exec(char *);
extern void helpinit(int, int);
extern void runhelp(cmd_t);
extern void scr_help(void);
extern void scr_config(void);
extern void scr_cleanup(void);
extern void scr_redraw(int);
extern void scr_reverse(struct msg_cache *);
extern void scr_adjust(struct msg_cache *);
extern void scr_topdown(int);
extern void scr_bottomup(int);
extern void scr_next(void);
extern void scr_prev(void);
extern void scr_add(struct msg_cache *);
extern time_t scr_hold(int);
extern void scr_release(time_t);
extern void scr_echo(int);
extern void prompt(const char *, ...);
extern void resetprompt(void);
extern void status(int, const char *, ...);
extern void cgets(char *, int);
extern void backspace(void);
extern void linekill(int);
extern void erasecmds(void);
extern int istag(tag_t);
#else
extern void cmd_init();
extern void cmd_dispatch();
extern void bell();
extern void cmd_exit();
extern void Exit();
extern char *strerror();
extern void msg_init();
extern int msg_reply();
extern void msg_format();
extern int msg_filter();
extern void msg_dispatch();
extern struct msg_cache *getmsgbytag();
extern void expire_all();
extern void msg_reset();
extern char *mreg_comp();
extern int mreg_exec();
extern void helpinit();
extern void runhelp();
extern void scr_help();
extern void scr_config();
extern void scr_cleanup();
extern void scr_redraw();
extern void scr_reverse();
extern void scr_adjust();
extern void scr_topdown();
extern void scr_bottomup();
extern void scr_next();
extern void scr_prev();
extern void scr_add();
extern time_t scr_hold();
extern void scr_release();
extern void scr_echo();
extern void prompt();
extern void resetprompt();
extern void status();
extern void cgets();
extern void backspace();
extern void linekill();
extern void erasecmds();
extern int istag();
#endif
