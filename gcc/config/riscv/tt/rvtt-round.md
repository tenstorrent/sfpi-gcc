;; Machine description for Tenstorrent SFPU Intrinsics -- conversion and rounding.
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

;; Numeric conversion and rounding: sfpcast, divide by power of
;; two, and the stochastic-rounding words.
(define_expand "rvtt_sfpcast"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI    2 "const_int_operand" "n")
	  ] UNSPECV_SFPCAST))]
  "TARGET_XTT_TENSIX"
  {
    emit_insn (gen_rvtt_sfpcast_lv (operands[0],
      rvtt_gen_rtx_noval (XTT32SImode), operands[1], operands[2]));
    DONE;
  })

(define_insn "rvtt_sfpcast_lv"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand"  "xn,0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc,xrxc")
          (match_operand:SI    3 "const_int_operand" "n,n")
	  ] UNSPECV_SFPCAST))]
  "TARGET_XTT_TENSIX"
  "@
   SFPCAST\t%x0, %x2, %3
   SFPCAST\t%x0, %x2, %3\t# LV:%x1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; Subunit: the architectural reference (SFPCAST.md) confirms the
   ;; former 9(h)-class inference: "Backend execution unit: Vector Unit
   ;; (SFPU), simple sub-unit".
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_write_port" "shared_simple_round")
   ;; The effect claims (reads operand 2, writes operand 0, lane-
   ;; predicated CC read, no CC write / config / RWC effect) are audited
   ;; per mod1 (operand 3) and hold only for the proven conversions:
   ;;   mod1 0 (SM32->FP32 round-nearest-even): the reference simulator
   ;;     TENSIX_EXECUTE_SFPCAST + SFPCAST.md functional model.
   ;;   mod1 3, BH only (self-inverse sign-preserving conditional
   ;;     negate, the SM32<->INT32 conversion): the reference simulator mod3 branch +
   ;;     SFPCAST_IntInt.md; hardware exact-equality boundary testing).
   ;; mod1 1 (stochastic rounding) additionally advances the PRNG --
   ;; architectural state outside the effect vocabulary -- and BH mod1 2
   ;; is the documented cast-as-ABS hardware bug the simulator refuses
   ;; to execute; both keep the refusing defaults, as does every higher
   ;; (non-contractual) mod.  QSR retains only the mod-0 claim the
   ;; original effect audit recorded (unproven-by-simulator; flagged
   ;; with review carry-forward risk).
   (set (attr "xtt_lreg_read_ops")
	(if_then_else (match_test "INTVAL (operands[3]) == 0
				   || (TARGET_XTT_TENSIX_BH
				       && INTVAL (operands[3]) == 3)")
		      (const_int 7) (const_int 0)))
   (set (attr "xtt_lreg_write_ops")
	(if_then_else (match_test "INTVAL (operands[3]) == 0
				   || (TARGET_XTT_TENSIX_BH
				       && INTVAL (operands[3]) == 3)")
		      (const_int 2) (const_int 0)))
   (set (attr "xtt_cc_effect")
	(if_then_else (match_test "INTVAL (operands[3]) == 0
				   || (TARGET_XTT_TENSIX_BH
				       && INTVAL (operands[3]) == 3)")
		      (const_string "read") (const_string "unknown")))
   (set (attr "xtt_config_effect")
	(if_then_else (match_test "INTVAL (operands[3]) == 0
				   || (TARGET_XTT_TENSIX_BH
				       && INTVAL (operands[3]) == 3)")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_rwc_effect")
	(if_then_else (match_test "INTVAL (operands[3]) == 0
				   || (TARGET_XTT_TENSIX_BH
				       && INTVAL (operands[3]) == 3)")
		      (const_string "none") (const_string "unknown")))
   ;; D3 latency audit: S1 Simple; the frozen cast-round calendar
   ;; steps cast->rnd one slot and the hand exp kernel runs
   ;; SFPCAST->SFPMAD back-to-back on hardware: result latency 0
   ;; (audited mods only; unaudited mods stay opaque via the fields
   ;; above).
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "INTVAL (operands[3]) == 0
				   || (TARGET_XTT_TENSIX_BH
				       && INTVAL (operands[3]) == 3)")
		      (const_int 1) (const_int 0)))
   ;; Migrated effect-override row (now typed-effect attributes): pure value unary,
   ;; never touches CC; lane-gated consumer.
   (set_attr "xtt_lane_local" "yes")
   (set_attr "xtt_cc_write" "no")
   (set_attr "xtt_lane_gated" "yes")])

(define_expand "rvtt_sfpdivp2"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "reg_or_0_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_0_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] UNSPECV_SFPDIVP2))]
  "TARGET_XTT_TENSIX"
{
  emit_insn (gen_rvtt_sfpdivp2_lv
    (operands[0], operands[1], rvtt_gen_rtx_noval (XTT32SImode),
     operands[2], operands[3], operands[4], operands[5], operands[6]));
  DONE;
})

(define_expand "rvtt_sfpdivp2_lv"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "reg_or_0_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_or_noval_operand")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
          (match_operand:SI    4 "reg_or_const_int_operand")
          (match_operand:SI    5 "reg_or_0_operand")
          (match_operand:SI    6 "const_int_operand")
          (match_operand:SI    7 "const_int_operand")
	  ] UNSPECV_SFPDIVP2))]
  "TARGET_XTT_TENSIX"
{
  auto mem = const0_rtx;
  auto opc = const0_rtx;
  auto enc = const0_rtx;
  auto imm = operands[4];
  if (!CONST_INT_P (imm))
    {
      mem = gen_rtx_MEM (SImode, operands[1]);
      int op
        = TARGET_XTT_TENSIX_WH 	? TT_OP_BH_SFPDIVP2 (0, 0, 0, INTVAL (operands[7]))
        : TARGET_XTT_TENSIX_BH  ? TT_OP_BH_SFPDIVP2 (0, 0, 0, INTVAL (operands[7]))
        : TARGET_XTT_TENSIX_QSR ? TT_OP_QSR_SFPDIVP2 (0, 0, 0, INTVAL (operands[7]))
        : (gcc_unreachable (), 0);
      opc = GEN_INT (op);
      enc = GEN_INT (rvtt_synth (UINTVAL (operands[6])).src_shift (4).dst_shift (8));
      imm = operands[5];
    }

  emit_insn (gen_rvtt_sfpdivp2_lv_int
    (operands[0], mem, opc, enc, imm,
     operands[3], operands[2], operands[7]));
  DONE;
})

