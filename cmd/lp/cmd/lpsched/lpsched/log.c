/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)log.c	1.11	95/07/27 SMI"	/* SVr4.0 1.1.1.6	*/

#include "stdarg.h"
#include "lpsched.h"

static void log(char *, va_list);

/**
 ** open_logfile() - OPEN FILE FOR LOGGING MESSAGE
 ** close_logfile() - CLOSE SAME
 **/

FILE *
open_logfile(char *name)
{
	register FILE		*fp;
	char			path[80];

	sprintf(path, "%s/%s", Lp_Logs, name);
	fp = fopen(path, "a");
	return (fp);
}

void
close_logfile(FILE *fp)
{
	fclose (fp);
	return;
}

/**
 ** fail() - LOG MESSAGE AND EXIT (ABORT IF DEBUGGING)
 **/

/*VARARGS1*/
void
fail(char *format, ...)
{
	va_list			ap;
    
	va_start (ap, format);
	log (format, ap);
	va_end (ap);

#if	defined(DEBUG)
	if (debug & DB_ABORT)
		abort ();
	else
#endif
		exit (1);
	/*NOTREACHED*/
}

/**
 ** note() - LOG MESSAGE
 **/

/*VARARGS1*/
void
note(char *format, ...)
{
	va_list			ap;

	va_start (ap, format);
	log (format, ap);
	va_end (ap);
}

/**
 ** schedlog() - LOG MESSAGE IF IN DEBUG MODE
 **/

/*VARARGS1*/
void
schedlog(char *format, ...)
{
	va_list			ap;

#if	defined(DEBUG)
	if (debug & DB_SCHEDLOG) {
		va_start (ap, format);
		log (format, ap);
		va_end (ap);
	}
#endif
}

/**
 ** schedtrace() - trace lpsched function calls
 **/

/*VARARGS1*/
int
schedtrace(char *format, ...)
{
	va_list			ap;

#if	defined(DEBUG)
	if (debug & DB_SCHEDLOG) {
		va_start (ap, format);
		log (format, ap);
		va_end (ap);
	}
#endif
	return (0);
}

/**
 ** mallocfail() - COMPLAIN ABOUT MEMORY ALLOCATION FAILURE
 **/

void
mallocfail(void)
{
	fail ("Memory allocation failed!\n");
	/*NOTREACHED*/
}

/**
 ** log() - LOW LEVEL ROUTINE THAT LOGS MESSSAGES
 **/

static void
log(char *format, va_list ap)
{
	int			close_it;
	FILE			*fp;
	static int		nodate	= 0;

	if (!am_in_background) {
		fp = stdout;
		close_it = 0;
	} else {
		if (!(fp = open_logfile("lpsched")))
			return;
		close_it = 1;
	}

	if (am_in_background && !nodate) {
		time_t curtime;
		struct tm *tm;

		time(&curtime);
		if ((tm = localtime(&curtime)) != NULL)
			fprintf (fp, "%.2d/%.2d %.2d:%.2d:%.2d: ", 
			 	tm->tm_mon+1, tm->tm_mday, tm->tm_hour,
				tm->tm_min, tm->tm_sec);
		else
			fprintf(fp, "bad date: ");
	}
	nodate = 0;

	vfprintf (fp, format, ap);
	if (format[strlen(format) - 1] != '\n')
		nodate = 1;

	if (close_it)
		close_logfile (fp);
	else
		fflush (fp);
}

/**
 ** execlog()
 **/

/*VARARGS1*/
void
execlog(char *format, ...)
{
	va_list			ap;

#if	defined(DEBUG)
	FILE			*fp	= open_logfile("exec");
	time_t			now = time((time_t *)0);
	char			buffer[BUFSIZ];
	EXEC *			ep;
	static int		nodate	= 0;

	va_start (ap, format);
	if (fp) {
		setbuf (fp, buffer);
		if (!nodate)
			fprintf (fp, "%24.24s: ", ctime(&now));
		nodate = 0;
		if (!STREQU(format, "%e")) {
			vfprintf (fp, format, ap);
			if (format[strlen(format) - 1] != '\n')
				nodate = 1;
		} else switch ((ep = va_arg(ap, EXEC *))->type) {
		case EX_INTERF:
			fprintf (
				fp,
				"      EX_INTERF %s %s\n",
				ep->ex.printer->printer->name,
				ep->ex.printer->request->secure->req_id
			);
			break;
		case EX_SLOWF:
			fprintf (
				fp,
				"      EX_SLOWF %s\n",
				ep->ex.request->secure->req_id
			);
			break;
		case EX_ALERT:
			fprintf (
				fp,
				"      EX_ALERT %s\n",
				ep->ex.printer->printer->name
			);
			break;
		case EX_FAULT_MESSAGE:
			fprintf (
				fp,
				"      EX_FAULT_MESSAGE %s\n",
				ep->ex.printer->printer->name
			);
			break;
		case EX_FORM_MESSAGE:
			fprintf (
				fp,
				"      EX_FORM_MESSAGE %s\n",
				ep->ex.form->form->name
			);
			break;
		case EX_FALERT:
			fprintf (
				fp,
				"      EX_FALERT %s\n",
				ep->ex.form->form->name
			);
			break;
		case EX_PALERT:
			fprintf (
				fp,
				"      EX_PALERT %s\n",
				ep->ex.pwheel->pwheel->name
			);
			break;
		case EX_NOTIFY:
			fprintf (
				fp,
				"      EX_NOTIFY %s\n",
				ep->ex.request->secure->req_id
			);
			break;
		default:
			fprintf (fp, "      EX_???\n");
			break;
		}
		close_logfile (fp);
	}
#endif
}
