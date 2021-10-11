#include <sys/scsi/scsi.h>

scsi_hba_tran
./"devinfop"16t"hba_privp"16t"tgt_privp"n{tran_hba_dip,X}{tran_hba_private,X}{tran_tgt_private,X}
+/"scsi_devp"16t"tran_tgt_init"16t"tran_tgt_probe"n{tran_sd,X}{tran_tgt_init,X}{tran_tgt_probe,X}
+/"tran_tgt_free"16t"tran_start"16t"tran_reset"n{tran_tgt_free,X}{tran_start,X}{tran_reset,X}
+/"tran_abort"16t"tran_getcap"16t"tran_setcap"n{tran_abort,X}{tran_getcap,X}{tran_setcap,X}
+/"tran_init_pkt"16t"tran_dest_pkt"16t"tran_dmafree"n{tran_init_pkt,X}{tran_destroy_pkt,X}{tran_dmafree,X}
+/"tran_sync_pkt"16t"tran_rst_notify"16t"tran_min_xfer"n{tran_sync_pkt,X}{tran_reset_notify,X}{tran_min_xfer,X}
+/"tran_hba_flags"16t"minbrst"8t"maxbrst"n{tran_hba_flags,X}{tran_min_burst_size,B}{tran_max_burst_size,B} {END}
