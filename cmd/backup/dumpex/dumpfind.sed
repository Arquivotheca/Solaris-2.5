#!/bin/sh

#
#	@(#)dumpfind.sed 1.3 93/10/14
# 
# This script recovers the disaster recover file from the end
# of a tape and notifies the user of what he should do to recover
# his database
#

PATH=XXXHSMROOTXXX/sbin:/bin:/usr/bin:/usr/sbin:/sbin
export PATH

############ Initialization
SH_NAME="dumpfind"
TAPE_LOC=""
REMOTE=""
DEVICE=""
FHOST=""
FDIR=""
TMPDIR="/var/tmp/$SH_NAME.$$"
DLIST="/var/tmp/dumplist.$$"
FLIST="/var/tmp/flist.$$"
SORTLIST="/var/tmp/sortlist.$$"

EXITCODE=0

die () {
    echo "$SH_NAME: $1 - Exiting..."
    cd /
    /bin/rm -rf $TMPDIR
    exit $2
}

rcmd() {
    stat=`rsh -n $1 sh -c \'$2\; echo \$\?\'`
    if [ $? -ne 0 -o "$stat" != "0" ]
    then
	return 1
    else
	return 0
    fi
}

############ Introduction
/bin/cat << EOF

This program will help you recover the Online: Backup database or any
other file system, without using the recover program.  To run this
program, you will need the tape with the disaster recovery file on it
(usually the last backup tape written).  It also helps to have the tape
label/file number information from the dumpex completion email message.
The tape label/file number is not necessary, but it saves time.

EOF
echo "Enter return to continue: \c"
read ans

############ Prompt user for tape device
while [ "$TAPE_LOC" = "" ]
do
    echo
    echo "Enter media drive location (local | remote): \c"
    read TAPE_LOC
    case "$TAPE_LOC" in
    l*)
	TAPE_LOC=local;;
    r*)
	TAPE_LOC=remote;;
    *)
	echo
	echo "$SH_NAME: Incorrect response, use \"local\" or \"remote\""
	TAPE_LOC="" ;;
    esac
done

if [ $TAPE_LOC = remote ]
then
    while [ "$REMOTE" = "" ]
    do
        echo
        echo "Enter hostname of remote host: \c"
        read REMOTE
    done
    rcmd $REMOTE "echo 0 > /dev/null"
    if [ $? -ne 0 ]
    then
	die "Problem reaching remote host $REMOTE" 1
    fi
fi

while [ "$DEVICE" = "" ]
do
    echo 
    echo "Enter nonrewinding tape device name (for example, /dev/rmt/0bn)."
    echo "If you are using a low-density tape in a high-density drive,"
    echo "specify the low-density tape device (for example, /dev/rmt/0lbn)."
    echo "Tape device name: \c"
    read DEVICE
done
if [ $TAPE_LOC = remote ]
then
    RDEVICE="$REMOTE:$DEVICE"
else
    RDEVICE=$DEVICE
fi

############ Find out file number
echo
echo "Enter file number of disaster recovery file (return = don't know): \c"
read FILENO
if [ "$FILENO" != "" ]
then
    RFILENO=`expr $FILENO - 1`
fi

############ Change to /var/tmp and set up tmp dir
mkdir $TMPDIR 
if [ $? -ne 0 ]
then
    die "Problems with mkdir $TMPDIR" 1 
fi
cd $TMPDIR
if [ $? -ne 0 ]
then
    die "Problems with cd to $TMPDIR" 1 
fi

############ Position to the correct file
echo
if [ "$FILENO" = "" ]			# We don't know where it is
then
    if [ $TAPE_LOC = remote ]		# and it is remote
    then
	echo "Positioning $DEVICE on $REMOTE to end-of-media"
	rcmd $REMOTE "/usr/bin/mt -f $DEVICE rewind"
	rcmd $REMOTE "/usr/bin/mt -f $DEVICE eom"
	if [ $? -ne 0 ]
	then
	    die "Problems with /usr/bin/mt eom for $DEVICE on $REMOTE" 1
	fi
	echo "Back spacing $DEVICE on $REMOTE one file"
	rcmd $REMOTE "/usr/bin/mt -f $DEVICE nbsf 1"
	if [ $? -ne 0 ]
	then
	    die "Problems with /usr/bin/mt nbsf for $DEVICE on $REMOTE" 1
	fi
    else
	echo "Positioning $DEVICE to end-of-media"
	/usr/bin/mt -f $DEVICE rewind
	/usr/bin/mt -f $DEVICE eom
	if [ $? -ne 0 ]
	then
	    die "Problems with /usr/bin/mt eom for $DEVICE" 1
	fi
	echo "Back spacing $DEVICE one file"
	/usr/bin/mt -f $DEVICE nbsf 1
	if [ $? -ne 0 ]
	then
	    die "Problems with /usr/bin/mt nbsf for $DEVICE" 1
	fi
    fi
