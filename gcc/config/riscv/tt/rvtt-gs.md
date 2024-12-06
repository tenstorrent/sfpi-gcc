;; Machine description for Tenstorrent SFPU Intrinsics.
;; Copyright (C) 2011-2020 Free Software Foundation, Inc.
;; Contributed by Paul Keller (pkeller@tenstorrent.com)
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
  ;; LV for keep dst reg alive as input for predicated liveness
  UNSPECV_GS_L1_LOAD_WAR
  UNSPECV_GS_SFPASSIGN_LV
  UNSPECV_GS_SFPPRESERVELREG
  UNSPECV_GS_SFPPRESERVELREG0_INT
  UNSPECV_GS_SFPPRESERVELREG1_INT
  UNSPECV_GS_SFPPRESERVELREG2_INT
  UNSPECV_GS_SFPPRESERVELREG3_INT
  UNSPECV_GS_SFPLOAD
  UNSPECV_GS_SFPLOAD_LV
  UNSPECV_GS_SFPLOAD_INT
  UNSPECV_GS_SFPXLOADI
  UNSPECV_GS_SFPXLOADI_LV
  UNSPECV_GS_SFPLOADI_INT
  UNSPECV_GS_SFPSTORE
  UNSPECV_GS_SFPSTORE_INT
  UNSPECV_GS_SFPMULI
  UNSPECV_GS_SFPMULI_INT
  UNSPECV_GS_SFPADDI
  UNSPECV_GS_SFPADDI_INT
  UNSPECV_GS_SFPMUL
  UNSPECV_GS_SFPMUL_LV
  UNSPECV_GS_SFPMUL_INT
  UNSPECV_GS_SFPADD
  UNSPECV_GS_SFPADD_LV
  UNSPECV_GS_SFPADD_INT
  UNSPECV_GS_SFPIADD_V_INT
  UNSPECV_GS_SFPXIADD_V
  UNSPECV_GS_SFPIADD_I
  UNSPECV_GS_SFPIADD_I_LV
  UNSPECV_GS_SFPXIADD_I
  UNSPECV_GS_SFPXIADD_I_LV
  UNSPECV_GS_SFPIADD_I_INT
  UNSPECV_GS_SFPSHFT_V
  UNSPECV_GS_SFPSHFT_I
  UNSPECV_GS_SFPSHFT_I_INT
  UNSPECV_GS_SFPABS
  UNSPECV_GS_SFPABS_LV
  UNSPECV_GS_SFPABS_INT
  UNSPECV_GS_SFPAND
  UNSPECV_GS_SFPOR
  UNSPECV_GS_SFPNOT
  UNSPECV_GS_SFPNOT_LV
  UNSPECV_GS_SFPNOT_INT
  UNSPECV_GS_SFPLZ
  UNSPECV_GS_SFPLZ_LV
  UNSPECV_GS_SFPLZ_INT
  UNSPECV_GS_SFPSETMAN_V
  UNSPECV_GS_SFPSETMAN_I
  UNSPECV_GS_SFPSETMAN_I_LV
  UNSPECV_GS_SFPSETMAN_I_INT
  UNSPECV_GS_SFPSETEXP_V
  UNSPECV_GS_SFPSETEXP_I
  UNSPECV_GS_SFPSETEXP_I_LV
  UNSPECV_GS_SFPSETEXP_I_INT
  UNSPECV_GS_SFPSETSGN_V
  UNSPECV_GS_SFPSETSGN_I
  UNSPECV_GS_SFPSETSGN_I_LV
  UNSPECV_GS_SFPSETSGN_I_INT
  UNSPECV_GS_SFPMAD
  UNSPECV_GS_SFPMAD_LV
  UNSPECV_GS_SFPMAD_INT
  UNSPECV_GS_SFPMOV
  UNSPECV_GS_SFPMOV_LV
  UNSPECV_GS_SFPMOV_INT
  UNSPECV_GS_SFPDIVP2
  UNSPECV_GS_SFPDIVP2_LV
  UNSPECV_GS_SFPDIVP2_INT
  UNSPECV_GS_SFPEXEXP
  UNSPECV_GS_SFPEXEXP_LV
  UNSPECV_GS_SFPEXEXP_INT
  UNSPECV_GS_SFPEXMAN
  UNSPECV_GS_SFPEXMAN_LV
  UNSPECV_GS_SFPEXMAN_INT
  UNSPECV_GS_SFPSETCC_I
  UNSPECV_GS_SFPSETCC_V
  UNSPECV_GS_SFPXFCMPS
  UNSPECV_GS_SFPXFCMPV
  UNSPECV_GS_SFPENCC
  UNSPECV_GS_SFPCOMPC
  UNSPECV_GS_SFPPUSHC
  UNSPECV_GS_SFPPOPC
  UNSPECV_GS_SFPLUT
  UNSPECV_GS_SFPNOP
])

(define_insn "rvtt_gs_l1_load_war"
  [(unspec_volatile [(match_operand:SI 0)] UNSPECV_GS_L1_LOAD_WAR)]
  "TARGET_RVTT_GS"
  "mv\t%0,%0\t# GS L1 war")

