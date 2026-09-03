;; Machine description for Tenstorrent SFPU Intrinsics -- configuration access.
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

;; Configuration-register access: sfpreadconfig, sfpwriteconfig
;; and the immediate-form SFPCONFIG.
(define_expand "rvtt_sfpreadconfig"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "const_int_operand" "n")
	  ] UNSPECV_SFPCONFIG))]
  "TARGET_XTT_TENSIX_BH_QSR"
  {
    emit_insn (gen_rvtt_sfpreadconfig_lv
      (operands[0], rvtt_gen_rtx_noval (XTT32SImode), operands[1]));
    DONE;
  })

(define_insn "rvtt_sfpreadconfig_lv"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn,0")
          (match_operand:SI 2 "const_int_operand" "n,n")
	  ] UNSPECV_SFPCONFIG))]
  "TARGET_XTT_TENSIX_BH_QSR"
  "@
   SFPMOV\t%x0, L%2, 8\t# CFG:%2
   SFPMOV\t%x0, L%2, 8\t# LV:%x1 CFG:%2"
  [(set_attr "xtt_subunit" "cfg")
   (set_attr "xtt_lreg_read_ops" "3")
   (set_attr "xtt_lreg_write_ops" "2")
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "read")
   (set_attr "xtt_config_dest_op" "3")
   (set_attr "xtt_rwc_effect" "none")
   (set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")])

(define_insn "rvtt_sfpwriteconfig_v"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "register_operand"   "x0")
     (match_operand:SI   1 "const_int_operand"  "n")
     ] UNSPECV_SFPCONFIG)]
  "TARGET_XTT_TENSIX"
  "SFPCONFIG\t%1, 0, 0\t# R:%x0 CFG:%1"
  [(set_attr "xtt_subunit" "cfg")
   (set_attr "xtt_lreg_read_ops" "2")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "dest")
   (set_attr "xtt_config_dest_op" "2")
   (set_attr "xtt_rwc_effect" "none")
   (set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_BH | XTT_DYNAMIC_BUG_QSR)"))])

;; Immediate-form SFPCONFIG.  Operands:
;; 0 = Imm16, 1 = destination (VD field), 2 = Mod1.  Assembler operand
;; order is (VD, Imm16, Mod1) -- binutils riscv-opc-sfpu-insns.h
;; "J4mf9ff,J8u16,J0md7ff" -- matching the hand kernels' TTI_SFPCONFIG
;; (Imm16, VD, Mod1) words bit-for-bit (e.g. TTI_SFPCONFIG(0x4,0xF,1)
;; == 0x910004f1).
;;
;; Effect audit (SFPCONFIG.md, WormholeB0 tree, shared verbatim by the
;; BlackholeA0 tree; functional model read 2026-08-21):
;;  - Frontend checking pins Mod1 to {1,3,5,7} (MOD1_IMM16_IS_VALUE set,
;;    MOD1_IMM16_IS_LANE_MASK clear) and the destination to LaneConfig
;;    (15) -- gimple-rvtt-check.cc, named refusal
;;    sfpconfig-imm-dest-unaudited.  Within that envelope the functional
;;    model's case-15 arm reads Imm16 only: NO LReg read (that is the
;;    point of the form) and NO LReg write (the audited case-15 fact:
;;    SFPCONFIG dest-15 words never touch LReg[11..14] -- the
;;    LaneConfig word audit; both reference-simulator models agree).  Hence
;;    lreg_read_ops/write_ops = empty audited masks (bias 1), not the
;;    refusing 0.
;;  - cc_effect read: the write is gated per column by
;;    UseLaneFlagsForLaneEnable/LaneFlags (functional model head), the
;;    same envelope as the value form above.
;;  - config_effect dest with the destination operand at position 1
;;    (+1-biased attribute = 2), identical decoding position to the
;;    value form.
;;  - replay barrier + the BH/QSR dynamic-bug envelope mirror the value
;;    form: LaneConfig bits (DISABLE_BACKDOOR_LOAD, ENABLE_DEST_INDEX,
;;    EXCHANGE_SRCB_SRCC, ROW_MASK) change the semantics of neighbouring
;;    instructions, and SFPCONFIG.md's scheduling note allows the next
;;    instruction to observe either value of DISABLE_BACKDOOR_LOAD.
;;  - No LReg result exists; xtt_result_latency keeps the unaudited
;;    default, exactly as the value form does.
(define_insn "rvtt_sfpconfig_i"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI   0 "const_int_operand"  "n")
     (match_operand:SI   1 "const_int_operand"  "n")
     (match_operand:SI   2 "const_int_operand"  "n")
     ] UNSPECV_SFPCONFIG)]
  "TARGET_XTT_TENSIX"
  "SFPCONFIG\t%1, %0, %2\t# CFG:%1"
  [(set_attr "xtt_subunit" "cfg")
   (set_attr "xtt_lreg_read_ops" "1")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "dest")
   (set_attr "xtt_config_dest_op" "2")
   (set_attr "xtt_rwc_effect" "none")
   (set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_BH | XTT_DYNAMIC_BUG_QSR)"))])

