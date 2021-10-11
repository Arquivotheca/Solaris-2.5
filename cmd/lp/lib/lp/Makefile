#
# ident	"@(#)Makefile	1.9	94/08/30 SMI"	/* SVr4.0 1.13	*/
#
# Copyright (c) 1990 by Sun Microsystems, Inc.
#
# cmd/lp/lib/lp/Makefile
#

LIBRARY	=	liblp.a

OBJECTS =	Syscalls.o	\
		Sys_malloc.o	\
		addlist.o	\
		addstring.o	\
		appendlist.o	\
		alerts.o	\
		charset.o	\
		cs_strcmp.o	\
		cs_strncmp.o	\
		dellist.o	\
		dashos.o	\
		debug.o		\
		dirs.o		\
		duplist.o	\
		files.o		\
		freelist.o	\
		getlist.o	\
		getname.o	\
		getpaths.o	\
		getspooldir.o	\
		isterminfo.o	\
		joinlist.o	\
		lenlist.o	\
		lp_errno.o	\
		makepath.o	\
		makestr.o	\
		mergelist.o	\
		next.o		\
		printlist.o	\
		sdn.o		\
		sprintlist.o	\
		searchlist.o	\
		set_charset.o	\
		set_pitch.o	\
		set_size.o	\
		sop.o		\
		strip.o		\
		syntax.o	\
		tidbit.o	\
		tinames.o	\
		wherelist.o	\
		which.o


include		../../../../lib/Makefile.lib
include         ../../Makefile.lp

CPPFLAGS =	-I../../include $(CPPFLAGS.master)
# for messaging catalog file
#
POFILE= lp.po
POFILES= $(OBJECTS:%.o=%.po)
XGETFLAGS +=-a -x $(LPROOT)/lp.xcl

.KEEP_STATE:
catalog:        $(POFILE)

$(POFILE):      $(POFILES)
	$(RM)	$@
	cat     $(POFILES)      > $@

all install :	$(LIBS)

strip lint :

include		../../../../lib/Makefile.targ
