;; Machine description for Tenstorrent SFPU Intrinsics.
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

;; WH  - ISA 2.0
;; BH  - ISA 3.0
;; QSR - ISA 4.0 (NEO)
;; TRI - ISA 4.1 (Not Yet Supported)

(include "tt/rvtt-predicates.md")
(include "tt/rvtt-tune.md")
(include "tt/rvtt-cost.md")

(define_c_enum "unspec" [
  UNSPEC_SYNTH_OPCODE
  UNSPEC_SFPCSTLREG
  UNSPEC_SFPNOVAL
  UNSPEC_SFPOMIT
  UNSPEC_SFPCLEAVE ; cleave together and cleave apart, yay auto-antonyms!
])

(define_c_enum "unspecv" [
  ;; Tenstorrent SFPU unspecs.
  ;; INT for internal
  ;; IMM for immediate
  ;; LV for keep dst reg alive as input for predicated liveness

  UNSPECV_SFPVARLREG
  UNSPECV_SFPRAWLREG_ACCESS
  UNSPECV_TTREGION

  UNSPECV_SFPNOP
  UNSPECV_SFPBANKDONE
  UNSPECV_SFPASSIGN

  UNSPECV_SFPLOADI
  UNSPECV_SFPLOAD
  UNSPECV_SFPLOADMACRO
  UNSPECV_SFPLOADDISCARD
  UNSPECV_SFPLOADSRCS
  UNSPECV_SFPSTORE
  UNSPECV_SFPSTORESRCS

  UNSPECV_SFPSETCC
  UNSPECV_SFPENCC
  UNSPECV_SFPCOMPC
  UNSPECV_SFPPUSHC
  UNSPECV_SFPPOPC

  UNSPECV_SFPMUL
  UNSPECV_SFPMULI
  UNSPECV_SFPADD
  UNSPECV_SFPADDI
  UNSPECV_SFPMAD
  UNSPECV_SFPIADD

  UNSPECV_SFPMOV
  UNSPECV_SFPEXEXP
  UNSPECV_SFPEXMAN
  UNSPECV_SFPABS
  UNSPECV_SFPLZ
  UNSPECV_SFPSETEXP
  UNSPECV_SFPSETMAN
  UNSPECV_SFPSETSGN
  UNSPECV_SFPAND
  UNSPECV_SFPOR
  UNSPECV_SFPXOR
  UNSPECV_SFPNOT

  UNSPECV_SFPSHFT
  UNSPECV_SFPCAST

  UNSPECV_SFPDIVP2
  UNSPECV_SFPSTOCHRND

  UNSPECV_SFPCONFIG
  UNSPECV_OWNED_SETC16

  UNSPECV_SFPLUT
  UNSPECV_SFPLUTFP32

  UNSPECV_SFPSWAP
  UNSPECV_SFPTRANSP
  UNSPECV_SFPTRANSP_GATHER
  UNSPECV_SFPSHFT2_COPY4
  UNSPECV_SFPSHFT2_SUBVEC_COPY4
  UNSPECV_SFPSHFT2_SUBVEC_SHFL1_COPY4
  UNSPECV_SFPSHFT2_SUBVEC_SHFL1

  UNSPECV_SFPGT
  UNSPECV_SFPLE

  UNSPECV_SFPMUL24
  UNSPECV_SFPARECIP
  UNSPECV_SFPNONLINEAR

  UNSPECV_TTSETRWC
  UNSPECV_TTDSTFACE
  UNSPECV_TTINCRWC
  UNSPECV_TTREPLAY
  UNSPECV_TTINSN
  UNSPECV_TTSETC16
  UNSPECV_TTMOP
  UNSPECV_TTMOPCFG

  UNSPECV_TTMOVD2B
  UNSPECV_TTMOVB2A
  UNSPECV_TTMOVB2D
  UNSPECV_TTMOVA2D
  UNSPECV_TTTRNSPSRCB
  UNSPECV_TTSTALLWAIT
  UNSPECV_TTRMWCIB
])

(define_enum "xtt_delay" [
  none
  static
  dynamic
])
(define_enum_attr "xtt_delay" "xtt_delay"
  (const_string "none"))
;; Bubble cost hook.  It depends only on the delay contract, not on issue
;; class.  The scheduler decides whether a STATIC/DYNAMIC delay applies; this
;; attribute only supplies the (currently fixed) count.  Values above one
;; would need a walker over the emitted Tensix distance.
(define_attr "xtt_delay_bubbles" ""
  (if_then_else (eq_attr "xtt_delay" "static,dynamic")
                (const_int 1)
                (const_int 0)))

;; Instructions marked safe here have no hidden CC, Dst, RWC, template, or
;; replay ownership effects.  The post-RA latency scheduler still checks all
;; physical-register dependencies before moving one.  New patterns default to
;; barrier until their effects are described.
(define_enum "xtt_latency_reorder" [
  barrier
  safe
])
(define_enum_attr "xtt_latency_reorder" "xtt_latency_reorder"
  (const_string "barrier"))
;; BH & QSR eratta covers problems with detecting dependent insns in dynamic delay slots
;; this is a bit mask BH:bit0, QSR:bit1
(define_enum "xtt_dynamic_bug" [
  none
  (bh 1)
  (qsr 2)
])
(define_enum_attr "xtt_dynamic_bug" "xtt_dynamic_bug" (const_string "none"))

;; Resources used by the dump-only SFPLOADMACRO descriptor.  This does not
;; make an instruction macro-eligible: the verifier must still check
;; dependencies, per-target timing, write-port use, and event semantics.
(define_enum "xtt_macro_resource" [
  none
  load
  simple_mad_write
  store
])
(define_enum_attr "xtt_macro_resource" "xtt_macro_resource"
  (const_string "none"))

;; rvtt_synth_opcode is used to synthesize sfp/tt instructions that
;; are injected into the instruction stream.  rvtt_synth_opcode is
;; tied to 1 or more insns (unrolling can do that). The ID does that
;; (SSA DEP-USE chains are insufficient as we need to prevent CSE
;; merging unrelated synth_opcode builtin calls). The first src
;; operand is later replaced with the constant parts of the
;; instruction encoding (once register allocation has
;; happened). Because different insns might have different register
;; uses (but mostly don't) we need to check that the registers are
;; still consistent and fixup if not at code emission time.
(define_insn "rvtt_synth_opcode"
  [(set (match_operand:SI 0 "register_operand" "=r")
        (unspec:SI [
	  (match_operand:SI 1 "const_int_operand" "n")
          (match_operand:SI 2 "const_int_operand" "n")
	  ] UNSPEC_SYNTH_OPCODE))]
  "TARGET_XTT_TENSIX"
{
  static char pattern[32];
  unsigned pos = 0;

  pos += snprintf (&pattern[pos], sizeof (pattern) - pos,
		   "li\t%%0, %%2\t# %d:%x", unsigned (UINTVAL (operands[1])),
		   unsigned (UINTVAL (operands[2])));
  gcc_assert (pos < sizeof (pattern));

  return pattern;
}
  [(set_attr "type" "const")])

(define_expand "rvtt_lreg_pressure"
  [(unspec:SI [
     (match_operand:SI 0 "const_int_operand")]
     0)]
  "TARGET_XTT_TENSIX"
  {
    // We have to emit something.  DCE will throw this away
    emit_move_insn (gen_reg_rtx (SImode), operands[0]);
    DONE;
  })

(define_expand "rvtt_sfpreadlreg"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI 1 "const_int_operand")
	  ] UNSPECV_SFPVARLREG))]
  "TARGET_XTT_TENSIX"
  {
    unsigned regno = INTVAL (operands[1]);
    if (regno >= SFPU_CREG_IDX_LWM)
      {
        rtx src_op = rvtt_gen_rtx_creg (GET_MODE (operands[0]), regno);
        emit_insn (gen_rtx_SET (operands[0], src_op));
        DONE;
      }
  })

(define_expand "rvtt_sfpwritelreg"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "register_operand")
     (match_operand:SI      1 "const_int_operand")
     ] UNSPECV_SFPVARLREG)]
  "TARGET_XTT_TENSIX")

(define_int_iterator rvtt_lregs [0 1 2 3 4 5 6 7])
;; We have to map the number to a string.
(define_int_attr rvtt_lregs_value
  [(0 "0") (1 "1") (2 "2") (3 "3") (4 "4") (5 "5") (6 "6") (7 "7")])

(define_insn "rvtt_sfpwritelreg<rvtt_lregs_value>"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "register_operand" "x<rvtt_lregs_value>")
     (const_int rvtt_lregs)
     ] UNSPECV_SFPVARLREG)]
  "TARGET_XTT_TENSIX"
  "# WRITE %x0"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "length" "0")])

(define_insn "rvtt_sfpreadlreg<rvtt_lregs_value>"
  [(set (match_operand:XTT32SI 0 "register_operand" "=x<rvtt_lregs_value>")
        (unspec_volatile:XTT32SI [
	  (const_int rvtt_lregs)
	  ] UNSPECV_SFPVARLREG))]
  "TARGET_XTT_TENSIX"
  "# READ %x0"
;; not a xtt_dynamic_bug consumer, it is for the user to get this right.
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "length" "0")])

;; Compiler-only marker of LREG ownership, placed after the raw instruction
;; region it describes.  Operand 0 releases values after their last raw use;
;; operand 1 starts newly written values.  It must remain volatile until the
;; pre-IRA lreg-livein pass has made the hard LREG values visible to IRA.  It
;; emits no Tensix instruction.
(define_insn "rvtt_sfprawlreg_access"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand" "n")
     (match_operand:SI 1 "const_int_operand" "n")
     ] UNSPECV_SFPRAWLREG_ACCESS)]
  "TARGET_XTT_TENSIX"
  "# RAWLREG %0, %1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "length" "0")])

;; Markers declaring the effects of a raw instruction region, read by the
;; GIMPLE analyses of the programmable constant registers.  They emit no
;; Tensix instruction.
(define_insn "rvtt_ttregion_begin"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand" "n")
     (match_operand:SI 1 "const_int_operand" "n")
     ] UNSPECV_TTREGION)]
  "TARGET_XTT_TENSIX"
  "# TTREGION %0, %1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "length" "0")])

(define_insn "rvtt_ttregion_end"
  [(unspec_volatile:XTT32SI [
     (const_int 0)
     ] UNSPECV_TTREGION)]
  "TARGET_XTT_TENSIX"
  "# TTREGION END"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "length" "0")])

(define_insn "rvtt_sfpnovalue"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec:XTT32SI [
	  (const_int 0)
	  ] UNSPEC_SFPNOVAL))]
  "TARGET_XTT_TENSIX"
  "# NOVALUE %x0"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "length" "0")])

;; We need the predicate markers until here.  Turn them into something DCE will remove.
(define_expand "rvtt_sfpxpred"
  [(set:SI (match_operand:SI 0 "register_operand")
     (unspec:SI [
        (match_operand:SI 1 "const_int_operand")
        (match_operand:SI 2 "reg_or_const_int_operand")]
	0))]
  "TARGET_XTT_TENSIX"
  {
    emit_move_insn (operands[0], operands[2]);
    DONE;
  })

(define_expand "rvtt_sfpxcond"
  [(set:SI (match_operand:SI 0 "register_operand")
     (unspec:SI [
        (match_operand:SI 1 "const_int_operand")
        (match_operand:SI 2 "reg_or_const_int_operand")
        (match_operand:SI 3 "reg_or_const_int_operand")]
	0))]
  "TARGET_XTT_TENSIX"
  {
    emit_move_insn (operands[0], operands[2]);
    DONE;
  })

;; These builtins are converted by gimple passes, but the insns are still
;; needed due to the way we expand them.

(define_expand "rvtt_sfpxlogic"
  [(set (match_operand:SI 0 "register_operand")
        (unspec:SI [
          (match_operand:SI 1 "const_int_operand")
          (match_operand:SI 2 "reg_or_const_int_operand")
          (match_operand:SI 3 "reg_or_const_int_operand")
          ] 0))]
  "TARGET_XTT_TENSIX"
  "FAIL;")

(define_expand "rvtt_sfpxcondiX"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec:SI [
	  (match_operand:SI 1 "register_operand")
	  ] 0))]
  "TARGET_XTT_TENSIX"
  "FAIL;")

(define_expand "rvtt_sfpxcmp"
  [(set (match_operand:SI 0 "register_operand")
        (unspec_volatile:SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "const_int_operand")
	  ] 0))]
  "TARGET_XTT_TENSIX"
{
  FAIL;
})

