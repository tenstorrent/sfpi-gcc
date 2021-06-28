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
  UNSPECV_SFPMULI
  UNSPECV_SFPADDI
  UNSPECV_SFPMUL
  UNSPECV_SFPADD
  UNSPECV_SFPIADDV
  UNSPECV_SFPIADDI
  UNSPECV_SFPSHFTV
  UNSPECV_SFPSHFTI
  UNSPECV_SFPABS
  UNSPECV_SFPAND
  UNSPECV_SFPOR
  UNSPECV_SFPNOT
  UNSPECV_SFPLZ
  UNSPECV_SFPSETMANV
  UNSPECV_SFPSETMANI
  UNSPECV_SFPSETEXPV
  UNSPECV_SFPSETEXPI
  UNSPECV_SFPSETSGNV
  UNSPECV_SFPSETSGNI
  UNSPECV_SFPMAD_VVV
  UNSPECV_SFPMAD_RVV
  UNSPECV_SFPMAD_VRV
  UNSPECV_SFPMAD_VVR
  UNSPECV_SFPMAD_RRV
  UNSPECV_SFPMAD_VRR
  UNSPECV_SFPMAD_RVR
  UNSPECV_SFPMAD_RRR
  UNSPECV_SFPMOV
  UNSPECV_SFPDIVP2
  UNSPECV_SFPEXEXP
  UNSPECV_SFPEXMAN
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

(define_insn "riscv_sfpmuli"
  [(unspec_volatile [(match_operand:SI    0 "immediate_operand" "N")
	                   (match_operand:V64SF 1 "register_operand"  "x")
                     (match_operand:SI    2 "immediate_operand" "M")] UNSPECV_SFPMULI)]
  "TARGET_SFPU"
  "SFPMULI\t%0, %1, %2")

(define_insn "riscv_sfpaddi"
  [(unspec_volatile [(match_operand:SI    0 "immediate_operand" "N")
	                   (match_operand:V64SF 1 "register_operand"  "x")
                     (match_operand:SI    2 "immediate_operand" "M")] UNSPECV_SFPADDI)]
  "TARGET_SFPU"
  "SFPADDI\t%0, %1, %2")

(define_insn "riscv_sfpdivp2"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
			  (unspec_volatile [(match_operand:SI    1 "immediate_operand" "N")
												  (match_operand:V64SF 2 "register_operand"  "x")
												  (match_operand:SI    3 "immediate_operand" "M")] UNSPECV_SFPDIVP2))]
  "TARGET_SFPU"
  "SFPDIVP2\t%1, %2, %0, %3")

(define_insn "riscv_sfpexexp"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:SI    2 "immediate_operand" "M")] UNSPECV_SFPEXEXP))]
  "TARGET_SFPU"
  "SFPEXEXP\t%1, %0, %2")

(define_insn "riscv_sfpexman"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:SI    2 "immediate_operand" "M")] UNSPECV_SFPEXMAN))]
  "TARGET_SFPU"
  "SFPEXMAN\t%1, %0, %2")

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

(define_insn "riscv_sfpadd"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M")] UNSPECV_SFPADD))]
  "TARGET_SFPU"
  "SFPADD\t%0, %1, %2, %3")

(define_insn "riscv_sfpiaddv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:V64SF 2 "register_operand"  "0")
                          (match_operand:SI    3 "immediate_operand" "M")] UNSPECV_SFPIADDV))]
  "TARGET_SFPU"
  "SFPIADD\t0, %1, %0, %3")

(define_insn "riscv_sfpiaddi"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:SI    1 "immediate_operand" "N")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M")] UNSPECV_SFPIADDI))]
  "TARGET_SFPU"
  "SFPIADD\t%1, %2, %0, %3")

(define_insn "riscv_sfpshftv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:V64SF 2 "register_operand"  "0")
                          (match_operand:SI    3 "immediate_operand" "M")] UNSPECV_SFPSHFTV))]
  "TARGET_SFPU"
  "SFPSHFT\t0, %1, %0, %3")

(define_insn "riscv_sfpshfti"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:SI    1 "immediate_operand" "P")
                          (match_operand:V64SF 2 "register_operand"  "0")
                          (match_operand:SI    3 "immediate_operand" "M")] UNSPECV_SFPSHFTI))]
  "TARGET_SFPU"
  "SFPSHFT\t%1, L0, %0, %3")

(define_insn "riscv_sfpabs"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:HI    2 "immediate_operand" "M")] UNSPECV_SFPABS))]
  "TARGET_SFPU"
  "SFPABS\t%1, %0, %2")

(define_insn "riscv_sfpand"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:HI    2 "immediate_operand" "M")] UNSPECV_SFPAND))]
  "TARGET_SFPU"
  "SFPAND\t%1, %0, %2")

(define_insn "riscv_sfpor"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:HI    2 "immediate_operand" "M")] UNSPECV_SFPOR))]
  "TARGET_SFPU"
  "SFPOR\t%1, %0, %2")

(define_insn "riscv_sfpnot"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:HI    2 "immediate_operand" "M")] UNSPECV_SFPNOT))]
  "TARGET_SFPU"
  "SFPNOT\t%1, %0, %2")

