;; Machine description for Tenstorrent SFPU Intrinsics -- loads and stores.
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

;; Dst load and store words: sfploadi, sfpload and its variants
;; (discard destination, source-counted form, the macro launches,
;; and the field-operand owned SETC16 word), and the sfpstore
;; forms.
(define_expand "rvtt_sfploadi"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec:XTT32SI [
	  (match_operand:SI 1 "address_operand")
          (match_operand:SI 2 "reg_or_const_int_operand")
          (match_operand:SI 3 "reg_or_0_operand")
          (match_operand:SI 4 "const_int_operand")
          (match_operand:SI 5 "const_int_operand")
	  ] UNSPECV_SFPLOADI))]
  "TARGET_XTT_TENSIX"
{
  emit_insn (gen_rvtt_sfploadi_lv
    (operands[0], operands[1], rvtt_gen_rtx_noval (XTT32SImode),
     operands[2], operands[3], operands[4], operands[5]));
  DONE;
})

(define_expand "rvtt_sfploadi_lv"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_0_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] UNSPECV_SFPLOADI))]
  "TARGET_XTT_TENSIX"
{
  auto mem = const0_rtx;
  auto opc = const0_rtx;
  auto enc = const0_rtx;
  auto imm = operands[3];
  if (!CONST_INT_P (imm))
    {
      mem = gen_rtx_MEM (SImode, operands[1]);
      int op
        = TARGET_XTT_TENSIX_WH  ? TT_OP_WH_SFPLOADI (0, INTVAL (operands[6]), 0)
        : TARGET_XTT_TENSIX_BH  ? TT_OP_BH_SFPLOADI (0, INTVAL (operands[6]), 0)
        : TARGET_XTT_TENSIX_QSR ? TT_OP_QSR_SFPLOADI (0, INTVAL (operands[6]), 0)
        : (gcc_unreachable (), 0);
      opc = GEN_INT (op);
      enc = GEN_INT (rvtt_synth (UINTVAL (operands[5])).src_shift (0).dst_shift (20));
      imm = operands[4];
    }

  emit_insn (gen_rvtt_sfploadi_lv_int
    (operands[0], mem, opc, enc, imm,
     rvtt_gen_rtx_noval (XTT32SImode),
     operands[2], operands[6]));
  DONE;
})

