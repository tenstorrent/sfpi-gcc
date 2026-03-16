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

(include "tt/rvtt-predicates.md")
(include "tt/rvtt-tune.md")

; & in spec means early clobber, written before inputs are used, cannot reuse input reg

(define_c_enum "unspec" [
  UNSPEC_SYNTH_OPCODE
  UNSPEC_SFPCSTLREG
  UNSPEC_SFPNOVAL
  UNSPEC_SFPCLEAVE ; cleave together and cleave apart, yay auto-antonyms!
])

(define_c_enum "unspecv" [
  ;; Tenstorrent SFPU unspecs.
  ;; INT for internal
  ;; IMM for immediate
  ;; LV for keep dst reg alive as input for predicated liveness

  UNSPECV_SFPVARLREG

  UNSPECV_SFPNOP
  UNSPECV_SFPASSIGN

  UNSPECV_SFPLOADI
  UNSPECV_SFPLOAD
  UNSPECV_SFPSTORE

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

  UNSPECV_TTINCRWC
  UNSPECV_TTREPLAY
])

(define_enum "xtt_delay" [
  none
  static
  dynamic
])
(define_enum_attr "xtt_delay_wh" "xtt_delay"
  (const_string "none"))
(define_enum_attr "xtt_delay_bh" "xtt_delay"
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
		   "li\t%%0, %%1\t# %d:%x", unsigned (INTVAL (operands[2])),
		   unsigned (INTVAL (operands[1])));
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
  ""
  [(set_attr "type" "tensix")
   (set_attr "length" "0")])

(define_insn "rvtt_sfpreadlreg<rvtt_lregs_value>"
  [(set (match_operand:XTT32SI 0 "register_operand" "=x<rvtt_lregs_value>")
        (unspec_volatile:XTT32SI [
	  (const_int rvtt_lregs)
	  ] UNSPECV_SFPVARLREG))]
  "TARGET_XTT_TENSIX"
  ""
  [(set_attr "type" "tensix")
   (set_attr "length" "0")])

(define_expand "rvtt_sfpnovalue"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec:XTT32SI [
	  (const_int 0)
	  ] UNSPEC_SFPNOVAL))]
  "TARGET_XTT_TENSIX")

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
  rvtt_emit_sfpxfcmps (operands[1], operands[2], operands[3], operands[6]);
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
  emit_insn (gen_rvtt_sfpxloadi_lv
    (operands[0], operands[1], rvtt_gen_rtx_noval (XTT32SImode),
     operands[2], operands[3], operands[4], operands[5]));
  DONE;
})

(define_expand "rvtt_sfpxloadi_lv"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_0_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] 0))]
  "TARGET_XTT_TENSIX"
{
  rvtt_emit_sfpxloadi
    (operands[0], operands[2],
     operands[1], operands[6], operands[3], operands[4], operands[5]);
  DONE;
})

(define_expand "rvtt_sfpxiadd_v"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand"  "0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI    3 "const_int_operand" "N04U")
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
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpselect2"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
     (unspec:XTT32SI [
       (match_operand:XTT64SI 1 "register_operand" "xr")
       (match_operand:SI 2 "const_int_operand" "n")
       ] UNSPEC_SFPCLEAVE))]
  "TARGET_XTT_TENSIX"
  "SELECT %0, %1, %2"
  [(set_attr "type" "tensix")])

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
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpselect4"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
     (unspec:XTT32SI [
       (match_operand:XTT128SI 1 "register_operand" "xr")
       (match_operand:SI 2 "const_int_operand" "n")
       ] UNSPEC_SFPCLEAVE))]
  "TARGET_XTT_TENSIX"
  "SELECT %0, %1, %2"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpnop"
  [(unspec_volatile:XTT32SI [
     (const_int 0)
     ] UNSPECV_SFPNOP)]
  "TARGET_XTT_TENSIX"
  "SFPNOP"
  [(set_attr "type" "tensix")])

(define_expand "movxtt32si"
  [(set (match_operand:XTT32SI 0 "")
	(match_operand:XTT32SI 1 ""))]
  "TARGET_XTT_TENSIX"
{
  if (riscv_legitimize_move (GET_MODE (operands[0]), operands[0], operands[1]))
    DONE;
})

(define_insn "*rvtt_store"
  [(set (match_operand:XTT32SI 0 "memory_operand" "=m")
        (match_operand:XTT32SI 1 "reg_or_cstlreg_operand" "xrxc"))]
  "TARGET_XTT_TENSIX"
  {
    rvtt_mov_error (insn, false);
    return "BADSTORE %x1,%0";
  }
  [(set_attr "type" "tensix")])

