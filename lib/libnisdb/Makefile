#
#ident	"@(#)Makefile	1.8	94/12/06 SMI"
#
# Copyright (c) 1993 by Sun Microsystems, Inc.
#
# lib/libnisdb/Makefile
#
LIBRARY= libnisdb.a
VERS =	.2
PROTOCOL_DIR= $(ROOT)/include/rpcsvc

OBJECTS = \
	db_entry.o db_entry_c_xdr.o \
	db_item.o db_item_c_xdr.o \
	db_vers.o db_vers_c_xdr.o \
	db_pickle.o \
	db_table.o db_table_c_xdr.o \
	db_index_entry.o db_index_entry_c.o \
	db_index.o db_index_c_xdr.o \
	db_scheme.o db_scheme_c_xdr.o \
	db_query.o db_query_c_xdr.o \
	db_mindex.o db_mindex_c_xdr.o \
	db_log_entry.o db_log_entry_c_xdr.o \
	db_log.o \
	db.o \
	xdr_nullptr.o \
	db_dictionary.o db_dictionary_c_xdr.o \
	db_dictxdr.o db_dictlog.o db_dictlog_c_xdr.o \
	nis_db.o
#
objs/%.o pics/%.o profs/%.o: %.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o profs/%.o: %.cc
	$(COMPILE.cc) -o $@ $<
	$(POST_PROCESS_O)

X_FILES= \
    db_c.x             db_dictionary_c.x db_entry_c.x  db_index_c.x     \
    db_index_entry_c.x db_item_c.x       db_log_c.x    db_log_entry_c.x \
    db_mindex_c.x      db_query_c.x      db_scheme_c.x db_table_c.x     \
    db_vers_c.x	       db_dictlog_c.x

DERIVED_HEADERS= $(X_FILES:%_c.x=%_c.h) $(X_FILES:%_c.x=%.h)
DERIVED_SOURCES= $(X_FILES:%_c.x=%_c_xdr.c)
DERIVED_FILES= $(DERIVED_HEADERS) $(DERIVED_SOURCES)

# delete the derived files when cleaning up
CLEANFILES += $(DERIVED_FILES)

#
# Pattern matching rules that define how to build the derived files.
#
%_c.h: %_c.x
	$(RM) $@
	$(RPCGEN) -DUSINGC -h -o $@ $<

%.h: %_c.x
	$(RM) $@
	$(RPCGEN) -h -o $@ $<

%_c_xdr.c: %_c.x
	$(RM) $@
	$(RPCGEN) -DUSINGC -c -o $@ $<

# include library definitions
include ../Makefile.lib

SRCS= db_dictionary_c_xdr.c db_dictlog_c_xdr.c db_dictxdr.c \
db_entry_c_xdr.c db_index_c_xdr.c db_index_entry_c_xdr.c db_item_c_xdr.c \
db_log_c_xdr.c db_log_entry_c_xdr.c db_mindex_c_xdr.c db_query_c_xdr.c \
db_scheme_c_xdr.c db_table_c_xdr.c db_vers_c_xdr.c
#db_c_xdr.c xdr_nullptr.c

LIBS += $(DYNLIB)
LDLIBS += -ldl -lc

.KEEP_STATE:

all:	$(DERIVED_FILES) $(LIBS)

install: all $(ROOTLIBS) $(ROOTLINKS)

lint: $(SRCS:.c=.ln) $(LINTLIB)

# include library targets
include ../Makefile.targ