(define_insn "rvtt_sfploadi_lv_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr,xr,xr")
        (unspec_volatile:XTT32SI [
          (match_operand:SI    1 "mem_or_0_operand" "J,J,m,m")
          (match_operand:SI    2 "const_int_operand" "J,J,n,n") ;; opcode
          (match_operand:SI    3 "const_int_operand" "J,J,n,n") ;; id, src & dst shifts
          (match_operand:SI    4 "reg_or_const_int_operand" "n,n,r,r") ;; imm or insn
          (match_operand:XTT32SI 5 "noval_operand" "xn,xn,xn,xn") ;; src (none)
          (match_operand:XTT32SI 6 "reg_or_cstlreg_or_noval_operand" "xn,0,xn,0") ;; lv
          (match_operand:SI    7 "const_int_operand" "n,n,n,n")
	  ] UNSPECV_SFPLOADI))
   (clobber (match_scratch:SI  8 "=X,X,&r,&r"))]
  "TARGET_XTT_TENSIX"
  {
    return rvtt_synth::pattern (which_alternative >> 1,
       which_alternative & 1
       ? "SFPLOADI\t%x0, %4, %7\t# LV:%x6"
       : "SFPLOADI\t%x0, %4, %7",
      operands, true, 8);
  }
  ;; Effect audit (D3 latency audit, WH/BH): the reference simulator
  ;; TENSIX_EXECUTE_SFPLOADI writes the destination's enabled lanes for
  ;; mod0 0-8 and 10 (8/10 are the half-word merges, reading the tied
  ;; live value), touches no CC bit, no configuration word and no
  ;; counter; SFPLOADI.md carries no next-cycle constraint, and the
  ;; hardware-proven hand exp kernel (ckernel_sfpu_exp.h) consumes its
  ;; in-body SFPLOADI one slot later (SFPSWAP reads LREG1 back-to-back):
  ;; result latency 0.  Mod0 9 and >10 are UndefinedBehavior in the
  ;; simulator and keep the refusing defaults.  Sub-unit placement is
  ;; not in the S1 legality table and stays unclaimed (none).
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set (attr "xtt_lreg_read_ops")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[7])
				   && (IN_RANGE (INTVAL (operands[7]), 0, 8)
				       || INTVAL (operands[7]) == 10)")
		      (const_int 98) (const_int 0)))
   (set (attr "xtt_lreg_write_ops")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[7])
				   && (IN_RANGE (INTVAL (operands[7]), 0, 8)
				       || INTVAL (operands[7]) == 10)")
		      (const_int 2) (const_int 0)))
   (set (attr "xtt_cc_effect")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[7])
				   && (IN_RANGE (INTVAL (operands[7]), 0, 8)
				       || INTVAL (operands[7]) == 10)")
		      (const_string "read") (const_string "unknown")))
   (set (attr "xtt_config_effect")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[7])
				   && (IN_RANGE (INTVAL (operands[7]), 0, 8)
				       || INTVAL (operands[7]) == 10)")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_rwc_effect")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[7])
				   && (IN_RANGE (INTVAL (operands[7]), 0, 8)
				       || INTVAL (operands[7]) == 10)")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[7])
				   && (IN_RANGE (INTVAL (operands[7]), 0, 8)
				       || INTVAL (operands[7]) == 10)")
		      (const_int 1) (const_int 0)))
   ;; Migrated effect-override row (now typed-effect attributes): lane-predicated LREG
   ;; immediate materialization, never touches CC; lane-gated consumer.
   (set_attr "xtt_lane_local" "yes")
   (set_attr "xtt_cc_write" "no")
   (set_attr "xtt_lane_gated" "yes")])

(define_expand "rvtt_sfpload"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
          (match_operand:SI 1 "reg_or_0_operand")
          (match_operand:SI 2 "reg_or_const_int_operand")
          (match_operand:SI 3 "reg_or_0_operand")
          (match_operand:SI 4 "const_int_operand")
          (match_operand:SI 5 "const_int_operand")
          (match_operand:SI 6 "const_int_operand")
          ] UNSPECV_SFPLOAD))]
  "TARGET_XTT_TENSIX"
{
  emit_insn (gen_rvtt_sfpload_lv
    (operands[0], operands[1], rvtt_gen_rtx_noval (XTT32SImode), operands[2],
     operands[3], operands[4], operands[5], operands[6]));
  DONE;
})

;; Load into the architectural discard destination L8.  Unlike an ordinary
;; sfpload this has no allocatable SFPU result, which keeps the fixed encoding
;; visible to late RTL passes without manufacturing a dead L0--L7 value.
(define_expand "rvtt_sfploaddiscard"
  [(unspec_volatile:SI [
     (match_operand:SI 0 "const_int_operand")
     (match_operand:SI 1 "const_int_operand")
     (match_operand:SI 2 "const_int_operand")
     ] UNSPECV_SFPLOADDISCARD)]
  "TARGET_XTT_TENSIX"
{
  int op
    = TARGET_XTT_TENSIX_WH
      ? TT_OP_WH_SFPLOAD (8, INTVAL (operands[1]), INTVAL (operands[2]),
			  INTVAL (operands[0]))
    : TARGET_XTT_TENSIX_BH
      ? TT_OP_BH_SFPLOAD (8, INTVAL (operands[1]), INTVAL (operands[2]),
			  INTVAL (operands[0]))
    : TARGET_XTT_TENSIX_QSR
      ? TT_OP_QSR_SFPLOAD (8, INTVAL (operands[1]), INTVAL (operands[2]), 0,
			   INTVAL (operands[0]))
    : (gcc_unreachable (), 0);
  emit_insn (gen_rvtt_sfploaddiscard_int (GEN_INT (op)));
  DONE;
})

