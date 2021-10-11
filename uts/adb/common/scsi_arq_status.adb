
#include <sys/scsi/scsi.h>

scsi_arq_status
./"status"8t"rqstat"8t"reason"8t"resid"n{sts_status,B}{sts_rqpkt_status,B}{sts_rqpkt_reason,B}{sts_rqpkt_resid,B}
+/"state"16t"stats"n{sts_rqpkt_state,X}{sts_rqpkt_statistics,X}
+/"sensedata"n{sts_sensedata,20B} {END}
