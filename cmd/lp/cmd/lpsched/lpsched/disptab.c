/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)disptab.c	1.10	93/10/18 SMI"	/* SVr4.0 1.5.1.4	*/

# include	"dispatch.h"

static void		r_H(),
			r_HS();

static DISPATCH			dispatch_table[] = {
/* R_BAD_MESSAGE            */  0,			D_BADMSG,
/* S_NEW_QUEUE              */  0,			D_BADMSG,
/* R_NEW_QUEUE              */  0,			D_BADMSG,
/* S_ALLOC_FILES            */  s_alloc_files,		0,
/* R_ALLOC_FILES            */  0,			D_BADMSG,
/* S_PRINT_REQUEST          */  s_print_request,	0,
/* R_PRINT_REQUEST          */  0,			D_BADMSG,
/* S_START_CHANGE_REQUEST   */  s_start_change_request,	0,
/* R_START_CHANGE_REQUEST   */  0,			D_BADMSG,
/* S_END_CHANGE_REQUEST     */  s_end_change_request,	0,
/* R_END_CHANGE_REQUEST     */  0,			D_BADMSG,
/* S_CANCEL_REQUEST         */  s_cancel_request,	0,
/* R_CANCEL_REQUEST         */  0,			D_BADMSG,
/* S_INQUIRE_REQUEST        */  s_inquire_request,	0,
/* R_INQUIRE_REQUEST        */  0,                      D_BADMSG,
/* S_LOAD_PRINTER           */  s_load_printer,		D_ADMIN,
/* R_LOAD_PRINTER           */  r_H,                    D_BADMSG,
/* S_UNLOAD_PRINTER         */  s_unload_printer,	D_ADMIN,
/* R_UNLOAD_PRINTER         */  r_H,                    D_BADMSG,
/* S_INQUIRE_PRINTER_STATUS */  s_inquire_printer_status, 0,
/* R_INQUIRE_PRINTER_STATUS */  0,                      D_BADMSG,
/* S_LOAD_CLASS             */  s_load_class,		D_ADMIN,
/* R_LOAD_CLASS             */  r_H,                    D_BADMSG,
/* S_UNLOAD_CLASS           */  s_unload_class,		D_ADMIN,
/* R_UNLOAD_CLASS           */  r_H,                    D_BADMSG,
/* S_INQUIRE_CLASS          */  s_inquire_class,	0,
/* R_INQUIRE_CLASS          */  0,                      D_BADMSG,
/* S_MOUNT                  */  s_mount,		D_ADMIN,
/* R_MOUNT                  */  r_H,                    D_BADMSG,
/* S_UNMOUNT                */  s_unmount,		D_ADMIN,
/* R_UNMOUNT                */  r_H,                    D_BADMSG,
/* S_MOVE_REQUEST           */  s_move_request,		D_ADMIN,
/* R_MOVE_REQUEST           */  r_H,                    D_BADMSG,
/* S_MOVE_DEST              */  s_move_dest,		D_ADMIN,
/* R_MOVE_DEST              */  r_HS,                   D_BADMSG,
/* S_ACCEPT_DEST            */  s_accept_dest,		D_ADMIN,
/* R_ACCEPT_DEST            */  r_H,                    D_BADMSG,
/* S_REJECT_DEST            */  s_reject_dest,		D_ADMIN,
/* R_REJECT_DEST            */  r_H,                    D_BADMSG,
/* S_ENABLE_DEST            */  s_enable_dest,		D_ADMIN,
/* R_ENABLE_DEST            */  r_H,                    D_BADMSG,
/* S_DISABLE_DEST           */  s_disable_dest,		D_ADMIN,
/* R_DISABLE_DEST           */  r_HS,                   D_BADMSG,
/* S_LOAD_FILTER_TABLE      */  s_load_filter_table,	D_ADMIN,
/* R_LOAD_FILTER_TABLE      */  r_H,                    D_BADMSG,
/* S_UNLOAD_FILTER_TABLE    */  s_unload_filter_table,	D_ADMIN,
/* R_UNLOAD_FILTER_TABLE    */  r_H,                    D_BADMSG,
/* S_LOAD_PRINTWHEEL        */  s_load_printwheel,	D_ADMIN,
/* R_LOAD_PRINTWHEEL        */  r_H,                    D_BADMSG,
/* S_UNLOAD_PRINTWHEEL      */  s_unload_printwheel,	D_ADMIN,
/* R_UNLOAD_PRINTWHEEL      */  r_H,                    D_BADMSG,
/* S_LOAD_USER_FILE         */  s_load_user_file,	D_ADMIN,
/* R_LOAD_USER_FILE         */  r_H,                    D_BADMSG,
/* S_UNLOAD_USER_FILE       */  s_unload_user_file,	D_ADMIN,
/* R_UNLOAD_USER_FILE       */  r_H,                    D_BADMSG,
/* S_LOAD_FORM              */  s_load_form,		D_ADMIN,
/* R_LOAD_FORM              */  r_H,                    D_BADMSG,
/* S_UNLOAD_FORM            */  s_unload_form,		D_ADMIN,
/* R_UNLOAD_FORM            */  r_H,                    D_BADMSG,
/* S_GETSTATUS              */  0,			D_ADMIN,
/* R_GETSTATUS              */  0,                      D_BADMSG,
/* S_QUIET_ALERT            */  s_quiet_alert,		D_ADMIN,
/* R_QUIET_ALERT            */  r_H,                    D_BADMSG,
/* S_SEND_FAULT             */  s_send_fault,		0,
/* R_SEND_FAULT             */  0,                      D_BADMSG,
/* S_SHUTDOWN               */  s_shutdown,		D_ADMIN,
/* R_SHUTDOWN               */  r_H,                    D_BADMSG,
/* S_GOODBYE		    */	0,			D_BADMSG,
/* S_CHILD_DONE		    */	s_child_done,		0,
/* I_GET_TYPE		    */	0,			D_BADMSG,
/* I_QUEUE_CHK		    */	0,			D_BADMSG,
/* R_CONNECT		    */	0,			D_BADMSG,
/* S_GET_STATUS		    */	0,			D_BADMSG,
/* R_GET_STATUS		    */	0,			D_BADMSG,
/* S_INQUIRE_REQUEST_RANK   */	s_inquire_request_rank,	0,
/* R_INQUIRE_REQUEST_RANK   */	0,			D_BADMSG,
/* S_CANCEL		    */	s_cancel,		0,
/* R_CANCEL		    */	0,			D_BADMSG,
/* S_NEW_CHILD		    */	0,			D_BADMSG,
/* R_NEW_CHILD		    */	r_new_child,		D_SYSTEM,
/* S_SEND_JOB		    */	0,			D_BADMSG,
/* R_SEND_JOB		    */	r_send_job,		D_SYSTEM,
/* S_JOB_COMPLETED	    */	s_job_completed,	0,
/* R_JOB_COMPLETED	    */	0,			D_BADMSG,
/* S_INQUIRE_REMOTE_PRINTER */	s_inquire_remote_printer, 0,
/* R_INQUIRE_REMOTE_PRINTER */	0,			D_BADMSG,
/* S_LOAD_SYSTEM            */  s_load_system,		D_ADMIN,
/* R_LOAD_SYSTEM            */  r_H,                    D_BADMSG,
/* S_UNLOAD_SYSTEM          */  s_unload_system,	D_ADMIN,
/* R_UNLOAD_SYSTEM          */  r_H,                    D_BADMSG,
/* S_CLEAR_FAULT            */  s_clear_fault,		0,
/* R_CLEAR_FAULT            */  0,                      D_BADMSG,
/* S_MOUNT_TRAY             */  s_mount_tray,		D_ADMIN,
/* R_MOUNT_TRAY             */  r_H,                    D_BADMSG,
/* S_UNMOUNT_TRAY           */  s_unmount_tray,		D_ADMIN,
/* R_UNMOUNT_TRAY           */  r_H,                    D_BADMSG,
/* S_MAX_TRAYS              */  s_max_trays,		D_ADMIN,
/* R_MAX_TRAYS              */  r_H,                    D_BADMSG,
/* S_PAPER_CHANGED          */  s_paper_changed,	0,
/* R_PAPER_CHANGED          */  0,			D_BADMSG,
/* S_PAPER_ALLOWED          */  s_paper_allowed,	0,
/* R_PAPER_ALLOWED          */  0,			D_BADMSG,
};