(define_insn "rvtt_sfploaddiscard_int"
  [(unspec_volatile:SI [
     (match_operand:SI 0 "const_int_operand" "n")
     ] UNSPECV_SFPLOADDISCARD)]
  "TARGET_XTT_TENSIX"
  ".ttinsn\t%0"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")])

;; Field-operand owned SETC16 (macro-planner design 4.3): the pass hands
;; only the architectural fields; the emitted word is packed by the
;; capability tables at output time.  (The pre-encoded-word form
;; rvtt_owned_setc16_int was deleted with the quarantined pass.)
(define_insn "rvtt_owned_setc16"
  [(unspec_volatile:SI [
     (match_operand:SI 0 "const_int_operand" "n") ;; config register
     (match_operand:SI 1 "const_int_operand" "n") ;; 16-bit value
     ] UNSPECV_OWNED_SETC16)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  {
    return rvtt_output_owned_setc16 (operands);
  }
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")
   (set_attr "xtt_subunit" "cfg")
   (set_attr "xtt_lreg_read_ops" "1")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "none")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")])

(define_expand "rvtt_sfploadsrcs"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
          (match_operand:SI 1 "reg_or_0_operand")
          (match_operand:SI 2 "reg_or_const_int_operand")
          (match_operand:SI 3 "reg_or_0_operand")
          (match_operand:SI 4 "const_int_operand")
          (match_operand:SI 5 "const_int_operand")
          (match_operand:SI 6 "const_int_operand")
          (match_operand:SI 7 "const_int_operand")
          ] UNSPECV_SFPLOADSRCS))]
  "TARGET_XTT_TENSIX_QSR"
{
  emit_insn (gen_rvtt_sfploadsrcs_lv
    (operands[0], operands[1], rvtt_gen_rtx_noval (XTT32SImode), operands[2],
     operands[3], operands[4], operands[5], operands[6], operands[7]));
  DONE;
})

(define_expand "rvtt_sfpload_lv"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
          (match_operand:SI    1 "reg_or_0_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_or_noval_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_0_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:SI    6 "const_int_operand")
          (match_operand:SI    7 "const_int_operand")
	  ] UNSPECV_SFPLOAD))]
  "TARGET_XTT_TENSIX"
{
  auto mem = const0_rtx;
  auto opc = const0_rtx;
  auto enc = const0_rtx;
  auto imm = operands[3];
  if (!CONST_INT_P (imm))
    {
      mem = gen_rtx_MEM (SImode, operands[1]);
      int op
        = TARGET_XTT_TENSIX_WH 	? TT_OP_WH_SFPLOAD (0, INTVAL (operands[6]), INTVAL (operands[7]), 0)
	: TARGET_XTT_TENSIX_BH 	? TT_OP_BH_SFPLOAD (0, INTVAL (operands[6]), INTVAL (operands[7]), 0)
	: TARGET_XTT_TENSIX_QSR	? TT_OP_QSR_SFPLOAD (0, INTVAL (operands[6]), INTVAL (operands[7]), 0, 0)
        : (gcc_unreachable (), 0);
      opc = GEN_INT (op);
      enc = GEN_INT (rvtt_synth (UINTVAL (operands[5])).dst_shift (20));
      imm = operands[4];
    }

  emit_insn (gen_rvtt_sfpload_lv_int
    (operands[0], mem, opc, enc, imm,
     rvtt_gen_rtx_noval (XTT32SImode),
     operands[2], operands[6], operands[7]));
  DONE;
})

