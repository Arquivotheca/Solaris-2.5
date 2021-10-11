/*
 * Internal definitions for the myrcmd.c rcmd(3) replacement module.
 *	@(#)myrcmd.h 1.1 92/09/22
 */

/* Failure return values */
#define	MYRCMD_EBAD		-1
#define	MYRCMD_NOHOST		-2
#define	MYRCMD_ENOPORT		-3
#define	MYRCMD_ENOSOCK		-4
#define	MYRCMD_ENOCONNECT	-5

/*
 * On a failure, the output that would have normally gone to stderr is
 * now placed in the global string "myrcmd_stderr".  Callers should check
 * to see if there is anything in the string before trying to print it.
 */
extern char myrcmd_stderr[];
