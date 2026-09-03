;; Machine description for Tenstorrent SFPU Intrinsics -- field and bit operations.
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

;; Field-set words (exponent, mantissa, sign), the bitwise logical
;; family, and the shift words.
(define_int_iterator rvtt_set_op [
  UNSPECV_SFPSETEXP
  UNSPECV_SFPSETMAN
  UNSPECV_SFPSETSGN
  ])
(define_int_attr rvtt_set_name [
  (UNSPECV_SFPSETEXP "exp")
  (UNSPECV_SFPSETMAN "man")
  (UNSPECV_SFPSETSGN "sgn")
  ])
(define_int_attr rvtt_set_insn [
  (UNSPECV_SFPSETEXP "EXP")
  (UNSPECV_SFPSETMAN "MAN")
  (UNSPECV_SFPSETSGN "SGN")
  ])

(define_expand "rvtt_sfpset<rvtt_set_name>_v"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
          (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI      3 "const_int_operand")
	  ] rvtt_set_op))]
  "TARGET_XTT_TENSIX"
  {
    emit_insn (gen_rvtt_sfpset<rvtt_set_name>_v_lv
      (operands[0], rvtt_gen_rtx_noval (XTT32SImode),
       operands[1], operands[2], operands[3]));
    DONE;
  })

(define_insn_and_rewrite "rvtt_sfpset<rvtt_set_name>_v_lv"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr,xr")
        (unspec_volatile:XTT32SI [
          (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_or_omit_operand" "xn,xo,xrxc")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand" "xrxc,xrxc,xrxc")
	  (match_operand:XTT32SI 3 "reg_or_cstlreg_operand" "0,0,0")
          (match_operand:SI      4 "const_int_operand" "n,n,n")
	  ] rvtt_set_op))]
  "TARGET_XTT_TENSIX"
  "@
   SFPSET<rvtt_set_insn>\t%x0, %x2, 0, %4
   SFPSET<rvtt_set_insn>\t%x0, %x2, 0, %4\t# LV:%x3
   #"
  "&& !noval_or_omit_operand (operands[1], GET_MODE (operands[1]))"
  {
    rvtt_merge_lv_src (&operands[1], &operands[3]);
  }
  ;; Effect audit (D3 latency audit, WH/BH), per mod1 (operand 4)
  ;; against the reference simulator's TENSIX_EXECUTE_SFPSETEXP/SFPSETMAN/SFPSETSGN:
  ;; the register forms (SETEXP mod 0/2, SETMAN mod 0, SETSGN mod 0)
  ;; read the source (operand 2) and the tied destination, lane-write
  ;; the destination, touch no CC bit, configuration word, or counter
  ;; (mod 1 is the immediate form carried by the _i patterns; higher
  ;; mods are simulator-refused and keep the refusing defaults).
  ;; Sub-unit: S1 Simple column; Simple dependence chains step one slot
  ;; (hardware-proven hand exp kernel runs SFPAND->SFPSETEXP and
  ;; SFPSETEXP->SFPSTOCHRND back-to-back): result latency 0.
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set (attr "xtt_subunit")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && (INTVAL (operands[4]) == 0
				       || (<rvtt_set_op> == UNSPECV_SFPSETEXP
					   && INTVAL (operands[4]) == 2))")
		      (const_string "simple") (const_string "none")))
   (set (attr "xtt_lreg_read_ops")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && (INTVAL (operands[4]) == 0
				       || (<rvtt_set_op> == UNSPECV_SFPSETEXP
					   && INTVAL (operands[4]) == 2))")
		      (const_int 16) (const_int 0)))
   (set (attr "xtt_lreg_write_ops")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && (INTVAL (operands[4]) == 0
				       || (<rvtt_set_op> == UNSPECV_SFPSETEXP
					   && INTVAL (operands[4]) == 2))")
		      (const_int 2) (const_int 0)))
   (set (attr "xtt_cc_effect")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && (INTVAL (operands[4]) == 0
				       || (<rvtt_set_op> == UNSPECV_SFPSETEXP
					   && INTVAL (operands[4]) == 2))")
		      (const_string "read") (const_string "unknown")))
   (set (attr "xtt_config_effect")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && (INTVAL (operands[4]) == 0
				       || (<rvtt_set_op> == UNSPECV_SFPSETEXP
					   && INTVAL (operands[4]) == 2))")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_rwc_effect")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && (INTVAL (operands[4]) == 0
				       || (<rvtt_set_op> == UNSPECV_SFPSETEXP
					   && INTVAL (operands[4]) == 2))")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && (INTVAL (operands[4]) == 0
				       || (<rvtt_set_op> == UNSPECV_SFPSETEXP
					   && INTVAL (operands[4]) == 2))")
		      (const_int 1) (const_int 0)))
   ;; Migrated effect-override rows (now typed-effect attributes): field inserts
   ;; never touch CC; lane-gated consumers.
   (set_attr "xtt_lane_local" "yes")
   (set_attr "xtt_cc_write" "no")
   (set_attr "xtt_lane_gated" "yes")])