(define_expand "rvtt_sfploadsrcs_lv"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
          (match_operand:SI    1 "reg_or_0_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_or_noval_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_0_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:SI    6 "const_int_operand")
          (match_operand:SI    7 "const_int_operand")
          (match_operand:SI    8 "const_int_operand")
	  ] UNSPECV_SFPLOADSRCS))]
  "TARGET_XTT_TENSIX_QSR"
{
  auto mem = const0_rtx;
  auto opc = const0_rtx;
  auto enc = const0_rtx;
  auto imm = operands[3];
  if (!CONST_INT_P (imm))
    {
      mem = gen_rtx_MEM (SImode, operands[1]);
      int op = TT_OP_QSR_SFPLOAD (0, INTVAL (operands[6]), INTVAL (operands[7]), 1, INTVAL (operands[8]));
      opc = GEN_INT (op);
      enc = GEN_INT (rvtt_synth (UINTVAL (operands[5])).dst_shift (20));
      imm = operands[4];
    }

  emit_insn (gen_rvtt_sfploadsrcs_lv_int
    (operands[0], mem, opc, enc, imm,
     rvtt_gen_rtx_noval (XTT32SImode),
     operands[2], operands[6], operands[7], operands[8]));
  DONE;
})

(define_insn "rvtt_sfpload_lv_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr,xr,xr")
        (unspec_volatile:XTT32SI [
          (match_operand:SI    1 "mem_or_0_operand" "J,J,m,m")
          (match_operand:SI    2 "const_int_operand" "J,J,n,n") ;; opcode
          (match_operand:SI    3 "const_int_operand" "J,J,n,n") ;; id, src & dst shifts
          (match_operand:SI    4 "reg_or_const_int_operand" "n,n,r,r") ;; imm or insn
          (match_operand:XTT32SI 5 "noval_operand" "xn,xn,xn,xn") ;; src (none)
          (match_operand:XTT32SI 6 "reg_or_cstlreg_or_noval_operand" "xn,0,xn,0") ;; lv
          (match_operand:SI    7 "const_int_operand" "n,n,n,n")
          (match_operand:SI    8 "const_int_operand" "n,n,n,n")
          ] UNSPECV_SFPLOAD))
   (clobber (match_scratch:SI  9 "=X,X,&r,&r"))]
  "TARGET_XTT_TENSIX"
  {
    return rvtt_synth::pattern (which_alternative >> 1,
      which_alternative & 1
      ? (TARGET_XTT_TENSIX_QSR ? "SFPLOAD\t%x0, %4, %7, %8, 0, 0\t# LV:%x6"
         : "SFPLOAD\t%x0, %4, %7, %8\t# LV:%x6")
      : (TARGET_XTT_TENSIX_QSR ? "SFPLOAD\t%x0, %4, %7, %8, 0, 0"
         : "SFPLOAD\t%x0, %4, %7, %8"),
      operands, true, 9);
  }
  [(set_attr "type" "tensix")
   (set_attr "xtt_macro_resource" "load")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_subunit" "load")
   (set_attr "xtt_lreg_write_port" "own")
   (set_attr "xtt_lreg_read_ops" "65")
   (set_attr "xtt_lreg_write_ops" "2")
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "addr_mode")
   ;; D3 latency audit: SFPLOAD.md's three-instruction rule is the
   ;; cross-unit Dst race, not an SFPU result delay; the hardware-proven
   ;; hand exp kernel consumes SFPLOAD's LREG result in the next slot
   ;; (SFPLOAD->SFPMAD back-to-back): result latency 0.
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   || TARGET_XTT_TENSIX_WH")
		      (const_int 1) (const_int 0)))
   (set_attr "xtt_macro_encodable" "yes")])