(define_expand "rvtt_sfpxloadi"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec:XTT32SI [
	  (match_operand:SI 1 "address_operand")
          (match_operand:SI 2 "reg_or_const_int_operand")
          (match_operand:SI 3 "reg_or_0_operand")
          (match_operand:SI 4 "const_int_operand")
          (match_operand:SI 5 "const_int_operand")
	  ] 0))]
  "TARGET_XTT_TENSIX"
{
  FAIL;
})

(define_insn "rvtt_sfpconcat2"
  [(set (match_operand:XTT64SI 0 "register_operand" "=xr")
     (unspec:XTT64SI [
       (match_operand:XTT32SI 1 "register_operand" "xr")
       (match_operand:XTT32SI 2 "register_operand" "xr")
       ] UNSPEC_SFPCLEAVE))]
  "TARGET_XTT_TENSIX"
  "SFPBAD.CONCAT %0, %1, %2"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")])

(define_insn "rvtt_sfpselect2"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
     (unspec:XTT32SI [
       (match_operand:XTT64SI 1 "register_operand" "xr")
       (match_operand:SI 2 "const_int_operand" "n")
       ] UNSPEC_SFPCLEAVE))]
  "TARGET_XTT_TENSIX"
  "SFPBAD.SELECT %0, %1, %2"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")])

(define_insn "rvtt_sfpconcat4"
  [(set (match_operand:XTT128SI 0 "register_operand" "=xr")
     (unspec:XTT128SI [
       (match_operand:XTT32SI 1 "register_operand" "xr")
       (match_operand:XTT32SI 2 "register_operand" "xr")
       (match_operand:XTT32SI 3 "register_operand" "xr")
       (match_operand:XTT32SI 4 "register_operand" "xr")
       ] UNSPEC_SFPCLEAVE))]
  "TARGET_XTT_TENSIX"
  "SFPBAD.CONCAT %0, %1, %2, %3, %4"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")])

(define_insn "rvtt_sfpselect4"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
     (unspec:XTT32SI [
       (match_operand:XTT128SI 1 "register_operand" "xr")
       (match_operand:SI 2 "const_int_operand" "n")
       ] UNSPEC_SFPCLEAVE))]
  "TARGET_XTT_TENSIX"
  "SFPBAD.SELECT %0, %1, %2"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")])

(define_insn "rvtt_sfpconcat8"
  [(set (match_operand:XTT256SI 0 "register_operand" "=xr")
     (unspec:XTT256SI [
       (match_operand:XTT32SI 1 "register_operand" "xr")
       (match_operand:XTT32SI 2 "register_operand" "xr")
       (match_operand:XTT32SI 3 "register_operand" "xr")
       (match_operand:XTT32SI 4 "register_operand" "xr")
       (match_operand:XTT32SI 5 "register_operand" "xr")
       (match_operand:XTT32SI 6 "register_operand" "xr")
       (match_operand:XTT32SI 7 "register_operand" "xr")
       (match_operand:XTT32SI 8 "register_operand" "xr")
       ] UNSPEC_SFPCLEAVE))]
  "TARGET_XTT_TENSIX"
  "SFPBAD.CONCAT %0, %1, %2, %3, %4, %5, %6, %7, %8"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")])

(define_insn "rvtt_sfpselect8"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
     (unspec:XTT32SI [
       (match_operand:XTT256SI 1 "register_operand" "xr")
       (match_operand:SI 2 "const_int_operand" "n")
       ] UNSPEC_SFPCLEAVE))]
  "TARGET_XTT_TENSIX"
  "SFPBAD.SELECT %0, %1, %2"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")])

(define_insn "rvtt_sfpnop"
  [(unspec_volatile:XTT32SI [
     (const_int 0)
     ] UNSPECV_SFPNOP)]
  "TARGET_XTT_TENSIX"
  "SFPNOP"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_result_latency" "1")
   (set_attr "xtt_lreg_read_ops" "1")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "none")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")])

(define_insn "rvtt_sfpbankdone"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI   0 "const_int_operand" "n")
     (match_operand:SI   1 "const_int_operand" "n")
     (match_operand:SI   2 "const_int_operand" "n")
     ] UNSPECV_SFPBANKDONE)]
  "TARGET_XTT_TENSIX_QSR"
  "SFPBANKDONE\t%0, %1, %2"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")])

(define_expand "movxtt32si"
  [(set (match_operand:XTT32SI 0 "")
	(match_operand:XTT32SI 1 ""))]
  "TARGET_XTT_TENSIX"
{
  /* A zero vector is a constant LREG read, not a materialised constant:
     the SFPU has no constant-vector move, and the read costs no
     register.  GCC asks for one when it invents a value for an
     undefined vector -- an uninitialised vFloat reaching a diamond
     join, say.  */
  if (GET_CODE (operands[1]) == CONST_VECTOR
      && operands[1] == CONST0_RTX (XTT32SImode)
      && register_operand (operands[0], XTT32SImode))
    {
      emit_insn (gen_rtx_SET (operands[0],
			      rvtt_gen_rtx_creg (XTT32SImode, CREG_IDX_0)));
      DONE;
    }
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
    static const char *const assembly[] = {
      "SFPMOV\t%0, %x1, 2",
      "SFPBAD.LOAD\t%x0, %1",
      "SFPBAD.STORE\t%x1, %0",
    };
    if (which_alternative)
      rvtt_mov_error (insn);
    return assembly[which_alternative];
  }
  ;; The remaining alternative is the all-lanes SFPMOV mod 2 copy: it reads
  ;; operand 1, writes every lane of operand 0, and has no CC, configuration
  ;; or counter effect.  Simple sub-unit, result latency 0.  The
  ;; BADLOAD/BADSTORE alternatives are error paths.
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
  ;; The remaining alternative is the lane-predicated SFPMOV merge (result
  ;; tied to the live value, enabled lanes take the source): Simple sub-unit,
  ;; reads CC and operands 1 and 2, writes operand 0, no configuration or
  ;; counter effect.  The macro planner turns this into the
  ;; predicated-overwrite select calendar.
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_write_port" "shared_simple_round")
   (set_attr "xtt_lreg_read_ops" "7")
   (set_attr "xtt_lreg_write_ops" "2")
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   ;; Simple sub-unit; a dependent Simple instruction can issue in the next
   ;; slot, so result latency 0.
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   || TARGET_XTT_TENSIX_WH")
		      (const_int 1) (const_int 0)))])

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
  ;; SFPLOADI writes the enabled lanes of the destination for mod0 0-8 and 10
  ;; (8 and 10 merge a half word into the tied live value) and has no CC,
  ;; configuration or counter effect.  SFPLOADI.md has no next-cycle
  ;; constraint, so result latency 0.  Mod0 9 and above 10 are undefined and
  ;; keep the default attributes.  No sub-unit is claimed.
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
   ;; Typed-effect attributes: lane-predicated LREG immediate
   ;; materialization, never touches CC; lane-gated consumer.
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
      int op = TT_OP_QSR_SFPLOAD (0, INTVAL (operands[6]), INTVAL (operands[7]), INTVAL (operands[8]), 1 << 10);
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
   ;; SFPLOAD.md's three-instruction rule concerns the Dst race between units,
   ;; not an SFPU result delay; the LREG result can be used in the next slot,
   ;; so result latency 0.
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
   ;; Replay: a launch is just an instruction word.  Replaying it reads the
   ;; descriptor configuration current at that point, exactly as executing the
   ;; original word there would.  The formation pass emits launches only after
   ;; programming and owning the descriptor slots they reference.  Launches
   ;; are replay members only with -mtt-tensix-macro-planner-replay.
   (set (attr "xtt_replay")
	(if_then_else (match_test "riscv_tt_macro_planner_replay")
		      (const_string "safe")
		      (const_string "barrier")))])

;; A macro launch whose delayed template writes an LREG in addition to the
;; launch VD (for example an SFPSWAP template writing its fixed comparison
;; operand).  Keep that hidden architectural write explicit in RTL: it is a
;; register operand the planner fills from the capability tables'
;; template_hidden_lreg_writes(), never a register fixed by the pattern.
;; Formation may select this pattern only after proving the hidden register
;; private to the complete periodic region.
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
   ;; planner fills from template_hidden_lreg_writes(), never a register
   ;; fixed by the pattern.
   (clobber (match_operand:XTT32SI 7 "register_operand" "=xr"))]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  ".ttinsn\t%6"
  [(set_attr "type" "tensix")
   (set_attr "xtt_macro_resource" "load")
   ;; Replay: as for rvtt_sfploadmacro_int above.  The hidden template write
   ;; is repeated at every replay just as the original word would do it, and
   ;; is modeled by the clobber.
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
   ;; SFPSTORE writes only Dst and has no LREG result.  SFPSTORE.md has no
   ;; next-cycle rule, and SFPSTORE is not among the consumers of a MAD result
   ;; that the Blackhole SFPMAD.md errata say the automatic stalling misses,
   ;; so a dependent store is covered by the scoreboard on BH and by the nop
   ;; inserter on WH (xtt_delay is unaffected).  Result latency 0, BH/WH only.
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
        = TT_OP_QSR_SFPSTORE (0, INTVAL (operands[5]), INTVAL (operands[6]), INTVAL (operands[7]), 1 << 10);
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

(define_expand "rvtt_sfpsetcc_wh"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "reg_or_cstlreg_operand"  "xrxc")
     (match_operand:SI   1 "const_int_operand" "n")
     ] UNSPECV_SFPSETCC)]
  "TARGET_XTT_TENSIX_WH_BH"
  {
    emit_insn (gen_rvtt_sfpsetcc
      (operands[0], operands[1], const0_rtx));
    DONE;
  })

(define_expand "rvtt_sfpsetcc"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "reg_or_cstlreg_operand"  "xrxc")
     (match_operand:SI   1 "const_int_operand" "n")
     (match_operand:SI   2 "const_int_operand" "n")
     ] UNSPECV_SFPSETCC)]
  "TARGET_XTT_TENSIX"
  {
    operands[2] = GEN_INT (INTVAL (operands[2]) << 11);
  })

;; Described so that predicated code names its CC effect at the predicate
;; write instead of becoming an opaque boundary.  This is also the
;; predicate-definition event of the select calendar.
(define_insn "*rvtt_sfpsetcc"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "reg_or_cstlreg_operand"  "xrxc")
     (match_operand:SI   1 "const_int_operand" "n")
     (match_operand:SI   2 "const_int_operand" "n")
     ] UNSPECV_SFPSETCC)]
  "TARGET_XTT_TENSIX"
  "SFPSETCC\t%x0, %2, %1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_read_ops" "2")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "readwrite")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   ;; Lane-gated consumer (typed-effect attributes).
   (set_attr "xtt_lane_gated" "yes")])

(define_insn "rvtt_sfpencc"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "const_int_operand" "n")
     (match_operand:SI    1 "const_int_operand" "n")
     ] UNSPECV_SFPENCC)]
  "TARGET_XTT_TENSIX"
  "SFPENCC\t%1, %0"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_read_ops" "1")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "write")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")])

  ;; SFPCOMPC.md: reads and updates only the lane flags and flag stack; no
  ;; LREG, configuration or counter effect.  There is no register result, so
  ;; latency entry 1 (0 slots): the flag update is visible to the next
  ;; instruction.
(define_insn "rvtt_sfpcompc"
  [(unspec_volatile:XTT32SI [
     (const_int 0)
     ] UNSPECV_SFPCOMPC)]
  "TARGET_XTT_TENSIX"
  "SFPCOMPC"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_read_ops" "1")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "readwrite")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   (set_attr "xtt_result_latency" "1")])

  ;; SFPPUSHC.md: reads and updates only the lane flags and flag stack; no
  ;; LREG, configuration or counter effect.  There is no register result, so
  ;; latency entry 1 (0 slots): the flag update is visible to the next
  ;; instruction.
(define_insn "rvtt_sfppushc"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "const_int_operand" "n")
     ] UNSPECV_SFPPUSHC)]
  "TARGET_XTT_TENSIX"
  "SFPPUSHC\t%0"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_read_ops" "1")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "readwrite")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   (set_attr "xtt_result_latency" "1")])

  ;; SFPPOPC.md: reads and updates only the lane flags and flag stack; no
  ;; LREG, configuration or counter effect.  There is no register result, so
  ;; latency entry 1 (0 slots): the flag update is visible to the next
  ;; instruction.
