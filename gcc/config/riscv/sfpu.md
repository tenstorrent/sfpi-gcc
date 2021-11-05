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

; & in spec means early clobber, written before inputs are used, cannot reuse input reg

(define_c_enum "unspecv" [
  ;; Tenstorrent SFPU unspecs.
  ;; INT for internal
  ;; IMM for immediate
  ;; LV for keep dst reg alive as input for predicated liveness
  UNSPECV_SFPASSIGNLR
  UNSPECV_SFPASSIGNLR_INT
  UNSPECV_SFPKEEPALIVE
  UNSPECV_SFPLOAD
  UNSPECV_SFPLOAD_LV
  UNSPECV_SFPLOAD_INT
  UNSPECV_SFPLOADI
  UNSPECV_SFPLOADI_LV
  UNSPECV_SFPLOADI_INT
  UNSPECV_SFPSTORE
  UNSPECV_SFPSTORE_INT
  UNSPECV_SFPMULI
  UNSPECV_SFPADDI
  UNSPECV_SFPADDI_INT
  UNSPECV_SFPMUL
  UNSPECV_SFPMUL_LV
  UNSPECV_SFPADD
  UNSPECV_SFPADD_LV
  UNSPECV_SFPIADD_V
  UNSPECV_SFPIADD_I
  UNSPECV_SFPIADD_I_LV
  UNSPECV_SFPIADD_I_INT
  UNSPECV_SFPSHFT_V
  UNSPECV_SFPSHFT_I
  UNSPECV_SFPSHFT_I_INT
  UNSPECV_SFPABS
  UNSPECV_SFPABS_LV
  UNSPECV_SFPABS_INT
  UNSPECV_SFPAND
  UNSPECV_SFPOR
  UNSPECV_SFPNOT
  UNSPECV_SFPNOT_LV
  UNSPECV_SFPNOT_INT
  UNSPECV_SFPLZ
  UNSPECV_SFPLZ_LV
  UNSPECV_SFPLZ_INT
  UNSPECV_SFPSETMAN_V
  UNSPECV_SFPSETMAN_I
  UNSPECV_SFPSETMAN_I_LV
  UNSPECV_SFPSETMAN_I_INT
  UNSPECV_SFPSETEXP_V
  UNSPECV_SFPSETEXP_I
  UNSPECV_SFPSETEXP_I_LV
  UNSPECV_SFPSETEXP_I_INT
  UNSPECV_SFPSETSGN_V
  UNSPECV_SFPSETSGN_I
  UNSPECV_SFPSETSGN_I_LV
  UNSPECV_SFPSETSGN_I_INT
  UNSPECV_SFPMAD
  UNSPECV_SFPMAD_LV
  UNSPECV_SFPMAD_INT
  UNSPECV_SFPMOV
  UNSPECV_SFPMOV_LV
  UNSPECV_SFPMOV_INT
  UNSPECV_SFPDIVP2
  UNSPECV_SFPDIVP2_LV
  UNSPECV_SFPDIVP2_INT
  UNSPECV_SFPEXEXP
  UNSPECV_SFPEXEXP_LV
  UNSPECV_SFPEXEXP_INT
  UNSPECV_SFPEXMAN
  UNSPECV_SFPEXMAN_LV
  UNSPECV_SFPEXMAN_INT
  UNSPECV_SFPSETCC_I
  UNSPECV_SFPSETCC_V
  UNSPECV_SFPENCC
  UNSPECV_SFPCOMPC
  UNSPECV_SFPPUSHC
  UNSPECV_SFPPOPC
  UNSPECV_SFPLUT
  UNSPECV_SFPNOP
  UNSPECV_SFPILLEGAL
  UNSPECV_SFPNONIMM_DST
  UNSPECV_SFPNONIMM_DST_SRC
  UNSPECV_SFPNONIMM_SRC
  UNSPECV_SFPNONIMM_STORE
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
        error_at(INSN_LOCATION(insn), "Error: cannot load SFPU register (uninit var or non-inline function definition?)- exiting!");
      } else {
        error("Error: cannot load SFPU register (uninit var or non-inline function definition?) - exiting!");
      }
      return "SFPILLEGAL";

    case 2:
      if (INSN_HAS_LOCATION (insn)) {
        error_at(INSN_LOCATION(insn), "Error: cannot store SFPU register (spill or function invocation?) - exiting!");
      } else {
        error("Error: cannot store SFPU register (spill or function invocation?) - exiting!");
      }
      return "SFPILLEGAL";

    default:
      gcc_unreachable();
      break;
    }
  }
  [(set_attr "move_type" "fmove,fpload,fpstore")
   (set_attr "mode" "V64SF")])

