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

(define_mode_iterator RVTT_ANY_INT [SI HI QI])
(define_mode_attr rvtt_any_int_mode_name [(SI "si") (HI "hi") (QI "qi")])
(define_mode_attr rvtt_any_int_mode_mnem [(SI "w") (HI "h") (QI "b")])
(define_mode_attr rvtt_any_uint_mode_load_mod [(SI "") (HI "u") (QI "u")])

(define_c_enum "unspec" [
  UNSPEC_SYNTH_OPCODE
])

(define_c_enum "unspecv" [
  ;; Tenstorrent SFPU unspecs.
  ;; INT for internal
  ;; IMM for immediate
  ;; LV for keep dst reg alive as input for predicated liveness

  UNSPECV_SFPSYNTH_INSN

  UNSPECV_SFPASSIGNLREG
  UNSPECV_SFPASSIGNLREG_INT

  UNSPECV_SFPPRESERVELREG

  UNSPECV_SFPNOP

  UNSPECV_TTINCRWC
  UNSPECV_TTREPLAY
])

(define_expand "movv64sf"
  [(set (match_operand:V64SF 0 "")
        (match_operand:V64SF 1 ""))]
  ""
{
  if (riscv_legitimize_move (V64SFmode, operands[0], operands[1]))
    DONE;
})

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
         (unspec:SI [(match_operand:SI   1 "const_int_operand" "n")
	             (match_operand:SI   2 "const_int_operand" "n")] UNSPEC_SYNTH_OPCODE))]
  "TARGET_RVTT"
{
  static char pattern[32];
  unsigned pos = 0;

  pos += snprintf (&pattern[pos], sizeof (pattern) - pos,
		   "li\t%%0, %%1\t# %d:%x", unsigned (INTVAL (operands[2])),
		   unsigned (INTVAL (operands[1])));
  gcc_assert (pos < sizeof (pattern));

  return pattern;
})

;; Name the operands, there are too many(!), sadly we can't use them
;; everywhere (so there are asserts as needed).

(define_c_enum "synth_ops" [
  SYNTH_mem
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
  [(set (match_operand:V64SF 7 "register_operand" "=xr,xr,xr,xr") ; result
        (unspec_volatile:V64SF [(match_operand:SI    0 "memory_operand"   "m,m,m,m") ; instrn_buffer
                                (match_operand:SI    1 "const_int_operand" "n,n,n,n") ; flags
                                (match_operand:SI    2 "register_operand"  "r,r,r,r") ; synth'd insn
                                (match_operand:SI    3 "const_int_operand" "n,n,n,n") ; cst opcode
                                (match_operand:SI    4 "const_int_operand" "n,n,n,n") ; id
                                (match_operand:V64SF 5 "reg_or_vec0_operand" "xr,xn,xr,xn") ; src
                                (match_operand:SI    6 "const_int_operand" "n,n,n,n") ; src shift
                                (match_operand:SI    8 "const_int_operand" "n,n,n,n") ; dst shift
                                (match_operand:V64SF 9 "reg_or_vec0_operand" "7,7,xn,xn") ; lv
                               ] UNSPECV_SFPSYNTH_INSN))
   (clobber (match_scratch:SI 10 "=&r,&r,&r,&r"))]
  "TARGET_RVTT"
{
  return rvtt_synth_insn_pattern (operands, 10);
})

(define_insn "rvtt_sfpsynth_insn"
  [(unspec_volatile [(match_operand:SI    0 "memory_operand"    "m,m") ; instrn_buffer
                     (match_operand:SI    1 "const_int_operand" "n,n") ; flags
                     (match_operand:SI    2 "register_operand"  "r,r") ; synth'd insn
                     (match_operand:SI    3 "const_int_operand" "n,n") ; cst opcode
                     (match_operand:SI    4 "const_int_operand" "n,n") ; id
	             (match_operand:V64SF 5 "reg_or_vec0_operand" "xr,xn") ; src
                     (match_operand:SI    6 "const_int_operand" "n,n") ; src shift
                    ] UNSPECV_SFPSYNTH_INSN)
   (clobber (match_scratch:SI 7 "=&r, X"))]
  "TARGET_RVTT"
{
  return rvtt_synth_insn_pattern (operands, 7);
})

(define_expand "rvtt_sfpassignlreg"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI 1 "const_int_operand" "N04U")] UNSPECV_SFPASSIGNLREG))]
  "TARGET_RVTT"
{
  rvtt_emit_sfpassignlreg(operands[0], operands[1]);
  DONE;
})

