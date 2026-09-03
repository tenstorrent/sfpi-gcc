;; Machine description for Tenstorrent SFPU Intrinsics -- arithmetic.
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

;; SFPU arithmetic: the mul/add/mad family, the integer
;; multiply-add-immediate family, integer add, and the unary
;; operations.
(define_int_iterator rvtt_muladd_op [
  UNSPECV_SFPMUL
  UNSPECV_SFPADD
  ])
(define_int_attr rvtt_muladd_name [
  (UNSPECV_SFPMUL "mul")
  (UNSPECV_SFPADD "add")
  ])
(define_int_attr rvtt_muladd_insn [
  (UNSPECV_SFPMUL "MUL")
  (UNSPECV_SFPADD "ADD")
  ])
(define_expand "rvtt_sfp<rvtt_muladd_name>"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI    3 "const_int_operand" "n")
	  ] rvtt_muladd_op))]
  "TARGET_XTT_TENSIX"
  {
    emit_insn (gen_rvtt_sfp<rvtt_muladd_name>_lv
      (operands[0], rvtt_gen_rtx_noval (XTT32SImode),
       operands[1], operands[2], operands[3]));
    DONE;
  })

(define_insn "rvtt_sfp<rvtt_muladd_name>_lv"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn,0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc,xrxc")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "xrxc,xrxc")
          (match_operand:SI    4 "const_int_operand" "n,n")
	  ] rvtt_muladd_op))]
  "TARGET_XTT_TENSIX"
  "@
   SFP<rvtt_muladd_insn>\t%x0, %x2, %x3, %4
   SFP<rvtt_muladd_insn>\t%x0, %x2, %x3, %4\t# LV:%x1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_latency_reorder" "safe")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_delay" "dynamic")
   (set_attr "xtt_subunit" "mad")
   (set_attr "xtt_result_latency" "2")
   (set_attr "xtt_lreg_write_port" "own")
   (set_attr "xtt_lreg_read_ops" "15")
   (set_attr "xtt_lreg_write_ops" "2")
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   ;; Lane-gated consumer (typed-effect attributes).
   (set_attr "xtt_lane_gated" "yes")])

(define_expand "rvtt_sfpmad"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
          (match_operand:SI    4 "const_int_operand")
	  ] UNSPECV_SFPMAD))]
  "TARGET_XTT_TENSIX"
{
  emit_insn (gen_rvtt_sfpmad_lv
    (operands[0], rvtt_gen_rtx_noval (XTT32SImode),
     operands[1], operands[2], operands[3], operands[4]));
  DONE;
})

(define_insn "rvtt_sfpmad_lv"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn,0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc,xrxc")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "xrxc,xrxc")
          (match_operand:XTT32SI 4 "reg_or_cstlreg_operand"  "xrxc,xrxc")
          (match_operand:SI    5 "const_int_operand" "n,n")
	  ] UNSPECV_SFPMAD))]
  "TARGET_XTT_TENSIX"
  "@
   SFPMAD\t%x0, %x2, %x3, %x4, %5
   SFPMAD\t%x0, %x2, %x3, %x4, %5\t# LV:%x1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_latency_reorder" "safe")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_delay" "dynamic")
   (set_attr "xtt_subunit" "mad")
   (set_attr "xtt_result_latency" "2")
   (set_attr "xtt_lreg_write_port" "own")
   (set_attr "xtt_lreg_read_ops" "31")
   (set_attr "xtt_lreg_write_ops" "2")
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   ;; Lane-gated consumer (typed-effect attributes).
   (set_attr "xtt_lane_gated" "yes")])

(define_int_iterator rvtt_muliaddi_op [
  UNSPECV_SFPMULI
  UNSPECV_SFPADDI
  ])
(define_int_attr rvtt_muliaddi_name [
  (UNSPECV_SFPMULI "muli")
  (UNSPECV_SFPADDI "addi")
  ])
(define_int_attr rvtt_muliaddi_insn [
  (UNSPECV_SFPMULI "MULI")
  (UNSPECV_SFPADDI "ADDI")
  ])

