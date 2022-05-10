;; Machine description for Tenstorrent SFPU Wormhole Intrinsics.
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

;; (include "sfpu-predicates.md")

; & in spec means early clobber, written before inputs are used, cannot reuse input reg

(define_c_enum "unspecv" [
  ;; Tenstorrent SFPU unspecs.
  ;; INT for internal
  ;; IMM for immediate
  ;; LV for keep dst reg alive as input for predicated liveness
  UNSPECV_WORMHOLE_SFPASSIGN_LV
  UNSPECV_WORMHOLE_SFPASSIGNLR
  UNSPECV_WORMHOLE_SFPASSIGNLR_INT
  UNSPECV_WORMHOLE_SFPKEEPALIVE
  UNSPECV_WORMHOLE_SFPKEEPALIVE0_INT
  UNSPECV_WORMHOLE_SFPKEEPALIVE1_INT
  UNSPECV_WORMHOLE_SFPKEEPALIVE2_INT
  UNSPECV_WORMHOLE_SFPKEEPALIVE3_INT
  UNSPECV_WORMHOLE_SFPLOAD
  UNSPECV_WORMHOLE_SFPLOAD_LV
  UNSPECV_WORMHOLE_SFPLOAD_INT
  UNSPECV_WORMHOLE_SFPLOADI
  UNSPECV_WORMHOLE_SFPLOADI_LV
  UNSPECV_WORMHOLE_SFPLOADI_INT
  UNSPECV_WORMHOLE_SFPSTORE
  UNSPECV_WORMHOLE_SFPSTORE_INT
  UNSPECV_WORMHOLE_SFPMULI
  UNSPECV_WORMHOLE_SFPMULI_INT
  UNSPECV_WORMHOLE_SFPADDI
  UNSPECV_WORMHOLE_SFPADDI_INT
  UNSPECV_WORMHOLE_SFPMUL
  UNSPECV_WORMHOLE_SFPMUL_LV
  UNSPECV_WORMHOLE_SFPMUL_INT
  UNSPECV_WORMHOLE_SFPADD
  UNSPECV_WORMHOLE_SFPADD_LV
  UNSPECV_WORMHOLE_SFPADD_INT
  UNSPECV_WORMHOLE_SFPIADD_V
  UNSPECV_WORMHOLE_SFPIADD_V_EX
  UNSPECV_WORMHOLE_SFPIADD_I
  UNSPECV_WORMHOLE_SFPIADD_I_LV
  UNSPECV_WORMHOLE_SFPIADD_I_EX
  UNSPECV_WORMHOLE_SFPIADD_I_EX_LV
  UNSPECV_WORMHOLE_SFPIADD_I_INT
  UNSPECV_WORMHOLE_SFPSHFT_V
  UNSPECV_WORMHOLE_SFPSHFT_I
  UNSPECV_WORMHOLE_SFPSHFT_I_INT
  UNSPECV_WORMHOLE_SFPABS
  UNSPECV_WORMHOLE_SFPABS_LV
  UNSPECV_WORMHOLE_SFPABS_INT
  UNSPECV_WORMHOLE_SFPAND
  UNSPECV_WORMHOLE_SFPOR
  UNSPECV_WORMHOLE_SFPNOT
  UNSPECV_WORMHOLE_SFPNOT_LV
  UNSPECV_WORMHOLE_SFPNOT_INT
  UNSPECV_WORMHOLE_SFPLZ
  UNSPECV_WORMHOLE_SFPLZ_LV
  UNSPECV_WORMHOLE_SFPLZ_INT
  UNSPECV_WORMHOLE_SFPSETMAN_V
  UNSPECV_WORMHOLE_SFPSETMAN_I
  UNSPECV_WORMHOLE_SFPSETMAN_I_LV
  UNSPECV_WORMHOLE_SFPSETMAN_I_INT
  UNSPECV_WORMHOLE_SFPSETEXP_V
  UNSPECV_WORMHOLE_SFPSETEXP_I
  UNSPECV_WORMHOLE_SFPSETEXP_I_LV
  UNSPECV_WORMHOLE_SFPSETEXP_I_INT
  UNSPECV_WORMHOLE_SFPSETSGN_V
  UNSPECV_WORMHOLE_SFPSETSGN_I
  UNSPECV_WORMHOLE_SFPSETSGN_I_LV
  UNSPECV_WORMHOLE_SFPSETSGN_I_INT
  UNSPECV_WORMHOLE_SFPMAD
  UNSPECV_WORMHOLE_SFPMAD_LV
  UNSPECV_WORMHOLE_SFPMAD_INT
  UNSPECV_WORMHOLE_SFPMOV
  UNSPECV_WORMHOLE_SFPMOV_LV
  UNSPECV_WORMHOLE_SFPMOV_INT
  UNSPECV_WORMHOLE_SFPDIVP2
  UNSPECV_WORMHOLE_SFPDIVP2_LV
  UNSPECV_WORMHOLE_SFPDIVP2_INT
  UNSPECV_WORMHOLE_SFPEXEXP
  UNSPECV_WORMHOLE_SFPEXEXP_LV
  UNSPECV_WORMHOLE_SFPEXEXP_INT
  UNSPECV_WORMHOLE_SFPEXMAN
  UNSPECV_WORMHOLE_SFPEXMAN_LV
  UNSPECV_WORMHOLE_SFPEXMAN_INT
  UNSPECV_WORMHOLE_SFPSETCC_I
  UNSPECV_WORMHOLE_SFPSETCC_V
  UNSPECV_WORMHOLE_SFPSCMP_EX
  UNSPECV_WORMHOLE_SFPVCMP_EX
  UNSPECV_WORMHOLE_SFPENCC
  UNSPECV_WORMHOLE_SFPCOMPC
  UNSPECV_WORMHOLE_SFPPUSHC
  UNSPECV_WORMHOLE_SFPPOPC
  UNSPECV_WORMHOLE_SFPLUT
  UNSPECV_WORMHOLE_SFPNOP
  UNSPECV_WORMHOLE_SFPILLEGAL
  UNSPECV_WORMHOLE_SFPNONIMM_DST
  UNSPECV_WORMHOLE_SFPNONIMM_DST_SRC
  UNSPECV_WORMHOLE_SFPNONIMM_SRC
  UNSPECV_WORMHOLE_SFPNONIMM_STORE
])

