;; Machine description for Tenstorrent SFPU Intrinsics -- ordered comparisons.
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

;; The ordered-comparison (gt/le) family in value, no-value and
;; CC-only forms.
(define_int_iterator rvtt_gtle_op [
  UNSPECV_SFPGT
  UNSPECV_SFPLE
  ])
(define_int_attr rvtt_gtle_name [
  (UNSPECV_SFPGT "gt")
  (UNSPECV_SFPLE "le")
  ])
(define_int_attr rvtt_gtle_insn [
  (UNSPECV_SFPGT "GT")
  (UNSPECV_SFPLE "LE")
  ])

(define_expand "rvtt_sfp<rvtt_gtle_name>"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
          (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "const_int_operand")
          ] rvtt_gtle_op))]
  "TARGET_XTT_TENSIX_BH_QSR"
  {
    emit_insn (gen_rvtt_sfp<rvtt_gtle_name>_lv
      (operands[0], rvtt_gen_rtx_noval (XTT32SImode),
       operands[1], operands[2], operands[3]));
    DONE;
  })

(define_expand "rvtt_sfp<rvtt_gtle_name>_lv"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
          (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
          (match_operand:SI    4 "const_int_operand")
          ] rvtt_gtle_op))]
  "TARGET_XTT_TENSIX_BH_QSR"
  {
    if (!(INTVAL (operands[4]) & SFPGTLE_MOD1_SET_DEST))
      {
        emit_insn (gen_rvtt_sfp<rvtt_gtle_name>_nv
          (operands[2], operands[3], operands[4]));
        emit_insn (gen_rvtt_sfpassign_lv (operands[0], operands[1], operands[2]));
        DONE;
      }
  })

(define_insn_and_rewrite "*rvtt_sfp<rvtt_gtle_name>_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr,xr")
        (unspec_volatile:XTT32SI [
          (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_or_omit_operand"  "xn,xo,xrxc")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "0,0,0")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "xrxc,xrxc,xrxc")
          (match_operand:SI    4 "const_int_operand" "n,n,n")
          ] rvtt_gtle_op))]
  "TARGET_XTT_TENSIX_BH_QSR"
  "@
   SFP<rvtt_gtle_insn>\t%x0, %x3, 0, %4
   SFP<rvtt_gtle_insn>\t%x0, %x3, 0, %4\t# LV:%x2
   #"
  "&& true"
  {
    if (find_reg_note (curr_insn, REG_UNUSED, operands[0]))
      {
        emit_insn (gen_rvtt_sfp<rvtt_gtle_name>_nv
          (operands[2], operands[3],
           GEN_INT (INTVAL (operands[4]) & ~SFPGTLE_MOD1_SET_DEST)));
        DONE;
      }

    if (noval_or_omit_operand (operands[1], GET_MODE (operands[1])))
      FAIL; // nothing to change

    rvtt_merge_lv_src (&operands[1], &operands[2]);
  }
  ;; Effect audit (BH, pure SET_VD form mod1 == 8 only): three sources --
  ;; (1) ISA spec (docs/tensix_instruction_set_architecture.md, SFPGT/
  ;;     SFPLE): "simple sub-unit"; SET_VD writes -1/0 into VD under
  ;;     LaneEnabled; LaneFlags written only under SFPGT_MOD1_SET_CC and
  ;;     the flag stack only under MUTATE_STACK -- neither in mod 8;
  ;; (2) the reference simulator's TENSIX_EXECUTE_SFPGT/SFPLE (mod1==8 arm): reads the
  ;;     tied destination and lreg_c, lane-writes the destination mask,
  ;;     no CC write, configuration, or counter effect;
  ;; (3) the hardware-proven hand exp kernel issues SFPGT inside the
  ;;     poly-MAD chain's shadow with its consumer SFPAND in the S1
  ;;     Simple column -- the same one-slot Simple dependence stepping
  ;;     whose latency-0 the SFPAND->SFPSETEXP back-to-back audit
  ;;     already carries.
  ;; Every other mod1 (CC-setting and stack-mutating forms) keeps the
  ;; refusing defaults, as does every non-BH target.
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; D3 latency-audit extension (2026-08-18, BH mod1 == 8 arm only;
   ;; three sources in rvtt-cost.md's table): result latency 0 -- [ISA]
   ;; SFPGT.md/SFPLE.md carry no next-cycle result-read rule; [SIM]
   ;; immediate lane-write of the tied destination; [HAND] the hand exp
   ;; kernel's SFPGT->SFPAND one-slot Simple stepping cited by the
   ;; mod-8 effect audit above.  Every other mod keeps the refusing
   ;; default.
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   && INTVAL (operands[4]) == 8")
		      (const_int 1) (const_int 0)))
   (set (attr "xtt_subunit")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   && INTVAL (operands[4]) == 8")
		      (const_string "simple") (const_string "none")))
   (set (attr "xtt_lreg_read_ops")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   && INTVAL (operands[4]) == 8")
		      (const_int 16) (const_int 0)))
   (set (attr "xtt_lreg_write_ops")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   && INTVAL (operands[4]) == 8")
		      (const_int 2) (const_int 0)))
   (set (attr "xtt_cc_effect")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   && INTVAL (operands[4]) == 8")
		      (const_string "read") (const_string "unknown")))
   (set (attr "xtt_config_effect")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   && INTVAL (operands[4]) == 8")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_rwc_effect")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   && INTVAL (operands[4]) == 8")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   && INTVAL (operands[4]) == 8")
		      (const_int 1) (const_int 0)))])