(define_insn "riscv_sfplz"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:HI    2 "immediate_operand" "M")] UNSPECV_SFPLZ))]
  "TARGET_SFPU"
  "SFPLZ\t%1, %0, %2")

(define_insn "riscv_sfpsetmanv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:V64SF 2 "register_operand"  "0")
                          (match_operand:SI    3 "immediate_operand" "M")] UNSPECV_SFPSETMANV))]
  "TARGET_SFPU"
  "SFPSETMAN\t0, %1, %0, %3")

(define_insn "riscv_sfpsetmani"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:SI    1 "immediate_operand" "N")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M")] UNSPECV_SFPSETMANI))]
  "TARGET_SFPU"
  "SFPSETMAN\t%1, %2, %0, %3")

(define_insn "riscv_sfpsetexpv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:V64SF 2 "register_operand"  "0")
                          (match_operand:SI    3 "immediate_operand" "M")] UNSPECV_SFPSETEXPV))]
  "TARGET_SFPU"
  "SFPSETEXP\t0, %1, %0, %3")

(define_insn "riscv_sfpsetexpi"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:SI    1 "immediate_operand" "N")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M")] UNSPECV_SFPSETEXPI))]
  "TARGET_SFPU"
  "SFPSETEXP\t%1, %2, %0, %3")

(define_insn "riscv_sfpsetsgnv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:V64SF 2 "register_operand"  "0")
                          (match_operand:SI    3 "immediate_operand" "M")] UNSPECV_SFPSETSGNV))]
  "TARGET_SFPU"
  "SFPSETSGN\t0, %1, %0, %3")

(define_insn "riscv_sfpsetsgni"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:SI    1 "immediate_operand" "N")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M")] UNSPECV_SFPSETSGNI))]
  "TARGET_SFPU"
  "SFPSETSGN\t%1, %2, %0, %3")

(define_insn "riscv_sfpmadvvv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:V64SF 3 "register_operand"  "x")
                          (match_operand:SI    4 "immediate_operand" "M")] UNSPECV_SFPMAD_VVV))]
  "TARGET_SFPU"
  "SFPMAD\t%1, %2, %3, %0, %4")

(define_insn "riscv_sfpmadrvv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:SI    1 "immediate_operand" "M")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:V64SF 3 "register_operand"  "x")
                          (match_operand:SI    4 "immediate_operand" "M")] UNSPECV_SFPMAD_RVV))]
  "TARGET_SFPU"
  "SFPMAD\t%S1, %2, %3, %0, %4")

(define_insn "riscv_sfpmadvrv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:SI    2 "immediate_operand" "M")
                          (match_operand:V64SF 3 "register_operand"  "x")
                          (match_operand:SI    4 "immediate_operand" "M")] UNSPECV_SFPMAD_VRV))]
  "TARGET_SFPU"
  "SFPMAD\t%1, %S2, %3, %0, %4")

(define_insn "riscv_sfpmadvvr"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M")
                          (match_operand:SI    4 "immediate_operand" "M")] UNSPECV_SFPMAD_VVR))]
  "TARGET_SFPU"
  "SFPMAD\t%1, %2, %S3, %0, %4")

(define_insn "riscv_sfpmadrrv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:SI    1 "immediate_operand" "M")
                          (match_operand:SI    2 "immediate_operand" "M")
                          (match_operand:V64SF 3 "register_operand"  "x")
                          (match_operand:SI    4 "immediate_operand" "M")] UNSPECV_SFPMAD_RRV))]
  "TARGET_SFPU"
  "SFPMAD\t%S1, %S2, %3, %0, %4")

(define_insn "riscv_sfpmadvrr"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:SI    2 "immediate_operand" "M")
                          (match_operand:SI    3 "immediate_operand" "M")
                          (match_operand:SI    4 "immediate_operand" "M")] UNSPECV_SFPMAD_VRR))]
  "TARGET_SFPU"
  "SFPMAD\t%1, %S2, %S3, %0, %4")

(define_insn "riscv_sfpmadrvr"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:SI    1 "immediate_operand" "M")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M")
                          (match_operand:SI    4 "immediate_operand" "M")] UNSPECV_SFPMAD_RVR))]
  "TARGET_SFPU"
  "SFPMAD\t%S1, %2, %S3, %0, %4")

(define_insn "riscv_sfpmadrrr"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:SI    1 "immediate_operand" "M")
                          (match_operand:SI    2 "immediate_operand" "M")
                          (match_operand:SI    3 "immediate_operand" "M")
                          (match_operand:SI    4 "immediate_operand" "M")] UNSPECV_SFPMAD_RRR))]
  "TARGET_SFPU"
  "SFPMAD\t%S1, %S2, %S3, %0, %4")

(define_insn "riscv_sfpsetcci"
  [(unspec_volatile [(match_operand:SI    0 "immediate_operand" "O")
                     (match_operand:SI    1 "immediate_operand" "M")] UNSPECV_SFPSETCCI)]
  "TARGET_SFPU"
  "SFPSETCC\t%0, L0, %1")

(define_insn "riscv_sfpsetccv"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "x")
                     (match_operand:SI    1 "immediate_operand" "M")] UNSPECV_SFPSETCCV)]
  "TARGET_SFPU"
  "SFPSETCC\t0, %0, %1")

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