(define_expand "rvtt_sfp<rvtt_muliaddi_name>"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "reg_or_0_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_0_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] rvtt_muliaddi_op))]
  "TARGET_XTT_TENSIX"
{
  emit_insn (gen_rvtt_sfp<rvtt_muliaddi_name>_lv
    (operands[0], operands[1], rvtt_gen_rtx_noval (XTT32SImode),
     operands[2], operands[3], operands[4], operands[5], operands[6]));
  DONE;
})

(define_expand "rvtt_sfp<rvtt_muliaddi_name>_lv"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "reg_or_0_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_or_noval_operand")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
          (match_operand:SI    4 "reg_or_const_int_operand")
          (match_operand:SI    5 "reg_or_0_operand")
          (match_operand:SI    6 "const_int_operand")
          (match_operand:SI    7 "const_int_operand")
	  ] rvtt_muliaddi_op))]
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
        = TARGET_XTT_TENSIX_WH 	? TT_OP_WH_SFP<rvtt_muliaddi_insn> (0, 0, INTVAL (operands[7]))
	: TARGET_XTT_TENSIX_BH 	? TT_OP_BH_SFP<rvtt_muliaddi_insn> (0, 0, INTVAL (operands[7]))
	: TARGET_XTT_TENSIX_QSR	? TT_OP_QSR_SFP<rvtt_muliaddi_insn> (0, 0, INTVAL (operands[7]))
        : (gcc_unreachable (), 0);
      opc = GEN_INT (op);
      enc = GEN_INT (rvtt_synth (UINTVAL (operands[6])).src_shift (4).dst_shift (4));
      imm = operands[5];
    }

  emit_insn (gen_rvtt_sfp<rvtt_muliaddi_name>_int_lv
    (operands[0], mem, opc, enc, imm,
     operands[3], operands[2], operands[7]));
  DONE;
})

(define_insn_and_rewrite "rvtt_sfp<rvtt_muliaddi_name>_int_lv"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr,xr,xr,xr,xr")
        (unspec_volatile:XTT32SI [
          (match_operand:SI    1 "mem_or_0_operand" "J,J,J,m,m,m")
          (match_operand:SI    2 "const_int_operand" "J,J,J,n,n,n") ;; opcode
          (match_operand:SI    3 "const_int_operand" "J,J,J,n,n,n") ;; id, src & dst shifts
          (match_operand:SI    4 "reg_or_const_int_operand" "n,n,n,r,r,r") ;; imm or insn
          (match_operand:XTT32SI 5 "reg_or_cstlreg_operand" "0,0,0,0,0,0") ;; src
          (match_operand:XTT32SI 6 "reg_or_cstlreg_or_noval_or_omit_operand" "xn,xo,xrxc,xn,xo,xrxc") ;; lv
          (match_operand:SI    7 "const_int_operand" "n,n,n,n,n,n")
          ] rvtt_muliaddi_op))
   (clobber (match_scratch:SI  8 "=X,X,X,&r,&r,&r"))]
  "TARGET_XTT_TENSIX"
  {
    int which = which_alternative;
    bool dyn = which >= 3;
    if (dyn)
      which -= 3;
    return rvtt_synth::pattern (dyn,
    which == 0 ? "SFP<rvtt_muliaddi_insn>\t%x0, %4, %7" :
    which == 1 ? "SFP<rvtt_muliaddi_insn>\t%x0, %4, %7\t# LV:%x5" :
    "#",
    operands, true, 8);
  }
  "&& !noval_or_omit_operand (operands[6], GET_MODE (operands[6]))"
  {
    rvtt_merge_lv_src (&operands[6], &operands[5]);
  }
  [(set_attr "type" "tensix")
   (set_attr "xtt_latency_reorder" "safe")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_delay" "dynamic")
   (set_attr "xtt_subunit" "mad")
   (set_attr "xtt_result_latency" "2")
   (set_attr "xtt_lreg_write_port" "own")
   (set_attr "xtt_lreg_read_ops" "97")
   (set_attr "xtt_lreg_write_ops" "2")
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   ;; Lane-gated consumer (typed-effect attributes).
   (set_attr "xtt_lane_gated" "yes")])

(define_expand "rvtt_sfpiadd_v"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	(match_operand:XTT32SI 1 "register_operand")
        (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
        (match_operand:SI      3 "const_int_operand")
	] UNSPECV_SFPIADD))]
  "TARGET_XTT_TENSIX"
  {
    emit_insn (gen_rvtt_sfpiadd_v_lv
      (operands[0], rvtt_gen_rtx_noval (XTT32SImode),
       operands[1], operands[2], operands[3]));
    DONE;
  })