(define_insn "*movv64sf_hardfloat_nocc"
  [(set (match_operand:V64SF 0 "nonimmediate_operand" "=x")
        (match_operand:V64SF 1 "move_operand"         " x"))]
  "TARGET_SFPU  &&
   (   register_operand (operands[0], V64SFmode)
    || reg_or_0_operand (operands[1], V64SFmode))"
  {
    output_asm_insn("SFPMOV\t%1, %0, 0", operands);
    output_asm_insn("SFPNOP", operands);
    return "SFPNOP";
  })

(define_expand "riscv_sfpassignlr"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI 1 "immediate_operand" "M")] UNSPECV_SFPASSIGNLR))]
  "TARGET_SFPU"
{
  int lregnum = INTVAL(operands[1]);
  SET_REGNO(operands[0], SFPU_REG_FIRST + lregnum);
  emit_insn(gen_riscv_sfpassignlr_int(operands[0]));
  DONE;
})

(define_insn "riscv_sfpassignlr_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(const_int 0)] UNSPECV_SFPASSIGNLR_INT))]
  "TARGET_SFPU"
  "")

(define_insn "riscv_sfpkeepalive"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand" "x")] UNSPECV_SFPKEEPALIVE)]
  "TARGET_SFPU"
  "")

(define_expand "riscv_sfpload"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI 1 "address_operand"  "")
                          (match_operand:SI 2 "immediate_operand" "")
                          (match_operand:SI 3 "nonmemory_operand" "")] UNSPECV_SFPLOAD))]
  "TARGET_SFPU"
{
  rtx live = riscv_sfpu_gen_const0_vector();
  riscv_sfpu_emit_sfpload(operands[0], live, operands[1], operands[2], operands[3]);
  DONE;
})

(define_expand "riscv_sfpload_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")
                          (match_operand:SI    4 "nonmemory_operand" "")] UNSPECV_SFPLOAD_LV))]
  "TARGET_SFPU"
{
  rtx live = operands[2];
  riscv_sfpu_emit_sfpload(operands[0], live, operands[1], operands[3], operands[4]);
  DONE;
})

(define_insn "riscv_sfpload_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:SI    2 "immediate_operand" "M, M")
                          (match_operand:SI    3 "immediate_operand" "N, N")] UNSPECV_SFPLOAD_INT))]
  "TARGET_SFPU"
  "@
   SFPLOAD\t%0, %2, %3
   SFPLOAD\t%0, %2, %3")


;;; SFPLOADI and SFPLOADI_LV
(define_expand "riscv_sfploadi"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec [(match_operand:SI 1 "address_operand"  "")
                          (match_operand:SI 2 "immediate_operand" "")
                          (match_operand:SI 3 "nonmemory_operand" "")] UNSPECV_SFPLOADI))]
  "TARGET_SFPU"
{
  rtx live = riscv_sfpu_gen_const0_vector();
  riscv_sfpu_emit_sfploadi(operands[0], live, operands[1], operands[2], operands[3]);
  DONE;
})

(define_expand "riscv_sfploadi_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec [(match_operand:SI    1 "address_operand"   "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")
                          (match_operand:SI    4 "nonmemory_operand" "")] UNSPECV_SFPLOADI_LV))]
  "TARGET_SFPU"
{
  rtx live = operands[2];
  riscv_sfpu_emit_sfploadi(operands[0], live, operands[1], operands[3], operands[4]);
  DONE;
})

(define_insn "riscv_sfploadi_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x,x,x,x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E,E,0,0")
                          (match_operand:SI    2 "immediate_operand" "M,M,M,M")
                          (match_operand:SI    3 "immediate_operand" "R,N,R,N")] UNSPECV_SFPLOADI_INT))]
  "TARGET_SFPU"
  "@
  SFPLOADI\t%0, %2, %s3
  SFPLOADI\t%0, %2, %u3
  SFPLOADI\t%0, %2, %s3
  SFPLOADI\t%0, %2, %u3")

(define_expand "riscv_sfpstore"
  [(unspec_volatile [(match_operand:SI    0 "address_operand"   "")
                     (match_operand:V64SF 1 "register_operand"  "")
                     (match_operand:SI    2 "immediate_operand" "")
                     (match_operand:SI    3 "nonmemory_operand" "")] UNSPECV_SFPSTORE)]
  "TARGET_SFPU"
{
  if (GET_CODE(operands[3]) == CONST_INT) {
    emit_insn (gen_riscv_sfpstore_int(operands[1], operands[2], operands[3]));
  } else {
    int base = TT_OP_SFPSTORE(0, INTVAL(operands[2]), 0);
    riscv_sfpu_emit_nonimm_store(operands[0], operands[1], 0, operands[3], base, 16, 16, 20);
  }
  DONE;
})