(define_insn "rvtt_sfpdivp2_lv_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr,xr,xr")
        (unspec_volatile:XTT32SI [
          (match_operand:SI    1 "mem_or_0_operand" "J,J,m,m")
          (match_operand:SI    2 "const_int_operand" "J,J,n,n") ;; opcode
          (match_operand:SI    3 "const_int_operand" "J,J,n,n") ;; id, src & dst shifts
          (match_operand:SI    4 "reg_or_const_int_operand" "n,n,r,r") ;; imm or insn
          (match_operand:XTT32SI 5 "reg_or_cstlreg_operand" "xrxc,xrxc,xrxc,xrxc") ;; src
          (match_operand:XTT32SI 6 "reg_or_cstlreg_or_noval_operand" "xn,0,xn,0") ;; lv
          (match_operand:SI    7 "const_int_operand" "n,n,n,n")
	  ] UNSPECV_SFPDIVP2))
   (clobber (match_scratch:SI  8 "=X,X,&r,&r"))]
  "TARGET_XTT_TENSIX"
  {
    return rvtt_synth::pattern (which_alternative >> 1,
      which_alternative & 1
      ? "SFPDIVP2\t%x0, %x5, %4, %7\t# LV:%x6"
      : "SFPDIVP2\t%x0, %x5, %4, %7",
      operands, true, 8);
  }
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; SFPDIVP2.md: "Backend execution unit: Vector Unit (SFPU), simple
   ;; sub-unit"; same write-port class as the other Simple-unit
   ;; exponent operations.
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_write_port" "shared_simple_round")
   ;; Effects audited per mod1 (operand 7) from the reference simulator
   ;; TENSIX_EXECUTE_SFPDIVP2 (verifies mod1 <= 1; TT_VERSION <= 1 =
   ;; WH/BH) + the SFPDIVP2.md functional model (QSR has no simulator
   ;; specification and keeps the refusing defaults).  Both mods read
   ;; LReg[VC] (operand 5) only and write VD (operand 0)
   ;; lane-predicated; neither touches the lane flags, configuration,
   ;; or counters.  The lane-predicated write keeps the live-in
   ;; (operand 6, tied to the destination in the LV alternatives), so
   ;; both carry the read claim.  The single-word constant-immediate
   ;; alternatives only (a register immediate raises the
   ;; runtime-synthesized instruction push, whose memory effects this
   ;; audit does not cover).
   (set (attr "xtt_lreg_read_ops")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[4])
				   && IN_RANGE (INTVAL (operands[7]), 0, 1)")
		      (const_int 98) (const_int 0)))
   (set (attr "xtt_lreg_write_ops")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[4])
				   && IN_RANGE (INTVAL (operands[7]), 0, 1)")
		      (const_int 2) (const_int 0)))
   (set (attr "xtt_cc_effect")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[4])
				   && IN_RANGE (INTVAL (operands[7]), 0, 1)")
		      (const_string "read") (const_string "unknown")))
   (set (attr "xtt_config_effect")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[4])
				   && IN_RANGE (INTVAL (operands[7]), 0, 1)")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_rwc_effect")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[4])
				   && IN_RANGE (INTVAL (operands[7]), 0, 1)")
		      (const_string "none") (const_string "unknown")))
   ;; D3 latency audit: S1 Simple; Simple dependence chains step one
   ;; slot and SFPDIVP2.md has no next-cycle constraint: result
   ;; latency 0 (audited mods only).
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[4])
				   && IN_RANGE (INTVAL (operands[7]), 0, 1)")
		      (const_int 1) (const_int 0)))
   ;; Migrated effect-override row (now typed-effect attributes): pure value unary,
   ;; never touches CC; lane-gated consumer.
   (set_attr "xtt_lane_local" "yes")
   (set_attr "xtt_cc_write" "no")
   (set_attr "xtt_lane_gated" "yes")])