(define_insn "rvtt_sfppopc"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "const_int_operand" "n")
     ] UNSPECV_SFPPOPC)]
  "TARGET_XTT_TENSIX"
  "SFPPOPC\t%0"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_read_ops" "1")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "readwrite")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   (set_attr "xtt_result_latency" "1")])

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

    rvtt_merge_lv_src (&operands[1], &operands[2],
        INTVAL (operands[4]) & SFPIADD_MOD1_ARG_2SCOMP_LREG_DST
	? nullptr : &operands[3]);
  }
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; SFPIADD.md: "Backend execution unit: Vector Unit (SFPU), simple
   ;; sub-unit".  The write port is inferred as for SFPSHFT/SFPCAST.
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_write_port" "shared_simple_round")
   ;; Effects depend on mod1 (operand 4) and are described for WH and BH per
   ;; SFPIADD.md; Quasar keeps the default attributes.  For this
   ;; register-argument pattern they hold for mod1 <= 10 with ARG_IMM clear:
   ;; reads VC (operand 3) and VB=VD (operand 2), writes VD (operand 0),
   ;; lane-predicated.  LaneFlags are written unless MOD1_CC_NONE is set
   ;; without MOD1_CC_GTE0 ((mod1 & 12) == 4).  An ARG_IMM mod would not read
   ;; VB and is left opaque.
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
   ;; Simple sub-unit, and SFPIADD.md has no next-cycle constraint: result
   ;; latency 0 (for the mods described above).
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && IN_RANGE (INTVAL (operands[4]), 0, 10)
				   && (INTVAL (operands[4]) & 1) == 0")
		      (const_int 1) (const_int 0)))
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_BH | XTT_DYNAMIC_BUG_QSR)"))
   ;; Typed-effect attributes: SFPIADD's mod field can architecturally set
   ;; CC -- conservative CC write, any mod.
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
   ;; Typed-effect attributes: conservative CC write, any mod; lane-gated
   ;; consumer.
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
      enc = GEN_INT (rvtt_synth (UINTVAL (operands[6])).src_shift (8).dst_shift (4));
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
        // Bake L8 as the dst reg here
        auto enc = rvtt_synth (INTVAL (operands[3]));
        opcode = GEN_INT (INTVAL (opcode) | (0x8 << enc.dst_shift ()));
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
   ;; Effects depend on mod1 (operand 7) and are described for WH and BH per
   ;; SFPIADD.md; Quasar keeps the default attributes.  For this
   ;; immediate-argument pattern they hold for ARG_IMM set, 2SCOMP clear and
   ;; mod1 <= 10 (mods 1, 5 and 9), and only in the single-word
   ;; constant-immediate alternatives (a register immediate needs an
   ;; instruction built at run time, whose memory effects are not described):
   ;; VD (operand 0) = LReg[VC] (operand 5) + SignExtend (Imm12),
   ;; lane-predicated.  VB is not read.  Disabled lanes keep the live-in value
   ;; (operand 6, tied to the destination in the LV alternatives), so it is
   ;; read too.  LaneFlags are written as for the register form above.
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
   ;; Simple sub-unit, and SFPIADD.md has no next-cycle constraint: result
   ;; latency 0 (for the mods described above).
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[4])
				   && IN_RANGE (INTVAL (operands[7]), 0, 10)
				   && (INTVAL (operands[7]) & 3) == 1")
		      (const_int 1) (const_int 0)))
   ;; Typed-effect attributes: conservative CC write, any mod; lane-gated
   ;; consumer.
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
      "SFPIADD\tL8, %x4, %3, %5",
      operands, false, 6);
  }
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; Typed-effect attributes: conservative CC write, any mod; lane-gated
   ;; consumer.
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
  ;; Effects depend on mod1 (operand 3).  SFPMOV, SFPEXEXP, SFPEXMAN, SFPABS
  ;; and SFPLZ read operand 2 and write the enabled lanes of operand 0
  ;; (reading the tied live value for disabled lanes), with no configuration
  ;; or counter effect.  SFPMOV mod 2 copies all lanes regardless of CC; the
  ;; other mods are lane-predicated; SFPEXEXP mod 2/10 and SFPLZ mod 2 also
  ;; set the lane flag from the result.  Other mods (such as SFPMOV mod 8,
  ;; which advances the PRNG) keep the default attributes.  All five are
  ;; Simple sub-unit instructions with result latency 0.
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
   ;; Effect attributes are per insn code, independent of the mod: pure value
   ;; unaries.  SFPEXEXP and SFPLZ can set CC depending on the mod, so they
   ;; are treated as writing CC; the rest never touch CC.
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
  "SFP<rvtt_unary_insn>\tL8, %x0, %1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; Typed-effect attributes: same per-family classification as the _lv
   ;; forms above.
   (set_attr "xtt_lane_local" "yes")
   (set (attr "xtt_cc_write")
	(if_then_else (match_test "<rvtt_unary_op> == UNSPECV_SFPEXEXP
				   || <rvtt_unary_op> == UNSPECV_SFPLZ")
		      (const_string "yes") (const_string "no")))
   (set_attr "xtt_lane_gated" "yes")])

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
  ;; Effects depend on mod1 (operand 4).  The register forms of SFPSETEXP (mod
  ;; 0/2), SFPSETMAN (mod 0) and SFPSETSGN (mod 0) read the source (operand 2)
  ;; and the tied destination and write the enabled lanes of the destination,
  ;; with no CC, configuration or counter effect.  Mod 1 is the immediate form
  ;; (the _i patterns); higher mods keep the default attributes.  Simple
  ;; sub-unit, result latency 0.
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
   ;; Typed-effect attributes: field inserts never touch CC; lane-gated
   ;; consumers.
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
      enc = GEN_INT (rvtt_synth (UINTVAL (operands[6])).src_shift (8).dst_shift (4));
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
          (match_operand:SI    1 "mem_or_0_operand" "J,J,m,m")
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
  ;; The mod 1 immediate forms of SFPSETEXP, SFPSETMAN and SFPSETSGN read the
  ;; source and the tied destination and write the enabled lanes of the
  ;; destination, with no CC, configuration or counter effect; the SFPSET*.md
  ;; pages give no next-cycle rule.  Simple sub-unit, result latency 0.  Other
  ;; mods keep the default attributes.
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
   ;; Typed-effect attributes: field inserts never touch CC; lane-gated
   ;; consumers.
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
    rvtt_merge_lv_src (&operands[1], &operands[2], &operands[3]);
  }
  ;; SFPAND, SFPOR and SFPXOR read the tied destination and operand 3 and
  ;; write the enabled lanes of the destination, with no CC, configuration or
  ;; counter effect.  Simple sub-unit, result latency 0.
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
  ;; (BH) For mod1 <= 1, SFPAND and SFPOR read operands 2 and 3 (and the tied
  ;; live value) and write the enabled lanes of the destination, with no CC,
  ;; configuration or counter effect.  Higher mods keep the default
  ;; attributes.  Simple sub-unit, result latency 0.
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
  ;; Effects depend on mod1 (operand 4).  The variable-shift forms of SFPSHFT
  ;; (mod 0 logical; mod 2 arithmetic right, BH only) read the shift amount
  ;; (operand 3) and the tied destination and write the enabled lanes of the
  ;; destination, with no CC, configuration or counter effect.  Simple
  ;; sub-unit, and SFPSHFT.md has no next-cycle constraint: result latency 0.
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
   ;; Lane-gated consumer (typed-effect attributes).  The immediate-shift
   ;; patterns are not annotated; only this register-shift pattern is.
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
   ;; Simple-unit event by analogy with the documented cast-round Simple
   ;; assignments.
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_write_port" "shared_simple_round")
   (set_attr "xtt_lreg_read_ops" "97")
   (set_attr "xtt_lreg_write_ops" "2")
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   ;; Simple sub-unit; a dependent Simple instruction can issue in the next
   ;; slot, so result latency 0.
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
   ;; Same Simple-unit inference as the BH/QSR form above.
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_write_port" "shared_simple_round")
   (set_attr "xtt_lreg_read_ops" "97")
   (set_attr "xtt_lreg_write_ops" "2")
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   ;; Simple sub-unit; a dependent Simple instruction can issue in the next
   ;; slot, so result latency 0.
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   || TARGET_XTT_TENSIX_WH")
		      (const_int 1) (const_int 0)))])

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
   ;; Subunit: SFPCAST.md documents "Backend execution unit: Vector Unit
   ;; (SFPU), simple sub-unit".
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_write_port" "shared_simple_round")
   ;; The effects (reads operand 2, writes operand 0, reads CC, no CC write,
   ;; configuration or RWC effect) depend on mod1 (operand 3) and are claimed
   ;; only for:
   ;;   mod1 0 (SM32 to FP32, round to nearest even), per SFPCAST.md;
   ;;   mod1 3, BH only (sign-preserving conditional negate, the
   ;;     SM32<->INT32 conversion), per SFPCAST_IntInt.md.
   ;; Mod1 1 (stochastic rounding) also advances the PRNG, which the effect
   ;; attributes cannot express, and BH mod1 2 is the documented cast-as-ABS
   ;; hardware bug; both keep the default attributes, as do higher mods.
   ;; Quasar claims only mod 0.
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
   ;; Simple sub-unit: result latency 0 (only for the mods described above;
   ;; other mods stay opaque through the attributes above).
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "INTVAL (operands[3]) == 0
				   || (TARGET_XTT_TENSIX_BH
				       && INTVAL (operands[3]) == 3)")
		      (const_int 1) (const_int 0)))
   ;; Typed-effect attributes: pure value unary, never touches CC;
   ;; lane-gated consumer.
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
      enc = GEN_INT (rvtt_synth (UINTVAL (operands[6])).src_shift (8).dst_shift (4));
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
   ;; Effects depend on mod1 (operand 7) and are described for WH and BH per
   ;; SFPDIVP2.md; Quasar keeps the default attributes.  Both mods read
   ;; LReg[VC] (operand 5) and write VD (operand 0) lane-predicated, with no
   ;; lane flag, configuration or counter effect.  Disabled lanes keep the
   ;; live-in value (operand 6, tied to the destination in the LV
   ;; alternatives), so it is read too.  Only the single-word
   ;; constant-immediate alternatives are described (a register immediate
   ;; needs an instruction built at run time, whose memory effects are not
   ;; described).
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
   ;; Simple sub-unit, and SFPDIVP2.md has no next-cycle constraint: result
   ;; latency 0 (for the mods described above).
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[4])
				   && IN_RANGE (INTVAL (operands[7]), 0, 1)")
		      (const_int 1) (const_int 0)))
   ;; Typed-effect attributes: pure value unary, never touches CC;
   ;; lane-gated consumer.
   (set_attr "xtt_lane_local" "yes")
   (set_attr "xtt_cc_write" "no")
   (set_attr "xtt_lane_gated" "yes")])

(define_expand "rvtt_sfpstochrnd"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
          (match_operand:XTT32SI 1 "register_operand")
	  (match_operand:SI    2 "const_int_operand")
          (match_operand:SI    3 "const_int_operand")
	  ] UNSPECV_SFPSTOCHRND))]
  "TARGET_XTT_TENSIX"
{
  emit_insn (gen_rvtt_sfpstochrnd_lv
    (operands[0], rvtt_gen_rtx_noval (XTT32SImode),
     operands[1], operands[2], operands[3]));
  DONE;
})

(define_insn "rvtt_sfpstochrnd_lv"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn,0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc,xrxc")
          (match_operand:SI    3 "const_int_operand" "n,n")
          (match_operand:SI    4 "const_int_operand" "n,n")
	  ] UNSPECV_SFPSTOCHRND))]
  "TARGET_XTT_TENSIX"
  "@
   SFPSTOCHRND\t%x0, L9, %x2, 0, %3, %4
   SFPSTOCHRND\t%x0, L9, %x2, 0, %3, %4\t# LV:%x1"
  [(set_attr "type" "tensix")
   ;; L9 is architecturally read-only zero; the typed LREG mask tracks
   ;; the allocatable L0-L7 file.  PRNG state is modeled separately.
   (set_attr "xtt_prng_effect" "advance")
   (set_attr "xtt_subunit" "round")
   (set_attr "xtt_lreg_write_port" "shared_simple_round")
   (set_attr "xtt_lreg_read_ops" "7")
   (set_attr "xtt_lreg_write_ops" "2")
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   (set_attr "xtt_lane_gated" "yes")])

