;; Machine description for Tenstorrent SFPU Wormhole Intrinsics.
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
  UNSPECV_WH_SFPASSIGN_LV
  UNSPECV_WH_SFPPRESERVELREG
  UNSPECV_WH_SFPPRESERVELREG0_INT
  UNSPECV_WH_SFPPRESERVELREG1_INT
  UNSPECV_WH_SFPPRESERVELREG2_INT
  UNSPECV_WH_SFPPRESERVELREG3_INT
  UNSPECV_WH_SFPPRESERVELREG4_INT
  UNSPECV_WH_SFPPRESERVELREG5_INT
  UNSPECV_WH_SFPPRESERVELREG6_INT
  UNSPECV_WH_SFPPRESERVELREG7_INT
  UNSPECV_WH_SFPLOAD
  UNSPECV_WH_SFPLOAD_LV
  UNSPECV_WH_SFPLOAD_INT
  UNSPECV_WH_SFPXLOADI
  UNSPECV_WH_SFPXLOADI_LV
  UNSPECV_WH_SFPLOADI_INT
  UNSPECV_WH_SFPSTORE
  UNSPECV_WH_SFPSTORE_INT
  UNSPECV_WH_SFPMULI
  UNSPECV_WH_SFPMULI_INT
  UNSPECV_WH_SFPADDI
  UNSPECV_WH_SFPADDI_INT
  UNSPECV_WH_SFPMUL
  UNSPECV_WH_SFPMUL_LV
  UNSPECV_WH_SFPMUL_INT
  UNSPECV_WH_SFPADD
  UNSPECV_WH_SFPADD_LV
  UNSPECV_WH_SFPADD_INT
  UNSPECV_WH_SFPIADD_V_INT
  UNSPECV_WH_SFPXIADD_V
  UNSPECV_WH_SFPIADD_I
  UNSPECV_WH_SFPIADD_I_LV
  UNSPECV_WH_SFPXIADD_I
  UNSPECV_WH_SFPXIADD_I_LV
  UNSPECV_WH_SFPIADD_I_INT
  UNSPECV_WH_SFPSHFT_V
  UNSPECV_WH_SFPSHFT_I
  UNSPECV_WH_SFPSHFT_I_INT
  UNSPECV_WH_SFPABS
  UNSPECV_WH_SFPABS_LV
  UNSPECV_WH_SFPABS_INT
  UNSPECV_WH_SFPAND
  UNSPECV_WH_SFPOR
  UNSPECV_WH_SFPXOR
  UNSPECV_WH_SFPNOT
  UNSPECV_WH_SFPNOT_LV
  UNSPECV_WH_SFPNOT_INT
  UNSPECV_WH_SFPLZ
  UNSPECV_WH_SFPLZ_LV
  UNSPECV_WH_SFPLZ_INT
  UNSPECV_WH_SFPSETMAN_V
  UNSPECV_WH_SFPSETMAN_I
  UNSPECV_WH_SFPSETMAN_I_LV
  UNSPECV_WH_SFPSETMAN_I_INT
  UNSPECV_WH_SFPSETEXP_V
  UNSPECV_WH_SFPSETEXP_I
  UNSPECV_WH_SFPSETEXP_I_LV
  UNSPECV_WH_SFPSETEXP_I_INT
  UNSPECV_WH_SFPSETSGN_V
  UNSPECV_WH_SFPSETSGN_I
  UNSPECV_WH_SFPSETSGN_I_LV
  UNSPECV_WH_SFPSETSGN_I_INT
  UNSPECV_WH_SFPMAD
  UNSPECV_WH_SFPMAD_LV
  UNSPECV_WH_SFPMAD_INT
  UNSPECV_WH_SFPMOV
  UNSPECV_WH_SFPMOV_LV
  UNSPECV_WH_SFPMOV_INT
  UNSPECV_WH_SFPDIVP2
  UNSPECV_WH_SFPDIVP2_LV
  UNSPECV_WH_SFPDIVP2_INT
  UNSPECV_WH_SFPEXEXP
  UNSPECV_WH_SFPEXEXP_LV
  UNSPECV_WH_SFPEXEXP_INT
  UNSPECV_WH_SFPEXMAN
  UNSPECV_WH_SFPEXMAN_LV
  UNSPECV_WH_SFPEXMAN_INT
  UNSPECV_WH_SFPSETCC_I
  UNSPECV_WH_SFPSETCC_V
  UNSPECV_WH_SFPXFCMPS
  UNSPECV_WH_SFPXFCMPV
  UNSPECV_WH_SFPENCC
  UNSPECV_WH_SFPCOMPC
  UNSPECV_WH_SFPPUSHC
  UNSPECV_WH_SFPPOPC
  UNSPECV_WH_SFPCAST
  UNSPECV_WH_SFPCAST_LV
  UNSPECV_WH_SFPCAST_INT
  UNSPECV_WH_SFPSHFT2_E
  UNSPECV_WH_SFPSHFT2_E_LV
  UNSPECV_WH_SFPSHFT2_E_INT
  UNSPECV_WH_SFPSTOCHRND_I
  UNSPECV_WH_SFPSTOCHRND_I_LV
  UNSPECV_WH_SFPSTOCHRND_I_INT
  UNSPECV_WH_SFPSTOCHRND_V
  UNSPECV_WH_SFPSTOCHRND_V_LV
  UNSPECV_WH_SFPSTOCHRND_V_INT
  UNSPECV_WH_SFPLUT
  UNSPECV_WH_SFPLUTFP32_3R
  UNSPECV_WH_SFPLUTFP32_6R
  UNSPECV_WH_SFPCONFIG_V
  UNSPECV_WH_SFPSWAP
  UNSPECV_WH_SFPSWAP_INT
  UNSPECV_WH_SFPTRANSP
  UNSPECV_WH_SFPSHFT2_G
  UNSPECV_WH_SFPSHFT2_GE
  UNSPECV_WH_SFPNOP
])

