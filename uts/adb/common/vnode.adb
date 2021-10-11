#include <sys/types.h>
#include <sys/time.h>
#include <sys/vnode.h>

vnode
./"flag"8t"refcnt"20t"vfsmnt"19t"vop"n{v_flag,x}{v_count,D}{v_vfsmountedhere,X}{v_op,p}
+/"vfsp"16t"stream"16t"pages"n{v_vfsp,X}{v_stream,X}{v_pages,X}
+/"type"16t"rdev"8t"data"n{v_type,D}{v_rdev,X}{v_data,X}
+/"filocks"n{v_filocks,X}{END}
