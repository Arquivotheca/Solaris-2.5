#! /bin/csh -f
#
# Copyright (c) 1980 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#
#	@(#)vgrind.csh 1.15 92/04/12 SMI; from UCB 5.3 (Berkeley) 11/13/85
#
# vgrind

# Get formatter, path to back end program, and path to macros from the
# environment, if present, using the defaults given in the else clauses
# below.  Note that an explicit path is necessary for the macros (as
# opposed to "-mvgrind"), since we use it as an argument to cat when in
# filter mode.
if ( $?TROFF ) then
	set troff = "$TROFF"
else
	set troff = "troff"
endif
if ( $?VFONTEDPR ) then
	set vfontedpr = "$VFONTEDPR"
else
	set vfontedpr = /usr/lib/vfontedpr
endif
if ( $?TMAC_VGRIND ) then
	set macros = "$TMAC_VGRIND"
else
	set macros = "/usr/share/lib/tmac/tmac.vgrind"
endif

# We explicitly recognize some arguments as options that should be passed
# along to $troff.  Others go directly to $vfontedpr.  We collect the former
# in troffopts and the latter in args.  We treat certain arguments as file
# names for the purpose of processing the index.  When we encounter these,
# we collect them into the files variable as well as adding them to args.
set troffopts
set args
set files

# Initially not in filter mode.
unset filter

# Collect arguments.  The code partitions arguments into the troffopts,
# args, and files variables as described above, but otherwise leaves their
# relative ordering undisturbed.  Also, note the use of `:q' in variable
# references to preserve each token as a single word.
top:
if ($#argv > 0) then
    switch ( $1:q )

    # Options to be passed along to the formatter ($troff).

    case -o*:
	set troffopts = ( $troffopts:q $1:q )
	shift
	goto top

    case -P*:    # Printer specification -- pass on to troff for disposition
	set troffopts = ( $troffopts:q $1:q )
	shift
	goto top

    case -T*:    # Output device specification -- pass on to troff
	set troffopts = ( $troffopts:q $1:q )
	shift
	goto top

    case -t:
	set troffopts = ( $troffopts:q -t )
	shift
	goto top

    case -W:	# Versatec/Varian toggle.
	set troffopts = ( $troffopts:q -W )
	shift
	goto top

    # Options that affect both the formatter and vfontedpr.

    case -2:	# Two column output
	# N.B.: Implies landscape mode (there's just not enough width for
	# two columns of program output in portrait mode).
	# XXX:	Should specify variant of default output device that sets
	#	page length and width appropriately for landscape mode; as
	#	it is, tmac.vgrind has to hack this information up non-
	#	portably.
	set troffopts = ( $troffopts:q -L )
	set args = ( $args:q $1:q )
	shift
	goto top

    # Options to be passed along to vfontedpr.  The last two cases cover
    # ones that we don't explicitly recognize; the others require special
    # processing of one sort or another.

    case -d:
    case -h:
	if ($#argv < 2) then
	    printf "`gettext TEXT_DOMAIN 'vgrind: %s option must have argument'`\n" $1:q
	    goto done
	else
	    set args = ( $args:q $1:q $2:q )
	    shift
	    shift
	    goto top
	endif

    case -f:
	set filter
	set args = ( $args:q $1:q )
	shift
	goto top

    case -w:    # Alternative tab width (4 chars) -- vfontedpr wants it as -t
	set args = ( $args:q -t )
	shift
	goto top

    case -*:
	# We call this out as an explicit option to prevent flags that we
	# pass on to vfontedpr from being treated as file names.  Of course
	# this keeps us from dealing gracefully with files whose names do
	# start with `-'...
	set args  = ( $args:q  $1:q )
	shift
	goto top

    default:
    case -:
	# Record both as ordinary argument and as file name.  Note that
	# `-' behaves as a file name, even though it may well not make
	# sense as one to the commands below that see it.
	set files = ( $files:q $1:q )
	set args  = ( $args:q  $1:q )
	shift
	goto top
    endsw
endif

if (-r index) then
    echo > nindex
    foreach i ( $files )
	#	make up a sed delete command for filenames
	#	being careful about slashes.
	echo "? $i ?d" | sed -e "s:/:\\/:g" -e "s:?:/:g" >> nindex
    end
    sed -f nindex index > xindex
    if ($?filter) then
	$vfontedpr $args:q | cat $macros -
    else
	$vfontedpr $args:q | \
	    /bin/sh -c "$troff -rx1 $troffopts -i $macros - 2>> xindex"
    endif
    sort -df +0 -2 xindex > index
    rm nindex xindex
else
    if ($?filter) then
	$vfontedpr $args:q | cat $macros -
    else
	$vfontedpr $args:q | $troff -i $troffopts $macros -
    endif
endif

done:
