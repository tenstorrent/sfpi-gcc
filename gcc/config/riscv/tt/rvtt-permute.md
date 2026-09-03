;; Machine description for Tenstorrent SFPU Intrinsics -- cross-lane permutes.
;; Copyright (C) 2022-2026 Tenstorrent Inc.
;; Originated by Paul Keller (pkeller@tenstorrent.com)
;; Rewritten by Nathan Sidwell (nsidwell@tenstorrent.com, nathan@acm.org).

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

;; Cross-lane data movement: sfpswap and its constant splits, the
;; transpose family, indexed swap and gather, and the SFPSHFT2
;; sub-vector copies and shuffles.
(define_insn "rvtt_sfpswap_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand" "0")
	  (match_operand:XTT32SI 3 "reg_or_cstlreg_operand" "1")
          (match_operand:SI    4 "const_int_operand"  "n")
	  ] UNSPECV_SFPSWAP))
   (set (match_operand:XTT32SI 1 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_dup 2)
	  (match_dup 3)
          (match_dup 4)
	  ] UNSPECV_SFPSWAP))]
  "TARGET_XTT_TENSIX"
  "SFPSWAP\t%x2, %x3, %4"
  [(set_attr "type" "tensix")
   (set_attr "xtt_macro_resource" "simple_mad_write")
   (set_attr "xtt_replay" "safe")
   ;; D3 latency-audit extension (2026-08-18, BH/WH; three sources in
   ;; rvtt-cost.md's table): result latency 0 -- [ISA] SFPSWAP.md's
   ;; next-cycle rule is an ACCEPTANCE stall with no result-read
   ;; constraint; [SIM] TENSIX_EXECUTE_SFPSWAP atomic update; [HAND]
   ;; reduce_custom's hardware-proven chained back-to-back dependent
   ;; SFPSWAPs.  The acceptance stall is the separate structural fact
   ;; xtt_next_slot_stall (pricing charges one slot; the interlock
   ;; scheduler refuses next-slot-stall insns as fill participants).
   (set_attr "xtt_result_latency" "1")
   (set_attr "xtt_next_slot_stall" "yes")
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_write_port" "borrows_mad")
   (set_attr "xtt_lreg_read_ops" "13")
   (set_attr "xtt_lreg_write_ops" "4")
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   (set_attr "xtt_macro_encodable" "yes")
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_BH | XTT_DYNAMIC_BUG_QSR)"))])

(define_insn "*rvtt_sfpswap_cst1"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand" "0")
	  (match_operand:XTT32SI 2 "cstlreg_operand" "xc")
          (match_operand:SI    3 "const_int_operand"  "n")
	  (const_int 1)
	  ] UNSPECV_SFPSWAP))]
  "TARGET_XTT_TENSIX"
  "SFPSWAP\t%x1, %x2, %3"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; D3 latency-audit extension (2026-08-18, BH/WH; three sources in
   ;; rvtt-cost.md's table): result latency 0 -- [ISA] SFPSWAP.md's
   ;; next-cycle rule is an ACCEPTANCE stall with no result-read
   ;; constraint; [SIM] TENSIX_EXECUTE_SFPSWAP atomic update; [HAND]
   ;; reduce_custom's hardware-proven chained back-to-back dependent
   ;; SFPSWAPs.  The acceptance stall is the separate structural fact
   ;; xtt_next_slot_stall (pricing charges one slot; the interlock
   ;; scheduler refuses next-slot-stall insns as fill participants).
   (set_attr "xtt_result_latency" "1")
   (set_attr "xtt_next_slot_stall" "yes")
   ;; Same audited SFPSWAP effect envelope as rvtt_sfpswap_int (WH and
   ;; BH functional models are bit-identical; default-LaneConfig
   ;; envelope, the planner refuses config mutation around rows).  Here
   ;; the VC source (operand 2) is a hardware constant register -- every
   ;; cstlreg is L8..L15 (SFPU_CREG_IDX_LWM) -- and SFPSWAP.md drops
   ;; writes to LRegs >= 8 ("if (VC < 8)"), matching the reference simulator
   ;; TENSIX_EXECUTE_SFPSWAP.  The dual write therefore reduces to the
   ;; single VD result tied to operand 0; both sources are read
   ;; (constant-register reads fall outside the allocatable-LREG mask
   ;; domain by construction).  Lane-predicated; never writes CC.
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_write_port" "borrows_mad")
   (set_attr "xtt_lreg_read_ops" "7")
   (set_attr "xtt_lreg_write_ops" "2")
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   (set_attr "xtt_macro_encodable" "yes")
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_BH | XTT_DYNAMIC_BUG_QSR)"))])

(define_split
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 3 "cstlreg_operand")
          (match_operand:SI    4 "const_int_operand")
	  ] UNSPECV_SFPSWAP))
   (set (match_operand:XTT32SI 1 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_dup 2)
	  (match_dup 3)
          (match_dup 4)
	  ] UNSPECV_SFPSWAP))]
  "bool (find_reg_note (insn, REG_UNUSED, operands[1]))
   && !(cstlreg_operand (operands[2], XTT32SImode)
        && find_reg_note (insn, REG_UNUSED, operands[0]))"
  [(set (match_dup 0)
        (unspec_volatile:XTT32SI [
 	  (match_dup 2)
	  (match_dup 3)
          (match_dup 4)
	  (const_int 1)
	  ] UNSPECV_SFPSWAP))])