(define_insn "riscv_sfpstore_int"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "x")
                     (match_operand:SI    1 "immediate_operand" "M")
                     (match_operand:SI    2 "nonmemory_operand" "N")] UNSPECV_SFPSTORE_INT)]
  "TARGET_SFPU"
  "SFPSTORE\t%0, %1, %2")

(define_insn "riscv_sfpmuli"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:SI    2 "immediate_operand" "N")
                          (match_operand:SI    3 "immediate_operand" "M")] UNSPECV_SFPMULI))]
  "TARGET_SFPU"
{
  output_asm_insn("SFPMULI\t%2, %0, %3", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_expand "riscv_sfpaddi"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:SI    4 "immediate_operand" "")] UNSPECV_SFPADDI))]
  "TARGET_SFPU"
{
  if (GET_CODE(operands[3]) == CONST_INT) {
    emit_insn (gen_riscv_sfpaddi_int(operands[0], operands[2], operands[3], operands[4]));
  } else {
    int base = TT_OP_SFPADDI(0, 0, INTVAL(operands[4]));
    riscv_sfpu_emit_nonimm_dst(operands[1], operands[0], 2, operands[2], operands[3], base, 16, 8, 4);
  }
  DONE;
})

(define_insn "riscv_sfpaddi_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:SI    2 "nonmemory_operand" "N")
                          (match_operand:SI    3 "immediate_operand" "M")] UNSPECV_SFPADDI_INT))]
  "TARGET_SFPU"
{
  output_asm_insn("SFPADDI\t%2, %0, %3", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_expand "riscv_sfpdivp2"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:SI    2 "nonmemory_operand" "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:SI    4 "immediate_operand" "")] UNSPECV_SFPDIVP2))]
  "TARGET_SFPU"
{
  rtx live = riscv_sfpu_gen_const0_vector();
  riscv_sfpu_emit_sfpdivp2(operands[0], live, operands[1], operands[2], operands[3], operands[4]);
  DONE;
})

(define_expand "riscv_sfpdivp2_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:V64SF 4 "register_operand"  "")
                          (match_operand:SI    5 "immediate_operand" "")] UNSPECV_SFPDIVP2_LV))]
  "TARGET_SFPU"
{
  rtx live = operands[2];
  riscv_sfpu_emit_sfpdivp2(operands[0], live, operands[1], operands[3], operands[4], operands[5]);
  DONE;
})

(define_insn "riscv_sfpdivp2_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:SI    2 "immediate_operand" "P, P")
                          (match_operand:V64SF 3 "register_operand"  "x, x")
                          (match_operand:SI    4 "immediate_operand" "M, M")] UNSPECV_SFPDIVP2_INT))]
  "TARGET_SFPU"
{
  output_asm_insn("SFPDIVP2\t%2, %3, %0, %4", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_int_iterator simple_op
  [UNSPECV_SFPEXEXP
   UNSPECV_SFPEXMAN
   UNSPECV_SFPABS
   UNSPECV_SFPMOV
   UNSPECV_SFPLZ])
(define_int_attr simple_op_name
  [(UNSPECV_SFPEXEXP "exexp")
   (UNSPECV_SFPEXMAN "exman")
   (UNSPECV_SFPABS "abs")
   (UNSPECV_SFPMOV "mov")
   (UNSPECV_SFPLZ "lz")])
(define_int_iterator simple_op_lv
  [UNSPECV_SFPEXEXP_LV
   UNSPECV_SFPEXMAN_LV
   UNSPECV_SFPABS_LV
   UNSPECV_SFPMOV_LV
   UNSPECV_SFPLZ_LV])
(define_int_attr simple_op_name_lv
  [(UNSPECV_SFPEXEXP_LV "exexp")
   (UNSPECV_SFPEXMAN_LV "exman")
   (UNSPECV_SFPABS_LV "abs")
   (UNSPECV_SFPMOV_LV "mov")
   (UNSPECV_SFPLZ_LV "lz")])
(define_int_iterator simple_op_int
  [UNSPECV_SFPEXEXP_INT
   UNSPECV_SFPEXMAN_INT
   UNSPECV_SFPABS_INT
   UNSPECV_SFPMOV_INT
   UNSPECV_SFPLZ_INT])
(define_int_attr simple_op_name_int
  [(UNSPECV_SFPEXEXP_INT "exexp")
   (UNSPECV_SFPEXMAN_INT "exman")
   (UNSPECV_SFPABS_INT "abs")
   (UNSPECV_SFPMOV_INT "mov")
   (UNSPECV_SFPLZ_INT "lz")])
(define_int_attr simple_op_call_int
  [(UNSPECV_SFPEXEXP_INT "EXEXP")
   (UNSPECV_SFPEXMAN_INT "EXMAN")
   (UNSPECV_SFPABS_INT "ABS")
   (UNSPECV_SFPMOV_INT "MOV")
   (UNSPECV_SFPLZ_INT "LZ")])
(define_int_attr simple_op_id_int
  [(UNSPECV_SFPEXEXP_INT "UNSPECV_SFPEXEXP_INT")
   (UNSPECV_SFPEXMAN_INT "UNSPECV_SFPEXMAN_INT")
   (UNSPECV_SFPABS_INT "UNSPECV_SFPABS_INT")
   (UNSPECV_SFPMOV_INT "UNSPECV_SFPMOV_INT")
   (UNSPECV_SFPLZ_INT "UNSPECV_SFPLZ_INT")])

(define_expand "riscv_sfp<simple_op_name>"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:SI    2 "immediate_operand" "")] simple_op))]
  "TARGET_SFPU"
{
  rtx live = riscv_sfpu_gen_const0_vector();
  emit_insn (gen_riscv_sfp<simple_op_name>_int(operands[0], live, operands[1], operands[2]));
  DONE;
})