(define_constants [
  ;; Tenstorrent SFPU registers.
	(LREG0 66)
	(LREG1 67)
	(LREG2 68)
	(LREG3 69)
])

(define_expand "wormhole_movv64sf"
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
  "TARGET_WORMHOLE  &&
   (   register_operand (operands[0], V64SFmode)
    || reg_or_0_operand (operands[1], V64SFmode))"
  {
    switch (which_alternative) {
    case 0:
      // Note: must re-enable all elements until we know if we are in a predicated state
      output_asm_insn("SFPPUSHC", operands);
      output_asm_insn("SFPENCC\t3, 2", operands);
      output_asm_insn("SFPNOP", operands);
      output_asm_insn("SFPMOV\t%1, %0, 0", operands);
      output_asm_insn("SFPNOP", operands);
      output_asm_insn("SFPNOP", operands);
      output_asm_insn("SFPPOPC", operands);
      return "SFPNOP";
      break;

    case 1:
      if (INSN_HAS_LOCATION (insn)) {
        error_at(INSN_LOCATION(insn), "cannot load sfpu register");
      } else {
        error("cannot load sfpu register");
      }
      gcc_assert(0);
      return "SFPILLEGAL";

    case 2:
      if (INSN_HAS_LOCATION (insn)) {
        fatal_error(INSN_LOCATION(insn), "cannot store sfpu register (register spill)");
      } else {
        error("cannot store sfpu register (register spill)");
      }
      return "SFPILLEGAL";

    default:
      gcc_unreachable();
      break;
    }
  }
  [(set_attr "move_type" "fmove,fpload,fpstore")
   (set_attr "length" "32,4,4")
   (set_attr "mode" "V64SF")])

(define_insn "*movv64sf_hardfloat_nocc"
  [(set (match_operand:V64SF 0 "nonimmediate_operand" "=x")
        (match_operand:V64SF 1 "move_operand"         " x"))]
  "TARGET_WORMHOLE  &&
   (   register_operand (operands[0], V64SFmode)
    || reg_or_0_operand (operands[1], V64SFmode))"
  {
    output_asm_insn("SFPMOV\t%1, %0, 0", operands);
    output_asm_insn("SFPNOP", operands);
    return "SFPNOP";
  })

(define_insn "riscv_wormhole_sfpassign_lv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_WORMHOLE_SFPASSIGN_LV))]
  "TARGET_WORMHOLE"
{
    output_asm_insn("SFPMOV\t%2, %0, 0", operands);
    output_asm_insn("SFPNOP", operands);
    return "SFPNOP";
})

(define_expand "riscv_wormhole_sfpassignlr"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI 1 "immediate_operand" "M")] UNSPECV_WORMHOLE_SFPASSIGNLR))]
  "TARGET_WORMHOLE"
{
  riscv_sfpu_emit_sfpassignlr(operands[0], operands[1]);
  DONE;
})

(define_insn "riscv_wormhole_sfpassignlr_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(const_int 0)] UNSPECV_WORMHOLE_SFPASSIGNLR_INT))]
  "TARGET_WORMHOLE"
  "")

(define_expand "riscv_wormhole_sfpkeepalive"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "")
                     (match_operand:SI    1 "immediate_operand" "M")] UNSPECV_WORMHOLE_SFPKEEPALIVE)]

  "TARGET_WORMHOLE"
{
  static rtx (*fn_ptr[4])(rtx) = {gen_riscv_wormhole_sfpkeepalive0_int, gen_riscv_wormhole_sfpkeepalive1_int, 
                                  gen_riscv_wormhole_sfpkeepalive2_int, gen_riscv_wormhole_sfpkeepalive3_int};
  emit_insn(fn_ptr[INTVAL(operands[1])](operands[0]));
  DONE;
})

(define_int_iterator wormhole_keepalive_int [UNSPECV_WORMHOLE_SFPKEEPALIVE0_INT UNSPECV_WORMHOLE_SFPKEEPALIVE1_INT UNSPECV_WORMHOLE_SFPKEEPALIVE2_INT UNSPECV_WORMHOLE_SFPKEEPALIVE3_INT])
(define_int_attr wormhole_keepalive_int_name [(UNSPECV_WORMHOLE_SFPKEEPALIVE0_INT "0") (UNSPECV_WORMHOLE_SFPKEEPALIVE1_INT "1") (UNSPECV_WORMHOLE_SFPKEEPALIVE2_INT "2") (UNSPECV_WORMHOLE_SFPKEEPALIVE3_INT "3")])
(define_insn "riscv_wormhole_sfpkeepalive<wormhole_keepalive_int_name>_int"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand" "Q<wormhole_keepalive_int_name>")] wormhole_keepalive_int)]
  "TARGET_WORMHOLE"
  "")

(define_expand "riscv_wormhole_sfpload"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI 1 "address_operand"  "")
                          (match_operand:SI 2 "immediate_operand" "")
                          (match_operand:SI 3 "immediate_operand" "")
                          (match_operand:SI 4 "nonmemory_operand" "")] UNSPECV_WORMHOLE_SFPLOAD))]
  "TARGET_WORMHOLE"
{
  rtx live = riscv_sfpu_gen_const0_vector();
  riscv_wormhole_sfpu_emit_sfpload(operands[0], live, operands[1], operands[2], operands[3], operands[4]);
  DONE;
})

(define_expand "riscv_wormhole_sfpload_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")
                          (match_operand:SI    4 "immediate_operand" "")
                          (match_operand:SI    5 "nonmemory_operand" "")] UNSPECV_WORMHOLE_SFPLOAD_LV))]
  "TARGET_WORMHOLE"
{
  rtx live = operands[2];
  riscv_wormhole_sfpu_emit_sfpload(operands[0], live, operands[1], operands[3], operands[4], operands[5]);
  DONE;
})

(define_insn "riscv_wormhole_sfpload_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:SI    2 "immediate_operand" "M, M")
                          (match_operand:SI    3 "immediate_operand" "M, M")
                          (match_operand:SI    4 "immediate_operand" "N, N")] UNSPECV_WORMHOLE_SFPLOAD_INT))]
  "TARGET_WORMHOLE"
  "@
   SFPLOAD\t%0, %2, %3, %4
   SFPLOAD\t%0, %2, %3, %4")