(define_insn "rvtt_sfp<rvtt_gtle_name>_nv"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "reg_or_cstlreg_operand"  "xrxc")
     (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "xrxc")
     (match_operand:SI    2 "const_int_operand" "n")
     ] rvtt_gtle_op)]
  "TARGET_XTT_TENSIX_BH_QSR"
  "SFP<rvtt_gtle_insn>\t%x0, %x1, 0, %2"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")])

;; Pure SET_CC comparison form (BH mod1 == 1, fixed in the template):
;; LaneFlags = (VD <op> VC) for enabled lanes, no LREG write, no flag
;; stack effect.  Emitted only by the -mtt-tensix-optimize-native-compare
;; float compare lowering (rvtt_emit_sfpxfcmps/v GT/LE arms) with VC the
;; constant +0.0 register.
;; Effect audit (BH, SET_CC form mod1 == 1 only): three sources --
;; (1) ISA spec (tt-isa-documentation BlackholeA0 SFPGT.md/SFPLE.md):
;;     "simple sub-unit"; with Mod1 == SET_CC only LaneFlags is written,
;;     under LaneEnabled (disabled lanes keep their flags); VD and VC are
;;     read; no flag-stack, configuration, or counter effect;
;; (2) the reference simulator's TENSIX_EXECUTE_SFPGT/SFPLE (instr_mod1 == 1 arm): reads
;;     lreg_dest and lreg_c, lane-writes only the cc mask under the
;;     current enables -- the identical read-modify-write flag shape the
;;     SFPSETCC "readwrite" audit carries;
;; (3) the hardware-proven hand corpus issues SFPGT mod1 == 1 in the
;;     rounding_ops floor/ceil fixups and softmax_k with its dependent
;;     predicated consumer in the next Simple slot.
;; Every other mod1 (SET_VD and stack-mutating forms) keeps its own
;; pattern; the stack-mutate mods stay unemitted (sim fail-closed).
(define_insn "rvtt_sfp<rvtt_gtle_name>_cc"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "register_operand"  "xr")
     (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "xrxc")
     ] rvtt_gtle_op)]
  "TARGET_XTT_TENSIX_BH"
  "SFP<rvtt_gtle_insn>\t%x0, %x1, 0, 1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_read_ops" "4")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "readwrite")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")])

