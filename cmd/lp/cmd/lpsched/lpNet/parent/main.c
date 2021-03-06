/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/


#ident	"@(#)main.c	1.4	93/11/19 SMI"	/* SVr4.0 1.3	*/

/*=================================================================*/
/*
*/
#include	"lpNet.h"
#include	"boolean.h"
#include	"errorMgmt.h"
#include	"debug.h"

/*----------------------------------------------------------*/
/*
**	Global variables.
*/
	processInfo	ProcessInfo;

/*=================================================================*/

/*=================================================================*/
/*
*/
int
main (argc, argv)

int	argc;
char	*argv [];
{
	/*----------------------------------------------------------*/
	/*
	*/
		boolean	done;
	static	char	FnName []	= "main";

	/*----------------------------------------------------------*/
	/*
	*/
	lpNetInit (argc, argv);

	done = False;

	while (! done)
		switch (ProcessInfo.processType) {
		case	ParentProcess:
			lpNetParent ();
			break;

		case	ChildProcess:
			lpNetChild ();
			break;

		default:
			TrapError (Fatal, Internal, FnName,
			"Unknown processType.");
		}

	return	0;	/*  Never reached.	*/
}
/*==================================================================*/

/*==================================================================*/
/*
*/
void
Exit (exitCode)

int	exitCode;
{
	if (exitCode != 0)
		WriteLogMsg ("Abnormal process termination.");
#if defined(DEBUG)
	else
		WriteLogMsg ("Normal process termination.");
#endif

	exit (exitCode);
}
/*==================================================================*/
