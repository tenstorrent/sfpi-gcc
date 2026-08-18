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
  UNSPECV_SFPLUTFP32_3R
  UNSPECV_SFPLUTFP32_6R

  UNSPECV_SFPSWAP
  UNSPECV_SFPTRANSP
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
  UNSPECV_TTSETC16
  UNSPECV_TTMOP
  UNSPECV_TTMOPCFG
])

(define_enum "xtt_delay" [
  none
  static
  dynamic
])
(define_enum_attr "xtt_delay" "xtt_delay"
  (const_string "none"))
;; F1.2 bubble cost hook.  It deliberately depends only on the established
;; delay contract, never on issue class.  The scheduler still decides whether
;; a STATIC/DYNAMIC delay applies using its existing probe and erratum logic;
;; this generated target attribute only supplies the (currently fixed) count.
;; Values above one require an emitted-Tensix distance walker and are deferred
;; to F1.3.
(define_attr "xtt_delay_bubbles" ""
  (if_then_else (eq_attr "xtt_delay" "static,dynamic")
                (const_int 1)
                (const_int 0)))

;; Instructions marked safe here have no hidden CC, Dst, RWC, template, or
;; replay ownership effects.  The post-RA latency scheduler still proves all
;; physical-register dependencies before moving one.  Defaulting to barrier
;; keeps new patterns ineligible until their effects are audited.
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

;; Resource vocabulary consumed by the dump-only SFPLOADMACRO descriptor.
;; This does not make an instruction macro-eligible: the verifier must still
;; prove dependencies, per-target timing, write-port use, and event semantics.
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

;; This is a compiler-only raw-LLK ownership marker placed after the opaque
;; instruction region it describes.  Operand 0 releases values after their
;; last raw use; operand 1 starts newly written values.  It must remain
;; volatile until the pre-IRA lreg-livein pass has made the indicated hard
;; LREG values visible to IRA, but it emits no Tensix instruction itself.
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

;; Typed effects declaration markers for a raw instruction region (D2
;; compiler half; consumed at gimple by the prgm-const freedom proof).
;; Zero-length ghosts: they emit no Tensix instruction word.
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

;; These builtins are converted by gimple passes, but the insns are still
;; needed due to the way we expand them.

(define_expand "rvtt_sfpxvif"
  [(set (match_operand:SI 0 "register_operand")
        (unspec:SI [
	  (const_int 0)
	  ] 0))]
  "TARGET_XTT_TENSIX"
  "FAIL;")

(define_expand "rvtt_sfpxbool"
  [(set (match_operand:SI 0 "register_operand")
        (unspec:SI [
          (match_operand:SI 1 "register_operand")
          ] 0))]
  "TARGET_XTT_TENSIX"
  "FAIL;")

(define_expand "rvtt_sfpxcondb"
  [(unspec:SI [
     (match_operand:SI 0 "register_operand")
     (match_operand:SI 1 "register_operand")
     ] 0)]
  "TARGET_XTT_TENSIX"
  "FAIL;")

(define_expand "rvtt_sfpxcondi"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec:SI [
	  (match_operand:SI 1 "register_operand")
	  ] 0))]
  "TARGET_XTT_TENSIX"
  "FAIL;")

(define_expand "rvtt_sfpxicmps"
  [(set (match_operand:SI 0 "register_operand")
        (unspec:SI [
	  (match_operand:SI    1 "reg_or_0_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_0_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] 0))]
  "TARGET_XTT_TENSIX"
  "FAIL;")

(define_expand "rvtt_sfpxicmpv"
  [(set (match_operand:SI 0 "register_operand")
        (unspec:SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "const_int_operand")
	  ] 0))]
  "TARGET_XTT_TENSIX"
  "FAIL;")

(define_expand "rvtt_sfpxfcmps"
  [(set (match_operand:SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "reg_or_0_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_const_int_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] 0))]
  "TARGET_XTT_TENSIX"
{
  rvtt_emit_sfpxfcmps (operands[2], operands[3], operands[6]);
  DONE;
})

(define_expand "rvtt_sfpxfcmpv"
  [(set (match_operand:SI 0 "register_operand")
        (unspec_volatile:SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "const_int_operand")
	  ] 0))]
  "TARGET_XTT_TENSIX"
{
  rvtt_emit_sfpxfcmpv (operands[1], operands[2], operands[3]);
  DONE;
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
  rvtt_emit_sfpxloadi (operands[0], rvtt_gen_rtx_noval (XTT32SImode),
			       operands[2]);
  DONE;
})

(define_expand "rvtt_sfpxiadd_v"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand"  "0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI      3 "const_int_operand" "n")
	  ] 0))]
  "TARGET_XTT_TENSIX"
{
  rvtt_emit_sfpxiadd_v (operands[0], operands[1], operands[2], operands[3]);
  DONE;
})

