;; Machine description for Tenstorrent SFPU Intrinsics.
;; Copyright (C) 2011-2020 Free Software Foundation, Inc.
;; Contributed by Ayonam Ray (ayonam@helprack.com)
;; Based on RISC-V target for GNU compiler.

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


(define_c_enum "unspecv" [
  ;; Tenstorrent SFPU unspecs.
  UNSPECV_SFPLOAD
  UNSPECV_SFPLOADI
  UNSPECV_SFPSTORE
  UNSPECV_SFPMUL
  UNSPECV_SFPMAD_VVV
  UNSPECV_SFPMAD_IVV
  UNSPECV_SFPMAD_VIV
  UNSPECV_SFPMAD_VVI
  UNSPECV_SFPMAD_IIV
  UNSPECV_SFPMAD_VII
  UNSPECV_SFPMAD_IVI
  UNSPECV_SFPMAD_III
  UNSPECV_SFPMOV
  UNSPECV_SFPSETCCI
  UNSPECV_SFPSETCCV
  UNSPECV_SFPENCC
  UNSPECV_SFPCOMPC
  UNSPECV_SFPPUSHC
  UNSPECV_SFPPOPC
])

(define_expand "movv64sf"
  [(set (match_operand:V64SF 0 "")
        (match_operand:V64SF 1 ""))]
  ""
{
  if (riscv_legitimize_move (V64SFmode, operands[0], operands[1]))
    DONE;
})

(define_insn "*movv64sf_hardfloat"
  [(set (match_operand:V64SF 0 "nonimmediate_operand" "=x,x,m")
        (match_operand:V64SF 1 "move_operand"         " x,m,x"))]
  "TARGET_SFPU  &&
   (   register_operand (operands[0], V64SFmode)
    || reg_or_0_operand (operands[1], V64SFmode))"
  "@SFPMOV\t%0, %1
    SFPMOV\t%0, %1
    SFPMOV\t%0, %1"
  [(set_attr "move_type" "fmove,fpload,fpstore")
   (set_attr "mode" "V64SF")])

(define_insn "riscv_sfpload"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:SI 1 "immediate_operand" "M")
                          (match_operand:SI 2 "immediate_operand" "N")] UNSPECV_SFPLOAD))]
  "TARGET_SFPU"
  "SFPLOAD\t%0, %1, %2")

(define_insn "riscv_sfploadi"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:SI 1 "immediate_operand" "M")
                          (match_operand:HI 2 "immediate_operand" "N")] UNSPECV_SFPLOADI))]
  "TARGET_SFPU"
  "SFPLOADI\t%0, %1, %2")

(define_insn "riscv_sfpstore"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "x")
                     (match_operand:SI    1 "immediate_operand" "M")
                     (match_operand:SI    2 "immediate_operand" "N")] UNSPECV_SFPSTORE)]
  "TARGET_SFPU"
  "SFPSTORE\t%0, %1, %2")

(define_insn "riscv_sfpmov"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:SI    2 "immediate_operand" "M")] UNSPECV_SFPMOV))]
  "TARGET_SFPU"
  "SFPMOV\t%0, %1, %2")

(define_insn "riscv_sfpmul"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M")] UNSPECV_SFPMUL))]
  "TARGET_SFPU"
  "SFPMUL\t%0, %1, %2, %3")

(define_insn "riscv_sfpmadvvv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:V64SF 3 "register_operand"  "x")
                          (match_operand:SI    4 "immediate_operand" "M")] UNSPECV_SFPMAD_VVV))]
  "TARGET_SFPU"
  "SFPMAD\t%1, %2, %3, %4, %0")

(define_insn "riscv_sfpmadivv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:SI    1 "immediate_operand" "M")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:V64SF 3 "register_operand"  "x")
                          (match_operand:SI    4 "immediate_operand" "M")] UNSPECV_SFPMAD_IVV))]
  "TARGET_SFPU"
  "SFPMAD\t%S1, %2, %3, %4, %0")

(define_insn "riscv_sfpmadviv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:SI    2 "immediate_operand" "M")
                          (match_operand:V64SF 3 "register_operand"  "x")
                          (match_operand:SI    4 "immediate_operand" "M")] UNSPECV_SFPMAD_VIV))]
  "TARGET_SFPU"
  "SFPMAD\t%1, %S2, %3, %4, %0")

(define_insn "riscv_sfpmadvvi"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M")
                          (match_operand:SI    4 "immediate_operand" "M")] UNSPECV_SFPMAD_VVI))]
  "TARGET_SFPU"
  "SFPMAD\t%1, %2, %S3, %4, %0")

(define_insn "riscv_sfpmadiiv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:SI    1 "immediate_operand" "M")
                          (match_operand:SI    2 "immediate_operand" "M")
                          (match_operand:V64SF 3 "register_operand"  "x")
                          (match_operand:SI    4 "immediate_operand" "M")] UNSPECV_SFPMAD_IIV))]
  "TARGET_SFPU"
  "SFPMAD\t%S1, %S2, %3, %4, %0")

(define_insn "riscv_sfpmadvii"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:SI    2 "immediate_operand" "M")
                          (match_operand:SI    3 "immediate_operand" "M")
                          (match_operand:SI    4 "immediate_operand" "M")] UNSPECV_SFPMAD_VII))]
  "TARGET_SFPU"
  "SFPMAD\t%1, %S2, %S3, %4, %0")

(define_insn "riscv_sfpmadivi"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:SI    1 "immediate_operand" "M")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M")
                          (match_operand:SI    4 "immediate_operand" "M")] UNSPECV_SFPMAD_IVI))]
  "TARGET_SFPU"
  "SFPMAD\t%S1, %2, %S3, %4, %0")

(define_insn "riscv_sfpmadiii"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:SI    1 "immediate_operand" "M")
                          (match_operand:SI    2 "immediate_operand" "M")
                          (match_operand:SI    3 "immediate_operand" "M")
                          (match_operand:SI    4 "immediate_operand" "M")] UNSPECV_SFPMAD_III))]
  "TARGET_SFPU"
  "SFPMAD\t%S1, %S2, %S3, %4, %0")

(define_insn "riscv_sfpsetcci"
  [(unspec_volatile [(match_operand:SI    0 "immediate_operand" "O")
                     (match_operand:SI    1 "immediate_operand" "M")] UNSPECV_SFPSETCCI)]
  "TARGET_SFPU"
  "SFPSETCC\t%0, L0, %2")

(define_insn "riscv_sfpsetccv"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "x")
                     (match_operand:SI    1 "immediate_operand" "M")] UNSPECV_SFPSETCCV)]
  "TARGET_SFPU"
  "SFPSETCC\t0, %1, %2")

(define_insn "riscv_sfpencc"
  [(unspec_volatile [(match_operand:SI 0 "immediate_operand" "O")
                     (match_operand:SI 1 "immediate_operand" "M")] UNSPECV_SFPENCC)]
  "TARGET_SFPU"
  "SFPENCC\t%0, %1")

(define_insn "riscv_sfpcompc"
  [(unspec_volatile [(const_int 0)] UNSPECV_SFPCOMPC)]
  "TARGET_SFPU"
  "SFPCOMPC")

(define_insn "riscv_sfppushc"
  [(unspec_volatile [(const_int 0)] UNSPECV_SFPPUSHC)]
  "TARGET_SFPU"
  "SFPPUSHC")

(define_insn "riscv_sfppopc"
  [(unspec_volatile [(const_int 0)] UNSPECV_SFPPOPC)]
  "TARGET_SFPU"
  "SFPPOPC")

