#!/bin/sh
#
#ident	"@(#)sovv.sh	1.5	94/10/20 SMI"
#
# Copyright (c) 1994 by Sun Microsystems, Inc.
#
# Shared Object Version Validation
#
# A sweep is made over the output of a build ($ROOT) to locate all shared
# objects.  These belong to one of two groups:
#
#   o	the compilation environment
#
#	These shared objects have a ".so" suffix and are suitable for use
#	by the link-editor (ld(1)) using the "-l" option.  These objects
#	should be symbolic links to the associated runtime environment
#	version of the shared object.
#
#   o	The runtime environment
#
#	These shared objects have a ".so.X" suffix, where "X" is a version
#	number.  These objects are suitable for binding by the runtime
#	linker (ld.so.1), either as explict dependencies or via dlopen(3x).
#	It is only necessary for a runtime shared object to have an
#	associated compilation environment symlink if the object is used
#	during the link-edit of other dynamic images.
#
# Using the above file list a number of validations are carried out and
# databases created:
#
#   o	Compilation environment files are validated to insure that they
#	are symbolic links to their runtime environment counterparts.
#
#   o	Create a "Version Control Database".
#
#	pvs -don	>> $INTFDIR/$RELEASE/$CONSOLIDATION
#
#	This output file contains the version control directives for all
#	compilation environment shared objects that make up this
#	consolidation.  This database allows users to build dynamic images
#	specifically to the interfaces offered by this release.
#	This file is packaged and delivered as part of the consolidation.
#
#	Note: no SUNW_private interfaces are recorded in this output.
#
#   o	Create "Version Definition Databases".
#
#	pvs -dovs	>  $INTFDIR/$RELEASE/$CONSOLIDATION-$File
#
#	These output files, one for each versioned shared object, contain
#	the complete version definition to symbol mapping definitions.
#	These files are used to validate interfaces from release to release
#	and are not delivered as part of the consolidation.

if [ "X$ROOT" = "X" -o "X$RELEASE" = "X" ] ; then
	echo "ROOT, and RELEASE environment variables must be set"
	exit 1
fi

Verbose=no
Error=0
Name=$0

Usage="usage: vbld [ -v ] [ -d database ]"
set -- `getopt d:vx $*`
if [ $? -ne 0 ] ; then
	echo $Usage
	exit 1
fi
for Arg in $*
	do
	case $Arg in
	-x) set -x ; shift ;;
	-v) Verbose=yes ; shift ;;
	-d) database=$2 ; shift 2;;
	--) shift ; break ;;
	-*) echo $Usage ; exit 1 ;;
	esac
done

Intfdir=etc/interface
Consolidation=ON

# Build up a list of shared objects, catching both the compilation environment
# and runtime environment names.

cd $ROOT

Sofiles=/tmp/sovv-$$
Srchdirs=" \
	usr/lib \
	usr/4lib \
	usr/ucblib "

find $Srchdirs \( -name '*.so*' -a ! -name ld.so \) -print > $Sofiles
if [ ! -s $Sofiles ] ; then
	echo "$Name: fatal: no files found"
	exit 1
fi


# From this list of files extract the compilation environment names.

if [ $Verbose = "yes" ] ; then
	echo "Searching for compilation environment versioned shared objects"
fi

Consname=$Intfdir/$RELEASE/$Consolidation
if [ ! -d $Intfdir/$RELEASE ] ; then
	mkdir -p $Intfdir/$RELEASE
fi
trap "rm -f $Sofiles $Consname-tmp; exit 0" 0 1 2 3 15

for Path in `grep '.so$' $Sofiles`
do
	# Make sure these are symbolic links to the appropriate runtime
	# environment counterpart.

	if [ ! -h $Path ] ; then
		echo "$Name: warning: $Path: file is not a symbolic link"
		Error=1
	fi

	# Determine if this file has any version definitions and if so add
	# them to the consolidation file.

	pvs -don $Path 	>>	$Consname-tmp
	if [ $? -eq 0 ] ; then
		if [ $Verbose = "yes" ] ; then
			echo " $Path"
		fi
	else
		if [ $Verbose = "yes" ] ; then
			echo " $Path ... no versions found"
		fi
	fi
done

# Convert the relative names of the version control directives to full paths.
# Note: any SUNW_private interfaces are also stripped from the output.

if [ -f $Consname-tmp ] ; then
	fgrep -v SUNW_private < $Consname-tmp | sed -e "s/^/\//" > $Consname
	rm -f	$Consname-tmp
fi


# From this list of files extract the runtime environment names.

if [ $Verbose = "yes" ] ; then
	echo "Searching for runtime environment versioned shared objects"
fi

for Path in `fgrep '.so.' $Sofiles`
do
	# Determine if the file has any version definitions, and if so obtain
	# the symbol associations.

	pvs -d $Path		>	/dev/null
	if [ $? -eq 0 ] ; then
		if [ $Verbose = "yes" ] ; then
			echo " $Path"
		fi
		File=`basename $Path`
		rm -f			  	$Consname-$File
		pvs -vdos $Path	>		$Consname-$File
	else
		if [ $Verbose = "yes" ] ; then
			echo " $Path ... no versions found"
		fi
	fi
done

# Create the SVID ABI specific version control definitions.  Here we know
# exactly what shared objects are defined by the ABI.  In this case convention
# dictates that the ABI version name is SVID.1;

Svidfiles=" \
	usr/lib/libc.so.1 \
	usr/lib/libsys.so.1 \
	usr/lib/libnsl.so.1"

if [ $Verbose = "yes" ] ; then
	echo
	echo "Searching for SVID versioned shared objects"
fi

Consname=$Intfdir/SVID.1/$Consolidation
if [ ! -d $Intfdir/SVID.1 ] ; then
	mkdir -p $Intfdir/SVID.1.1
fi
trap "rm -f $Sofiles $Consname-tmp; exit 0" 0 1 2 3 15

for Path in $Svidfiles
do
	if [ -f $Path ] ; then
		pvs -do -N SVID.1 $Path	>> $Consname-tmp
		if [ $? -eq 0 ] ; then
			if [ $Verbose = "yes" ] ; then
				echo " $Path"
			fi
		else
			echo " $Path: fatal: no SVID versions found"
			Error=1;
		fi
	else
		echo " $Path: fatal: file not found"
		Error=1
	fi
done

# Convert the relative names of the version control directives to full paths
 
if [ -f $Consname-tmp ] ; then
	sed -e "s/^/\//"	< $Consname-tmp	> $Consname
	rm -f	$Consname-tmp
fi

exit $Error