else					# We do know where it is
    if [ $TAPE_LOC = remote ]		# and it is remote
    then
	echo "Positioning $DEVICE on $REMOTE to file $FILENO"
	rcmd $REMOTE "/usr/bin/mt -f $DEVICE asf $RFILENO"
	if [ $? -ne 0 ]
	then
	   die "Problems with /usr/bin/mt asf $RFILENO for $DEVICE on $REMOTE" 1
	fi
    else
	echo "Positioning $DEVICE to file $FILENO"
	/usr/bin/mt -f $DEVICE asf $RFILENO
	if [ $? -ne 0 ]
	then
	    die "Problems with /usr/bin/mt asf $RFILENO for $DEVICE" 1
	fi
    fi
fi

############ At this point, we are positioned to the file, so we read
############ with hsmrestore t | head and make sure the file we are looking
############ for is there.
echo "Verifying disaster recovery file on $RDEVICE"
TNAME=`hsmrestore tf $RDEVICE | head -10 | grep tlist | sed -e 's/[^\.]*//`
if [ $? -ne 0 -o "X$TNAME" = "X" ] 
then
    echo "$SH_NAME: Problems with disaster recovery file"
    die "File not found or restore error" 1
fi
if [ $TAPE_LOC = remote ]
then
    rcmd $REMOTE "/usr/bin/mt -f $DEVICE eom"
    if [ $? -ne 0 ]
    then
	die "Problems with /usr/bin/mt eom $FILENO for $DEVICE on $REMOTE" 1
    fi
else
    /usr/bin/mt -f $DEVICE eom
    if [ $? -ne 0 ]
    then
	die "Problems with /usr/bin/mt eom $FILENO for $DEVICE" 1
    fi
fi

############ Re-position back to the begining of the file
if [ $TAPE_LOC = remote ]		# and it is remote
then
    echo "Positioning $DEVICE on $REMOTE back to disaster recovery file"
    rcmd $REMOTE "/usr/bin/mt -f $DEVICE nbsf 1"
    if [ $? -ne 0 ]
    then
	die "Problems with /usr/bin/mt nbsf $FILENO for $DEVICE on $REMOTE" 1
    fi
else
    echo "Positioning $DEVICE back to disaster recovery file"
    /usr/bin/mt -f $DEVICE nbsf 1
    if [ $? -ne 0 ]
    then
	die "Problems with /usr/bin/mt nbsf $FILENO for $DEVICE" 1
    fi
fi

############ Read just the file
echo "Extracting disaster recovery file"
/bin/cat << EOF | hsmrestore xfy $RDEVICE $TNAME > errs 2>&1
1
n
EOF
if [ $? -ne 0 ]
then
    die "Problems restoring disaster recovery file" 1
fi

############ Copy the tapelist to /var/tmp
sed -e '1d' $TNAME > $DLIST
if [ $? -ne 0 ]
then
    die "$SH_NAME: Couldn't copy tapelist file to $DLIST" 1
fi

############ Find out what fs database was on or ask for a machine/filesystem
while [ "$DOINGDB" = "" ]
do
    echo
    echo "What file system do you want to restore? (database | other): \c"
    read DOINGDB
    case "$DOINGDB" in
    d*)
        DOINGDB=database;;
    o*)
        DOINGDB=other;;
    *)
        echo
        echo "$SH_NAME: Incorrect response, use \"database\" or \"other\""
        DOINGDB="" ;;
    esac
done

if [ $DOINGDB = database ]
then
    read FHOST DBDIR DIR FDIR junk < $TNAME