(define_insn "rvtt_gs_sfpgccmov_cc"
  [(set (match_operand:V64SF 0 "nonimmediate_operand" "=x,x,m")
        (match_operand:V64SF 1 "move_operand"         " x,m,x"))]
  "TARGET_RVTT_GS  &&
   (   register_operand (operands[0], V64SFmode)
    || reg_or_0_operand (operands[1], V64SFmode))"
  {
    switch (which_alternative) {
    case 0:
      // Note: must re-enable all elements until we know if we are in a predicated state
      output_asm_insn("SFPPUSHC", operands);
      output_asm_insn("SFPENCC\t3, 2", operands);
      output_asm_insn("TTNOP", operands);
      output_asm_insn("SFPMOV\t%0, %1, 0", operands);
      output_asm_insn("TTNOP", operands);
      output_asm_insn("TTNOP", operands);
      output_asm_insn("SFPPOPC", operands);
      return "TTNOP";
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
   (set_attr "mode" "V64SF")])

(define_insn "rvtt_gs_sfpassign_lv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_GS_SFPASSIGN_LV))]
  "TARGET_RVTT_GS"
{
    output_asm_insn("SFPMOV\t%0, %2, 0", operands);
    output_asm_insn("TTNOP", operands);
    return "TTNOP";
})

(define_expand "rvtt_gs_sfppreservelreg"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "")
                     (match_operand:SI    1 "immediate_operand" "M04U")] UNSPECV_GS_SFPPRESERVELREG)]

  "TARGET_RVTT_GS"
{
  static rtx (*fn_ptr[4])(rtx) = {gen_rvtt_gs_sfppreservelreg0_int, gen_rvtt_gs_sfppreservelreg1_int,
                                  gen_rvtt_gs_sfppreservelreg2_int, gen_rvtt_gs_sfppreservelreg3_int};
  emit_insn(fn_ptr[INTVAL(operands[1])](operands[0]));
  DONE;
})

(define_int_iterator preservelreg_int
 [UNSPECV_GS_SFPPRESERVELREG0_INT UNSPECV_GS_SFPPRESERVELREG1_INT
 UNSPECV_GS_SFPPRESERVELREG2_INT UNSPECV_GS_SFPPRESERVELREG3_INT])
(define_int_attr preservelreg_int_name
 [(UNSPECV_GS_SFPPRESERVELREG0_INT "0") (UNSPECV_GS_SFPPRESERVELREG1_INT "1")
  (UNSPECV_GS_SFPPRESERVELREG2_INT "2") (UNSPECV_GS_SFPPRESERVELREG3_INT "3")])
(define_insn "rvtt_gs_sfppreservelreg<preservelreg_int_name>_int"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand" "Q<preservelreg_int_name>")] preservelreg_int)]
  "TARGET_RVTT_GS"
  "")

(define_expand "rvtt_gs_sfpload"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI 1 "address_operand"  "")
                          (match_operand:SI 2 "immediate_operand" "")
                          (match_operand:SI 3 "nonmemory_operand" "")
                          (match_operand:SI 4 "register_operand" "")
                          (match_operand:SI 5 "immediate_operand" "")] UNSPECV_GS_SFPLOAD))]
  "TARGET_RVTT_GS"
{
  rtx live = rvtt_gen_const0_vector();
  rvtt_gs_emit_sfpload(operands[0], live, operands[1], operands[2], operands[3], operands[4], operands[5]);
  DONE;
})

(define_expand "rvtt_gs_sfpload_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")
                          (match_operand:SI    4 "nonmemory_operand" "")
                          (match_operand:SI    5 "register_operand" "")
                          (match_operand:SI    6 "immediate_operand" "")] UNSPECV_GS_SFPLOAD_LV))]
  "TARGET_RVTT_GS"
{
  rtx live = operands[2];
  rvtt_gs_emit_sfpload(operands[0], live, operands[1], operands[3], operands[4], operands[5], operands[6]);
  DONE;
})

(define_insn "rvtt_gs_sfpload_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:SI    2 "immediate_operand" "M04U, M04U")
                          (match_operand:SI    3 "immediate_operand" "M16U, M16U")] UNSPECV_GS_SFPLOAD_INT))]
  "TARGET_RVTT_GS"
  "@
   SFPLOAD\t%0, %3, %2
   SFPLOAD\t%0, %3, %2")


(define_expand "rvtt_gs_sfpxloadi"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec [(match_operand:SI 1 "address_operand"  "")
                          (match_operand:SI 2 "immediate_operand" "")
                          (match_operand:SI 3 "nonmemory_operand" "")
                          (match_operand:SI 4 "register_operand"  "")
                          (match_operand:SI 5 "immediate_operand" "")] UNSPECV_GS_SFPXLOADI))]
  "TARGET_RVTT_GS"
{
  rtx live = rvtt_gen_const0_vector();
  rvtt_gs_emit_sfpxloadi(operands[0], live, operands[1], operands[2], operands[3], operands[4], operands[5]);
  DONE;
})