(define_insn "*rvtt_sfpswap_cst2"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "cstlreg_operand" "xs")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand" "0")
          (match_operand:SI    3 "const_int_operand"  "n")
	  (const_int 2)
	  ] UNSPECV_SFPSWAP))]
  "TARGET_XTT_TENSIX"
  "SFPSWAP\t%x1, %x2, %3"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; D3 latency-audit extension (2026-08-18, BH/WH; three sources in
   ;; rvtt-cost.md's table): result latency 0 -- [ISA] SFPSWAP.md's
   ;; next-cycle rule is an ACCEPTANCE stall with no result-read
   ;; constraint; [SIM] TENSIX_EXECUTE_SFPSWAP atomic update; [HAND]
   ;; reduce_custom's hardware-proven chained back-to-back dependent
   ;; SFPSWAPs.  The acceptance stall is the separate structural fact
   ;; xtt_next_slot_stall (pricing charges one slot; the interlock
   ;; scheduler refuses next-slot-stall insns as fill participants).
   (set_attr "xtt_result_latency" "1")
   (set_attr "xtt_next_slot_stall" "yes")
   ;; Same audited SFPSWAP effect envelope as rvtt_sfpswap_int; here the
   ;; VD operand (operand 1) is a hardware constant register.  The "xs"
   ;; constraint (cstlreg < 12) is exactly SFPSWAP.md's VD execution
   ;; gate ("if (VD < 12 || ...)"), and since every cstlreg is L8..L15
   ;; the VD-side write is architecturally dropped ("if (VD < 8)",
   ;; matching the reference simulator).  The surviving write is the VC result tied to
   ;; operand 0; both sources are read.  Lane-predicated; never writes
   ;; CC.  Default-LaneConfig envelope as for rvtt_sfpswap_int.
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_write_port" "borrows_mad")
   (set_attr "xtt_lreg_read_ops" "7")
   (set_attr "xtt_lreg_write_ops" "2")
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   (set_attr "xtt_macro_encodable" "yes")
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_BH | XTT_DYNAMIC_BUG_QSR)"))])

(define_split
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 2 "cstlreg_operand")
	  (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
          (match_operand:SI    4 "const_int_operand")
	  ] UNSPECV_SFPSWAP))
   (set (match_operand:XTT32SI 1 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_dup 2)
	  (match_dup 3)
          (match_dup 4)
	  ] UNSPECV_SFPSWAP))]
  "find_reg_note (insn, REG_UNUSED, operands[0])
   && !(cstlreg_operand (operands[3], XTT32SImode)
        && find_reg_note (insn, REG_UNUSED, operands[1]))"
  [(set (match_dup 1)
        (unspec_volatile:XTT32SI [
 	  (match_dup 2)
	  (match_dup 3)
          (match_dup 4)
	  (const_int 2)
	  ] UNSPECV_SFPSWAP))])

(define_insn "*rvtt_sfpswap_cst3"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "cstlreg_operand" "xs")
     (match_operand:XTT32SI 1 "cstlreg_operand" "xc")
     (match_operand:SI    2 "const_int_operand"  "n")
     (const_int 3)
  ] UNSPECV_SFPSWAP)]
  "TARGET_XTT_TENSIX"
  "SFPSWAP\t%x0, %x1, %2"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; D3 latency-audit extension (2026-08-18, BH/WH; three sources in
   ;; rvtt-cost.md's table): result latency 0 -- [ISA] SFPSWAP.md's
   ;; next-cycle rule is an ACCEPTANCE stall with no result-read
   ;; constraint; [SIM] TENSIX_EXECUTE_SFPSWAP atomic update; [HAND]
   ;; reduce_custom's hardware-proven chained back-to-back dependent
   ;; SFPSWAPs.  The acceptance stall is the separate structural fact
   ;; xtt_next_slot_stall (pricing charges one slot; the interlock
   ;; scheduler refuses next-slot-stall insns as fill participants).
   (set_attr "xtt_result_latency" "1")
   (set_attr "xtt_next_slot_stall" "yes")
   ;; Same audited SFPSWAP effect envelope as rvtt_sfpswap_int with BOTH
   ;; operands hardware constant registers (VD constrained "xs" < 12,
   ;; SFPSWAP.md's VD execution gate).  Every cstlreg is L8..L15, so
   ;; both architectural writes are dropped ("if (VC < 8)" /
   ;; "if (VD < 8)", matching the reference simulator): under the default-LaneConfig
   ;; envelope this event reads its two constant sources and the lane
   ;; state and writes no allocatable LREG (write mask audited empty; no
   ;; writeback-port occupancy claim).  Never writes CC.
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_read_ops" "4")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   (set_attr "xtt_macro_encodable" "yes")
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_BH | XTT_DYNAMIC_BUG_QSR)"))])

(define_split
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 2 "cstlreg_operand")
	  (match_operand:XTT32SI 3 "cstlreg_operand")
          (match_operand:SI    4 "const_int_operand")
	  ] UNSPECV_SFPSWAP))
   (set (match_operand:XTT32SI 1 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_dup 2)
	  (match_dup 3)
          (match_dup 4)
	  ] UNSPECV_SFPSWAP))]
  "bool (find_reg_note (insn, REG_UNUSED, operands[0]))
   && bool (find_reg_note (insn, REG_UNUSED, operands[1]))"
  [(unspec_volatile:XTT32SI [
     (match_dup 2)
     (match_dup 3)
     (match_dup 4)
     (const_int 3)
     ] UNSPECV_SFPSWAP)])

