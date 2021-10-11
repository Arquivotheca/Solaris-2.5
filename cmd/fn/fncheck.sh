#!/bin/sh
#
#ident	"@(#)fncheck.sh 1.3	95/03/27 SMI"
#

AWK=/usr/bin/nawk
CAT=/usr/bin/cat
DIFF=/usr/bin/diff
EGREP=/usr/bin/egrep
FNSLIST=/usr/bin/fnlist
NISCAT=/usr/bin/niscat
RM="/usr/bin/rm -f"
SORT=/usr/bin/sort
UNIQ=/usr/bin/uniq
FNSCREATE=/usr/sbin/fncreate
FNSDESTROY=/usr/sbin/fndestroy
ORGUNIT=thisorgunit
usage()
{
        pgm=`basename $1`
	echo "$pgm: $2"
        ${CAT} <<END
Usage: $pgm [-r] [-s] [-u] [-t type] [domain_name]
END
#	hostname|username]
#	[-r(everse)|-s(ource)]
#	organization_name
	rmtmpfiles
        exit 1
}
 
hostnameflag=0
usernameflag=0
tflag=0
rsflag=1
reverseflag=0
sourceflag=0
updateflag=0
orgnameflag=1

TMP=/tmp
nishost=${TMP}/nishost.$$
nisuser=${TMP}/nisuser.$$
fnshost=${TMP}/fnshost.$$
fnsuser=${TMP}/fnsuser.$$
errorfile=${TMP}/errorfile.$$
REV_HOST=${TMP}/rev_host.$$
SOURCE_HOST=${TMP}/source_host.$$
REV_USER=${TMP}/rev_user.$$
SOURCE_USER=${TMP}/source_user.$$
RESULT=${TMP}/result.$$

# set -x 


while getopts t:rsu c
do
        case $c in
        t)      # hostname or username
                case $OPTARG in
                hostname) hostnameflag=1
                        ;;
                username) usernameflag=1
                        ;;
                *)      usage $0 "Wrong option for -t"
                        ;;
                esac
                tflag=1
                ;;
        r)      # reverse
                reverseflag=1
		rsflag=0
                ;;
        s)      # source
                sourceflag=1
		rsflag=0
                ;;
        u)      # update
                updateflag=1
                ;;
        *)      # illegal value
                usage $0 " "
                exit 1
                ;;
        esac
done
 
shift `expr $OPTIND - 1`
org_name=$1

if [ x"$org_name" = x ]
then
	org_name=`/usr/bin/domainname`
	orgnameflag=0
#	usage $0 "Missing organization_name"
fi

rmtmpfiles ()
{
	$RM $nishost $nisuser $fnshost $fnsuser $errorfile $REV_HOST $REV_USER \
		$SOURCE_HOST $SOURCE_USER $RESULT
}
 
trap `rmtmpfiles ; exit 1` 0 1 2 15

compare ()
{
	SOURCE=$1
	DEST=$2
	FILE=$3

	${DIFF} $SOURCE $DEST | ${EGREP} '^<' | \
		${AWK} '{print $2}' >> $FILE
}


# add entries in FNSP from NIS+
# if updateflag is not set then add it to the RESULTS file
# $1 - the string for type of entry "user" or "host"
# $2 - file that contains the entries to be added
add()
{

	TYPE=$1
	FILE=$2
	if [ $updateflag -eq 0 ]
	then
		echo "${TYPE}s in NIS+ table with no FNS contexts :" >> $RESULT
		${CAT} $FILE >> ${RESULT}
	else
		echo "Adding ${TYPE}s in FNS"
		for u in `${CAT} $FILE`
		do
		if [ orgnameflag -eq 1 ]
		then
			${FNSCREATE} -t $TYPE -v org/${org_name}/$TYPE/$u
		else
			${FNSCREATE} -t $TYPE -v ${ORGUNIT}/$TYPE/$u
		fi
		done
	fi
}