(define_insn_and_rewrite "rvtt_sfpiadd_v_lv"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_or_omit_operand" "xn,xo,xrxc")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand" "0,0,0")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand" "xrxc,xrxc,xrxc")
          (match_operand:SI      4 "const_int_operand" "n,n,n")
	  ] UNSPECV_SFPIADD))]
  "TARGET_XTT_TENSIX"
  "@
   SFPIADD\t%x0, %x3, 0, %4
   SFPIADD\t%x0, %x3, 0, %4\t# LV:%x2
   #"
  "&& true"
  {
    if (TARGET_XTT_TENSIX_QSR
        && find_reg_note (curr_insn, REG_UNUSED, operands[0])
        && cstlreg_operand (operands[2], GET_MODE (operands[2])))
      {
        emit_insn (gen_rvtt_sfpiadd_v_nv
	  (operands[2], operands[3], operands[4]));
        DONE;
      }

    if (noval_or_omit_operand (operands[1], GET_MODE (operands[1])))
      FAIL; // No need to do anything

    rvtt_merge_lv_src (&operands[1], &operands[2]);
  }
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; SFPIADD.md: "Backend execution unit: Vector Unit (SFPU), simple
   ;; sub-unit".  The write-port claim is the same Simple-unit 9(h)-class
   ;; inference already recorded for SFPSHFT/SFPCAST.
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_write_port" "shared_simple_round")
   ;; Effects audited per mod1 (operand 4) from the reference simulator
   ;; TENSIX_EXECUTE_SFPIADD + the SFPIADD.md functional model, both of
   ;; which cover WH and BH only (QSR has no simulator specification and
   ;; keeps the refusing defaults).  The proven envelope for this
   ;; register-argument pattern is mod1 <= 10 with the ARG_IMM bit
   ;; clear: reads VC (operand 3) and VB=VD (operand 2), writes VD
   ;; (operand 0), lane-predicated.  LaneFlags are written unless
   ;; MOD1_CC_NONE is set without MOD1_CC_GTE0 ((mod1 & 12) == 4);
   ;; the reference simulator and the functional model agree on that effect class for
   ;; every admitted mod (their mod-12 value divergence -- invert
   ;; vs sign-derived -- stays inside the cc-write class).  An ARG_IMM
   ;; mod in this operand shape would not read VB, so its read claim is
   ;; unproven here and it refuses.
   (set (attr "xtt_lreg_read_ops")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && IN_RANGE (INTVAL (operands[4]), 0, 10)
				   && (INTVAL (operands[4]) & 1) == 0")
		      (const_int 13) (const_int 0)))
   (set (attr "xtt_lreg_write_ops")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && IN_RANGE (INTVAL (operands[4]), 0, 10)
				   && (INTVAL (operands[4]) & 1) == 0")
		      (const_int 2) (const_int 0)))
   (set (attr "xtt_cc_effect")
	(cond [(match_test "!((TARGET_XTT_TENSIX_BH
			       || TARGET_XTT_TENSIX_WH)
			      && IN_RANGE (INTVAL (operands[4]), 0, 10)
			      && (INTVAL (operands[4]) & 1) == 0)")
		 (const_string "unknown")
	       (match_test "(INTVAL (operands[4]) & 12) == 4")
		 (const_string "read")]
	      (const_string "readwrite")))
   (set (attr "xtt_config_effect")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && IN_RANGE (INTVAL (operands[4]), 0, 10)
				   && (INTVAL (operands[4]) & 1) == 0")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_rwc_effect")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && IN_RANGE (INTVAL (operands[4]), 0, 10)
				   && (INTVAL (operands[4]) & 1) == 0")
		      (const_string "none") (const_string "unknown")))
   ;; D3 latency audit: S1 Simple; Simple dependence chains step one
   ;; slot and SFPIADD.md has no next-cycle constraint: result
   ;; latency 0 (audited mods only).
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && IN_RANGE (INTVAL (operands[4]), 0, 10)
				   && (INTVAL (operands[4]) & 1) == 0")
		      (const_int 1) (const_int 0)))
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_BH | XTT_DYNAMIC_BUG_QSR)"))
   ;; Migrated effect-override row (now typed-effect attributes): SFPIADD's mod field
   ;; can architecturally set CC -- conservative CC write, any mod.
   (set_attr "xtt_lane_local" "yes")
   (set_attr "xtt_cc_write" "yes")
   (set_attr "xtt_lane_gated" "yes")])

