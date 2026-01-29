;; Machine description for Tenstorrent SFPU Intrinsics.
;; Copyright (C) 2022-2025 Tenstorrent Inc.
;; Originated by Paul Keller (pkeller@tenstorrent.com)

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
  UNSPECV_SFPSWAP
  UNSPECV_SFPTRANSP

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

(define_insn "*rvtt_sfpswap_cst0"
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
  [(parallel [
    (set (match_dup 4)
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI 3 "const_int_operand")
	  ] UNSPECV_SFPSWAP))
    (set (match_dup 5)
        (unspec_volatile:XTT32SI [
	  (match_dup 1)
	  (match_dup 2)
          (match_dup 3)
	  ] UNSPECV_SFPSWAP))])
  (set (match_operand:XTT64SI 0 "register_operand")
     (unspec:XTT64SI [
       (match_dup 4)
       (match_dup 5)
       ] UNSPEC_SFPCLEAVE))]
  "TARGET_XTT_TENSIX"
{
  operands[4] = gen_reg_rtx (XTT32SImode);
  operands[5] = gen_reg_rtx (XTT32SImode);
})

(define_insn "*rvtt_sfptransp"
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
  [(parallel [
    (set (match_dup 5)
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
	  (match_operand:XTT32SI 4 "reg_or_cstlreg_operand")
	  ] UNSPECV_SFPTRANSP))
    (set (match_dup 6)
        (unspec_volatile:XTT32SI [
	  (match_dup 1)
	  (match_dup 2)
          (match_dup 3)
          (match_dup 4)
	  ] UNSPECV_SFPTRANSP))
    (set (match_dup 7)
        (unspec_volatile:XTT32SI [
	  (match_dup 1)
	  (match_dup 2)
          (match_dup 3)
          (match_dup 4)
	  ] UNSPECV_SFPTRANSP))
    (set (match_dup 8)
        (unspec_volatile:XTT32SI [
	  (match_dup 1)
	  (match_dup 2)
          (match_dup 3)
          (match_dup 4)
	  ] UNSPECV_SFPTRANSP))])
  (set (match_operand:XTT128SI 0 "register_operand")
     (unspec:XTT128SI [
       (match_dup 5)
       (match_dup 6)
       (match_dup 7)
       (match_dup 8)
       ] UNSPEC_SFPCLEAVE))]
  "TARGET_XTT_TENSIX"
{
  operands[5] = gen_reg_rtx (XTT32SImode);
  operands[6] = gen_reg_rtx (XTT32SImode);
  operands[7] = gen_reg_rtx (XTT32SImode);
  operands[8] = gen_reg_rtx (XTT32SImode);
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