;;; SFPLOADI and SFPLOADI_LV
(define_expand "riscv_wormhole_sfploadi"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec [(match_operand:SI 1 "address_operand"  "")
                          (match_operand:SI 2 "immediate_operand" "")
                          (match_operand:SI 3 "nonmemory_operand" "")] UNSPECV_WORMHOLE_SFPLOADI))]
  "TARGET_WORMHOLE"
{
  rtx live = riscv_sfpu_gen_const0_vector();
  riscv_wormhole_sfpu_emit_sfploadi(operands[0], live, operands[1], operands[2], operands[3]);
  DONE;
})

(define_expand "riscv_wormhole_sfploadi_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec [(match_operand:SI    1 "address_operand"   "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")
                          (match_operand:SI    4 "nonmemory_operand" "")] UNSPECV_WORMHOLE_SFPLOADI_LV))]
  "TARGET_WORMHOLE"
{
  rtx live = operands[2];
  riscv_wormhole_sfpu_emit_sfploadi(operands[0], live, operands[1], operands[3], operands[4]);
  DONE;
})

;; (define_insn "riscv_wormhole_sfploadi_int"
;;   [(set (match_operand:V64SF 0 "register_operand" "=x,x,x,x")
;;         (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E,E,0,0")
;;                           (match_operand:SI    2 "immediate_operand" "M,M,M,M")
;;                           (match_operand:SI    3 "immediate_operand" "R,N,R,N")] UNSPECV_WORMHOLE_SFPLOADI_INT))]
;;   "TARGET_WORMHOLE"
;;   "@
;;   SFPLOADI\t%0, %2, %s3
;;   SFPLOADI\t%0, %2, %u3
;;   SFPLOADI\t%0, %2, %s3
;;   SFPLOADI\t%0, %2, %u3")

(define_insn "riscv_wormhole_sfploadi_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x,x,x,x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E,E,0,0")
                          (match_operand:SI    2 "immediate_operand" "M,M,M,M")
                          (match_operand:SI    3 "immediate_operand" "R,N,R,N")] UNSPECV_WORMHOLE_SFPLOADI_INT))]
  "TARGET_WORMHOLE"
  "@
  SFPLOADI\t%0, %2, %s3
  SFPLOADI\t%0, %2, %u3
  SFPLOADI\t%0, %2, %s3
  SFPLOADI\t%0, %2, %u3")

(define_expand "riscv_wormhole_sfpstore"
  [(unspec_volatile [(match_operand:SI    0 "address_operand"   "")
                     (match_operand:V64SF 1 "register_operand"  "")
                     (match_operand:SI    2 "immediate_operand" "")
                     (match_operand:SI    3 "nonmemory_operand" "")] UNSPECV_WORMHOLE_SFPSTORE)]
  "TARGET_WORMHOLE"
{
  if (GET_CODE(operands[3]) == CONST_INT) {
    emit_insn (gen_riscv_wormhole_sfpstore_int(operands[1], operands[2], operands[3]));
  } else {
    int base = TT_OP_SFPSTORE(0, INTVAL(operands[2]), 0);
    riscv_wormhole_sfpu_emit_nonimm_store(operands[0], operands[1], 0, operands[3], base, 16, 16, 20);
  }
  DONE;
})

(define_insn "riscv_wormhole_sfpstore_int"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "x")
                     (match_operand:SI    1 "immediate_operand" "M")
                     (match_operand:SI    2 "nonmemory_operand" "N")] UNSPECV_WORMHOLE_SFPSTORE_INT)]
  "TARGET_WORMHOLE"
  "SFPSTORE\t%0, %1, %2")


(define_int_iterator wormhole_muliaddi [UNSPECV_WORMHOLE_SFPMULI UNSPECV_WORMHOLE_SFPADDI])
(define_int_attr wormhole_muliaddi_name [(UNSPECV_WORMHOLE_SFPMULI "muli") (UNSPECV_WORMHOLE_SFPADDI "addi")])
(define_int_attr wormhole_muliaddi_call [(UNSPECV_WORMHOLE_SFPMULI "MULI") (UNSPECV_WORMHOLE_SFPADDI "ADDI")])
(define_expand "riscv_wormhole_sfp<wormhole_muliaddi_name>"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:SI    4 "immediate_operand" "")] wormhole_muliaddi))]
  "TARGET_WORMHOLE"
{
  if (GET_CODE(operands[3]) == CONST_INT) {
    emit_insn (gen_riscv_wormhole_sfp<wormhole_muliaddi_name>_int(operands[0], operands[2], operands[3], operands[4]));
  } else {
    int base = TT_OP_SFP<wormhole_muliaddi_call>(0, 0, INTVAL(operands[4]));
    riscv_wormhole_sfpu_emit_nonimm_dst(operands[1], operands[0], 2, operands[2], operands[3], base, 16, 8, 4);
  }
  DONE;
})

(define_int_iterator wormhole_muliaddi_int [UNSPECV_WORMHOLE_SFPMULI_INT UNSPECV_WORMHOLE_SFPADDI_INT])
(define_int_attr wormhole_muliaddi_int_name [(UNSPECV_WORMHOLE_SFPMULI_INT "muli") (UNSPECV_WORMHOLE_SFPADDI_INT "addi")])
(define_int_attr wormhole_muliaddi_int_call [(UNSPECV_WORMHOLE_SFPMULI_INT "MULI") (UNSPECV_WORMHOLE_SFPADDI_INT "ADDI")])
(define_insn "riscv_wormhole_sfp<wormhole_muliaddi_int_name>_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:SI    2 "nonmemory_operand" "n")
                          (match_operand:SI    3 "immediate_operand" "M")] wormhole_muliaddi_int))]
  "TARGET_WORMHOLE"
{
  output_asm_insn("SFP<wormhole_muliaddi_int_call>\t%2, %0, %3", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_expand "riscv_wormhole_sfpdivp2"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:SI    2 "nonmemory_operand" "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:SI    4 "immediate_operand" "")] UNSPECV_WORMHOLE_SFPDIVP2))]
  "TARGET_WORMHOLE"
{
  rtx live = riscv_sfpu_gen_const0_vector();
  riscv_wormhole_sfpu_emit_sfpdivp2(operands[0], live, operands[1], operands[2], operands[3], operands[4]);
  DONE;
})

(define_expand "riscv_wormhole_sfpdivp2_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:V64SF 4 "register_operand"  "")
                          (match_operand:SI    5 "immediate_operand" "")] UNSPECV_WORMHOLE_SFPDIVP2_LV))]
  "TARGET_WORMHOLE"
{
  rtx live = operands[2];
  riscv_wormhole_sfpu_emit_sfpdivp2(operands[0], live, operands[1], operands[3], operands[4], operands[5]);
  DONE;
})