(define_insn "rvtt_wh_sfpgccmov_cc"
  [(set (match_operand:V64SF 0 "nonimmediate_operand" "=x,x,m")
        (match_operand:V64SF 1 "move_operand"         " x,m,x"))]
  "TARGET_RVTT_WH  &&
   (   register_operand (operands[0], V64SFmode)
    || reg_or_0_operand (operands[1], V64SFmode))"
  {
    switch (which_alternative) {
    case 0:
      // Note: must re-enable all elements until we know if we are in a predicated state
      return "SFPMOV\t%0, %1, 2";
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

(define_insn "rvtt_wh_sfpassign_lv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_WH_SFPASSIGN_LV))]
  "TARGET_RVTT_WH"
  "SFPMOV\t%0, %2, 0"
)

(define_expand "rvtt_wh_sfppreservelreg"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "")
                     (match_operand:SI    1 "immediate_operand" "M04U")] UNSPECV_WH_SFPPRESERVELREG)]

  "TARGET_RVTT_WH"
{
  static rtx (*fn_ptr[8])(rtx) = {gen_rvtt_wh_sfppreservelreg0_int, gen_rvtt_wh_sfppreservelreg1_int,
                                  gen_rvtt_wh_sfppreservelreg2_int, gen_rvtt_wh_sfppreservelreg3_int,
                                  gen_rvtt_wh_sfppreservelreg4_int, gen_rvtt_wh_sfppreservelreg5_int,
                                  gen_rvtt_wh_sfppreservelreg6_int, gen_rvtt_wh_sfppreservelreg7_int};
  emit_insn(fn_ptr[INTVAL(operands[1])](operands[0]));
  DONE;
})

(define_int_iterator wormhole_preservelreg_int
 [UNSPECV_WH_SFPPRESERVELREG0_INT UNSPECV_WH_SFPPRESERVELREG1_INT
  UNSPECV_WH_SFPPRESERVELREG2_INT UNSPECV_WH_SFPPRESERVELREG3_INT
  UNSPECV_WH_SFPPRESERVELREG4_INT UNSPECV_WH_SFPPRESERVELREG5_INT
  UNSPECV_WH_SFPPRESERVELREG6_INT UNSPECV_WH_SFPPRESERVELREG7_INT])
(define_int_attr wormhole_preservelreg_int_name
 [(UNSPECV_WH_SFPPRESERVELREG0_INT "0") (UNSPECV_WH_SFPPRESERVELREG1_INT "1")
  (UNSPECV_WH_SFPPRESERVELREG2_INT "2") (UNSPECV_WH_SFPPRESERVELREG3_INT "3")
  (UNSPECV_WH_SFPPRESERVELREG4_INT "4") (UNSPECV_WH_SFPPRESERVELREG5_INT "5")
  (UNSPECV_WH_SFPPRESERVELREG6_INT "6") (UNSPECV_WH_SFPPRESERVELREG7_INT "7")])
(define_insn "rvtt_wh_sfppreservelreg<wormhole_preservelreg_int_name>_int"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand" "Q<wormhole_preservelreg_int_name>")] wormhole_preservelreg_int)]
  "TARGET_RVTT_WH"
  "")

(define_expand "rvtt_wh_sfpload"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI 1 "address_operand"  "")
                          (match_operand:SI 2 "immediate_operand" "")
                          (match_operand:SI 3 "immediate_operand" "")
                          (match_operand:SI 4 "nonmemory_operand" "")
                          (match_operand:SI 5 "register_operand" "")
                          (match_operand:SI 6 "immediate_operand" "")] UNSPECV_WH_SFPLOAD))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_gen_const0_vector();
  rvtt_wh_emit_sfpload(operands[0], live, operands[1], operands[2], operands[3], operands[4], operands[5], operands[6]);
  DONE;
})

(define_expand "rvtt_wh_sfpload_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")
                          (match_operand:SI    4 "immediate_operand" "")
                          (match_operand:SI    5 "nonmemory_operand" "")
                          (match_operand:SI    6 "register_operand" "")
                          (match_operand:SI    7 "immediate_operand" "")] UNSPECV_WH_SFPLOAD_LV))]
  "TARGET_RVTT_WH"
{
  rtx live = operands[2];
  rvtt_wh_emit_sfpload(operands[0], live, operands[1], operands[3], operands[4], operands[5], operands[6], operands[7]);
  DONE;
})

(define_insn "rvtt_wh_sfpload_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:SI    2 "immediate_operand" "M04U, M04U")
                          (match_operand:SI    3 "immediate_operand" "M02U, M02U")
                          (match_operand:SI    4 "immediate_operand" "M14U, M14U")] UNSPECV_WH_SFPLOAD_INT))]
  "TARGET_RVTT_WH"
  "@
   SFPLOAD\t%0, %4, %2, %3
   SFPLOAD\t%0, %4, %2, %3")


;;; SFPLOADI and SFPLOADI_LV
(define_expand "rvtt_wh_sfpxloadi"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec [(match_operand:SI 1 "address_operand"  "")
                          (match_operand:SI 2 "immediate_operand" "")
                          (match_operand:SI 3 "nonmemory_operand" "")
                          (match_operand:SI 4 "register_operand"  "")
                          (match_operand:SI 5 "immediate_operand" "")] UNSPECV_WH_SFPXLOADI))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_gen_const0_vector();
  rvtt_wh_emit_sfpxloadi(operands[0], live, operands[1], operands[2], operands[3], operands[4], operands[5]);
  DONE;
})

(define_expand "rvtt_wh_sfpxloadi_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec [(match_operand:SI    1 "address_operand"   "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")
                          (match_operand:SI    4 "nonmemory_operand" "")
                          (match_operand:SI    5 "register_operand"  "")
                          (match_operand:SI    6 "immediate_operand" "")] UNSPECV_WH_SFPXLOADI_LV))]
  "TARGET_RVTT_WH"
{
  rtx live = operands[2];
  rvtt_wh_emit_sfpxloadi(operands[0], live, operands[1], operands[3], operands[4], operands[5], operands[6]);
  DONE;
})

(define_insn "rvtt_wh_sfploadi_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x,x,x,x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E,E,0,0")
                          (match_operand:SI    2 "immediate_operand" "M04U,M04U,M04U,M04U")
                          (match_operand:SI    3 "immediate_operand" "M16S,M16U,M16S,M16U")] UNSPECV_WH_SFPLOADI_INT))]
  "TARGET_RVTT_WH"
  "@
  SFPLOADI\t%0, %s3, %2
  SFPLOADI\t%0, %u3, %2
  SFPLOADI\t%0, %s3, %2
  SFPLOADI\t%0, %u3, %2")