(define_insn "*rvtt_load"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (match_operand:XTT32SI 1 "memory_operand" "m"))]
  "TARGET_XTT_TENSIX"
  {
    rvtt_mov_error (insn, true);
    return "BADLOAD %x1,%0";
  }
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpassign"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (match_operand:XTT32SI 1 "reg_or_cstlreg_operand" "xrxc"))]
  "TARGET_XTT_TENSIX"
  "SFPMOV\t%0, %x1, 2"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpassign_lv"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
	  ] UNSPECV_SFPASSIGN))]
  "TARGET_XTT_TENSIX"
  "SFPMOV\t%0, %x2, 0\t# LV:%x1"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfploadi_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr,xr,xr")
        (unspec_volatile:XTT32SI [
          (match_operand:SI    1 "mem_or_0_operand" "J,J,m,m")
          (match_operand:SI    2 "const_int_operand" "J,J,n,n") ;; opcode
          (match_operand:SI    3 "const_int_operand" "J,J,n,n") ;; id, src & dst shifts
          (match_operand:SI    4 "reg_or_const_int_operand" "N16U,N16U,r,r") ;; imm or insn
          (match_operand:XTT32SI 5 "noval_operand" "xn,xn,xn,xn") ;; src (none)
          (match_operand:XTT32SI 6 "reg_or_cstlreg_or_noval_operand" "xn,0,xn,0") ;; lv
          (match_operand:SI    7 "const_int_operand" "N04U,N04U,N04U,N04U")
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
  [(set_attr "type" "tensix")])

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
        = TARGET_XTT_TENSIX_WH
	? TT_OP_WH_SFPLOAD (0, INTVAL (operands[6]), INTVAL (operands[7]), 0)
	: TARGET_XTT_TENSIX_BH
	? TT_OP_BH_SFPLOAD (0, INTVAL (operands[6]), INTVAL (operands[7]), 0)
	: 0;
      opc = GEN_INT (op);
      enc = GEN_INT (rvtt_synth (UINTVAL (operands[5])).dst_shift (20));
      imm = operands[4];
    }
  else
    imm = rvtt_clamp_unsigned (imm, (TARGET_XTT_TENSIX_WH ? 0x3fff
                                   : TARGET_XTT_TENSIX_BH ? 0x1fff
                                   : 0));

  emit_insn (gen_rvtt_sfpload_int
    (operands[0], mem, opc, enc, imm,
     rvtt_gen_rtx_noval (XTT32SImode),
     operands[2], operands[6], operands[7]));
  DONE;
})

(define_insn "rvtt_sfpload_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr,xr,xr")
        (unspec_volatile:XTT32SI [
          (match_operand:SI    1 "mem_or_0_operand" "J,J,m,m")
          (match_operand:SI    2 "const_int_operand" "J,J,n,n") ;; opcode
          (match_operand:SI    3 "const_int_operand" "J,J,n,n") ;; id, src & dst shifts
          (match_operand:SI    4 "reg_or_const_int_operand" "N14U,N14U,r,r") ;; imm or insn
          (match_operand:XTT32SI 5 "noval_operand" "xn,xn,xn,xn") ;; src (none)
          (match_operand:XTT32SI 6 "reg_or_cstlreg_or_noval_operand" "xn,0,xn,0") ;; lv
          (match_operand:SI    7 "const_int_operand" "N04U,N04U,N04U,N04U")
          (match_operand:SI    8 "const_int_operand" "N03U,N03U,N03U,N03U") ;; largest constraint (bh)
          ] UNSPECV_SFPLOAD))
   (clobber (match_scratch:SI  9 "=X,X,&r,&r"))]
  "TARGET_XTT_TENSIX"
  {
    return rvtt_synth::pattern (which_alternative >> 1,
      which_alternative & 1
      ? "SFPLOAD\t%0, %4, %7, %8\t# LV:%x6"
      : "SFPLOAD\t%0, %4, %7, %8",
      operands, true, 9);
  }
  [(set_attr "type" "tensix")])

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
        = TARGET_XTT_TENSIX_WH
	? TT_OP_WH_SFPSTORE (0, INTVAL (operands[5]), INTVAL (operands[6]), 0)
        : TARGET_XTT_TENSIX_BH
	? TT_OP_BH_SFPSTORE (0, INTVAL (operands[5]), INTVAL (operands[6]), 0)
        : 0;
      opc = GEN_INT (op);
      enc = GEN_INT (rvtt_synth (UINTVAL (operands[4])).src_shift (20));
      imm = operands[3];
    }
  else
    imm = rvtt_clamp_unsigned (imm, (TARGET_XTT_TENSIX_WH ? 0x3fff
                                   : TARGET_XTT_TENSIX_BH ? 0x1fff
                                   : 0));

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
    (match_operand:SI    3 "reg_or_const_int_operand" "N14U,r") ;; imm or insn
    (match_operand:XTT32SI 4 "reg_or_cstlreg_operand"  "xrxs,xrxs") ;; src
    (match_operand:SI    5 "const_int_operand" "N04U,N04U")
    (match_operand:SI    6 "const_int_operand" "N03U,N03U") ;; largest constraint (bh)
    ] UNSPECV_SFPSTORE)
   (clobber (match_scratch:SI  7 "=X,&r"))]
  "TARGET_XTT_TENSIX"
  {
    return rvtt_synth::pattern (which_alternative,
      "SFPSTORE\t%x4, %3, %5, %6",
      operands, false, 7);
  }
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpsetcc_i"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI   0 "const_int_operand" "N04U")
     (match_operand:SI   1 "const_int_operand" "N01U")
     ] UNSPECV_SFPSETCC)]
  "TARGET_XTT_TENSIX"
  "SFPSETCC\tL0, %1, %0"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpsetcc_v"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "reg_or_cstlreg_operand"  "xrxc")
     (match_operand:SI   1 "const_int_operand" "N04U")
     ] UNSPECV_SFPSETCC)]
  "TARGET_XTT_TENSIX"
  "SFPSETCC\t%x0, 0, %1"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpencc"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "const_int_operand" "N04U")
     (match_operand:SI    1 "const_int_operand" "N02U")
     ] UNSPECV_SFPENCC)]
  "TARGET_XTT_TENSIX"
  "SFPENCC\t%1, %0"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpcompc"
  [(unspec_volatile:XTT32SI [
     (const_int 0)
     ] UNSPECV_SFPCOMPC)]
  "TARGET_XTT_TENSIX"
  "SFPCOMPC"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfppushc"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "const_int_operand" "N04U")
     ] UNSPECV_SFPPUSHC)]
  "TARGET_XTT_TENSIX"
  "SFPPUSHC\t%0"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfppopc"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "const_int_operand" "N04U")
     ] UNSPECV_SFPPOPC)]
  "TARGET_XTT_TENSIX"
  "SFPPOPC\t%0"
  [(set_attr "type" "tensix")])

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
(define_int_attr rvtt_muladd_ops [
  (UNSPECV_SFPMUL "%x1, %x2, L9")
  (UNSPECV_SFPADD "L10, %x1, %x2")
  ])
