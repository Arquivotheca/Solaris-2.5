#include <sys/thread.h>
#include <sys/callo.h>

callout
./"next"16t"xid"16t"runtime"n{c_next,X}{c_xid,X}{c_runtime,X}
+/"func"16t"arg"16t"executor"16t"cv"n{c_func,P}{c_arg,X}{c_executor,X}{c_done,x}
