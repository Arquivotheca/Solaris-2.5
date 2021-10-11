#!/bin/sh
# Copyright (c) 1993 by Sun Microsystems, Inc.

#ident  "@(#)nfsfind.sh 1.4     93/05/18 SMI"        /*      */
#
# Check shared NFS filesystems for .nfs* files that
# are more than a week old.
#
# These files are created by NFS clients when an open file
# is removed. To preserve some semblance of Unix semantics
# the client renames the file to a unique name so that the
# file appears to have been removed from the directory, but
# is still usable by the process that has the file open.

if [ ! -s /etc/dfs/sharetab ]; then exit ; fi

for dir in `awk '$3 == "nfs" {print $1}' /etc/dfs/sharetab`
do
        find $dir -name .nfs\* -mtime +7 -mount -exec rm -f {} \;
done