(define_int_attr rvtt_muladd_ops_lv [
  (UNSPECV_SFPMUL "%x2, %x3, L9")
  (UNSPECV_SFPADD "L10, %x2, %x3")
  ])

(define_expand "rvtt_sfp<rvtt_muladd_name>"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI    3 "const_int_operand" "N04U")
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
          (match_operand:SI    4 "const_int_operand" "N04U,N04U")
	  ] rvtt_muladd_op))]
  "TARGET_XTT_TENSIX"
  "@
   SFP<rvtt_muladd_insn>\t%0, <rvtt_muladd_ops_lv>, %4
   SFP<rvtt_muladd_insn>\t%0, <rvtt_muladd_ops_lv>, %4\t# LV:%x1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_wh" "dynamic")
   (set_attr "xtt_delay_bh" "dynamic")])

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
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr, xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn,0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc, xrxc")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "xrxc, xrxc")
          (match_operand:XTT32SI 4 "reg_or_cstlreg_operand"  "xrxc, xrxc")
          (match_operand:SI    5 "const_int_operand" "N04U,N04U")
	  ] UNSPECV_SFPMAD))]
  "TARGET_XTT_TENSIX"
  "@
   SFPMAD\t%0, %x2, %x3, %x4, %5
   SFPMAD\t%0, %x2, %x3, %x4, %5\t# LV:%x1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_wh" "dynamic")
   (set_attr "xtt_delay_bh" "dynamic")])

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
  auto mem = const0_rtx;
  auto opc = const0_rtx;
  auto enc = const0_rtx;
  auto imm = operands[3];
  if (!CONST_INT_P (imm))
    {
      mem = gen_rtx_MEM (SImode, operands[1]);
      int op
        = TARGET_XTT_TENSIX_WH
	? TT_OP_WH_SFP<rvtt_muliaddi_insn> (0, 0, INTVAL (operands[6]))
	: TARGET_XTT_TENSIX_BH
	? TT_OP_BH_SFP<rvtt_muliaddi_insn> (0, 0, INTVAL (operands[6]))
	: 0;
      opc = GEN_INT (op);
      enc = GEN_INT (rvtt_synth (UINTVAL (operands[5])).src_shift (4).dst_shift (4));
      imm = operands[4];
    }
  else
    imm = rvtt_clamp_unsigned (imm, 0xffff);

  emit_insn (gen_rvtt_sfp<rvtt_muliaddi_name>_int
    (operands[0], mem, opc, enc, imm,
     operands[2], rvtt_gen_rtx_noval (XTT32SImode), operands[6]));
  DONE;
})

(define_insn "rvtt_sfp<rvtt_muliaddi_name>_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
          (match_operand:SI    1 "mem_or_0_operand" "J,m")
          (match_operand:SI    2 "const_int_operand" "J,n") ;; opcode
          (match_operand:SI    3 "const_int_operand" "J,n") ;; id, src & dst shifts
          (match_operand:SI    4 "reg_or_const_int_operand" "N16U,r") ;; imm or insn
          (match_operand:XTT32SI 5 "reg_or_cstlreg_operand" "0,0") ;; src
          (match_operand:XTT32SI 6 "noval_operand" "xn,xn") ;; lv
          (match_operand:SI    7 "const_int_operand" "N04U,N04U")
          ] rvtt_muliaddi_op))
   (clobber (match_scratch:SI  8 "=X,&r"))]
  "TARGET_XTT_TENSIX"
  {
    return rvtt_synth::pattern (which_alternative,
      "SFP<rvtt_muliaddi_insn>\t%x0, %4, %7",
      operands, true, 8);
  }
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_wh" "dynamic")
   (set_attr "xtt_delay_bh" "dynamic")])

(define_insn "rvtt_sfpiadd_v_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	(match_operand:XTT32SI 1 "register_operand"  "0")
        (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
        (match_operand:SI    3 "const_int_operand" "N04U")
	] UNSPECV_SFPIADD))]
  "TARGET_XTT_TENSIX"
  "SFPIADD\t%0, %x2, 0, %3"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpiadd_i_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr, xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn,0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc, xrxc")
          (match_operand:SI    3 "const_int_operand" "n, n")
          (match_operand:SI    4 "const_int_operand" "N04U,N04U")
	  ] UNSPECV_SFPIADD))]
  "TARGET_XTT_TENSIX"
  "@
   SFPIADD\t%0, %x2, %3, %4
   SFPIADD\t%0, %x2, %3, %4\t# LV:%x1"
  [(set_attr "type" "tensix")])

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

