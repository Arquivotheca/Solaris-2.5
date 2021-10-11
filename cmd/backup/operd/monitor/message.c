/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#ident	"@(#)message.c 1.0 91/01/28 SMI"

#ident	"@(#)message.c 1.20 92/03/25"

#include <stdio.h>
#include <ctype.h>
#include <locale.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <setjmp.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/timeb.h>
#include "defs.h"

struct msg_cache *msgcache[NPIDS][NSEQ];	/* message cache */
struct msg_cache timeorder;			/* time ordered list */
struct msg_cache *top;				/* first displayed message */
struct msg_cache *bottom;			/* last displayed message */
int maxcache;

static struct msg_cache	*msgfreelist;	/* free-list */
static int	msgcnt;			/* number of allocated messages */
static int	incache;		/* number of active messages */

jmp_buf	resetbuf;
int	ready;

#ifdef __STDC__
static void reset(int);
static int cmp_msgid(msg_id *, msg_id *);
static void msg_add(msg_t *);
static struct msg_cache *getmsgbyid(msg_id *);
static int cmp_tag(tag_t, tag_t);
static void msg_rm(struct msg_cache *);
#else
static void reset();
static int cmp_msgid();
static void msg_add();
static struct msg_cache *getmsgbyid();
static int cmp_tag();
static void msg_rm();
#endif

/*ARGSUSED*/
static void
reset(sig)
	int	sig;
{
	if (ready)
		longjmp(resetbuf, 1);
	status(1, gettext("Connection with server lost, aborting"));
	Exit(-1);
}

/*
 * Intialize message cache
 */
void
#ifdef __STDC__
msg_init(void)
#else
msg_init()
#endif
{
	(void) signal(SIGPIPE, reset);
	timeorder.mc_nextrcvd = &timeorder;
	timeorder.mc_prevrcvd = &timeorder;
}

/*
 * Generate a reply to a message
 */
msg_reply(tag)
	tag_t	tag;		/* tag name */
{
	char buf[MAXMSGLEN];
	struct msg_cache *mc;
	char *program;
	int n;

	mc = getmsgbytag(tag);
	if (mc == NULL) {
		bell();
		status(1, "%s%s\n%s",
			gettext("Message response rejected: "),
			gettext("That message does not exist."),
			gettext(
		    "You may only respond to messages marked (*) or (?)."));
		return (-1);
	}
	if ((mc->mc_status & GOTACK) || (mc->mc_status & GOTNACK)) {
		bell();
		status(1, "%s%s\n%s",
			gettext("Message response rejected: "),
			gettext(
		    "A response to that message has already been received."),
			gettext(
		    "You may only respond to messages marked (*) or (?)."));
		return (-1);
	}
	scr_reverse(mc);
	program = mc->mc_msg.msg_progname;
	mainprompt = 0;
	prompt("%s[%d]@%s> ",
	    program == NULL || *program == '\0' ? gettext("unnamed") : program,
	    mc->mc_msg.msg_ident.mid_pid, mc->mc_msg.msg_ident.mid_host);
	buf[0] = '\0';
	current_input = buf;
	cgets(buf, sizeof (buf));
	current_input = NULL;
	scr_reverse(mc);
	if (buf[0] == '\0') {	/* abort response */
		bell();
		status(1, gettext("Message response aborted"));
	} else {
		n = oper_reply(mc->mc_msg.msg_arbiter,
		    &mc->mc_msg.msg_ident, buf);
		if (n == 0) {
			bell();
			status(1, "%s%s\n%s",
				gettext("Message response rejected: "),
				gettext("Could not forward your response."),
				gettext(
			"You may only respond to messages marked (*) or (?)."));
		}
		mc->mc_status |= SENTREPLY;
	}
	return (n == 0 ? -1 : n);
}

/*
 * Format a message for printing.  Modifies
 * nlines to reflect the number of lines
 * of display required.  Replaces the original
 * message's length and data.
 */