(define_expand "riscv_sfp<simple_op_name_lv>_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")] simple_op_lv))]
  "TARGET_SFPU"
{
  emit_insn (gen_riscv_sfp<simple_op_name_lv>_int(operands[0], operands[1], operands[2], operands[3]));
  DONE;
})

(define_insn "riscv_sfp<simple_op_name_int>_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")
                          (match_operand:SI    3 "immediate_operand" "M, M")] simple_op_int))]
  "TARGET_SFPU"
{
    output_asm_insn("SFP<simple_op_call_int>\t%2, %0, %3", operands);

    int mod1 = INTVAL(operands[3]);
    // EXEXP and LZ require 3 nops when setting the CC
    if (((<simple_op_id_int> == UNSPECV_SFPEXEXP_INT) &&
         (mod1 == 2 || mod1 == 3 || mod1 == 8 || mod1 == 9 || mod1 == 10 || mod1 == 11)) ||
        ((<simple_op_id_int> == UNSPECV_SFPLZ_INT) &&
         (mod1 == 2 || mod1 == 8 || mod1 == 10 || mod1 == 11))) {
      output_asm_insn("SFPNOP", operands);
    }

    output_asm_insn("SFPNOP", operands);

    return "SFPNOP";
})

(define_int_iterator muladd [UNSPECV_SFPMUL UNSPECV_SFPADD])
(define_int_attr muladd_name [(UNSPECV_SFPMUL "mul") (UNSPECV_SFPADD "add")])
(define_int_attr muladd_call [(UNSPECV_SFPMUL "MUL\t%1, %2, L4") (UNSPECV_SFPADD "ADD\tL10, %1, %2")])
(define_insn "riscv_sfp<muladd_name>"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M")] muladd))]
  "TARGET_SFPU"
{
  output_asm_insn("SFP<muladd_call>, %0, %3", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_int_iterator muladd_lv [UNSPECV_SFPMUL_LV UNSPECV_SFPADD_LV])
(define_int_attr muladd_name_lv [(UNSPECV_SFPMUL_LV "mul") (UNSPECV_SFPADD_LV "add")])
(define_int_attr muladd_call_lv [(UNSPECV_SFPMUL_LV "MUL\t%2, %3, L4") (UNSPECV_SFPADD_LV "ADD\tL10, %2, %3")])
(define_insn "riscv_sfp<muladd_name_lv>_lv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:V64SF 3 "register_operand"  "x")
                          (match_operand:SI    4 "immediate_operand" "M")] muladd_lv))]
  "TARGET_SFPU"
{
  output_asm_insn("SFP<muladd_call_lv>, %0, %4", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_insn "riscv_sfpiadd_v"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M")] UNSPECV_SFPIADD_V))]
  "TARGET_SFPU"
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

(define_expand "riscv_sfpiadd_i"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:SI    4 "immediate_operand" "")] UNSPECV_SFPIADD_I))]
  "TARGET_SFPU"
{
  rtx live = riscv_sfpu_gen_const0_vector();
  riscv_sfpu_emit_sfpiadd_i(operands[0], live, operands[1], operands[2], operands[3], operands[4]);
  DONE;
})