(define_insn "rvtt_sfp<rvtt_unary_name>_lv"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn,0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand" "xrxc,xrxc")
          (match_operand:SI    3 "const_int_operand" "N04U,N04U")
	  ] rvtt_unary_op))]
  "TARGET_XTT_TENSIX"
  "@
   SFP<rvtt_unary_insn>\t%0, %x2, %3
   SFP<rvtt_unary_insn>\t%0, %x2, %3\t# LV:%x1"
  [(set_attr "type" "tensix")])

(define_peephole2
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:SI    3 "const_0_operand")
	  ] UNSPECV_SFPLZ))
   (unspec_volatile:XTT32SI [
     (match_dup:XTT32SI     2)
     (match_operand:SI    4 "const_int_operand")
     ] UNSPECV_SFPSETCC)]
  "TARGET_XTT_TENSIX && (INTVAL (operands[4]) == SFPSETCC_MOD1_LREG_NE0
                                 || INTVAL (operands[4]) == SFPSETCC_MOD1_LREG_EQ0)"
  [(const_int 0)]
{
  rtx mod = GEN_INT (INTVAL (operands[4]) == SFPSETCC_MOD1_LREG_NE0
                     ? SFPLZ_MOD1_CC_NE0 : SFPLZ_MOD1_CC_EQ0);

  emit_insn (gen_rvtt_sfplz_lv (operands[0], operands[1], operands[2], mod));
})

(define_peephole2
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:SI    3 "const_0_operand")
	  ] UNSPECV_SFPLZ))
   (unspec_volatile:XTT32SI [
     (match_operand:SI    4 "const_int_operand")
     ] UNSPECV_SFPPUSHC)
   (unspec_volatile:XTT32SI [
     (match_dup:XTT32SI     2)
     (match_operand:SI    5 "const_int_operand")
     ] UNSPECV_SFPSETCC)]
  "TARGET_XTT_TENSIX && (INTVAL (operands[4]) == SFPSETCC_MOD1_LREG_NE0
                                 || INTVAL (operands[4]) == SFPSETCC_MOD1_LREG_EQ0)"
  [(const_int 0)]
{
  rtx mod = GEN_INT (INTVAL (operands[4]) == SFPSETCC_MOD1_LREG_NE0
                     ? SFPLZ_MOD1_CC_NE0 : SFPLZ_MOD1_CC_EQ0);

  emit_insn (gen_rvtt_sfppushc (operands[4]));
  emit_insn (gen_rvtt_sfplz_lv (operands[0], operands[1], operands[2], mod));
})

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

(define_insn "rvtt_sfpset<rvtt_set_name>_v"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
          (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "xrxc")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "0")
	  ] rvtt_set_op))]
  "TARGET_XTT_TENSIX"
  "SFPSET<rvtt_set_insn>\t%0, %x1, 0, 0"
  [(set_attr "type" "tensix")])

(define_expand "rvtt_sfpset<rvtt_set_name>_i"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "reg_or_0_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_0_operand")
          (match_operand:SI    5 "const_int_operand")
	  ] rvtt_set_op))]
  "TARGET_XTT_TENSIX"
{
  emit_insn (gen_rvtt_sfpset<rvtt_set_name>_i_lv
    (operands[0], operands[1], rvtt_gen_rtx_noval (XTT32SImode),
     operands[2], operands[3], operands[4], operands[5]));
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
	  ] rvtt_set_op))]
  "TARGET_XTT_TENSIX"
{
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
        = TARGET_XTT_TENSIX_WH
	? TT_OP_WH_SFPSET<rvtt_set_insn> (0, 0, 0, 1)
        : TARGET_XTT_TENSIX_BH
	? TT_OP_BH_SFPSET<rvtt_set_insn> (0, 0, 0, 1)
        : 0;
      opc = GEN_INT (op);
      enc = GEN_INT (rvtt_synth (UINTVAL (operands[6])).src_shift (4).dst_shift (8));
      imm = operands[5];
    }
  else
    {
      if (<rvtt_set_op> == UNSPECV_SFPSETMAN
          && INTVAL (imm) >= 4096)
        {
          rtx tmp = gen_reg_rtx (XTT32SImode);
          rvtt_emit_sfpxloadi
            (tmp, operands[2], operands[1],
             GEN_INT (SFPXLOADI_MOD0_UINT32),
             operands[4], const0_rtx, const0_rtx);
          emit_insn (gen_rvtt_sfpset<rvtt_set_name>_v (operands[0], operands[3], tmp));
	  DONE;
        }
      imm = rvtt_clamp_unsigned (imm, 0xfff);
    }

  emit_insn (gen_rvtt_sfpset<rvtt_set_name>_i_int
    (operands[0], mem, opc, enc, imm,
     operands[3], operands[2]));
  DONE;
})

