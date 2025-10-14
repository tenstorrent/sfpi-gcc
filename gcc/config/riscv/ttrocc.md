;; Machine description for Tenstorrent ROCC Intrinsics.
;; Copyright (C) 2025 Tenstorrent Inc.

;; This file is part of GCC.

;; GCC is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 3, or (at your option)
;; any later version.

;; GCC is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING3.  If not see
;; <http://www.gnu.org/licenses/>.

;; TT rocc extension

;; FIXME: We need to use a proper target string like TARGET_XVTTROCC but once the 15.1 update is complete
;; for now, keep the empty string

(define_c_enum "unspecv" [
  UNSPECV_NOC_FENCE
  UNSPECV_DBG_POSTCODE
  UNSPECV_LLK_INTF_WRITE  
  UNSPECV_LLK_INTF_READ  
  UNSPECV_FDS_INTF_WRITE  
  UNSPECV_FDS_INTF_READ  
  UNSPECV_CMDBUF_WR_REG  
  UNSPECV_CMDBUF_RD_REG  
  UNSPECV_CMDBUF_GET_VC_SPACE  
  UNSPECV_CMDBUF_GET_VC_SPACE_VC  
  UNSPECV_CMDBUF_WR_SENT  
  UNSPECV_CMDBUF_WR_SENT_TRID  
  UNSPECV_CMDBUF_TR_ACK  
  UNSPECV_CMDBUF_TR_ACK_TRID  
  UNSPECV_CMDBUF_RESET  
  UNSPECV_CMDBUF_IDMA_GET_VC_SPACE  
  UNSPECV_CMDBUF_IDMA_GET_VC_SPACE_VC  
  UNSPECV_CMDBUF_IDMA_TR_ACK  
  UNSPECV_CMDBUF_IDMA_TR_ACK_TRID  
  UNSPECV_CMDBUF_ISSUE_TRANS  
  UNSPECV_CMDBUF_ISSUE_INLINE_TRANS  
  UNSPECV_CMDBUF_ISSUE_INLINE_ADDR_TRANS  
  UNSPECV_CMDBUF_ISSUE_READ1_TRANS  
  UNSPECV_CMDBUF_ISSUE_READ2_TRANS  
  UNSPECV_CMDBUF_ISSUE_WRITE1_TRANS  
  UNSPECV_CMDBUF_ISSUE_WRITE2_TRANS  
])

(define_insn "riscv_ttrocc_noc_fence"
  [(unspec_volatile [(const_int 0)] UNSPECV_NOC_FENCE)]
  ""
  "tt.rocc.noc_fence")

(define_insn "riscv_ttrocc_dbg_postcode"
  [(unspec_volatile [(match_operand:DI 0 "const_int_operand")] UNSPECV_DBG_POSTCODE)]
  ""
  "tt.rocc.dbg_postcode\t%0")

(define_insn "riscv_ttrocc_llk_intf_write"
  [(unspec_volatile [(match_operand:DI 0 "const_int_operand")
                     (match_operand:DI 1 "const_int_operand")] UNSPECV_LLK_INTF_WRITE)]
  ""
  "tt.rocc.llk_intf_write\t%0,%1")

(define_insn "riscv_ttrocc_llk_intf_read"
  [(unspec_volatile [(match_operand:DI 0 "register_operand")
                     (match_operand:DI 1 "const_int_operand")] UNSPECV_LLK_INTF_READ)]
  ""
  "tt.rocc.llk_intf_read\t%1")

(define_insn "riscv_ttrocc_fds_intf_write"
  [(unspec_volatile [(match_operand:DI 0 "const_int_operand")
                     (match_operand:DI 1 "const_int_operand")] UNSPECV_FDS_INTF_WRITE)]
  ""
  "tt.rocc.fds_intf_write\t%0,%1")

(define_insn "riscv_ttrocc_fds_intf_read"
  [(unspec_volatile [(match_operand:DI 0 "register_operand")
                     (match_operand:DI 1 "const_int_operand")] UNSPECV_FDS_INTF_READ)]
  ""
  "tt.rocc.fds_intf_read\t%1")