(define_expand "riscv_sfpiadd_i_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"   "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:SI    4 "nonmemory_operand" "")
                          (match_operand:SI    5 "immediate_operand" "")] UNSPECV_SFPIADD_I_LV))]
  "TARGET_SFPU"
{
  rtx live = operands[2];
  riscv_sfpu_emit_sfpiadd_i(operands[0], live, operands[1], operands[3], operands[4], operands[5]);
  DONE;
})

(define_insn "riscv_sfpiadd_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")
                          (match_operand:SI    3 "immediate_operand" "P, P")
                          (match_operand:SI    4 "immediate_operand" "M, M")] UNSPECV_SFPIADD_I_INT))]
  "TARGET_SFPU"
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

(define_insn "riscv_sfpshft_v"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_SFPSHFT_V))]
  "TARGET_SFPU"
{
  output_asm_insn("SFPSHFT\t0, %2, %0, 0", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_expand "riscv_sfpshft_i"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")] UNSPECV_SFPSHFT_I))]
  "TARGET_SFPU"
{
  if (GET_CODE(operands[3]) == CONST_INT) {
    emit_insn (gen_riscv_sfpshft_i_int(operands[0], operands[2], operands[3]));
  } else {
    int base = TT_OP_SFPSHFT(0, 0, 0, 1);
    riscv_sfpu_emit_nonimm_dst(operands[1], operands[0], 2, operands[2], operands[3], base, 20, 8, 4);
  }
  DONE;
})

(define_insn "riscv_sfpshft_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:SI    2 "nonmemory_operand" "P")] UNSPECV_SFPSHFT_I_INT))]
  "TARGET_SFPU"
{
  output_asm_insn("SFPSHFT\t%2, L0, %0, 1", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_insn "riscv_sfpand"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_SFPAND))]
  "TARGET_SFPU"
{
  output_asm_insn("SFPAND\t%2, %0", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_insn "riscv_sfpor"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_SFPOR))]
  "TARGET_SFPU"
{
  output_asm_insn("SFPOR\t%2, %0", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_expand "riscv_sfpnot"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")] UNSPECV_SFPNOT))]
  "TARGET_SFPU"
{
  rtx live = riscv_sfpu_gen_const0_vector();
  emit_insn (gen_riscv_sfpnot_int(operands[0], live, operands[1]));
  DONE;
})

(define_expand "riscv_sfpnot_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")] UNSPECV_SFPNOT_LV))]
  "TARGET_SFPU"
{
  rtx live = operands[1];
  emit_insn (gen_riscv_sfpnot_int(operands[0], live, operands[2]));
  DONE;
})