(define_expand "rvtt_sfpstochrnd_descale_v"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
          (match_operand:XTT32SI 1 "register_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
	  (match_operand:SI    3 "const_int_operand")
          (match_operand:SI    4 "const_int_operand")
	  ] UNSPECV_SFPSTOCHRND))]
  "TARGET_XTT_TENSIX"
{
  emit_insn (gen_rvtt_sfpstochrnd_descale_v_lv
    (operands[0], rvtt_gen_rtx_noval (XTT32SImode),
     operands[1], operands[2], operands[3], operands[4]));
  DONE;
})

(define_insn "rvtt_sfpstochrnd_descale_v_lv"
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
   ;; Every rounding mode advances per-lane PRNG state.  Leave replay and
   ;; effect attributes at their defaults until that state is modeled as an
   ;; ordering dependency in all scheduling passes.
   (set_attr "xtt_subunit" "round")])

(define_expand "rvtt_sfpstochrnd_descale_i"
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
  emit_insn (gen_rvtt_sfpstochrnd_descale_i_lv
    (operands[0], operands[1], rvtt_gen_rtx_noval (XTT32SImode),
     operands[2], operands[3],
     operands[4], operands[5], operands[6], operands[7]));
  DONE;
})

(define_expand "rvtt_sfpstochrnd_descale_i_lv"
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
  operands[7] = GEN_INT (INTVAL (operands[7]) | SFPSTOCHRND_MOD1_IMM8);

  auto mem = const0_rtx;
  auto opc = const0_rtx;
  auto enc = const0_rtx;
  auto imm = operands[4];
  if (!CONST_INT_P (imm))
    {
      mem = gen_rtx_MEM (SImode, operands[1]);
      int op
        = TARGET_XTT_TENSIX_WH  ? TT_OP_WH_SFP_STOCH_RND (INTVAL (operands[8]),
	             0, CREG_IDX_0, 0, 0, INTVAL (operands[7]))
        : TARGET_XTT_TENSIX_BH  ? TT_OP_BH_SFP_STOCH_RND (INTVAL (operands[8]),
	             0, CREG_IDX_0, 0, 0, INTVAL (operands[7]))
        : TARGET_XTT_TENSIX_QSR ? TT_OP_QSR_SFP_STOCH_RND (INTVAL (operands[8]),
	             0, CREG_IDX_0, 0, 0, INTVAL (operands[7]))
        : (gcc_unreachable (), 0);
      opc = GEN_INT (op);
      enc = GEN_INT (rvtt_synth (UINTVAL (operands[6])).src_shift (8).dst_shift (4));
      imm = operands[5];
    }

  emit_insn (gen_rvtt_sfpstochrnd_descale_i_lv_int
    (operands[0], mem, opc, enc, imm,
     operands[3], operands[2], operands[7], operands[8]));
  DONE;
})

(define_insn "rvtt_sfpstochrnd_descale_i_lv_int"
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
      ? "SFPSTOCHRND\t%x0, L9, %x5, %4, %7, %8\t# LV:%x6"
      : "SFPSTOCHRND\t%x0, L9, %x5, %4, %7, %8",
      operands, true, 9);
  }
  [(set_attr "type" "tensix")
   ;; Like the plain and vector-descale forms, this instruction advances
   ;; per-lane PRNG state even for deterministic rounding.  Keep it opaque
   ;; until the scheduler and replay passes model PRNG ordering.
   (set_attr "xtt_subunit" "round")])

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
     (match_operand:XTT32SI 0 "register_operand" "x0")
     (match_operand:SI   1 "const_int_operand" "n")
     (match_operand:SI   2 "const_int_operand" "n")
     ] UNSPECV_SFPCONFIG)]
  "TARGET_XTT_TENSIX"
  "SFPCONFIG\t%2, 0, %1\t# R:%x0 CFG:%2"
  [(set_attr "xtt_subunit" "cfg")
   ;; Operand 0 supplies the value from L0.  Numeric LREG attributes
   ;; encode the operand-position mask plus one, hence bit 0 + 1 == 2.
   (set_attr "xtt_lreg_read_ops" "2")
   (set_attr "xtt_lreg_write_ops" "1")
   ;; SFPCONFIG is lane-gated and writes the destination named by operand 2.
   ;; Keep it a replay barrier: the written LaneConfig bits can change the
   ;; meaning of neighbouring instructions.  This is the value-form
   ;; counterpart of rvtt_sfpconfig_i below.
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "dest")
   (set_attr "xtt_config_dest_op" "3")
   (set_attr "xtt_rwc_effect" "none")
   (set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_BH | XTT_DYNAMIC_BUG_QSR)"))])

(define_expand "rvtt_sfpwriteconfig_i"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "mem_or_0_operand")
     (match_operand:SI 1 "reg_or_const_int_operand")
     (match_operand:SI 2 "reg_or_0_operand")
     (match_operand:SI 3 "const_int_operand")
     (match_operand:SI 4 "const_int_operand")
     (match_operand:SI 5 "const_int_operand")
     ] UNSPECV_SFPCONFIG)]
  "TARGET_XTT_TENSIX"
{
  operands[4] = GEN_INT (INTVAL (operands[4]) | SFPCONFIG_MOD1_ARG_IMM);
  auto mem = const0_rtx;
  auto opc = const0_rtx;
  auto enc = const0_rtx;
  auto imm = operands[1];
  if (!CONST_INT_P (imm))
    {
      mem = gen_rtx_MEM (SImode, operands[0]);
      int op
        = TARGET_XTT_TENSIX_WH 	? TT_OP_WH_SFPCONFIG (0, INTVAL (operands[5]), INTVAL (operands[4]))
	: TARGET_XTT_TENSIX_BH 	? TT_OP_BH_SFPCONFIG (0, INTVAL (operands[5]), INTVAL (operands[4]))
	: TARGET_XTT_TENSIX_QSR	? TT_OP_QSR_SFPCONFIG (0, INTVAL (operands[5]), INTVAL (operands[4]))
        : (gcc_unreachable (), 0);
      opc = GEN_INT (op);
      enc = GEN_INT (rvtt_synth (UINTVAL (operands[3])));
      imm = operands[2];
    }

  emit_insn (gen_rvtt_sfpwriteconfig_i_int
    (mem, opc, enc, imm,
    rvtt_gen_rtx_noval (XTT32SImode),
    operands[4], operands[5]));
  DONE;
})

(define_insn "rvtt_sfpwriteconfig_i_int"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "mem_or_0_operand" "J,m")
     (match_operand:SI 1 "const_int_operand" "J,n") ;; opcode
     (match_operand:SI 2 "const_int_operand" "J,n") ;; id
     (match_operand:SI 3 "reg_or_const_int_operand" "n,r") ;; imm or insn
     (match_operand:XTT32SI 4 "noval_operand" "xn,xn") ;; src (none)
     (match_operand:SI 5 "const_int_operand" "n,n")
     (match_operand:SI 6 "const_int_operand" "n,n")
     ] UNSPECV_SFPCONFIG)]
  "TARGET_XTT_TENSIX"
  {
    return rvtt_synth::pattern (which_alternative,
      "SFPCONFIG\t%6, %3, %5\t# CFG:%6", operands, false, -1);
  }
  [(set_attr "xtt_subunit" "cfg")
   ;; The LREG position masks stay 0 for this operand list ("SFPCONFIG %6, %3,
   ;; %5 # CFG:%6", with a noval source operand), so rvtt_insn_effects treats
   ;; the insn as opaque.  Check the effects against this operand list before
   ;; giving real masks.
   (set_attr "xtt_lreg_read_ops" "0")
   (set_attr "xtt_lreg_write_ops" "0")
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "dest")
   ;; +1 biased operand position: the config dest is %6, hence 7 (compare
   ;; rvtt_sfpmov_cfg, whose "CFG:%2" carries "3").
   (set_attr "xtt_config_dest_op" "7")
   (set_attr "xtt_rwc_effect" "none")
   (set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_BH | XTT_DYNAMIC_BUG_QSR)"))])

;; Immediate-form SFPCONFIG.  Operands: 0 = Imm16, 1 = destination (VD field),
;; 2 = Mod1.  Assembler operand order is (VD, Imm16, Mod1) (binutils
;; riscv-opc-sfpu-insns.h "J4mf9ff,J8u16,J0md7ff"), which encodes the same
;; words as TTI_SFPCONFIG (Imm16, VD, Mod1), e.g. TTI_SFPCONFIG(0x4,0xF,1) ==
;; 0x910004f1.
;;
;; Effects (SFPCONFIG.md, identical for Wormhole and Blackhole):
;;  - gimple-rvtt-check.cc restricts Mod1 to {1,3,5,7} (MOD1_IMM16_IS_VALUE
;;    set, MOD1_IMM16_IS_LANE_MASK clear) and the destination to LaneConfig
;;    (15).  In that case SFPCONFIG reads only Imm16 and neither reads nor
;;    writes any LReg, so lreg_read_ops/write_ops are empty masks (bias 1)
;;    rather than the opaque 0.
;;  - It reads CC: the write is gated per column by
;;    UseLaneFlagsForLaneEnable/LaneFlags, as for the value form.
;;  - The config destination is operand 1 (+1-biased attribute = 2), as in
;;    the value form.
;;  - As for the value form, it is a replay barrier and has the BH/QSR
;;    dynamic delay: LaneConfig bits (DISABLE_BACKDOOR_LOAD,
;;    ENABLE_DEST_INDEX, EXCHANGE_SRCB_SRCC, ROW_MASK) change the semantics
;;    of neighbouring instructions, and SFPCONFIG.md allows the next
;;    instruction to observe either value of DISABLE_BACKDOOR_LOAD.
;;  - There is no LReg result; xtt_result_latency keeps its default.
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

(define_insn "rvtt_sfplut"
  [(set (match_operand:XTT32SI 0 "register_operand" "=x3")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "x0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "x1")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "x2")
          (match_operand:XTT32SI 4 "reg_or_cstlreg_operand"  "0")
          (match_operand:SI    5 "const_int_operand" "n")
	  ] UNSPECV_SFPLUT))]
  "TARGET_XTT_TENSIX_WH_BH"
  "SFPLUT\t%x0, %5\t# R:%x1,%x2,%x3,%x4"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_delay" "dynamic")
   ;; SFPLUT.md (identical for BH and WH): reads LReg[0..2] (the coefficient
   ;; table) and LReg[3] (the input, tied to the destination) and writes the
   ;; enabled lanes of the destination, with no CC write and no configuration,
   ;; RWC or Dst access.  MAD sub-unit, scheduled "as per SFPMAD": result
   ;; latency 1.  Only mod0 0 and SGN_RETAIN (4) on BH/WH are described;
   ;; INDIRECT_VD (mod0 & 8) redirects the write through LReg[7], which the
   ;; position masks cannot express, so it keeps the default attributes.
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

(define_insn "rvtt_sfplut_qsr"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
          (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "x3")
          (match_operand:SI    2 "const_int_operand" "n")
	  ] UNSPECV_SFPLUT))]
  "TARGET_XTT_TENSIX_QSR"
  "SFPLUT\t%x0, %2\t# R:%x1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay" "dynamic")])

(define_insn "rvtt_sfplutfp32"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
          (match_operand:XTT32SI 1 "register_operand"  "x3")
          (match_operand:SI    2 "const_int_operand" "n")
	  ] UNSPECV_SFPLUTFP32))]
  "TARGET_XTT_TENSIX_QSR"
  "SFPLUTFP32\t%x0, %2\t# R:%x1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay" "dynamic")])