(define_insn "rvtt_sfpset<rvtt_set_name>_i_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr,xr,xr")
        (unspec_volatile:XTT32SI [
          (match_operand:SI    1 "mem_or_0_operand" "J,J,n,m")
          (match_operand:SI    2 "const_int_operand" "J,J,n,n") ;; opcode
          (match_operand:SI    3 "const_int_operand" "J,J,n,n") ;; id, src & dst shifts
          (match_operand:SI    4 "reg_or_const_int_operand" "N12U,N12U,r,r") ;; imm or insn
          (match_operand:XTT32SI 5 "reg_or_cstlreg_operand" "xrxc,xrxc,xrxc,xrxc") ;; src
          (match_operand:XTT32SI 6 "reg_or_cstlreg_or_noval_operand" "xn,0,xn,0") ;; lv
          ] rvtt_set_op))
   (clobber (match_scratch:SI  7 "=X,X,&r,&r"))]
  "TARGET_XTT_TENSIX"
  {
    return rvtt_synth::pattern (which_alternative >> 1,
      which_alternative & 1
      ? "SFPSET<rvtt_set_insn>\t%x0, %x5, %4, 1\t# LV:%x6"
      : "SFPSET<rvtt_set_insn>\t%x0, %x5, %4, 1",
      operands, true, 7);
  }
  [(set_attr "type" "tensix")])

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

;; the inputs are not commutative, because op1 could be a live vqlue
;; that needs the (non-enabled) components propagating to the output.
(define_insn "rvtt_sfp<rvtt_logical_name>"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
	  ] rvtt_logical_op))]
  "TARGET_XTT_TENSIX"
  "SFP<rvtt_logical_insn>\t%0, %x2"
  [(set_attr "type" "tensix")])

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
   SFPNOT\t%0, %x2
   SFPNOT\t%0, %x2\t# LV:%x1"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpshft_v"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand"  "0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI    3 "const_int_operand"  "N04U")
	  ] UNSPECV_SFPSHFT))]
  "TARGET_XTT_TENSIX"
  "SFPSHFT\t%0, %x2, 0, %3"
  [(set_attr "type" "tensix")])

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
  unsigned mod = SFPSHFT_MOD1_SHFT_IMM
      | (TARGET_XTT_TENSIX_BH ? SFPSHFT_MOD1_SRC_LREG_C : 0);
  operands[6] = GEN_INT (INTVAL (operands[6]) | mod);

  auto mem = const0_rtx;
  auto opc = const0_rtx;
  auto enc = const0_rtx;
  auto imm = operands[3];
  if (!CONST_INT_P (imm))
    {
      mem = gen_rtx_MEM (SImode, operands[1]);
      int op
        = TARGET_XTT_TENSIX_WH
	? TT_OP_WH_SFPSHFT (0, 0, 0, INTVAL (operands[6]))
        : TARGET_XTT_TENSIX_BH
	? TT_OP_BH_SFPSHFT (0, 0, 0, INTVAL (operands[6]))
        : 0;
      opc = GEN_INT (op);
      enc = GEN_INT (rvtt_synth (UINTVAL (operands[5]))
                    .src_shift (TARGET_XTT_TENSIX_WH ? 4 : TARGET_XTT_TENSIX_BH ? 8 : 0)
		    .dst_shift (4));
      imm = operands[4];
    }
  else
    imm = rvtt_clamp_signed (imm, 0x7ff);

  emit_insn (gen_rvtt_sfpshft_i_int
    (operands[0], mem, opc, enc, imm,
     operands[2], rvtt_gen_rtx_noval (XTT32SImode),
     operands[6]));
  DONE;
})

(define_expand "rvtt_sfpshft_i_int"
  [(parallel [
     (set (match_operand:XTT32SI 0 "register_operand")
          (unspec_volatile:XTT32SI [
            (match_operand:SI    1 "mem_or_0_operand")
            (match_operand:SI    2 "const_int_operand") ;; opcode
            (match_operand:SI    3 "const_int_operand") ;; id, src & dst shifts
            (match_operand:SI    4 "reg_or_const_int_operand") ;; imm or insn
            (match_operand:XTT32SI 5 "reg_or_cstlreg_operand") ;; src
            (match_operand:XTT32SI 6 "noval_operand") ;; lv
            (match_operand:SI    7 "const_int_operand")
            ] UNSPECV_SFPSHFT))
       (clobber (match_scratch:SI  8))])]
  "TARGET_XTT_TENSIX")

(define_insn "*rvtt_sfpshft_i_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
          (match_operand:SI    1 "mem_or_0_operand" "J,m")
          (match_operand:SI    2 "const_int_operand" "n,n") ;; opcode
          (match_operand:SI    3 "const_int_operand" "n,n") ;; id, src & dst shifts
          (match_operand:SI    4 "reg_or_const_int_operand" "N12S,r") ;; imm or insn
          (match_operand:XTT32SI 5 "reg_or_cstlreg_operand" "xrxc,xrxc") ;; src
          (match_operand:XTT32SI 6 "noval_operand" "xn,xn") ;; lv
          (match_operand:SI    7 "const_int_operand" "N04U,N04U")
	  ] UNSPECV_SFPSHFT))
   (clobber (match_scratch:SI  8 "=X,&r"))]
  "TARGET_XTT_TENSIX_BH"
  {
    return rvtt_synth::pattern (which_alternative,
      "SFPSHFT\t%x0, %x5, %4, %7",
      operands, true, 8);
  }
  [(set_attr "type" "tensix")])