void
msg_format(mc, linelen, indent)
	struct msg_cache *mc;
	int	linelen;	/* length of text line */
	int	indent;		/* indentation for FIRST line */
{
	msg_t	*msgp = &mc->mc_msg;
	struct	tm *mtm;
	char	msgbuf[1024];
	register char *cp, *dp;
	register int i;
	int thisline;		/* length of current line */
	int len;		/* length of formatted message text */
	int inspace = 0;	/* currently in white space */

	if (mc->mc_status & FORMAT)
		return;		/* already re-formatted */

	mc->mc_nlines = 1;

	mtm = localtime(&msgp->msg_time);
	(void) strftime(msgbuf, sizeof (msgbuf), gettext(MSGDATEFMT), mtm);
	cp = &msgbuf[strlen(msgbuf)];
	(void) sprintf(cp, " %s@%s: ",
	    msgp->msg_progname[0] ? msgp->msg_progname : gettext("unnamed"),
	    msgp->msg_ident.mid_host);
	cp = strrchr(msgbuf, ' ');
	len = ++cp - msgbuf;
	thisline = linelen - indent;
	/*
	 * Fit the message text onto as many lines
	 * as needed by breaking on word boundaries.
	 * All contiguous white space is treated as
	 * a single blank.
	 */
	for (i = 0, dp = msgp->msg_data; i < msgp->msg_len; i++) {
		register char *wp;	/* pointer within a word */
		int lpw;		/* letters this word */

		if (isspace((u_char)*dp)) {
			++dp;
			if (!inspace) {
				inspace++;
				if (++len > thisline) {
					len = 0;
					mc->mc_nlines++;
					thisline = linelen - TABCOLS;
				} else
					*cp++ = ' ';
			}
			continue;
		}
		inspace = 0;
		for (wp = dp, lpw = 0; *wp && !isspace((u_char)*wp); wp++)
			lpw++;
		if (len + lpw > thisline) {
			for (; len < thisline; len++)
				*cp++ = ' ';		/* pad to eol */
			len = 0;
			mc->mc_nlines++;
			thisline = linelen - TABCOLS;
		}
		(void) strncpy(cp, dp, lpw);
		cp += lpw;
		dp += lpw;
		len += lpw;
	}
	*cp = '\0';
	/*
	 * We may have to extend the data portion
	 * of the message to make room for the
	 * additional status info and padding
	 * we added to the original text.
	 */
	len = strlen(msgbuf)+1;
	if (len > MAXMSGLEN) {
		mc = (struct msg_cache *)realloc((char *)mc,
		    (unsigned)(sizeof (struct msg_cache)+(len - MAXMSGLEN)));
		if (mc == NULL) {
			bell();
			status(0, "realloc: %s", strerror(errno));
			Exit(-1);
		}
	}
	(void) strcpy(mc->mc_msg.msg_data, msgbuf);
	mc->mc_msg.msg_len = len;
	mc->mc_status |= FORMAT;
}

/*
 * Apply filters to a message.  Return 1
 * if message passes filter parameters,
 * 0 otherwise.
 */
msg_filter(mc)
	struct msg_cache *mc;
{
	msg_t	*msgp = &mc->mc_msg;

	if (mc->mc_status & EXPIRED)
		return (0);
	if ((msgp->msg_type & MSG_DISPLAY) == 0)
		return (0);
	if (current_filter && (mreg_exec(msgp->msg_data) != 1))
		return (0);
	return (1);
}

/*
 * Dispatch to appropriate message
 * processing function
 */
void
msg_dispatch(msgp)
	msg_t	*msgp;
{
	struct msg_cache *target;

	if (HASTARGET(msgp->msg_type)) {
		target = getmsgbyid(&msgp->msg_target);
		if (target == NULL) {
#ifdef DEBUG

			/* XGETTEXT:  #ifdef DEBUG only */
			status(1, gettext(
			    "Target for %x (%d from %d on %s) not found"),
				msgp->msg_type,
				msgp->msg_target.mid_seq,
				msgp->msg_target.mid_pid,
				msgp->msg_target.mid_host);
#endif
			return;
		}
		if (msgp->msg_type & MSG_ACK) {
			target->mc_status |= GOTACK;
		} else if (msgp->msg_type & MSG_NACK) {
			target->mc_status |= GOTNACK;
			status(1, "%s%s",
				gettext("Message response rejected: "),
				gettext(
		    "A response to that message has already been received."));
		} else if (msgp->msg_type & MSG_NOTOP) {
			target->mc_status |= GOTNACK;
			status(1, gettext(
			    "%sYou are not an operator on system '%s'."),
				gettext("Message response rejected: "),
				target->mc_msg.msg_ident.mid_host);
		} else if (msgp->msg_type & MSG_CANCEL) {
			msg_rm(target);
			if (!suspend)
				scr_topdown(0);
		}
		resetprompt();
		scr_redraw(0);
	} else
		msg_add(msgp);
}