(define_insn "riscv_ttrocc_cmdbuf_wr_reg"
  [(unspec_volatile [(match_operand:DI 0 "const_int_operand")
                     (match_operand:DI 1 "const_int_operand")
                     (match_operand:DI 2 "register_operand")
                     ] UNSPECV_CMDBUF_WR_REG)]
  ""
  ;; We hardcode two extra unused registers per the HW engineers' request
  "tt.rocc.cmdbuf_wr_reg\tx0,%0,%1,%2,x0")
    
(define_insn "riscv_ttrocc_cmdbuf_rd_reg"
  [(unspec_volatile [(match_operand:DI 0 "register_operand")
                     (match_operand:DI 1 "const_int_operand")
                     (match_operand:DI 2 "const_int_operand")
                     ] UNSPECV_CMDBUF_RD_REG)
   (return)]
  ""
  ;; We hardcode three extra unused registers per the HW engineers' request
  "tt.rocc.cmdbuf_rd_reg\tx0,%1,%2,x0,x0")

(define_insn "riscv_ttrocc_cmdbuf_get_vc_space"
  [(unspec_volatile [(match_operand:DI 0 "register_operand")
                     (match_operand:DI 1 "const_int_operand")
                     ] UNSPECV_CMDBUF_GET_VC_SPACE)]
  ""
  "tt.rocc.cmdbuf_get_vc_space\t%1")
   
(define_insn "riscv_ttrocc_cmdbuf_get_vc_space_vc"
  [(unspec_volatile [(match_operand:DI 0 "register_operand")
                     (match_operand:DI 1 "const_int_operand")
                     (match_operand:DI 2 "register_operand")
                     ] UNSPECV_CMDBUF_GET_VC_SPACE_VC)]
  ""
  "tt.rocc.cmdbuf_get_vc_space_vc\t%1,%2")
   
(define_insn "riscv_ttrocc_cmdbuf_wr_sent"
  [(unspec_volatile [(match_operand:DI 0 "register_operand")
                     (match_operand:DI 1 "const_int_operand")
                     ] UNSPECV_CMDBUF_WR_SENT)]
  ""
  "tt.rocc.cmdbuf_wr_sent\t%1")
   
(define_insn "riscv_ttrocc_cmdbuf_wr_sent_trid"
  [(unspec_volatile [(match_operand:DI 0 "register_operand")
                     (match_operand:DI 1 "const_int_operand")
                     (match_operand:DI 2 "register_operand")
                     ] UNSPECV_CMDBUF_WR_SENT_TRID)]
  ""
  "tt.rocc.cmdbuf_wr_sent_trid\t%1,%2")
   
(define_insn "riscv_ttrocc_cmdbuf_tr_ack"
  [(unspec_volatile [(match_operand:DI 0 "register_operand")
                     (match_operand:DI 1 "const_int_operand")
                     ] UNSPECV_CMDBUF_TR_ACK)]
  ""
  "tt.rocc.cmdbuf_tr_ack\t%1")
   
(define_insn "riscv_ttrocc_cmdbuf_tr_ack_trid"
  [(unspec_volatile [(match_operand:DI 0 "register_operand")
                     (match_operand:DI 1 "const_int_operand")
                     (match_operand:DI 2 "register_operand")
                     ] UNSPECV_CMDBUF_TR_ACK_TRID)]
  ""
  "tt.rocc.cmdbuf_tr_ack_trid\t%1,%2")

(define_insn "riscv_ttrocc_cmdbuf_reset"
  [(unspec_volatile [(match_operand:DI 0 "const_int_operand")
                     ] UNSPECV_CMDBUF_RESET)]
  ""
  "tt.rocc.cmdbuf_reset\t%0")

(define_insn "riscv_ttrocc_cmdbuf_idma_get_vc_space"
  [(unspec_volatile [(match_operand:DI 0 "register_operand")
                     (match_operand:DI 1 "const_int_operand")
                     ] UNSPECV_CMDBUF_IDMA_GET_VC_SPACE)]
  ""
  "tt.rocc.cmdbuf_idma_get_vc_space\t%1")
   