(define_insn_and_split "rvtt_sfplutfp32_3r"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand"  "x0")
          (match_operand:XTT32SI 2 "register_operand"  "x1")
          (match_operand:XTT32SI 3 "register_operand"  "x2")
          (match_operand:XTT32SI 4 "register_operand"  "x3")
          (match_operand:SI    5 "const_int_operand" "n")
	  ] UNSPECV_SFPLUTFP32))
        (clobber (match_scratch:XTT32SI 6 "=x7"))]
  "TARGET_XTT_TENSIX_WH_BH"
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
	  ] UNSPECV_SFPLUTFP32))]
  "TARGET_XTT_TENSIX_WH_BH && reload_completed"
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
	  ] UNSPECV_SFPLUTFP32))]
  "TARGET_XTT_TENSIX_WH_BH"
  "SFPLUTFP32\t%x0, %8\t# R:%x1,%x2,%x3,%x4,%x5,%x6,%x7"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_delay" "dynamic")
   ;; SFPLUTFP32.md (BH and WH): reads the table LReg[0..2] and LReg[4..6] and
   ;; the input LReg[3], writes VD only where lanes are enabled, and touches
   ;; no CC, configuration, RWC or Dst state; scheduled "as per SFPMAD":
   ;; result latency 1.  With INDIRECT_VD (mod1 & 8) the write goes through
   ;; LReg[7] instead, which the position masks cannot express, so it keeps
   ;; the default attributes.
   (set (attr "xtt_subunit")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[8])
				   && (INTVAL (operands[8]) & 8) == 0")
		      (const_string "mad") (const_string "none")))
   (set (attr "xtt_lreg_read_ops")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[8])
				   && (INTVAL (operands[8]) & 8) == 0")
		      (const_int 255) (const_int 0)))
   (set (attr "xtt_lreg_write_ops")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[8])
				   && (INTVAL (operands[8]) & 8) == 0")
		      (const_int 2) (const_int 0)))
   (set (attr "xtt_cc_effect")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[8])
				   && (INTVAL (operands[8]) & 8) == 0")
		      (const_string "read") (const_string "unknown")))
   (set (attr "xtt_config_effect")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[8])
				   && (INTVAL (operands[8]) & 8) == 0")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_rwc_effect")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[8])
				   && (INTVAL (operands[8]) & 8) == 0")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "(TARGET_XTT_TENSIX_BH
				    || TARGET_XTT_TENSIX_WH)
				   && CONST_INT_P (operands[8])
				   && (INTVAL (operands[8]) & 8) == 0")
		      (const_int 2) (const_int 0)))
   ;; Lane-gated consumer (typed-effect attributes).
   (set_attr "xtt_lane_gated" "yes")])

(define_insn "rvtt_sfpswap_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand" "0")
	  (match_operand:XTT32SI 3 "reg_or_cstlreg_operand" "1")
          (match_operand:SI    4 "const_int_operand"  "n")
          (match_operand:SI    5 "const_int_operand"  "n")
	  ] UNSPECV_SFPSWAP))
   (set (match_operand:XTT32SI 1 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_dup 2)
	  (match_dup 3)
          (match_dup 4)
          (match_dup 5)
	  ] UNSPECV_SFPSWAP))]
  "TARGET_XTT_TENSIX"
  {
    return TARGET_XTT_TENSIX_WH_BH
      ? "SFPSWAP\t%x2, %x3, %4"
      : "SFPSWAP\t%x2, %x3, %5, %4";
  }
  [(set_attr "type" "tensix")
   (set_attr "xtt_macro_resource" "simple_mad_write")
   (set_attr "xtt_replay" "safe")
   ;; SFPSWAP.md's next-cycle rule is an acceptance stall, not a constraint on
   ;; reading the result, so result latency 0.  The stall is described
   ;; separately by xtt_next_slot_stall.
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
          (match_operand:SI    4 "const_int_operand"  "n")
	  (const_int 1)
	  ] UNSPECV_SFPSWAP))]
  "TARGET_XTT_TENSIX"
  {
    return TARGET_XTT_TENSIX_WH_BH
      ? "SFPSWAP\t%x1, %x2, %3"
      : "SFPSWAP\t%x1, %x2, %4, %3";
  }
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; SFPSWAP.md's next-cycle rule is an acceptance stall, not a constraint on
   ;; reading the result, so result latency 0.  The stall is described
   ;; separately by xtt_next_slot_stall.
   (set_attr "xtt_result_latency" "1")
   (set_attr "xtt_next_slot_stall" "yes")
   ;; Effects as for rvtt_sfpswap_int (identical on WH and BH, assuming the
   ;; default LaneConfig; the planner does not allow configuration changes
   ;; around rows).  Here the VC source (operand 2) is a constant register
   ;; (L8..L15, SFPU_CREG_IDX_LWM) and SFPSWAP.md drops writes to LRegs >= 8
   ;; ("if (VC < 8)"), so the only write is the VD result tied to operand 0.
   ;; Both sources are read (constant registers are outside the
   ;; allocatable-LREG masks).  Lane-predicated; never writes CC.
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
          (match_operand:SI    5 "const_int_operand")
	  ] UNSPECV_SFPSWAP))
   (set (match_operand:XTT32SI 1 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_dup 2)
	  (match_dup 3)
          (match_dup 4)
          (match_dup 5)
	  ] UNSPECV_SFPSWAP))]
  "bool (find_reg_note (insn, REG_UNUSED, operands[1]))
   && !(cstlreg_operand (operands[2], XTT32SImode)
        && find_reg_note (insn, REG_UNUSED, operands[0]))"
  [(set (match_dup 0)
        (unspec_volatile:XTT32SI [
 	  (match_dup 2)
	  (match_dup 3)
          (match_dup 4)
          (match_dup 5)
	  (const_int 1)
	  ] UNSPECV_SFPSWAP))])

(define_insn "*rvtt_sfpswap_cst2"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "cstlreg_operand" "xs")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand" "0")
          (match_operand:SI    3 "const_int_operand"  "n")
          (match_operand:SI    4 "const_int_operand"  "n")
	  (const_int 2)
	  ] UNSPECV_SFPSWAP))]
  "TARGET_XTT_TENSIX"
  {
    return TARGET_XTT_TENSIX_WH_BH
      ? "SFPSWAP\t%x1, %x2, %3"
      : "SFPSWAP\t%x1, %x2, %4, %3";
  }
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; SFPSWAP.md's next-cycle rule is an acceptance stall, not a constraint on
   ;; reading the result, so result latency 0.  The stall is described
   ;; separately by xtt_next_slot_stall.
   (set_attr "xtt_result_latency" "1")
   (set_attr "xtt_next_slot_stall" "yes")
   ;; Effects as for rvtt_sfpswap_int; here the VD operand (operand 1) is a
   ;; constant register.  The "xs" constraint (cstlreg < 12) matches
   ;; SFPSWAP.md's condition for executing at all ("if (VD < 12 || ...)"), and
   ;; since constant registers are L8..L15 the VD-side write is dropped ("if
   ;; (VD < 8)").  The only write is the VC result tied to operand 0; both
   ;; sources are read.  Lane-predicated; never writes CC; default LaneConfig
   ;; assumed as for rvtt_sfpswap_int.
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
          (match_operand:SI    5 "const_int_operand")
	  ] UNSPECV_SFPSWAP))
   (set (match_operand:XTT32SI 1 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_dup 2)
	  (match_dup 3)
          (match_dup 4)
          (match_dup 5)
	  ] UNSPECV_SFPSWAP))]
  "find_reg_note (insn, REG_UNUSED, operands[0])
   && !(cstlreg_operand (operands[3], XTT32SImode)
        && find_reg_note (insn, REG_UNUSED, operands[1]))"
  [(set (match_dup 1)
        (unspec_volatile:XTT32SI [
 	  (match_dup 2)
	  (match_dup 3)
          (match_dup 4)
          (match_dup 5)
	  (const_int 2)
	  ] UNSPECV_SFPSWAP))])

(define_insn "*rvtt_sfpswap_cst3"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "cstlreg_operand" "xs")
     (match_operand:XTT32SI 1 "cstlreg_operand" "xc")
     (match_operand:SI    2 "const_int_operand"  "n")
     (match_operand:SI    3 "const_int_operand"  "n")
     (const_int 3)
  ] UNSPECV_SFPSWAP)]
  "TARGET_XTT_TENSIX"
  {
    return TARGET_XTT_TENSIX_WH_BH
      ? "SFPSWAP\t%x0, %x1, %2"
      : "SFPSWAP\t%x0, %x1, %3, %2";
  }
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; SFPSWAP.md's next-cycle rule is an acceptance stall, not a constraint on
   ;; reading the result, so result latency 0.  The stall is described
   ;; separately by xtt_next_slot_stall.
   (set_attr "xtt_result_latency" "1")
   (set_attr "xtt_next_slot_stall" "yes")
   ;; Effects as for rvtt_sfpswap_int with both operands constant registers
   ;; (VD constrained "xs" < 12 as SFPSWAP.md requires).  Both writes are
   ;; dropped since both registers are >= 8, so with the default LaneConfig
   ;; this reads its two sources and the lane state and writes no allocatable
   ;; LREG (empty write mask, no writeback port claimed).  Never writes CC.
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
          (match_operand:SI    5 "const_int_operand")
	  ] UNSPECV_SFPSWAP))
   (set (match_operand:XTT32SI 1 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_dup 2)
	  (match_dup 3)
          (match_dup 4)
          (match_dup 5)
	  ] UNSPECV_SFPSWAP))]
  "bool (find_reg_note (insn, REG_UNUSED, operands[0]))
   && bool (find_reg_note (insn, REG_UNUSED, operands[1]))"
  [(unspec_volatile:XTT32SI [
     (match_dup 2)
     (match_dup 3)
     (match_dup 4)
     (match_dup 5)
     (const_int 3)
     ] UNSPECV_SFPSWAP)])

(define_expand "rvtt_sfpswap_wh"
  [(set (match_operand:XTT64SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI 3 "const_int_operand")
	  ] UNSPECV_SFPSWAP))]
  "TARGET_XTT_TENSIX_WH_BH"
{
  emit_insn (gen_rvtt_sfpswap
    (operands[0], operands[1], operands[2], operands[3], const0_rtx));
  DONE;
})

(define_expand "rvtt_sfpswap"
  [(set (match_operand:XTT64SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI 3 "const_int_operand")
          (match_operand:SI 4 "const_int_operand")
	  ] UNSPECV_SFPSWAP))]
  "TARGET_XTT_TENSIX"
{
  rtx a = gen_reg_rtx (XTT32SImode);
  rtx b = gen_reg_rtx (XTT32SImode);
  emit_insn (gen_rvtt_sfpswap_int
    (a, b, operands[1], operands[2], operands[3], operands[4]));
  emit_insn (gen_rvtt_sfpconcat2
    (operands[0], a, b));
  DONE;
})

