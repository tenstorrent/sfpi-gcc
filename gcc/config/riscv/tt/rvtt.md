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

  UNSPECV_SFPSYNTH_INSN
  UNSPECV_SFPSYNTH_STORE_INSN

  UNSPECV_SFPVARLREG

  UNSPECV_SFPNOP
  UNSPECV_SFPASSIGN

  UNSPECV_SFPSETCC
  UNSPECV_SFPENCC
  UNSPECV_SFPCOMPC
  UNSPECV_SFPPUSHC
  UNSPECV_SFPPOPC

  UNSPECV_SFPMOV
  UNSPECV_SFPEXEXP
  UNSPECV_SFPEXMAN
  UNSPECV_SFPABS
  UNSPECV_SFPLZ
  UNSPECV_SFPAND
  UNSPECV_SFPOR
  UNSPECV_SFPXOR
  UNSPECV_SFPNOT
  UNSPECV_SFPSHFT

  UNSPECV_SFPLUT
  UNSPECV_SFPLUTFP32_3R
  UNSPECV_SFPLUTFP32_6R

  UNSPECV_SFPSWAP
  UNSPECV_SFPTRANSP
  UNSPECV_SFPSHFT2_COPY4
  UNSPECV_SFPSHFT2_SUBVEC_COPY4
  UNSPECV_SFPSHFT2_SUBVEC_SHFL1_COPY4
  UNSPECV_SFPSHFT2_SUBVEC_SHFL1

  UNSPECV_TTINCRWC
  UNSPECV_TTREPLAY
])

(define_enum "xtt_delay" [
  none
  static
  dynamic
  operand
])
(define_enum_attr "xtt_delay_wh" "xtt_delay"
  (const_string "none"))
(define_enum_attr "xtt_delay_bh" "xtt_delay"
  (const_string "none"))

;; rvtt_synth_opcode and rvtt_sfpsynth_insn{,_dst} are used to
;; synthesize sfp/tt instructions that are injected into the
;; instruction stream.  rvtt_synth_opcode is tied to 1 or more
;; rvtt_sfpsynth_insn{,_dst} insns (unrolling can do that). The ID
;; does that (SSA DEP-USE chains are insufficient as we need to
;; prevent CSE merging unrelated synth_opcode builtin calls). The
;; first src operand is later replaced with the constant parts of the
;; instruction encoding (once register allocation has
;; happened). Because different rvtt_sfpsynth_insns might have
;; different register uses (but mostly don't) we need to check that
;; the registers are still consistent and fixup if not at code
;; emission time.  The rvtt_sfpsynth_insn's use const0_vec for
;; non-used srcs and/or dsts.

(define_insn "rvtt_synth_opcode"
  [(set (match_operand:SI 0 "register_operand" "=r")
        (unspec:SI [
	  (match_operand:SI   1 "const_int_operand" "n")
          (match_operand:SI   2 "const_int_operand" "n")
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

;; Name the operands, there are too many(!), sadly we can't use them
;; everywhere (so there are asserts as needed).

(define_c_enum "synth_ops" [
  SYNTH_mem
  SYNTH_icode
  SYNTH_flags
  SYNTH_synthed
  SYNTH_opcode
  SYNTH_id
  SYNTH_src
  SYNTH_src_shift
  SYNTH_dst ;; only in _dst
  SYNTH_dst_shift
  SYNTH_lv
  ])
(define_insn "rvtt_sfpsynth_insn_dst"
  [(set (match_operand:XTT32SI 8 "register_operand" "=xr,xr,xr,xr") ; result
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    0 "memory_operand"   "m,m,m,m") ; instrn_buffer
          (match_operand:SI    1 "const_int_operand" "n,n,n,n") ; CODE_FOR_
          (match_operand:SI    2 "const_int_operand" "n,n,n,n") ; flags
          (match_operand:SI    3 "register_operand"  "r,r,r,r") ; synth'd insn
          (match_operand:SI    4 "const_int_operand" "n,n,n,n") ; cst opcode
          (match_operand:SI    5 "const_int_operand" "n,n,n,n") ; id
          (match_operand:XTT32SI 6 "reg_or_cstlreg_or_noval_operand" "xrxc,xn,xrxc,xn") ; src
          (match_operand:SI    7 "const_int_operand" "n,n,n,n") ; src shift
          (match_operand:SI    9 "const_int_operand" "n,n,n,n") ; dst shift
          (match_operand:XTT32SI 10 "reg_or_cstlreg_or_noval_operand" "8,8,xn,xn") ; lv
          ] UNSPECV_SFPSYNTH_INSN))
   (clobber (match_scratch:SI 11 "=&r,&r,&r,&r"))]
  "TARGET_XTT_TENSIX"
{
  gcc_assert (SYNTH_lv + 1 == 11);
  return rvtt_synth_insn_pattern (operands, 11);
}
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_wh" "operand")
   (set_attr "xtt_delay_bh" "operand")])

(define_insn "rvtt_sfpsynth_insn"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "memory_operand"    "m,m") ; instrn_buffer
     (match_operand:SI    1 "const_int_operand" "n,n") ; CODE_FOR_
     (match_operand:SI    2 "const_int_operand" "n,n") ; flags
     (match_operand:SI    3 "register_operand"  "r,r") ; synth'd insn
     (match_operand:SI    4 "const_int_operand" "n,n") ; cst opcode
     (match_operand:SI    5 "const_int_operand" "n,n") ; id
     (match_operand:XTT32SI 6 "reg_or_cstlreg_or_noval_operand" "xrxc,xn") ; src
     (match_operand:SI    7 "const_int_operand" "n,n") ; src shift
     ] UNSPECV_SFPSYNTH_INSN)
   (clobber (match_scratch:SI 8 "=&r, X"))]
  "TARGET_XTT_TENSIX"
{
  gcc_assert (SYNTH_dst == 8);
  return rvtt_synth_insn_pattern (operands, 8);
}
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_wh" "operand")
   (set_attr "xtt_delay_bh" "operand")])