(define_insn "*rvtt_sfpshft_i_wh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
          (match_operand:SI    1 "mem_or_0_operand" "J,m")
          (match_operand:SI    2 "const_int_operand" "n,n") ;; opcode
          (match_operand:SI    3 "const_int_operand" "n,n") ;; id, src & dst shifts
          (match_operand:SI    4 "reg_or_const_int_operand" "N12S,r") ;; imm or insn
          (match_operand:XTT32SI 5 "reg_or_cstlreg_operand" "0,0") ;; src
          (match_operand:XTT32SI 6 "noval_operand" "xn,xn") ;; lv
          (match_operand:SI    7 "const_int_operand" "N04U,N04U")
	  ] UNSPECV_SFPSHFT))
   (clobber (match_scratch:SI  8 "=X,&r"))]
  "TARGET_XTT_TENSIX_WH"
  {
    return rvtt_synth::pattern (which_alternative,
      "SFPSHFT\t%x0, L0, %4, %7",
      operands, true, 8);
  }
  [(set_attr "type" "tensix")])

(define_expand "rvtt_sfpcast"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI    2 "const_int_operand" "N04U")
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
          (match_operand:SI    3 "const_int_operand" "N04U,N04U")
	  ] UNSPECV_SFPCAST))]
  "TARGET_XTT_TENSIX"
  "@
   SFPCAST\t%0, %x2, %3
   SFPCAST\t%0, %x2, %3\t# LV:%x1"
  [(set_attr "type" "tensix")])

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
        = TARGET_XTT_TENSIX_WH
	? TT_OP_BH_SFPDIVP2 (0, 0, 0, INTVAL (operands[7]))
        : TARGET_XTT_TENSIX_BH
	? TT_OP_BH_SFPDIVP2 (0, 0, 0, INTVAL (operands[7]))
        : 0;
      opc = GEN_INT (op);
      enc = GEN_INT (rvtt_synth (UINTVAL (operands[6])).src_shift (4).dst_shift (8));
      imm = operands[5];
    }
  else
    imm = rvtt_clamp_signed (imm, 0x7ff);

  emit_insn (gen_rvtt_sfpdivp2_int
    (operands[0], mem, opc, enc, imm,
     operands[3], operands[2], operands[7]));
  DONE;
})

(define_insn "rvtt_sfpdivp2_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr,xr,xr")
        (unspec_volatile:XTT32SI [
          (match_operand:SI    1 "mem_or_0_operand" "J,J,m,m")
          (match_operand:SI    2 "const_int_operand" "J,J,n,n") ;; opcode
          (match_operand:SI    3 "const_int_operand" "J,J,n,n") ;; id, src & dst shifts
          (match_operand:SI    4 "reg_or_const_int_operand" "N12S,N12S,r,r") ;; imm or insn
          (match_operand:XTT32SI 5 "reg_or_cstlreg_operand" "xrxc,xrxc,xrxc,xrxc") ;; src
          (match_operand:XTT32SI 6 "reg_or_cstlreg_or_noval_operand" "xn,0,xn,0") ;; lv
          (match_operand:SI    7 "const_int_operand" "N04U,N04U,N04U,N04U")
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
  [(set_attr "type" "tensix")])

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
  operands[7] = GEN_INT (INTVAL (operands[7]) | SFPSTOCHRND_MOD1_IMM8);

  auto mem = const0_rtx;
  auto opc = const0_rtx;
  auto enc = const0_rtx;
  auto imm = operands[4];
  if (!CONST_INT_P (imm))
    {
      mem = gen_rtx_MEM (SImode, operands[1]);
      int op
        = TARGET_XTT_TENSIX_WH
	? TT_OP_WH_SFP_STOCH_RND (INTVAL (operands[8]), 0, 0, 0, 0, INTVAL (operands[7]))
        : TARGET_XTT_TENSIX_BH
	? TT_OP_BH_SFP_STOCH_RND (INTVAL (operands[8]), 0, 0, 0, 0, INTVAL (operands[7]))
        : 0;
      opc = GEN_INT (op);
      enc = GEN_INT (rvtt_synth (UINTVAL (operands[6])).src_shift (4).dst_shift (8));
      imm = operands[5];
    }
  else
    imm = rvtt_clamp_unsigned (imm, 0x1f);

  emit_insn (gen_rvtt_sfpstochrnd_i_int
    (operands[0], mem, opc, enc, imm,
     operands[3], operands[2], operands[7], operands[8]));
  DONE;
})

(define_insn "rvtt_sfpstochrnd_i_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr,xr,xr")
        (unspec_volatile:XTT32SI [
          (match_operand:SI    1 "mem_or_0_operand" "J,J,m,m")
          (match_operand:SI    2 "const_int_operand" "J,J,n,n") ;; opcode
          (match_operand:SI    3 "const_int_operand" "J,J,n,n") ;; id, src & dst shifts
          (match_operand:SI    4 "reg_or_const_int_operand" "N05U,N05U,r,r") ;; imm or insn
          (match_operand:XTT32SI 5 "reg_or_cstlreg_operand" "xrxc,xrxc,xrxc,xrxc") ;; src
          (match_operand:XTT32SI 6 "reg_or_cstlreg_or_noval_operand" "xn,0,xn,0") ;; lv
          (match_operand:SI    7 "const_int_operand" "N04U,N04U,N04U,N04U")
          (match_operand:SI    8 "const_int_operand" "N01U,N01U,N01U,N01U")
	  ] UNSPECV_SFPSTOCHRND))
   (clobber (match_scratch:SI 9 "=X,X,&r,&r"))]
  "TARGET_XTT_TENSIX"
  {
    return rvtt_synth::pattern (which_alternative >> 1,
      which_alternative & 1
      ? "SFPSTOCHRND\t%x0, L0, %x5, %7, %8, %4\t# LV:%x6"
      : "SFPSTOCHRND\t%x0, L0, %x5, %7, %8, %4",
      operands, true, 9);
  }
  [(set_attr "type" "tensix")])

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
          (match_operand:SI    4 "const_int_operand" "N04U,N04U")
          (match_operand:SI    5 "const_int_operand" "N01U,N01U")
	  ] UNSPECV_SFPSTOCHRND))]
  "TARGET_XTT_TENSIX"
  "@
   SFPSTOCHRND\t%0, %x3, %x2, %4, %5, 0
   SFPSTOCHRND\t%0, %x3, %x2, %4, %5, 0\t# LV:%x1"
  [(set_attr "type" "tensix")])

