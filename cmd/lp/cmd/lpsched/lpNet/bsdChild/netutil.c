/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)netutil.c	1.14	93/11/18 SMI"	/* SVr4.0 1.5	*/

#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <tiuser.h>
#include <setjmp.h>
#include <signal.h>
#include "lp.h"
#include "lpNet.h"
#include "lpd.h"
#include "debug.h"

extern char _sys_errs[];

static	jmp_buf	 Env;
static	FILE	*fpRemote;
static	short	TLI_State_Errors = 0;


void Alarm_Handler(int sig)
{
	logit( LOG_INFO | LOG_DEBUG, "remote timeout");
}

/*
 * Used to establish outgoing connections.
 * (doesn't need stderr)
 */
openRemote(void)
{
	int	ev;
	int backoff = 3, cnt = 0;

	DEFINE_FNNAME ("openRemote")
	ENTRYP

	if (CONNECTED_TO_REMOTE) {
		logit(LOG_WARNING, "%s already connected to %s", Name, Rhost);
		if ((ev = t_look(CIP->fd)) < 0 ||
		     ev & (T_ERROR|T_DISCONNECT|T_ORDREL)) {
			DisconnectSystem(CIP);
			FreeConnectionInfo(&CIP);
		}
	}
 	while (!CONNECTED_TO_REMOTE) {
		int sleepTime = 0;

		logit(LOG_DEBUG, "Attempting to connect");
	
 		if (CIP = ConnectToSystem(SIP)) {
 				break;
 		}

 		if (!SIP->timeout || SIP->retry < 0 || cnt++ >= 5) {
			logit(LOG_INFO, "connection failed");
  			return(0);
  		}
		if (SIP->retry == 0)
			sleepTime = backoff;
		else
			sleepTime = SIP->retry * backoff;
		
		backoff *= 2;
		if (backoff > 60)
			backoff = 60;
	
 		logit(LOG_INFO, "%s retrying (%d) connection to %s in %d secs",
			Name, cnt, Rhost, sleepTime);
 		if (sleepTime) 
 			(void)sleep(sleepTime);
  	}
 	logit(LOG_DEBUG, "%s connected to %s", Name, Rhost); 
	signal(SIGALRM, Alarm_Handler);
  	return(1);
}
/*
 * Close remote connection and free resources
 */
void
closeRemote(void)
{
	if (CONNECTED_TO_REMOTE) {
		logit(LOG_DEBUG, "%s disconnecting from %s", Name, Rhost); 
		DisconnectSystem(CIP);
		FreeConnectionInfo(&CIP);
		if (fpRemote) {
			(void)fclose(fpRemote);
			fpRemote = NULL;
		}
	}
}

/* 
 * TLIXxxxx() functions were added to solve problems with using the
 *  "tirdwr" streams module on top of the TLI connection.  All refernces
 *  to read(2), write(2)  and fgets() for a TLI connection now use TLIRead(),
 *  TLIWrite() and TLIGets() instead, and the streams module is no longer
 *  pushed on the stream.  -BUG 1133272
 */

/*
 * Return the event string for a t_getstate() (not reentrant due to
 *                                             default message)
 */
static char *
TLIState(int state)
{
	static char buf[64];

	switch (state) {
	case T_UNINIT:
		return("uninitialized");
	case T_UNBND:
		return("initialized but unbound");
	case T_IDLE:
		return("bound, but idle");
	case T_OUTCON:
		return("outgoing connection pending for server");
	case T_INCON:
		return("incoming connection pending for server");
	case T_DATAXFER:
		return("data transfer");
	case T_OUTREL:
		return("outgoing orderly release");
	case T_INREL:
		return("incoming orderly release");
	case T_FAKE:
		return("fake, indetermintate");
	case	-1:
		if (t_errno == TSYSERR)
			sprintf(buf, "s_error: %s", _sys_errs[errno]);
		else
			sprintf(buf, "t_error: %s", t_errlist[t_errno]);
		return(buf);
	default:
		sprintf(buf, "unknown state: %d", state);
		return(buf);
	}
}