(define_insn "rvtt_sfpiadd_v_nv"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "cstlreg_operand" "xc")
     (match_operand:XTT32SI 1 "reg_or_cstlreg_operand" "xrxc")
     (match_operand:SI      2 "const_int_operand" "n")
     ] UNSPECV_SFPIADD)]
  "TARGET_XTT_TENSIX_QSR"
  "SFPIADD\t%x0, %x1, 0, %2"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_BH | XTT_DYNAMIC_BUG_QSR)"))
   ;; Migrated effect-override row (now typed-effect attributes): conservative CC
   ;; write, any mod; lane-gated consumer.
   (set_attr "xtt_lane_local" "yes")
   (set_attr "xtt_cc_write" "yes")
   (set_attr "xtt_lane_gated" "yes")])

(define_expand "rvtt_sfpiadd_i"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
          (match_operand:SI 1 "reg_or_0_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI 3 "reg_or_const_int_operand")
          (match_operand:SI 4 "reg_or_0_operand")
          (match_operand:SI 5 "const_int_operand")
          (match_operand:SI 6 "const_int_operand")
          ] UNSPECV_SFPIADD))]
  "TARGET_XTT_TENSIX"
{
  emit_insn (gen_rvtt_sfpiadd_i_lv
    (operands[0], operands[1], rvtt_gen_rtx_noval (XTT32SImode), operands[2],
     operands[3], operands[4], operands[5], operands[6]));
  DONE;
})

(define_expand "rvtt_sfpiadd_i_lv"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
          (match_operand:SI 1 "reg_or_0_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_or_noval_operand")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
          (match_operand:SI 4 "reg_or_const_int_operand")
          (match_operand:SI 5 "reg_or_0_operand")
          (match_operand:SI 6 "const_int_operand")
          (match_operand:SI 7 "const_int_operand")
          ] UNSPECV_SFPIADD))]
  "TARGET_XTT_TENSIX"
{
  operands[7] = GEN_INT (INTVAL (operands[7]) | SFPIADD_MOD1_ARG_IMM);
  auto mem = const0_rtx;
  auto opc = const0_rtx;
  auto enc = const0_rtx;
  auto imm = operands[4];
  if (!CONST_INT_P (imm))
    {
      mem = gen_rtx_MEM (SImode, operands[1]);
      int op
        = TARGET_XTT_TENSIX_WH 	? TT_OP_WH_SFPIADD (0, 0, 0, INTVAL (operands[7]))
	: TARGET_XTT_TENSIX_BH 	? TT_OP_BH_SFPIADD (0, 0, 0, INTVAL (operands[7]))
	: TARGET_XTT_TENSIX_QSR	? TT_OP_QSR_SFPIADD (0, 0, 0, INTVAL (operands[7]))
        : (gcc_unreachable (), 0);
      opc = GEN_INT (op);
      enc = GEN_INT (rvtt_synth (UINTVAL (operands[6])).dst_shift (4).src_shift (8));
      imm = operands[5];
    }

  emit_insn (gen_rvtt_sfpiadd_i_lv_int
    (operands[0], mem, opc, enc, imm,
     operands[3], operands[2], operands[7]));
  DONE;
})