# delete entries in FNSP that don't exist in NIS+
# $1 - the string for type of entry "user" or "host"
# $2 - file that contains the entries to be deleted from FNSP
delete()
{
	TYPE=$1
	FILE=$2

	if [ $updateflag -eq 0 ]
	then
		echo "${TYPE} contexts in FNS that are not in corresponding NIS+ table :">> $RESULT
		${CAT} $FILE >> ${RESULT}
	else
		echo "Deleting ${TYPE}s in FNS"
		for u in `${CAT} $FILE`
		do
			# If there are subcontext, error message will be
			# printed to that effect
		if [ orgnameflag -eq 1 ]
		then
			${FNSDESTROY} org/${org_name}/$TYPE/$u/service
			${FNSDESTROY} org/${org_name}/$TYPE/$u
		else
			${FNSDESTROY} ${ORGUNIT}/$TYPE/$u/service
			${FNSDESTROY} ${ORGUNIT}/$TYPE/$u
		fi
		done
	# These three lines should be removed when we can delete properly
	#	echo "Deleting $TYPE contexts in FNS is not implemented in fnscheck"
	#	echo "use /usr/bin/fnsdestroy to destroy listed contexts"
	#	echo "${TYPE} contexts in FNS that are not in corresponding \
	#		NIS+ table :">> $RESULT
	#	${CAT} $FILE >> ${RESULT}
	fi
}

# Updates the users and hosts by calling the add/delete functions
# $1 nis file name
# $2 type - user/host
# $3 fns file name
# $4 source file name
# $5 reverese file name
update()
{
	NISFILE=$1
	HU_TYPE=$2
	FNSFILE=$3
	SRC_FILE=$4
	REV_FILE=$5

	# Check for correct domainname. First NIS+
	if [ ! -s $NISFILE ]
	then
		usage $0 "NIS+ Error: Incorrect domain_name"
	fi

	if [ $orgnameflag -eq 1 ]
	then
		${FNSLIST} org/${org_name}/$HU_TYPE/ | sed -n '2,$p' |\
			 ${SORT} > $FNSFILE
	else
		${FNSLIST} ${ORGUNIT}/$HU_TYPE | sed -n '2,$p' |\
			 ${SORT} > $FNSFILE
	fi	
	
	# Check the correctness of domain name for FNS
	${EGREP} "Error:" -f $FNSFILE > $errorfile 2> /dev/null
	if [ -s $errorfile ]
	then
		usage $0 "FNS Error: Incorrect domain_name"
	fi

	if [ $rsflag -eq 1 ]
	then
		compare $NISFILE $FNSFILE $SRC_FILE
		compare $FNSFILE $NISFILE $REV_FILE

		if [ ! -s $REV_FILE -a ! -s $SRC_FILE ]
		then
			echo "NIS+ $HU_TYPE table and FNS contexts are consistent."
		fi
		if [ -s $SRC_FILE ]
		then
			add $HU_TYPE $SRC_FILE
		fi
		if [ -s $REV_FILE ]
		then
			delete $HU_TYPE $REV_FILE
		fi
	# source flag only
	elif [ $sourceflag -eq 1 ]
	then
		compare $NISFILE $FNSFILE $SRC_FILE

		if [ -s $SRC_FILE ]
		then
			add $HU_TYPE $SRC_FILE
		else
			echo "All NIS+ ${HU_TYPE}s table have $HU_TYPE contexts in FNS."
		fi

	# reverse flag only
	elif [ $reverseflag -eq 1 ]
	then
		compare $FNSFILE $NISFILE $REV_HOST

		if [ -s $REV_FILE ]
		then
			delete $HU_TYPE $REV_FILE
		else
			echo "All ${HU_TYPE}s contexts in FNS are in the NIS+ $HU_TYPE table."
		fi
	fi
}	


if [ $hostnameflag -eq 1 -o $tflag -eq 0 ]
then
	${NISCAT} hosts.org_dir.${org_name} 2> /dev/null | ${AWK} '{print $2}' |\
		 ${SORT} | ${UNIQ} > $nishost

	update $nishost host $fnshost $SOURCE_HOST $REV_HOST
fi

if [ $usernameflag -eq 1 -o $tflag -eq 0 ]
then
	${NISCAT} passwd.org_dir.${org_name} 2> /dev/null | ${AWK} -F":" '{print $1}' | \
		${SORT} | ${UNIQ} > $nisuser

	update $nisuser user $fnsuser $SOURCE_USER $REV_USER
fi

if [ -f $RESULT ]
then 
	${CAT} $RESULT
fi

rmtmpfiles

exit 0