(define_insn "riscv_wormhole_sfpdivp2_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:SI    2 "immediate_operand" "n, n")
                          (match_operand:V64SF 3 "register_operand"  "x, x")
                          (match_operand:SI    4 "immediate_operand" "M, M")] UNSPECV_WORMHOLE_SFPDIVP2_INT))]
  "TARGET_WORMHOLE"
{
  output_asm_insn("SFPDIVP2\t%2, %3, %0, %4", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_int_iterator wormhole_simple_op
  [UNSPECV_WORMHOLE_SFPEXEXP
   UNSPECV_WORMHOLE_SFPEXMAN
   UNSPECV_WORMHOLE_SFPABS
   UNSPECV_WORMHOLE_SFPMOV
   UNSPECV_WORMHOLE_SFPLZ])
(define_int_attr wormhole_simple_op_name
  [(UNSPECV_WORMHOLE_SFPEXEXP "exexp")
   (UNSPECV_WORMHOLE_SFPEXMAN "exman")
   (UNSPECV_WORMHOLE_SFPABS "abs")
   (UNSPECV_WORMHOLE_SFPMOV "mov")
   (UNSPECV_WORMHOLE_SFPLZ "lz")])
(define_int_iterator wormhole_simple_op_lv
  [UNSPECV_WORMHOLE_SFPEXEXP_LV
   UNSPECV_WORMHOLE_SFPEXMAN_LV
   UNSPECV_WORMHOLE_SFPABS_LV
   UNSPECV_WORMHOLE_SFPMOV_LV
   UNSPECV_WORMHOLE_SFPLZ_LV])
(define_int_attr wormhole_simple_op_name_lv
  [(UNSPECV_WORMHOLE_SFPEXEXP_LV "exexp")
   (UNSPECV_WORMHOLE_SFPEXMAN_LV "exman")
   (UNSPECV_WORMHOLE_SFPABS_LV "abs")
   (UNSPECV_WORMHOLE_SFPMOV_LV "mov")
   (UNSPECV_WORMHOLE_SFPLZ_LV "lz")])
(define_int_iterator wormhole_simple_op_int
  [UNSPECV_WORMHOLE_SFPEXEXP_INT
   UNSPECV_WORMHOLE_SFPEXMAN_INT
   UNSPECV_WORMHOLE_SFPABS_INT
   UNSPECV_WORMHOLE_SFPMOV_INT
   UNSPECV_WORMHOLE_SFPLZ_INT])
(define_int_attr wormhole_simple_op_name_int
  [(UNSPECV_WORMHOLE_SFPEXEXP_INT "exexp")
   (UNSPECV_WORMHOLE_SFPEXMAN_INT "exman")
   (UNSPECV_WORMHOLE_SFPABS_INT "abs")
   (UNSPECV_WORMHOLE_SFPMOV_INT "mov")
   (UNSPECV_WORMHOLE_SFPLZ_INT "lz")])
(define_int_attr wormhole_simple_op_call_int
  [(UNSPECV_WORMHOLE_SFPEXEXP_INT "EXEXP")
   (UNSPECV_WORMHOLE_SFPEXMAN_INT "EXMAN")
   (UNSPECV_WORMHOLE_SFPABS_INT "ABS")
   (UNSPECV_WORMHOLE_SFPMOV_INT "MOV")
   (UNSPECV_WORMHOLE_SFPLZ_INT "LZ")])
(define_int_attr wormhole_simple_op_id_int
  [(UNSPECV_WORMHOLE_SFPEXEXP_INT "UNSPECV_WORMHOLE_SFPEXEXP_INT")
   (UNSPECV_WORMHOLE_SFPEXMAN_INT "UNSPECV_WORMHOLE_SFPEXMAN_INT")
   (UNSPECV_WORMHOLE_SFPABS_INT "UNSPECV_WORMHOLE_SFPABS_INT")
   (UNSPECV_WORMHOLE_SFPMOV_INT "UNSPECV_WORMHOLE_SFPMOV_INT")
   (UNSPECV_WORMHOLE_SFPLZ_INT "UNSPECV_WORMHOLE_SFPLZ_INT")])

(define_expand "riscv_wormhole_sfp<wormhole_simple_op_name>"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:SI    2 "immediate_operand" "")] wormhole_simple_op))]
  "TARGET_WORMHOLE"
{
  rtx live = riscv_sfpu_gen_const0_vector();
  emit_insn (gen_riscv_wormhole_sfp<wormhole_simple_op_name>_int(operands[0], live, operands[1], operands[2]));
  DONE;
})

(define_expand "riscv_wormhole_sfp<wormhole_simple_op_name_lv>_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")] wormhole_simple_op_lv))]
  "TARGET_WORMHOLE"
{
  emit_insn (gen_riscv_wormhole_sfp<wormhole_simple_op_name_lv>_int(operands[0], operands[1], operands[2], operands[3]));
  DONE;
})

(define_insn "riscv_wormhole_sfp<wormhole_simple_op_name_int>_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")
                          (match_operand:SI    3 "immediate_operand" "M, M")] wormhole_simple_op_int))]
  "TARGET_WORMHOLE"
{
    output_asm_insn("SFP<wormhole_simple_op_call_int>\t%2, %0, %3", operands);

    int mod1 = INTVAL(operands[3]);
    // EXEXP and LZ require 3 nops when setting the CC
    if (((<wormhole_simple_op_id_int> == UNSPECV_WORMHOLE_SFPEXEXP_INT) &&
         (mod1 == 2 || mod1 == 3 || mod1 == 8 || mod1 == 9 || mod1 == 10 || mod1 == 11)) ||
        ((<wormhole_simple_op_id_int> == UNSPECV_WORMHOLE_SFPLZ_INT) &&
         (mod1 == 2 || mod1 == 8 || mod1 == 10 || mod1 == 11))) {
      output_asm_insn("SFPNOP", operands);
    }

    output_asm_insn("SFPNOP", operands);

    return "SFPNOP";
})

