/*
 *	db_log_c.x
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

%#pragma ident	"@(#)db_log_c.x	1.3	92/07/14 SMI"

#if RPC_HDR
%#ifndef _DB_LOG_H
%#define _DB_LOG_H

#ifdef USINGC
%#include "db_log_entry_c.h"
#else
%#include "db_pickle.h"
%#include "db_log_entry.h"
#endif USINGC
#endif RPC_HDR

#ifndef USINGC
#ifdef RPC_HDR
%class db_log: public pickle_file {
% public:
%
%/* Constructor:  create log file; default is PICKLE_READ mode. */
%  db_log( char* f, pickle_mode m = PICKLE_READ ): pickle_file(f,m)  {}
%
%/* Execute given function 'func' on log.
%  function takes as arguments: pointer to log entry, character pointer to 
%  another argument, and pointer to an integer, which is used as a counter.
%  'func' should increment this value for each successful application.
%  The log is traversed until either 'func' returns FALSE, or when the log
%  is exhausted.  The second argument to 'execute_on_log' is passed as the
%  second argument to 'func'. The third argument, 'clean' determines whether
%  the log entry is deleted after the function has been applied.
%  Returns the number of times that 'func' incremented its third argument. */
%  int execute_on_log( bool_t(* f) (db_log_entry *, char *, int *), 
%		      char *, bool_t = TRUE );
%
%/* Print contents of log file to stdout */
%  print();
%
%/* Make copy of current log to log pointed to by 'f'. */  
%  copy( db_log*);
%
%/*Rewinds current log */
%  rewind();
%
%/*Append given log entry to log. */
%  append( db_log_entry * );
%
%/* Return the next element in current log; return NULL if end of log or error.
%   Log must have been opened for READ. */
%  db_log_entry *get();
%
%/*  bool_t dump( pptr ) {return TRUE;}*/     // does nothing.
%};
#endif RPC_HDR
#endif USINGC

#if RPC_HDR
%#endif _DB_LOG_H
#endif RPC_HDR