/*
 * Return the event string for a t_look event (not reentrant due to
 *                                             default message)
 */
static char *
TLIEvent(int ev)
{
	static char buf[64];

	switch (ev) {
	case T_CONNECT:
		return("Connection Confirmation Received");
	case T_DATA:
		return("normal data received");
	case T_DISCONNECT:
		return("disconnect received");
	case T_ERROR:
		return("fatal error indication");
	case T_EXDATA:
		return("expedited data received");
	case T_LISTEN:
		return("connection indcation received");
	case T_ORDREL:
		return("orderly release indication");
	case T_UDERR:
		return("datagram error indication");
	case	-1:
		if (t_errno == TSYSERR)
			sprintf(buf, "s_error: %s", _sys_errs[errno]);
		else
			sprintf(buf, "t_error: %s", t_errlist[t_errno]);
		return(buf);
	default:
		sprintf(buf, "unknown event: %d", ev);
		return(buf);
	}
}

/*
 *  Emulate read(2) for a TLI connection using t_rcv()
 */
int
TLIRead(int fd, char *buf, int bufsize)
{
	int flags=0;
	int rc=0;

/* #if defined(DEBUG)	
	logit(LOG_DEBUG, "Called: TLIRead(%d, %x, %d)", fd, buf, bufsize);
#endif /* DEBUG */
	alarm(SIP->retry*12);
	if ((rc = t_rcv(fd, buf, bufsize, &flags)) < 0) {
		alarm(0);
		switch (t_errno) {
		case TSYSERR:
			logit(LOG_ERR, "TLIRead(t_rcv()) Error: %s",
				 PERROR);
			break;
		case TLOOK:
			if ((rc = t_look(CIP->fd)) != T_ORDREL) {
				logit(LOG_ERR,
				      "TLIRead(t_rcv()) Error: Async Event(%s)",
			  	      TLIEvent(rc));
				rc = -1;
			} else
				rc = 0;
			break;
		case TSTATECHNG:
			t_sync(fd);
			logit(LOG_ERR,
				"TLIRead(t_rvc()) Warning: %s (%s)\n",
				t_errlist[t_errno], TLIState(t_getstate(fd)));
			if (TLI_State_Errors++ >= 5) {
				logit(LOG_ERR, "TLIRead(t_rvc()) failed\n");
				errno = EIO;
				return(-1);
			}
			sleep(1);
			rc = TLIRead(fd, buf, bufsize);
			break;
		default:
			logit(LOG_ERR, "TLIRead(t_rvc()) Error: %s\n",
				t_errlist[t_errno]);
		}
	errno = EIO;
	}
	alarm(0);
	if (rc != -1) errno=0;
/* #if defined(DEBUG)
	logit(LOG_DEBUG, "Return: TLIRead(): %d", rc);
#endif /* DEBUG */

	TLI_State_Errors = 0;
	return(rc);
}

/*
 * Emulate write(2) for a TLI connection using t_snd()
 */