(define_expand "rvtt_sfpstochrnd_i"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "reg_or_0_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_0_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:SI    6 "const_int_operand")
          (match_operand:SI    7 "const_int_operand")
	  ] UNSPECV_SFPSTOCHRND))]
  "TARGET_XTT_TENSIX"
{
  emit_insn (gen_rvtt_sfpstochrnd_i_lv
    (operands[0], operands[1], rvtt_gen_rtx_noval (XTT32SImode),
     operands[2], operands[3],
     operands[4], operands[5], operands[6], operands[7]));
  DONE;
})

(define_expand "rvtt_sfpstochrnd_i_lv"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "reg_or_0_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_or_noval_operand")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
          (match_operand:SI    4 "reg_or_const_int_operand")
          (match_operand:SI    5 "reg_or_0_operand")
          (match_operand:SI    6 "const_int_operand")
          (match_operand:SI    7 "const_int_operand")
          (match_operand:SI    8 "const_int_operand")
	  ] UNSPECV_SFPSTOCHRND))]
  "TARGET_XTT_TENSIX"
{
  unsigned mod1 = INTVAL (operands[7]);
  if (mod1 == SFPSTOCHRND_MOD1_INT32_TO_UINT8
      || mod1 == SFPSTOCHRND_MOD1_INT32_TO_INT8)
    operands[7] = GEN_INT (mod1 | SFPSTOCHRND_MOD1_IMM8);

  auto mem = const0_rtx;
  auto opc = const0_rtx;
  auto enc = const0_rtx;
  auto imm = operands[4];
  if (!CONST_INT_P (imm))
    {
      mem = gen_rtx_MEM (SImode, operands[1]);
      int op
        = TARGET_XTT_TENSIX_WH  ? TT_OP_WH_SFP_STOCH_RND (INTVAL (operands[8]),
	             0, 0, 0, 0, INTVAL (operands[7]) | SFPSTOCHRND_MOD1_IMM8)
        : TARGET_XTT_TENSIX_BH  ? TT_OP_BH_SFP_STOCH_RND (INTVAL (operands[8]),
	             0, 0, 0, 0, INTVAL (operands[7]) | SFPSTOCHRND_MOD1_IMM8)
        : TARGET_XTT_TENSIX_QSR ? TT_OP_QSR_SFP_STOCH_RND (INTVAL (operands[8]),
	             0, 0, 0, 0, INTVAL (operands[7]) | SFPSTOCHRND_MOD1_IMM8)
        : (gcc_unreachable (), 0);
      opc = GEN_INT (op);
      enc = GEN_INT (rvtt_synth (UINTVAL (operands[6])).src_shift (4).dst_shift (8));
      imm = operands[5];
    }

  emit_insn (gen_rvtt_sfpstochrnd_i_lv_int
    (operands[0], mem, opc, enc, imm,
     operands[3], operands[2], operands[7], operands[8]));
  DONE;
})