(define_expand "rvtt_wh_sfpstore"
  [(unspec_volatile [(match_operand:SI    0 "address_operand"   "")
                     (match_operand:V64SF 1 "register_operand"  "")
                     (match_operand:SI    2 "immediate_operand" "")
                     (match_operand:SI    3 "immediate_operand" "")
                     (match_operand:SI    4 "nonmemory_operand" "")
                     (match_operand:SI    5 "register_operand" "")
                     (match_operand:SI    6 "immediate_operand" "")] UNSPECV_WH_SFPSTORE)]
  "TARGET_RVTT_WH"
{
  if (GET_CODE(operands[4]) == CONST_INT) {
    emit_insn (gen_rvtt_wh_sfpstore_int(operands[1], operands[2],operands[3],
                                         rvtt_clamp_unsigned(operands[4], 0x3FFF)));
  } else {
    unsigned long int op = TT_OP_WH_SFPSTORE(0, INTVAL(operands[2]), INTVAL(operands[3]), 0);
    emit_insn (gen_rvtt_sfpnonimm_store(operands[1], operands[0], GEN_INT(0), GEN_INT(20), operands[5], GEN_INT(op), operands[6]));
  }
  DONE;
})

(define_insn "rvtt_wh_sfpstore_int"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "x")
                     (match_operand:SI    1 "immediate_operand" "M04U")
                     (match_operand:SI    2 "immediate_operand" "M02U")
                     (match_operand:SI    3 "nonmemory_operand" "M14U")] UNSPECV_WH_SFPSTORE_INT)]
  "TARGET_RVTT_WH"
  "SFPSTORE\t%3, %0, %1, %2")


(define_int_iterator wormhole_muliaddi [UNSPECV_WH_SFPMULI UNSPECV_WH_SFPADDI])
(define_int_attr wormhole_muliaddi_name [(UNSPECV_WH_SFPMULI "muli") (UNSPECV_WH_SFPADDI "addi")])
(define_int_attr wormhole_muliaddi_call [(UNSPECV_WH_SFPMULI "MULI") (UNSPECV_WH_SFPADDI "ADDI")])
(define_expand "rvtt_wh_sfp<wormhole_muliaddi_name>"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:SI    4 "register_operand"  "")
                          (match_operand:SI    5 "immediate_operand" "")
                          (match_operand:SI    6 "immediate_operand" "")] wormhole_muliaddi))]
  "TARGET_RVTT_WH"
{
  if (GET_CODE(operands[3]) == CONST_INT) {
    emit_insn (gen_rvtt_wh_sfp<wormhole_muliaddi_name>_int(operands[0], operands[2],
               rvtt_clamp_unsigned(operands[3], 0xFFFF), operands[6]));
  } else {
    unsigned long int op = TT_OP_WH_SFP<wormhole_muliaddi_call>(0, 0, INTVAL(operands[6]));
    emit_insn (gen_rvtt_sfpnonimm_dst(operands[0], operands[1], GEN_INT(INSN_SCHED_DYN), operands[2], GEN_INT(4), operands[4], GEN_INT(op), operands[5]));
  }
  DONE;
})

(define_int_iterator wormhole_muliaddi_int [UNSPECV_WH_SFPMULI_INT UNSPECV_WH_SFPADDI_INT])
(define_int_attr wormhole_muliaddi_int_name [(UNSPECV_WH_SFPMULI_INT "muli") (UNSPECV_WH_SFPADDI_INT "addi")])
(define_int_attr wormhole_muliaddi_int_call [(UNSPECV_WH_SFPMULI_INT "MULI") (UNSPECV_WH_SFPADDI_INT "ADDI")])
(define_insn "rvtt_wh_sfp<wormhole_muliaddi_int_name>_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:SI    2 "immediate_operand" "M16U")
                          (match_operand:SI    3 "immediate_operand" "M04U")] wormhole_muliaddi_int))]
  "TARGET_RVTT_WH"
  "SFP<wormhole_muliaddi_int_call>\t%0, %2, %3"
)

(define_expand "rvtt_wh_sfpdivp2"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:SI    2 "nonmemory_operand" "")
                          (match_operand:SI    3 "register_operand"  "")
                          (match_operand:SI    4 "immediate_operand" "")
                          (match_operand:V64SF 5 "register_operand"  "")
                          (match_operand:SI    6 "immediate_operand" "")] UNSPECV_WH_SFPDIVP2))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_gen_const0_vector();
  rvtt_wh_emit_sfpdivp2(operands[0], live, operands[1], operands[2], operands[5], operands[6], operands[3], operands[4]);
  DONE;
})

(define_expand "rvtt_wh_sfpdivp2_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:SI    4 "register_operand"  "")
                          (match_operand:SI    5 "immediate_operand" "")
                          (match_operand:V64SF 6 "register_operand"  "")
                          (match_operand:SI    7 "immediate_operand" "")] UNSPECV_WH_SFPDIVP2_LV))]
  "TARGET_RVTT_WH"
{
  rtx live = operands[2];
  rvtt_wh_emit_sfpdivp2(operands[0], live, operands[1], operands[3], operands[6], operands[7], operands[4], operands[5]);
  DONE;
})

(define_insn "rvtt_wh_sfpdivp2_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:SI    2 "immediate_operand" "M12S, M12S")
                          (match_operand:V64SF 3 "register_operand"  "x, x")
                          (match_operand:SI    4 "immediate_operand" "M04U, M04U")] UNSPECV_WH_SFPDIVP2_INT))]
  "TARGET_RVTT_WH"
  "SFPDIVP2\t%0, %3, %2, %4"
)

(define_int_iterator wormhole_simple_op
  [UNSPECV_WH_SFPEXEXP
   UNSPECV_WH_SFPEXMAN
   UNSPECV_WH_SFPABS
   UNSPECV_WH_SFPMOV
   UNSPECV_WH_SFPLZ])
(define_int_attr wormhole_simple_op_name
  [(UNSPECV_WH_SFPEXEXP "exexp")
   (UNSPECV_WH_SFPEXMAN "exman")
   (UNSPECV_WH_SFPABS "abs")
   (UNSPECV_WH_SFPMOV "mov")
   (UNSPECV_WH_SFPLZ "lz")])