(define_insn "rvtt_sfpsynth_store_insn"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "memory_operand"    "m") ; instrn_buffer
     (match_operand:SI    1 "const_int_operand" "n") ; CODE_FOR_
     (match_operand:SI    2 "const_int_operand" "n") ; flags
     (match_operand:SI    3 "register_operand"  "r") ; synth'd insn
     (match_operand:SI    4 "const_int_operand" "n") ; cst opcode
     (match_operand:SI    5 "const_int_operand" "n") ; id
     (match_operand:XTT32SI 6 "register_operand" "xrxs") ; src
     (match_operand:SI    7 "const_int_operand" "n") ; src shift
     ] UNSPECV_SFPSYNTH_STORE_INSN)
   (clobber (match_scratch:SI 8 "=&r"))]
  "TARGET_XTT_TENSIX"
{
  gcc_assert (SYNTH_dst == 8);
  return rvtt_synth_insn_pattern (operands, 8);
}
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_wh" "operand")
   (set_attr "xtt_delay_bh" "operand")])

(define_expand "rvtt_sfpreadlreg"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [(match_operand:SI 1 "const_int_operand")] UNSPECV_SFPVARLREG))]
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
     (match_operand:SI    1 "const_int_operand")
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
        (unspec:XTT32SI [(const_int 0)] UNSPEC_SFPNOVAL))]
  "TARGET_XTT_TENSIX")

;; These builtins are converted by gimple passes, but the insns are still
;; needed due to the way we expand them.

(define_expand "rvtt_sfpxicmps"
  [(set (match_operand:SI 0 "register_operand")
        (unspec:SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_0_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] 0))]
  "TARGET_XTT_TENSIX"
{
  DONE;
})