;; A complete WH/BH macro launch.  The formation pass may emit this only after
;; it has materialized and owns the referenced sequence/template/misc slot.
;; Both logical Dst accesses remain operands so ordinary memory dependencies
;; survive replacement of the explicit load/transform/store region.
(define_insn "rvtt_sfploadmacro_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
          (match_operand:SI 1 "mem_or_0_operand" "X") ;; load Dst effect
          (match_operand:SI 2 "mem_or_0_operand" "X") ;; store Dst effect
          (match_operand:SI 3 "const_int_operand" "n") ;; address
          (match_operand:SI 4 "const_int_operand" "n") ;; load/store mode
          (match_operand:SI 5 "const_int_operand" "n") ;; address mode
          ;; Keep the raw 32-bit instruction in DImode: WH/BH macro opcodes
          ;; have bit 31 set, so their unsigned spelling is not a canonical
          ;; SImode CONST_INT even though the assembler accepts that spelling.
          (match_operand:DI 6 "const_int_operand" "n") ;; encoded launch word
          ] UNSPECV_SFPLOADMACRO))]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  ".ttinsn\t%6"
  [(set_attr "type" "tensix")
   (set_attr "xtt_macro_resource" "load")
   ;; Replay-membership audit: a launch is a pure instruction
   ;; WORD -- recording captures the word, never state; execution at a
   ;; replay site reads the then-current descriptor configuration,
   ;; identical to executing the original word there.  The formation
   ;; pass emits launches only after materializing and owning the
   ;; referenced descriptor slots, and the production handwritten Where
   ;; kernel records TT_SFPLOADMACRO in its replay buffer
   ;; (ckernel_sfpu_where.h load_replay_buf) -- the architectural
   ;; precedent the simulator executes through the same path.
   ;; Membership is the opt-in -mtt-tensix-macro-planner-replay
   ;; delivery increment: off keeps every formed calendar
   ;; byte-identical to the pre-flag output (the simulator-proven shapes are
   ;; admitted per A/B, not wholesale).
   (set (attr "xtt_replay")
	(if_then_else (match_test "riscv_tt_macro_planner_replay")
		      (const_string "safe")
		      (const_string "barrier")))])

;; A macro launch whose delayed Simple template is SFPSWAP.  In addition to
;; the launch VD, the template writes the fixed comparison operand (L2).  Keep
;; that hidden architectural write explicit in RTL; formation only selects
;; this pattern after proving L2 is private to the complete periodic region.
;; Generic hidden-clobber launch (macro-planner design 4.3): the hidden
;; physical-LREG write of the launched template is a register operand the
;; planner fills from the capability tables'
;; template_hidden_lreg_writes(), never a pattern-frozen register.
;; (Replaced the fixed-L2 rvtt_sfploadmacro_swap_int, since deleted.)
(define_insn "rvtt_sfploadmacro_hidden_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
          (match_operand:SI 1 "mem_or_0_operand" "X") ;; load Dst effect
          (match_operand:SI 2 "mem_or_0_operand" "X") ;; store Dst effect
          (match_operand:SI 3 "const_int_operand" "n") ;; address
          (match_operand:SI 4 "const_int_operand" "n") ;; load/store mode
          (match_operand:SI 5 "const_int_operand" "n") ;; address mode
          (match_operand:DI 6 "const_int_operand" "n") ;; encoded launch word
          ] UNSPECV_SFPLOADMACRO))
   ;; Created post-reload: the hidden write is a hard-register operand the
   ;; planner fills from template_hidden_lreg_writes(), never a
   ;; pattern-frozen register.
   (clobber (match_operand:XTT32SI 7 "register_operand" "=xr"))]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  ".ttinsn\t%6"
  [(set_attr "type" "tensix")
   (set_attr "xtt_macro_resource" "load")
   ;; Replay-membership audit: as rvtt_sfploadmacro_int above;
   ;; the hidden template write re-executes at every replay exactly as
   ;; the original word would, and stays modeled by the clobber.
   (set (attr "xtt_replay")
	(if_then_else (match_test "riscv_tt_macro_planner_replay")
		      (const_string "safe")
		      (const_string "barrier")))])