(define_int_iterator wormhole_simple_op_lv
  [UNSPECV_WH_SFPEXEXP_LV
   UNSPECV_WH_SFPEXMAN_LV
   UNSPECV_WH_SFPABS_LV
   UNSPECV_WH_SFPMOV_LV
   UNSPECV_WH_SFPLZ_LV])
(define_int_attr wormhole_simple_op_name_lv
  [(UNSPECV_WH_SFPEXEXP_LV "exexp")
   (UNSPECV_WH_SFPEXMAN_LV "exman")
   (UNSPECV_WH_SFPABS_LV "abs")
   (UNSPECV_WH_SFPMOV_LV "mov")
   (UNSPECV_WH_SFPLZ_LV "lz")])
(define_int_iterator wormhole_simple_op_int
  [UNSPECV_WH_SFPEXEXP_INT
   UNSPECV_WH_SFPEXMAN_INT
   UNSPECV_WH_SFPABS_INT
   UNSPECV_WH_SFPMOV_INT
   UNSPECV_WH_SFPLZ_INT])
(define_int_attr wormhole_simple_op_name_int
  [(UNSPECV_WH_SFPEXEXP_INT "exexp")
   (UNSPECV_WH_SFPEXMAN_INT "exman")
   (UNSPECV_WH_SFPABS_INT "abs")
   (UNSPECV_WH_SFPMOV_INT "mov")
   (UNSPECV_WH_SFPLZ_INT "lz")])
(define_int_attr wormhole_simple_op_call_int
  [(UNSPECV_WH_SFPEXEXP_INT "EXEXP")
   (UNSPECV_WH_SFPEXMAN_INT "EXMAN")
   (UNSPECV_WH_SFPABS_INT "ABS")
   (UNSPECV_WH_SFPMOV_INT "MOV")
   (UNSPECV_WH_SFPLZ_INT "LZ")])
(define_int_attr wormhole_simple_op_id_int
  [(UNSPECV_WH_SFPEXEXP_INT "UNSPECV_WH_SFPEXEXP_INT")
   (UNSPECV_WH_SFPEXMAN_INT "UNSPECV_WH_SFPEXMAN_INT")
   (UNSPECV_WH_SFPABS_INT "UNSPECV_WH_SFPABS_INT")
   (UNSPECV_WH_SFPMOV_INT "UNSPECV_WH_SFPMOV_INT")
   (UNSPECV_WH_SFPLZ_INT "UNSPECV_WH_SFPLZ_INT")])

(define_expand "rvtt_wh_sfp<wormhole_simple_op_name>"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:SI    2 "immediate_operand" "")] wormhole_simple_op))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_gen_const0_vector();
  emit_insn (gen_rvtt_wh_sfp<wormhole_simple_op_name>_int(operands[0], live, operands[1], operands[2]));
  DONE;
})

(define_expand "rvtt_wh_sfp<wormhole_simple_op_name_lv>_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")] wormhole_simple_op_lv))]
  "TARGET_RVTT_WH"
{
  emit_insn (gen_rvtt_wh_sfp<wormhole_simple_op_name_lv>_int(operands[0], operands[1], operands[2], operands[3]));
  DONE;
})

(define_insn "rvtt_wh_sfp<wormhole_simple_op_name_int>_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")
                          (match_operand:SI    3 "immediate_operand" "M04U, M04U")] wormhole_simple_op_int))]
  "TARGET_RVTT_WH"
  "SFP<wormhole_simple_op_call_int>\t%0, %2, %3"
)

(define_int_iterator wormhole_muladd [UNSPECV_WH_SFPMUL UNSPECV_WH_SFPADD])
(define_int_attr wormhole_muladd_name [(UNSPECV_WH_SFPMUL "mul") (UNSPECV_WH_SFPADD "add")])
(define_expand "rvtt_wh_sfp<wormhole_muladd_name>"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")] wormhole_muladd))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_gen_const0_vector();
  emit_insn (gen_rvtt_wh_sfp<wormhole_muladd_name>_int(operands[0], live, operands[1], operands[2], operands[3]));
  DONE;
})

(define_int_iterator wormhole_muladd_lv [UNSPECV_WH_SFPMUL_LV UNSPECV_WH_SFPADD_LV])
(define_int_attr wormhole_muladd_name_lv [(UNSPECV_WH_SFPMUL_LV "mul") (UNSPECV_WH_SFPADD_LV "add")])
(define_expand "rvtt_wh_sfp<wormhole_muladd_name_lv>_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:SI    4 "immediate_operand" "")] wormhole_muladd_lv))]
  "TARGET_RVTT_WH"
{
  rtx live = operands[1];
  emit_insn (gen_rvtt_wh_sfp<wormhole_muladd_name_lv>_int(operands[0], live, operands[2], operands[3], operands[4]));
  DONE;
})

(define_int_iterator wormhole_muladd_int [UNSPECV_WH_SFPMUL_INT UNSPECV_WH_SFPADD_INT])
(define_int_attr wormhole_muladd_name_int [(UNSPECV_WH_SFPMUL_INT "mul") (UNSPECV_WH_SFPADD_INT "add")])
(define_int_attr wormhole_muladd_call_int [(UNSPECV_WH_SFPMUL_INT "MUL\t%0, %2, %3, L9") (UNSPECV_WH_SFPADD_INT "ADD\t%0, L10, %2, %3")])
(define_insn "rvtt_wh_sfp<wormhole_muladd_name_int>_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")
                          (match_operand:V64SF 3 "register_operand"  "x, x")
                          (match_operand:SI    4 "immediate_operand" "M04U, M04U")] wormhole_muladd_int))]
  "TARGET_RVTT_WH"
  "SFP<wormhole_muladd_call_int>, %4"
)

(define_insn "rvtt_wh_sfpiadd_v_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M04U")] UNSPECV_WH_SFPIADD_V_INT))]
  "TARGET_RVTT_WH"
  "SFPIADD\t%0, %2, 0, %3"
)

(define_insn "rvtt_wh_sfpiadd_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")
                          (match_operand:SI    3 "immediate_operand" "n, n")
                          (match_operand:SI    4 "immediate_operand" "M04U, M04U")] UNSPECV_WH_SFPIADD_I_INT))]
  "TARGET_RVTT_WH"
  "SFPIADD\t%0, %2, %3, %4"
)