(define_int_iterator wormhole_muladd [UNSPECV_WORMHOLE_SFPMUL UNSPECV_WORMHOLE_SFPADD])
(define_int_attr wormhole_muladd_name [(UNSPECV_WORMHOLE_SFPMUL "mul") (UNSPECV_WORMHOLE_SFPADD "add")])
(define_expand "riscv_wormhole_sfp<wormhole_muladd_name>"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")] wormhole_muladd))]
  "TARGET_WORMHOLE"
{
  rtx live = riscv_sfpu_gen_const0_vector();
  emit_insn (gen_riscv_wormhole_sfp<wormhole_muladd_name>_int(operands[0], live, operands[1], operands[2], operands[3]));
  DONE;
})

(define_int_iterator wormhole_muladd_lv [UNSPECV_WORMHOLE_SFPMUL_LV UNSPECV_WORMHOLE_SFPADD_LV])
(define_int_attr wormhole_muladd_name_lv [(UNSPECV_WORMHOLE_SFPMUL_LV "mul") (UNSPECV_WORMHOLE_SFPADD_LV "add")])
(define_expand "riscv_wormhole_sfp<wormhole_muladd_name_lv>_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:SI    4 "immediate_operand" "")] wormhole_muladd_lv))]
  "TARGET_WORMHOLE"
{
  rtx live = operands[1];
  emit_insn (gen_riscv_wormhole_sfp<wormhole_muladd_name_lv>_int(operands[0], live, operands[2], operands[3], operands[4]));
  DONE;
})

(define_int_iterator wormhole_muladd_int [UNSPECV_WORMHOLE_SFPMUL_INT UNSPECV_WORMHOLE_SFPADD_INT])
(define_int_attr wormhole_muladd_name_int [(UNSPECV_WORMHOLE_SFPMUL_INT "mul") (UNSPECV_WORMHOLE_SFPADD_INT "add")])
(define_int_attr wormhole_muladd_call_int [(UNSPECV_WORMHOLE_SFPMUL_INT "MUL\t%2, %3, L4") (UNSPECV_WORMHOLE_SFPADD_INT "ADD\tL10, %2, %3")])
(define_insn "riscv_wormhole_sfp<wormhole_muladd_name_int>_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")
                          (match_operand:V64SF 3 "register_operand"  "x, x")
                          (match_operand:SI    4 "immediate_operand" "M, M")] wormhole_muladd_int))]
  "TARGET_WORMHOLE"
{
  output_asm_insn("SFP<wormhole_muladd_call_int>, %0, %4", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_insn "riscv_wormhole_sfpiadd_v"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M")] UNSPECV_WORMHOLE_SFPIADD_V))]
  "TARGET_WORMHOLE"
{
  output_asm_insn("SFPIADD\t0, %2, %0, %3", operands);
  output_asm_insn("SFPNOP", operands);

  int mod1 = INTVAL(operands[3]);
  // Careful - this includes a few "reserved" values
  if (mod1 < 3 || mod1 > 7) {
    output_asm_insn("SFPNOP", operands);
  }
  return "SFPNOP";
})

(define_expand "riscv_wormhole_sfpiadd_i"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:SI    4 "immediate_operand" "")] UNSPECV_WORMHOLE_SFPIADD_I))]
  "TARGET_WORMHOLE"
{
  rtx live = riscv_sfpu_gen_const0_vector();
  riscv_wormhole_sfpu_emit_sfpiadd_i(operands[0], live, operands[1], operands[2], operands[3], operands[4]);
  DONE;
})

(define_expand "riscv_wormhole_sfpiadd_i_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"   "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:SI    4 "nonmemory_operand" "")
                          (match_operand:SI    5 "immediate_operand" "")] UNSPECV_WORMHOLE_SFPIADD_I_LV))]
  "TARGET_WORMHOLE"
{
  rtx live = operands[2];
  riscv_wormhole_sfpu_emit_sfpiadd_i(operands[0], live, operands[1], operands[3], operands[4], operands[5]);
  DONE;
})

(define_insn "riscv_wormhole_sfpiadd_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")
                          (match_operand:SI    3 "immediate_operand" "n, n")
                          (match_operand:SI    4 "immediate_operand" "M, M")] UNSPECV_WORMHOLE_SFPIADD_I_INT))]
  "TARGET_WORMHOLE"
{
  output_asm_insn("SFPIADD\t%3, %2, %0, %4", operands);
  output_asm_insn("SFPNOP", operands);

  int mod1 = INTVAL(operands[4]);
  // Careful - this includes a few "reserved" values
  if (mod1 < 3 || mod1 > 7) {
    output_asm_insn("SFPNOP", operands);
  }
  return "SFPNOP";
})

(define_expand "riscv_wormhole_sfpiadd_v_ex"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M")] UNSPECV_WORMHOLE_SFPIADD_V_EX))]
  "TARGET_WORMHOLE"
{
  riscv_wormhole_sfpu_emit_sfpiadd_v_ex(operands[0], operands[1], operands[2], operands[3]);
  DONE;
})


(define_expand "riscv_wormhole_sfpiadd_i_ex"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:SI    4 "immediate_operand" "")] UNSPECV_WORMHOLE_SFPIADD_I_EX))]
  "TARGET_WORMHOLE"
{
  rtx live = riscv_sfpu_gen_const0_vector();
  riscv_wormhole_sfpu_emit_sfpiadd_i_ex(operands[0], live, operands[1], operands[2], operands[3], operands[4]);
  DONE;
})

(define_expand "riscv_wormhole_sfpiadd_i_ex_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"   "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:SI    4 "nonmemory_operand" "")
                          (match_operand:SI    5 "immediate_operand" "")] UNSPECV_WORMHOLE_SFPIADD_I_EX_LV))]
  "TARGET_WORMHOLE"
{
  rtx live = operands[2];
  riscv_wormhole_sfpu_emit_sfpiadd_i_ex(operands[0], live, operands[1], operands[3], operands[4], operands[5]);
  DONE;
})