(define_expand "rvtt_sfpswap"
  [(set (match_operand:XTT64SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI 3 "const_int_operand")
	  ] UNSPECV_SFPSWAP))]
  "TARGET_XTT_TENSIX"
{
  rtx a = gen_reg_rtx (XTT32SImode);
  rtx b = gen_reg_rtx (XTT32SImode);
  emit_insn (gen_rvtt_sfpswap_int
    (a, b, operands[1], operands[2], operands[3]));
  emit_insn (gen_rvtt_sfpconcat2
    (operands[0], a, b));
  DONE;
})

(define_insn "rvtt_sfptransp_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=x0")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 4 "reg_or_cstlreg_operand" "0")
	  (match_operand:XTT32SI 5 "reg_or_cstlreg_operand" "1")
	  (match_operand:XTT32SI 6 "reg_or_cstlreg_operand" "2")
	  (match_operand:XTT32SI 7 "reg_or_cstlreg_operand" "3")
	  ] UNSPECV_SFPTRANSP))
   (set (match_operand:XTT32SI 1 "register_operand" "=x1")
        (unspec_volatile:XTT32SI [
	  (match_dup 4)
	  (match_dup 5)
          (match_dup 6)
          (match_dup 7)
	  ] UNSPECV_SFPTRANSP))
   (set (match_operand:XTT32SI 2 "register_operand" "=x2")
        (unspec_volatile:XTT32SI [
	  (match_dup 4)
	  (match_dup 5)
          (match_dup 6)
          (match_dup 7)
	  ] UNSPECV_SFPTRANSP))
   (set (match_operand:XTT32SI 3 "register_operand" "=x3")
        (unspec_volatile:XTT32SI [
	  (match_dup 4)
	  (match_dup 5)
          (match_dup 6)
          (match_dup 7)
	  ] UNSPECV_SFPTRANSP))]
  "TARGET_XTT_TENSIX"
  "SFPTRANSP"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; DELIBERATELY UNAUDITED (refusing defaults kept).  Architectural
   ;; SFPTRANSP always permutes BOTH banks (SFPTRANSP.md Transpose4(0)
   ;; and Transpose4(4)); this legacy tuple models only the L0-L3 bank,
   ;; so an effect claim here would under-state the write set (the L4-L7
   ;; companion writes are not SETs of this PARALLEL).  Effect audits may
   ;; never under-claim: the complete-write-set form is
   ;; rvtt_sfptransp8_int below, which is the audited one.
   ])

(define_expand "rvtt_sfptransp"
  [(set (match_operand:XTT128SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 4 "reg_or_cstlreg_operand")
	  ] UNSPECV_SFPTRANSP))]
  "TARGET_XTT_TENSIX"
{
  rtx a = gen_reg_rtx (XTT32SImode);
  rtx b = gen_reg_rtx (XTT32SImode);
  rtx c = gen_reg_rtx (XTT32SImode);
  rtx d = gen_reg_rtx (XTT32SImode);

  emit_insn (gen_rvtt_sfptransp_int
    (a, b, c, d, operands[1], operands[2], operands[3], operands[4]));
  emit_insn (gen_rvtt_sfpconcat4
    (operands[0], a, b, c, d));
  DONE;
})

