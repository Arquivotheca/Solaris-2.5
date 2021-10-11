#include <sys/types.h>
#include <sys/flock_impl.h>

graph
.$<<mutex{OFFSETOK}
+/"active head"n
./"first active lock"16t"last active lock"n{active_locks.l_next,X}{active_locks.l_prev,X}n
+/"first sleeping lock"16t"last sleeping lock"n{sleeping_locks.l_next,X}{sleeping_locks.l_prev,X}n
+/"index"16t"flk_lockmgr_status_t"n{index,X}{lockmgr_status,X}n