(define_insn "riscv_wormhole_sfpshft_v"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_WORMHOLE_SFPSHFT_V))]
  "TARGET_WORMHOLE"
{
  output_asm_insn("SFPSHFT\t0, %2, %0, 0", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_expand "riscv_wormhole_sfpshft_i"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")] UNSPECV_WORMHOLE_SFPSHFT_I))]
  "TARGET_WORMHOLE"
{
  if (GET_CODE(operands[3]) == CONST_INT) {
    emit_insn (gen_riscv_wormhole_sfpshft_i_int(operands[0], operands[2], riscv_sfpu_clamp_signed(operands[3], 0x7FF)));
  } else {
    int base = TT_OP_SFPSHFT(0, 0, 0, 1);
    riscv_wormhole_sfpu_emit_nonimm_dst(operands[1], operands[0], 2, operands[2], operands[3], base, 20, 8, 4);
  }
  DONE;
})

(define_insn "riscv_wormhole_sfpshft_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:SI    2 "nonmemory_operand" "n")] UNSPECV_WORMHOLE_SFPSHFT_I_INT))]
  "TARGET_WORMHOLE"
{
  output_asm_insn("SFPSHFT\t%2, L0, %0, 1", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_insn "riscv_wormhole_sfpand"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_WORMHOLE_SFPAND))]
  "TARGET_WORMHOLE"
{
  output_asm_insn("SFPAND\t%2, %0", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_insn "riscv_wormhole_sfpor"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_WORMHOLE_SFPOR))]
  "TARGET_WORMHOLE"
{
  output_asm_insn("SFPOR\t%2, %0", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_expand "riscv_wormhole_sfpnot"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")] UNSPECV_WORMHOLE_SFPNOT))]
  "TARGET_WORMHOLE"
{
  rtx live = riscv_sfpu_gen_const0_vector();
  emit_insn (gen_riscv_wormhole_sfpnot_int(operands[0], live, operands[1]));
  DONE;
})

(define_expand "riscv_wormhole_sfpnot_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")] UNSPECV_WORMHOLE_SFPNOT_LV))]
  "TARGET_WORMHOLE"
{
  rtx live = operands[1];
  emit_insn (gen_riscv_wormhole_sfpnot_int(operands[0], live, operands[2]));
  DONE;
})

(define_insn "riscv_wormhole_sfpnot_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")] UNSPECV_WORMHOLE_SFPNOT_INT))]
  "TARGET_WORMHOLE"
{
  output_asm_insn("SFPNOT\t%2, %0", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_int_iterator wormhole_set_float_op_v [UNSPECV_WORMHOLE_SFPSETEXP_V UNSPECV_WORMHOLE_SFPSETMAN_V UNSPECV_WORMHOLE_SFPSETSGN_V])
(define_int_attr wormhole_set_float_name_v [(UNSPECV_WORMHOLE_SFPSETEXP_V "exp") (UNSPECV_WORMHOLE_SFPSETMAN_V "man") (UNSPECV_WORMHOLE_SFPSETSGN_V "sgn")])
(define_int_attr wormhole_set_float_call_v [(UNSPECV_WORMHOLE_SFPSETEXP_V "EXP") (UNSPECV_WORMHOLE_SFPSETMAN_V "MAN") (UNSPECV_WORMHOLE_SFPSETSGN_V "SGN")])
(define_insn "riscv_wormhole_sfpset<wormhole_set_float_name_v>_v"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] wormhole_set_float_op_v))]
  "TARGET_WORMHOLE"
{
  output_asm_insn("SFPSET<wormhole_set_float_call_v>\t0, %2, %0, 0", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_int_iterator wormhole_set_float_op_i [UNSPECV_WORMHOLE_SFPSETEXP_I UNSPECV_WORMHOLE_SFPSETMAN_I UNSPECV_WORMHOLE_SFPSETSGN_I])
(define_int_attr wormhole_set_float_name_i [(UNSPECV_WORMHOLE_SFPSETEXP_I "exp") (UNSPECV_WORMHOLE_SFPSETMAN_I "man") (UNSPECV_WORMHOLE_SFPSETSGN_I "sgn")])
(define_int_attr wormhole_set_float_call_i [(UNSPECV_WORMHOLE_SFPSETEXP_I "EXP") (UNSPECV_WORMHOLE_SFPSETMAN_I "MAN") (UNSPECV_WORMHOLE_SFPSETSGN_I "SGN")])
(define_expand "riscv_wormhole_sfpset<wormhole_set_float_name_i>_i"
  [(set (match_operand:V64SF 0 "register_operand")
        (unspec_volatile [(match_operand:SI    1 "address_operand")
                          (match_operand:SI    2 "nonmemory_operand")
                          (match_operand:V64SF 3 "register_operand")] wormhole_set_float_op_i))]
  "TARGET_WORMHOLE"
{
  rtx live = riscv_sfpu_gen_const0_vector();
  if (GET_CODE(operands[2]) == CONST_INT) {
    emit_insn (gen_riscv_wormhole_sfpset<wormhole_set_float_name_i>_i_int(operands[0], live,
                                                        riscv_sfpu_clamp_unsigned(operands[2], 0xFFF), operands[3]));
  } else {
    int base = TT_OP_SFPSET<wormhole_set_float_call_i>(0, 0, 0, 1);
    riscv_wormhole_sfpu_emit_nonimm_dst_src(operands[1], operands[0], 2, live, operands[3], operands[2], base, 20, 8, 4, 8);
  }
  DONE;
})

(define_int_iterator wormhole_set_float_op_i_lv [UNSPECV_WORMHOLE_SFPSETEXP_I_LV UNSPECV_WORMHOLE_SFPSETMAN_I_LV UNSPECV_WORMHOLE_SFPSETSGN_I_LV])
(define_int_attr wormhole_set_float_name_i_lv [(UNSPECV_WORMHOLE_SFPSETEXP_I_LV "exp") (UNSPECV_WORMHOLE_SFPSETMAN_I_LV "man") (UNSPECV_WORMHOLE_SFPSETSGN_I_LV "sgn")])
(define_int_attr wormhole_set_float_call_i_lv [(UNSPECV_WORMHOLE_SFPSETEXP_I_LV "EXP") (UNSPECV_WORMHOLE_SFPSETMAN_I_LV "MAN") (UNSPECV_WORMHOLE_SFPSETSGN_I_LV "SGN")])
(define_expand "riscv_wormhole_sfpset<wormhole_set_float_name_i_lv>_i_lv"
  [(set (match_operand:V64SF 0 "register_operand")
        (unspec_volatile [(match_operand:SI    1 "address_operand")
                          (match_operand:V64SF 2 "register_operand")
                          (match_operand:SI    3 "nonmemory_operand")
                          (match_operand:V64SF 4 "register_operand")] wormhole_set_float_op_i_lv))]
  "TARGET_WORMHOLE"
{
  rtx live = operands[2];
  if (GET_CODE(operands[3]) == CONST_INT) {
    emit_insn (gen_riscv_wormhole_sfpset<wormhole_set_float_name_i_lv>_i_int(operands[0], live,
                                                           riscv_sfpu_clamp_unsigned(operands[3], 0xFFF), operands[4]));
  } else {
    int base = TT_OP_SFPSET<wormhole_set_float_call_i_lv>(0, 0, 0, 1);
    riscv_wormhole_sfpu_emit_nonimm_dst_src(operands[1], operands[0], 2, live, operands[4], operands[3], base, 20, 8, 4, 8);
  }
  DONE;
})

(define_int_iterator wormhole_set_float_op_i_int [UNSPECV_WORMHOLE_SFPSETEXP_I_INT UNSPECV_WORMHOLE_SFPSETMAN_I_INT UNSPECV_WORMHOLE_SFPSETSGN_I_INT])
(define_int_attr wormhole_set_float_name_i_int [(UNSPECV_WORMHOLE_SFPSETEXP_I_INT "exp") (UNSPECV_WORMHOLE_SFPSETMAN_I_INT "man") (UNSPECV_WORMHOLE_SFPSETSGN_I_INT "sgn")])
(define_int_attr wormhole_set_float_call_i_int [(UNSPECV_WORMHOLE_SFPSETEXP_I_INT "EXP") (UNSPECV_WORMHOLE_SFPSETMAN_I_INT "MAN") (UNSPECV_WORMHOLE_SFPSETSGN_I_INT "SGN")])
(define_insn "riscv_wormhole_sfpset<wormhole_set_float_name_i_int>_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:SI    2 "immediate_operand" "n, n")
                          (match_operand:V64SF 3 "register_operand"  "x, x")] wormhole_set_float_op_i_int))]
  "TARGET_WORMHOLE"
{
  output_asm_insn("SFPSET<wormhole_set_float_call_i_int>\t%2, %3, %0, 1", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_expand "riscv_wormhole_sfpmad"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:SI    4 "immediate_operand" "")] UNSPECV_WORMHOLE_SFPMAD))]
  "TARGET_WORMHOLE"
{
  rtx live = riscv_sfpu_gen_const0_vector();
  emit_insn (gen_riscv_wormhole_sfpmad_int(operands[0], live, operands[1], operands[2], operands[3], operands[4]));
  DONE;
})