(define_expand "rvtt_sfpreadconfig"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "const_int_operand" "N04U")
	  ] UNSPECV_SFPCONFIG))]
  "TARGET_XTT_TENSIX_BH"
  {
    emit_insn (gen_rvtt_sfpreadconfig_lv
      (operands[0], rvtt_gen_rtx_noval (XTT32SImode), operands[1]));
    DONE;
  })

(define_insn "rvtt_sfpreadconfig_lv"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn,0")
          (match_operand:SI 2 "const_int_operand" "N04U,N04U")
	  ] UNSPECV_SFPCONFIG))]
  "TARGET_XTT_TENSIX_BH"
  "@
   SFPMOV\t%0, L%2, 8
   SFPMOV\t%0, L%2, 8\t# LV:%x1"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpwriteconfig_v"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "register_operand"   "x0")
     (match_operand:SI   1 "const_int_operand"  "N04U")
     ] UNSPECV_SFPCONFIG)]
  "TARGET_XTT_TENSIX"
  "SFPCONFIG\t%1, 0, 0"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfplut"
  [(set (match_operand:XTT32SI 0 "register_operand" "=x3")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "x0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "x1")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "x2")
          (match_operand:XTT32SI 4 "reg_or_cstlreg_operand"  "0")
          (match_operand:SI    5 "const_int_operand" "N04U")
	  ] UNSPECV_SFPLUT))]
  "TARGET_XTT_TENSIX"
  "SFPLUT\t%0, %5"
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_wh" "dynamic")
   (set_attr "xtt_delay_bh" "dynamic")])

(define_insn_and_split "rvtt_sfplutfp32_3r"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand"  "x0")
          (match_operand:XTT32SI 2 "register_operand"  "x1")
          (match_operand:XTT32SI 3 "register_operand"  "x2")
          (match_operand:XTT32SI 4 "register_operand"  "x3")
          (match_operand:SI    5 "const_int_operand" "N04U")
	  ] UNSPECV_SFPLUTFP32_3R))
        (clobber (match_scratch:XTT32SI 6 "=x7"))]
  "TARGET_XTT_TENSIX"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  // The dst register is determined by the value in L7,
  // so we need to emit a loadi to L7 first. How pleasant.
  emit_insn (gen_rvtt_sfploadi_int
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
   (set_attr "xtt_delay_wh" "dynamic")
   (set_attr "xtt_delay_bh" "dynamic")])

(define_insn "rvtt_sfplutfp32_3r_split"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand"  "x0")
          (match_operand:XTT32SI 2 "register_operand"  "x1")
          (match_operand:XTT32SI 3 "register_operand"  "x2")
          (match_operand:XTT32SI 4 "register_operand"  "x3")
          (match_operand:SI    5 "const_int_operand" "N04U")
          (match_operand:XTT32SI 6 "register_operand"  "x7")
	  ] UNSPECV_SFPLUTFP32_3R))]
  "TARGET_XTT_TENSIX && reload_completed"
  "SFPLUTFP32\t%0, %5"
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_wh" "dynamic")
   (set_attr "xtt_delay_bh" "dynamic")])

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
          (match_operand:SI    8 "const_int_operand" "N04U")
	  ] UNSPECV_SFPLUTFP32_6R))]
  "TARGET_XTT_TENSIX"
  "SFPLUTFP32\t%0, %8"
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_wh" "dynamic")
   (set_attr "xtt_delay_bh" "dynamic")])

(define_insn "rvtt_sfpswap_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand" "0")
	  (match_operand:XTT32SI 3 "reg_or_cstlreg_operand" "1")
          (match_operand:SI    4 "const_int_operand"  "N04U")
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
   (set_attr "xtt_delay_wh" "static")
   (set_attr "xtt_delay_bh" "static")])