;; IndexEn couples value registers L0--L3 to companion index registers
;; L4--L7.  Model the four results in one PARALLEL and constrain allocation to
;; the twelve legal ordered value pairs.  The matching operands make every
;; result read/write, while the exact register alternatives encode
;; index_reg == value_reg + 4 rather than relying on a post-RA assertion.
(define_insn "rvtt_sfpswap_indexed_int"
  [(set (match_operand:XTT32SI 0 "register_operand"
          "=x0,x0,x0,x1,x1,x1,x2,x2,x2,x3,x3,x3")
        (unspec_volatile:XTT32SI [
          (match_operand:XTT32SI 4 "register_operand"
           "0,0,0,0,0,0,0,0,0,0,0,0")
          (match_operand:XTT32SI 5 "register_operand"
           "1,1,1,1,1,1,1,1,1,1,1,1")
          (match_operand:XTT32SI 6 "register_operand"
           "2,2,2,2,2,2,2,2,2,2,2,2")
          (match_operand:XTT32SI 7 "register_operand"
           "3,3,3,3,3,3,3,3,3,3,3,3")
          (match_operand:SI 8 "const_int_operand"
           "n,n,n,n,n,n,n,n,n,n,n,n")
        ] UNSPECV_SFPSWAP))
   (set (match_operand:XTT32SI 1 "register_operand"
          "=x1,x2,x3,x0,x2,x3,x0,x1,x3,x0,x1,x2")
        (unspec_volatile:XTT32SI [(match_dup 4) (match_dup 5)
                                  (match_dup 6) (match_dup 7)
                                  (match_dup 8)] UNSPECV_SFPSWAP))
   (set (match_operand:XTT32SI 2 "register_operand"
          "=x4,x4,x4,x5,x5,x5,x6,x6,x6,x7,x7,x7")
        (unspec_volatile:XTT32SI [(match_dup 4) (match_dup 5)
                                  (match_dup 6) (match_dup 7)
                                  (match_dup 8)] UNSPECV_SFPSWAP))
   (set (match_operand:XTT32SI 3 "register_operand"
          "=x5,x6,x7,x4,x6,x7,x4,x5,x7,x4,x5,x6")
        (unspec_volatile:XTT32SI [(match_dup 4) (match_dup 5)
                                  (match_dup 6) (match_dup 7)
                                  (match_dup 8)] UNSPECV_SFPSWAP))]
  "TARGET_XTT_TENSIX"
  "SFPSWAP\t%x4, %x5, %8\t# INDEXED R:%x6,%x7"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; D3 latency-audit extension (2026-08-18, BH/WH; three sources in
   ;; rvtt-cost.md's table): result latency 0 -- [ISA] SFPSWAP.md's
   ;; next-cycle rule is an ACCEPTANCE stall with no result-read
   ;; constraint; [SIM] TENSIX_EXECUTE_SFPSWAP atomic update; [HAND]
   ;; reduce_custom's hardware-proven chained back-to-back dependent
   ;; SFPSWAPs.  The acceptance stall is the separate structural fact
   ;; xtt_next_slot_stall (pricing charges one slot; the interlock
   ;; scheduler refuses next-slot-stall insns as fill participants).
   (set_attr "xtt_result_latency" "1")
   (set_attr "xtt_next_slot_stall" "yes")
   ;; Audited multi-result effect envelope (SFPSWAP.md functional model,
   ;; ENABLE_DEST_INDEX leg; the reference simulator's TENSIX_EXECUTE_SFPSWAP agrees).  One
   ;; SFPSWAP event on the Simple sub-unit, LREG writeback borrowing the
   ;; MAD port exactly as the audited rvtt_sfpswap_int envelope.  Under
   ;; LaneConfig.ENABLE_DEST_INDEX the single event conditionally swaps
   ;; the value pair AND unconditionally swaps (on the same per-lane
   ;; ShouldSwap) the companion pair LReg[4+(VC&3)]/LReg[4+(VD&3)]; the
   ;; register alternatives above pin companion == value + 4, so the four
   ;; SETs of this PARALLEL are the complete architectural write set.
   ;; Reads: the four matched sources (operands 4-7).  Writes: the four
   ;; results (operands 0-3).  Lane-predicated (LaneEnabled gates every
   ;; write); never writes CC; no configuration or RWC effect.  No macro
   ;; template capability proof exists for the indexed form -- the
   ;; encodable default (no) stands until one does.
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_write_port" "borrows_mad")
   (set_attr "xtt_lreg_read_ops" "241")
   (set_attr "xtt_lreg_write_ops" "16")
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_BH | XTT_DYNAMIC_BUG_QSR)"))])

(define_expand "rvtt_sfpswap_indexed"
  [(set (match_operand:XTT128SI 0 "register_operand")
        (unspec_volatile:XTT128SI [
          (match_operand:XTT32SI 1 "register_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:XTT32SI 3 "register_operand")
          (match_operand:XTT32SI 4 "register_operand")
          (match_operand:SI 5 "const_int_operand")
        ] UNSPECV_SFPSWAP))]
  "TARGET_XTT_TENSIX"
{
  rtx value_a = gen_reg_rtx (XTT32SImode);
  rtx value_b = gen_reg_rtx (XTT32SImode);
  rtx index_a = gen_reg_rtx (XTT32SImode);
  rtx index_b = gen_reg_rtx (XTT32SImode);
  emit_insn (gen_rvtt_sfpswap_indexed_int
    (value_a, value_b, index_a, index_b, operands[1], operands[2],
     operands[3], operands[4], operands[5]));
  emit_insn (gen_rvtt_sfpconcat4
    (operands[0], value_a, value_b, index_a, index_b));
  DONE;
})

;; SFPTRANSP transforms both architectural banks.  The public tuple carries
;; L0--L3, as before; the second bank remains explicit SETs in the same RTL
;; instruction, so DF/IRA see the L4--L7 uses and definitions even when the C
;; caller observes those results through later fixed-LREG reads.
(define_insn "rvtt_sfptransp8_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=x0")
        (unspec_volatile:XTT32SI [
          (match_operand:XTT32SI 8 "register_operand" "0")
          (match_operand:XTT32SI 9 "register_operand" "1")
          (match_operand:XTT32SI 10 "register_operand" "2")
          (match_operand:XTT32SI 11 "register_operand" "3")
          (match_operand:XTT32SI 12 "register_operand" "4")
          (match_operand:XTT32SI 13 "register_operand" "5")
          (match_operand:XTT32SI 14 "register_operand" "6")
          (match_operand:XTT32SI 15 "register_operand" "7")
        ] UNSPECV_SFPTRANSP))
   (set (match_operand:XTT32SI 1 "register_operand" "=x1")
        (unspec_volatile:XTT32SI [(match_dup 8) (match_dup 9) (match_dup 10) (match_dup 11)
                                  (match_dup 12) (match_dup 13) (match_dup 14) (match_dup 15)] UNSPECV_SFPTRANSP))
   (set (match_operand:XTT32SI 2 "register_operand" "=x2")
        (unspec_volatile:XTT32SI [(match_dup 8) (match_dup 9) (match_dup 10) (match_dup 11)
                                  (match_dup 12) (match_dup 13) (match_dup 14) (match_dup 15)] UNSPECV_SFPTRANSP))
   (set (match_operand:XTT32SI 3 "register_operand" "=x3")
        (unspec_volatile:XTT32SI [(match_dup 8) (match_dup 9) (match_dup 10) (match_dup 11)
                                  (match_dup 12) (match_dup 13) (match_dup 14) (match_dup 15)] UNSPECV_SFPTRANSP))
   (set (match_operand:XTT32SI 4 "register_operand" "=x4")
        (unspec_volatile:XTT32SI [(match_dup 8) (match_dup 9) (match_dup 10) (match_dup 11)
                                  (match_dup 12) (match_dup 13) (match_dup 14) (match_dup 15)] UNSPECV_SFPTRANSP))
   (set (match_operand:XTT32SI 5 "register_operand" "=x5")
        (unspec_volatile:XTT32SI [(match_dup 8) (match_dup 9) (match_dup 10) (match_dup 11)
                                  (match_dup 12) (match_dup 13) (match_dup 14) (match_dup 15)] UNSPECV_SFPTRANSP))
   (set (match_operand:XTT32SI 6 "register_operand" "=x6")
        (unspec_volatile:XTT32SI [(match_dup 8) (match_dup 9) (match_dup 10) (match_dup 11)
                                  (match_dup 12) (match_dup 13) (match_dup 14) (match_dup 15)] UNSPECV_SFPTRANSP))
   (set (match_operand:XTT32SI 7 "register_operand" "=x7")
        (unspec_volatile:XTT32SI [(match_dup 8) (match_dup 9) (match_dup 10) (match_dup 11)
                                  (match_dup 12) (match_dup 13) (match_dup 14) (match_dup 15)] UNSPECV_SFPTRANSP))]
  "TARGET_XTT_TENSIX"
  "SFPTRANSP\t# R:%x8,%x9,%x10,%x11,%x12,%x13,%x14,%x15"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; Audited multi-result effect envelope (SFPTRANSP.md: "Backend
   ;; execution unit: Vector Unit (SFPU), simple sub-unit"; the reference simulator
   ;; TENSIX_EXECUTE_SFPTRANSP agrees).  One event permutes BOTH
   ;; four-register banks -- Transpose4(0) and Transpose4(4) -- so the
   ;; eight SETs of this PARALLEL are the complete architectural write
   ;; set: reads operands 8-15, writes operands 0-7.  Lane-predicated
   ;; (each element write is gated by LaneEnabled); never writes CC; no
   ;; configuration or RWC effect.  No writeback-port claim is on record
   ;; for SFPTRANSP -- the port default (none, refusing) stands until the
   ;; capability tables prove one.  No macro template capability proof
   ;; exists -- the encodable default (no) stands.
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_read_ops" "65281")
   (set_attr "xtt_lreg_write_ops" "256")
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")])

(define_expand "rvtt_sfptransp8"
  [(set (match_operand:XTT128SI 0 "register_operand")
        (unspec_volatile:XTT128SI [
          (match_operand:XTT32SI 1 "register_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:XTT32SI 3 "register_operand")
          (match_operand:XTT32SI 4 "register_operand")
          (match_operand:XTT32SI 5 "register_operand")
          (match_operand:XTT32SI 6 "register_operand")
          (match_operand:XTT32SI 7 "register_operand")
          (match_operand:XTT32SI 8 "register_operand")
        ] UNSPECV_SFPTRANSP))]
  "TARGET_XTT_TENSIX"
{
  rtx result[8];
  for (unsigned i = 0; i != 8; ++i)
    result[i] = gen_reg_rtx (XTT32SImode);
  emit_insn (gen_rvtt_sfptransp8_int
    (result[0], result[1], result[2], result[3],
     result[4], result[5], result[6], result[7],
     operands[1], operands[2], operands[3], operands[4],
     operands[5], operands[6], operands[7], operands[8]));
  emit_insn (gen_rvtt_sfpconcat4
    (operands[0], result[0], result[1], result[2], result[3]));
  DONE;
})

;; Companion-preserving involution bundle, formed only by the
;; transp-involution pass (-mtt-tensix-optimize-transp-involution) from a
;; proven "SFPTRANSP of four freshly, fully loaded L0-L3 operands" shape.
;; Emitted as ONE atomic multi-word instruction:
;;
;;   [SFPENCC all-lanes]   (operand 10 != 0 only: forced lane state)
;;   SFPTRANSP             (scrambles both banks; L0-L3 are dead here)
;;   SFPLOAD  L0, mod0, addr_mode, addr0
;;   SFPLOAD  L1, mod0, addr_mode, addr1
;;   SFPLOAD  L2, mod0, addr_mode, addr2
;;   SFPLOAD  L3, mod0, addr_mode, addr3
;;   SFPTRANSP             (transposes the fresh loads; descrambles L4-L7)
;;
;; Soundness of the four-SET-only write claim (audited against
;; SFPTRANSP.md's Transpose4 functional model and the reference simulator
;; TENSIX_EXECUTE_SFPTRANSP, and SFPLOAD.md / TENSIX_EXECUTE_SFPLOAD):
;; under the all-lanes lane state the bundle's formation proof establishes
;; (or its leading SFPENCC forces), TRANSP-then-TRANSP is the identity
;; permutation on the L4-L7 companion bank -- element (reg B+i, lane
;; j*8+c) swaps with (reg B+j, lane i*8+c) and swaps back -- while the
;; four interposed loads fully overwrite L0-L3, so the second transpose
;; leaves L0-L3 = Transpose4 of the loaded rows and L4-L7 = their
;; incoming values.  Under a PARTIAL lane state the pair is NOT an
;; involution (mixed-enable swap pairs are one-directional), which is why
;; formation must prove or force all-lanes.  The atomicity of the single
;; insn is what makes the mid-bundle scramble of L4-L7 unobservable.
;;
;; The loads must carry the target's architectural no-increment address
;; mode (formation proves it), so the bundle has no RWC effect; the
;; transposes have none architecturally.
(define_insn "rvtt_sfptransp_gather_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=x0")
        (unspec_volatile:XTT32SI [
          (match_operand:SI 4 "const_int_operand" "n")
          (match_operand:SI 5 "const_int_operand" "n")
          (match_operand:SI 6 "const_int_operand" "n")
          (match_operand:SI 7 "const_int_operand" "n")
          (match_operand:SI 8 "const_int_operand" "n")
          (match_operand:SI 9 "const_int_operand" "n")
          (match_operand:SI 10 "const_int_operand" "n")
        ] UNSPECV_SFPTRANSP_GATHER))
   (set (match_operand:XTT32SI 1 "register_operand" "=x1")
        (unspec_volatile:XTT32SI [(match_dup 4) (match_dup 5) (match_dup 6)
                                  (match_dup 7) (match_dup 8) (match_dup 9)
                                  (match_dup 10)] UNSPECV_SFPTRANSP_GATHER))
   (set (match_operand:XTT32SI 2 "register_operand" "=x2")
        (unspec_volatile:XTT32SI [(match_dup 4) (match_dup 5) (match_dup 6)
                                  (match_dup 7) (match_dup 8) (match_dup 9)
                                  (match_dup 10)] UNSPECV_SFPTRANSP_GATHER))
   (set (match_operand:XTT32SI 3 "register_operand" "=x3")
        (unspec_volatile:XTT32SI [(match_dup 4) (match_dup 5) (match_dup 6)
                                  (match_dup 7) (match_dup 8) (match_dup 9)
                                  (match_dup 10)] UNSPECV_SFPTRANSP_GATHER))]
  "TARGET_XTT_TENSIX_BH || TARGET_XTT_TENSIX_WH"
{
  static char buffer[256];
  char *p = buffer;
  if (INTVAL (operands[10]) != 0)
    /* The architectural all-lanes enable word from the capability table
       (the same word the cc_write_all_lanes derivation proves against),
       emitted verbatim.  */
    p += sprintf (p, ".ttinsn\t%u\n\t",
		  (unsigned) rvtt_macro::sfpencc_all_lanes_word ());
  sprintf (p,
	   "SFPTRANSP\n\t"
	   "SFPLOAD\t%%x0, %%4, %%8, %%9\n\t"
	   "SFPLOAD\t%%x1, %%5, %%8, %%9\n\t"
	   "SFPLOAD\t%%x2, %%6, %%8, %%9\n\t"
	   "SFPLOAD\t%%x3, %%7, %%8, %%9\n\t"
	   "SFPTRANSP");
  return buffer;
}
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; Audited effect envelope: writes exactly the four results (the
   ;; companion bank's net effect is the identity, see above); reads no
   ;; LREG; reads CC (every constituent write is lane-gated -- formation
   ;; proves or forces the all-lanes state); the encc-carrying form also
   ;; writes CC (to the architectural all-lanes state).  No configuration
   ;; effect; no RWC effect (no-increment address mode proven at
   ;; formation).  Load-class result latency.
   (set_attr "xtt_subunit" "load")
   (set_attr "xtt_lreg_read_ops" "1")
   (set_attr "xtt_lreg_write_ops" "16")
   (set (attr "xtt_cc_effect")
	(if_then_else (match_test "INTVAL (operands[10]) != 0")
		      (const_string "readwrite") (const_string "read")))
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   || TARGET_XTT_TENSIX_WH")
		      (const_int 1) (const_int 0)))
   (set (attr "length")
	(if_then_else (match_test "INTVAL (operands[10]) != 0")
		      (const_int 28) (const_int 24)))])