(define_expand "riscv_wormhole_sfpmad_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:V64SF 4 "register_operand"  "")
                          (match_operand:SI    5 "immediate_operand" "")] UNSPECV_WORMHOLE_SFPMAD_LV))]
  "TARGET_WORMHOLE"
{
  rtx live = operands[1];
  emit_insn (gen_riscv_wormhole_sfpmad_int(operands[0], live, operands[2], operands[3], operands[4], operands[5]));
  DONE;
})

(define_insn "riscv_wormhole_sfpmad_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")
                          (match_operand:V64SF 3 "register_operand"  "x, x")
                          (match_operand:V64SF 4 "register_operand"  "x, x")
                          (match_operand:SI    5 "immediate_operand" "M, M")] UNSPECV_WORMHOLE_SFPMAD_INT))]
  "TARGET_WORMHOLE"
{
  output_asm_insn("SFPMAD\t%2, %3, %4, %0, %5", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_insn "riscv_wormhole_sfpsetcc_i"
  [(unspec_volatile [(match_operand:SI    0 "immediate_operand" "n")
                     (match_operand:SI    1 "immediate_operand" "M")] UNSPECV_WORMHOLE_SFPSETCC_I)]
  "TARGET_WORMHOLE"
{
  output_asm_insn("SFPSETCC\t%0, L0, %1", operands);
  return "SFPNOP";
})

(define_insn "riscv_wormhole_sfpsetcc_v"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "x")
                     (match_operand:SI    1 "immediate_operand" "M")] UNSPECV_WORMHOLE_SFPSETCC_V)]
  "TARGET_WORMHOLE"
{
  output_asm_insn("SFPSETCC\t0, %0, %1", operands);
  return "SFPNOP";
})

(define_expand "riscv_wormhole_sfpscmp_ex"
  [(unspec_volatile [(match_operand:SI    0 "address_operand"   "")
                     (match_operand:V64SF 1 "register_operand"  "")
                     (match_operand:SI    2 "nonmemory_operand" "")
                     (match_operand:SI    3 "immediate_operand" "")] UNSPECV_WORMHOLE_SFPSCMP_EX)]
  "TARGET_WORMHOLE"
{
  riscv_wormhole_sfpu_emit_sfpscmp_ex(operands[0], operands[1], operands[2], operands[3]);
  DONE;
})

(define_expand "riscv_wormhole_sfpvcmp_ex"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "")
                     (match_operand:V64SF 1 "register_operand"  "")
                     (match_operand:SI    2 "immediate_operand" "")] UNSPECV_WORMHOLE_SFPVCMP_EX)]
  "TARGET_WORMHOLE"
{
  riscv_wormhole_sfpu_emit_sfpvcmp_ex(operands[0], operands[1], operands[2]);
  DONE;
})

(define_insn "riscv_wormhole_sfpencc"
  [(unspec_volatile [(match_operand:SI 0 "immediate_operand" "n")
                     (match_operand:SI 1 "immediate_operand" "M")] UNSPECV_WORMHOLE_SFPENCC)]
  "TARGET_WORMHOLE"
{
  output_asm_insn("SFPENCC\t%0, %1", operands);
  return "SFPNOP";
})

(define_insn "riscv_wormhole_sfpcompc"
  [(unspec_volatile [(const_int 0)] UNSPECV_WORMHOLE_SFPCOMPC)]
  "TARGET_WORMHOLE"
{
  output_asm_insn("SFPCOMPC", operands);
  return "SFPNOP";
})

(define_insn "riscv_wormhole_sfppushc"
  [(unspec_volatile [(const_int 0)] UNSPECV_WORMHOLE_SFPPUSHC)]
  "TARGET_WORMHOLE"
  "SFPPUSHC")

(define_insn "riscv_wormhole_sfppopc"
  [(unspec_volatile [(const_int 0)] UNSPECV_WORMHOLE_SFPPOPC)]
  "TARGET_WORMHOLE"
{
  output_asm_insn("SFPPOPC", operands);
  return "SFPNOP";
})

