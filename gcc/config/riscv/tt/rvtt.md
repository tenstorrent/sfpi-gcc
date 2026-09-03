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

(include "tt/rvtt-pseudo.md")
(include "tt/rvtt-move.md")
(include "tt/rvtt-ldst.md")
(include "tt/rvtt-cc.md")
(include "tt/rvtt-arith.md")
(include "tt/rvtt-bitops.md")
(include "tt/rvtt-round.md")
(include "tt/rvtt-config.md")
(include "tt/rvtt-lut.md")
(include "tt/rvtt-permute.md")
(include "tt/rvtt-cmp.md")
(include "tt/rvtt-mathext.md")
(include "tt/rvtt-tensix.md")