(define_insn "rvtt_sfploadsrcs_lv_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr,xr,xr")
        (unspec_volatile:XTT32SI [
          (match_operand:SI    1 "mem_or_0_operand" "J,J,m,m")
          (match_operand:SI    2 "const_int_operand" "J,J,n,n") ;; opcode
          (match_operand:SI    3 "const_int_operand" "J,J,n,n") ;; id, src & dst shifts
          (match_operand:SI    4 "reg_or_const_int_operand" "n,n,r,r") ;; imm or insn
          (match_operand:XTT32SI 5 "noval_operand" "xn,xn,xn,xn") ;; src (none)
          (match_operand:XTT32SI 6 "reg_or_cstlreg_or_noval_operand" "xn,0,xn,0") ;; lv
          (match_operand:SI    7 "const_int_operand" "n,n,n,n")
          (match_operand:SI    8 "const_int_operand" "n,n,n,n")
          (match_operand:SI    9 "const_int_operand" "n,n,n,n")
          ] UNSPECV_SFPLOADSRCS))
   (clobber (match_scratch:SI  10 "=X,X,&r,&r"))]
  "TARGET_XTT_TENSIX_QSR"
  {
    return rvtt_synth::pattern (which_alternative >> 1,
      which_alternative & 1
      ? "SFPLOAD\t%x0, %4, %7, %8, 1, %9\t# LV:%x6"
      : "SFPLOAD\t%x0, %4, %7, %8, 1, %9",
      operands, true, 10);
  }
  [(set_attr "type" "tensix")
   (set_attr "xtt_macro_resource" "load")
   (set_attr "xtt_replay" "safe")])

(define_expand "rvtt_sfpstore"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "reg_or_0_operand")
     (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
     (match_operand:SI    2 "reg_or_const_int_operand")
     (match_operand:SI    3 "reg_or_0_operand")
     (match_operand:SI    4 "const_int_operand")
     (match_operand:SI    5 "const_int_operand")
     (match_operand:SI    6 "const_int_operand")
     ] UNSPECV_SFPSTORE)]
  "TARGET_XTT_TENSIX"
{
  auto mem = const0_rtx;
  auto opc = const0_rtx;
  auto enc = const0_rtx;
  auto imm = operands[2];
  if (!CONST_INT_P (imm))
    {
      mem = gen_rtx_MEM (SImode, operands[0]);
      int op
        = TARGET_XTT_TENSIX_WH 	? TT_OP_WH_SFPSTORE (0, INTVAL (operands[5]), INTVAL (operands[6]), 0)
        : TARGET_XTT_TENSIX_BH  ? TT_OP_BH_SFPSTORE (0, INTVAL (operands[5]), INTVAL (operands[6]), 0)
        : TARGET_XTT_TENSIX_QSR ? TT_OP_QSR_SFPSTORE (0, INTVAL (operands[5]), INTVAL (operands[6]), 0, 0)
        : (gcc_unreachable (), 0);
      opc = GEN_INT (op);
      enc = GEN_INT (rvtt_synth (UINTVAL (operands[4])).src_shift (20));
      imm = operands[3];
    }

  emit_insn (gen_rvtt_sfpstore_int
    (mem, opc, enc, imm,
     operands[1], operands[5], operands[6]));
  DONE;
})