(define_insn "*rvtt_sfpswap_cst1"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand" "0")
	  (match_operand:XTT32SI 2 "cstlreg_operand" "xc")
          (match_operand:SI    3 "const_int_operand"  "N04U")
	  (const_int 1)
	  ] UNSPECV_SFPSWAP))]
  "TARGET_XTT_TENSIX"
  "SFPSWAP\t%x1, %x2, %3"
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_wh" "static")
   (set_attr "xtt_delay_bh" "static")])

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
	  (match_operand:XTT32SI 1 "cstlreg_operand" "xc")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand" "0")
          (match_operand:SI    3 "const_int_operand"  "N04U")
	  (const_int 2)
	  ] UNSPECV_SFPSWAP))]
  "TARGET_XTT_TENSIX"
  "SFPSWAP\t%x1, %x2, %3"
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_wh" "static")
   (set_attr "xtt_delay_bh" "static")])

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
     (match_operand:XTT32SI 0 "cstlreg_operand" "xc")
     (match_operand:XTT32SI 1 "cstlreg_operand" "xc")
     (match_operand:SI    2 "const_int_operand"  "N04U")
     (const_int 3)
  ] UNSPECV_SFPSWAP)]
  "TARGET_XTT_TENSIX"
  "SFPSWAP\t%x0, %x1, %2"
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_wh" "static")
   (set_attr "xtt_delay_bh" "static")])

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
  [(set_attr "type" "tensix")])

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
  [(set_attr "type" "tensix")])

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
  [(set_attr "type" "tensix")])

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
   (set_attr "xtt_delay_wh" "static")
   (set_attr "xtt_delay_bh" "static")])

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
   (set_attr "xtt_delay_wh" "static")
   (set_attr "xtt_delay_bh" "static")])

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
   (set_attr "xtt_delay_wh" "static")
   (set_attr "xtt_delay_bh" "static")])

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
  "TARGET_XTT_TENSIX_BH"
  {
    if (!(INTVAL (operands[3]) & SFPGTLE_MOD1_SET_DEST))
      {
        emit_insn (gen_rvtt_sfp<rvtt_gtle_name>_nv
          (operands[1], operands[2], operands[3]));
        emit_insn (gen_rvtt_sfpassign (operands[0], operands[1]));
        DONE;
      }
  })

(define_insn_and_split "rvtt_sfp<rvtt_gtle_name>_int"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
          (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI    3 "const_int_operand" "N04U")
          ] rvtt_gtle_op))]
  "TARGET_XTT_TENSIX_BH"
  "SFP<rvtt_gtle_insn>\t%x0, %x2, 0, %3"
  "&& bool (find_reg_note (insn, REG_UNUSED, operands[0]))"
  [(unspec_volatile:XTT32SI [
     (match_dup 1)
     (match_dup 2)
     (match_dup 3)
     ] rvtt_gtle_op)]
  {
    operands[3] = GEN_INT
      (INTVAL (operands[3]) & (0xf ^ SFPGTLE_MOD1_SET_DEST));
  }
  [(set_attr "type" "tensix")])

(define_insn_and_split "rvtt_sfp<rvtt_gtle_name>_nv"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "reg_or_cstlreg_operand"  "xrxc")
     (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "xrxc")
     (match_operand:SI    2 "const_int_operand" "N04U")
     ] rvtt_gtle_op)]
  "TARGET_XTT_TENSIX_BH"
  "SFP<rvtt_gtle_insn>\t%x0, %x1, 0, %2"
  "&& !(INTVAL (operands[2]) & 0xb)"
  [(const_int 0)]
  {}
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpmul24"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI    3 "const_int_operand" "N04U")
	  ] UNSPECV_SFPMUL24))]
  "TARGET_XTT_TENSIX_BH"
  "SFPMUL24\t%0, %x1, %x2, %3"
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_bh" "dynamic")])

(define_insn "rvtt_sfpmul24_lv"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand" "0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI    4 "const_int_operand" "N04U")
	  ] UNSPECV_SFPMUL24))]
  "TARGET_XTT_TENSIX_BH"
  "SFPMUL24\t%0, %x2, %x3, %4"
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_bh" "dynamic")])

(define_insn "rvtt_sfparecip"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
          (match_operand:SI    2 "const_int_operand")
	  ] UNSPECV_SFPARECIP))]
  "TARGET_XTT_TENSIX_BH"
  "SFPARECIP\t%0, %x1, %2"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfparecip_lv"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand"  "0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xr")
          (match_operand:SI    3 "const_int_operand" "N04U")
	  ] UNSPECV_SFPARECIP))]
  "TARGET_XTT_TENSIX_BH"
  "SFPARECIP\t%0, %x2, %3"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_ttincrwc"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "const_int_operand" "n")
     (match_operand:SI    1 "const_int_operand" "n")
     (match_operand:SI    2 "const_int_operand" "n")
     (match_operand:SI    3 "const_int_operand" "n")
     ] UNSPECV_TTINCRWC)]
  "TARGET_XTT_TENSIX"
  "TTINCRWC\t%0, %1, %2, %3"
  [(set_attr "type" "tensix")])

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
        = TARGET_XTT_TENSIX_WH
        ? TT_OP_WH_REPLAY (INTVAL (operands[4]), 0,
    	                   INTVAL (operands[5]), INTVAL (operands[6]))
        : TARGET_XTT_TENSIX_BH
        ? TT_OP_BH_REPLAY (INTVAL (operands[4]), 0,
                           INTVAL (operands[5]), INTVAL (operands[6]))
        : 0;
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
     (match_operand:SI    3 "reg_or_const_int_operand" "NP5U,r") ;; imm or insn
     (match_operand:XTT32SI 4 "noval_operand" "xn,xn") ;; src (none)
     (match_operand:SI    5 "const_int_operand"  "N05U,N05U")
     (match_operand:SI    6 "const_int_operand"  "N01U,N01U")
     (match_operand:SI    7 "const_int_operand"  "N01U,N01U")
     ] UNSPECV_TTREPLAY)]
  "TARGET_XTT_TENSIX"
  {
    return rvtt_synth::pattern (which_alternative,
      "TTREPLAY\t%5, %3, %6, %7",
      operands, false, -1);
  }
  [(set_attr "type" "tensix")])