(define_expand "rvtt_sfpxiadd_i"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "const_int_operand")
          (match_operand:SI    5 "reg_or_const_int_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] 0))]
  "TARGET_XTT_TENSIX"
{
  emit_insn (gen_rvtt_sfpxiadd_i_lv
    (operands[0], operands[1], rvtt_gen_rtx_noval (XTT32SImode),
     operands[2], operands[3], operands[4], operands[5], operands[6]));
  DONE;
})

(define_expand "rvtt_sfpxiadd_i_lv"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
          (match_operand:SI    4 "reg_or_const_int_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:SI    6 "reg_or_const_int_operand")
          (match_operand:SI    7 "const_int_operand")
	  ] 0))]
  "TARGET_XTT_TENSIX"
{
  rvtt_emit_sfpxiadd_i (operands[0], operands[2], operands[1], operands[3], operands[4], operands[7]);
  DONE;
})

(define_insn "rvtt_sfpconcat2"
  [(set (match_operand:XTT64SI 0 "register_operand" "=xr")
     (unspec:XTT64SI [
       (match_operand:XTT32SI 1 "register_operand" "xr")
       (match_operand:XTT32SI 2 "register_operand" "xr")
       ] UNSPEC_SFPCLEAVE))]
  "TARGET_XTT_TENSIX"
  "CONCAT %0, %1, %2"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")])

(define_insn "rvtt_sfpselect2"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
     (unspec:XTT32SI [
       (match_operand:XTT64SI 1 "register_operand" "xr")
       (match_operand:SI 2 "const_int_operand" "n")
       ] UNSPEC_SFPCLEAVE))]
  "TARGET_XTT_TENSIX"
  "SELECT %0, %1, %2"
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
  "CONCAT %0, %1, %2, %3, %4"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")])

(define_insn "rvtt_sfpselect4"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
     (unspec:XTT32SI [
       (match_operand:XTT128SI 1 "register_operand" "xr")
       (match_operand:SI 2 "const_int_operand" "n")
       ] UNSPEC_SFPCLEAVE))]
  "TARGET_XTT_TENSIX"
  "SELECT %0, %1, %2"
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
  "SFPNOP\t%0, %1, %2"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")])

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
  ;; is the all-lanes SFPMOV mod-2 copy (craq-sim TENSIX_EXECUTE_SFPMOV
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
  ;; Audited (WP9 CC-template extension): the surviving alternative is
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
  ;; Effect audit (D3 latency audit, WH/BH): craq-sim
  ;; TENSIX_EXECUTE_SFPLOADI writes the destination's enabled lanes for
  ;; mod0 0-8 and 10 (8/10 are the half-word merges, reading the tied
  ;; live value), touches no CC bit, no configuration word and no
  ;; counter; SFPLOADI.md carries no next-cycle constraint, and the
  ;; silicon-proven hand exp kernel (ckernel_sfpu_exp.h) consumes its
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
		      (const_int 1) (const_int 0)))])

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
;; rvtt_owned_setc16_int was deleted with the quarantined pass at WP8.)
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
   ;; cross-unit Dst race, not an SFPU result delay; the silicon-proven
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
   ;; Replay-membership audit (WP10): a launch is a pure instruction
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
   ;; byte-identical to the pre-flag output (the CRAQ-proven shapes are
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
;; (Replaced the fixed-L2 rvtt_sfploadmacro_swap_int, deleted at WP7.)
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
   ;; Replay-membership audit (WP10): as rvtt_sfploadmacro_int above;
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
   (set_attr "xtt_macro_encodable" "yes")])

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

(define_insn "rvtt_sfpsetcc_i"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI   0 "const_int_operand" "n")
     (match_operand:SI   1 "const_int_operand" "n")
     ] UNSPECV_SFPSETCC)]
  "TARGET_XTT_TENSIX"
  "SFPSETCC\tL0, %1, %0"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_read_ops" "1")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "readwrite")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")])