(define_expand "rvtt_gs_sfpxloadi_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec [(match_operand:SI    1 "address_operand"   "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")
                          (match_operand:SI    4 "nonmemory_operand" "")
                          (match_operand:SI    5 "register_operand"  "")
                          (match_operand:SI    6 "immediate_operand" "")] UNSPECV_GS_SFPXLOADI_LV))]
  "TARGET_RVTT_GS"
{
  rtx live = operands[2];
  rvtt_gs_emit_sfpxloadi(operands[0], live, operands[1], operands[3], operands[4], operands[5], operands[6]);
  DONE;
})

(define_insn "rvtt_gs_sfploadi_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x,x,x,x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E,E,0,0")
                          (match_operand:SI    2 "immediate_operand" "M04U,M04U,M04U,M04U")
                          (match_operand:SI    3 "immediate_operand" "M16S,M16U,M16S,M16U")] UNSPECV_GS_SFPLOADI_INT))]
  "TARGET_RVTT_GS"
  "@
  SFPLOADI\t%0, %s3, %2
  SFPLOADI\t%0, %u3, %2
  SFPLOADI\t%0, %s3, %2
  SFPLOADI\t%0, %u3, %2")

(define_expand "rvtt_gs_sfpstore"
  [(unspec_volatile [(match_operand:SI    0 "address_operand"   "")
                     (match_operand:V64SF 1 "register_operand"  "")
                     (match_operand:SI    2 "immediate_operand" "")
                     (match_operand:SI    3 "nonmemory_operand" "")
                     (match_operand:SI    4 "register_operand"  "")
                     (match_operand:SI    5 "immediate_operand" "")] UNSPECV_GS_SFPSTORE)]
  "TARGET_RVTT_GS"
{
  if (GET_CODE(operands[3]) == CONST_INT) {
    emit_insn (gen_rvtt_gs_sfpstore_int(operands[1], operands[2],
                                         rvtt_clamp_unsigned(operands[3], 0x7FFF)));
  } else {
    unsigned long int op = TT_OP_GS_SFPSTORE(0, INTVAL(operands[2]), 0);
    emit_insn (gen_rvtt_sfpnonimm_store(operands[1], operands[0], GEN_INT(INSN_SCHED_DYN), GEN_INT(20), operands[4], GEN_INT(op), operands[5]));
  }
  DONE;
})

(define_insn "rvtt_gs_sfpstore_int"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "x")
                     (match_operand:SI    1 "immediate_operand" "M04U")
                     (match_operand:SI    2 "nonmemory_operand" "M16U")] UNSPECV_GS_SFPSTORE_INT)]
  "TARGET_RVTT_GS"
  "SFPSTORE\t%2, %0, %1")


(define_int_iterator muliaddi [UNSPECV_GS_SFPMULI UNSPECV_GS_SFPADDI])
(define_int_attr muliaddi_name [(UNSPECV_GS_SFPMULI "muli") (UNSPECV_GS_SFPADDI "addi")])
(define_int_attr muliaddi_call [(UNSPECV_GS_SFPMULI "MULI") (UNSPECV_GS_SFPADDI "ADDI")])
(define_expand "rvtt_gs_sfp<muliaddi_name>"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:SI    4 "register_operand"  "")
                          (match_operand:SI    5 "immediate_operand" "")
                          (match_operand:SI    6 "immediate_operand" "")] muliaddi))]
  "TARGET_RVTT_GS"
{
  if (GET_CODE(operands[3]) == CONST_INT) {
    emit_insn (gen_rvtt_gs_sfp<muliaddi_name>_int(operands[0], operands[2],
               rvtt_clamp_unsigned(operands[3], 0xFFFF), operands[6]));
  } else {
    unsigned long int op = TT_OP_GS_SFP<muliaddi_call>(0, 0, INTVAL(operands[6]));
    emit_insn (gen_rvtt_sfpnonimm_dst(operands[0], operands[1], GEN_INT(2), operands[2], GEN_INT(4), operands[4], GEN_INT(op), operands[5]));
  }
  DONE;
})

(define_int_iterator muliaddi_int [UNSPECV_GS_SFPMULI_INT UNSPECV_GS_SFPADDI_INT])
(define_int_attr muliaddi_int_name [(UNSPECV_GS_SFPMULI_INT "muli") (UNSPECV_GS_SFPADDI_INT "addi")])
(define_int_attr muliaddi_int_call [(UNSPECV_GS_SFPMULI_INT "MULI") (UNSPECV_GS_SFPADDI_INT "ADDI")])
(define_insn "rvtt_gs_sfp<muliaddi_int_name>_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:SI    2 "immediate_operand" "M16U")
                          (match_operand:SI    3 "immediate_operand" "M04U")] muliaddi_int))]
  "TARGET_RVTT_GS"
{
  output_asm_insn("SFP<muliaddi_int_call>\t%0, %2, %3", operands);
  output_asm_insn("TTNOP", operands);
  return "TTNOP";
})