(define_insn "riscv_sfpnot_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")] UNSPECV_SFPNOT_INT))]
  "TARGET_SFPU"
{
  output_asm_insn("SFPNOT\t%2, %0", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_int_iterator set_float_op_v [UNSPECV_SFPSETEXP_V UNSPECV_SFPSETMAN_V UNSPECV_SFPSETSGN_V])
(define_int_attr set_float_name_v [(UNSPECV_SFPSETEXP_V "exp") (UNSPECV_SFPSETMAN_V "man") (UNSPECV_SFPSETSGN_V "sgn")])
(define_int_attr set_float_call_v [(UNSPECV_SFPSETEXP_V "EXP") (UNSPECV_SFPSETMAN_V "MAN") (UNSPECV_SFPSETSGN_V "SGN")])
(define_insn "riscv_sfpset<set_float_name_v>_v"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] set_float_op_v))]
  "TARGET_SFPU"
{
  output_asm_insn("SFPSET<set_float_call_v>\t0, %2, %0, 0", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_int_iterator set_float_op_i [UNSPECV_SFPSETEXP_I UNSPECV_SFPSETMAN_I UNSPECV_SFPSETSGN_I])
(define_int_attr set_float_name_i [(UNSPECV_SFPSETEXP_I "exp") (UNSPECV_SFPSETMAN_I "man") (UNSPECV_SFPSETSGN_I "sgn")])
(define_int_attr set_float_call_i [(UNSPECV_SFPSETEXP_I "EXP") (UNSPECV_SFPSETMAN_I "MAN") (UNSPECV_SFPSETSGN_I "SGN")])
(define_expand "riscv_sfpset<set_float_name_i>_i"
  [(set (match_operand:V64SF 0 "register_operand")
        (unspec_volatile [(match_operand:SI    1 "address_operand")
                          (match_operand:SI    2 "nonmemory_operand")
                          (match_operand:V64SF 3 "register_operand")] set_float_op_i))]
  "TARGET_SFPU"
{
  rtx live = riscv_sfpu_gen_const0_vector();
  if (GET_CODE(operands[2]) == CONST_INT) {
    emit_insn (gen_riscv_sfpset<set_float_name_i>_i_int(operands[0], live, operands[2], operands[3]));
  } else {
    int base = TT_OP_SFPSET<set_float_call_i>(0, 0, 0, 1);
    riscv_sfpu_emit_nonimm_dst_src(operands[1], operands[0], 2, live, operands[3], operands[2], base, 20, 8, 4, 8);
  }
  DONE;
})

(define_int_iterator set_float_op_i_lv [UNSPECV_SFPSETEXP_I_LV UNSPECV_SFPSETMAN_I_LV UNSPECV_SFPSETSGN_I_LV])
(define_int_attr set_float_name_i_lv [(UNSPECV_SFPSETEXP_I_LV "exp") (UNSPECV_SFPSETMAN_I_LV "man") (UNSPECV_SFPSETSGN_I_LV "sgn")])
(define_int_attr set_float_call_i_lv [(UNSPECV_SFPSETEXP_I_LV "EXP") (UNSPECV_SFPSETMAN_I_LV "MAN") (UNSPECV_SFPSETSGN_I_LV "SGN")])
(define_expand "riscv_sfpset<set_float_name_i_lv>_i_lv"
  [(set (match_operand:V64SF 0 "register_operand")
        (unspec_volatile [(match_operand:SI    1 "address_operand")
                          (match_operand:V64SF 2 "register_operand")
                          (match_operand:SI    3 "nonmemory_operand")
                          (match_operand:V64SF 4 "register_operand")] set_float_op_i_lv))]
  "TARGET_SFPU"
{
  rtx live = operands[2];
  if (GET_CODE(operands[3]) == CONST_INT) {
    emit_insn (gen_riscv_sfpset<set_float_name_i_lv>_i_int(operands[0], live, operands[3], operands[4]));
  } else {
    int base = TT_OP_SFPSET<set_float_call_i_lv>(0, 0, 0, 1);
    riscv_sfpu_emit_nonimm_dst_src(operands[1], operands[0], 2, live, operands[4], operands[3], base, 20, 8, 4, 8);
  }
  DONE;
})

(define_int_iterator set_float_op_i_int [UNSPECV_SFPSETEXP_I_INT UNSPECV_SFPSETMAN_I_INT UNSPECV_SFPSETSGN_I_INT])
(define_int_attr set_float_name_i_int [(UNSPECV_SFPSETEXP_I_INT "exp") (UNSPECV_SFPSETMAN_I_INT "man") (UNSPECV_SFPSETSGN_I_INT "sgn")])
(define_int_attr set_float_call_i_int [(UNSPECV_SFPSETEXP_I_INT "EXP") (UNSPECV_SFPSETMAN_I_INT "MAN") (UNSPECV_SFPSETSGN_I_INT "SGN")])
(define_insn "riscv_sfpset<set_float_name_i_int>_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:SI    2 "immediate_operand" "O, O")
                          (match_operand:V64SF 3 "register_operand"  "x, x")] set_float_op_i_int))]
  "TARGET_SFPU"
{
  output_asm_insn("SFPSET<set_float_call_i_int>\t%2, %3, %0, 1", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_expand "riscv_sfpmad"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:SI    4 "immediate_operand" "")] UNSPECV_SFPMAD))]
  "TARGET_SFPU"
{
  rtx live = riscv_sfpu_gen_const0_vector();
  emit_insn (gen_riscv_sfpmad_int(operands[0], live, operands[1], operands[2], operands[3], operands[4]));
  DONE;
})

(define_expand "riscv_sfpmad_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:V64SF 4 "register_operand"  "")
                          (match_operand:SI    5 "immediate_operand" "")] UNSPECV_SFPMAD_LV))]
  "TARGET_SFPU"
{
  rtx live = operands[1];
  emit_insn (gen_riscv_sfpmad_int(operands[0], live, operands[2], operands[3], operands[4], operands[5]));
  DONE;
})

