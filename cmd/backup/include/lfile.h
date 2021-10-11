/*	@(#)lfile.h 1.0 90/11/14 SMI	*/

/*	@(#)lfile.h 1.2 93/04/28	*/

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

/*
 * The tape label file or "L file" is the communications path between
 * dump and dumpex for tape status information
 *
 * Here's a sample L file (all text begins at column 1)
 *
 *	----LFILE----
 *	libname
 *	N-00000
 *	N-00001
 *	N-00002
 *
 * The first line contains a security string which both dump and
 * dumpex expect to see when they open the file.
 *
 * The second line contains the tape library name.  The full name of
 * each tape then becomes like libname:00003.  Because the label must
 * fit in the dump header "c_label" field (see <protocols/dumprestore.h>),
 * the full name is limited to 16 characters (LBLSIZE).
 *
 * Subsequent lines look like this:
 *
 *	N-00000
 *	^^^
 *	|||
 *	||tape id number
 *	|used flag:  - = untouched, + = touched this tape
 *	status flag:  N-new P-Partial E-Error F-Full U-new/unlabelled
 *
 * Dump uses the status flag to know on which tapes to write and by
 * dumpex to know which tapes are errored out.
 *
 * Dump reads in the file and updates both the status and the used
 * flags as it writes each volume.  It leaves the first two lines
 * intact and changes subsequent lines.  For each new tape it uses
 * that is not pre-specified in the file, dump adds a new line to
 * the end (after prompting the operator for the new name).
 *
 * NB:  You need to update the `used flag' whenever you write to a
 * tape and you need to set and interpret status that changes from
 * {NP} -> P -> F (or -> E).
 *
 * One line is special (when returned from dump):
 *
 *	N+00001 5
 *
 * This tells the executor the tape position of the NEXT dump to be done
 * on the tape (which is the first remaining writeable tape).  The first
 * file on a tape is file 1.
 *
 * Dump will never write on a tape marked as Errored or Full.  Although
 * technically dump need only update the file on exit, it modifies the
 * on-disk copy whenever it makes a status change (just in case).
 *
 */

/*
 * Constants used in the L-file
 */
#define	LF_HEADER	"----LFILE----\n"
#define	LF_MAXIDLEN	5	/* ID numbers can be in the range 0-99999 */
#define	LF_LIBSEP	':'	/* separates library name from vol number */

#define	LF_USED		'+'	/* dump wrote on tape */
#define	LF_NOTUSED	'-'	/* dump has not written on tape */

#define	LF_UNLABELD	'U'	/* unlabelled tape, no data on it */
#define	LF_NEWLABELD	'N'	/* labelled tape, no user data on it */
#define	LF_PARTIAL	'P'	/* partially filled tape */
#define	LF_FULL		'F'	/* completely filled tape */
#define	LF_ERRORED	'E'	/* errored tape -- no futher writes allowed */
