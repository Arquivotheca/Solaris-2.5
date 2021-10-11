#
#ident	"@(#)Makefile	1.26	95/02/10 SMI"
#
# Copyright (c) 1995 by Sun Microsystems, Inc.
# All rights reserved.
#
# lib/libcurses/screen/Makefile
#
LIBRARY=	libcurses.a

OBJECTS=	$(OBJS1) $(OBJS2) $(OBJS3) $(OBJS4) $(OBJS5) $(OBJS6)

OBJS1=	_addch.o _addchnstr.o _addchstr.o _addnstr.o _addstr.o \
	_attroff.o _attron.o _attrset.o _beep.o _bkgd.o \
	_bkgdset.o _border.o _box.o _clear.o _clrtobot.o \
	_clrtoeol.o _crmode.o _del_curterm.o _delch.o _deleteln.o \
	_echo.o _echochar.o _erase.o _fixterm.o _flash.o \
	_garbagdlins.o _garbagedwin.o _getch.o _getstr.o \
	_halfdelay.o _hline.o _inch.o _inchnstr.o _inchstr.o \
	_innstr.o _insch.o _insdelln.o _insertln.o _insnstr.o \
	_insstr.o _instr.o _intrflush.o _meta.o _move.o \
	_mvaddch.o _mvaddchnstr.o _mvaddchstr.o _mvaddnstr.o \
	_mvaddstr.o _mvdelch.o _mvgetch.o _mvgetstr.o _mvhline.o \
	_mvinch.o _mvinchnstr.o _mvinchstr.o _mvinnstr.o \
	_mvinsch.o _mvinsnstr.o _mvinsstr.o _mvinstr.o _mvvline.o \
	_mvwaddch.o _mvwaddchnst.o _mvwaddchstr.o _mvwaddnstr.o \
	_mvwaddstr.o _mvwdelch.o _mvwgetch.o _mvwgetstr.o \
	_mvwhline.o _mvwinch.o _mvwinchnst.o _mvwinchstr.o \
	_mvwinnstr.o _mvwinsch.o _mvwinsnstr.o _mvwinsstr.o \
	_mvwinstr.o _mvwvline.o _newterm.o _nl.o _nocrmode.o \
	_noecho.o _nonl.o _noqiflush.o _overlay.o _overwrite.o \
	_qiflush.o _refresh.o _resetterm.o _saveterm.o \
	_scr_init.o _scr_restore.o _scr_set.o _scrl.o _scroll.o \
	_set_curterm.o _set_term.o _setscrreg.o _slk_init.o \
	_standend.o _standout.o _subpad.o _timeout.o _touchline.o \
	_unctrl.o _vline.o _waddchstr.o _waddstr.o _wclear.o \
	_wdeleteln.o _werase.o _winsertln.o _winsstr.o \
	_wstandend.o _wstandout.o V2.__sscans.o V2._sprintw.o \
	V2.makenew.o V3.box.o V3.initscr.o V3.m_addch.o V3.m_addstr.o \
	V3.m_clear.o V3.m_erase.o V3.m_initscr.o V3.m_move.o V3.m_newterm.o \
	V3.m_refresh.o V3.newterm.o V3.pechochar.o V3.upd_old_y.o \
	V3.vidattr.o V3.vidputs.o V3.waddch.o V3.wattroff.o V3.wattron.o \
	V3.wattrset.o V3.wechochar.o V3.winsch.o baudrate.o can_change.o \
	color_cont.o cbreak.o chkinput.o clearok.o copywin.o curs_set.o \
	curserr.o curses.o def_prog.o delay.o delay_out.o \
	delkey.o delkeymap.o delscreen.o delterm.o delwin.o \
	derwin.o doupdate.o draino.o dupwin.o endwin.o erasechar.o \
	flushinp.o getattrs.o getbegyx.o getbkgd.o getmaxyx.o \
	getparyx.o getsyx.o gettmode.o getwin.o getyx.o has_colors.o \
	has_ic.o has_il.o idcok.o idlok.o immedok.o init_acs.o init_color.o \
	init_costs.o init_pair.o initscr.o is_linetou.o is_wintou.o \
	keyname.o keypad.o killchar.o leaveok.o