(define_insn "riscv_sfpmad_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")
                          (match_operand:V64SF 3 "register_operand"  "x, x")
                          (match_operand:V64SF 4 "register_operand"  "x, x")
                          (match_operand:SI    5 "immediate_operand" "M, M")] UNSPECV_SFPMAD_INT))]
  "TARGET_SFPU"
{
  output_asm_insn("SFPMAD\t%2, %3, %4, %0, %5", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_insn "riscv_sfpsetcc_i"
  [(unspec_volatile [(match_operand:SI    0 "immediate_operand" "O")
                     (match_operand:SI    1 "immediate_operand" "M")] UNSPECV_SFPSETCC_I)]
  "TARGET_SFPU"
{
  output_asm_insn("SFPSETCC\t%0, L0, %1", operands);
  return "SFPNOP";
})

(define_insn "riscv_sfpsetcc_v"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "x")
                     (match_operand:SI    1 "immediate_operand" "M")] UNSPECV_SFPSETCC_V)]
  "TARGET_SFPU"
{
  output_asm_insn("SFPSETCC\t0, %0, %1", operands);
  return "SFPNOP";
})

(define_insn "riscv_sfpencc"
  [(unspec_volatile [(match_operand:SI 0 "immediate_operand" "O")
                     (match_operand:SI 1 "immediate_operand" "M")] UNSPECV_SFPENCC)]
  "TARGET_SFPU"
{
  output_asm_insn("SFPENCC\t%0, %1", operands);
  return "SFPNOP";
})

(define_insn "riscv_sfpcompc"
  [(unspec_volatile [(const_int 0)] UNSPECV_SFPCOMPC)]
  "TARGET_SFPU"
{
  output_asm_insn("SFPCOMPC", operands);
  return "SFPNOP";
})

(define_insn "riscv_sfppushc"
  [(unspec_volatile [(const_int 0)] UNSPECV_SFPPUSHC)]
  "TARGET_SFPU"
  "SFPPUSHC")

(define_insn "riscv_sfppopc"
  [(unspec_volatile [(const_int 0)] UNSPECV_SFPPOPC)]
  "TARGET_SFPU"
{
  output_asm_insn("SFPPOPC", operands);
  return "SFPNOP";
})

(define_insn "riscv_sfplut"
  [(set (match_operand:V64SF 0 "register_operand" "=Q3")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "Q0")
                          (match_operand:V64SF 2 "register_operand"  "Q1")
                          (match_operand:V64SF 3 "register_operand"  "Q2")
                          (match_operand:V64SF 4 "register_operand"  "0")
                          (match_operand:SI    5 "immediate_operand" "M")] UNSPECV_SFPLUT))]
  "TARGET_SFPU"
{
  output_asm_insn("SFPLUT\t%0, %5", operands);
  output_asm_insn("SFPNOP", operands);
  return "SFPNOP";
})

(define_insn "riscv_sfpnop"
  [(unspec_volatile [(const_int 0)] UNSPECV_SFPNOP)]
  "TARGET_SFPU"
  "SFPNOP")

(define_insn "riscv_sfpillegal"
  [(unspec_volatile [(const_int 0)] UNSPECV_SFPILLEGAL)]
  "TARGET_SFPU"
  "SFPTOOMANYBOOLEANOPERATORS")

(define_insn "riscv_sfpnonimm_dst"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:SI    1 "address_operand"   "r, r") ; instrn_buf_add
                          (match_operand:SI    2 "immediate_operand" "i, i") ; # nops
                          (match_operand:V64SF 3 "nonmemory_operand" "E, 0") ; live lreg_dst
                          (match_operand:SI    4 "immediate_operand" "i, i") ; op
                          (match_operand:SI    5 "immediate_operand" "i, i") ; dst shft
                          (match_operand:SI    6 "register_operand"  "r, r") ; insn_base
                                                                         ] UNSPECV_SFPNONIMM_DST))
        (clobber (match_scratch:SI 7 "=&r, &r"))]
  "TARGET_SFPU"
{
  operands[4] = gen_rtx_CONST_INT(SImode, INTVAL(operands[4]) +
                                          ((REGNO(operands[0]) - SFPU_REG_FIRST) << INTVAL(operands[5])));
  output_asm_insn("li\t%7,%4", operands);
  output_asm_insn("add\t%7, %7, %6", operands);
  return riscv_sfpu_output_nonimm_store_and_nops("sw\t%7,0(%1)", INTVAL(operands[2]), operands);
})

