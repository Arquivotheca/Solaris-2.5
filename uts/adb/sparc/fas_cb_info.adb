
#include <sys/scsi/scsi.h>
#include <sys/scsi/adapters/fasreg.h>
#include <sys/scsi/adapters/fasvar.h>

callback_info
./"qf"16t"qb"n{c_qf,X}{c_qb,X}
+/"mutex:"
.$<<mutex{OFFSETOK}
+/"in callback"n{c_in_callback,B}{END}