;; Audited (WP8) so predicated shapes name their CC capability at the
;; predicate write instead of dissolving into an opaque boundary; since
;; the WP9 CC-template extension this is the select calendar's
;; predicate-definition event (region discovery admits it as a row
;; member; unproven CC forms still refuse by name).
(define_insn "rvtt_sfpsetcc_v"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "reg_or_cstlreg_operand"  "xrxc")
     (match_operand:SI   1 "const_int_operand" "n")
     ] UNSPECV_SFPSETCC)]
  "TARGET_XTT_TENSIX"
  "SFPSETCC\t%x0, 0, %1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_read_ops" "2")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "readwrite")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")])

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

(define_insn "rvtt_sfpcompc"
  [(unspec_volatile:XTT32SI [
     (const_int 0)
     ] UNSPECV_SFPCOMPC)]
  "TARGET_XTT_TENSIX"
  "SFPCOMPC"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")])

(define_insn "rvtt_sfppushc"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "const_int_operand" "n")
     ] UNSPECV_SFPPUSHC)]
  "TARGET_XTT_TENSIX"
  "SFPPUSHC\t%0"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")])

(define_insn "rvtt_sfppopc"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "const_int_operand" "n")
     ] UNSPECV_SFPPOPC)]
  "TARGET_XTT_TENSIX"
  "SFPPOPC\t%0"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")])

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
   (set_attr "xtt_rwc_effect" "none")])

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
   (set_attr "xtt_rwc_effect" "none")])

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
   (set_attr "xtt_rwc_effect" "none")])

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
   ;; Effects audited per mod1 (operand 4) from craq-sim
   ;; TENSIX_EXECUTE_SFPIADD + the SFPIADD.md functional model, both of
   ;; which cover WH and BH only (QSR has no simulator specification and
   ;; keeps the refusing defaults).  The proven envelope for this
   ;; register-argument pattern is mod1 <= 10 with the ARG_IMM bit
   ;; clear: reads VC (operand 3) and VB=VD (operand 2), writes VD
   ;; (operand 0), lane-predicated.  LaneFlags are written unless
   ;; MOD1_CC_NONE is set without MOD1_CC_GTE0 ((mod1 & 12) == 4);
   ;; craq-sim and the functional model agree on that effect class for
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
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_BH | XTT_DYNAMIC_BUG_QSR)"))])

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
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_BH | XTT_DYNAMIC_BUG_QSR)"))])

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
   (set_attr "xtt_replay" "safe")])

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
   (set_attr "xtt_replay" "safe")])

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
  ;; the craq-sim executors (TENSIX_EXECUTE_SFPMOV/SFPEXEXP/SFPEXMAN/
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
  ;; exexp->exman->shft->exman->cast back-to-back on silicon): result
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
		      (const_int 1) (const_int 0)))])

