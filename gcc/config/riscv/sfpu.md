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
  UNSPECV_SFPSTORE_V
  UNSPECV_SFPSTORE_R
  UNSPECV_SFPMULI
  UNSPECV_SFPADDI
  UNSPECV_SFPMUL
  UNSPECV_SFPADD
  UNSPECV_SFPIADD_V
  UNSPECV_SFPIADD_I
  UNSPECV_SFPIADD_R
  UNSPECV_SFPSHFT_V
  UNSPECV_SFPSHFT_I
  UNSPECV_SFPABS
  UNSPECV_SFPAND
  UNSPECV_SFPOR
  UNSPECV_SFPNOT
  UNSPECV_SFPLZ
  UNSPECV_SFPSETMAN_V
  UNSPECV_SFPSETMAN_I
  UNSPECV_SFPSETEXP_V
  UNSPECV_SFPSETEXP_I
  UNSPECV_SFPSETSGN_V
  UNSPECV_SFPSETSGN_I
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
  UNSPECV_SFPSETCC_I
  UNSPECV_SFPSETCC_V
  UNSPECV_SFPENCC
  UNSPECV_SFPCOMPC
  UNSPECV_SFPPUSHC
  UNSPECV_SFPPOPC
  UNSPECV_SFPLUT
])

(define_constants [
  ;; Tenstorrent SFPU registers.
	(LREG0 66)
	(LREG1 67)
	(LREG2 68)
	(LREG3 69)
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
	{
	  switch (which_alternative) {
		  case 0:
			  return "SFPMOV\t%1, %0, 0";
				break;

			case 1:
			case 2:
				if (INSN_HAS_LOCATION (insn)) {
					error_at(INSN_LOCATION(insn), "Error: Not enough SFPU registers.  Need to spill.  Exiting!\n");
					exit(1);
				} else {
					error("Error: Not enough SFPU registers.  Need to spill.  Exiting!\n");
					exit(1);
				}
				break;

			default:
			  gcc_unreachable();
				break;
		}
	}
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

(define_insn "riscv_sfpstore_v"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "x")
                     (match_operand:SI    1 "immediate_operand" "M")
                     (match_operand:SI    2 "immediate_operand" "N")] UNSPECV_SFPSTORE_V)]
  "TARGET_SFPU"
  "SFPSTORE\t%0, %1, %2")

(define_insn "riscv_sfpstore_r"
  [(unspec_volatile [(match_operand:SI    0 "immediate_operand" "M")
                     (match_operand:SI    1 "immediate_operand" "M")
                     (match_operand:SI    2 "immediate_operand" "N")] UNSPECV_SFPSTORE_R)]
  "TARGET_SFPU"
  "SFPSTORE\t%S0, %1, %2")

(define_insn "riscv_sfpmuli"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
	                        (match_operand:SI    2 "immediate_operand" "N")
                          (match_operand:SI    3 "immediate_operand" "M")] UNSPECV_SFPMULI))]
  "TARGET_SFPU"
  "SFPMULI\t%2, %0, %3")

(define_insn "riscv_sfpaddi"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
	                        (match_operand:SI    2 "immediate_operand" "N")
                          (match_operand:SI    3 "immediate_operand" "M")] UNSPECV_SFPADDI))]
  "TARGET_SFPU"
  "SFPADDI\t%2, %0, %3")

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
  "SFPMOV\t%1, %0, %2")

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

(define_insn "riscv_sfpiadd_v"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M")] UNSPECV_SFPIADD_V))]
  "TARGET_SFPU"
  "SFPIADD\t0, %2, %0, %3")

(define_insn "riscv_sfpiadd_i"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:SI    2 "immediate_operand" "N")
                          (match_operand:SI    3 "immediate_operand" "M")] UNSPECV_SFPIADD_I))]
  "TARGET_SFPU"
  "SFPIADD\t%2, %1, %0, %3")

(define_insn "riscv_sfpiadd_r"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:SI    2 "immediate_operand" "M")
                          (match_operand:SI    3 "immediate_operand" "M")] UNSPECV_SFPIADD_R))]
  "TARGET_SFPU"
  "SFPIADD\t0, %S2, %0, %3")

(define_insn "riscv_sfpshft_v"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_SFPSHFT_V))]
  "TARGET_SFPU"
  "SFPSHFT\t0, %2, %0, 0")

(define_insn "riscv_sfpshft_i"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:SI    2 "immediate_operand" "P")] UNSPECV_SFPSHFT_I))]
  "TARGET_SFPU"
  "SFPSHFT\t%2, L0, %0, 1")

(define_insn "riscv_sfpabs"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:HI    2 "immediate_operand" "M")] UNSPECV_SFPABS))]
  "TARGET_SFPU"
  "SFPABS\t%1, %0, %2")

(define_insn "riscv_sfpand"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_SFPAND))]
  "TARGET_SFPU"
  "SFPAND\t%2, %0")

(define_insn "riscv_sfpor"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_SFPOR))]
  "TARGET_SFPU"
  "SFPOR\t%2, %0")

(define_insn "riscv_sfpnot"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")] UNSPECV_SFPNOT))]
  "TARGET_SFPU"
  "SFPNOT\t%1, %0")