(define_insn "rvtt_sfptransp_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=x0")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 8 "reg_or_cstlreg_operand" "0")
	  (match_operand:XTT32SI 9 "reg_or_cstlreg_operand" "1")
	  (match_operand:XTT32SI 10 "reg_or_cstlreg_operand" "2")
	  (match_operand:XTT32SI 11 "reg_or_cstlreg_operand" "3")
	  ] UNSPECV_SFPTRANSP))
   (set (match_operand:XTT32SI 1 "register_operand" "=x1")
        (unspec_volatile:XTT32SI [
	  (match_dup 8)
	  (match_dup 9)
          (match_dup 10)
          (match_dup 11)
	  ] UNSPECV_SFPTRANSP))
   (set (match_operand:XTT32SI 2 "register_operand" "=x2")
        (unspec_volatile:XTT32SI [
	  (match_dup 8)
	  (match_dup 9)
          (match_dup 10)
          (match_dup 11)
	  ] UNSPECV_SFPTRANSP))
   (set (match_operand:XTT32SI 3 "register_operand" "=x3")
        (unspec_volatile:XTT32SI [
	  (match_dup 8)
	  (match_dup 9)
          (match_dup 10)
          (match_dup 11)
	  ] UNSPECV_SFPTRANSP))
   (set (match_operand:XTT32SI 4 "register_operand" "=x4")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 12 "reg_or_cstlreg_operand" "4")
	  (match_operand:XTT32SI 13 "reg_or_cstlreg_operand" "5")
	  (match_operand:XTT32SI 14 "reg_or_cstlreg_operand" "6")
	  (match_operand:XTT32SI 15 "reg_or_cstlreg_operand" "7")
	  ] UNSPECV_SFPTRANSP))
   (set (match_operand:XTT32SI 5 "register_operand" "=x5")
        (unspec_volatile:XTT32SI [
	  (match_dup 12)
	  (match_dup 13)
          (match_dup 14)
          (match_dup 15)
	  ] UNSPECV_SFPTRANSP))
   (set (match_operand:XTT32SI 6 "register_operand" "=x6")
        (unspec_volatile:XTT32SI [
	  (match_dup 12)
	  (match_dup 13)
          (match_dup 14)
          (match_dup 15)
	  ] UNSPECV_SFPTRANSP))
   (set (match_operand:XTT32SI 7 "register_operand" "=x7")
        (unspec_volatile:XTT32SI [
	  (match_dup 12)
	  (match_dup 13)
          (match_dup 14)
          (match_dup 15)
	  ] UNSPECV_SFPTRANSP))]
  "TARGET_XTT_TENSIX"
  "SFPTRANSP"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; Effects (SFPTRANSP.md: "Backend execution unit: Vector Unit (SFPU),
   ;; simple sub-unit"): one instruction permutes both four-register banks
   ;; (Transpose4(0) and Transpose4(4)), so the eight SETs are the complete
   ;; write set: reads operands 8-15, writes operands 0-7.  Each element write
   ;; is gated by LaneEnabled; never writes CC; no configuration or RWC
   ;; effect.  No writeback port and no macro template use are claimed.
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_read_ops" "65281")
   (set_attr "xtt_lreg_write_ops" "256")
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")])

(define_expand "rvtt_sfptransp"
  [(set (match_operand:XTT256SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 4 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 5 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 6 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 7 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 8 "reg_or_cstlreg_operand")
	  ] UNSPECV_SFPTRANSP))]
  "TARGET_XTT_TENSIX"
{
  rtx a = gen_reg_rtx (XTT32SImode);
  rtx b = gen_reg_rtx (XTT32SImode);
  rtx c = gen_reg_rtx (XTT32SImode);
  rtx d = gen_reg_rtx (XTT32SImode);
  rtx e = gen_reg_rtx (XTT32SImode);
  rtx f = gen_reg_rtx (XTT32SImode);
  rtx g = gen_reg_rtx (XTT32SImode);
  rtx h = gen_reg_rtx (XTT32SImode);

  emit_insn (gen_rvtt_sfptransp_int
    (a, b, c, d, e, f, g, h,
    operands[1], operands[2], operands[3], operands[4], operands[5], operands[6], operands[7], operands[8]));
  emit_insn (gen_rvtt_sfpconcat8
    (operands[0], a, b, c, d, e, f, g, h));
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
   ;; SFPSWAP.md's next-cycle rule is an acceptance stall, not a constraint on
   ;; reading the result, so result latency 0.  The stall is described
   ;; separately by xtt_next_slot_stall.
   (set_attr "xtt_result_latency" "1")
   (set_attr "xtt_next_slot_stall" "yes")
   ;; Effects (SFPSWAP.md, ENABLE_DEST_INDEX case): one SFPSWAP on the Simple
   ;; sub-unit, writing back through the MAD port as rvtt_sfpswap_int does.
   ;; With LaneConfig.ENABLE_DEST_INDEX it conditionally swaps the value pair
   ;; and, on the same per-lane condition, the companion pair
   ;; LReg[4+(VC&3)]/LReg[4+(VD&3)]; the register alternatives force companion
   ;; == value + 4, so the four SETs are the complete write set.  Reads
   ;; operands 4-7 and writes operands 0-3, lane-predicated; never writes CC;
   ;; no configuration or RWC effect.  Not claimed as usable in a macro
   ;; template.
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

;; SFPTRANSP of both banks whose builtin returns only L0--L3; the L4--L7
;; results remain explicit SETs of rvtt_sfptransp_int.
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
  emit_insn (gen_rvtt_sfptransp_int
    (result[0], result[1], result[2], result[3],
     result[4], result[5], result[6], result[7],
     operands[1], operands[2], operands[3], operands[4],
     operands[5], operands[6], operands[7], operands[8]));
  emit_insn (gen_rvtt_sfpconcat4
    (operands[0], result[0], result[1], result[2], result[3]));
  DONE;
})

;; Companion-preserving transpose bundle, formed only by the transp-involution
;; pass (-mtt-tensix-optimize-transp-involution) from an SFPTRANSP of four
;; freshly and fully loaded L0-L3 operands.  Emitted as one multi-word
;; instruction:
;;
;;   [SFPENCC all-lanes]   (operand 10 != 0 only: forced lane state)
;;   SFPTRANSP             (scrambles both banks; L0-L3 are dead here)
;;   SFPLOAD  L0, mod0, addr_mode, addr0
;;   SFPLOAD  L1, mod0, addr_mode, addr1
;;   SFPLOAD  L2, mod0, addr_mode, addr2
;;   SFPLOAD  L3, mod0, addr_mode, addr3
;;   SFPTRANSP             (transposes the fresh loads; descrambles L4-L7)
;;
;; Claiming only four SETs is correct (SFPTRANSP.md Transpose4, SFPLOAD.md):
;; with all lanes enabled, which the pass proves or the leading SFPENCC
;; forces, two transposes are the identity on the L4-L7 bank (element (reg
;; B+i, lane j*8+c) swaps with (reg B+j, lane i*8+c) and back), while the
;; loads overwrite L0-L3.  Afterwards L0-L3 hold the transposed rows and L4-L7
;; their incoming values.  With only some lanes enabled the pair is not an
;; involution, hence the all-lanes requirement.  Because the bundle is a
;; single insn, the intermediate state of L4-L7 cannot be observed.
;;
;; The loads use the no-increment address mode (checked by the pass), so the
;; bundle has no RWC effect.
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
   ;; Effects: writes exactly the four results (the companion bank is
   ;; unchanged, see above); reads no LREG; reads CC (every write is
   ;; lane-gated); the form with SFPENCC also writes CC (enabling all lanes).
   ;; No configuration or RWC effect.  Load-class result latency.
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

(define_expand "rvtt_sfp<rvtt_gtle_name>_bh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
          (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "const_int_operand")
          ] rvtt_gtle_op))]
  "TARGET_XTT_TENSIX_BH"
  {
    emit_insn (gen_rvtt_sfp<rvtt_gtle_name>_lv
      (operands[0], rvtt_gen_rtx_noval (XTT32SImode),
       operands[1], operands[2], operands[3], const0_rtx));
    DONE;
  })

(define_expand "rvtt_sfp<rvtt_gtle_name>_lv_bh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
          (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
          (match_operand:SI    4 "const_int_operand")
          ] rvtt_gtle_op))]
  "TARGET_XTT_TENSIX_BH"
  {
    emit_insn (gen_rvtt_sfp<rvtt_gtle_name>_lv
      (operands[0], operands[1],
       operands[2], operands[3], operands[4], const0_rtx));
    DONE;
  })

(define_expand "rvtt_sfp<rvtt_gtle_name>"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
          (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "const_int_operand")
          (match_operand:SI    4 "const_int_operand")
          ] rvtt_gtle_op))]
  "TARGET_XTT_TENSIX_QSR"
  {
    emit_insn (gen_rvtt_sfp<rvtt_gtle_name>_lv
      (operands[0], rvtt_gen_rtx_noval (XTT32SImode),
       operands[1], operands[2], operands[3], operands[4]));
    DONE;
  })

(define_expand "rvtt_sfp<rvtt_gtle_name>_lv"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
          (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
          (match_operand:SI    4 "const_int_operand")
          (match_operand:SI    5 "const_int_operand")
          ] rvtt_gtle_op))]
  "TARGET_XTT_TENSIX_BH_QSR"
  {
    // We clear this later if result is unused
    operands[4] = GEN_INT (INTVAL (operands[4]) | SFPGTLE_MOD1_SET_DEST);
  })

(define_insn_and_rewrite "*rvtt_sfp<rvtt_gtle_name>_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr,xr")
        (unspec_volatile:XTT32SI [
          (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_or_omit_operand"  "xn,xo,xrxc")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "0,0,0")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "xrxc,xrxc,xrxc")
          (match_operand:SI    4 "const_int_operand" "n,n,n")
          (match_operand:SI    5 "const_int_operand" "n,n,n")
          ] rvtt_gtle_op))]
  "TARGET_XTT_TENSIX_BH_QSR"
  "@
   SFP<rvtt_gtle_insn>\t%x0, %x3, %5, %4
   SFP<rvtt_gtle_insn>\t%x0, %x3, %5, %4\t# LV:%x2
   #"
  "&& true"
  {
    if (find_reg_note (curr_insn, REG_UNUSED, operands[0]))
      {
        emit_insn (gen_rvtt_sfp<rvtt_gtle_name>_nv
          (operands[2], operands[3],
           GEN_INT (INTVAL (operands[4]) & ~SFPGTLE_MOD1_SET_DEST),
	   operands[5]));
        DONE;
      }

    if (noval_or_omit_operand (operands[1], GET_MODE (operands[1])))
      FAIL; // nothing to change

    rvtt_merge_lv_src (&operands[1], &operands[2]);
  }
  ;; (BH, SET_VD form mod1 == 8 only) Per SFPGT.md and SFPLE.md: Simple
  ;; sub-unit; SET_VD writes -1/0 into the enabled lanes of VD; LaneFlags are
  ;; written only with SFPGT_MOD1_SET_CC and the flag stack only with
  ;; MUTATE_STACK, neither of which is set in mod 8.  So it reads the tied
  ;; destination and VC and writes the destination, with no CC write,
  ;; configuration or counter effect.  Other mods and other targets keep the
  ;; default attributes.
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; (BH mod1 == 8 only) SFPGT.md and SFPLE.md give no next-cycle rule:
   ;; result latency 0.  Other mods keep the default.
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
     (match_operand:XTT32SI 0 "reg_or_cstlreg_operand"  "xrxs")
     (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "xrxc")
     (match_operand:SI    2 "const_int_operand" "n")
     (match_operand:SI    3 "const_int_operand" "n")
     ] rvtt_gtle_op)]
  "TARGET_XTT_TENSIX_BH_QSR"
  "SFP<rvtt_gtle_insn>\t%x0, %x1, %3, %2"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; With mod1 == 1 and a zero immediate this is the word the SET_CC pattern
   ;; below emits, so it has that pattern's effects.  Every other mod1 keeps
   ;; the default attributes.
   (set (attr "xtt_subunit")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   && INTVAL (operands[2]) == 1
				   && INTVAL (operands[3]) == 0")
		      (const_string "simple") (const_string "none")))
   (set (attr "xtt_lreg_read_ops")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   && INTVAL (operands[2]) == 1
				   && INTVAL (operands[3]) == 0")
		      (const_int 4) (const_int 0)))
   (set (attr "xtt_lreg_write_ops")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   && INTVAL (operands[2]) == 1
				   && INTVAL (operands[3]) == 0")
		      (const_int 1) (const_int 0)))
   (set (attr "xtt_cc_effect")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   && INTVAL (operands[2]) == 1
				   && INTVAL (operands[3]) == 0")
		      (const_string "readwrite") (const_string "unknown")))
   (set (attr "xtt_config_effect")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   && INTVAL (operands[2]) == 1
				   && INTVAL (operands[3]) == 0")
		      (const_string "none") (const_string "unknown")))
   (set (attr "xtt_rwc_effect")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   && INTVAL (operands[2]) == 1
				   && INTVAL (operands[3]) == 0")
		      (const_string "none") (const_string "unknown")))])

(define_expand "rvtt_sfpmul24"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "const_int_operand")
	  ] UNSPECV_SFPMUL24))]
  "TARGET_XTT_TENSIX_BH_QSR"
{
  emit_insn (gen_rvtt_sfpmul24_lv
    (operands[0], rvtt_gen_rtx_noval (XTT32SImode),
     operands[1], operands[2], operands[3]));
  DONE;
})

(define_insn "rvtt_sfpmul24_lv"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn,0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc,xrxc")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "xrxc,xrxc")
          (match_operand:SI    4 "const_int_operand" "n,n")
	  ] UNSPECV_SFPMUL24))]
  "TARGET_XTT_TENSIX_BH_QSR"
  "@
   SFPMUL24\t%x0, %x2, %x3, %4
   SFPMUL24\t%x0, %x2, %x3, %4\t# LV:%x1"
  [(set_attr "xtt_subunit" "mad")
   (set_attr "xtt_result_latency" "2")
   (set_attr "xtt_lreg_write_port" "own")
   (set_attr "xtt_lreg_read_ops" "15")
   (set_attr "xtt_lreg_write_ops" "2")
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   (set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_delay" "dynamic")
   ;; Lane-gated consumer (typed-effect attributes).
   (set_attr "xtt_lane_gated" "yes")])

(define_expand "rvtt_sfparecip"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
          (match_operand:SI    2 "const_int_operand")
	  ] UNSPECV_SFPARECIP))]
  "TARGET_XTT_TENSIX_BH"
{
  emit_insn (gen_rvtt_sfparecip_lv
    (operands[0], rvtt_gen_rtx_noval (XTT32SImode),
     operands[1], operands[2]));
  DONE;
})

