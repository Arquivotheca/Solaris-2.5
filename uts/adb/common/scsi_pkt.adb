#include <sys/scsi/scsi.h>

scsi_pkt
./"pkt_ha_privp"n{pkt_ha_private,X}
+$<<scsi_addr{OFFSETOK}
+/"pkt_privp"16t"comp_fnp"16t"flags"16t"time allowed"n{pkt_private,X}{pkt_comp,X}{pkt_flags,X}{pkt_time,X}
+/"statusp"16t"cdbp"16t"resid count"n{pkt_scbp,X}{pkt_cdbp,X}{pkt_resid,X}
+/"state"16t"stats"16t"reason"n{pkt_state,X}{pkt_statistics,X}{pkt_reason,V}{END}
