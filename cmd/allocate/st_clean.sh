#! /bin/sh
#
# @(#)st_clean.sh 1.3 93/09/27 SMI; SunOS BSM
#
#  This a clean script for all tape drives
# 

USAGE="st_clean [-s|-f|-r] device info_label"
PATH="/usr/sbin:/usr/bin"

con_msg() {
   echo "Media in $DEVICE is ready.  Please, label and store safely." > /dev/console
}

e_con_msg() {
   echo "`basename $0`: Error cleaning up device $DEVICE." > /dev/console
}

user_msg() {
   echo "Media in $DEVICE is ready.  Please, label and store safely." >/dev/tty
}

e_user_msg() {
   echo "`basename $0`: Error cleaning up device $DEVICE." >/dev/tty
   echo "Please inform system administrator" >/dev/tty
}

mk_error() {
   chown allocate /etc/security/dev/$1
   chmod 0100 /etc/security/dev/$1
}

while getopts ifs c
do
   case $c in
   i)   FLAG=$c;;
   f)   FLAG=$c;;
   s)   FLAG=$c;;
   \?)   echo $USAGE >/dev/tty
      exit 1 ;;
   esac
done
shift `expr $OPTIND - 1`

# get the map information

TAPE=$1
MAP=`dminfo -v -n $TAPE`
DEVICE=`echo $MAP | cut -f1 -d:`
TYPE=`echo $MAP | cut -f2 -d:`
FILES=`echo $MAP | cut -f3 -d:`
DEVFILE=`echo $FILES | cut -f1 -d" "`

#if init then do once and exit

if [ "$FLAG" = "i" ] ; then
   x="`mt -f $DEVFILE rewoffl 2>&1`"
   z="$?"   

   case $z in
   0)

   # if this is a open reel tape than we a sucessful
   # else must be a cartrige tape we failed

      if mt -f $DEVFILE status 2>&1 | grep "no tape loaded" >/dev/null ; then  
         con_msg
         exit 0
      else 
         e_con_msg
         mk_error $DEVICE
         exit 1
      fi;;
   1) 
   
   # only one error mesage is satisfactory

      if echo $x | grep "no tape loaded" >/dev/null ; then
         con_msg
         exit 0
      else
         e_con_msg
         mk_error $DEVICE
         exit 1
      fi;;

   2) 

   # clean up failed exit with error

      e_con_msg
      mk_error $DEVICE
      exit 1;;

   esac
else
# interactive clean up
   x="`mt -f $DEVFILE rewoffl 2>&1`"
   z="$?"

   case $z in
   0)

   # if this is a open reel tape than we a sucessful
   # else must be a cartrige tape we must retry until user removes tape

      if mt -f $DEVFILE status 2>&1 | grep "no tape loaded"  > /dev/null ; then
         user_msg
         exit 0
      else
         while true
         do
            if mt -f $DEVFILE status 2>&1 | grep "no tape loaded" > /dev/null ; then
               user_msg
               exit 0
            else
               echo "Please remove the tape from the $DEVICE"  >/dev/tty
               /usr/5bin/echo \\007 >/dev/tty
               sleep 3
            fi
         done
      fi;;
   1)

   # only one error mesage is satisfactory

      if echo $x | grep "no tape loaded" > /dev/null ; then
         user_msg
         exit 0
      else
         e_user_msg
         mk_error $DEVICE
         exit 1
      fi;;

   2)

   # clean up failed exit with error

      e_user_msg
      mk_error $DEVICE
      exit 1;;

   esac
fi
exit 2