(define_expand "rvtt_gs_sfpdivp2"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:SI    2 "nonmemory_operand" "")
                          (match_operand:SI    3 "register_operand"  "")
                          (match_operand:SI    4 "immediate_operand" "")
                          (match_operand:V64SF 5 "register_operand"  "")
                          (match_operand:SI    6 "immediate_operand" "")] UNSPECV_GS_SFPDIVP2))]
  "TARGET_RVTT_GS"
{
  rtx live = rvtt_gen_const0_vector();
  rvtt_gs_emit_sfpdivp2(operands[0], live, operands[1], operands[2], operands[5], operands[6], operands[3], operands[4]);
  DONE;
})

(define_expand "rvtt_gs_sfpdivp2_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:SI    4 "register_operand"  "")
                          (match_operand:SI    5 "immediate_operand" "")
                          (match_operand:V64SF 6 "register_operand"  "")
                          (match_operand:SI    7 "immediate_operand" "")] UNSPECV_GS_SFPDIVP2_LV))]
  "TARGET_RVTT_GS"
{
  rtx live = operands[2];
  rvtt_gs_emit_sfpdivp2(operands[0], live, operands[1], operands[3], operands[6], operands[7], operands[4], operands[5]);
  DONE;
})

(define_insn "rvtt_gs_sfpdivp2_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:SI    2 "immediate_operand" "n, n")
                          (match_operand:V64SF 3 "register_operand"  "x, x")
                          (match_operand:SI    4 "immediate_operand" "M04U, M04U")] UNSPECV_GS_SFPDIVP2_INT))]
  "TARGET_RVTT_GS"
{
  output_asm_insn("SFPDIVP2\t%0, %3, %2, %4", operands);
  output_asm_insn("TTNOP", operands);
  return "TTNOP";
})

(define_int_iterator simple_op
  [UNSPECV_GS_SFPEXEXP
   UNSPECV_GS_SFPEXMAN
   UNSPECV_GS_SFPABS
   UNSPECV_GS_SFPMOV
   UNSPECV_GS_SFPLZ])
(define_int_attr simple_op_name
  [(UNSPECV_GS_SFPEXEXP "exexp")
   (UNSPECV_GS_SFPEXMAN "exman")
   (UNSPECV_GS_SFPABS "abs")
   (UNSPECV_GS_SFPMOV "mov")
   (UNSPECV_GS_SFPLZ "lz")])
(define_int_iterator simple_op_lv
  [UNSPECV_GS_SFPEXEXP_LV
   UNSPECV_GS_SFPEXMAN_LV
   UNSPECV_GS_SFPABS_LV
   UNSPECV_GS_SFPMOV_LV
   UNSPECV_GS_SFPLZ_LV])
(define_int_attr simple_op_name_lv
  [(UNSPECV_GS_SFPEXEXP_LV "exexp")
   (UNSPECV_GS_SFPEXMAN_LV "exman")
   (UNSPECV_GS_SFPABS_LV "abs")
   (UNSPECV_GS_SFPMOV_LV "mov")
   (UNSPECV_GS_SFPLZ_LV "lz")])
(define_int_iterator simple_op_int
  [UNSPECV_GS_SFPEXEXP_INT
   UNSPECV_GS_SFPEXMAN_INT
   UNSPECV_GS_SFPABS_INT
   UNSPECV_GS_SFPMOV_INT
   UNSPECV_GS_SFPLZ_INT])
(define_int_attr simple_op_name_int
  [(UNSPECV_GS_SFPEXEXP_INT "exexp")
   (UNSPECV_GS_SFPEXMAN_INT "exman")
   (UNSPECV_GS_SFPABS_INT "abs")
   (UNSPECV_GS_SFPMOV_INT "mov")
   (UNSPECV_GS_SFPLZ_INT "lz")])
(define_int_attr simple_op_call_int
  [(UNSPECV_GS_SFPEXEXP_INT "EXEXP")
   (UNSPECV_GS_SFPEXMAN_INT "EXMAN")
   (UNSPECV_GS_SFPABS_INT "ABS")
   (UNSPECV_GS_SFPMOV_INT "MOV")
   (UNSPECV_GS_SFPLZ_INT "LZ")])
(define_int_attr simple_op_id_int
  [(UNSPECV_GS_SFPEXEXP_INT "UNSPECV_GS_SFPEXEXP_INT")
   (UNSPECV_GS_SFPEXMAN_INT "UNSPECV_GS_SFPEXMAN_INT")
   (UNSPECV_GS_SFPABS_INT "UNSPECV_GS_SFPABS_INT")
   (UNSPECV_GS_SFPMOV_INT "UNSPECV_GS_SFPMOV_INT")
   (UNSPECV_GS_SFPLZ_INT "UNSPECV_GS_SFPLZ_INT")])

(define_expand "rvtt_gs_sfp<simple_op_name>"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:SI    2 "immediate_operand" "")] simple_op))]
  "TARGET_RVTT_GS"
{
  rtx live = rvtt_gen_const0_vector();
  emit_insn (gen_rvtt_gs_sfp<simple_op_name>_int(operands[0], live, operands[1], operands[2]));
  DONE;
})