(define_insn "rvtt_sfpstochrnd_i_lv_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr,xr,xr")
        (unspec_volatile:XTT32SI [
          (match_operand:SI    1 "mem_or_0_operand" "J,J,m,m")
          (match_operand:SI    2 "const_int_operand" "J,J,n,n") ;; opcode
          (match_operand:SI    3 "const_int_operand" "J,J,n,n") ;; id, src & dst shifts
          (match_operand:SI    4 "reg_or_const_int_operand" "n,n,r,r") ;; imm or insn
          (match_operand:XTT32SI 5 "reg_or_cstlreg_operand" "xrxc,xrxc,xrxc,xrxc") ;; src
          (match_operand:XTT32SI 6 "reg_or_cstlreg_or_noval_operand" "xn,0,xn,0") ;; lv
          (match_operand:SI    7 "const_int_operand" "n,n,n,n")
          (match_operand:SI    8 "const_int_operand" "n,n,n,n")
	  ] UNSPECV_SFPSTOCHRND))
   (clobber (match_scratch:SI 9 "=X,X,&r,&r"))]
  "TARGET_XTT_TENSIX"
  {
    return rvtt_synth::pattern (which_alternative >> 1,
      which_alternative & 1
      ? "SFPSTOCHRND\t%x0, L0, %x5, %4, %7, %8\t# LV:%x6"
      : "SFPSTOCHRND\t%x0, L0, %x5, %4, %7, %8",
      operands, true, 9);
  }
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_subunit" "round")
   (set_attr "xtt_lreg_write_port" "shared_simple_round")
   (set_attr "xtt_lreg_read_ops" "97")
   (set_attr "xtt_lreg_write_ops" "2")
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   ;; D3 latency audit: Round sub-unit; the frozen cast-round calendar
   ;; places the dependent store one slot after SFPSTOCHRND, and the
   ;; hand exp kernel runs SFPSTOCHRND->SFPSTORE back-to-back on
   ;; hardware: result latency 0.
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   || TARGET_XTT_TENSIX_WH")
		      (const_int 1) (const_int 0)))
   ;; Lane-gated consumer (typed-effect attributes).
   (set_attr "xtt_lane_gated" "yes")])

(define_expand "rvtt_sfpstochrnd_v"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
          (match_operand:XTT32SI 1 "register_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
	  (match_operand:SI    3 "const_int_operand")
          (match_operand:SI    4 "const_int_operand")
	  ] UNSPECV_SFPSTOCHRND))]
  "TARGET_XTT_TENSIX"
{
  emit_insn (gen_rvtt_sfpstochrnd_v_lv
    (operands[0], rvtt_gen_rtx_noval (XTT32SImode),
     operands[1], operands[2], operands[3], operands[4]));
  DONE;
})

(define_insn "rvtt_sfpstochrnd_v_lv"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn,0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc,xrxc")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "xrxc,xrxc")
          (match_operand:SI    4 "const_int_operand" "n,n")
          (match_operand:SI    5 "const_int_operand" "n,n")
	  ] UNSPECV_SFPSTOCHRND))]
  "TARGET_XTT_TENSIX"
  "@
   SFPSTOCHRND\t%x0, %x3, %x2, 0, %4, %5
   SFPSTOCHRND\t%x0, %x3, %x2, 0, %4, %5\t# LV:%x1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_subunit" "round")
   (set_attr "xtt_lreg_write_port" "shared_simple_round")
   (set_attr "xtt_lreg_read_ops" "15")
   (set_attr "xtt_lreg_write_ops" "2")
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   ;; D3 latency audit: Round sub-unit; the frozen cast-round calendar
   ;; places the dependent store one slot after SFPSTOCHRND, and the
   ;; hand exp kernel runs SFPSTOCHRND->SFPSTORE back-to-back on
   ;; hardware: result latency 0.
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   || TARGET_XTT_TENSIX_WH")
		      (const_int 1) (const_int 0)))
   ;; Lane-gated consumer (typed-effect attributes).
   (set_attr "xtt_lane_gated" "yes")])