(define_expand "rvtt_sfpset<rvtt_set_name>_i"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "reg_or_0_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_0_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] rvtt_set_op))]
  "TARGET_XTT_TENSIX"
{
  emit_insn (gen_rvtt_sfpset<rvtt_set_name>_i_lv
    (operands[0], operands[1], rvtt_gen_rtx_noval (XTT32SImode),
     operands[2], operands[3], operands[4], operands[5], operands[6]));
  DONE;
})

(define_expand "rvtt_sfpset<rvtt_set_name>_i_lv"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "reg_or_0_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_or_noval_operand")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
          (match_operand:SI    4 "reg_or_const_int_operand")
          (match_operand:SI    5 "reg_or_0_operand")
          (match_operand:SI    6 "const_int_operand")
          (match_operand:SI    7 "const_int_operand")
	  ] rvtt_set_op))]
  "TARGET_XTT_TENSIX"
{
  operands[7] = GEN_INT (INTVAL (operands[7]) | SFPSET<rvtt_set_insn>_MOD1_IMM);
  auto mem = const0_rtx;
  auto opc = const0_rtx;
  auto enc = const0_rtx;
  auto imm = operands[4];
  if (!CONST_INT_P (imm))
    {
      // This should have been expanded at by synth-expand
      gcc_assert (<rvtt_set_op> != UNSPECV_SFPSETMAN);

      mem = gen_rtx_MEM (SImode, operands[1]);
      int op
        = TARGET_XTT_TENSIX_WH  ? TT_OP_WH_SFPSET<rvtt_set_insn> (0, 0, 0, INTVAL (operands[7]))
        : TARGET_XTT_TENSIX_BH  ? TT_OP_BH_SFPSET<rvtt_set_insn> (0, 0, 0, INTVAL (operands[7]))
        : TARGET_XTT_TENSIX_QSR ? TT_OP_QSR_SFPSET<rvtt_set_insn> (0, 0, 0, INTVAL (operands[7]))
        : (gcc_unreachable (), 0);
      opc = GEN_INT (op);
      enc = GEN_INT (rvtt_synth (UINTVAL (operands[6])).src_shift (4).dst_shift (8));
      imm = operands[5];
    }

  emit_insn (gen_rvtt_sfpset<rvtt_set_name>_i_lv_int
    (operands[0], mem, opc, enc, imm,
     operands[3], operands[2], operands[7]));
  DONE;
})