(define_insn "riscv_ttrocc_cmdbuf_idma_get_vc_space_vc"
  [(unspec_volatile [(match_operand:DI 0 "register_operand")
                     (match_operand:DI 1 "const_int_operand")
                     (match_operand:DI 2 "register_operand")
                     ] UNSPECV_CMDBUF_IDMA_GET_VC_SPACE_VC)]
  ""
  "tt.rocc.cmdbuf_idma_get_vc_space_vc\t%1,%2")
   
   
(define_insn "riscv_ttrocc_cmdbuf_idma_tr_ack"
  [(unspec_volatile [(match_operand:DI 0 "register_operand")
                     (match_operand:DI 1 "const_int_operand")
                     ] UNSPECV_CMDBUF_IDMA_TR_ACK)]
  ""
  "tt.rocc.cmdbuf_idma_tr_ack\t%1")
   
(define_insn "riscv_ttrocc_cmdbuf_idma_tr_ack_trid"
  [(unspec_volatile [(match_operand:DI 0 "register_operand")
                     (match_operand:DI 1 "const_int_operand")
                     (match_operand:DI 2 "register_operand")
                     ] UNSPECV_CMDBUF_IDMA_TR_ACK_TRID)]
  ""
  "tt.rocc.cmdbuf_idma_tr_ack_trid\t%1,%2")

(define_insn "riscv_ttrocc_cmdbuf_issue_trans"
  [(unspec_volatile [(match_operand:DI 0 "const_int_operand")
                     ] UNSPECV_CMDBUF_ISSUE_TRANS)]
  ""
  "tt.rocc.cmdbuf_issue_trans\t%0")

(define_insn "riscv_ttrocc_cmdbuf_issue_inline_trans"
  [(unspec_volatile [(match_operand:DI 0 "const_int_operand")
                     (match_operand:DI 1 "register_operand")
                     ] UNSPECV_CMDBUF_ISSUE_INLINE_TRANS)]
  ""
  "tt.rocc.cmdbuf_issue_inline_trans\t%0,%1")

(define_insn "riscv_ttrocc_cmdbuf_issue_inline_addr_trans"
  [(unspec_volatile [(match_operand:DI 0 "const_int_operand")
                     (match_operand:DI 1 "register_operand")
                     (match_operand:DI 2 "register_operand")
                     ] UNSPECV_CMDBUF_ISSUE_INLINE_ADDR_TRANS)]
  ""
  "tt.rocc.cmdbuf_issue_inline_addr_trans\t%0,%1,%2")

(define_insn "riscv_ttrocc_cmdbuf_issue_read1_trans"
  [(unspec_volatile [(match_operand:DI 0 "const_int_operand")
                     (match_operand:DI 1 "register_operand")
                     ] UNSPECV_CMDBUF_ISSUE_READ1_TRANS)]
  ""
  "tt.rocc.cmdbuf_issue_read1_trans\t%0,%1")

(define_insn "riscv_ttrocc_cmdbuf_issue_read2_trans"
  [(unspec_volatile [(match_operand:DI 0 "const_int_operand")
                     (match_operand:DI 1 "register_operand")
                     (match_operand:DI 2 "register_operand")
                     ] UNSPECV_CMDBUF_ISSUE_READ2_TRANS)]
  ""
  "tt.rocc.cmdbuf_issue_read2_trans\t%0,%1,%2")

(define_insn "riscv_ttrocc_cmdbuf_issue_write1_trans"
  [(unspec_volatile [(match_operand:DI 0 "const_int_operand")
                     (match_operand:DI 1 "register_operand")
                     ] UNSPECV_CMDBUF_ISSUE_WRITE1_TRANS)]
  ""
  "tt.rocc.cmdbuf_issue_write1_trans\t%0,%1")

(define_insn "riscv_ttrocc_cmdbuf_issue_write2_trans"
  [(unspec_volatile [(match_operand:DI 0 "const_int_operand")
                     (match_operand:DI 1 "register_operand")
                     (match_operand:DI 2 "register_operand")
                     ] UNSPECV_CMDBUF_ISSUE_WRITE2_TRANS)]
  ""
  "tt.rocc.cmdbuf_issue_write2_trans\t%0,%1,%2")