(define_expand "rvtt_sfpxicmpv"
  [(set (match_operand:SI 0 "register_operand")
        (unspec:SI [
	  (match_operand:XTT32SI 1 "register_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:SI    3 "const_int_operand")
	  ] 0))]
  "TARGET_XTT_TENSIX"
{
  DONE;
})

(define_expand "rvtt_sfpxvif"
  [(set (match_operand:SI 0 "register_operand")
        (unspec:SI [
	  (const_int 0)
	  ] 0))]
  "TARGET_XTT_TENSIX"
{
  DONE;
})

(define_expand "rvtt_sfpxbool"
  [(set (match_operand:SI 0 "register_operand")
        (unspec:SI [
	(match_operand:SI 1 "register_operand")
	] 0))]
  "TARGET_XTT_TENSIX"
{
  DONE;
})

(define_expand "rvtt_sfpxcondb"
  [(unspec:SI [
     (match_operand:SI 0 "register_operand")
     (match_operand:SI 1 "register_operand")
     ] 0)]
  "TARGET_XTT_TENSIX"
{
  DONE;
})

(define_expand "rvtt_sfpxcondi"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec:SI [
	  (match_operand:SI 1 "register_operand")
	  ] 0))]
  "TARGET_XTT_TENSIX"
{
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

(define_insn "rvtt_sfpmovwhole"
  [(set (match_operand:XTT32SI 0 "nonimmediate_operand" "=xr,xr,m")
        (match_operand:XTT32SI 1 "nonimmediate_or_cstlreg_operand" " xrxc,m,xrxc"))]
  "TARGET_XTT_TENSIX
   && (register_operand (operands[0], XTT32SImode)
       || reg_or_cstlreg_operand (operands[1], XTT32SImode))"
  {
    if (!which_alternative)
      return "SFPMOV\t%0, %x1, 2";

    rvtt_mov_error (insn, which_alternative == 1);
    gcc_unreachable ();
  }
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpassign_lv"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
	  ] UNSPECV_SFPASSIGN))]
  "TARGET_XTT_TENSIX"
  "SFPMOV\t%0, %x2, 0"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpsetcc_i"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "const_int_operand" "N01U")
     (match_operand:SI    1 "const_int_operand" "N04U")
     ] UNSPECV_SFPSETCC)]
  "TARGET_XTT_TENSIX"
  "SFPSETCC\tL0, %0, %1"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpsetcc_v"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "reg_or_cstlreg_operand"  "xrxc")
     (match_operand:SI    1 "const_int_operand" "N04U")
     ] UNSPECV_SFPSETCC)]
  "TARGET_XTT_TENSIX"
  "SFPSETCC\t%x0, 0, %1"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpencc"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand" "N02U")
     (match_operand:SI 1 "const_int_operand" "N04U")
     ] UNSPECV_SFPENCC)]
  "TARGET_XTT_TENSIX"
  "SFPENCC\t%0, %1"
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
     (match_operand:SI 0 "const_int_operand" "N04U")
     ] UNSPECV_SFPPUSHC)]
  "TARGET_XTT_TENSIX"
  "SFPPUSHC\t%0"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfppopc"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand" "N04U")
     ] UNSPECV_SFPPOPC)]
  "TARGET_XTT_TENSIX"
  "SFPPOPC\t%0"
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
  "SFP<rvtt_unary_insn>\t%0, %x2, %3"
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
  "SFPNOT\t%0, %x2"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpshft_v"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand"  "0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI 3 "const_int_operand"  "N04U")
	  ] UNSPECV_SFPSHFT))]
  "TARGET_XTT_TENSIX"
  "SFPSHFT\t%0, %x2, 0, %3"
  [(set_attr "type" "tensix")])

(define_expand "rvtt_sfpshft_i"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_0_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] UNSPECV_SFPSHFT))]
  "TARGET_XTT_TENSIX"
{
  rtx insn;
  if (CONST_INT_P (operands[3]))
    insn = (TARGET_XTT_TENSIX_WH ? gen_rvtt_sfpshft_i_wh
            : TARGET_XTT_TENSIX_BH ? gen_rvtt_sfpshft_i_bh
	    : (gcc_unreachable (), nullptr))
    	 (operands[0], operands[2], rvtt_clamp_signed (operands[3], 0x7FF), operands[6]);
  else {
    unsigned op = (TARGET_XTT_TENSIX_WH ? TT_OP_WH_SFPSHFT(0, 0, 0, INTVAL (operands[6]) | 1)
                   : TARGET_XTT_TENSIX_BH ? TT_OP_BH_SFPSHFT(0, 0, 0, INTVAL (operands[6]) | 5)
		   : (gcc_unreachable (), 0));
    insn = rvtt_sfpsynth_insn_dst
         (operands[1], (TARGET_XTT_TENSIX_WH ? CODE_FOR_rvtt_sfpshft_i_wh
	                : CODE_FOR_rvtt_sfpshft_i_bh),
          0, operands[4], op, operands[5], operands[2], 8, operands[0], 4, nullptr);
  }
  emit_insn (insn);
  DONE;
})