/*
 * Add a message to the cache
 */
static void
msg_add(msgp)
	msg_t	*msgp;
{
	struct msg_cache *mc, **bucket;
	time_t	h;

	if (getmsgbyid(&msgp->msg_ident)) {	/* duplicate */
#ifdef DEBUG

		/* XGETTEXT:  #ifdef DEBUG only */
		status(1, gettext("Duplicate message %d@%s[%d] received"),
		    msgp->msg_ident.mid_seq,
		    msgp->msg_ident.mid_host,
		    msgp->msg_ident.mid_pid);
#endif
		return;
	}
	/*
	 * There are three places we can obtain storage:
	 *   - the free list of cancelled/expired messages
	 *   - dynamic memory allocation
	 *   - the oldest active message
	 * The oldest active message is only used if the
	 * number of cached messages is equal or greater
	 * than the specified maximum.
	 */
	if (msgfreelist) {
		mc = msgfreelist;
		msgfreelist = msgfreelist->mc_nextrcvd;
		if (msgfreelist == &timeorder)
			msgfreelist = NULL;
	} else if (maxcache == 0 || msgcnt < maxcache) {
		msgcnt++;
		mc = (struct msg_cache *)
		    malloc((unsigned)sizeof (struct msg_cache));
		if (mc == NULL) {
			bell();
			status(0, "malloc: %s", strerror(errno));
			Exit(-1);
		}
	} else {
		for (mc = timeorder.mc_nextrcvd;
		    mc != &timeorder; mc = mc->mc_nextrcvd)
			if ((mc->mc_status & DISPLAY) == 0)
				break;
		if (mc == &timeorder) {
			status(0, gettext(
		    "PANIC: cannot allocate space for incoming message!"));
			Exit(-1);
		}
		/*
		 * remove from time-ordered list
		 */
		mc->mc_nextrcvd->mc_prevrcvd = mc->mc_prevrcvd;
		mc->mc_prevrcvd->mc_nextrcvd = mc->mc_nextrcvd;
		/*
		 * remove from hash bucket
		 */
		if (mc->mc_prevhash)
			mc->mc_prevhash->mc_nexthash = mc->mc_nexthash;
		if (mc->mc_nexthash)
			mc->mc_nexthash->mc_prevhash = mc->mc_prevhash;
		bucket = &HASHMSG(&msgp->msg_ident);
		if (*bucket)
			*bucket = NULL;
		incache--;
	}
	incache++;
	mc->mc_msg = *msgp;
	(void) time(&mc->mc_rcvd);
	mc->mc_nlines = 0;
	mc->mc_status = 0;
	mc->mc_tag = NOTAG;
	if (mc->mc_msg.msg_type & MSG_NEEDREPLY)
		mc->mc_status |= NEEDREPLY;
	/*
	 * Add to cache at beginning of appropriate bucket
	 */
	bucket = &HASHMSG(&msgp->msg_ident);
	if (*bucket == NULL) {
		mc->mc_nexthash = NULL;
		mc->mc_prevhash = NULL;
		*bucket = mc;
	} else {
		mc->mc_nexthash = (*bucket)->mc_nexthash;
		mc->mc_prevhash = *bucket;
		(*bucket)->mc_nexthash = mc;
	}
	/*
	 * Add to time-ordered receipt list
	 */
	h = scr_hold(0);
	mc->mc_nextrcvd = &timeorder;
	mc->mc_prevrcvd = timeorder.mc_prevrcvd;
	timeorder.mc_prevrcvd->mc_nextrcvd = mc;
	timeorder.mc_prevrcvd = mc;
	scr_release(h);
	scr_add(mc);
}

/*
 * Locate a message in the cache
 */