(define_expand "rvtt_sfptransp_gather"
  [(set (match_operand:XTT128SI 0 "register_operand")
        (unspec_volatile:XTT128SI [
          (match_operand:SI 1 "const_int_operand")
          (match_operand:SI 2 "const_int_operand")
          (match_operand:SI 3 "const_int_operand")
          (match_operand:SI 4 "const_int_operand")
          (match_operand:SI 5 "const_int_operand")
          (match_operand:SI 6 "const_int_operand")
        ] UNSPECV_SFPTRANSP_GATHER))]
  "TARGET_XTT_TENSIX_BH || TARGET_XTT_TENSIX_WH"
{
  rtx a = gen_reg_rtx (XTT32SImode);
  rtx b = gen_reg_rtx (XTT32SImode);
  rtx c = gen_reg_rtx (XTT32SImode);
  rtx d = gen_reg_rtx (XTT32SImode);
  /* The leading-enable operand is 0 from the builtin path: the forming
     pass materializes any needed all-lanes enable as a separate typed
     SFPENCC covering the whole formation group.  */
  emit_insn (gen_rvtt_sfptransp_gather_int
    (a, b, c, d, operands[1], operands[2], operands[3], operands[4],
     operands[5], operands[6], const0_rtx));
  emit_insn (gen_rvtt_sfpconcat4 (operands[0], a, b, c, d));
  DONE;
})