(define_insn "rvtt_sfpshft_i_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI    2 "const_int_operand" "N12S")
          (match_operand:SI    3 "const_int_operand" "N04U")
	  ] UNSPECV_SFPSHFT))]
  "TARGET_XTT_TENSIX_BH"
  "SFPSHFT\t%0, %x1, %2, %3 | 5"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpshft_i_wh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "0")
          (match_operand:SI    2 "const_int_operand" "N12S")
          (match_operand:SI    3 "const_int_operand" "N04U")
	  ] UNSPECV_SFPSHFT))]
  "TARGET_XTT_TENSIX_WH"
  "SFPSHFT\t%0, L0, %2, %3 | 1"
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
  // so we need to emit a loadi to L7 first.
  emit_insn (
    // Although ugly, this should be going away.
    (TARGET_XTT_TENSIX_WH ? gen_rvtt_sfploadi_int_wh : gen_rvtt_sfploadi_int_bh)
    (operands[6], rvtt_gen_rtx_noval (XTT32SImode),
     GEN_INT (SFPLOADI_MOD0_USHORT),
     GEN_INT (REGNO (operands[0]) - SFPU_REG_FIRST)));
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
	  (match_operand:SI 7 "const_int_operand" "n")
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
	  (match_operand:SI 4 "const_int_operand")
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
	  (match_operand:SI 8 "const_int_operand" "n")
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
	  (match_operand:SI 5 "const_int_operand")
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
	  (match_operand:SI 8 "const_int_operand" "n")
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
	  (match_operand:SI 5 "const_int_operand")
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
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "0,xn")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand" "xrxc,xrxc")
	  (match_operand:SI 3 "const_int_operand" "n,n")
	  ] UNSPECV_SFPSHFT2_SUBVEC_SHFL1))]
  "TARGET_XTT_TENSIX"
  "SFPSHFT2\t%x0, %x2, 0, %3"
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_wh" "static")
   (set_attr "xtt_delay_bh" "static")])

(define_insn "rvtt_sfpshft2_subvec_shfl1_dead"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "reg_or_cstlreg_or_noval_operand" "xrxc,xn")
     (match_operand:XTT32SI 1 "reg_or_cstlreg_operand" "xrxc,xrxc")
     (match_operand:SI 2 "const_int_operand" "n,n")
     ] UNSPECV_SFPSHFT2_SUBVEC_SHFL1)]
  "TARGET_XTT_TENSIX"
  "SFPSHFT2\tL8, %x1, 0, %2"
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_wh" "static")
   (set_attr "xtt_delay_bh" "static")])

(define_split
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
	  (match_operand:SI 3 "const_int_operand")
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
	  (match_operand:SI 2 "const_int_operand")
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
	  (match_operand:SI 3 "const_int_operand")
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
     (match_operand:SI    0 "address_operand")
     (match_operand:SI    1 "reg_or_const_int_operand")
     (match_operand:SI    2 "reg_or_0_operand")
     (match_operand:SI    3 "const_int_operand")
     (match_operand:SI    4 "const_int_operand")
     (match_operand:SI    5 "const_int_operand")
     (match_operand:SI    6 "const_int_operand")
     ] UNSPECV_TTREPLAY)]
  "TARGET_XTT_TENSIX"
{
  rtx insn = nullptr;
  if (CONST_INT_P (operands[1]))
    insn = gen_rvtt_ttreplay_int (operands[4], operands[1], operands[5], operands[6]);
  else
    {
      unsigned op
          = TARGET_XTT_TENSIX_WH ? TT_OP_WH_REPLAY (INTVAL (operands[4]), 0,
	    		     		      INTVAL (operands[5]), INTVAL (operands[6]))
          : TARGET_XTT_TENSIX_BH ? TT_OP_BH_REPLAY (INTVAL (operands[4]), 0,
	    		     		      INTVAL (operands[5]), INTVAL (operands[6]))
	  : (gcc_unreachable (), 0);
      insn = rvtt_sfpsynth_insn (operands[0], CODE_FOR_rvtt_ttreplay_int,
      	     			 0, operands[2], op, operands[3]);
    }
  emit_insn (insn);
  DONE;
})

(define_insn "rvtt_ttreplay_int"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "const_int_operand"  "N05U")
     (match_operand:SI    1 "const_int_operand"  "NP5U")
     (match_operand:SI    2 "const_int_operand"  "N01U")
     (match_operand:SI    3 "const_int_operand"  "N01U")
     ] UNSPECV_TTREPLAY)]
  "TARGET_XTT_TENSIX"
  "TTREPLAY\t%0, %1, %2, %3"
  [(set_attr "type" "tensix")])

(include "tt/rvtt-peephole.md")