(define_expand "rvtt_gs_sfp<simple_op_name_lv>_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")] simple_op_lv))]
  "TARGET_RVTT_GS"
{
  emit_insn (gen_rvtt_gs_sfp<simple_op_name_lv>_int(operands[0], operands[1], operands[2], operands[3]));
  DONE;
})

(define_insn "rvtt_gs_sfp<simple_op_name_int>_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")
                          (match_operand:SI    3 "immediate_operand" "M04U, M04U")] simple_op_int))]
  "TARGET_RVTT_GS"
{
    output_asm_insn("SFP<simple_op_call_int>\t%0, %2, %3", operands);

    int mod1 = INTVAL(operands[3]);
    // EXEXP and LZ require 3 nops when setting the CC
    if (((<simple_op_id_int> == UNSPECV_GS_SFPEXEXP_INT) &&
         (mod1 == 2 || mod1 == 3 || mod1 == 8 || mod1 == 9 || mod1 == 10 || mod1 == 11)) ||
        ((<simple_op_id_int> == UNSPECV_GS_SFPLZ_INT) &&
         (mod1 == 2 || mod1 == 8 || mod1 == 10 || mod1 == 11))) {
      output_asm_insn("TTNOP", operands);
    }

    output_asm_insn("TTNOP", operands);

    return "TTNOP";
})

(define_int_iterator muladd [UNSPECV_GS_SFPMUL UNSPECV_GS_SFPADD])
(define_int_attr muladd_name [(UNSPECV_GS_SFPMUL "mul") (UNSPECV_GS_SFPADD "add")])
(define_expand "rvtt_gs_sfp<muladd_name>"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")] muladd))]
  "TARGET_RVTT_GS"
{
  rtx live = rvtt_gen_const0_vector();
  emit_insn (gen_rvtt_gs_sfp<muladd_name>_int(operands[0], live, operands[1], operands[2], operands[3]));
  DONE;
})

(define_int_iterator muladd_lv [UNSPECV_GS_SFPMUL_LV UNSPECV_GS_SFPADD_LV])
(define_int_attr muladd_name_lv [(UNSPECV_GS_SFPMUL_LV "mul") (UNSPECV_GS_SFPADD_LV "add")])
(define_expand "rvtt_gs_sfp<muladd_name_lv>_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:SI    4 "immediate_operand" "")] muladd_lv))]
  "TARGET_RVTT_GS"
{
  rtx live = operands[1];
  emit_insn (gen_rvtt_gs_sfp<muladd_name_lv>_int(operands[0], live, operands[2], operands[3], operands[4]));
  DONE;
})

(define_int_iterator muladd_int [UNSPECV_GS_SFPMUL_INT UNSPECV_GS_SFPADD_INT])
(define_int_attr muladd_name_int [(UNSPECV_GS_SFPMUL_INT "mul") (UNSPECV_GS_SFPADD_INT "add")])
(define_int_attr muladd_call_int [(UNSPECV_GS_SFPMUL_INT "MUL\t%0, %2, %3, L4") (UNSPECV_GS_SFPADD_INT "ADD\t%0, L10, %2, %3")])
(define_insn "rvtt_gs_sfp<muladd_name_int>_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")
                          (match_operand:V64SF 3 "register_operand"  "x, x")
                          (match_operand:SI    4 "immediate_operand" "M04U, M04U")] muladd_int))]
  "TARGET_RVTT_GS"
{
  output_asm_insn("SFP<muladd_call_int>, %4", operands);
  output_asm_insn("TTNOP", operands);
  return "TTNOP";
})

(define_insn "rvtt_gs_sfpiadd_v_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M04U")] UNSPECV_GS_SFPIADD_V_INT))]
  "TARGET_RVTT_GS"
{
  output_asm_insn("SFPIADD\t%0, %2, 0, %3", operands);
  output_asm_insn("TTNOP", operands);

  int mod1 = INTVAL(operands[3]);
  // Careful - this includes a few "reserved" values
  if (mod1 < 3 || mod1 > 7) {
    output_asm_insn("TTNOP", operands);
  }
  return "TTNOP";
})

(define_insn "rvtt_gs_sfpiadd_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")
                          (match_operand:SI    3 "immediate_operand" "n, n")
                          (match_operand:SI    4 "immediate_operand" "M04U, M04U")] UNSPECV_GS_SFPIADD_I_INT))]
  "TARGET_RVTT_GS"
{
  output_asm_insn("SFPIADD\t%0, %2, %3, %4", operands);
  output_asm_insn("TTNOP", operands);

  int mod1 = INTVAL(operands[4]);
  // Careful - this includes a few "reserved" values
  if (mod1 < 3 || mod1 > 7) {
    output_asm_insn("TTNOP", operands);
  }
  return "TTNOP";
})

(define_expand "rvtt_gs_sfpxiadd_v"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M04U")] UNSPECV_GS_SFPXIADD_V))]
  "TARGET_RVTT_GS"
{
  rvtt_gs_emit_sfpxiadd_v(operands[0], operands[1], operands[2], operands[3]);
  DONE;
})