(define_insn_and_split "rvtt_sfpiadd_i_lv_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr,xr,xr")
        (unspec_volatile:XTT32SI [
          (match_operand:SI    1 "mem_or_0_operand" "J,J,m,m")
          (match_operand:SI    2 "const_int_operand" "J,J,n,n") ;; opcode
          (match_operand:SI    3 "const_int_operand" "J,J,n,n") ;; id, src & dst shifts
          (match_operand:SI    4 "reg_or_const_int_operand" "n,n,r,r") ;; imm or insn
          (match_operand:XTT32SI 5 "reg_or_cstlreg_operand"  "xrxc,xrxc,xrxc,xrxc") ;; src
	  (match_operand:XTT32SI 6 "reg_or_cstlreg_or_noval_operand" "xn,0,xn,0") ;; lv
          (match_operand:SI    7 "const_int_operand" "n,n,n,n")
	  ] UNSPECV_SFPIADD))
   (clobber (match_scratch:SI  8 "=X,X,&r,&r"))]
  "TARGET_XTT_TENSIX"
  {
    return rvtt_synth::pattern (which_alternative >> 1,
      which_alternative & 1
      ? "SFPIADD\t%x0, %x5, %4, %7\t# LV:%x6"
      : "SFPIADD\t%x0, %x5, %4, %7",
      operands, true, 8);
  }
  "&& TARGET_XTT_TENSIX_QSR
   && find_reg_note (insn, REG_UNUSED, operands[0])"
  [(const_int 0)]
  {
    rtx opcode = operands[2];
    if (operands[1] != const0_rtx)
      {
        // Bake L15 as the dst reg here
        auto enc = rvtt_synth (INTVAL (operands[3]));
        opcode = GEN_INT (INTVAL (opcode) | (0xf << enc.dst_shift ()));
      }
    emit_insn (gen_rvtt_sfpiadd_i_nv
      (operands[1], opcode, operands[3], operands[4],
       operands[5], operands[7]));
    DONE;
  }
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; SFPIADD.md: "Backend execution unit: Vector Unit (SFPU), simple
   ;; sub-unit" -- the same Simple-unit write-port class as the
   ;; register-argument pattern above.
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_write_port" "shared_simple_round")
   ;; Effects audited per mod1 (operand 7) from the reference simulator
   ;; TENSIX_EXECUTE_SFPIADD (tensix.cpp:8894: verifies mod1 <= 10 and
   ;; (mod1 & 3) <= 2; TT_VERSION <= 1 = WH/BH) + the SFPIADD.md
   ;; functional model (QSR has no simulator specification and keeps
   ;; the refusing defaults).  The proven envelope for this
   ;; immediate-argument pattern is the ARG_IMM bit SET with the
   ;; 2SCOMP bit clear and mod1 <= 10 (mods 1, 5, 9), in the
   ;; single-word constant-immediate alternatives only (a register
   ;; immediate raises the runtime-synthesized instruction push, whose
   ;; memory effects this audit does not cover): src = LReg[VC]
   ;; (operand 5) + SignExtend(Imm12), written to VD (operand 0)
   ;; lane-predicated -- VB is NOT read (SFPIADD.md functional model:
   ;; the ARG_IMM arm reads only LReg[VC]); the lane-predicated write
   ;; keeps the live-in (operand 6, tied to the destination in the LV
   ;; alternatives) in disabled lanes, so both carry the read claim.
   ;; LaneFlags are written unless MOD1_CC_NONE is set without
   ;; MOD1_CC_GTE0 ((mod1 & 12) == 4) -- same effect class as the
   ;; register-argument audit above.
   (set (attr "xtt_lreg_read_ops")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[4])
				   && IN_RANGE (INTVAL (operands[7]), 0, 10)
				   && (INTVAL (operands[7]) & 3) == 1")
		      (const_int 98) (const_int 0)))
   (set (attr "xtt_lreg_write_ops")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[4])
				   && IN_RANGE (INTVAL (operands[7]), 0, 10)
				   && (INTVAL (operands[7]) & 3) == 1")
		      (const_int 2) (const_int 0)))
   (set (attr "xtt_cc_effect")
	(cond [(match_test "!((TARGET_XTT_TENSIX_BH
			       || TARGET_XTT_TENSIX_WH)
			      && CONST_INT_P (operands[4])
			      && IN_RANGE (INTVAL (operands[7]), 0, 10)
			      && (INTVAL (operands[7]) & 3) == 1)")
		 (const_string "unknown")
	       (match_test "(INTVAL (operands[7]) & 12) == 4")
		 (const_string "read")]
	      (const_string "readwrite")))
   (set (attr "xtt_config_effect")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[4])
				   && IN_RANGE (INTVAL (operands[7]), 0, 10)
				   && (INTVAL (operands[7]) & 3) == 1")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_rwc_effect")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[4])
				   && IN_RANGE (INTVAL (operands[7]), 0, 10)
				   && (INTVAL (operands[7]) & 3) == 1")
		      (const_string "none") (const_string "unknown")))
   ;; D3 latency audit: S1 Simple; Simple dependence chains step one
   ;; slot and SFPIADD.md has no next-cycle constraint: result
   ;; latency 0 (audited mods only).
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[4])
				   && IN_RANGE (INTVAL (operands[7]), 0, 10)
				   && (INTVAL (operands[7]) & 3) == 1")
		      (const_int 1) (const_int 0)))
   ;; Migrated effect-override row (now typed-effect attributes): conservative CC
   ;; write, any mod; lane-gated consumer.
   (set_attr "xtt_lane_local" "yes")
   (set_attr "xtt_cc_write" "yes")
   (set_attr "xtt_lane_gated" "yes")])