(define_insn "rvtt_sfpshft2_copy4_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=x0")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 4 "reg_or_cstlreg_operand" "1")
	  (match_operand:SI     7 "const_int_operand" "n")
	  ] UNSPECV_SFPSHFT2_COPY4))
   (set (match_operand:XTT32SI 1 "register_operand" "=x1")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 5 "reg_or_cstlreg_operand" "2")
	  (match_dup 7)
	  ] UNSPECV_SFPSHFT2_COPY4))
   (set (match_operand:XTT32SI 2 "register_operand" "=x2")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 6 "reg_or_cstlreg_operand" "3")
	  (match_dup 7)
	  ] UNSPECV_SFPSHFT2_COPY4))
   (set (match_operand:XTT32SI 3 "register_operand" "=x3")
        (unspec_volatile:XTT32SI [
	  (match_dup 7)
	  ] UNSPECV_SFPSHFT2_COPY4))]
  "TARGET_XTT_TENSIX"
  "SFPSHFT2\t%x0, %x0, 0, %7"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_BH | XTT_DYNAMIC_BUG_QSR)"))])

(define_expand "rvtt_sfpshft2_copy4"
  [(set (match_operand:XTT128SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
	  (match_operand:SI    4 "const_int_operand")
	  ] UNSPECV_SFPSHFT2_COPY4))]
  "TARGET_XTT_TENSIX"
{
  rtx a = gen_reg_rtx (XTT32SImode);
  rtx b = gen_reg_rtx (XTT32SImode);
  rtx c = gen_reg_rtx (XTT32SImode);
  rtx d = gen_reg_rtx (XTT32SImode);

  emit_insn (gen_rvtt_sfpshft2_copy4_int
    (a, b, c, d, operands[1], operands[2], operands[3],
     operands[4]));
  emit_insn (gen_rvtt_sfpconcat4
    (operands[0], a, b, c, d));
  DONE;
})

