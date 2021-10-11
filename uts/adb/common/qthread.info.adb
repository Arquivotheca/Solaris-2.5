#include <sys/thread.h>

_kthread
.="thread_id = "X
./"wchan = "{t_wchan,X}"_klwp = "{t_lwp,X}"procp = "{t_procp,X}
{*t_procp,<f}$<<qproc.info