(define_insn "rvtt_sfpiadd_i_nv"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "mem_or_0_operand" "J,m")
     (match_operand:SI    1 "const_int_operand" "J,n") ;; opcode
     (match_operand:SI    2 "const_int_operand" "J,n") ;; id, src & dst shifts
     (match_operand:SI    3 "reg_or_const_int_operand" "n,r") ;; imm or insn
     (match_operand:XTT32SI 4 "reg_or_cstlreg_operand"  "xrxc,xrxc") ;; src
     (match_operand:SI    5 "const_int_operand" "n,n")
     ] UNSPECV_SFPIADD)
   (clobber (match_scratch:SI  6 "=X,&r"))]
  "TARGET_XTT_TENSIX_QSR"
  {
    return rvtt_synth::pattern (which_alternative,
      "SFPIADD\tL15, %x4, %3, %5",
      operands, false, 6);
  }
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; Migrated effect-override row (now typed-effect attributes): conservative CC
   ;; write, any mod; lane-gated consumer.
   (set_attr "xtt_lane_local" "yes")
   (set_attr "xtt_cc_write" "yes")
   (set_attr "xtt_lane_gated" "yes")])

(define_int_iterator rvtt_unary_op [
  UNSPECV_SFPMOV
  UNSPECV_SFPEXEXP
  UNSPECV_SFPEXMAN
  UNSPECV_SFPABS
  UNSPECV_SFPLZ
  ])
(define_int_attr rvtt_unary_name [
  (UNSPECV_SFPMOV "mov")
  (UNSPECV_SFPEXEXP "exexp")
  (UNSPECV_SFPEXMAN "exman")
  (UNSPECV_SFPABS "abs")
  (UNSPECV_SFPLZ "lz")
  ])
(define_int_attr rvtt_unary_insn [
  (UNSPECV_SFPMOV "MOV")
  (UNSPECV_SFPEXEXP "EXEXP")
  (UNSPECV_SFPEXMAN "EXMAN")
  (UNSPECV_SFPABS "ABS")
  (UNSPECV_SFPLZ "LZ")
  ])

(define_expand "rvtt_sfp<rvtt_unary_name>"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand")
          (match_operand:SI    2 "const_int_operand")
	  ] rvtt_unary_op))]
  "TARGET_XTT_TENSIX"
{
  emit_insn (gen_rvtt_sfp<rvtt_unary_name>_lv (
      operands[0], rvtt_gen_rtx_noval (XTT32SImode), operands[1], operands[2]));
  DONE;
})

