#
# Copyright (c) 1991 by Sun Microsystems, Inc.
#
#ident	"@(#)Makefile	1.2	95/06/14 SMI"
#
# uts/sun4u/forthdebug/forthgen/Makefile

UTSBASE = ../../..

#
#       Define the module and object file sets.
#
FORTHGEN	= $(OBJS_DIR)/forthgen
FORTHGEN_OBJS	= parser.o stabs.o
OBJECTS		= $(FORTHGEN_OBJS:%=$(OBJS_DIR)/%)
LINTS		= $(FORTHGEN_OBJS:%.o=$(LINTS_DIR)/%.ln)
SRCS_DIR        = $(UTSBASE)/sun4u/forthdebug/forthgen

#
#       Include common rules.
#
CLEANFILES	= $(OBJECTS)

FILEMODE        = 755
OWNER		= bin
GROUP		= bin

include $(UTSBASE)/sun4u/Makefile.sun4u

NATIVECFLAGS	= -c


#
#       Default build targets.
#
.KEEP_STATE:


all:	$(FORTHGEN)

install:	all

lint:	$(SRCS_DIR)/parser.c  $(SRCS_DIR)/stabs.c	
	@$(LINT) $(LINTFLAGS) $(SRCS_DIR)/parser.c $(SRCS_DIR)/stabs.c 

clean:
	$(RM) $(CLEANFILES)

clobber: clean
	$(RM) $(FORTHGEN)

clean.lint:
	$(RM) $(LINTS)


$(FORTHGEN):	$(FORTHGEN_OBJS)
	$(NATIVECC) -o $@ $(FORTHGEN_OBJS) -lm

$(OBJS_DIR)/parser.o:   $(SRCS_DIR)/parser.c
	$(NATIVECC) $(NATIVECFLAGS) $(SRCS_DIR)/parser.c -o $@
 
$(OBJS_DIR)/stabs.o:    $(SRCS_DIR)/stabs.c
	$(NATIVECC) $(NATIVECFLAGS) $(SRCS_DIR)/stabs.c -o $@