(define_expand "rvtt_wh_sfpxiadd_v"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M04U")] UNSPECV_WH_SFPXIADD_V))]
  "TARGET_RVTT_WH"
{
  rvtt_wh_emit_sfpxiadd_v(operands[0], operands[1], operands[2], operands[3]);
  DONE;
})


(define_expand "rvtt_wh_sfpxiadd_i"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:SI    4 "immediate_operand" "")
                          (match_operand:SI    5 "nonmemory_operand" "")
                          (match_operand:SI    6 "immediate_operand" "")] UNSPECV_WH_SFPXIADD_I))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_gen_const0_vector();
  rvtt_wh_emit_sfpxiadd_i(operands[0], live, operands[1], operands[2], operands[3], operands[6]);
  DONE;
})

(define_expand "rvtt_wh_sfpxiadd_i_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"   "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:SI    4 "nonmemory_operand" "")
                          (match_operand:SI    5 "immediate_operand" "")
                          (match_operand:SI    6 "nonmemory_operand" "")
                          (match_operand:SI    7 "immediate_operand" "")] UNSPECV_WH_SFPXIADD_I_LV))]
  "TARGET_RVTT_WH"
{
  rtx live = operands[2];
  rvtt_wh_emit_sfpxiadd_i(operands[0], live, operands[1], operands[3], operands[4], operands[7]);
  DONE;
})

(define_insn "rvtt_wh_sfpshft_v"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_WH_SFPSHFT_V))]
  "TARGET_RVTT_WH"
  "SFPSHFT\t%0, %2, 0, 0"
)

(define_expand "rvtt_wh_sfpshft_i"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:SI    4 "register_operand" "")
                          (match_operand:SI    5 "immediate_operand" "")] UNSPECV_WH_SFPSHFT_I))]
  "TARGET_RVTT_WH"
{
  if (GET_CODE(operands[3]) == CONST_INT) {
    emit_insn (gen_rvtt_wh_sfpshft_i_int(operands[0], operands[2], rvtt_clamp_signed(operands[3], 0x7FF)));
  } else {
    unsigned long int op = TT_OP_WH_SFPSHFT(0, 0, 0, 1);
    emit_insn (gen_rvtt_sfpnonimm_dst(operands[0], operands[1], GEN_INT(0), operands[2], GEN_INT(4), operands[4], GEN_INT(op), operands[5]));
  }
  DONE;
})

(define_insn "rvtt_wh_sfpshft_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:SI    2 "immediate_operand" "M12S")] UNSPECV_WH_SFPSHFT_I_INT))]
  "TARGET_RVTT_WH"
  "SFPSHFT\t%0, L0, %2, 1"
)

(define_insn "rvtt_wh_sfpand"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_WH_SFPAND))]
  "TARGET_RVTT_WH"
  "SFPAND\t%0, %2"
)

(define_insn "rvtt_wh_sfpor"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_WH_SFPOR))]
  "TARGET_RVTT_WH"
  "SFPOR\t%0, %2"
)

(define_insn "rvtt_wh_sfpxor"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_WH_SFPXOR))]
  "TARGET_RVTT_WH"
  "SFPXOR\t%0, %2"
)

(define_expand "rvtt_wh_sfpnot"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")] UNSPECV_WH_SFPNOT))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_gen_const0_vector();
  emit_insn (gen_rvtt_wh_sfpnot_int(operands[0], live, operands[1]));
  DONE;
})

(define_expand "rvtt_wh_sfpnot_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")] UNSPECV_WH_SFPNOT_LV))]
  "TARGET_RVTT_WH"
{
  rtx live = operands[1];
  emit_insn (gen_rvtt_wh_sfpnot_int(operands[0], live, operands[2]));
  DONE;
})

(define_insn "rvtt_wh_sfpnot_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")] UNSPECV_WH_SFPNOT_INT))]
  "TARGET_RVTT_WH"
  "SFPNOT\t%0, %2"
)

(define_expand "rvtt_wh_sfpcast"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:SI    2 "immediate_operand" "")] UNSPECV_WH_SFPCAST))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_gen_const0_vector();
  emit_insn (gen_rvtt_wh_sfpcast_int(operands[0], live, operands[1], operands[2]));
  DONE;
})

(define_expand "rvtt_wh_sfpcast_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")] UNSPECV_WH_SFPCAST_LV))]
  "TARGET_RVTT_WH"
{
  rtx live = operands[2];
  emit_insn (gen_rvtt_wh_sfpcast_int(operands[0], live, operands[2], operands[3]));
  DONE;
})

(define_insn "rvtt_wh_sfpcast_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")
                          (match_operand:SI    3 "immediate_operand" "M04U, M04U")] UNSPECV_WH_SFPCAST_INT))]
  "TARGET_RVTT_WH"
  "SFPCAST %0, %2, %3")

(define_expand "rvtt_wh_sfpshft2_e"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:SI    2 "immediate_operand" "")] UNSPECV_WH_SFPSHFT2_E))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_gen_const0_vector();
  rvtt_wh_emit_sfpshft2_e(operands[0], live, operands[1], operands[2]);
  DONE;
})

(define_expand "rvtt_wh_sfpshft2_e_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")] UNSPECV_WH_SFPSHFT2_E_LV))]
  "TARGET_RVTT_WH"
{
  rtx live = operands[1];
  rvtt_wh_emit_sfpshft2_e(operands[0], live, operands[2], operands[3]);
  DONE;
})

(define_insn "rvtt_wh_sfpshft2_e_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")
                          (match_operand:SI    3 "immediate_operand" "M04U, M04U")] UNSPECV_WH_SFPSHFT2_E_INT))]
  "TARGET_RVTT_WH"
  "SFPSHFT2\t%0, %2, 0, %3")

(define_expand "rvtt_wh_sfpstochrnd_i"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:SI    2 "immediate_operand" "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:SI    4 "register_operand"  "")
                          (match_operand:SI    5 "immediate_operand" "")
                          (match_operand:V64SF 6 "register_operand"  "")
                          (match_operand:SI    7 "immediate_operand" "")] UNSPECV_WH_SFPSTOCHRND_I))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_gen_const0_vector();
  rvtt_wh_emit_sfpstochrnd_i(operands[0], live, operands[1], operands[2], operands[3],
                                   operands[6], operands[7], operands[4], operands[5]);
  DONE;
})