(define_insn "rvtt_sfpset<rvtt_set_name>_i_lv_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr,xr,xr")
        (unspec_volatile:XTT32SI [
          (match_operand:SI    1 "mem_or_0_operand" "J,J,n,m")
          (match_operand:SI    2 "const_int_operand" "J,J,n,n") ;; opcode
          (match_operand:SI    3 "const_int_operand" "J,J,n,n") ;; id, src & dst shifts
          (match_operand:SI    4 "reg_or_const_int_operand" "n,n,r,r") ;; imm or insn
          (match_operand:XTT32SI 5 "reg_or_cstlreg_operand" "xrxc,xrxc,xrxc,xrxc") ;; src
          (match_operand:XTT32SI 6 "reg_or_cstlreg_or_noval_operand" "xn,0,xn,0") ;; lv
          (match_operand:SI    7 "const_int_operand"  "n,n,n,n")
          ] rvtt_set_op))
   (clobber (match_scratch:SI  8 "=X,X,&r,&r"))]
  "TARGET_XTT_TENSIX"
  {
    return rvtt_synth::pattern (which_alternative >> 1,
      which_alternative & 1
      ? "SFPSET<rvtt_set_insn>\t%x0, %x5, %4, %7\t# LV:%x6"
      : "SFPSET<rvtt_set_insn>\t%x0, %x5, %4, %7",
      operands, true, 8);
  }
  ;; Effect audit extension (WH/BH, the mod-1
  ;; immediate arm the register-form audit above names): [SIM] the
  ;; reference simulator's TENSIX_EXECUTE_SFPSETEXP/SFPSETMAN/SFPSETSGN mod-1 arms read the
  ;; source and the tied destination, lane-write the destination, touch
  ;; no CC bit, configuration word, or counter; [ISA] the SFPSET*.md
  ;; immediate arms carry no next-cycle result-read rule (the audited
  ;; latency-0 page convention); [HAND] the hardware-proven hand
  ;; signbit/round-class calendars record and replay immediate-form
  ;; SETSGN back-to-back.  S1 Simple column, result latency 0.  Every
  ;; other mod keeps the refusing defaults.
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set (attr "xtt_subunit")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && INTVAL (operands[7]) == 1")
		      (const_string "simple") (const_string "none")))
   (set (attr "xtt_lreg_read_ops")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && INTVAL (operands[7]) == 1")
		      (const_int 97) (const_int 0)))
   (set (attr "xtt_lreg_write_ops")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && INTVAL (operands[7]) == 1")
		      (const_int 2) (const_int 0)))
   (set (attr "xtt_cc_effect")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && INTVAL (operands[7]) == 1")
		      (const_string "read") (const_string "unknown")))
   (set (attr "xtt_config_effect")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && INTVAL (operands[7]) == 1")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_rwc_effect")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && INTVAL (operands[7]) == 1")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && INTVAL (operands[7]) == 1")
		      (const_int 1) (const_int 0)))
   ;; Migrated effect-override rows (now typed-effect attributes): field inserts
   ;; never touch CC; lane-gated consumers.
   (set_attr "xtt_lane_local" "yes")
   (set_attr "xtt_cc_write" "no")
   (set_attr "xtt_lane_gated" "yes")])

(define_int_iterator rvtt_logical_op [
  UNSPECV_SFPAND
  UNSPECV_SFPOR
  UNSPECV_SFPXOR
  ])
(define_int_attr rvtt_logical_name [
  (UNSPECV_SFPAND "and")
  (UNSPECV_SFPOR "or")
  (UNSPECV_SFPXOR "xor")
  ])
(define_int_attr rvtt_logical_insn [
  (UNSPECV_SFPAND "AND")
  (UNSPECV_SFPOR "OR")
  (UNSPECV_SFPXOR "XOR")
  ])

(define_expand "rvtt_sfp<rvtt_logical_name>"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
	  ] rvtt_logical_op))]
  "TARGET_XTT_TENSIX"
  {
    emit_insn (gen_rvtt_sfp<rvtt_logical_name>_lv
      (operands[0], rvtt_gen_rtx_noval (XTT32SImode),
       operands[1], operands[2]));
    DONE;
  })

(define_expand "rvtt_sfp<rvtt_logical_name>_lv"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
	  ] rvtt_logical_op))]
  "TARGET_XTT_TENSIX"
  {
    rtx insn = nullptr;
    if (<rvtt_logical_op> == UNSPECV_SFPXOR
        || TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_QSR)
      insn = gen_rvtt_sfp<rvtt_logical_name>_lv_2op
        (operands[0], operands[1], operands[2], operands[3]);
    else
      {
        // There is no XOR use of VB, but this needs to be compilable
	constexpr unsigned SFPXOR_MOD1_USE_VB __attribute__((unused)) = ~0u;
        insn = gen_rvtt_sfp<rvtt_logical_name>_lv_bh
          (operands[0], operands[1], operands[2], operands[3],
           GEN_INT (SFP<rvtt_logical_insn>_MOD1_USE_VB));
      }	   
    emit_insn (insn);
    DONE;
  })