(define_insn "rvtt_sfpshft2_subvec_copy4_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=x0")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 4 "reg_or_cstlreg_operand" "1")
	  (match_operand:SI    8 "const_int_operand" "n")
	  ] UNSPECV_SFPSHFT2_SUBVEC_COPY4))
   (set (match_operand:XTT32SI 1 "register_operand" "=x1")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 5 "reg_or_cstlreg_operand" "2")
	  (match_dup 8)
	  ] UNSPECV_SFPSHFT2_SUBVEC_COPY4))
   (set (match_operand:XTT32SI 2 "register_operand" "=x2")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 6 "reg_or_cstlreg_operand" "3")
	  (match_dup 8)
	  ] UNSPECV_SFPSHFT2_SUBVEC_COPY4))
   (set (match_operand:XTT32SI 3 "register_operand" "=x3")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 7 "reg_or_cstlreg_operand" "0")
	  (match_dup 8)
	  ] UNSPECV_SFPSHFT2_SUBVEC_COPY4))]
  "TARGET_XTT_TENSIX"
  ;; Missing-comma asm bug fixed: the template printed "SFPSHFT2\t%x0 %x0, ..." which gas
  ;; rejects, so every emission of the CHAINED_COPY4 form failed to
  ;; assemble.  The instruction itself is doc-exact (raw-word probed,
  ;; SFPSHFT2.md Mod1=1 SUBVEC_CHAINED_COPY4); dg twin
  ;; shft2-chained-copy4-assemble-bh.C asserts assembly succeeds.
  "SFPSHFT2\t%x0, %x0, 0, %8"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_BH | XTT_DYNAMIC_BUG_QSR)"))])

(define_expand "rvtt_sfpshft2_subvec_copy4"
  [(set (match_operand:XTT128SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 4 "reg_or_cstlreg_operand")
	  (match_operand:SI     5 "const_int_operand")
	  ] UNSPECV_SFPSHFT2_SUBVEC_COPY4))]
  "TARGET_XTT_TENSIX"
{
  rtx a = gen_reg_rtx (XTT32SImode);
  rtx b = gen_reg_rtx (XTT32SImode);
  rtx c = gen_reg_rtx (XTT32SImode);
  rtx d = gen_reg_rtx (XTT32SImode);

  emit_insn (gen_rvtt_sfpshft2_subvec_copy4_int
    (a, b, c, d, operands[1], operands[2], operands[3], operands[4],
     operands[5]));
  emit_insn (gen_rvtt_sfpconcat4
    (operands[0], a, b, c, d));
  DONE;
})

