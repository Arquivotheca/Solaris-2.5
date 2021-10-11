
#include <sys/kmem.h>
#include <sys/kmem_impl.h>
#include <sys/scsi/adapters/espvar.h>

callback_info
./"next"16t"qf"16t"qb"n{c_next,X}{c_qf,X}{c_qb,X}
+/"mutex:"
.$<<mutex{OFFSETOK}
+/"cv"8t"thread"n{c_cv,x}{c_thread,X}
+/"qlen"16t"id"8t"now ql"8t"spawn"8t"count"8t"signal needed"n{c_qlen,X}{c_id,B}{c_cb_now_qlen,B}{c_spawned,B}{c_count,B}{c_signal_needed,B}{END}