(define_insn_and_rewrite "rvtt_sfp<rvtt_logical_name>_lv_2op"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_or_omit_operand" "xn,xo,xrxc")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand" "0,0,0")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand" "xrxc,xrxc,xrxc")
	  ] rvtt_logical_op))]
  "<rvtt_logical_op> == UNSPECV_SFPXOR
   || TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_QSR"
  "@
   SFP<rvtt_logical_insn>\t%x0, %x3
   SFP<rvtt_logical_insn>\t%x0, %x3\t# LV:%x2
   #"
  "&& !noval_or_omit_operand (operands[1], GET_MODE (operands[1]))"
  {
    rvtt_merge_lv_src (&operands[1], &operands[2]);
  }
  ;; Effect audit (D3 latency audit, WH/BH): the reference simulator
  ;; TENSIX_EXECUTE_SFPAND/SFPOR/SFPXOR (tensix_execute_sfpu_int32)
  ;; read the tied destination and operand 3 and lane-write the
  ;; destination; no CC write, configuration, or counter effect.
  ;; Sub-unit: S1 Simple; the hardware-proven hand exp kernel runs
  ;; SFPAND->SFPSETEXP back-to-back: result latency 0.
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set (attr "xtt_subunit")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   || TARGET_XTT_TENSIX_WH")
		      (const_string "simple") (const_string "none")))
   (set (attr "xtt_lreg_read_ops")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   || TARGET_XTT_TENSIX_WH")
		      (const_int 16) (const_int 0)))
   (set (attr "xtt_lreg_write_ops")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   || TARGET_XTT_TENSIX_WH")
		      (const_int 2) (const_int 0)))
   (set (attr "xtt_cc_effect")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   || TARGET_XTT_TENSIX_WH")
		      (const_string "read") (const_string "unknown")))
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
		      (const_int 1) (const_int 0)))
   ;; Lane-gated consumer (typed-effect attributes).
   (set_attr "xtt_lane_gated" "yes")])

(define_insn "rvtt_sfp<rvtt_logical_name>_lv_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn,0")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand" "xrxc,xrxc")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand" "xrxc,xrxc")
          (match_operand:SI      4 "const_int_operand"  "n,n")
	  ] rvtt_logical_op))]
  "<rvtt_logical_op> != UNSPECV_SFPXOR && TARGET_XTT_TENSIX_BH"
  "@
   SFP<rvtt_logical_insn>\t%x0, %x2, %x3, %4
   SFP<rvtt_logical_insn>\t%x0, %x2, %x3, %4\t# LV:%1"
  ;; Effect audit (D3 latency audit, BH): the reference simulator mod1<=1 branch of
  ;; TENSIX_EXECUTE_SFPAND/SFPOR -- reads operands 2 and 3 (and the tied
  ;; live value), lane-writes the destination; no CC write,
  ;; configuration, or counter effect.  Higher mods are simulator-
  ;; refused and keep the refusing defaults.  S1 Simple; result latency
  ;; 0 (hand exp kernel SFPAND->SFPSETEXP back-to-back on hardware).
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set (attr "xtt_subunit")
	(if_then_else (match_test "IN_RANGE (INTVAL (operands[4]), 0, 1)")
		      (const_string "simple") (const_string "none")))
   (set (attr "xtt_lreg_read_ops")
	(if_then_else (match_test "IN_RANGE (INTVAL (operands[4]), 0, 1)")
		      (const_int 16) (const_int 0)))
   (set (attr "xtt_lreg_write_ops")
	(if_then_else (match_test "IN_RANGE (INTVAL (operands[4]), 0, 1)")
		      (const_int 2) (const_int 0)))
   (set (attr "xtt_cc_effect")
	(if_then_else (match_test "IN_RANGE (INTVAL (operands[4]), 0, 1)")
		      (const_string "read") (const_string "unknown")))
   (set (attr "xtt_config_effect")
	(if_then_else (match_test "IN_RANGE (INTVAL (operands[4]), 0, 1)")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_rwc_effect")
	(if_then_else (match_test "IN_RANGE (INTVAL (operands[4]), 0, 1)")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "IN_RANGE (INTVAL (operands[4]), 0, 1)")
		      (const_int 1) (const_int 0)))
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_BH)"))
   ;; Lane-gated consumer (typed-effect attributes).
   (set_attr "xtt_lane_gated" "yes")])