(define_expand "rvtt_wh_sfpstochrnd_i_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")
                          (match_operand:SI    4 "nonmemory_operand" "")
                          (match_operand:SI    5 "register_operand"  "")
                          (match_operand:SI    6 "immediate_operand" "")
                          (match_operand:V64SF 7 "register_operand"  "")
                          (match_operand:SI    8 "immediate_operand" "")] UNSPECV_WH_SFPSTOCHRND_I_LV))]
  "TARGET_RVTT_WH"
{
  rtx live = operands[2];
  rvtt_wh_emit_sfpstochrnd_i(operands[0], live, operands[1], operands[3], operands[4],
                                   operands[7], operands[8], operands[5], operands[6]);
  DONE;
})

(define_insn "rvtt_wh_sfpstochrnd_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:SI    2 "immediate_operand" "M01U, M01U")
                          (match_operand:SI    3 "immediate_operand" "M05U, M05U")
                          (match_operand:V64SF 4 "register_operand"  "x, x")
                          (match_operand:SI    5 "immediate_operand" "M04U, M04U")] UNSPECV_WH_SFPSTOCHRND_I_INT))]
  "TARGET_RVTT_WH"
  "SFPSTOCHRND\t%0, L0, %4, %5, %2, %3");

(define_expand "rvtt_wh_sfpstochrnd_v"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "immediate_operand" "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:SI    4 "immediate_operand" "")] UNSPECV_WH_SFPSTOCHRND_V))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_gen_const0_vector();
  emit_insn (gen_rvtt_wh_sfpstochrnd_v_int(operands[0], live, operands[1], operands[2], operands[3], operands[4]));
  DONE;
})

(define_expand "rvtt_wh_sfpstochrnd_v_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:SI    2 "immediate_operand" "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:V64SF 4 "register_operand"  "")
                          (match_operand:SI    5 "immediate_operand" "")] UNSPECV_WH_SFPSTOCHRND_V_LV))]
  "TARGET_RVTT_WH"
{
  rtx live = operands[1];
  emit_insn (gen_rvtt_wh_sfpstochrnd_v_int(operands[0], live, operands[2], operands[3], operands[4], operands[5]));
  DONE;
})

(define_insn "rvtt_wh_sfpstochrnd_v_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:SI    2 "immediate_operand" "M01U, M01U")
                          (match_operand:V64SF 3 "register_operand"  "x, x")
                          (match_operand:V64SF 4 "register_operand"  "x, x")
                          (match_operand:SI    5 "immediate_operand" "M04U, M04U")] UNSPECV_WH_SFPSTOCHRND_V_INT))]
  "TARGET_RVTT_WH"
  "SFPSTOCHRND\t%0, %3, %4, %5, %2, 0")

(define_int_iterator wormhole_set_float_op_v [UNSPECV_WH_SFPSETEXP_V UNSPECV_WH_SFPSETMAN_V UNSPECV_WH_SFPSETSGN_V])
(define_int_attr wormhole_set_float_name_v [(UNSPECV_WH_SFPSETEXP_V "exp") (UNSPECV_WH_SFPSETMAN_V "man") (UNSPECV_WH_SFPSETSGN_V "sgn")])
(define_int_attr wormhole_set_float_call_v [(UNSPECV_WH_SFPSETEXP_V "EXP") (UNSPECV_WH_SFPSETMAN_V "MAN") (UNSPECV_WH_SFPSETSGN_V "SGN")])
(define_insn "rvtt_wh_sfpset<wormhole_set_float_name_v>_v"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] wormhole_set_float_op_v))]
  "TARGET_RVTT_WH"
  "SFPSET<wormhole_set_float_call_v>\t%0, %2, 0, 0"
)

(define_int_iterator wormhole_set_float_op_i [UNSPECV_WH_SFPSETEXP_I UNSPECV_WH_SFPSETSGN_I])
(define_int_attr wormhole_set_float_name_i [(UNSPECV_WH_SFPSETEXP_I "exp") (UNSPECV_WH_SFPSETSGN_I "sgn")])
(define_int_attr wormhole_set_float_call_i [(UNSPECV_WH_SFPSETEXP_I "EXP") (UNSPECV_WH_SFPSETSGN_I "SGN")])
(define_expand "rvtt_wh_sfpset<wormhole_set_float_name_i>_i"
  [(set (match_operand:V64SF 0 "register_operand")
        (unspec_volatile [(match_operand:SI    1 "address_operand")
                          (match_operand:SI    2 "nonmemory_operand")
                          (match_operand:SI    3 "register_operand")
                          (match_operand:SI    4 "immediate_operand")
                          (match_operand:V64SF 5 "register_operand")] wormhole_set_float_op_i))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_gen_const0_vector();
  if (GET_CODE(operands[2]) == CONST_INT) {
    emit_insn (gen_rvtt_wh_sfpset<wormhole_set_float_name_i>_i_int(operands[0], live,
                                  rvtt_clamp_unsigned(operands[2], 0xFFF), operands[5]));
  } else {
    unsigned long int op = TT_OP_WH_SFPSET<wormhole_set_float_call_i>(0, 0, 0, 1);
    emit_insn (gen_rvtt_sfpnonimm_dst_src(operands[0], operands[1], GEN_INT(0), live,
                                           operands[5], GEN_INT(4), GEN_INT(8), operands[3], GEN_INT(op), operands[4]));
  }
  DONE;
})

(define_int_iterator wormhole_set_float_op_i_lv [UNSPECV_WH_SFPSETEXP_I_LV UNSPECV_WH_SFPSETSGN_I_LV])
(define_int_attr wormhole_set_float_name_i_lv [(UNSPECV_WH_SFPSETEXP_I_LV "exp") (UNSPECV_WH_SFPSETSGN_I_LV "sgn")])
(define_int_attr wormhole_set_float_call_i_lv [(UNSPECV_WH_SFPSETEXP_I_LV "EXP") (UNSPECV_WH_SFPSETSGN_I_LV "SGN")])
(define_expand "rvtt_wh_sfpset<wormhole_set_float_name_i_lv>_i_lv"
  [(set (match_operand:V64SF 0 "register_operand")
        (unspec_volatile [(match_operand:SI    1 "address_operand")
                          (match_operand:V64SF 2 "register_operand")
                          (match_operand:SI    3 "nonmemory_operand")
                          (match_operand:SI    4 "register_operand")
                          (match_operand:SI    5 "immediate_operand")
                          (match_operand:V64SF 6 "register_operand")] wormhole_set_float_op_i_lv))]
  "TARGET_RVTT_WH"
{
  rtx live = operands[2];
  if (GET_CODE(operands[3]) == CONST_INT) {
    emit_insn (gen_rvtt_wh_sfpset<wormhole_set_float_name_i_lv>_i_int(operands[0], live,
                                   rvtt_clamp_unsigned(operands[3], 0xFFF), operands[6]));
  } else {
    unsigned long int op = TT_OP_WH_SFPSET<wormhole_set_float_call_i_lv>(0, 0, 0, 1);
    emit_insn (gen_rvtt_sfpnonimm_dst_src(operands[0], operands[1], GEN_INT(0),
               live, operands[6], GEN_INT(4), GEN_INT(8), operands[4], GEN_INT(op), operands[5]));
  }
  DONE;
})