(define_insn "riscv_sfpnonimm_dst_src"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:SI    1 "address_operand"   "r, r") ; instrn_buf_add
                          (match_operand:SI    2 "immediate_operand" "i, i") ; # nops
                          (match_operand:V64SF 3 "nonmemory_operand" "E, 0") ; live lreg_dst
                          (match_operand:V64SF 4 "register_operand"  "x, x") ; lreg_src
                          (match_operand:SI    5 "immediate_operand" "i, i") ; op
                          (match_operand:SI    6 "immediate_operand" "i, i") ; dst shft
                          (match_operand:SI    7 "immediate_operand" "i, i") ; src shft
                          (match_operand:SI    8 "register_operand"  "r, r") ; insn_base
                                                                         ] UNSPECV_SFPNONIMM_DST_SRC))
        (clobber (match_scratch:SI 9 "=&r, &r"))]
  "TARGET_SFPU"
{
  operands[5] = gen_rtx_CONST_INT(SImode, INTVAL(operands[5]) +
                                          ((REGNO(operands[0]) - SFPU_REG_FIRST) << INTVAL(operands[6])) +
                                          ((REGNO(operands[4]) - SFPU_REG_FIRST) << INTVAL(operands[7])));
  output_asm_insn("li\t%9,%5", operands);
  output_asm_insn("add\t%9, %9, %8", operands);
  return riscv_sfpu_output_nonimm_store_and_nops("sw\t%9,0(%1)", INTVAL(operands[2]), operands);
})

;;; Differentiate between src and store as store is used in the peephole un-optimization
(define_int_iterator nonimm_srcstore [UNSPECV_SFPNONIMM_SRC UNSPECV_SFPNONIMM_STORE])
(define_int_attr nonimm_srcstore_name [(UNSPECV_SFPNONIMM_SRC "src") (UNSPECV_SFPNONIMM_STORE "store")])

(define_insn "riscv_sfpnonimm_<nonimm_srcstore_name>"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "x")
                     (match_operand:SI    1 "address_operand"   "r") ; instrn_buf_add
                     (match_operand:SI    2 "immediate_operand" "i") ; # nops
                     (match_operand:SI    3 "immediate_operand" "i") ; op
                     (match_operand:SI    4 "immediate_operand" "i") ; dst shft
                     (match_operand:SI    5 "register_operand"  "r") ; insn_base
                                                                         ] nonimm_srcstore)
            (clobber (match_scratch:SI    6 "=&r"))]
  "TARGET_SFPU"
{
  operands[3] = gen_rtx_CONST_INT(SImode, INTVAL(operands[3]) +
                                          ((REGNO(operands[0]) - SFPU_REG_FIRST) << INTVAL(operands[4])));
  output_asm_insn("li\t%6,%3", operands);
  output_asm_insn("add\t%6, %6, %5", operands);
  return riscv_sfpu_output_nonimm_store_and_nops("sw\t%6,0(%1)", INTVAL(operands[2]), operands);
})

;;; Peephole optimizations

; These are un-optimizations to ensure a NOP fits between a store/load
(define_peephole2
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "")
                     (match_operand:SI    1 "immediate_operand" "")
                     (match_operand:SI    2 "immediate_operand" "")] UNSPECV_SFPSTORE_INT)
   (set (match_operand:V64SF 3 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 4 "nonmemory_operand" "")
                          (match_operand:SI    5 "immediate_operand" "")
                          (match_operand:SI    6 "immediate_operand" "")] UNSPECV_SFPLOAD_INT))]
  "TARGET_SFPU"
  [(const_int 0)]
{
  emit_insn(gen_riscv_sfpstore_int(operands[0], operands[1], operands[2]));
  emit_insn(gen_riscv_sfpnop());
  emit_insn(gen_riscv_sfpload_int(operands[3], operands[4], operands[5], operands[6]));
})

(define_peephole2
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "")
                     (match_operand:SI    1 "address_operand"   "")
                     (match_operand:SI    2 "immediate_operand" "")
                     (match_operand:SI    3 "immediate_operand" "")
                     (match_operand:SI    4 "register_operand"  "")] UNSPECV_SFPNONIMM_STORE)
   (set (match_operand:V64SF 5 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 6 "nonmemory_operand" "")
                          (match_operand:SI    7 "immediate_operand" "")
                          (match_operand:SI    8 "immediate_operand" "")] UNSPECV_SFPLOAD_INT))
   (clobber (match_scratch:SI 9 ""))]
  "TARGET_SFPU"
  [(const_int 0)]
{
  emit_insn(gen_riscv_sfpnonimm_store(operands[0], operands[1], GEN_INT(1), operands[2], operands[3], operands[4]));
  emit_insn(gen_riscv_sfpload_int(operands[5], operands[6], operands[7], operands[8]));
})