(define_expand "rvtt_sfpnot"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "xrxc")
	  ] UNSPECV_SFPNOT))]
  "TARGET_XTT_TENSIX"
  {
    emit_insn (gen_rvtt_sfpnot_lv (operands[0], rvtt_gen_rtx_noval (XTT32SImode), operands[1]));
    DONE;
  })

(define_insn "rvtt_sfpnot_lv"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand"  "xn,0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc,xrxc")
	  ] UNSPECV_SFPNOT))]
  "TARGET_XTT_TENSIX"
  "@
   SFPNOT\t%x0, %x2
   SFPNOT\t%x0, %x2\t# LV:%x1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; Lane-gated consumer (typed-effect attributes).
   (set_attr "xtt_lane_gated" "yes")])

(define_expand "rvtt_sfpshft_v"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "const_int_operand")
	  ] UNSPECV_SFPSHFT))]
  "TARGET_XTT_TENSIX"
  {
    emit_insn (gen_rvtt_sfpshft_v_lv
      (operands[0], rvtt_gen_rtx_noval (XTT32SImode),
       operands[1], operands[2], operands[3]));
    DONE;
  })

(define_expand "rvtt_sfpshft_v_lv"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
          (match_operand:SI    4 "const_int_operand")
	  ] UNSPECV_SFPSHFT))]
  "TARGET_XTT_TENSIX"
  {
    emit_insn (gen_rvtt_sfpshft_v_lv_int
        (operands[0], operands[1], operands[2], operands[3], operands[4]));
    DONE;
  })