(define_insn "rvtt_sfpshft2_subvec_shfl1_copy4_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=x0")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 4 "reg_or_cstlreg_operand" "1")
	  (match_operand:SI    8 "const_int_operand" "n")
	  ] UNSPECV_SFPSHFT2_SUBVEC_SHFL1_COPY4))
   (set (match_operand:XTT32SI 1 "register_operand" "=x1")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 5 "reg_or_cstlreg_operand" "2")
	  (match_dup 8)
	  ] UNSPECV_SFPSHFT2_SUBVEC_SHFL1_COPY4))
   (set (match_operand:XTT32SI 2 "register_operand" "=x2")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 6 "reg_or_cstlreg_operand" "3")
	  (match_dup 8)
	  ] UNSPECV_SFPSHFT2_SUBVEC_SHFL1_COPY4))
   (set (match_operand:XTT32SI 3 "register_operand" "=x3")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 7 "reg_or_cstlreg_operand" "xrxc")
	  (match_dup 8)
	  ] UNSPECV_SFPSHFT2_SUBVEC_SHFL1_COPY4))]
  "TARGET_XTT_TENSIX"
  "SFPSHFT2\t%x0, %x7, 0, %8"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_delay" "static")
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_BH | XTT_DYNAMIC_BUG_QSR)"))])

(define_expand "rvtt_sfpshft2_subvec_shfl1_copy4"
  [(set (match_operand:XTT128SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 4 "reg_or_cstlreg_operand")
	  (match_operand:SI     5 "const_int_operand")
	  ] UNSPECV_SFPSHFT2_SUBVEC_SHFL1_COPY4))]
  "TARGET_XTT_TENSIX"
{
  rtx a = gen_reg_rtx (XTT32SImode);
  rtx b = gen_reg_rtx (XTT32SImode);
  rtx c = gen_reg_rtx (XTT32SImode);
  rtx d = gen_reg_rtx (XTT32SImode);

  emit_insn (gen_rvtt_sfpshft2_subvec_shfl1_copy4_int
    (a, b, c, d, operands[1], operands[2], operands[3], operands[4],
     operands[5]));
  emit_insn (gen_rvtt_sfpconcat4
    (operands[0], a, b, c, d));
  DONE;
})

(define_insn "rvtt_sfpshft2_subvec_shfl1_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn,0")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand" "xrxc,xrxc")
	  (match_operand:SI    3 "const_int_operand" "n,n")
	  ] UNSPECV_SFPSHFT2_SUBVEC_SHFL1))]
  "TARGET_XTT_TENSIX"
  "SFPSHFT2\t%x0, %x2, 0, %3"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_delay" "static")
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_BH | XTT_DYNAMIC_BUG_QSR)"))])

(define_insn "rvtt_sfpshft2_subvec_shfl1_dead"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "reg_or_cstlreg_or_noval_operand" "xn,xrxc")
     (match_operand:XTT32SI 1 "reg_or_cstlreg_operand" "xrxc,xrxc")
     (match_operand:SI 2 "const_int_operand" "n,n")
     ] UNSPECV_SFPSHFT2_SUBVEC_SHFL1)]
  "TARGET_XTT_TENSIX"
  "@
   SFPSHFT2\tL8, %x1, 0, %2
   SFPSHFT2\tL8, %x1, 0, %2\t# LV:%x1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_delay" "static")
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_BH | XTT_DYNAMIC_BUG_QSR)"))])

(define_split
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
	  (match_operand:SI    3 "const_int_operand")
	  ] UNSPECV_SFPSHFT2_SUBVEC_SHFL1))]
  "bool (find_reg_note (insn, REG_UNUSED, operands[0]))"
  [(unspec_volatile:XTT32SI [
     (match_dup 1)
     (match_dup 2)
     (match_dup 3)
     ] UNSPECV_SFPSHFT2_SUBVEC_SHFL1)])

(define_expand "rvtt_sfpshft2_subvec_shfl1"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
	  (match_operand:SI    2 "const_int_operand")
	  ] UNSPECV_SFPSHFT2_SUBVEC_SHFL1))]
  "TARGET_XTT_TENSIX"
{
  emit_insn (gen_rvtt_sfpshft2_subvec_shfl1_lv
    (operands[0], rvtt_gen_rtx_noval (XTT32SImode), operands[1],
     operands[2]));
  DONE;
})

(define_expand "rvtt_sfpshft2_subvec_shfl1_lv"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
	  (match_operand:SI    3 "const_int_operand")
	  ] UNSPECV_SFPSHFT2_SUBVEC_SHFL1))]
  "TARGET_XTT_TENSIX"
{
  if (TARGET_XTT_TENSIX_WH && INTVAL (operands[3]) == SFPSHFT2_MOD1_SUBVEC_SHFLSHR1)
    {
      // WH_B0 HW bug (issue #3240): the shftr version of the insn doesn't set the
      // value shifted into place to 0 but instead uses the previous value (eg,
      // from a ror) Here we clear that value to 0 by rotating in the 0 register

      emit_insn (gen_rvtt_sfpshft2_subvec_shfl1_dead
        (rvtt_gen_rtx_noval (XTT32SImode),
	 rvtt_gen_rtx_creg (XTT32SImode, CREG_IDX_0),
	 GEN_INT (SFPSHFT2_MOD1_SUBVEC_SHFLROR1)));
    }
  emit_insn (gen_rvtt_sfpshft2_subvec_shfl1_int
    (operands[0], operands[1], operands[2],
     operands[3]));
  DONE;
})

