/*
 *	db_log.cc
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)db_log.cc	1.8	93/04/23 SMI"

#include <stdio.h>
/* #include <errno.h> */

#include <malloc.h>
#include <string.h>
#ifdef TDRPC
#include <sysent.h>
#endif
#include <unistd.h>

#include "db_headers.h"
#include "db_log.h"

static void
delete_log_entry(db_log_entry *lentry)
{
	db_query *q;
	entry_object *obj;
	if (lentry) {
		if ((q = lentry->get_query())) {
			delete q;
		}
		if ((obj = lentry->get_object())) {
			free_entry(obj);
		}
		delete lentry;
	}
}

/*
 * Execute given function 'func' on log.
 * function takes as arguments: pointer to log entry, character pointer to
 * another argument, and pointer to an integer, which is used as a counter.
 * 'func' should increment this value for each successful application.
 * The log is traversed until either 'func' returns FALSE, or when the log
 * is exhausted.  The second argument to 'execute_on_log' is passed as the
 * second argument to 'func'.  The third argument, 'clean' determines whether
 * the log entry is deleted after the function has been applied.
 * Returns the number of times that 'func' incremented its third argument.
 */
int
db_log::execute_on_log(bool_t (*func) (db_log_entry *, char *, int *),
			    char* arg, bool_t clean)
{
	db_log_entry    *j;
	int count = 0;
	bool_t done = FALSE;

	if (open() == FALSE) {   // open log
		return (0);
	}
	while (!done) {
		j = get();
		if (j == NULL)
			break;
		if ((*func)(j, arg, &count) == FALSE) done = TRUE;
		if (clean) delete_log_entry(j);
	}

	close();

	return (count);
}

static bool_t
print_log_entry(db_log_entry *j, char * dummy, int *count)
{
	j->print();
	++ *count;
	return (TRUE);
}

/* Print contents of log file to stdout */
int
db_log::print()
{
	return (execute_on_log(&(print_log_entry), NULL));
}

/* Make copy of current log to log pointed to by 'f'. */
db_log::copy(db_log *f)
{
	db_log_entry *j;

	for (;;) {
		j = get();
		if (j == NULL)
			break;
		if (f->append(j) < 0) {
		    WARNING_M("db_log::copy: could not append to log file: ");
		    return (-1);
		}
		delete_log_entry(j);
	}
	return (0);
}

/* Rewinds current log */
int
db_log::rewind()
{
	return (fseek(file, 0L, 0));
}

/*
 * Return the next element in current log; return NULL if end of log or error.
 * Log must have been opened for READ.
 */
db_log_entry
*db_log::get()
{
	db_log_entry *j;
	if (mode != PICKLE_READ)
		return (NULL);

	j = new db_log_entry;

	if (j == NULL) return (NULL);
	if (xdr_db_log_entry(&(xdr), j) == FALSE) {
		delete_log_entry (j);
/*    WARNING("Could not sucessfully finish reading log"); */
		return (NULL);
	}
	if (j->sane())
		return (j);
	else {
		WARNING("truncated log entry found");
		delete_log_entry(j);
		return (NULL);
	}
}

/* Append given log entry to log. */
int
db_log::append(db_log_entry *j)
{
	int status;
	if (mode != PICKLE_APPEND)
		return (-1);

	/* xdr returns TRUE if successful, FALSE otherwise */
	status = ((xdr_db_log_entry(&(xdr), j)) ? 0 : -1);
	if (status < 0) {
		WARNING("db_log: could not write log entry");
		return (status);
	}

	status = fflush(file);
	if (status < 0) {
		WARNING("db_log: could not flush log entry to disk");
		return (status);
	}

	status = fsync(fileno(file));
	if (status < 0) {
		WARNING("db_log: could not sync log entry to disk");
	}

	return (status);
}
