#
# uts/sun4c/fd/Makefile
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.8	94/03/07 SMI"
#
#	This makefile drives the production of the fd driver kernel module.
#
#	sun4c implementation architecture dependent
#

#
#	Path to the base of the uts directory tree (usually /usr/src/uts).
#
UTSBASE	= ../..

#
#	Define the module and object file sets.
#
MODULE		= fd
OBJECTS		= $(FD_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(FD_OBJS:%.o=$(LINTS_DIR)/%.ln)
ROOTMODULE	= $(ROOT_PSM_DRV_DIR)/$(MODULE)

#
#	Include common rules.
#
include $(UTSBASE)/sun4c/Makefile.sun4c

#
# Overrides
#
MODSTUBS_DIR	= $(OBJS_DIR)
$(MODSTUBS_O)	:= AS_CPPFLAGS += -DFD_MODULE
CLOBBERFILES    += $(MODULE) fd_genassym
$(OBJS_DIR)/fd_asm.o := AS_CPPFLAGS += -I.
CLEANFILES      += $(MODSTUBS_O) fd_assym.h

#
#	Default build targets.
#
.KEEP_STATE:

all:		$(MODULE)

lint:		$(MODULE).lint


install:  	$(MODULE) $(ROOTMODULE)

$(MODULE):      fd_assym.h

#
#	Include common targets.
#
include $(UTSBASE)/sun4c/Makefile.targ

#
#       Assym targets
#
fd_genassym:    $(FDGENASSYM_SRC)
	$(NATIVECC) $(ALWAYS_DEFS) $(NATIVE_INC_PATH) -o fd_genassym \
	$(FDGENASSYM_SRC)
 
fd_assym.h:     fd_genassym
	./fd_genassym > ./fd_assym.h