(define_insn_and_rewrite "rvtt_sfpshft_v_lv_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_or_omit_operand"  "xn,xo,xrxc")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "0,0,0")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "xrxc,xrxc,xrxc")
          (match_operand:SI    4 "const_int_operand"  "n,n,n")
	  ] UNSPECV_SFPSHFT))]
  "TARGET_XTT_TENSIX"
  "@
   SFPSHFT\t%x0, %x3, 0, %4
   SFPSHFT\t%x0, %x3, 0, %4\t# LV:%x2
   #"
  "&& !noval_or_omit_operand (operands[1], GET_MODE (operands[1]))"
  {
    rvtt_merge_lv_src (&operands[1], &operands[2]);
  }
  ;; Effect audit (D3 latency audit), per mod1 (operand 4) against
  ;; the reference simulator's TENSIX_EXECUTE_SFPSHFT: the variable-shift forms (mod 0
  ;; logical on both, mod 2 arithmetic-right on BH only) read the shift
  ;; amount (operand 3) and the tied destination, lane-write the
  ;; destination, touch no CC bit, configuration word, or counter.
  ;; Sub-unit: S1 Simple; the hardware-proven hand exp kernel runs
  ;; SFPSHFT->SFPEXMAN back-to-back and SFPSHFT.md has no next-cycle
  ;; constraint: result latency 0.
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set (attr "xtt_subunit")
	(if_then_else (match_test "INTVAL (operands[4]) == 0
				   ? (TARGET_XTT_TENSIX_BH
				      || TARGET_XTT_TENSIX_WH)
				   : (TARGET_XTT_TENSIX_BH
				      && INTVAL (operands[4]) == 2)")
		      (const_string "simple") (const_string "none")))
   (set (attr "xtt_lreg_read_ops")
	(if_then_else (match_test "INTVAL (operands[4]) == 0
				   ? (TARGET_XTT_TENSIX_BH
				      || TARGET_XTT_TENSIX_WH)
				   : (TARGET_XTT_TENSIX_BH
				      && INTVAL (operands[4]) == 2)")
		      (const_int 16) (const_int 0)))
   (set (attr "xtt_lreg_write_ops")
	(if_then_else (match_test "INTVAL (operands[4]) == 0
				   ? (TARGET_XTT_TENSIX_BH
				      || TARGET_XTT_TENSIX_WH)
				   : (TARGET_XTT_TENSIX_BH
				      && INTVAL (operands[4]) == 2)")
		      (const_int 2) (const_int 0)))
   (set (attr "xtt_cc_effect")
	(if_then_else (match_test "INTVAL (operands[4]) == 0
				   ? (TARGET_XTT_TENSIX_BH
				      || TARGET_XTT_TENSIX_WH)
				   : (TARGET_XTT_TENSIX_BH
				      && INTVAL (operands[4]) == 2)")
		      (const_string "read") (const_string "unknown")))
   (set (attr "xtt_config_effect")
	(if_then_else (match_test "INTVAL (operands[4]) == 0
				   ? (TARGET_XTT_TENSIX_BH
				      || TARGET_XTT_TENSIX_WH)
				   : (TARGET_XTT_TENSIX_BH
				      && INTVAL (operands[4]) == 2)")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_rwc_effect")
	(if_then_else (match_test "INTVAL (operands[4]) == 0
				   ? (TARGET_XTT_TENSIX_BH
				      || TARGET_XTT_TENSIX_WH)
				   : (TARGET_XTT_TENSIX_BH
				      && INTVAL (operands[4]) == 2)")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "INTVAL (operands[4]) == 0
				   ? (TARGET_XTT_TENSIX_BH
				      || TARGET_XTT_TENSIX_WH)
				   : (TARGET_XTT_TENSIX_BH
				      && INTVAL (operands[4]) == 2)")
		      (const_int 1) (const_int 0)))
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_BH | XTT_DYNAMIC_BUG_QSR)"))
   ;; Lane-gated consumer (typed-effect attributes).  The starred immediate-shift
   ;; forms are deliberately NOT annotated: the migrated allowlist named
   ;; only the never-recognized sfpshft_i expand codes, so the effective
   ;; membership -- this register-shift pattern only -- is preserved
   ;; verbatim.
   (set_attr "xtt_lane_gated" "yes")])

(define_expand "rvtt_sfpshft_i"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "reg_or_0_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_0_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] UNSPECV_SFPSHFT))]
  "TARGET_XTT_TENSIX"
  {
    emit_insn (gen_rvtt_sfpshft_i_lv
        (operands[0], operands[1], rvtt_gen_rtx_noval (XTT32SImode),
         operands[2], operands[3], operands[4], operands[5], operands[6]));
    DONE;
  })

(define_expand "rvtt_sfpshft_i_lv"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "reg_or_0_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_or_noval_operand")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
          (match_operand:SI    4 "reg_or_const_int_operand")
          (match_operand:SI    5 "reg_or_0_operand")
          (match_operand:SI    6 "const_int_operand")
          (match_operand:SI    7 "const_int_operand")
	  ] UNSPECV_SFPSHFT))]
  "TARGET_XTT_TENSIX"
{
  unsigned mod = SFPSHFT_MOD1_SHFT_IMM
      | (TARGET_XTT_TENSIX_BH_QSR ? SFPSHFT_MOD1_SRC_LREG_C : 0);
  operands[7] = GEN_INT (INTVAL (operands[7]) | mod);

  auto mem = const0_rtx;
  auto opc = const0_rtx;
  auto enc = const0_rtx;
  auto imm = operands[4];
  if (!CONST_INT_P (imm))
    {
      mem = gen_rtx_MEM (SImode, operands[1]);
      int op
        = TARGET_XTT_TENSIX_WH 	? TT_OP_WH_SFPSHFT (0, 0, 0, INTVAL (operands[7]))
        : TARGET_XTT_TENSIX_BH  ? TT_OP_BH_SFPSHFT (0, 0, 0, INTVAL (operands[7]))
        : TARGET_XTT_TENSIX_QSR ? TT_OP_QSR_SFPSHFT (0, 0, 0, INTVAL (operands[7]))
        : (gcc_unreachable (), 0);
      opc = GEN_INT (op);
      enc = GEN_INT (rvtt_synth (UINTVAL (operands[6]))
                    .src_shift (TARGET_XTT_TENSIX_WH ? 4 : TARGET_XTT_TENSIX_BH_QSR ? 8 : 0)
		    .dst_shift (4));
      imm = operands[5];
    }

  emit_insn (gen_rvtt_sfpshft_i_lv_int
    (operands[0], mem, opc, enc, imm,
     operands[3], operands[2],
     operands[7]));
  DONE;
})

