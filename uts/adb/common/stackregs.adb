#ifdef i386
#include <sys/reg.h>
#endif
#include <sys/frame.h>

frame
.>f
#ifdef sparc
./"locals:"n{fr_local,8X}
+/"ins:"n{fr_arg,6X}
#endif
,#{*fr_savpc,<f}$<
{*fr_savpc,<f}/i
,#{*fr_savfp,<f}$<
{*fr_savfp,<f}$<stackregs