(define_insn "rvtt_sfp<rvtt_unary_name>_nv"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "reg_or_cstlreg_operand" "xrxc")
     (match_operand:SI    1 "const_int_operand" "n")
     ] rvtt_unary_op)]
  "TARGET_XTT_TENSIX_QSR && <rvtt_unary_op> == UNSPECV_SFPLZ"
  "SFP<rvtt_unary_insn>\tL15, %x0, %1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")])

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
  ;; against craq-sim TENSIX_EXECUTE_SFPSETEXP/SFPSETMAN/SFPSETSGN:
  ;; the register forms (SETEXP mod 0/2, SETMAN mod 0, SETSGN mod 0)
  ;; read the source (operand 2) and the tied destination, lane-write
  ;; the destination, touch no CC bit, configuration word, or counter
  ;; (mod 1 is the immediate form carried by the _i patterns; higher
  ;; mods are simulator-refused and keep the refusing defaults).
  ;; Sub-unit: S1 Simple column; Simple dependence chains step one slot
  ;; (silicon-proven hand exp kernel runs SFPAND->SFPSETEXP and
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
		      (const_int 1) (const_int 0)))])

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
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")])

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
  ;; Effect audit (D3 latency audit, WH/BH): craq-sim
  ;; TENSIX_EXECUTE_SFPAND/SFPOR/SFPXOR (tensix_execute_sfpu_int32)
  ;; read the tied destination and operand 3 and lane-write the
  ;; destination; no CC write, configuration, or counter effect.
  ;; Sub-unit: S1 Simple; the silicon-proven hand exp kernel runs
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
		      (const_int 1) (const_int 0)))])

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
  ;; Effect audit (D3 latency audit, BH): craq-sim mod1<=1 branch of
  ;; TENSIX_EXECUTE_SFPAND/SFPOR — reads operands 2 and 3 (and the tied
  ;; live value), lane-writes the destination; no CC write,
  ;; configuration, or counter effect.  Higher mods are simulator-
  ;; refused and keep the refusing defaults.  S1 Simple; result latency
  ;; 0 (hand exp kernel SFPAND->SFPSETEXP back-to-back on silicon).
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
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_BH)"))])

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
   (set_attr "xtt_replay" "safe")])

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
  ;; craq-sim TENSIX_EXECUTE_SFPSHFT: the variable-shift forms (mod 0
  ;; logical on both, mod 2 arithmetic-right on BH only) read the shift
  ;; amount (operand 3) and the tied destination, lane-write the
  ;; destination, touch no CC bit, configuration word, or counter.
  ;; Sub-unit: S1 Simple; the silicon-proven hand exp kernel runs
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
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_BH | XTT_DYNAMIC_BUG_QSR)"))])

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
   ;; Subunit is the 9(h)-class inference recorded in NOTES-wp6-prep.md:
   ;; the shift is a Simple-unit event by analogy with the documented
   ;; cast-round Simple assignments; flagged for the architectural
   ;; reference.
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
   ;;   mod1 0 (SM32->FP32 round-nearest-even): craq-sim
   ;;     TENSIX_EXECUTE_SFPCAST + SFPCAST.md functional model.
   ;;   mod1 3, BH only (self-inverse sign-preserving conditional
   ;;     negate, the SM32<->INT32 conversion): craq-sim mod3 branch +
   ;;     SFPCAST_IntInt.md; silicon exact-equality boundary evidence
   ;;     (convert-smag-evidence-20260816).
   ;; mod1 1 (stochastic rounding) additionally advances the PRNG --
   ;; architectural state outside the effect vocabulary -- and BH mod1 2
   ;; is the documented cast-as-ABS hardware bug the simulator refuses
   ;; to execute; both keep the refusing defaults, as does every higher
   ;; (non-contractual) mod.  QSR retains only the mod-0 claim the WP5
   ;; audit recorded (unproven-by-simulator; flagged with review
   ;; carry-forward risk 1).
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
   ;; SFPCAST->SFPMAD back-to-back on silicon: result latency 0
   ;; (audited mods only; unaudited mods stay opaque via the fields
   ;; above).
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "INTVAL (operands[3]) == 0
				   || (TARGET_XTT_TENSIX_BH
				       && INTVAL (operands[3]) == 3)")
		      (const_int 1) (const_int 0)))])

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
   (set_attr "xtt_replay" "safe")])

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
   ;; silicon: result latency 0.
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   || TARGET_XTT_TENSIX_WH")
		      (const_int 1) (const_int 0)))])

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
   ;; silicon: result latency 0.
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   || TARGET_XTT_TENSIX_WH")
		      (const_int 1) (const_int 0)))])

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
   (set_attr "xtt_delay" "dynamic")])

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
   (set_attr "xtt_delay" "dynamic")])

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
   (set_attr "xtt_delay" "dynamic")])

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
   (set_attr "xtt_delay" "dynamic")])

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
   ;; Same audited SFPSWAP effect envelope as rvtt_sfpswap_int (WH and
   ;; BH functional models are bit-identical; default-LaneConfig
   ;; envelope, the planner refuses config mutation around rows).  Here
   ;; the VC source (operand 2) is a hardware constant register -- every
   ;; cstlreg is L8..L15 (SFPU_CREG_IDX_LWM) -- and SFPSWAP.md drops
   ;; writes to LRegs >= 8 ("if (VC < 8)"), matching craq-sim
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
   ;; Same audited SFPSWAP effect envelope as rvtt_sfpswap_int; here the
   ;; VD operand (operand 1) is a hardware constant register.  The "xs"
   ;; constraint (cstlreg < 12) is exactly SFPSWAP.md's VD execution
   ;; gate ("if (VD < 12 || ...)"), and since every cstlreg is L8..L15
   ;; the VD-side write is architecturally dropped ("if (VD < 8)",
   ;; matching craq-sim).  The surviving write is the VC result tied to
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
   ;; Same audited SFPSWAP effect envelope as rvtt_sfpswap_int with BOTH
   ;; operands hardware constant registers (VD constrained "xs" < 12,
   ;; SFPSWAP.md's VD execution gate).  Every cstlreg is L8..L15, so
   ;; both architectural writes are dropped ("if (VC < 8)" /
   ;; "if (VD < 8)", matching craq-sim): under the default-LaneConfig
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
   ;; Audited multi-result effect envelope (SFPSWAP.md functional model,
   ;; ENABLE_DEST_INDEX leg; craq-sim TENSIX_EXECUTE_SFPSWAP agrees).  One
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
   ;; execution unit: Vector Unit (SFPU), simple sub-unit"; craq-sim
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
  "SFPSHFT2\t%x0 %x0, 0, %8"
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
  ;; (2) craq-sim TENSIX_EXECUTE_SFPGT/SFPLE (mod1==8 arm): reads the
  ;;     tied destination and lreg_c, lane-writes the destination mask,
  ;;     no CC write, configuration, or counter effect;
  ;; (3) the silicon-proven hand exp kernel issues SFPGT inside the
  ;;     poly-MAD chain's shadow with its consumer SFPAND in the S1
  ;;     Simple column -- the same one-slot Simple dependence stepping
  ;;     whose latency-0 the SFPAND->SFPSETEXP back-to-back audit
  ;;     already carries.
  ;; Every other mod1 (CC-setting and stack-mutating forms) keeps the
  ;; refusing defaults, as does every non-BH target.
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
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
   (set_attr "xtt_delay" "dynamic")])

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
  "@
   SFPARECIP\t%x0, %x2, %3
   SFPARECIP\t%x0, %x2, %3\t# LV:%x1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")])

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
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_QSR)"))])

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
   (set_attr "xtt_rwc_effect" "inc")])