(define_expand "rvtt_gs_sfpxiadd_i"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:SI    4 "immediate_operand" "")
                          (match_operand:SI    5 "nonmemory_operand" "")
                          (match_operand:SI    6 "immediate_operand" "")] UNSPECV_GS_SFPXIADD_I))]
  "TARGET_RVTT_GS"
{
  rtx live = rvtt_gen_const0_vector();
  rvtt_gs_emit_sfpxiadd_i(operands[0], live, operands[1], operands[2], operands[3], operands[6]);
  DONE;
})

(define_expand "rvtt_gs_sfpxiadd_i_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"   "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:SI    4 "nonmemory_operand" "")
                          (match_operand:SI    5 "immediate_operand" "")
                          (match_operand:SI    6 "nonmemory_operand" "")
                          (match_operand:SI    7 "immediate_operand" "")] UNSPECV_GS_SFPXIADD_I_LV))]
  "TARGET_RVTT_GS"
{
  rtx live = operands[2];
  rvtt_gs_emit_sfpxiadd_i(operands[0], live, operands[1], operands[3], operands[4], operands[7]);
  DONE;
})

(define_insn "rvtt_gs_sfpshft_v"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_GS_SFPSHFT_V))]
  "TARGET_RVTT_GS"
{
  output_asm_insn("SFPSHFT\t%0, %2, 0, 0", operands);
  output_asm_insn("TTNOP", operands);
  return "TTNOP";
})

(define_expand "rvtt_gs_sfpshft_i"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:SI    4 "register_operand" "")
                          (match_operand:SI    5 "immediate_operand" "")] UNSPECV_GS_SFPSHFT_I))]
  "TARGET_RVTT_GS"
{
  if (GET_CODE(operands[3]) == CONST_INT) {
    emit_insn (gen_rvtt_gs_sfpshft_i_int(operands[0], operands[2], rvtt_clamp_signed(operands[3], 0x7FF)));
  } else {
    unsigned long int op = TT_OP_GS_SFPSHFT(0, 0, 0, 1);
    emit_insn (gen_rvtt_sfpnonimm_dst(operands[0], operands[1], GEN_INT(2), operands[2], GEN_INT(4), operands[4], GEN_INT(op), operands[5]));
  }
  DONE;
})

(define_insn "rvtt_gs_sfpshft_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:SI    2 "immediate_operand" "M12S")] UNSPECV_GS_SFPSHFT_I_INT))]
  "TARGET_RVTT_GS"
{
  output_asm_insn("SFPSHFT\t%0, L0, %2, 1", operands);
  output_asm_insn("TTNOP", operands);
  return "TTNOP";
})

(define_insn "rvtt_gs_sfpand"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_GS_SFPAND))]
  "TARGET_RVTT_GS"
{
  output_asm_insn("SFPAND\t%0, %2", operands);
  output_asm_insn("TTNOP", operands);
  return "TTNOP";
})

(define_insn "rvtt_gs_sfpor"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_GS_SFPOR))]
  "TARGET_RVTT_GS"
{
  output_asm_insn("SFPOR\t%0, %2", operands);
  output_asm_insn("TTNOP", operands);
  return "TTNOP";
})

(define_expand "rvtt_gs_sfpnot"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")] UNSPECV_GS_SFPNOT))]
  "TARGET_RVTT_GS"
{
  rtx live = rvtt_gen_const0_vector();
  emit_insn (gen_rvtt_gs_sfpnot_int(operands[0], live, operands[1]));
  DONE;
})

(define_expand "rvtt_gs_sfpnot_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")] UNSPECV_GS_SFPNOT_LV))]
  "TARGET_RVTT_GS"
{
  rtx live = operands[1];
  emit_insn (gen_rvtt_gs_sfpnot_int(operands[0], live, operands[2]));
  DONE;
})

(define_insn "rvtt_gs_sfpnot_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")] UNSPECV_GS_SFPNOT_INT))]
  "TARGET_RVTT_GS"
{
  output_asm_insn("SFPNOT\t%0, %2", operands);
  output_asm_insn("TTNOP", operands);
  return "TTNOP";
})

(define_int_iterator set_float_op_v [UNSPECV_GS_SFPSETEXP_V UNSPECV_GS_SFPSETMAN_V UNSPECV_GS_SFPSETSGN_V])
(define_int_attr set_float_name_v [(UNSPECV_GS_SFPSETEXP_V "exp") (UNSPECV_GS_SFPSETMAN_V "man") (UNSPECV_GS_SFPSETSGN_V "sgn")])
(define_int_attr set_float_call_v [(UNSPECV_GS_SFPSETEXP_V "EXP") (UNSPECV_GS_SFPSETMAN_V "MAN") (UNSPECV_GS_SFPSETSGN_V "SGN")])
(define_insn "rvtt_gs_sfpset<set_float_name_v>_v"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] set_float_op_v))]
  "TARGET_RVTT_GS"
{
  output_asm_insn("SFPSET<set_float_call_v>\t%0, %2, 0, 0", operands);
  output_asm_insn("TTNOP", operands);
  return "TTNOP";
})