(define_insn "rvtt_sfparecip_lv"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand"  "xn,0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc,xrxc")
          (match_operand:SI    3 "const_int_operand" "n,n")
	  ] UNSPECV_SFPARECIP))]
  "TARGET_XTT_TENSIX_BH"
{
  /* SFPARECIP_MOD1_COND_RECIP reads the condition sign from LReg[VB]
     (ISA: BlackholeA0 SFPARECIP.md functional model), and VB is the
     imm12 field's low nibble -- which the gas mnemonic forces to zero
     (binutils riscv-opc-sfpu-insns.h: match mask 0xfff000).  The sfpi
     surface's single-source approx_recip(src, RecipMode::IfNegative)
     contract is "reciprocal where src is negative", i.e. VB == VC, so
     the mnemonic cannot express it; emit the architecturally-encoded
     raw word instead (TT_OP encoding: op 0x99, imm12<<12 | VC<<8 |
     VD<<4 | Mod1, VB == imm12 low nibble).  Mod1 RECIP and EXP ignore
     VB (functional model reads only LReg[VC]) and keep the mnemonic.  */
  if (INTVAL (operands[3]) == (int) SFPARECIP_MOD1_COND_RECIP)
    {
      unsigned vd = REGNO (operands[0]) - SFPU_REG_FIRST;
      unsigned vc;
      if (REG_P (operands[2]))
	vc = REGNO (operands[2]) - SFPU_REG_FIRST;
      else
	{
	  gcc_assert (GET_CODE (operands[2]) == UNSPEC
		      && XINT (operands[2], 1) == UNSPEC_SFPCSTLREG);
	  vc = (unsigned) INTVAL (XVECEXP (operands[2], 0, 0));
	}
      unsigned word = (0x99u << 24) | (vc << 12) | (vc << 8) | (vd << 4)
		      | SFPARECIP_MOD1_COND_RECIP;
      static char buf[96];
      snprintf (buf, sizeof (buf),
		".ttinsn\t%u\t# SFPARECIP\tL%u, L%u, %u (VB=L%u)%s",
		word, vd, vc, (unsigned) SFPARECIP_MOD1_COND_RECIP, vc,
		which_alternative == 1 ? " LV" : "");
      return buf;
    }
  return which_alternative == 0 ? "SFPARECIP\t%x0, %x2, %3"
				: "SFPARECIP\t%x0, %x2, %3\t# LV:%x1";
}
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; Typed-effect attributes: pure value unary, never touches CC;
   ;; lane-gated consumer.
   (set_attr "xtt_lane_local" "yes")
   (set_attr "xtt_cc_write" "no")
   (set_attr "xtt_lane_gated" "yes")])

(define_expand "rvtt_sfpnonlinear"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
          (match_operand:SI    2 "const_int_operand")
	  ] UNSPECV_SFPNONLINEAR))]
  "TARGET_XTT_TENSIX_QSR"
{
  emit_insn (gen_rvtt_sfpnonlinear_lv
    (operands[0], rvtt_gen_rtx_noval (XTT32SImode),
     operands[1], operands[2]));
  DONE;
})

(define_insn "rvtt_sfpnonlinear_lv"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand"  "xn,0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc,xrxc")
          (match_operand:SI    3 "const_int_operand" "n,n")
	  ] UNSPECV_SFPNONLINEAR))]
  "TARGET_XTT_TENSIX_QSR"
  "@
   SFPNONLINEAR\t%x0, %x2, %3
   SFPNONLINEAR\t%x0, %x2, %3\t# LV:%x1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_QSR)"))
   ;; Lane-gated consumer (typed-effect attributes).
   (set_attr "xtt_lane_gated" "yes")])

(define_expand "rvtt_ttsetrwc"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand")
     (match_operand:SI 1 "const_int_operand")
     (match_operand:SI 2 "const_int_operand")
     (match_operand:SI 3 "const_int_operand")
     (match_operand:SI 4 "const_int_operand")
     (match_operand:SI 5 "const_int_operand")
     ] UNSPECV_TTSETRWC)]
  "TARGET_XTT_TENSIX"
{
  if (TARGET_XTT_TENSIX_QSR)
    {
      /* QSR has one RWC value field rather than separate D/B/A fields, and it
         does not implement WH/BH's C-to-CR mode (CR bit 3).  D can therefore
         map to QSR's unified value only when the mask selects D and/or
         fidelity clear, CR is either disabled or D-only, and CR_D is paired
         with a D update.  Conservatively refuse every other form.  */
      HOST_WIDE_INT cr = INTVAL (operands[1]);
      HOST_WIDE_INT mask = INTVAL (operands[5]);
      if ((mask & ~HOST_WIDE_INT (0xc)) != 0
          || (cr != 0 && cr != 4)
          || ((cr & 4) != 0 && (mask & 4) == 0))
        {
          error ("QSR TTSETRWC cannot represent this CR/mask combination");
          DONE;
        }
      emit_insn (gen_rvtt_ttsetrwc_qsr (operands[0], operands[1],
                                        operands[2], operands[5]));
    }
  else
    emit_insn (gen_rvtt_ttsetrwc_wh_bh (operands[0], operands[1],
                                        operands[2], operands[3],
                                        operands[4], operands[5]));
  DONE;
})

/* These volatile patterns are the compiler-visible architectural RWC/Dst
   state boundary.  They must remain replay barriers and may not be treated as
   ordinary arithmetic or decoded later from opaque instruction words.  */
(define_insn "rvtt_ttsetrwc_wh_bh"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand" "n")
     (match_operand:SI 1 "const_int_operand" "n")
     (match_operand:SI 2 "const_int_operand" "n")
     (match_operand:SI 3 "const_int_operand" "n")
     (match_operand:SI 4 "const_int_operand" "n")
     (match_operand:SI 5 "const_int_operand" "n")
     ] UNSPECV_TTSETRWC)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  "TTSETRWC\t%0, %1, %2, %3, %4, %5"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")
   (set_attr "xtt_subunit" "sync")
   (set_attr "xtt_lreg_read_ops" "1")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "none")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "set")])

(define_insn "rvtt_ttsetrwc_qsr"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand" "n")
     (match_operand:SI 1 "const_int_operand" "n")
     (match_operand:SI 2 "const_int_operand" "n")
     (match_operand:SI 3 "const_int_operand" "n")
     ] UNSPECV_TTSETRWC)]
  "TARGET_XTT_TENSIX_QSR"
  "TTSETRWC\t%0, %1, %2, %3"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")
   (set_attr "xtt_subunit" "sync")
   (set_attr "xtt_lreg_read_ops" "1")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "none")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "set")])

(define_insn "rvtt_ttincrwc"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "const_int_operand" "n")
     (match_operand:SI    1 "const_int_operand" "n")
     (match_operand:SI    2 "const_int_operand" "n")
     (match_operand:SI    3 "const_int_operand" "n")
     ] UNSPECV_TTINCRWC)]
  "TARGET_XTT_TENSIX"
  "TTINCRWC\t%0, %1, %2, %3"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")
   (set_attr "xtt_subunit" "sync")
   (set_attr "xtt_lreg_read_ops" "1")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "none")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "inc")
   ;; INCRWC only updates the RWC counters; there is no LREG result.  WH
   ;; INCRWC.md gives no next-cycle rule (the BH documentation has no INCRWC
   ;; page), and the counter update takes effect at issue, so a following
   ;; SFPLOAD sees the stepped counter.  Result latency 0, BH/WH only.
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   || TARGET_XTT_TENSIX_WH")
		      (const_int 1) (const_int 0)))])