;; Typed architectural Dst/RWC face advance: one face is two architectural
;; Dst += 8 counter steps with no LREG, CC, or configuration effect.  Late
;; analyses recognize the run-separator effect by this typed identity.
;; Raw `.ttinsn' constant words of the same architectural class (a
;; SETRWC-class word writing only the Dst counter pair) are field-decoded
;; against the capability tables and carry the identical effect set
;; (rvtt-raw-boundary.cc); every other raw word remains opaque and
;; refuses.
(define_expand "rvtt_ttdstface"
  [(unspec_volatile:XTT32SI [(const_int 0)] UNSPECV_TTDSTFACE)]
  "TARGET_XTT_TENSIX"
{
  if (TARGET_XTT_TENSIX_QSR)
    {
      /* QSR's unified-RWC model has no defined Dst face-step form; refuse
         rather than approximate (mirror of the TTSETRWC QSR refusal).  */
      error ("QSR cannot represent the Dst face advance");
      DONE;
    }
  emit_insn (gen_rvtt_ttdstface_wh_bh ());
  DONE;
})

;; Like the typed TTSETRWC above, this volatile pattern is the
;; compiler-visible architectural RWC/Dst state boundary; it must remain a
;; replay barrier.  The architectural mnemonic and its field values are
;; emission data owned by this pattern and the assembler, never decision
;; logic: the CR-mode Dst += 8 step (SETRWC CR=4, D=8, mask=4) is issued
;; twice to advance exactly one face.
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
;; configuration register index taken from a per-target capability table,
;; operand 1 the 16-bit value.  Only compiler passes that have proven
;; ownership of the addressed slot emit this (see rtl-rvtt-dst-autoincr.cc);
;; the assembler owns the encoding.
(define_insn "rvtt_ttsetc16_int"
  [(unspec_volatile:SI [
     (match_operand:SI    0 "const_int_operand" "n")
     (match_operand:SI    1 "const_int_operand" "n")
     ] UNSPECV_TTSETC16)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  "TTSETC16\t%0, %1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")])

;; MOP loop delivery (formed only by the rvtt_mop_form pass; capability
;; facts and provenance in rvtt-mop-tables.h).  MOP (opcode 0x01) fires
;; the programmed template; the operands are the raw encoding fields
;; (mop_type, loop_count, zmask low half).  MOP_CFG (opcode 0x03) sets
;; the persistent zmask high half.  Both are frontend work like REPLAY,
;; which craq-sim classifies as Tdma.  QSR's MOP encoding differs and is
;; not provided.
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
   ;; REPLAY is frontend work (opcode 0x04), which craq-sim classifies as
   ;; Tdma rather than as the SFPU work it may later expand into.
   (set_attr "xtt_issue" "tdma")])