OBJS2=	longname.o makenew.o memSset.o meta.o mvcur.o \
	mvderwin.o mvprintw.o mvscanw.o mvwin.o mvwprintw.o \
	mvwscanw.o napms.o newkey.o newpad.o newscreen.o \
	newwin.o nocbreak.o nodelay.o noraw.o \
	notimeout.o outch.o overlap.o pechochar.o pnoutref.o \
	prefresh.o printw.o putp.o putwin.o quick_echo.o \
	raw.o redrawwin.o reset_sh.o resetty.o restart.o \
	ring.o ripoffline.o savetty.o scanw.o scr_all.o \
	scr_dump.o scr_ll_dump.o scr_reset.o scrollok.o setcurscreen.o \
	setcurterm.o setecho.o setkeymap.o setnonl.o setqiflush.o \
	setsyx.o setterm.o setupterm.o slk_atroff.o slk_atron.o \
	slk_atrset.o slk_clear.o slk_label.o \
	slk_noutref.o slk_refresh.o slk_restore.o slk_set.o slk_start.o \
	slk_touch.o subwin.o syncok.o tcsearch.o termattrs.o \
	termcap.o termerr.o termname.o tgetch.o tgoto.o \
	tifget.o tifnames.o tiget.o tinames.o tinputfd.o \
	tnames.o touchwin.o tparm.o tputs.o trace.o \
	tstp.o ttimeout.o typeahead.o unctrl.o ungetch.o \
	untouchwin.o vidputs.o vidupdate.o vsscanf.o vwprintw.o \
	vwscanw.o waddch.o waddchnstr.o waddnstr.o wattroff.o \
	wattron.o wattrset.o wbkgd.o wbkgdset.o wborder.o \
	wclrtobot.o wclrtoeol.o wdelch.o wechochar.o wgetch.o \
	wgetstr.o whline.o winch.o winchnstr.o winchstr.o \
	winnstr.o winsch.o winsdelln.o winsnstr.o winstr.o \
	wmove.o wnoutref.o wprintw.o wredrawln.o wrefresh.o \
	wscanw.o wscrl.o wsetscrreg.o wsyncdown.o wsyncup.o \
	wtimeout.o wtouchln.o wvline.o pair_cont.o start_col.o \
	mouse.o

OBJS3=	mbaddch.o mbcharlen.o mbdisplen.o mbgetwidth.o \
	mbinch.o mbinsshift.o mbmove.o mbtranslate.o \
	pechowchar.o tgetwch.o ungetwch.o waddnwstr.o \
	waddwch.o waddwchnstr.o wechowchar.o wgetwstr.o \
	wgetwch.o winnwstr.o winsnwstr.o winswch.o \
	winwch.o winwchnstr.o winwstr.o \
	use_env.o

OBJS4=	_addnwstr.o _addwch.o _addwchnstr.o _addwchstr.o \
	_addwstr.o _echowchar.o _getnwstr.o _getwch.o \
	_getwstr.o _innwstr.o _insnwstr.o _inswch.o \
	_inswstr.o _inwch.o _inwchnstr.o _inwchstr.o \
	_inwstr.o _mvaddnwstr.o _mvaddwch.o _mvaddwchnstr.o \
	_mvaddwchstr.o _mvaddwstr.o _mvgetnwstr.o _mvgetwch.o \
	_mvgetwstr.o _mvinnwstr.o _mvinsnwstr.o _mvinswch.o \
	_mvinswstr.o _mvinwch.o _mvinwchnstr.o _mvinwchstr.o \
	_mvinwstr.o _mvwaddnwstr.o _mvwaddwch.o _mvwaddwchnstr.o \
	_mvwaddwchstr.o _mvwaddwstr.o _mvwgetnwstr.o _mvwgetwch.o \
	_mvwgetwstr.o _mvwinnwstr.o _mvwinsnwstr.o _mvwinswch.o \
	_mvwinswstr.o _mvwinwch.o _mvwinwchnstr.o _mvwinwchstr.o \
	_mvwinwstr.o _waddwchstr.o _waddwstr.o _winswstr.o \
	_winwchstr.o

