
#include <sys/types.h>
#include <sys/flock_impl.h>

lock_descriptor
./"next"16t"prev"n{l_next,X}{l_prev,X}n
+/"edge"
.$<<edge{OFFSETOK}
+/"stack"16t"stack1"16t"dstack"16t"sedge"n{l_stack,X}{l_stack1,X}{l_dstack,X}{l_sedge,X}n
+/"barrier_count"n{l_index,X}n
+/"graph"16t"vnode"16t"lock_type"16t"lock_state"n{l_graph,X}{l_vnode,X}{l_type,X}{l_state,X}n
+/"lock_start"16t"lock_end"n{l_start,X}{l_end,X}n
+/"flock_whence"16t"flock_start"16t"flock_len"n{l_flock.l_whence,x}{l_flock.l_start,X}{l_flock.l_len,X}n
+/"pid"16t"sysid"16t"procvertex"n{l_flock.l_pid,X}{l_flock.l_sysid,X}{pvertex,X}n