(define_int_iterator set_float_op_i [UNSPECV_GS_SFPSETEXP_I UNSPECV_GS_SFPSETMAN_I UNSPECV_GS_SFPSETSGN_I])
(define_int_attr set_float_name_i [(UNSPECV_GS_SFPSETEXP_I "exp") (UNSPECV_GS_SFPSETMAN_I "man") (UNSPECV_GS_SFPSETSGN_I "sgn")])
(define_int_attr set_float_call_i [(UNSPECV_GS_SFPSETEXP_I "EXP") (UNSPECV_GS_SFPSETMAN_I "MAN") (UNSPECV_GS_SFPSETSGN_I "SGN")])
(define_expand "rvtt_gs_sfpset<set_float_name_i>_i"
  [(set (match_operand:V64SF 0 "register_operand")
        (unspec_volatile [(match_operand:SI    1 "address_operand")
                          (match_operand:SI    2 "nonmemory_operand")
                          (match_operand:SI    3 "register_operand")
                          (match_operand:SI    4 "immediate_operand")
                          (match_operand:V64SF 5 "register_operand")] set_float_op_i))]
  "TARGET_RVTT_GS"
{
  rtx live = rvtt_gen_const0_vector();
  if (GET_CODE(operands[2]) == CONST_INT) {
    emit_insn (gen_rvtt_gs_sfpset<set_float_name_i>_i_int(operands[0], live,
                                                        rvtt_clamp_unsigned(operands[2], 0xFFF), operands[5]));
  } else {
    unsigned long int op = TT_OP_GS_SFPSET<set_float_call_i>(0, 0, 0, 1);
    emit_insn (gen_rvtt_sfpnonimm_dst_src(operands[0], operands[1], GEN_INT(2), live, operands[5], GEN_INT(4), GEN_INT(8), operands[3], GEN_INT(op), operands[4]));
  }
  DONE;
})

(define_int_iterator set_float_op_i_lv [UNSPECV_GS_SFPSETEXP_I_LV UNSPECV_GS_SFPSETMAN_I_LV UNSPECV_GS_SFPSETSGN_I_LV])
(define_int_attr set_float_name_i_lv [(UNSPECV_GS_SFPSETEXP_I_LV "exp") (UNSPECV_GS_SFPSETMAN_I_LV "man") (UNSPECV_GS_SFPSETSGN_I_LV "sgn")])
(define_int_attr set_float_call_i_lv [(UNSPECV_GS_SFPSETEXP_I_LV "EXP") (UNSPECV_GS_SFPSETMAN_I_LV "MAN") (UNSPECV_GS_SFPSETSGN_I_LV "SGN")])
(define_expand "rvtt_gs_sfpset<set_float_name_i_lv>_i_lv"
  [(set (match_operand:V64SF 0 "register_operand")
        (unspec_volatile [(match_operand:SI    1 "address_operand")
                          (match_operand:V64SF 2 "register_operand")
                          (match_operand:SI    3 "nonmemory_operand")
                          (match_operand:SI    4 "register_operand")
                          (match_operand:SI    5 "immediate_operand")
                          (match_operand:V64SF 6 "register_operand")] set_float_op_i_lv))]
  "TARGET_RVTT_GS"
{
  rtx live = operands[2];
  if (GET_CODE(operands[3]) == CONST_INT) {
    emit_insn (gen_rvtt_gs_sfpset<set_float_name_i_lv>_i_int(operands[0], live,
                                                           rvtt_clamp_unsigned(operands[3], 0xFFF), operands[6]));
  } else {
    unsigned long int op = TT_OP_GS_SFPSET<set_float_call_i_lv>(0, 0, 0, 1);
    emit_insn (gen_rvtt_sfpnonimm_dst_src(operands[0], operands[1], GEN_INT(4),
               live, operands[6], GEN_INT(4), GEN_INT(8), operands[4], GEN_INT(op), operands[5]));
  }
  DONE;
})

(define_int_iterator set_float_op_i_int [UNSPECV_GS_SFPSETEXP_I_INT UNSPECV_GS_SFPSETMAN_I_INT UNSPECV_GS_SFPSETSGN_I_INT])
(define_int_attr set_float_name_i_int [(UNSPECV_GS_SFPSETEXP_I_INT "exp") (UNSPECV_GS_SFPSETMAN_I_INT "man") (UNSPECV_GS_SFPSETSGN_I_INT "sgn")])
(define_int_attr set_float_call_i_int [(UNSPECV_GS_SFPSETEXP_I_INT "EXP") (UNSPECV_GS_SFPSETMAN_I_INT "MAN") (UNSPECV_GS_SFPSETSGN_I_INT "SGN")])
(define_insn "rvtt_gs_sfpset<set_float_name_i_int>_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:SI    2 "immediate_operand" "n, n")
                          (match_operand:V64SF 3 "register_operand"  "x, x")] set_float_op_i_int))]
  "TARGET_RVTT_GS"
{
  output_asm_insn("SFPSET<set_float_call_i_int>\t%0, %3, %2, 1", operands);
  output_asm_insn("TTNOP", operands);
  return "TTNOP";
})