OBJS5=	mbstowcs.o mbtowc.o wcstombs.o wctomb.o

OBJS6=	wmovenextch.o wmoveprevch.o wadjcurspos.o

# include global definitions for libraries
include ../../Makefile.lib

#LIBS += $(LINTLIB)
LOCALTMP=	./tmp
ED=	ed
CP=	cp

CPPFLAGS=	-I. $(CPPFLAGS.master)

$(LINTLIB):=	CPPFLAGS += -DNOMACROS
LINTFLAGS=	-vx
ROOTLIBDIR=	$(ROOT)/usr/ccs/lib
ROOTLINKS=	$(ROOTLIBDIR)/libtermcap.a  $(ROOTLIBDIR)/libtermlib.a

# TXTS allows the AT&T makefile to be bootstrapped into the NSE.
TXTS=	copyright.h

# definitions for install_h target
HDRS=		term.h curses.h unctrl.h
ROOTHDRDIR=	$(ROOT)/usr/include
ROOTHDRS=	$(HDRS:%=$(ROOTHDRDIR)/%)

# install rule for install_h target
$(ROOTHDRDIR)/%: %
	$(INS.file)

# definitions for tools
TOOLS=		tic tput captoinfo infocmp
$(TOOLS) :=	LDFLAGS += -s
tput captoinfo infocmp :=	LDLIBS += $(LIBRARY)
tput := LDLIBS += -lintl
tput := CFLAGS += -v

# NOTE WELL!  The following lines are CRITICAL, when messaging a command.
# Normally, TEXT_DOMAIN=SUNW_OST_OSLIB.  However, the fact that we've got
# some commands mixed up with this library screws things up immensely.
# It would be MUCH easier if the $(TOOLS) lived in the usr/src/cmd
# hierarchy; but, since they don't, we have to add some hackery here.
#
# What we have to do is to reset TEXT_DOMAIN (and consequently, 
# all the other macros which use it) to SUNW_OST_OSCMD.

$(TOOLS)	:= TEXT_DOMAIN = SUNW_OST_OSCMD
$(TOOLS:=.po)	:= TEXT_DOMAIN = SUNW_OST_OSCMD
$(TOOLS:=.cat)	:= TEXT_DOMAIN = SUNW_OST_OSCMD

ROOTBINDIR=	$(ROOT)/usr/bin
ROOTTOOLS=	$(TOOLS:%=$(ROOTBINDIR)/%)
$(ROOTTOOLS):=	FILEMODE= 555

# install rule for tools
$(ROOTBINDIR)/%: %
	$(INS.file)

TICOBJS=	tic_main.o tic_hash.o tic_captab.o tic_scan.o tic_error.o \
		tic_parse.o tic_read.o tnames.o tinames.o tifnames.o

BOBJS=		print.o iexpand.o

CAPOBJS=	captoinfo.o otermcap.o $(BOBJS)

INFOOBJS=	infocmp.o cexpand.o infotocap.o $(BOBJS)

POFILES =	tput.po
XGETFLAGS +=	-a -x $(POFILES:%.po=%.xcl)

# extra clean-up work
CLEANFILES=	$(TICOBJS) $(CAPOBJS) $(INFOOBJS) \
		tnames.c tinames.c tifnames.c \
		keyname.c termcap.c tifget.c tiget.c \
		$(LOCALTMP)/term.h.new $(LOCALTMP)/term.h.new1 \
		$(LOCALTMP)/term.h.new2 $(LOCALTMP)/keycaps

CLOBBERFILES=	$(TOOLS) term.h curses.h