(define_insn "riscv_wormhole_sfplut"
  [(set (match_operand:V64SF 0 "register_operand" "=Q3")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "Q0")
                          (match_operand:V64SF 2 "register_operand"  "Q1")
                          (match_operand:V64SF 3 "register_operand"  "Q2")
                          (match_operand:V64SF 4 "register_operand"  "0")
                          (match_operand:SI    5 "immediate_operand" "M")] UNSPECV_WORMHOLE_SFPLUT))]
  "TARGET_WORMHOLE"
{
  output_asm_insn("SFPLUT\t%0, %5", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_insn "riscv_wormhole_sfpnop"
  [(unspec_volatile [(const_int 0)] UNSPECV_WORMHOLE_SFPNOP)]
  "TARGET_WORMHOLE"
  "SFPNOP")

(define_insn "riscv_wormhole_sfpillegal"
  [(unspec_volatile [(const_int 0)] UNSPECV_WORMHOLE_SFPILLEGAL)]
  "TARGET_WORMHOLE"
  "SFPTOOMANYBOOLEANOPERATORS")

(define_insn "riscv_wormhole_sfpnonimm_dst"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:SI    1 "address_operand"   "r, r") ; instrn_buf_add
                          (match_operand:SI    2 "immediate_operand" "i, i") ; # nops
                          (match_operand:V64SF 3 "nonmemory_operand" "E, 0") ; live lreg_dst
                          (match_operand:SI    4 "immediate_operand" "i, i") ; op
                          (match_operand:SI    5 "immediate_operand" "i, i") ; dst shft
                          (match_operand:SI    6 "register_operand"  "r, r") ; insn_base
                                                                         ] UNSPECV_WORMHOLE_SFPNONIMM_DST))
        (clobber (match_scratch:SI 7 "=&r, &r"))]
  "TARGET_WORMHOLE"
{
  operands[4] = gen_rtx_CONST_INT(SImode, INTVAL(operands[4]) +
                                          (riscv_sfpu_regno(operands[0]) << INTVAL(operands[5])));
  output_asm_insn("li\t%7,%4", operands);
  output_asm_insn("add\t%7, %7, %6", operands);

  char lv[10];
  asm_fprintf(asm_out_file, "# Op(0x%x) %s d(%d)\n", UINTVAL(operands[4]) >> 24,
                            riscv_sfpu_lv_regno_str(lv, operands[3]), riscv_sfpu_regno(operands[0]));

  return riscv_wormhole_sfpu_output_nonimm_store_and_nops("sw\t%7,0(%1)", INTVAL(operands[2]), operands);
})

(define_insn "riscv_wormhole_sfpnonimm_dst_src"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:SI    1 "address_operand"   "r, r") ; instrn_buf_add
                          (match_operand:SI    2 "immediate_operand" "i, i") ; # nops
                          (match_operand:V64SF 3 "nonmemory_operand" "E, 0") ; live lreg_dst
                          (match_operand:V64SF 4 "register_operand"  "x, x") ; lreg_src
                          (match_operand:SI    5 "immediate_operand" "i, i") ; op
                          (match_operand:SI    6 "immediate_operand" "i, i") ; dst shft
                          (match_operand:SI    7 "immediate_operand" "i, i") ; src shft
                          (match_operand:SI    8 "register_operand"  "r, r") ; insn_base
                                                                         ] UNSPECV_WORMHOLE_SFPNONIMM_DST_SRC))
        (clobber (match_scratch:SI 9 "=&r, &r"))]
  "TARGET_WORMHOLE"
{
  operands[5] = gen_rtx_CONST_INT(SImode, INTVAL(operands[5]) +
                                          (riscv_sfpu_regno(operands[0]) << INTVAL(operands[6])) +
                                          (riscv_sfpu_regno(operands[4]) << INTVAL(operands[7])));
  output_asm_insn("li\t%9,%5", operands);
  output_asm_insn("add\t%9, %9, %8", operands);

  char lv[10];
  asm_fprintf(asm_out_file, "# Op(0x%x) %s d(%d) s(%d)\n", UINTVAL(operands[5]) >> 24,
                            riscv_sfpu_lv_regno_str(lv, operands[3]),
                            riscv_sfpu_regno(operands[0]), riscv_sfpu_regno(operands[4]));

  return riscv_wormhole_sfpu_output_nonimm_store_and_nops("sw\t%9,0(%1)", INTVAL(operands[2]), operands);
})

;;; Differentiate between src and store as store is used in the peephole un-optimization
(define_int_iterator wormhole_nonimm_srcstore [UNSPECV_WORMHOLE_SFPNONIMM_SRC UNSPECV_WORMHOLE_SFPNONIMM_STORE])
(define_int_attr wormhole_nonimm_srcstore_name [(UNSPECV_WORMHOLE_SFPNONIMM_SRC "src") (UNSPECV_WORMHOLE_SFPNONIMM_STORE "store")])

(define_insn "riscv_wormhole_sfpnonimm_<wormhole_nonimm_srcstore_name>"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "x")
                     (match_operand:SI    1 "address_operand"   "r") ; instrn_buf_add
                     (match_operand:SI    2 "immediate_operand" "i") ; # nops
                     (match_operand:SI    3 "immediate_operand" "i") ; op
                     (match_operand:SI    4 "immediate_operand" "i") ; dst shft
                     (match_operand:SI    5 "register_operand"  "r") ; insn_base
                                                                         ] wormhole_nonimm_srcstore)
            (clobber (match_scratch:SI    6 "=&r"))]
  "TARGET_WORMHOLE"
{
  operands[3] = gen_rtx_CONST_INT(SImode, INTVAL(operands[3]) +
                                          (riscv_sfpu_regno(operands[0]) << INTVAL(operands[4])));
  output_asm_insn("li\t%6,%3", operands);
  output_asm_insn("add\t%6, %6, %5", operands);

  asm_fprintf(asm_out_file, "# Op(0x%x) s(%d)\n", UINTVAL(operands[3]) >> 24, riscv_sfpu_regno(operands[0]));

  return riscv_wormhole_sfpu_output_nonimm_store_and_nops("sw\t%6,0(%1)", INTVAL(operands[2]), operands);
})

;; (include "sfpu-peephole.md")