int
TLIWrite(int fd, char *buf, int bufsize)
{
	int rc=0;
	int look_val = 0;
	
#if defined(DEBUG)
	logit(LOG_DEBUG, "Called: TLIWrite(%d, %x, %d)", fd, buf, bufsize);
#endif /* DEBUG */
	alarm(SIP->retry*12);
	if ((rc = t_snd(fd, buf, bufsize, NULL)) < 0) {
		alarm(0);
		switch (t_errno) {
		case TSYSERR:
			logit(LOG_ERR, "TLIWrite(t_snd()) Error: %s",
				PERROR);
			break;
		case TLOOK:
			look_val = t_look(CIP->fd);
			logit(LOG_ERR,
				"TLIWrite(t_snd()) Error: Async Event(%s)",
			  	TLIEvent(look_val));
			break;
		case TSTATECHNG:
			t_sync(fd);
			logit(LOG_ERR,
				"TLIWrite(t_snd()) Warning: %s (%s)\n",
				t_errlist[t_errno], TLIState(t_getstate(fd)));
			if (TLI_State_Errors++ >= 5) {
				logit(LOG_ERR, "TLIWrite(t_snd()) failed\n");
				errno = EIO;
				return(-1);
			}
			sleep(1);
			rc = TLIWrite(fd, buf, bufsize);
			break;
		default:
			logit(LOG_ERR, "TLIWrite(t_snd()) Error: %s\n",
				t_errlist[t_errno]);
		}
		errno = EIO;
	} else {
		alarm(0);
		/* because we don't get errors on disconnect, etc... */
		look_val = t_look(fd);
		if (look_val & (T_DISCONNECT | T_ORDREL)) {
			logit(LOG_ERR, "TLIWrite(connection) : %s",
				TLIEvent(look_val));
			rc = -1;
		}
	}


#if defined(DEBUG)
	logit(LOG_DEBUG, "Return: TLIWrite(): %d : %s", rc, 
		(look_val ? TLIEvent(look_val) : ""));
#endif /* DEBUG */
	TLI_State_Errors = 0;
	return(rc);
}

/*
 * Emulate fgets(3) for TLI connection using TLIRead/t_rcv
 */
char *
TLIGets(char *buf, int bufsize, int fd)
{
	char tmp;
	int count = 0;
	int rc = 0;

#if defined(DEBUG)
	logit(LOG_DEBUG, "Called: TLIGets(%x, %d, %d)", buf, bufsize, fd);
#endif /* DEBUG */

	memset(buf, 0 , bufsize);
	while ((count < bufsize) && ((rc = TLIRead(fd, &tmp, 1)) > 0))
		if ((buf[count++] = tmp) == '\n') break;

#if defined(DEBUG)
	logit(LOG_DEBUG, "Return: TLIGets(%s)", buf);
#endif /* DEBUG */

	if (count) return(buf);
	return(NULL);
}

/*
 * Read line delimited by newline from network connection
 */
char *
getNets(char *buf, int bufsize)
{
	return(TLIGets(buf, bufsize, CIP->fd));
}

/*
 * Send lpd message to remote
 */
/*VARARGS1*/
snd_lpd_msg(int type, ...)
{
	va_list	 argp;
	char	*printer;
	char	*fname;
	char	*person;
	char	*users;
	char	*jobs;
	size_t	 size;
	int	 n;

	va_start(argp, type);

	switch(type) {

	case PRINTJOB:
	case RECVJOB:
		n = sprintf(Buf, "%c%s\n", type, va_arg(argp, char *));
		break;

	case DISPLAYQS:
	case DISPLAYQL:
		printer = va_arg(argp, char *);
		users = va_arg(argp, char *);
		jobs = va_arg(argp, char *);
		n = sprintf(Buf, "%c%s %s %s\n", type, printer, users, jobs);
		break;

	case RMJOB:
		printer = va_arg(argp, char *);
		person = va_arg(argp, char *);
		users = va_arg(argp, char *);
		jobs = va_arg(argp, char *);
		n = sprintf(Buf, "%c%s %s %s %s\n", 
				type, printer, person, users, jobs);
		break;

	case RECVJOB_2NDARY:
		type = va_arg(argp, int);

		switch(type) {

		case CLEANUP:
			n = sprintf(Buf, "%c\n", type);
			break;

		case READCFILE:
		case READDFILE:
			size = va_arg(argp, size_t);
			fname = va_arg(argp, char *);
			n = sprintf(Buf, "%c%lu %s\n", type, size, fname);
			break;

		default:
			break;
		}
		break;

	default:
		break;
	}
	va_end(argp);
	logit(LOG_DEBUG, "sending %d byte lpd message: %d%s", n, *Buf, Buf+1);
	if (TLIWrite(CIP->fd, Buf, n) != n) {
		logit(LOG_INFO, "lost connection");
		return(0);
	}
	return(1);
}