(define_int_iterator wormhole_set_float_op_i_int [UNSPECV_WH_SFPSETEXP_I_INT UNSPECV_WH_SFPSETSGN_I_INT])
(define_int_attr wormhole_set_float_name_i_int [(UNSPECV_WH_SFPSETEXP_I_INT "exp") (UNSPECV_WH_SFPSETSGN_I_INT "sgn")])
(define_int_attr wormhole_set_float_call_i_int [(UNSPECV_WH_SFPSETEXP_I_INT "EXP") (UNSPECV_WH_SFPSETSGN_I_INT "SGN")])
(define_insn "rvtt_wh_sfpset<wormhole_set_float_name_i_int>_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:SI    2 "immediate_operand" "M12U, M12U")
                          (match_operand:V64SF 3 "register_operand"  "x, x")] wormhole_set_float_op_i_int))]
  "TARGET_RVTT_WH"
  "SFPSET<wormhole_set_float_call_i_int>\t%0, %3, %2, 1"
)

(define_expand "rvtt_wh_sfpsetman_i"
  [(set (match_operand:V64SF 0 "register_operand")
        (unspec_volatile [(match_operand:SI    1 "address_operand")
                          (match_operand:SI    2 "nonmemory_operand")
                          (match_operand:SI    3 "register_operand")
                          (match_operand:SI    4 "immediate_operand")
                          (match_operand:V64SF 5 "register_operand")
                          (match_operand:SI    6 "immediate_operand")] UNSPECV_WH_SFPSETMAN_I))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_gen_const0_vector();
  rvtt_wh_emit_sfpsetman(operands[0], live, operands[1], operands[2], operands[5]);
  DONE;
})

(define_expand "rvtt_wh_sfpsetman_i_lv"
  [(set (match_operand:V64SF 0 "register_operand")
        (unspec_volatile [(match_operand:SI    1 "address_operand")
                          (match_operand:V64SF 2 "register_operand")
                          (match_operand:SI    3 "nonmemory_operand")
                          (match_operand:SI    4 "register_operand")
                          (match_operand:SI    5 "immediate_operand")
                          (match_operand:V64SF 6 "register_operand")
                          (match_operand:SI    7 "immediate_operand")] UNSPECV_WH_SFPSETMAN_I_LV))]
  "TARGET_RVTT_WH"
{
  rtx live = operands[2];
  rvtt_wh_emit_sfpsetman(operands[0], live, operands[1], operands[3], operands[6]);
  DONE;
})

(define_insn "rvtt_wh_sfpsetman_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:SI    2 "immediate_operand" "M12U, M12U")
                          (match_operand:V64SF 3 "register_operand"  "x, x")] UNSPECV_WH_SFPSETMAN_I_INT))]
  "TARGET_RVTT_WH"
  "SFPSETMAN\t%0, %3, %2, 1"
)

(define_expand "rvtt_wh_sfpmad"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:SI    4 "immediate_operand" "")] UNSPECV_WH_SFPMAD))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_gen_const0_vector();
  emit_insn (gen_rvtt_wh_sfpmad_int(operands[0], live, operands[1], operands[2], operands[3], operands[4]));
  DONE;
})

(define_expand "rvtt_wh_sfpmad_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:V64SF 4 "register_operand"  "")
                          (match_operand:SI    5 "immediate_operand" "")] UNSPECV_WH_SFPMAD_LV))]
  "TARGET_RVTT_WH"
{
  rtx live = operands[1];
  emit_insn (gen_rvtt_wh_sfpmad_int(operands[0], live, operands[2], operands[3], operands[4], operands[5]));
  DONE;
})

(define_insn "rvtt_wh_sfpmad_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")
                          (match_operand:V64SF 3 "register_operand"  "x, x")
                          (match_operand:V64SF 4 "register_operand"  "x, x")
                          (match_operand:SI    5 "immediate_operand" "M04U, M04U")] UNSPECV_WH_SFPMAD_INT))]
  "TARGET_RVTT_WH"
  "SFPMAD\t%0, %2, %3, %4, %5"
)

(define_insn "rvtt_wh_sfpsetcc_i"
  [(unspec_volatile [(match_operand:SI    0 "immediate_operand" "M01U")
                     (match_operand:SI    1 "immediate_operand" "M04U")] UNSPECV_WH_SFPSETCC_I)]
  "TARGET_RVTT_WH"
  "SFPSETCC\tL0, %0, %1"
)

(define_insn "rvtt_wh_sfpsetcc_v"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "x")
                     (match_operand:SI    1 "immediate_operand" "M04U")] UNSPECV_WH_SFPSETCC_V)]
  "TARGET_RVTT_WH"
  "SFPSETCC\t%0, 0, %1"
)

(define_expand "rvtt_wh_sfpxfcmps"
  [(set (match_operand:SI 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"   "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:SI    4 "nonmemory_operand" "")
                          (match_operand:SI    5 "immediate_operand" "")
                          (match_operand:SI    6 "immediate_operand" "")] UNSPECV_WH_SFPXFCMPS))]
  "TARGET_RVTT_WH"
{
  rvtt_wh_emit_sfpxfcmps(operands[1], operands[2], operands[3], operands[6]);
  DONE;
})

