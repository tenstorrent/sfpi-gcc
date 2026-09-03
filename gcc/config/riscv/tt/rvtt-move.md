;; Machine description for Tenstorrent SFPU Intrinsics -- vector moves.
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

;; The 32-bit SFPU vector move carrier (movxtt32si) and the
;; sfpassign copy forms.
(define_expand "movxtt32si"
  [(set (match_operand:XTT32SI 0 "")
	(match_operand:XTT32SI 1 ""))]
  "TARGET_XTT_TENSIX"
{
  if (riscv_legitimize_move (GET_MODE (operands[0]), operands[0], operands[1]))
    DONE;
})

;; the simple set must accept reg-movs, loads and stores. You can't
;; break this apart otherwise reload blows up when trying to spill/fill

(define_insn "rvtt_sfpassign"
  [(set (match_operand:XTT32SI 0 "nonimmediate_operand" "=xr,xr,m")
        (match_operand:XTT32SI 1 "nonimmediate_or_cstlreg_operand" "xrxc,m,xrxc"))]
  "TARGET_XTT_TENSIX
   && (register_operand (operands[0], XTT32SImode)
       || reg_or_cstlreg_operand (operands[1], XTT32SImode))"
  {
    if (!which_alternative)
      return "SFPMOV\t%0, %x1, 2";
     rvtt_mov_error (insn, which_alternative == 1);
     return which_alternative == 1 ? "BADLOAD\t%x0, %1" :"BADSTORE\t%x1, %0";
  }
  ;; Effect audit (D3 latency audit, WH/BH): the surviving alternative
  ;; is the all-lanes SFPMOV mod-2 copy (the reference simulator's TENSIX_EXECUTE_SFPMOV
  ;; mod 2 forces the full lane mask): reads operand 1, writes every
  ;; lane of operand 0, no CC access, configuration, or counter effect.
  ;; S1 Simple; result latency 0 (Simple chains step one slot; the
  ;; frozen calendars and hand kernels consume Simple results
  ;; back-to-back).  The BADLOAD/BADSTORE alternatives are error paths.
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set (attr "xtt_subunit")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   || TARGET_XTT_TENSIX_WH")
		      (const_string "simple") (const_string "none")))
   (set (attr "xtt_lreg_read_ops")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   || TARGET_XTT_TENSIX_WH")
		      (const_int 3) (const_int 0)))
   (set (attr "xtt_lreg_write_ops")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   || TARGET_XTT_TENSIX_WH")
		      (const_int 2) (const_int 0)))
   (set (attr "xtt_cc_effect")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   || TARGET_XTT_TENSIX_WH")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_config_effect")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   || TARGET_XTT_TENSIX_WH")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_rwc_effect")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   || TARGET_XTT_TENSIX_WH")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   || TARGET_XTT_TENSIX_WH")
		      (const_int 1) (const_int 0)))])

(define_expand "rvtt_sfpassign_lv"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
	  ] UNSPECV_SFPASSIGN))]
  "TARGET_XTT_TENSIX"
  {
  })

(define_insn_and_split "*rvtt_sfpassign_lv_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn,0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand" "xrxc,xrxc")
	  ] UNSPECV_SFPASSIGN))]
  "TARGET_XTT_TENSIX"
  "@
   #
   SFPMOV\t%x0, %x2, 0\t# LV:%x1"
  "&& (noval_operand (operands[1], GET_MODE (operands[1]))
       || rtx_equal_p (operands[1], operands[2])
       || find_reg_note (insn, REG_UNUSED, operands[0]))"
  [(set (match_dup 0) (match_dup 2))]
  {
    // Setting it to a normal mov will leave DCE to deal with
    // the REG_UNUSED case, that's simpler than redetecting here.
  }
  ;; Audited (CC-template extension): the surviving alternative is
  ;; the lane-predicated SFPMOV merge (result tied to the live value,
  ;; enabled lanes take the source).  Simple unit, reads CC, reads
  ;; operands 1 (live, tied to 0) and 2, writes operand 0; no config or
  ;; counter effect.  This is the lane-merge shape the macro planner
  ;; coalesces into the predicated-overwrite select calendar.
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_write_port" "shared_simple_round")
   (set_attr "xtt_lreg_read_ops" "7")
   (set_attr "xtt_lreg_write_ops" "2")
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   ;; D3 latency audit: S1 Simple; Simple dependence chains step one
   ;; slot (frozen signbit/cast-round calendars; hand exp kernel):
   ;; result latency 0.
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   || TARGET_XTT_TENSIX_WH")
		      (const_int 1) (const_int 0)))])