(define_insn "riscv_sfplz"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:HI    2 "immediate_operand" "M")] UNSPECV_SFPLZ))]
  "TARGET_SFPU"
  "SFPLZ\t%1, %0, %2")

(define_insn "riscv_sfpsetman_v"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_SFPSETMAN_V))]
  "TARGET_SFPU"
  "SFPSETMAN\t0, %2, %0, 0")

(define_insn "riscv_sfpsetman_i"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:SI    1 "immediate_operand" "N")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_SFPSETMAN_I))]
  "TARGET_SFPU"
  "SFPSETMAN\t%1, %2, %0, 1")

(define_insn "riscv_sfpsetexp_v"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_SFPSETEXP_V))]
  "TARGET_SFPU"
  "SFPSETEXP\t0, %2, %0, 0")

(define_insn "riscv_sfpsetexp_i"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:SI    1 "immediate_operand" "N")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_SFPSETEXP_I))]
  "TARGET_SFPU"
  "SFPSETEXP\t%1, %2, %0, 1")

(define_insn "riscv_sfpsetsgn_v"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_SFPSETSGN_V))]
  "TARGET_SFPU"
  "SFPSETSGN\t0, %2, %0, 0")

(define_insn "riscv_sfpsetsgn_i"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:SI    1 "immediate_operand" "N")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_SFPSETSGN_I))]
  "TARGET_SFPU"
  "SFPSETSGN\t%1, %2, %0, 1")

(define_insn "riscv_sfpmad_vvv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:V64SF 3 "register_operand"  "x")
                          (match_operand:SI    4 "immediate_operand" "M")] UNSPECV_SFPMAD_VVV))]
  "TARGET_SFPU"
  "SFPMAD\t%1, %2, %3, %0, %4")

(define_insn "riscv_sfpmad_rvv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:SI    1 "immediate_operand" "M")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:V64SF 3 "register_operand"  "x")
                          (match_operand:SI    4 "immediate_operand" "M")] UNSPECV_SFPMAD_RVV))]
  "TARGET_SFPU"
  "SFPMAD\t%S1, %2, %3, %0, %4")

(define_insn "riscv_sfpmad_vrv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:SI    2 "immediate_operand" "M")
                          (match_operand:V64SF 3 "register_operand"  "x")
                          (match_operand:SI    4 "immediate_operand" "M")] UNSPECV_SFPMAD_VRV))]
  "TARGET_SFPU"
  "SFPMAD\t%1, %S2, %3, %0, %4")

(define_insn "riscv_sfpmad_vvr"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M")
                          (match_operand:SI    4 "immediate_operand" "M")] UNSPECV_SFPMAD_VVR))]
  "TARGET_SFPU"
  "SFPMAD\t%1, %2, %S3, %0, %4")

(define_insn "riscv_sfpmad_rrv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:SI    1 "immediate_operand" "M")
                          (match_operand:SI    2 "immediate_operand" "M")
                          (match_operand:V64SF 3 "register_operand"  "x")
                          (match_operand:SI    4 "immediate_operand" "M")] UNSPECV_SFPMAD_RRV))]
  "TARGET_SFPU"
  "SFPMAD\t%S1, %S2, %3, %0, %4")

(define_insn "riscv_sfpmad_vrr"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:SI    2 "immediate_operand" "M")
                          (match_operand:SI    3 "immediate_operand" "M")
                          (match_operand:SI    4 "immediate_operand" "M")] UNSPECV_SFPMAD_VRR))]
  "TARGET_SFPU"
  "SFPMAD\t%1, %S2, %S3, %0, %4")

(define_insn "riscv_sfpmad_rvr"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:SI    1 "immediate_operand" "M")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M")
                          (match_operand:SI    4 "immediate_operand" "M")] UNSPECV_SFPMAD_RVR))]
  "TARGET_SFPU"
  "SFPMAD\t%S1, %2, %S3, %0, %4")

(define_insn "riscv_sfpmad_rrr"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:SI    1 "immediate_operand" "M")
                          (match_operand:SI    2 "immediate_operand" "M")
                          (match_operand:SI    3 "immediate_operand" "M")
                          (match_operand:SI    4 "immediate_operand" "M")] UNSPECV_SFPMAD_RRR))]
  "TARGET_SFPU"
  "SFPMAD\t%S1, %S2, %S3, %0, %4")

(define_insn "riscv_sfpsetcc_i"
  [(unspec_volatile [(match_operand:SI    0 "immediate_operand" "O")
                     (match_operand:SI    1 "immediate_operand" "M")] UNSPECV_SFPSETCC_I)]
  "TARGET_SFPU"
  "SFPSETCC\t%0, L0, %1")

(define_insn "riscv_sfpsetcc_v"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "x")
                     (match_operand:SI    1 "immediate_operand" "M")] UNSPECV_SFPSETCC_V)]
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

(define_insn "riscv_sfplut"
  [(set (match_operand:V64SF 0 "register_operand" "=Q3")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "Q0")
                          (match_operand:V64SF 2 "register_operand"  "Q1")
                          (match_operand:V64SF 3 "register_operand"  "Q2")
                          (match_operand:V64SF 4 "register_operand"  "0")
                          (match_operand:SI    5 "immediate_operand" "M")] UNSPECV_SFPLUT))]
  "TARGET_SFPU"
  "SFPLUT\t%0, %5")