(define_insn_and_split "rvtt_sfp<rvtt_unary_name>_lv"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn,0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand" "xrxc,xrxc")
          (match_operand:SI    3 "const_int_operand" "n,n")
	  ] rvtt_unary_op))]
  "TARGET_XTT_TENSIX"
  "@
   SFP<rvtt_unary_insn>\t%x0, %x2, %3
   SFP<rvtt_unary_insn>\t%x0, %x2, %3\t# LV:%x1"
  "&& TARGET_XTT_TENSIX_QSR
   && <rvtt_unary_op> == UNSPECV_SFPLZ
   && find_reg_note (insn, REG_UNUSED, operands[0])"
  [(const_int 0)]
  {
    emit_insn (gen_rvtt_sfp<rvtt_unary_name>_nv
      (operands[2], operands[3]));
    DONE;
  }
  ;; Effect audit (D3 latency audit, WH/BH), per mod1 (operand 3) against
  ;; the the reference simulator executors (TENSIX_EXECUTE_SFPMOV/SFPEXEXP/SFPEXMAN/
  ;; SFPABS/SFPLZ): each reads operand 2 and lane-writes operand 0 (tied
  ;; live value read for disabled lanes); no configuration or counter
  ;; effect.  CC: SFPMOV mod 0/1 and every other audited mod are
  ;; lane-predicated reads; SFPMOV mod 2 is the all-lanes copy (no CC
  ;; access); SFPEXEXP mod 2/10 and SFPLZ mod 2 additionally set the
  ;; lane flag from the result (readwrite).  Unproven mods (SFPMOV 8 =
  ;; PRNG state advance; anything the simulator refuses) keep the
  ;; refusing defaults.  Sub-unit: all five opcodes sit in the S1 Simple
  ;; column; every proven Simple dependence chain steps one slot
  ;; (frozen signbit calendar shift->cast->store; hand exp kernel
  ;; exexp->exman->shft->exman->cast back-to-back on hardware): result
  ;; latency 0.
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set (attr "xtt_subunit")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && (<rvtt_unary_op> == UNSPECV_SFPMOV
				       ? IN_RANGE (INTVAL (operands[3]), 0, 2)
				       : <rvtt_unary_op> == UNSPECV_SFPEXEXP
				       ? (IN_RANGE (INTVAL (operands[3]), 0, 2)
					  || INTVAL (operands[3]) == 10)
				       : <rvtt_unary_op> == UNSPECV_SFPLZ
				       ? (INTVAL (operands[3]) == 0
					  || INTVAL (operands[3]) == 2
					  || INTVAL (operands[3]) == 4)
				       : IN_RANGE (INTVAL (operands[3]), 0, 1))")
		      (const_string "simple") (const_string "none")))
   (set (attr "xtt_lreg_read_ops")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && (<rvtt_unary_op> == UNSPECV_SFPMOV
				       ? IN_RANGE (INTVAL (operands[3]), 0, 2)
				       : <rvtt_unary_op> == UNSPECV_SFPEXEXP
				       ? (IN_RANGE (INTVAL (operands[3]), 0, 2)
					  || INTVAL (operands[3]) == 10)
				       : <rvtt_unary_op> == UNSPECV_SFPLZ
				       ? (INTVAL (operands[3]) == 0
					  || INTVAL (operands[3]) == 2
					  || INTVAL (operands[3]) == 4)
				       : IN_RANGE (INTVAL (operands[3]), 0, 1))")
		      (const_int 8) (const_int 0)))
   (set (attr "xtt_lreg_write_ops")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && (<rvtt_unary_op> == UNSPECV_SFPMOV
				       ? IN_RANGE (INTVAL (operands[3]), 0, 2)
				       : <rvtt_unary_op> == UNSPECV_SFPEXEXP
				       ? (IN_RANGE (INTVAL (operands[3]), 0, 2)
					  || INTVAL (operands[3]) == 10)
				       : <rvtt_unary_op> == UNSPECV_SFPLZ
				       ? (INTVAL (operands[3]) == 0
					  || INTVAL (operands[3]) == 2
					  || INTVAL (operands[3]) == 4)
				       : IN_RANGE (INTVAL (operands[3]), 0, 1))")
		      (const_int 2) (const_int 0)))
   (set (attr "xtt_cc_effect")
	(cond [(match_test "!((TARGET_XTT_TENSIX_BH
			       || TARGET_XTT_TENSIX_WH)
			      && (<rvtt_unary_op> == UNSPECV_SFPMOV
				  ? IN_RANGE (INTVAL (operands[3]), 0, 2)
				  : <rvtt_unary_op> == UNSPECV_SFPEXEXP
				  ? (IN_RANGE (INTVAL (operands[3]), 0, 2)
				     || INTVAL (operands[3]) == 10)
				  : <rvtt_unary_op> == UNSPECV_SFPLZ
				  ? (INTVAL (operands[3]) == 0
				     || INTVAL (operands[3]) == 2
				     || INTVAL (operands[3]) == 4)
				  : IN_RANGE (INTVAL (operands[3]), 0, 1)))")
		 (const_string "unknown")
	       (match_test "(<rvtt_unary_op> == UNSPECV_SFPEXEXP
			     && (INTVAL (operands[3]) == 2
				 || INTVAL (operands[3]) == 10))
			    || (<rvtt_unary_op> == UNSPECV_SFPLZ
				&& INTVAL (operands[3]) == 2)")
		 (const_string "readwrite")
	       (match_test "<rvtt_unary_op> == UNSPECV_SFPMOV
			    && INTVAL (operands[3]) == 2")
		 (const_string "none")]
	      (const_string "read")))
   (set (attr "xtt_config_effect")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && (<rvtt_unary_op> == UNSPECV_SFPMOV
				       ? IN_RANGE (INTVAL (operands[3]), 0, 2)
				       : <rvtt_unary_op> == UNSPECV_SFPEXEXP
				       ? (IN_RANGE (INTVAL (operands[3]), 0, 2)
					  || INTVAL (operands[3]) == 10)
				       : <rvtt_unary_op> == UNSPECV_SFPLZ
				       ? (INTVAL (operands[3]) == 0
					  || INTVAL (operands[3]) == 2
					  || INTVAL (operands[3]) == 4)
				       : IN_RANGE (INTVAL (operands[3]), 0, 1))")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_rwc_effect")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && (<rvtt_unary_op> == UNSPECV_SFPMOV
				       ? IN_RANGE (INTVAL (operands[3]), 0, 2)
				       : <rvtt_unary_op> == UNSPECV_SFPEXEXP
				       ? (IN_RANGE (INTVAL (operands[3]), 0, 2)
					  || INTVAL (operands[3]) == 10)
				       : <rvtt_unary_op> == UNSPECV_SFPLZ
				       ? (INTVAL (operands[3]) == 0
					  || INTVAL (operands[3]) == 2
					  || INTVAL (operands[3]) == 4)
				       : IN_RANGE (INTVAL (operands[3]), 0, 1))")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && (<rvtt_unary_op> == UNSPECV_SFPMOV
				       ? IN_RANGE (INTVAL (operands[3]), 0, 2)
				       : <rvtt_unary_op> == UNSPECV_SFPEXEXP
				       ? (IN_RANGE (INTVAL (operands[3]), 0, 2)
					  || INTVAL (operands[3]) == 10)
				       : <rvtt_unary_op> == UNSPECV_SFPLZ
				       ? (INTVAL (operands[3]) == 0
					  || INTVAL (operands[3]) == 2
					  || INTVAL (operands[3]) == 4)
				       : IN_RANGE (INTVAL (operands[3]), 0, 1))")
		      (const_int 1) (const_int 0)))
   ;; Migrated effect-override rows (now typed-effect attributes), keyed per insn
   ;; code and deliberately mod-independent: pure value unaries;
   ;; SFPEXEXP/SFPLZ mod fields can architecturally set CC
   ;; (conservative CC write), the rest never touch CC.
   (set_attr "xtt_lane_local" "yes")
   (set (attr "xtt_cc_write")
	(if_then_else (match_test "<rvtt_unary_op> == UNSPECV_SFPEXEXP
				   || <rvtt_unary_op> == UNSPECV_SFPLZ")
		      (const_string "yes") (const_string "no")))
   (set_attr "xtt_lane_gated" "yes")])

(define_insn "rvtt_sfp<rvtt_unary_name>_nv"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "reg_or_cstlreg_operand" "xrxc")
     (match_operand:SI    1 "const_int_operand" "n")
     ] rvtt_unary_op)]
  "TARGET_XTT_TENSIX_QSR && <rvtt_unary_op> == UNSPECV_SFPLZ"
  "SFP<rvtt_unary_insn>\tL15, %x0, %1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; Migrated effect-override rows (now typed-effect attributes): same per-family
   ;; classification as the _lv forms above.
   (set_attr "xtt_lane_local" "yes")
   (set (attr "xtt_cc_write")
	(if_then_else (match_test "<rvtt_unary_op> == UNSPECV_SFPEXEXP
				   || <rvtt_unary_op> == UNSPECV_SFPLZ")
		      (const_string "yes") (const_string "no")))
   (set_attr "xtt_lane_gated" "yes")])