;; stores cannot write from L12..L15 due to load macro side loading possibility
(define_insn "rvtt_sfpstore_int"
  [(unspec_volatile:XTT32SI [
    (match_operand:SI    0 "mem_or_0_operand" "J,m")
    (match_operand:SI    1 "const_int_operand" "J,n") ;; opcode
    (match_operand:SI    2 "const_int_operand" "J,n") ;; id, src & dst shifts
    (match_operand:SI    3 "reg_or_const_int_operand" "n,r") ;; imm or insn
    (match_operand:XTT32SI 4 "reg_or_cstlreg_operand"  "xrxs,xrxs") ;; src
    (match_operand:SI    5 "const_int_operand" "n,n")
    (match_operand:SI    6 "const_int_operand" "n,n")
    ] UNSPECV_SFPSTORE)
   (clobber (match_scratch:SI  7 "=X,&r"))]
  "TARGET_XTT_TENSIX"
  {
    return rvtt_synth::pattern (which_alternative,
      TARGET_XTT_TENSIX_QSR ? "SFPSTORE\t%x4, %3, %5, %6, 0, 0"
      : "SFPSTORE\t%x4, %3, %5, %6",
      operands, false, 7);
  }
  [(set_attr "type" "tensix")
   (set_attr "xtt_macro_resource" "store")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_subunit" "store")
   (set_attr "xtt_lreg_read_ops" "17")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "addr_mode")
   ;; Latency audit: SFPSTORE writes Dst only -- it has no
   ;; LREG result a following issue slot could wait on.  SFPSTORE.md
   ;; (BH and WH) carries no next-cycle rule (the audited latency-0
   ;; page convention), and the BH SFPMAD.md hardware-bug list of
   ;; consumers the automatic stalling logic misses does not name
   ;; SFPSTORE, so a store consuming a MAD result is scoreboard-covered
   ;; on BH and nop-inserter territory on WH (xtt_delay untouched by
   ;; this row).  The hardware-proven hand exp kernel issues its stores
   ;; back-to-back with dependent neighbours; the reference simulator
   ;; TENSIX_EXECUTE_SFPSTORE commits Dst at issue.  Result latency 0,
   ;; BH/WH only.
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   || TARGET_XTT_TENSIX_WH")
		      (const_int 1) (const_int 0)))
   (set_attr "xtt_macro_encodable" "yes")
   ;; Lane-gated consumer (typed-effect attributes): SFPSTORE moves only
   ;; CC-enabled lanes to Dst.
   (set_attr "xtt_lane_gated" "yes")])

(define_expand "rvtt_sfpstoresrcs"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "reg_or_0_operand")
     (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
     (match_operand:SI    2 "reg_or_const_int_operand")
     (match_operand:SI    3 "reg_or_0_operand")
     (match_operand:SI    4 "const_int_operand")
     (match_operand:SI    5 "const_int_operand")
     (match_operand:SI    6 "const_int_operand")
     (match_operand:SI    7 "const_int_operand")
     ] UNSPECV_SFPSTORESRCS)]
  "TARGET_XTT_TENSIX_QSR"
{
  auto mem = const0_rtx;
  auto opc = const0_rtx;
  auto enc = const0_rtx;
  auto imm = operands[2];
  if (!CONST_INT_P (imm))
    {
      mem = gen_rtx_MEM (SImode, operands[0]);
      int op
        = TT_OP_QSR_SFPSTORE (0, INTVAL (operands[5]), INTVAL (operands[6]), 1, INTVAL (operands[7]));
      opc = GEN_INT (op);
      enc = GEN_INT (rvtt_synth (UINTVAL (operands[4])).src_shift (20));
      imm = operands[3];
    }

  emit_insn (gen_rvtt_sfpstoresrcs_int
    (mem, opc, enc, imm,
     operands[1], operands[5], operands[6], operands[7]));
  DONE;
})

(define_insn "rvtt_sfpstoresrcs_int"
  [(unspec_volatile:XTT32SI [
    (match_operand:SI    0 "mem_or_0_operand" "J,m")
    (match_operand:SI    1 "const_int_operand" "J,n") ;; opcode
    (match_operand:SI    2 "const_int_operand" "J,n") ;; id, src & dst shifts
    (match_operand:SI    3 "reg_or_const_int_operand" "n,r") ;; imm or insn
    (match_operand:XTT32SI 4 "reg_or_cstlreg_operand"  "xrxs,xrxs") ;; src
    (match_operand:SI    5 "const_int_operand" "n,n")
    (match_operand:SI    6 "const_int_operand" "n,n")
    (match_operand:SI    7 "const_int_operand" "n,n")
    ] UNSPECV_SFPSTORESRCS)
   (clobber (match_scratch:SI  8 "=X,&r"))]
  "TARGET_XTT_TENSIX_QSR"
  {
    return rvtt_synth::pattern (which_alternative,
      "SFPSTORE\t%x4, %3, %5, %6, 1, %7",
      operands, false, 8);
  }
  [(set_attr "type" "tensix")
   (set_attr "xtt_macro_resource" "store")
   (set_attr "xtt_replay" "safe")])