#ifdef DEBUG

static char *			dispatch_names[] = {
"R_BAD_MESSAGE",
"S_NEW_QUEUE",
"R_NEW_QUEUE",
"S_ALLOC_FILES",
"R_ALLOC_FILES",
"S_PRINT_REQUEST",
"R_PRINT_REQUEST",
"S_START_CHANGE_REQUEST",
"R_START_CHANGE_REQUEST",
"S_END_CHANGE_REQUEST",
"R_END_CHANGE_REQUEST",
"S_CANCEL_REQUEST",
"R_CANCEL_REQUEST",
"S_INQUIRE_REQUEST",
"R_INQUIRE_REQUEST",
"S_LOAD_PRINTER",
"R_LOAD_PRINTER",
"S_UNLOAD_PRINTER",
"R_UNLOAD_PRINTER",
"S_INQUIRE_PRINTER_STATUS",
"R_INQUIRE_PRINTER_STATUS",
"S_LOAD_CLASS",
"R_LOAD_CLASS",
"S_UNLOAD_CLASS",
"R_UNLOAD_CLASS",
"S_INQUIRE_CLASS",
"R_INQUIRE_CLASS",
"S_MOUNT",
"R_MOUNT",
"S_UNMOUNT",
"R_UNMOUNT",
"S_MOVE_REQUEST",
"R_MOVE_REQUEST",
"S_MOVE_DEST",
"R_MOVE_DEST",
"S_ACCEPT_DEST",
"R_ACCEPT_DEST",
"S_REJECT_DEST",
"R_REJECT_DEST",
"S_ENABLE_DEST",
"R_ENABLE_DEST",
"S_DISABLE_DEST",
"R_DISABLE_DEST",
"S_LOAD_FILTER_TABLE",
"R_LOAD_FILTER_TABLE",
"S_UNLOAD_FILTER_TABLE",
"R_UNLOAD_FILTER_TABLE",
"S_LOAD_PRINTWHEEL",
"R_LOAD_PRINTWHEEL",
"S_UNLOAD_PRINTWHEEL",
"R_UNLOAD_PRINTWHEEL",
"S_LOAD_USER_FILE",
"R_LOAD_USER_FILE",
"S_UNLOAD_USER_FILE",
"R_UNLOAD_USER_FILE",
"S_LOAD_FORM",
"R_LOAD_FORM",
"S_UNLOAD_FORM",
"R_UNLOAD_FORM",
"S_GETSTATUS",
"R_GETSTATUS",
"S_QUIET_ALERT",
"R_QUIET_ALERT",
"S_SEND_FAULT",
"R_SEND_FAULT",
"S_SHUTDOWN",
"R_SHUTDOWN",
"S_GOODBYE",
"S_CHILD_DONE",
"I_GET_TYPE",
"I_QUEUE_CHK",
"R_CONNECT",
"S_GET_STATUS",
"R_GET_STATUS",
"S_INQUIRE_REQUEST_RANK",
"R_INQUIRE_REQUEST_RANK",
"S_CANCEL",
"R_CANCEL",
"S_NEW_CHILD",
"R_NEW_CHILD",
"S_SEND_JOB",
"R_SEND_JOB",
"S_JOB_COMPLETED",
"R_JOB_COMPLETED",
"S_INQUIRE_REMOTE_PRINTER",
"R_INQUIRE_REMOTE_PRINTER",
"S_LOAD_SYSTEM",
"R_LOAD_SYSTEM",
"S_UNLOAD_SYSTEM",
"R_UNLOAD_SYSTEM",
"S_CLEAR_FAULT",
"R_CLEAR_FAULT",
"S_MOUNT_TRAY",
"R_MOUNT_TRAY",
"S_UNMOUNT_TRAY",
"R_UNMOUNT_TRAY",
"S_MAX_TRAYS",
"R_MAX_TRAYS",
"S_PAPER_CHANGED",
"R_PAPER_CHANGED",
"S_PAPER_ALLOWED",
"R_PAPER_ALLOWED",
};