.KEEP_STATE:

.PARALLEL:	$(TICOBJS) $(CAPOBJS) $(INFOOBJS)

#
# The .INIT: target is being used here to make sure
# that the LOCALTMP directory has been created for
# any invocation of this Makefile.  
#
# Originally the targets that use the LOCALTMP directory 
# had had their targets dependent upon it.  eg:
#	
#   term.h + tnames.c + tinames.c + tifnames.c:  $(LOCALTMP) caps maketerm.ed
#	$(ED) - < maketerm.ed
#
# The problem with this was the those targets(curses.h, term.h, etc.) 
# were getting rebuilt on every invocation of the Makefile.
# 
# By creating the LOCALTMP directory with the INIT: target
# we are avoiding that overhead.
#
.INIT: $(LOCALTMP)

all: $(TXTS) .WAIT $(LIBS) .WAIT $(TOOLS)

install: all .WAIT $(ROOTLIBS) .WAIT $(ROOTTOOLS) .WAIT $(ROOTLINKS)

LINT_SRCS =	tput.c

lint:	$(LINT_SRCS)
	$(LINT.c) $(LINT_SRCS)

$(ROOTLINKS): $(ROOTLIBDIR)/$(LIBRARY)
	$(RM) $@; $(LN) $(ROOTLIBDIR)/$(LIBRARY) $@

install_h: copyright.h $(HDRS) .WAIT $(ROOTHDRS)

# term.h doesn't pass cstyle due to numerous lines exceeding the 80 character
# limit.  maketerm.ed can probably be fixed to prevent this, but it is arguable
# if this work would ever be worth it.
#
term.check	:= CSTYLE_TAIL = | grep -v "line > 80 characters" | true

CHECKHDRS=	$(HDRS:%.h=%.check)

check:	$(HDRS) .WAIT $(CHECKHDRS)

tools: $(TOOLS)

tic: $(TICOBJS)
	$(LINK.c) $(TICOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

captoinfo: $(CAPOBJS)
	$(LINK.c) $(CAPOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

infocmp: $(INFOOBJS)
	$(LINK.c) $(INFOOBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

term.h + tnames.c + tinames.c + tifnames.c: copyright.h caps maketerm.ed
	$(ED) - < maketerm.ed

$(LOCALTMP): 
	-@mkdir -p $(LOCALTMP)

keycaps: caps
	grep "KEY_" caps > keycaps
	@if tail -1 keycaps | grep 'KEY_MOUSE' > /dev/null;then :; \
		else echo new keys!! must change setkeymap.c; exit 1;fi

curses.h: copyright.h keycaps curses.ed
	$(ED) - < curses.ed

keyname.c: keycaps keyname.sh
	sh keyname.sh

termcap.c: copyright.h termcap.ed caps
	$(ED) - < termcap.ed
	$(RM) bool num str

tifget.c: copyright.h tifget.ed caps
	$(ED) - < tifget.ed

tiget.c: copyright.h tiget.ed caps
	$(ED) - < tiget.ed

# define library targets
include ../../Makefile.targ

# explicit dependency on derived headers
$(LIBS) $(PLIB): term.h curses.h

# message file databases:
# when other utilities have their messages gettext()'ed:
#	1) add them to the "_msg:" line (similar to "all" above)
#	2) create a *.cat dependency line (similar to tput.cat below)
#	3) copy the appropriate dependencies down here. The .c suffixes
#		should be changed to .po.
#	4) Make sure that your tools' TEXT_DOMAIN have been
#		set to SUNW_OST_OSCMD!  See above.
# 
# NOTE: the *.cat dependency here allows us to use a redefined
#	TEXT_DOMAIN = SUNW_OST_OSCMD above, so that we can move
#	the .po file to the right place.  This directory is created
#	by usr/src/Makefile.

%.cat: %.po
	$(RM) $(MSGDOMAIN)/$<
	$(CP) $< $(MSGDOMAIN)/$< 

_msg:	tput.cat
