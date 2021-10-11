#include <rpc/types.h>
#include <sys/vfs.h>
#define NFSSERVER
#include <nfs/nfs.h>

svcfh
./"fsid"16t16t"len"n{fh_fsid,2X}{fh_len,d}
+/"data"n{fh_data,10B}
+/"xlen"8t"xdata"n{fh_xlen,d}{fh_xdata,10B}{END}