(define_expand "rvtt_sfpshft_i_lv_int"
  [(parallel [
     (set (match_operand:XTT32SI 0 "register_operand")
          (unspec_volatile:XTT32SI [
            (match_operand:SI    1 "mem_or_0_operand")
            (match_operand:SI    2 "const_int_operand") ;; opcode
            (match_operand:SI    3 "const_int_operand") ;; id, src & dst shifts
            (match_operand:SI    4 "reg_or_const_int_operand") ;; imm or insn
            (match_operand:XTT32SI 5 "reg_or_cstlreg_operand") ;; src
            (match_operand:XTT32SI 6 "reg_or_cstlreg_or_noval_operand") ;; lv
            (match_operand:SI    7 "const_int_operand")
            ] UNSPECV_SFPSHFT))
       (clobber (match_scratch:SI  8))])]
  "TARGET_XTT_TENSIX")

(define_insn "*rvtt_sfpshft_i_lv_3op"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr,xr,xr")
        (unspec_volatile:XTT32SI [
          (match_operand:SI    1 "mem_or_0_operand" "J,J,m,m")
          (match_operand:SI    2 "const_int_operand" "J,J,n,n") ;; opcode
          (match_operand:SI    3 "const_int_operand" "J,J,n,n") ;; id, src & dst shifts
          (match_operand:SI    4 "reg_or_const_int_operand" "n,n,r,r") ;; imm or insn
          (match_operand:XTT32SI 5 "reg_or_cstlreg_operand" "xrxc,xrxc,xrxc,xrxc") ;; src
          (match_operand:XTT32SI 6 "reg_or_cstlreg_or_noval_operand" "xn,0,xn,0") ;; lv
          (match_operand:SI    7 "const_int_operand" "n,n,n,n")
	  ] UNSPECV_SFPSHFT))
   (clobber (match_scratch:SI  8 "=X,X,&r,&r"))]
  "TARGET_XTT_TENSIX_BH_QSR"
  {
    return rvtt_synth::pattern (which_alternative >> 1,
      which_alternative & 1
      ? "SFPSHFT\t%x0, %x5, %4, %7\t# LV:%x6"
      : "SFPSHFT\t%x0, %x5, %4, %7",
      operands, true, 8);
  }
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; Subunit is an inference, not a documented fact: the shift is a
   ;; Simple-unit event by analogy with the documented cast-round
   ;; Simple assignments; flagged for the architectural reference.
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_write_port" "shared_simple_round")
   (set_attr "xtt_lreg_read_ops" "97")
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

(define_insn_and_rewrite "*rvtt_sfpshft_i_lv_2op"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr,xr,xr,xr,xr")
        (unspec_volatile:XTT32SI [
          (match_operand:SI    1 "mem_or_0_operand" "J,J,J,m,m,m")
          (match_operand:SI    2 "const_int_operand" "J,J,J,n,n,n") ;; opcode
          (match_operand:SI    3 "const_int_operand" "J,J,J,n,n,n") ;; id, src & dst shifts
          (match_operand:SI    4 "reg_or_const_int_operand" "n,n,n,r,r,r") ;; imm or insn
          (match_operand:XTT32SI 5 "reg_or_cstlreg_operand" "0,0,0,0,0,0") ;; src
          (match_operand:XTT32SI 6 "reg_or_cstlreg_or_noval_or_omit_operand" "xn,xo,xrxc,xn,xo,xrxc") ;; lv
          (match_operand:SI    7 "const_int_operand" "n,n,n,n,n,n")
	  ] UNSPECV_SFPSHFT))
   (clobber (match_scratch:SI  8 "=X,X,X,&r,&r,&r"))]
  "TARGET_XTT_TENSIX_WH"
  {
    int which = which_alternative;
    bool dyn = which >= 3;
    if (dyn)
      which -= 3;
    return rvtt_synth::pattern (dyn,
      which == 0 ? "SFPSHFT\t%x0, L0, %4, %7" :
      which == 1 ? "SFPSHFT\t%x0, L0, %4, %7\t# LV:%x5" :
      "#",
      operands, true, 8);
  }
  "&& !noval_or_omit_operand (operands[6], GET_MODE (operands[6]))"
  {
    rvtt_merge_lv_src (&operands[6], &operands[5]);
  }
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; Same 9(h)-class Simple-unit inference as the BH/QSR form above.
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_write_port" "shared_simple_round")
   (set_attr "xtt_lreg_read_ops" "97")
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