;; Dst/RWC face advance: one face is two Dst += 8 counter steps with no LREG,
;; CC or configuration effect.  Later passes recognize it as a run separator
;; by this pattern.  Raw `.ttinsn' words of the same kind (a SETRWC-class word
;; writing only the Dst counter pair) are decoded against the capability
;; tables and get the same effects (rvtt-raw-boundary.cc); other raw words
;; remain opaque.
(define_expand "rvtt_ttdstface"
  [(unspec_volatile:XTT32SI [(const_int 0)] UNSPECV_TTDSTFACE)]
  "TARGET_XTT_TENSIX"
{
  if (TARGET_XTT_TENSIX_QSR)
    {
      /* QSR's unified-RWC model has no defined Dst face-step form; refuse
         rather than approximate (as for TTSETRWC).  */
      error ("QSR cannot represent the Dst face advance");
      DONE;
    }
  emit_insn (gen_rvtt_ttdstface_wh_bh ());
  DONE;
})

;; Like TTSETRWC above, this volatile pattern is a compiler-visible RWC/Dst
;; state boundary and must remain a replay barrier.  The mnemonic and field
;; values are emission data only: the CR-mode Dst += 8 step (SETRWC CR=4, D=8,
;; mask=4) is issued twice to advance one face.
(define_insn "rvtt_ttdstface_wh_bh"
  [(unspec_volatile:XTT32SI [(const_int 0)] UNSPECV_TTDSTFACE)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  "TTSETRWC\t0, 4, 8, 0, 0, 4\;TTSETRWC\t0, 4, 8, 0, 0, 4"
  [(set_attr "type" "tensix")
   (set_attr "length" "8")
   (set_attr "xtt_issue" "sync")
   (set_attr "xtt_replay" "barrier")
   (set_attr "xtt_subunit" "sync")
   (set_attr "xtt_lreg_read_ops" "1")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "none")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "face")])

;; Compiler-owned address-modifier programming.  Operand 0 is a SETC16
;; configuration register index from a per-target capability table, operand 1
;; the 16-bit value.  Only passes that own the addressed slot emit this (see
;; rtl-rvtt-dst-autoincr.cc); the assembler owns the encoding.
(define_insn "rvtt_ttsetc16_int"
  [(unspec_volatile:SI [
     (match_operand:SI    0 "const_int_operand" "n")
     (match_operand:SI    1 "const_int_operand" "n")
     ] UNSPECV_TTSETC16)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  "TTSETC16\t%0, %1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")])

;; All-lanes enable for hoisted initialization: expands to the SFPENCC that
;; enables all lanes, the word cc_enable_all_lanes_proved_p compares against
;; (rvtt_macro::sfpencc_all_lanes_word).  It takes no arguments because the
;; word is an architectural constant.
(define_expand "rvtt_sfpencc_all_lanes"
  [(const_int 0)]
  "TARGET_XTT_TENSIX"
{
  uint32_t word;
  gcc_assert (rvtt_macro::sfpencc_encode (SFPENCC_IMM12_BOTH,
					  SFPENCC_MOD1_EI_RI, &word)
	      && word == rvtt_macro::sfpencc_all_lanes_word ());
  emit_insn (gen_rvtt_sfpencc (GEN_INT (SFPENCC_MOD1_EI_RI),
			       GEN_INT (SFPENCC_IMM12_BOTH)));
  DONE;
})

;; SETC16 usable from GIMPLE for hoisted initialization: emitted only by the
;; cross-call hoist to program a callee's address modifiers in its caller.
;; Expands to the pattern above; the assembler owns the encoding.
(define_expand "rvtt_ttsetc16"
  [(unspec_volatile:SI [
     (match_operand:SI    0 "const_int_operand")
     (match_operand:SI    1 "const_int_operand")
     ] UNSPECV_TTSETC16)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
{
  emit_insn (gen_rvtt_ttsetc16_int (operands[0], operands[1]));
  DONE;
})

;; ---------------------------------------------------------------------
;; FPU face-transpose family: Matrix Unit (FPU) Dst <-> SrcA/SrcB row moves,
;; the SrcB[16:32) 16x16 transpose, the wait-gate stall and the backend-config
;; byte read-modify-write, as used for face transposes in Tenstorrent's LLK
;; (llk_math_transpose_dest.h).  Semantics follow the
;; MOVD2B/MOVB2A/MOVB2D/MOVA2D/TRNSPSRCB/RMWCIB pages of the WormholeB0 ISA
;; documentation, which also cover Blackhole.
;;
;; These have no xtt_* effect attributes, so rvtt_insn_effects treats them as
;; opaque and nothing is moved across them: they access Dst rows and Src banks
;; through state (ALU formats, RWC counters, bank validity) the compiler does
;; not model.  They are replay barriers for the same reason.  Quasar encodes
;; this family differently and is rejected at expand time.
(define_expand "rvtt_ttmovd2b"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand")
     (match_operand:SI 1 "const_int_operand")
     (match_operand:SI 2 "const_int_operand")
     (match_operand:SI 3 "const_int_operand")
     (match_operand:SI 4 "const_int_operand")
     ] UNSPECV_TTMOVD2B)]
  "TARGET_XTT_TENSIX"
{
  if (TARGET_XTT_TENSIX_QSR)
    {
      error ("QSR TTMOVD2B is unaudited (encoding differs); refuse");
      DONE;
    }
  emit_insn (gen_rvtt_ttmovd2b_wh_bh (operands[0], operands[1], operands[2],
				      operands[3], operands[4]));
  DONE;
})

(define_insn "rvtt_ttmovd2b_wh_bh"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand" "n") ;; UseDst32bLo
     (match_operand:SI 1 "const_int_operand" "n") ;; SrcBRow
     (match_operand:SI 2 "const_int_operand" "n") ;; AddrMod
     (match_operand:SI 3 "const_int_operand" "n") ;; Move4Rows
     (match_operand:SI 4 "const_int_operand" "n") ;; DstRow
     ] UNSPECV_TTMOVD2B)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  "TTMOVD2B\t%0, %1, %2, %3, %4"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")])

(define_expand "rvtt_ttmovb2a"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand")
     (match_operand:SI 1 "const_int_operand")
     (match_operand:SI 2 "const_int_operand")
     (match_operand:SI 3 "const_int_operand")
     ] UNSPECV_TTMOVB2A)]
  "TARGET_XTT_TENSIX"
{
  if (TARGET_XTT_TENSIX_QSR)
    {
      error ("QSR TTMOVB2A is unaudited (encoding differs); refuse");
      DONE;
    }
  emit_insn (gen_rvtt_ttmovb2a_wh_bh (operands[0], operands[1], operands[2],
				      operands[3]));
  DONE;
})

(define_insn "rvtt_ttmovb2a_wh_bh"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand" "n") ;; SrcARow
     (match_operand:SI 1 "const_int_operand" "n") ;; AddrMod
     (match_operand:SI 2 "const_int_operand" "n") ;; Move4Rows
     (match_operand:SI 3 "const_int_operand" "n") ;; SrcBRow
     ] UNSPECV_TTMOVB2A)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  "TTMOVB2A\t%0, %1, %2, %3"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")])

(define_expand "rvtt_ttmovb2d"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand")
     (match_operand:SI 1 "const_int_operand")
     (match_operand:SI 2 "const_int_operand")
     (match_operand:SI 3 "const_int_operand")
     (match_operand:SI 4 "const_int_operand")
     ] UNSPECV_TTMOVB2D)]
  "TARGET_XTT_TENSIX"
{
  if (TARGET_XTT_TENSIX_QSR)
    {
      error ("QSR TTMOVB2D is unaudited (encoding differs); refuse");
      DONE;
    }
  emit_insn (gen_rvtt_ttmovb2d_wh_bh (operands[0], operands[1], operands[2],
				      operands[3], operands[4]));
  DONE;
})

(define_insn "rvtt_ttmovb2d_wh_bh"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand" "n") ;; UseDst32bLo
     (match_operand:SI 1 "const_int_operand" "n") ;; SrcBRow
     (match_operand:SI 2 "const_int_operand" "n") ;; AddrMod
     (match_operand:SI 3 "const_int_operand" "n") ;; Mode
     (match_operand:SI 4 "const_int_operand" "n") ;; DstRow
     ] UNSPECV_TTMOVB2D)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  "TTMOVB2D\t%0, %1, %2, %3, %4"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")])

(define_expand "rvtt_ttmova2d"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand")
     (match_operand:SI 1 "const_int_operand")
     (match_operand:SI 2 "const_int_operand")
     (match_operand:SI 3 "const_int_operand")
     (match_operand:SI 4 "const_int_operand")
     ] UNSPECV_TTMOVA2D)]
  "TARGET_XTT_TENSIX"
{
  if (TARGET_XTT_TENSIX_QSR)
    {
      error ("QSR TTMOVA2D is unaudited (encoding differs); refuse");
      DONE;
    }
  emit_insn (gen_rvtt_ttmova2d_wh_bh (operands[0], operands[1], operands[2],
				      operands[3], operands[4]));
  DONE;
})

(define_insn "rvtt_ttmova2d_wh_bh"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand" "n") ;; UseDst32bLo
     (match_operand:SI 1 "const_int_operand" "n") ;; SrcARow
     (match_operand:SI 2 "const_int_operand" "n") ;; AddrMod
     (match_operand:SI 3 "const_int_operand" "n") ;; Move8Rows
     (match_operand:SI 4 "const_int_operand" "n") ;; DstRow
     ] UNSPECV_TTMOVA2D)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  "TTMOVA2D\t%0, %1, %2, %3, %4"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")])

(define_expand "rvtt_tttrnspsrcb"
  [(unspec_volatile:XTT32SI [(const_int 0)] UNSPECV_TTTRNSPSRCB)]
  "TARGET_XTT_TENSIX"
{
  if (TARGET_XTT_TENSIX_QSR)
    {
      error ("QSR TTTRNSPSRCB is unaudited; refuse");
      DONE;
    }
  emit_insn (gen_rvtt_tttrnspsrcb_wh_bh ());
  DONE;
})

(define_insn "rvtt_tttrnspsrcb_wh_bh"
  [(unspec_volatile:XTT32SI [(const_int 0)] UNSPECV_TTTRNSPSRCB)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  "TTTRNSPSRCB"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")])

(define_expand "rvtt_ttstallwait"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand")
     (match_operand:SI 1 "const_int_operand")
     ] UNSPECV_TTSTALLWAIT)]
  "TARGET_XTT_TENSIX"
{
  if (TARGET_XTT_TENSIX_QSR)
    {
      error ("QSR TTSTALLWAIT is unaudited (encoding differs); refuse");
      DONE;
    }
  emit_insn (gen_rvtt_ttstallwait_wh_bh (operands[0], operands[1]));
  DONE;
})

(define_insn "rvtt_ttstallwait_wh_bh"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand" "n") ;; StallRes
     (match_operand:SI 1 "const_int_operand" "n") ;; WaitRes
     ] UNSPECV_TTSTALLWAIT)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  "TTSTALLWAIT\t%0, %1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")])

;; Operand 0 selects the byte lane (RMWCIB0..3); the mnemonic choice is
;; emission data owned by this pattern and the assembler.
(define_expand "rvtt_ttrmwcib"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand")
     (match_operand:SI 1 "const_int_operand")
     (match_operand:SI 2 "const_int_operand")
     (match_operand:SI 3 "const_int_operand")
     ] UNSPECV_TTRMWCIB)]
  "TARGET_XTT_TENSIX"
{
  if (TARGET_XTT_TENSIX_QSR)
    {
      error ("QSR TTRMWCIB is unaudited (encoding differs); refuse");
      DONE;
    }
  emit_insn (gen_rvtt_ttrmwcib_wh_bh (operands[0], operands[1], operands[2],
				      operands[3]));
  DONE;
})

(define_insn "rvtt_ttrmwcib_wh_bh"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand" "n") ;; ByteIndex
     (match_operand:SI 1 "const_int_operand" "n") ;; Mask8
     (match_operand:SI 2 "const_int_operand" "n") ;; Data8
     (match_operand:SI 3 "const_int_operand" "n") ;; CfgAddr8
     ] UNSPECV_TTRMWCIB)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
{
  switch (INTVAL (operands[0]))
    {
    case 0: return "TTRMWCIB0\t%1, %2, %3";
    case 1: return "TTRMWCIB1\t%1, %2, %3";
    case 2: return "TTRMWCIB2\t%1, %2, %3";
    case 3: return "TTRMWCIB3\t%1, %2, %3";
    default: gcc_unreachable ();
    }
}
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")])

;; MOP loop delivery (formed only by MOP-loop formation; capability facts in
;; rvtt-mop-tables.h).  MOP (opcode 0x01) runs the programmed template; the
;; operands are the raw encoding fields (mop_type, loop_count, zmask low
;; half).  MOP_CFG (opcode 0x03) sets the persistent zmask high half.  Both
;; are frontend (Tdma) work like REPLAY.  Quasar's MOP encoding differs and is
;; not supported.
(define_insn "rvtt_ttmop_int"
  [(unspec_volatile:SI [
     (match_operand:SI    0 "const_int_operand" "n") ;; mop_type
     (match_operand:SI    1 "const_int_operand" "n") ;; loop_count
     (match_operand:SI    2 "const_int_operand" "n") ;; zmask low half
     ] UNSPECV_TTMOP)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  "TTMOP\t%0, %1, %2"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")
   (set_attr "xtt_issue" "tdma")])

(define_insn "rvtt_ttmopcfg_int"
  [(unspec_volatile:SI [
     (match_operand:SI    0 "const_int_operand" "n") ;; zmask high half
     ] UNSPECV_TTMOPCFG)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  "TTMOPCFG\t%0"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")
   (set_attr "xtt_issue" "tdma")])

(define_expand "rvtt_ttreplay"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "reg_or_0_operand")
     (match_operand:SI    1 "reg_or_const_int_operand")
     (match_operand:SI    2 "reg_or_0_operand")
     (match_operand:SI    3 "const_int_operand")
     (match_operand:SI    4 "const_int_operand")
     (match_operand:SI    5 "const_int_operand")
     (match_operand:SI    6 "const_int_operand")
     ] UNSPECV_TTREPLAY)]
  "TARGET_XTT_TENSIX"
{
  auto mem = const0_rtx;
  auto opc = const0_rtx;
  auto enc = const0_rtx;
  auto imm = operands[1];
  if (!CONST_INT_P (imm))
    {
      mem = gen_rtx_MEM (SImode, operands[0]);
      int op
        = TARGET_XTT_TENSIX_WH  ? TT_OP_WH_REPLAY (INTVAL (operands[4]),
	                 0, INTVAL (operands[5]), INTVAL (operands[6]))
        : TARGET_XTT_TENSIX_BH  ? TT_OP_BH_REPLAY (INTVAL (operands[4]),
	                 0, INTVAL (operands[5]), INTVAL (operands[6]))
        : TARGET_XTT_TENSIX_QSR ? TT_OP_QSR_REPLAY (INTVAL (operands[4]),
	                 0, 0, 0, INTVAL (operands[5]), INTVAL (operands[6]))
        : (gcc_unreachable (), 0);
      opc = GEN_INT (op);
      enc = GEN_INT (rvtt_synth (UINTVAL (operands[3])));
      imm = operands[2];
    }

  emit_insn (gen_rvtt_ttreplay_int
    (mem, opc, enc, imm,
     rvtt_gen_rtx_noval (XTT32SImode),
     operands[4], operands[5], operands[6]));
  DONE;
})

(define_insn "rvtt_ttreplay_int"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "mem_or_0_operand" "J,m")
     (match_operand:SI    1 "const_int_operand" "J,n") ;; opcode
     (match_operand:SI    2 "const_int_operand" "J,n") ;; id, src & dst shifts
     (match_operand:SI    3 "reg_or_const_int_operand" "n,r") ;; imm or insn
     (match_operand:XTT32SI 4 "noval_operand" "xn,xn") ;; src (none)
     (match_operand:SI    5 "const_int_operand"  "n,n")
     (match_operand:SI    6 "const_int_operand"  "n,n") ;; exec-while-load
     (match_operand:SI    7 "const_int_operand"  "n,n") ;; load
     ] UNSPECV_TTREPLAY)]
  "TARGET_XTT_TENSIX"
  {
    return rvtt_synth::pattern (which_alternative,
      TARGET_XTT_TENSIX_QSR ? "TTREPLAY\t%5, %3, 0, 0, %6, %7"
      : "TTREPLAY\t%5, %3, %6, %7",
      operands, false, -1);
  }
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "owner")
   ;; REPLAY (opcode 0x04) is frontend work, classed as Tdma rather than as
   ;; the SFPU work it may expand into.
   (set_attr "xtt_issue" "tdma")])

(define_expand "rvtt_ttinsn"
  [(unspec_volatile
    [(match_operand:SI    0 "reg_or_0_operand")
     (match_operand:SI    1 "reg_or_const_int_operand")
     (match_operand:SI    2 "const_int_operand")
     (match_operand:SI    3 "const_int_operand")
     (match_operand:SI    4 "const_int_operand")
     ] UNSPECV_TTINSN)]
  "TARGET_XTT_TENSIX"
{
  auto mem = const0_rtx;
  auto val = operands[1];

  if (!CONST_INT_P (val))
    mem = gen_rtx_MEM (SImode, operands[0]);
  emit_insn (gen_rvtt_ttinsn_int (mem, val));
  DONE;
})

(define_insn "rvtt_ttinsn_int"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "mem_or_0_operand"  "J,m")
     (match_operand:SI    1 "reg_or_const_int_operand" "n,r")
     ] UNSPECV_TTINSN)]
  "TARGET_XTT_TENSIX"
  "@
   .ttinsn\t%1
   sw\t%1,%0"
  [(set_attr "type" "tensix")])