(define_expand "rvtt_wh_sfpxfcmpv"
  [(set (match_operand:SI 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")] UNSPECV_WH_SFPXFCMPV))]
  "TARGET_RVTT_WH"
{
  rvtt_wh_emit_sfpxfcmpv(operands[1], operands[2], operands[3]);
  DONE;
})

(define_insn "rvtt_wh_sfpencc"
  [(unspec_volatile [(match_operand:SI 0 "immediate_operand" "M02U")
                     (match_operand:SI 1 "immediate_operand" "M04U")] UNSPECV_WH_SFPENCC)]
  "TARGET_RVTT_WH"
  "SFPENCC\t%0, %1"
)

(define_insn "rvtt_wh_sfpcompc"
  [(unspec_volatile [(const_int 0)] UNSPECV_WH_SFPCOMPC)]
  "TARGET_RVTT_WH"
  "SFPCOMPC"
)

(define_insn "rvtt_wh_sfppushc"
  [(unspec_volatile [(match_operand:SI 0 "immediate_operand" "M04U")] UNSPECV_WH_SFPPUSHC)]
  "TARGET_RVTT_WH"
  "SFPPUSHC\t%0")

(define_insn "rvtt_wh_sfppopc"
  [(unspec_volatile [(match_operand:SI 0 "immediate_operand" "M04U")] UNSPECV_WH_SFPPOPC)]
  "TARGET_RVTT_WH"
  "SFPPOPC\t%0"
)

(define_insn "rvtt_wh_sfplut"
  [(set (match_operand:V64SF 0 "register_operand" "=Q3")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "Q0")
                          (match_operand:V64SF 2 "register_operand"  "Q1")
                          (match_operand:V64SF 3 "register_operand"  "Q2")
                          (match_operand:V64SF 4 "register_operand"  "0")
                          (match_operand:SI    5 "immediate_operand" "M04U")] UNSPECV_WH_SFPLUT))]
  "TARGET_RVTT_WH"
  "SFPLUT\t%0, %5"
)

(define_insn "rvtt_wh_sfplutfp32_3r"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "Q0")
                          (match_operand:V64SF 2 "register_operand"  "Q1")
                          (match_operand:V64SF 3 "register_operand"  "Q2")
                          (match_operand:V64SF 4 "register_operand"  "Q3")
                          (match_operand:SI    5 "immediate_operand" "M04U")] UNSPECV_WH_SFPLUTFP32_3R))
        (clobber (match_scratch:V64SF 6 "=Q7"))
        (match_scratch:SI 7)]
  "TARGET_RVTT_WH"
{
  // Note: this insn must emit 2 insns, ie, this can't be done in an expand as
  // the hard regno is only known at reload time, not at expand time
  // This mean, e.g., the REPLAY pass must know this insn is really 2 insns
  operands[7] = GEN_INT(rvtt_sfpu_regno(operands[0]));
  output_asm_insn("SFPLOADI\t%6, %7, 2", operands);
  return "SFPLUTFP32\t%0, %5";
})

(define_insn "rvtt_wh_sfplutfp32_6r"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "Q0")
                          (match_operand:V64SF 2 "register_operand"  "Q1")
                          (match_operand:V64SF 3 "register_operand"  "Q2")
                          (match_operand:V64SF 4 "register_operand"  "Q4")
                          (match_operand:V64SF 5 "register_operand"  "Q5")
                          (match_operand:V64SF 6 "register_operand"  "Q6")
                          (match_operand:V64SF 7 "register_operand"  "Q3")
                          (match_operand:SI    8 "immediate_operand" "M04U")] UNSPECV_WH_SFPLUTFP32_6R))]
  "TARGET_RVTT_WH"
  "SFPLUTFP32\t%0, %8")

(define_insn "rvtt_wh_sfpconfig_v"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"   "Q0")
                     (match_operand:SI    1 "immediate_operand"  "M04U")] UNSPECV_WH_SFPCONFIG_V)]
  "TARGET_RVTT_WH"
  "SFPCONFIG\t%1, 0, 0")

(define_expand "rvtt_wh_sfpswap"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"   "")
                     (match_operand:V64SF 1 "register_operand"   "")
                     (match_operand:SI    2 "immediate_operand"  "")] UNSPECV_WH_SFPSWAP)]
  "TARGET_RVTT_WH"
{
  emit_insn (gen_rvtt_wh_sfpswap_int(operands[0], operands[1], operands[2]));
  DONE;
})

(define_insn "rvtt_wh_sfpswap_int"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"   "+x")
                     (match_operand:V64SF 1 "register_operand"   "+x")
                     (match_operand:SI    2 "immediate_operand"  "M04U")] UNSPECV_WH_SFPSWAP_INT)]
  "TARGET_RVTT_WH"
  "SFPSWAP\t%0, %1, %2")

(define_insn "rvtt_wh_sfptransp"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"   "+Q0")
                     (match_operand:V64SF 1 "register_operand"   "+Q1")
                     (match_operand:V64SF 2 "register_operand"   "+Q2")
                     (match_operand:V64SF 3 "register_operand"   "+Q3")] UNSPECV_WH_SFPTRANSP)]
  "TARGET_RVTT_WH"
  "SFPTRANSP")

(define_insn "rvtt_wh_sfpshft2_g"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"   "+Q0")
                     (match_operand:V64SF 1 "register_operand"   "+Q1")
                     (match_operand:V64SF 2 "register_operand"   "+Q2")
                     (match_operand:V64SF 3 "register_operand"   "+Q3")
                     (match_operand:SI    4 "immediate_operand"  "M04U")] UNSPECV_WH_SFPSHFT2_G)]
  "TARGET_RVTT_WH"
  "SFPSHFT2\t0, L0, L0, %0, %1, %2, %3, %4")

(define_insn "rvtt_wh_sfpshft2_ge"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"   "x")
                     (match_operand:V64SF 1 "register_operand"   "+Q0")
                     (match_operand:V64SF 2 "register_operand"   "+Q1")
                     (match_operand:V64SF 3 "register_operand"   "+Q2")
                     (match_operand:V64SF 4 "register_operand"   "+Q3")] UNSPECV_WH_SFPSHFT2_GE)]
  "TARGET_RVTT_WH"
  "SFPSHFT2\t0, %0, L0, %1, %2, %3, %4, 2")

(define_insn "rvtt_wh_sfpnop"
  [(unspec_volatile [(const_int 0)] UNSPECV_WH_SFPNOP)]
  "TARGET_RVTT_WH"
  "SFPNOP")

(include "tt/rvtt-peephole-wh.md")