static struct msg_cache *
getmsgbyid(id)
	msg_id *id;
{
	register struct msg_cache *mc;
	struct msg_cache **bucket = &HASHMSG(id);

	if (*bucket == NULL)
		return (NULL);
	for (mc = *bucket; mc; mc = mc->mc_nexthash)
		if (cmp_msgid(id, &mc->mc_msg.msg_ident) == 0)
			break;
	return (mc);
}

struct msg_cache *
getmsgbytag(tag)
	tag_t	tag;
{
	register struct msg_cache *mc;

	if (!top || !bottom)
		return (NULL);
	for (mc = top; mc != bottom->mc_nextrcvd; mc = mc->mc_nextrcvd)
		if (cmp_tag(mc->mc_tag, tag) == 0)
			break;
	return (mc != bottom->mc_nextrcvd ? mc : NULL);
}

static int
cmp_msgid(id1, id2)
	msg_id *id1, *id2;
{
	if (id1->mid_pid != id2->mid_pid)
		return (1);
	if (id1->mid_seq != id2->mid_seq)
		return (1);
	if (strcmp(id1->mid_host, id2->mid_host))
		return (1);
	return (0);
}

static int
cmp_tag(tag1, tag2)
	tag_t	tag1, tag2;
{
	if (tag1 == tag2)
		return (0);
	if (tag1 < tag2)
		return (-1);
	return (1);
}

/*
 * Make a pass through all cached messages and throw away
 * those that have expired.  This occurs every 60 seconds.
 */
void
#ifdef __STDC__
expire_all(void)
#else
expire_all()
#endif
{
	register struct msg_cache *mc, *next;
	register msg_t	*msgp;

#ifdef DEBUG
	if (screen_hold || suspend)
		/* XGETTEXT:  #ifdef DEBUG only */
		status(1,
		    gettext("%s:  marking expired messages"), "expire_all");
	else
		/* XGETTEXT:  #ifdef DEBUG only */
		status(1,
		    gettext("%s:  removing expired messages"), "expire_all");
#endif
#ifdef lint
	next = &timeorder;
#endif
	for (mc = timeorder.mc_nextrcvd; mc != &timeorder; mc = next) {
		next = mc->mc_nextrcvd;
		msgp = &mc->mc_msg;
		if (msgp->msg_ttl &&
		    current_time >= (mc->mc_rcvd + msgp->msg_ttl))
			msg_rm(mc);
	}
	if (!screen_hold && !suspend) {
		scr_topdown(0);
		resetprompt();
		scr_redraw(0);
	}
}

/*
 * Delete a message from the cache
 */
static void
msg_rm(mc)
	struct msg_cache *mc;
{
	struct msg_cache **bucket;
	time_t	h;

	mc->mc_status |= EXPIRED;
	if ((screen_hold || suspend) && (mc->mc_status & DISPLAY))
		return;
	h = scr_hold(0);
	scr_adjust(mc);
	/*
	 * remove from time-ordered list
	 */
	mc->mc_nextrcvd->mc_prevrcvd = mc->mc_prevrcvd;
	mc->mc_prevrcvd->mc_nextrcvd = mc->mc_nextrcvd;
	/*
	 * remove from active cache
	 */
	if (mc->mc_prevhash)
		mc->mc_prevhash->mc_nexthash = mc->mc_nexthash;
	if (mc->mc_nexthash)
		mc->mc_nexthash->mc_prevhash = mc->mc_prevhash;
	bucket = &HASHMSG(&mc->mc_msg.msg_ident);
	if (*bucket == mc)
		*bucket = NULL;
	/*
	 * add to free list
	 */
	mc->mc_nextrcvd = msgfreelist;
	mc->mc_prevrcvd = NULL;
	msgfreelist = mc;
	incache--;
	scr_release(h);
}

/*
 * Remove all messages from the cache
 */
void
#ifdef __STDC__
msg_reset(void)
#else
msg_reset()
#endif
{
	time_t	h = scr_hold(0);

	(void) memset((char *)msgcache, 0, sizeof (msgcache));
	msgfreelist = timeorder.mc_nextrcvd;
	if (msgfreelist == &timeorder)
		msgfreelist = NULL;
	timeorder.mc_nextrcvd = &timeorder;
	timeorder.mc_prevrcvd = &timeorder;
	msgs_above = msgs_below = 0;
	top = bottom = NULL;
	incache = 0;
	scr_release(h);
}