/* see include/msgs.h */
static char *			status_names[] = {
"MOK",
"MOKMORE",
"MOKREMOTE",
"MMORERR",
"MNODEST",
"MERRDEST",
"MDENYDEST",
"MNOMEDIA",
"MDENYMEDIA",
"MNOFILTER",
"MNOINFO",
"MNOMEM",
"MNOMOUNT",
"MNOOPEN",
"MNOPERM",
"MNOSTART",
"MUNKNOWN",
"M2LATE",
"MNOSPACE",
"MBUSY",
"MTRANSMITERR",
"MNOMORE",
"MGONEREMOTE",
"MNOTRAY"
};

#define LAST_STATUS 23

/**
 ** dispatchName() - ROUTINE TO GIVE ASCII DISPATCH NAME
 **/

char *
dispatchName(int type)
{
	if (type <= 0 || type > LAST_MESSAGE)
		type = 0;
	return (dispatch_names[type]);
}

char *
statusName(int status)
{
	if (status < 0 || status > LAST_STATUS)
		return ("unknown");
	return (status_names[status]);
}

#endif DEBUG

/**
 ** dispatch() - DISPATCH A ROUTINE TO HANDLE A MESSAGE
 **/

void
dispatch(int type, char *m, MESG *md)
{
	ENTRY ("dispatch")

	register DISPATCH	*pd	= &dispatch_table[type];

	DEBUGLOG2("dispatch %s\n", dispatchName(type));

	if (type <= 0 || type > LAST_MESSAGE || pd->fncp == NULL)
		mputm (md, R_BAD_MESSAGE);

	else if (!pd->fncp || pd->flags & D_BADMSG)
		mputm (md, R_BAD_MESSAGE);

	else if (pd->flags & D_ADMIN && !md->admin)
		if ((++pd)->fncp)
			(*pd->fncp) (md, type+1);
		else
			mputm (md, R_BAD_MESSAGE);

	else if (pd->flags & D_SYSTEM && md->type != MD_CHILD &&
		md->type != MD_BOUND)
		if ((++pd)->fncp)
			(*pd->fncp) (md, type+1);
		else
			mputm (md, R_BAD_MESSAGE);

	else
		(*pd->fncp) (m, md);
}

/**
 ** r_H() - SEND MNOPERM RESPONSE MESSAGE
 ** r_HS() - SEND MNOPERM RESPONSE MESSAGE
 **/

static void
r_H(MESG *md, int type)
{
	ENTRY ("r_H")

	mputm (md, type, MNOPERM);
	return;
}

static void
r_HS(MESG *md, int type)
{
	ENTRY ("r_HS")

	mputm (md, type, MNOPERM, "");
	return;
}
