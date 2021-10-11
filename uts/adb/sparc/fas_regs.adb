
#include <sys/scsi/scsi.h>
#include <sys/scsi/adapters/fasreg.h>

fasreg
./"cnt_lo"8t"cnt_mid"8t"fifo"8t"cmd"n{fas_xcnt_lo,B}{fas_xcnt_mid,B}{fas_fifo_data,B}{fas_cmd,B}
+/"stat"8t"intr"8t"step"8t"fifo_flag"n{fas_stat,B}{fas_intr,B}{fas_step,B}{fas_fifo_flag,B}
+/"conf"8t"stat2"8t"test"8t"conf2"n{fas_conf,B}{fas_clock_conv,B}{fas_test,B}{fas_conf2,B}
+/"conf3"8t"recmdlo"8t"recmdhi"n{fas_conf3,B}{fas_recmd_lo,B}{fas_recmd_hi,B}
