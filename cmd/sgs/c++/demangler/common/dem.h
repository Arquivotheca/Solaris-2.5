#pragma ident	"@(#)dem.h	1.11	93/11/11 SMI"

/*******************************************************************************
 
C++ source for the C++ Language System, Release 3.0.  This product
is a new release of the original cfront developed in the computer
science research center of AT&T Bell Laboratories.

Copyright (c) 1991 AT&T and UNIX System Laboratories, Inc.
Copyright (c) 1984, 1989, 1990 AT&T.  All Rights Reserved.

THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE of AT&T and UNIX System
Laboratories, Inc.  The copyright notice above does not evidence
any actual or intended publication of such source code.

*******************************************************************************/

#ifdef DBX_SUPPORT
#ifdef __cplusplus
extern "C" {
#endif
#endif DBX_SUPPORT

typedef struct DEMARG DEMARG;
typedef struct DEMCL DEMCL;
typedef struct DEM DEM;

enum DEM_TYPE {
	DEM_NONE,		/* placeholder */
	DEM_STI,		/* static construction function */
	DEM_STD,		/* static destruction function */
	DEM_VTBL,		/* virtual table */
	DEM_PTBL,		/* ptbl vector */
	DEM_FUNC,		/* function */
	DEM_MFUNC,		/* member function */
	DEM_SMFUNC,		/* static member function */
	DEM_CMFUNC,		/* const member function */
	DEM_OMFUNC,		/* conversion operator member function */
	DEM_CTOR,		/* constructor */
	DEM_DTOR,		/* destructor */
	DEM_DATA,		/* data */
	DEM_MDATA,		/* member data */
	DEM_LOCAL,		/* local variable */
	DEM_CTYPE,		/* class type */
#ifdef DBX_SUPPORT
	DEM_ANONU,		/* anonymous union within a class */
	DEM_GLOBALANONU,	/* global anonymous union */
	DEM_VFPTR,		/* virtual function pointer */
	DEM_VBCLASS,		/* instance of a base class (maybe virtual) */
	DEM_VBCLASSPTR,		/* pointer to virtual base class */
	DEM_OPERATOR,		/* operator function */
	DEM_UNNAMED_ARG,	/* unnamed argument */
	DEM_GEN_DATA,		/* compiler generated member data */
#endif DBX_SUPPORT
	DEM_TTYPE,		/* template class type */

	DEM_TYPE_END		/* used for cafe support... */
};

struct DEMARG {
	char* mods;		/* modifiers and declarators (page 123 in */
				/* ARM), e.g. "CP" */

	long* arr;		/* dimension if mod[i] == 'A' else NULL */

	DEMARG* func;		/* list of arguments if base == 'F' */
				/* else NULL */

	DEMARG* ret;		/* return type if base == 'F' else NULL */

	DEMCL* clname;		/* class/enum name if base == "C" */

	DEMCL** mname;		/* class name if mod[i] == "M" */
				/* in argument list (pointers to members) */

	DEMARG* next;		/* next argument or NULL */

	char* lit;		/* literal value for PT arguments */
				/* e.g. "59" in A<59> */

	char base;		/* base type of argument, */
				/* 'C' for class/enum types */
};

struct DEMCL {
	char* name;		/* name of class or enum without PT args */
				/* e.g. "Vector" */

	DEMARG* clargs;		/* arguments to class, NULL if not PT */

	char* rname;		/* raw class name with __pt__ if PT */
				/* e.g. "A__pt__2_i" */

	DEMCL* next;		/* next class name, NULL if not nested */
};

struct DEM {
	enum DEM_TYPE type;	/* type of name that was demangled */
	char* f;		/* function or data name;  NULL if type name */
				/* see page 125 of ARM for predefined list */

	char* vtname;		/* if != NULL name of source file for vtbl */

	DEMARG* fargs;		/* arguments of function name if __opargs__ */
				/* else NULL */

	DEMCL* cl;		/* name of relevant class or enum or NULL */
				/* used also for type-name-only input */

	DEMARG* args;		/* args to function, NULL if data or type */


	short slev;		/* scope level for local variables or -1 */

	char sc;		/* storage class type 'S' or 'C' or: */
				/* i -> __sti   d --> __std */
				/* b -> __ptbl_vec */
};

#define MAXDBUF 8192

#ifndef DBX_SUPPORT

int demangle();
int cfront_demangle();
void dem_printarg();
void dem_printarglist();
int dem_print();
void dem_printfunc();
int dem();
void dem_printcl();
char* dem_explain();

#else 

typedef struct DEM *DEMP;
typedef void (*ERR_FCT)(char *, ...);

#ifdef __STDC__
extern int		demangle		(char *, char *);
extern int		cfront_demangle		(char *, char *);
extern void		dbx_dem_init		(ERR_FCT);
extern DEMP		dbx_demangle		(char *, char);
extern enum DEM_TYPE	dem_getfieldtype	(DEMP);
extern char		*dem_getclass		(DEMP);
extern char		*dem_getname		(DEMP);
extern char		**dem_getparentclass	(DEMP);
extern char		*dem_gettemplatename	(DEMP);
extern int		is_cafe_symbol		(char *);

#else

extern int		demangle		(/* char *, char * */);
extern int		cfront_demangle		(/* char *, char * */);
extern void		dbx_dem_init		(/* ERR_FCT */);
extern DEMP		dbx_demangle		(/* char *, char */);
extern enum DEM_TYPE	dem_getfieldtype	(/* DEMP */);
extern char		*dem_getclass		(/* DEMP */);
extern char		*dem_getname		(/* DEMP */);
extern char		**dem_getparentclass	(/* DEMP */);
extern char		*dem_gettemplatename	(/* DEMP */);
extern int		is_cafe_symbol		(/* char * */);

#endif __STDC__

#ifdef __cplusplus
}
#endif
#endif DBX_SUPPORT