else
    while [ "$FHOST" = "" ]
    do
	echo
	echo "Enter the name of the machine that held the file system"
	echo "you are looking for: \c"
	read FHOST
    done

    while [ "$FDIR" = "" ]
    do
	echo
	echo "Enter the name of the file system or device you are looking for"
	echo "(for example, /stuff, /dev/sd1c, /dev/id001g): \c"
	read FDIR
    done
fi

############ Figure out which dumps are of the database and sort them
/bin/cat << EOF > findem.awk
BEGIN { startline = 0; }

# read all lines:
{
    line[NR] = \$0;
    level[NR] = \$3;
    if (\$3 == "0") { startline = NR; }  # last level 0
}

END {
    if (startline == 0) { exit; }
    for (i=startline; i<=NR;  )	# start at the last level 0
    {
	# Surely I am now pointing at the beginning of level

	curlev = level[i];

	# delete this level and all levels above it
 	if (curlev != "x")
	{
	    for (j = level[i]; j <= 9; j++) { tapes[j] = ""; }
	    tapes["x"] = "";
	}
	tapes[curlev] = tapes[curlev] " " i;

	for (i++; i<=NR; i++)
	{
	    if (length (level[i]) == 1 && level[i] != "A")
		break;
	    tapes[curlev] = tapes[curlev] " " i;
	}
    }

    for (levs = "0123456789x"; length(levs)>0; )
    {
 	thislev = substr(levs, 1, 1);
	levs = substr (levs, 2);

	# At this point, tapes[thislev] is a list of all dumps we want

	n = split (tapes[thislev], dumps);
	for (i = 1; i <= n; i++)
	{
	    split (line[dumps[i]], x);
	    printf ("Tape: %s File: %s - Level %s dump of %s from %s %s %s\n", x[1], x[2], x[3], x[4], x[8], x[9], x[11]);
	}
    }
}
EOF
sed -e '1d' $DLIST | grep $FHOST: | grep $FDIR | sort +5n | awk -f findem.awk > $FLIST
if [ ! -s $FLIST ]
then
    echo
    if [ $DOINGDB = database ]
    then
	echo "$SH_NAME: No complete set of dumps for database $FDIR"
	echo "	on machine $FHOST was found"
    else
	echo "$SH_NAME: No complete set of dumps for $FDIR was found"
    fi
    if [ -f $DLIST ]
    then
	echo "$SH_NAME: A complete tape/dump listing was left in $DLIST"
    fi
    cd /
    \rm -f $FLIST
    \rm -rf $TMPDIR
    exit 1
fi

############ Print it out in a pretty way

clear
if [ $DOINGDB = database ]
then
    cat << EOF
To recover the database, you must restore the dumps of the file system
in the order listed on the next screen.  Be sure that the file system is
mounted.

Once you have restored the file system, restart the database daemon.
Run 'dumpdm -a dir_rebuild' and 'dumpdm tapefile_rebuild' to ensure
database integrity.

You will probably have to run 'dumpdm tapeadd' to add the last few dumps
on the last tape.  See the Online: Backup documentation for more
information on disaster recovery.

Once all the dumps are added to the database, recover(1) can use the
database to restore all of the other filesystems.

EOF
else
    cat << EOF
To recover the file system, you will need to restore its dumps in the
order listed on the next screen.  Be sure that the file system is
mounted.  See the Online: Backup documentation for more information on
restoring a file system.

EOF
fi
echo "Enter return to continue: \c"
read ans

clear
echo 
if [ $DOINGDB = database ]
then
    echo "Restore the following files to recover the database:"
else
    echo "Restore the following files to recover $FHOST:$FDIR:"
fi
echo
/bin/cat $FLIST 
echo
echo
(fmt || cat)  << EOF 2>/dev/null
The file $FLIST contains this information.
Write its contents down on paper, or print
$FLIST for future reference.

EOF

############ If we are doing a database recover, sort the data to
############ find the last day's tapes
if [ $DOINGDB = database ]
then
    sort +5n $DLIST > $SORTLIST    
(fmt || cat) << EOF 2>/dev/null
The file $SORTLIST contains a
chronologically-sorted copy of the complete tape/dump listing.

EOF
fi

############ Clean up and let them know about the copy we left
(fmt || cat) << EOF 2>/dev/null
The file $DLIST
contains a copy of the complete tape/dump listing.

EOF
cd /
/bin/rm -rf $TMPDIR
exit 0