(define_expand "rvtt_gs_sfpmad"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:SI    4 "immediate_operand" "")] UNSPECV_GS_SFPMAD))]
  "TARGET_RVTT_GS"
{
  rtx live = rvtt_gen_const0_vector();
  emit_insn (gen_rvtt_gs_sfpmad_int(operands[0], live, operands[1], operands[2], operands[3], operands[4]));
  DONE;
})

(define_expand "rvtt_gs_sfpmad_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:V64SF 4 "register_operand"  "")
                          (match_operand:SI    5 "immediate_operand" "")] UNSPECV_GS_SFPMAD_LV))]
  "TARGET_RVTT_GS"
{
  rtx live = operands[1];
  emit_insn (gen_rvtt_gs_sfpmad_int(operands[0], live, operands[2], operands[3], operands[4], operands[5]));
  DONE;
})

(define_insn "rvtt_gs_sfpmad_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")
                          (match_operand:V64SF 3 "register_operand"  "x, x")
                          (match_operand:V64SF 4 "register_operand"  "x, x")
                          (match_operand:SI    5 "immediate_operand" "M04U, M04U")] UNSPECV_GS_SFPMAD_INT))]
  "TARGET_RVTT_GS"
{
  output_asm_insn("SFPMAD\t%0, %2, %3, %4, %5", operands);
  output_asm_insn("TTNOP", operands);
  return "TTNOP";
})

(define_insn "rvtt_gs_sfpsetcc_i"
  [(unspec_volatile [(match_operand:SI    0 "immediate_operand" "M01U")
                     (match_operand:SI    1 "immediate_operand" "M04U")] UNSPECV_GS_SFPSETCC_I)]
  "TARGET_RVTT_GS"
{
  output_asm_insn("SFPSETCC\tL0, %0, %1", operands);
  return "TTNOP";
})

(define_insn "rvtt_gs_sfpsetcc_v"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "x")
                     (match_operand:SI    1 "immediate_operand" "M04U")] UNSPECV_GS_SFPSETCC_V)]
  "TARGET_RVTT_GS"
{
  output_asm_insn("SFPSETCC\t%0, 0, %1", operands);
  return "TTNOP";
})

(define_expand "rvtt_gs_sfpxfcmps"
  [(set (match_operand:SI 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"   "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:SI    4 "immediate_operand" "")
                          (match_operand:SI    5 "nonmemory_operand" "")
                          (match_operand:SI    6 "immediate_operand" "")] UNSPECV_GS_SFPXFCMPS))]
  "TARGET_RVTT_GS"
{
  rvtt_gs_emit_sfpxfcmps(operands[1], operands[2], operands[3], operands[6]);
  DONE;
})

(define_expand "rvtt_gs_sfpxfcmpv"
  [(set (match_operand:SI 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")] UNSPECV_GS_SFPXFCMPV))]
  "TARGET_RVTT_GS"
{
  rvtt_gs_emit_sfpxfcmpv(operands[1], operands[2], operands[3]);
  DONE;
})

(define_insn "rvtt_gs_sfpencc"
  [(unspec_volatile [(match_operand:SI 0 "immediate_operand" "M02U")
                     (match_operand:SI 1 "immediate_operand" "M04U")] UNSPECV_GS_SFPENCC)]
  "TARGET_RVTT_GS"
{
  output_asm_insn("SFPENCC\t%0, %1", operands);
  return "TTNOP";
})

(define_insn "rvtt_gs_sfpcompc"
  [(unspec_volatile [(const_int 0)] UNSPECV_GS_SFPCOMPC)]
  "TARGET_RVTT_GS"
{
  output_asm_insn("SFPCOMPC", operands);
  return "TTNOP";
})

(define_insn "rvtt_gs_sfppushc"
  [(unspec_volatile [(const_int 0)] UNSPECV_GS_SFPPUSHC)]
  "TARGET_RVTT_GS"
  "SFPPUSHC")

(define_insn "rvtt_gs_sfppopc"
  [(unspec_volatile [(const_int 0)] UNSPECV_GS_SFPPOPC)]
  "TARGET_RVTT_GS"
{
  output_asm_insn("SFPPOPC", operands);
  return "TTNOP";
})

(define_insn "rvtt_gs_sfplut"
  [(set (match_operand:V64SF 0 "register_operand" "=Q3")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "Q0")
                          (match_operand:V64SF 2 "register_operand"  "Q1")
                          (match_operand:V64SF 3 "register_operand"  "Q2")
                          (match_operand:V64SF 4 "register_operand"  "0")
                          (match_operand:SI    5 "immediate_operand" "M04U")] UNSPECV_GS_SFPLUT))]
  "TARGET_RVTT_GS"
{
  output_asm_insn("SFPLUT\t%0, %5", operands);
  output_asm_insn("TTNOP", operands);
  return "TTNOP";
})

(define_insn "rvtt_gs_sfpnop"
  [(unspec_volatile [(const_int 0)] UNSPECV_GS_SFPNOP)]
  "TARGET_RVTT_GS"
  "TTNOP")

(include "tt/rvtt-peephole-gs.md")
