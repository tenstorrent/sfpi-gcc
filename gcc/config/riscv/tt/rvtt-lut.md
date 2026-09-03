;; Machine description for Tenstorrent SFPU Intrinsics -- look-up tables.
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

;; Look-up-table words: sfplut and the fp32 LUT variants.
(define_insn "rvtt_sfplut"
  [(set (match_operand:XTT32SI 0 "register_operand" "=x3")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "x0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "x1")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "x2")
          (match_operand:XTT32SI 4 "reg_or_cstlreg_operand"  "0")
          (match_operand:SI    5 "const_int_operand" "n")
	  ] UNSPECV_SFPLUT))]
  "TARGET_XTT_TENSIX"
  "SFPLUT\t%x0, %5\t# R:%x1,%x2,%x3,%x4"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_delay" "dynamic")
   ;; Effect/latency audit: SFPLUT.md (BH+WH,
   ;; identical functional models) reads LReg[0..2] (coefficient table)
   ;; and LReg[3] (input, the tied destination), lane-predicated write
   ;; to the destination, no CC write, no configuration, RWC, or Dst
   ;; access; MAD sub-unit; instruction scheduling "as per SFPMAD" ->
   ;; result latency 1 (the reference simulator's TENSIX_EXECUTE_SFPLUT matches
   ;; the model).  Audited
   ;; envelope: mod0 in {0, SGN_RETAIN=4} on BH/WH only --
   ;; INDIRECT_VD (mod0 & 8) redirects the write through LReg[7] and
   ;; keeps every refusing default (position masks cannot express it).
   (set (attr "xtt_subunit")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[5])
				   && (INTVAL (operands[5]) == 0
				       || INTVAL (operands[5]) == 4)")
		      (const_string "mad") (const_string "none")))
   (set (attr "xtt_lreg_read_ops")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[5])
				   && (INTVAL (operands[5]) == 0
				       || INTVAL (operands[5]) == 4)")
		      (const_int 31) (const_int 0)))
   (set (attr "xtt_lreg_write_ops")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[5])
				   && (INTVAL (operands[5]) == 0
				       || INTVAL (operands[5]) == 4)")
		      (const_int 2) (const_int 0)))
   (set (attr "xtt_cc_effect")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[5])
				   && (INTVAL (operands[5]) == 0
				       || INTVAL (operands[5]) == 4)")
		      (const_string "read") (const_string "unknown")))
   (set (attr "xtt_config_effect")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[5])
				   && (INTVAL (operands[5]) == 0
				       || INTVAL (operands[5]) == 4)")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_rwc_effect")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[5])
				   && (INTVAL (operands[5]) == 0
				       || INTVAL (operands[5]) == 4)")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[5])
				   && (INTVAL (operands[5]) == 0
				       || INTVAL (operands[5]) == 4)")
		      (const_int 2) (const_int 0)))
   ;; Lane-gated consumer (typed-effect attributes).
   (set_attr "xtt_lane_gated" "yes")])

(define_insn_and_split "rvtt_sfplutfp32_3r"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand"  "x0")
          (match_operand:XTT32SI 2 "register_operand"  "x1")
          (match_operand:XTT32SI 3 "register_operand"  "x2")
          (match_operand:XTT32SI 4 "register_operand"  "x3")
          (match_operand:SI    5 "const_int_operand" "n")
	  ] UNSPECV_SFPLUTFP32_3R))
        (clobber (match_scratch:XTT32SI 6 "=x7"))]
  "TARGET_XTT_TENSIX"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  // The dst register is determined by the value in L7,
  // so we need to emit a loadi to L7 first. How pleasant.
  emit_insn (gen_rvtt_sfploadi_lv_int
    (operands[6], const0_rtx, const0_rtx, const0_rtx,
    GEN_INT (REGNO (operands[0]) - SFPU_REG_FIRST),
    rvtt_gen_rtx_noval (XTT32SImode),
    rvtt_gen_rtx_noval (XTT32SImode),
    GEN_INT (SFPLOADI_MOD0_USHORT)));
  emit_insn (gen_rvtt_sfplutfp32_3r_split
    (operands[0], operands[1], operands[2], operands[3],
     operands[4], operands[5], operands[6]));
  DONE;
}
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_delay" "dynamic")
   ;; Lane-gated consumer (typed-effect attributes).
   (set_attr "xtt_lane_gated" "yes")])

(define_insn "rvtt_sfplutfp32_3r_split"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand"  "x0")
          (match_operand:XTT32SI 2 "register_operand"  "x1")
          (match_operand:XTT32SI 3 "register_operand"  "x2")
          (match_operand:XTT32SI 4 "register_operand"  "x3")
          (match_operand:SI    5 "const_int_operand" "n")
          (match_operand:XTT32SI 6 "register_operand"  "x7")
	  ] UNSPECV_SFPLUTFP32_3R))]
  "TARGET_XTT_TENSIX && reload_completed"
  "SFPLUTFP32\t%x0, %5\t# R:%x1,%x2,%x3,%x4,%x6"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_delay" "dynamic")
   ;; Lane-gated consumer (typed-effect attributes).
   (set_attr "xtt_lane_gated" "yes")])

(define_insn "rvtt_sfplutfp32_6r"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "x0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "x1")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "x2")
          (match_operand:XTT32SI 4 "reg_or_cstlreg_operand"  "x4")
          (match_operand:XTT32SI 5 "reg_or_cstlreg_operand"  "x5")
          (match_operand:XTT32SI 6 "reg_or_cstlreg_operand"  "x6")
          (match_operand:XTT32SI 7 "reg_or_cstlreg_operand"  "x3")
          (match_operand:SI    8 "const_int_operand" "n")
	  ] UNSPECV_SFPLUTFP32_6R))]
  "TARGET_XTT_TENSIX"
  "SFPLUTFP32\t%x0, %8\t# R:%x1,%x2,%x3,%x4,%x5,%x6,%x7"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_delay" "dynamic")
   ;; Lane-gated consumer (typed-effect attributes).
   (set_attr "xtt_lane_gated" "yes")])