(define_insn "rvtt_sfpassignlreg_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile:V64SF [(const_int 0)] UNSPECV_SFPASSIGNLREG_INT))]
  "TARGET_RVTT"
  "")

(define_expand "rvtt_sfppreservelreg"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "")
                     (match_operand:SI    1 "const_int_operand" "N04U")] UNSPECV_SFPPRESERVELREG)]
  "TARGET_RVTT")

(define_int_iterator rvtt_preservelreg [0 1 2 3 4 5 6 7])
;; We have to map the number to a string.
(define_int_attr rvtt_preservelreg_value
  [(0 "0") (1 "1") (2 "2") (3 "3") (4 "4") (5 "5") (6 "6") (7 "7")])
(define_insn "rvtt_sfppreservelreg<rvtt_preservelreg_value>"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand" "x<rvtt_preservelreg_value>")
                     (const_int rvtt_preservelreg)] UNSPECV_SFPPRESERVELREG)]
  "TARGET_RVTT"
  "" ;"; preserve %0"
  [(set_attr "length" "0")])

;; These builtins are converted by gimple passes, but the insns are still
;; needed due to the way we expand them.

(define_expand "rvtt_sfpxicmps"
  [(set (match_operand:SI 0 "register_operand" "")
        (unspec [(match_operand:SI    1 "address_operand"   "")
                 (match_operand:V64SF 2 "register_operand"  "")
                 (match_operand:SI    3 "reg_or_const_int_operand" "")
                 (match_operand:SI    4 "reg_or_0_operand" "")
                 (match_operand:SI    5 "const_int_operand" "")
                 (match_operand:SI    6 "const_int_operand" "")] 0))]
  "TARGET_RVTT"
{
  DONE;
})

(define_expand "rvtt_sfpxicmpv"
  [(set (match_operand:SI 0 "register_operand" "")
        (unspec [(match_operand:V64SF 1 "register_operand"  "")
                 (match_operand:V64SF 2 "register_operand"  "")
                 (match_operand:SI    3 "const_int_operand" "")] 0))]
  "TARGET_RVTT"
{
  DONE;
})

(define_expand "rvtt_sfpxvif"
  [(set (match_operand:SI 0 "register_operand" "")
        (unspec [(const_int 0)] 0))]
  "TARGET_RVTT"
{
  DONE;
})

(define_expand "rvtt_sfpxbool"
  [(set (match_operand:SI 0 "register_operand" "")
        (unspec [(match_operand:SI 1 "register_operand"  "")] 0))]
  "TARGET_RVTT"
{
  DONE;
})

(define_expand "rvtt_sfpxcondb"
  [(unspec [(match_operand:SI 0 "register_operand"  "")
            (match_operand:SI 1 "register_operand"  "")] 0)]
  "TARGET_RVTT"
{
  DONE;
})

(define_expand "rvtt_sfpxcondi"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec [(match_operand:SI 1 "register_operand"  "")] 0))]
  "TARGET_RVTT"
{
  DONE;
})

(define_insn "rvtt_sfpnop"
  [(unspec_volatile [(const_int 0)] UNSPECV_SFPNOP)]
  "TARGET_RVTT"
  "SFPNOP")

(define_insn "rvtt_ttincrwc"
  [(unspec_volatile [(match_operand:SI    0 "const_int_operand" "n")
                     (match_operand:SI    1 "const_int_operand" "n")
                     (match_operand:SI    2 "const_int_operand" "n")
                     (match_operand:SI    3 "const_int_operand" "n")] UNSPECV_TTINCRWC)]
  "TARGET_RVTT"
  "TTINCRWC\t%0, %1, %2, %3")

(define_insn "rvtt_ttreplay"
  [(unspec_volatile [(match_operand:SI    0 "const_int_operand"  "N05U")
                     (match_operand:SI    1 "const_int_operand"  "NP5U")
                     (match_operand:SI    2 "const_int_operand"  "N01U")
                     (match_operand:SI    3 "const_int_operand"  "N01U")] UNSPECV_TTREPLAY)]
  "TARGET_RVTT"
  "TTREPLAY\t%0, %1, %2, %3")
