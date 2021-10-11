#ifndef _KERNEL
#define _KERNEL
#endif
#include <sys/types.h>
#include <sys/file.h>

file
./"flag"16t"count"n{f_flag,x}16t{f_count,D}
+/"vnode"16t"offset"n{f_vnode,X}{f_offset,2X}
+/"cred"16t"audit_data"n{f_cred,X}{f_audit_data,X}{END}
