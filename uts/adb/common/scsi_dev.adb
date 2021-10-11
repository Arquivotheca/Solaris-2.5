#include <sys/scsi/scsi.h>

scsi_device
.$<<scsi_addr{OFFSETOK}
+/"devinfop"n{sd_dev,X}
+$<<mutex{OFFSETOK}
+/"reserved"n{sd_reserved,X}
+/"scsi_inqp"16t"scsi_extsenp"16t"privatep"n{sd_inq,X}{sd_sense,X}{sd_private,X}{END}
