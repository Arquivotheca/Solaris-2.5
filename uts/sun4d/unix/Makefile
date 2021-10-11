#
# Copyright (c) 1991 by Sun Microsystems, Inc.
#

#ident	"@(#)Makefile	1.21	95/01/25 SMI"

#
#	This makefile drives the production of /unix (and unix.o).
#
#	sun4d implementation architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..

#
#	Define the module and object file sets.
#
UNIX		= unix

OBJECTS		= $(SPECIAL_OBJS:%=$(OBJS_DIR)/%) \
		  $(CORE_OBJS:%=$(OBJS_DIR)/%) \
		  $(MACH_NOT_YET_KMODS:%=$(OBJS_DIR)/%)

LINTS		= $(SPECIAL_OBJS:%.o=$(LINTS_DIR)/%.ln) \
		  $(CORE_OBJS:%.o=$(LINTS_DIR)/%.ln) \
		  $(MACH_NOT_YET_KMODS:%.o=$(LINTS_DIR)/%.ln) \
		  $(LINTS_DIR)/vers.ln \
		  $(LINTS_DIR)/modstubs.ln

CLEANFILES	= $(OBJS_DIR)/$(UNIX).o $(MODSTUBS_O) $(OBJS_DIR)/vers.c \
		  $(OBJS_DIR)/vers.o $(OBJS_DIR)/hwbcopy.s

CLOBBERFILES	= $(UNIX)

ROOTMODULE	= $(ROOT_PSM_MOD_DIR)/$(UNIX)

KRTLD		= misc/krtld

GENUNIX		= genunix
GENUNIX_DIR	= ../../sparc/$(GENUNIX)

LIBOPTS		= -L $(GENUNIX_DIR) -l $(GENUNIX)
#
#	Include common rules.
#
include $(UTSBASE)/sun4d/Makefile.sun4d

#
#	This is UNIX_DIR. Use a short path.
#
UNIX_DIR	= .

#
#	Additional special clean.lint target
#
CLEANLINTFILES	= $(LINT_LIB)

#
# lint pass one enforcement
#
CFLAGS += -v

#
#	Default build targets.
#
.KEEP_STATE:

.PARALLEL:	$(OBJECTS)

all:		$(UNIX)

$(UNIX):	$(UNIX).o $(MODSTUBS_O) $(MAPFILE) $(GENLIB)
	${LD} -dy -b -o $@ -e _start -I $(KRTLD) -M $(MAPFILE) \
	$(UNIX).o $(MODSTUBS_O) $(LIBOPTS)
	$(POST_PROCESS)

$(UNIX).o:	$(OBJECTS) $(OBJS_DIR)/vers.o
	${LD} -r -o $@ $(OBJECTS) $(OBJS_DIR)/vers.o

$(GENLIB): FRC
	@(cd $(GENLIB_DIR); pwd; $(MAKE) $(TARGET))
	@pwd

#
#	The global lint target builds the kernel lint library (llib-lunix.ln)
#	which is equivalent to a lint of /unix.o. Then all kernel modules for
#	this architecture are linted against the kernel lint library.
#
#	Note:	lint errors in the kernel lint library will be repeated for
#		each module. It is important that the kernel lint library
#		be clean to keep the textual output to a reasonable level.
#
lint lintlib:	$(LINT_LIB)

$(LINT_LIB):	$(LINT_LIB_DIR) $(LINTS)
	@-$(ECHO) "\n$(UNIX): (library construction):"
	@$(LINT) -o$(UNIX) -xum $(LINTFLAGS) $(LINTS)
	@$(MV) $(@F) $@

#
#	Installation target.
#
install:  	all $(ROOTMODULE)

#
#	Include common targets.
#
include $(UTSBASE)/sun4d/Makefile.targ

FRC:
