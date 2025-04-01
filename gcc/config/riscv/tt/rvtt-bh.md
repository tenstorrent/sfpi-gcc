;; Machine description for Tenstorrent SFPU Blackhole Intrinsics.
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
  UNSPECV_BH_SFPASSIGN
  UNSPECV_BH_SFPPRESERVELREG
  UNSPECV_BH_SFPPRESERVELREG0_INT
  UNSPECV_BH_SFPPRESERVELREG1_INT
  UNSPECV_BH_SFPPRESERVELREG2_INT
  UNSPECV_BH_SFPPRESERVELREG3_INT
  UNSPECV_BH_SFPPRESERVELREG4_INT
  UNSPECV_BH_SFPPRESERVELREG5_INT
  UNSPECV_BH_SFPPRESERVELREG6_INT
  UNSPECV_BH_SFPPRESERVELREG7_INT
  UNSPECV_BH_SFPLOAD
  UNSPECV_BH_SFPXLOADI
  UNSPECV_BH_SFPSTORE
  UNSPECV_BH_SFPMULI
  UNSPECV_BH_SFPADDI
  UNSPECV_BH_SFPMUL
  UNSPECV_BH_SFPADD
  UNSPECV_BH_SFPIADD_V_INT
  UNSPECV_BH_SFPXIADD_V
  UNSPECV_BH_SFPIADD_I
  UNSPECV_BH_SFPIADD_I_LV
  UNSPECV_BH_SFPXIADD_I
  UNSPECV_BH_SFPXIADD_I_LV
  UNSPECV_BH_SFPIADD_I_INT
  UNSPECV_BH_SFPSHFT
  UNSPECV_BH_SFPABS
  UNSPECV_BH_SFPABS_LV
  UNSPECV_BH_SFPABS_INT
  UNSPECV_BH_SFPAND
  UNSPECV_BH_SFPOR
  UNSPECV_BH_SFPXOR
  UNSPECV_BH_SFPNOT
  UNSPECV_BH_SFPLZ
  UNSPECV_BH_SFPLZ_LV
  UNSPECV_BH_SFPLZ_INT
  UNSPECV_BH_SFPSETMAN_V
  UNSPECV_BH_SFPSETMAN_I
  UNSPECV_BH_SFPSETMAN_I_LV
  UNSPECV_BH_SFPSETMAN_I_INT
  UNSPECV_BH_SFPSETEXP_V
  UNSPECV_BH_SFPSETEXP_I
  UNSPECV_BH_SFPSETEXP_I_LV
  UNSPECV_BH_SFPSETEXP_I_INT
  UNSPECV_BH_SFPSETSGN_V
  UNSPECV_BH_SFPSETSGN_I
  UNSPECV_BH_SFPSETSGN_I_LV
  UNSPECV_BH_SFPSETSGN_I_INT
  UNSPECV_BH_SFPMAD
  UNSPECV_BH_SFPMAD_LV
  UNSPECV_BH_SFPMAD_INT
  UNSPECV_BH_SFPMOV
  UNSPECV_BH_SFPMOV_LV
  UNSPECV_BH_SFPMOV_INT
  UNSPECV_BH_SFPDIVP2
  UNSPECV_BH_SFPEXEXP
  UNSPECV_BH_SFPEXEXP_LV
  UNSPECV_BH_SFPEXEXP_INT
  UNSPECV_BH_SFPEXMAN
  UNSPECV_BH_SFPEXMAN_LV
  UNSPECV_BH_SFPEXMAN_INT
  UNSPECV_BH_SFPSETCC_I
  UNSPECV_BH_SFPSETCC_V
  UNSPECV_BH_SFPXFCMPS
  UNSPECV_BH_SFPXFCMPV
  UNSPECV_BH_SFPENCC
  UNSPECV_BH_SFPCOMPC
  UNSPECV_BH_SFPPUSHC
  UNSPECV_BH_SFPPOPC
  UNSPECV_BH_SFPCAST
  UNSPECV_BH_SFPSHFT2_E
  UNSPECV_BH_SFPSHFT2_E_LV
  UNSPECV_BH_SFPSHFT2_E_INT
  UNSPECV_BH_SFPSTOCHRND_I
  UNSPECV_BH_SFPSTOCHRND_I_LV
  UNSPECV_BH_SFPSTOCHRND_I_INT
  UNSPECV_BH_SFPSTOCHRND_V
  UNSPECV_BH_SFPSTOCHRND_V_LV
  UNSPECV_BH_SFPSTOCHRND_V_INT
  UNSPECV_BH_SFPLUT
  UNSPECV_BH_SFPLUTFP32_3R
  UNSPECV_BH_SFPLUTFP32_6R
  UNSPECV_BH_SFPCONFIG_V
  UNSPECV_BH_SFPREPLAY
  UNSPECV_BH_SFPSWAP
  UNSPECV_BH_SFPTRANSP
  UNSPECV_BH_SFPSHFT2_G
  UNSPECV_BH_SFPSHFT2_GE
  UNSPECV_BH_SFPNOP
  UNSPECV_BH_SFPMUL24
  UNSPECV_BH_SFPARECIP
  UNSPECV_BH_SFPGT
  UNSPECV_BH_SFPLE
  UNSPECV_BH_SFPMOV_CONFIG
])

(define_insn "rvtt_bh_sfpgccmov_cc"
  [(set (match_operand:V64SF 0 "nonimmediate_operand" "=x,x,m")
        (match_operand:V64SF 1 "move_operand"         " x,m,x"))]
  "TARGET_RVTT_BH  &&
   (   register_operand (operands[0], V64SFmode)
    || reg_or_0_operand (operands[1], V64SFmode))"
  {
    switch (which_alternative) {
    case 0:
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

(define_insn "rvtt_bh_sfpassign_lv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_BH_SFPASSIGN))]
  "TARGET_RVTT_BH"
  "SFPMOV\t%0, %2, 0"
)

(define_expand "rvtt_bh_sfppreservelreg"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "")
                     (match_operand:SI    1 "immediate_operand" "")] UNSPECV_BH_SFPPRESERVELREG)]
  "TARGET_RVTT_BH"
{
  static rtx (*fn_ptr[8])(rtx) = {gen_rvtt_bh_sfppreservelreg0_int, gen_rvtt_bh_sfppreservelreg1_int,
                                  gen_rvtt_bh_sfppreservelreg2_int, gen_rvtt_bh_sfppreservelreg3_int,
                                  gen_rvtt_bh_sfppreservelreg4_int, gen_rvtt_bh_sfppreservelreg5_int,
                                  gen_rvtt_bh_sfppreservelreg6_int, gen_rvtt_bh_sfppreservelreg7_int};
  emit_insn(fn_ptr[INTVAL(operands[1])](operands[0]));
  DONE;
})

(define_int_iterator blackhole_preservelreg_int
 [UNSPECV_BH_SFPPRESERVELREG0_INT UNSPECV_BH_SFPPRESERVELREG1_INT
  UNSPECV_BH_SFPPRESERVELREG2_INT UNSPECV_BH_SFPPRESERVELREG3_INT
  UNSPECV_BH_SFPPRESERVELREG4_INT UNSPECV_BH_SFPPRESERVELREG5_INT
  UNSPECV_BH_SFPPRESERVELREG6_INT UNSPECV_BH_SFPPRESERVELREG7_INT])
(define_int_attr blackhole_preservelreg_int_name
 [(UNSPECV_BH_SFPPRESERVELREG0_INT "0") (UNSPECV_BH_SFPPRESERVELREG1_INT "1")
  (UNSPECV_BH_SFPPRESERVELREG2_INT "2") (UNSPECV_BH_SFPPRESERVELREG3_INT "3")
  (UNSPECV_BH_SFPPRESERVELREG4_INT "4") (UNSPECV_BH_SFPPRESERVELREG5_INT "5")
  (UNSPECV_BH_SFPPRESERVELREG6_INT "6") (UNSPECV_BH_SFPPRESERVELREG7_INT "7")])
(define_insn "rvtt_bh_sfppreservelreg<blackhole_preservelreg_int_name>_int"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand" "Q<blackhole_preservelreg_int_name>")] blackhole_preservelreg_int)]
  "TARGET_RVTT_BH"
  "")

(define_expand "rvtt_bh_sfpload"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI 1 "address_operand"  "")
                          (match_operand:SI 2 "immediate_operand" "")
                          (match_operand:SI 3 "immediate_operand" "")
                          (match_operand:SI 4 "nonmemory_operand" "")
                          (match_operand:SI 5 "register_operand" "")
                          (match_operand:SI 6 "immediate_operand" "")] UNSPECV_BH_SFPLOAD))]
  "TARGET_RVTT_BH"
{
  rvtt_bh_emit_sfpload (operands[0], rvtt_gen_const0_vector(),
  		        operands[1], operands[2], operands[3], operands[4], operands[5], operands[6]);
  DONE;
})

(define_expand "rvtt_bh_sfpload_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")
                          (match_operand:SI    4 "immediate_operand" "")
                          (match_operand:SI    5 "nonmemory_operand" "")
                          (match_operand:SI    6 "register_operand" "")
                          (match_operand:SI    7 "immediate_operand" "")] UNSPECV_BH_SFPLOAD))]
  "TARGET_RVTT_BH"
{
  rvtt_bh_emit_sfpload (operands[0], operands[2],
  		        operands[1], operands[3], operands[4], operands[5], operands[6], operands[7]);
  DONE;
})

(define_insn "rvtt_bh_sfpload_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:SI    2 "immediate_operand" "M04U, M04U")
                          (match_operand:SI    3 "immediate_operand" "M03U, M03U")
                          (match_operand:SI    4 "immediate_operand" "M13U, M13U")] UNSPECV_BH_SFPLOAD))]
  "TARGET_RVTT_BH"
  "@
   SFPLOAD\t%0, %4, %2, %3
   SFPLOAD\t%0, %4, %2, %3")


;;; SFPLOADI and SFPLOADI_LV
(define_expand "rvtt_bh_sfpxloadi"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec [(match_operand:SI 1 "address_operand"  "")
                          (match_operand:SI 2 "immediate_operand" "")
                          (match_operand:SI 3 "nonmemory_operand" "")
                          (match_operand:SI 4 "register_operand"  "")
                          (match_operand:SI 5 "immediate_operand" "")] UNSPECV_BH_SFPXLOADI))]
  "TARGET_RVTT_BH"
{
  rvtt_bh_emit_sfpxloadi (operands[0], rvtt_gen_const0_vector(),
  			  operands[1], operands[2], operands[3], operands[4], operands[5]);
  DONE;
})

(define_expand "rvtt_bh_sfpxloadi_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec [(match_operand:SI    1 "address_operand"   "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")
                          (match_operand:SI    4 "nonmemory_operand" "")
                          (match_operand:SI    5 "register_operand"  "")
                          (match_operand:SI    6 "immediate_operand" "")] UNSPECV_BH_SFPXLOADI))]
  "TARGET_RVTT_BH"
{
  rvtt_bh_emit_sfpxloadi (operands[0], operands[2],
  		          operands[1], operands[3], operands[4], operands[5], operands[6]);
  DONE;
})

(define_insn "rvtt_bh_sfploadi_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x,x,x,x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E,E,0,0")
                          (match_operand:SI    2 "immediate_operand" "M04U,M04U,M04U,M04U")
                          (match_operand:SI    3 "immediate_operand" "M16S,M16U,M16S,M16U")] UNSPECV_BH_SFPXLOADI))]
  "TARGET_RVTT_BH"
  "@
  SFPLOADI\t%0, %s3, %2
  SFPLOADI\t%0, %u3, %2
  SFPLOADI\t%0, %s3, %2
  SFPLOADI\t%0, %u3, %2")

(define_expand "rvtt_bh_sfpstore"
  [(unspec_volatile [(match_operand:SI    0 "address_operand"   "")
                     (match_operand:V64SF 1 "register_operand"  "")
                     (match_operand:SI    2 "immediate_operand" "")
                     (match_operand:SI    3 "immediate_operand" "")
                     (match_operand:SI    4 "nonmemory_operand" "")
                     (match_operand:SI    5 "register_operand" "")
                     (match_operand:SI    6 "immediate_operand" "")] UNSPECV_BH_SFPSTORE)]
  "TARGET_RVTT_BH"
{
  if (GET_CODE(operands[4]) == CONST_INT)
    emit_insn (gen_rvtt_bh_sfpstore_int (operands[1], operands[2], operands[3],
                                         rvtt_clamp_unsigned (operands[4], 0x1FFF)));
  else
    {
      unsigned long int op = TT_OP_BH_SFPSTORE (0, INTVAL (operands[2]), INTVAL (operands[3]), 0);
      emit_insn (gen_rvtt_sfpnonimm_store (operands[1], operands[0],
      					   GEN_INT (0), GEN_INT (20), operands[5], GEN_INT (op), operands[6]));
    }
  DONE;
})

(define_insn "rvtt_bh_sfpstore_int"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "x")
                     (match_operand:SI    1 "immediate_operand" "M04U")
                     (match_operand:SI    2 "immediate_operand" "M03U")
                     (match_operand:SI    3 "nonmemory_operand" "M13U")] UNSPECV_BH_SFPSTORE)]
  "TARGET_RVTT_BH"
  "SFPSTORE\t%3, %0, %1, %2")


(define_int_iterator blackhole_muliaddi [UNSPECV_BH_SFPMULI UNSPECV_BH_SFPADDI])
(define_int_attr blackhole_muliaddi_name [(UNSPECV_BH_SFPMULI "muli") (UNSPECV_BH_SFPADDI "addi")])
(define_int_attr blackhole_muliaddi_insn [(UNSPECV_BH_SFPMULI "MULI") (UNSPECV_BH_SFPADDI "ADDI")])
(define_expand "rvtt_bh_sfp<blackhole_muliaddi_name>"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:SI    4 "register_operand"  "")
                          (match_operand:SI    5 "immediate_operand" "")
                          (match_operand:SI    6 "immediate_operand" "")] blackhole_muliaddi))]
  "TARGET_RVTT_BH"
{
  rtx insn;
  if (GET_CODE (operands[3]) == CONST_INT)
    insn = gen_rvtt_bh_sfp<blackhole_muliaddi_name>_int (operands[0], operands[2],
               rvtt_clamp_unsigned (operands[3], 0xFFFF), operands[6]);
  else {
    unsigned long int op = TT_OP_BH_SFP<blackhole_muliaddi_insn> (0, 0, INTVAL (operands[6]));
    insn = gen_rvtt_sfpnonimm_dst (operands[0], operands[1],
                                   GEN_INT (INSN_SCHED_DYN), operands[2], GEN_INT (4), operands[4], GEN_INT (op), operands[5]);
  }
  emit_insn (insn);
  DONE;
})

(define_insn "rvtt_bh_sfp<blackhole_muliaddi_name>_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:SI    2 "immediate_operand" "M16U")
                          (match_operand:SI    3 "immediate_operand" "M04U")] blackhole_muliaddi))]
  "TARGET_RVTT_BH"
  "SFP<blackhole_muliaddi_insn>\t%0, %2, %3"
)

(define_expand "rvtt_bh_sfpdivp2"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:SI    2 "nonmemory_operand" "")
                          (match_operand:SI    3 "register_operand"  "")
                          (match_operand:SI    4 "immediate_operand" "")
                          (match_operand:V64SF 5 "register_operand"  "")
                          (match_operand:SI    6 "immediate_operand" "")] UNSPECV_BH_SFPDIVP2))]
  "TARGET_RVTT_BH"
{
  rvtt_bh_emit_sfpdivp2 (operands[0], rvtt_gen_const0_vector (),
  		         operands[1], operands[2], operands[5], operands[6], operands[3], operands[4]);
  DONE;
})

(define_expand "rvtt_bh_sfpdivp2_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:SI    4 "register_operand"  "")
                          (match_operand:SI    5 "immediate_operand" "")
                          (match_operand:V64SF 6 "register_operand"  "")
                          (match_operand:SI    7 "immediate_operand" "")] UNSPECV_BH_SFPDIVP2))]
  "TARGET_RVTT_BH"
{
  rvtt_bh_emit_sfpdivp2 (operands[0], operands[2],
  		         operands[1], operands[3], operands[6], operands[7], operands[4], operands[5]);
  DONE;
})

(define_insn "rvtt_bh_sfpdivp2_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:SI    2 "immediate_operand" "M12S, M12S")
                          (match_operand:V64SF 3 "register_operand"  "x, x")
                          (match_operand:SI    4 "immediate_operand" "M04U, M04U")] UNSPECV_BH_SFPDIVP2))]
  "TARGET_RVTT_BH"
  "SFPDIVP2\t%0, %3, %2, %4"
)

(define_int_iterator blackhole_simple_op
  [UNSPECV_BH_SFPEXEXP
   UNSPECV_BH_SFPEXMAN
   UNSPECV_BH_SFPABS
   UNSPECV_BH_SFPMOV
   UNSPECV_BH_SFPLZ])
(define_int_attr blackhole_simple_op_name
  [(UNSPECV_BH_SFPEXEXP "exexp")
   (UNSPECV_BH_SFPEXMAN "exman")
   (UNSPECV_BH_SFPABS "abs")
   (UNSPECV_BH_SFPMOV "mov")
   (UNSPECV_BH_SFPLZ "lz")])
(define_int_iterator blackhole_simple_op_lv
  [UNSPECV_BH_SFPEXEXP_LV
   UNSPECV_BH_SFPEXMAN_LV
   UNSPECV_BH_SFPABS_LV
   UNSPECV_BH_SFPMOV_LV
   UNSPECV_BH_SFPLZ_LV])
(define_int_attr blackhole_simple_op_name_lv
  [(UNSPECV_BH_SFPEXEXP_LV "exexp")
   (UNSPECV_BH_SFPEXMAN_LV "exman")
   (UNSPECV_BH_SFPABS_LV "abs")
   (UNSPECV_BH_SFPMOV_LV "mov")
   (UNSPECV_BH_SFPLZ_LV "lz")])
(define_int_iterator blackhole_simple_op_int
  [UNSPECV_BH_SFPEXEXP_INT
   UNSPECV_BH_SFPEXMAN_INT
   UNSPECV_BH_SFPABS_INT
   UNSPECV_BH_SFPMOV_INT
   UNSPECV_BH_SFPLZ_INT])
(define_int_attr blackhole_simple_op_name_int
  [(UNSPECV_BH_SFPEXEXP_INT "exexp")
   (UNSPECV_BH_SFPEXMAN_INT "exman")
   (UNSPECV_BH_SFPABS_INT "abs")
   (UNSPECV_BH_SFPMOV_INT "mov")
   (UNSPECV_BH_SFPLZ_INT "lz")])
(define_int_attr blackhole_simple_op_call_int
  [(UNSPECV_BH_SFPEXEXP_INT "EXEXP")
   (UNSPECV_BH_SFPEXMAN_INT "EXMAN")
   (UNSPECV_BH_SFPABS_INT "ABS")
   (UNSPECV_BH_SFPMOV_INT "MOV")
   (UNSPECV_BH_SFPLZ_INT "LZ")])
(define_int_attr blackhole_simple_op_id_int
  [(UNSPECV_BH_SFPEXEXP_INT "UNSPECV_BH_SFPEXEXP_INT")
   (UNSPECV_BH_SFPEXMAN_INT "UNSPECV_BH_SFPEXMAN_INT")
   (UNSPECV_BH_SFPABS_INT "UNSPECV_BH_SFPABS_INT")
   (UNSPECV_BH_SFPMOV_INT "UNSPECV_BH_SFPMOV_INT")
   (UNSPECV_BH_SFPLZ_INT "UNSPECV_BH_SFPLZ_INT")])

(define_expand "rvtt_bh_sfp<blackhole_simple_op_name>"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:SI    2 "immediate_operand" "")] blackhole_simple_op))]
  "TARGET_RVTT_BH"
{
  rtx live = rvtt_gen_const0_vector();
  emit_insn (gen_rvtt_bh_sfp<blackhole_simple_op_name>_int(operands[0], live, operands[1], operands[2]));
  DONE;
})

(define_expand "rvtt_bh_sfp<blackhole_simple_op_name_lv>_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")] blackhole_simple_op_lv))]
  "TARGET_RVTT_BH"
{
  emit_insn (gen_rvtt_bh_sfp<blackhole_simple_op_name_lv>_int(operands[0], operands[1], operands[2], operands[3]));
  DONE;
})

(define_insn "rvtt_bh_sfp<blackhole_simple_op_name_int>_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")
                          (match_operand:SI    3 "immediate_operand" "M04U, M04U")] blackhole_simple_op_int))]
  "TARGET_RVTT_BH"
  "SFP<blackhole_simple_op_call_int>\t%0, %2, %3"
)

(define_insn "rvtt_bh_sfpmov_config"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:SI 1 "immediate_operand" "M04U")] UNSPECV_BH_SFPMOV_CONFIG))]
  "TARGET_RVTT_BH"
  "SFPMOV\t%0,L%1,8"
)

(define_insn "rvtt_bh_sfpmov_config_lv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand" "0")
			  (match_operand:SI 2 "immediate_operand" "M04U")] UNSPECV_BH_SFPMOV_CONFIG))]
  "TARGET_RVTT_BH"
  "SFPMOV\t%0,L%2,8"
)

(define_int_iterator blackhole_muladd [UNSPECV_BH_SFPMUL UNSPECV_BH_SFPADD])
(define_int_attr blackhole_muladd_name [(UNSPECV_BH_SFPMUL "mul") (UNSPECV_BH_SFPADD "add")])
(define_int_attr blackhole_muladd_insn [(UNSPECV_BH_SFPMUL "MUL") (UNSPECV_BH_SFPADD "ADD")])
(define_int_attr blackhole_muladd_ops [(UNSPECV_BH_SFPMUL "%2, %3, L9") (UNSPECV_BH_SFPADD "L10, %2, %3")])
(define_expand "rvtt_bh_sfp<blackhole_muladd_name>"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")] blackhole_muladd))]
  "TARGET_RVTT_BH"
{
  emit_insn (gen_rvtt_bh_sfp<blackhole_muladd_name>_lv (operands[0], rvtt_gen_const0_vector (),
  	                                                operands[1], operands[2], operands[3]));
  DONE;
})

(define_insn "rvtt_bh_sfp<blackhole_muladd_name>_lv"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")
                          (match_operand:V64SF 3 "register_operand"  "x, x")
                          (match_operand:SI    4 "immediate_operand" "M04U, M04U")] blackhole_muladd))]
  "TARGET_RVTT_BH"
  "SFP<blackhole_muladd_insn>\t%0, <blackhole_muladd_ops>, %4"
)

(define_insn "rvtt_bh_sfpiadd_v_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M04U")] UNSPECV_BH_SFPIADD_V_INT))]
  "TARGET_RVTT_BH"
  "SFPIADD\t%0, %2, 0, %3"
)

(define_insn "rvtt_bh_sfpiadd_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")
                          (match_operand:SI    3 "immediate_operand" "n, n")
                          (match_operand:SI    4 "immediate_operand" "M04U, M04U")] UNSPECV_BH_SFPIADD_I_INT))]
  "TARGET_RVTT_BH"
  "SFPIADD\t%0, %2, %3, %4"
)

(define_expand "rvtt_bh_sfpxiadd_v"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M04U")] UNSPECV_BH_SFPXIADD_V))]
  "TARGET_RVTT_BH"
{
  rvtt_bh_emit_sfpxiadd_v(operands[0], operands[1], operands[2], operands[3]);
  DONE;
})


(define_expand "rvtt_bh_sfpxiadd_i"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:SI    4 "immediate_operand" "")
                          (match_operand:SI    5 "nonmemory_operand" "")
                          (match_operand:SI    6 "immediate_operand" "")] UNSPECV_BH_SFPXIADD_I))]
  "TARGET_RVTT_BH"
{
  rtx live = rvtt_gen_const0_vector();
  rvtt_bh_emit_sfpxiadd_i(operands[0], live, operands[1], operands[2], operands[3], operands[6]);
  DONE;
})

(define_expand "rvtt_bh_sfpxiadd_i_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"   "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:SI    4 "nonmemory_operand" "")
                          (match_operand:SI    5 "immediate_operand" "")
                          (match_operand:SI    6 "nonmemory_operand" "")
                          (match_operand:SI    7 "immediate_operand" "")] UNSPECV_BH_SFPXIADD_I_LV))]
  "TARGET_RVTT_BH"
{
  rtx live = operands[2];
  rvtt_bh_emit_sfpxiadd_i(operands[0], live, operands[1], operands[3], operands[4], operands[7]);
  DONE;
})

(define_insn "rvtt_bh_sfpshft_v"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI 3 "immediate_operand"  "M04U")] UNSPECV_BH_SFPSHFT))]
  "TARGET_RVTT_BH"
  "SFPSHFT\t%0, %2, 0, %3"
)

(define_expand "rvtt_bh_sfpshft_i"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:SI    4 "register_operand" "")
                          (match_operand:SI    5 "immediate_operand" "")
                          (match_operand:SI    6 "immediate_operand" "")] UNSPECV_BH_SFPSHFT))]
  "TARGET_RVTT_BH"
{
  rtx insn;
  if (GET_CODE (operands[3]) == CONST_INT)
    insn = gen_rvtt_bh_sfpshft_i_int (operands[0], operands[2], rvtt_clamp_signed(operands[3], 0x7FF), operands[6]);
  else {
    unsigned long int op = TT_OP_BH_SFPSHFT(0, 0, 0, INTVAL (operands[6]) | 5);
    insn = gen_rvtt_sfpnonimm_dst_src (operands[0], operands[1], const0_rtx, rvtt_gen_const0_vector(),
				       operands[2], GEN_INT (4), GEN_INT (8),
				       operands[4], GEN_INT (op), operands[5]);
  }
  emit_insn (insn);
  DONE;
})

(define_insn "rvtt_bh_sfpshft_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:SI    2 "immediate_operand" "M12S")
                          (match_operand:SI    3 "immediate_operand" "M04U")] UNSPECV_BH_SFPSHFT))]
  "TARGET_RVTT_BH"
  "SFPSHFT\t%0, %1, %2, %3 | 5"
)

(define_insn "rvtt_bh_sfpand"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_BH_SFPAND))]
  "TARGET_RVTT_BH"
  "SFPAND\t%0, %2"
)

(define_insn "rvtt_bh_sfpor"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_BH_SFPOR))]
  "TARGET_RVTT_BH"
  "SFPOR\t%0, %2"
)

(define_insn "rvtt_bh_sfpxor"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_BH_SFPXOR))]
  "TARGET_RVTT_BH"
  "SFPXOR\t%0, %2"
)

(define_insn "rvtt_bh_sfpnot"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")] UNSPECV_BH_SFPNOT))]
  "TARGET_RVTT_BH"
  "SFPNOT\t%0,%1"
)

(define_insn "rvtt_bh_sfpnot_lv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] UNSPECV_BH_SFPNOT))]
  "TARGET_RVTT_BH"
  "SFPNOT\t%0, %2"
)

(define_insn "rvtt_bh_sfpcast"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:SI    2 "immediate_operand" "M04U")] UNSPECV_BH_SFPCAST))]
  "TARGET_RVTT_BH"
  "SFPCAST\t%0, %1, %2"
  )

(define_insn "rvtt_bh_sfpcast_lv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M04U")] UNSPECV_BH_SFPCAST))]
  "TARGET_RVTT_BH"
  "SFPCAST\t%0, %2, %3"
)

(define_expand "rvtt_bh_sfpshft2_e"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:SI    2 "immediate_operand" "")] UNSPECV_BH_SFPSHFT2_E))]
  "TARGET_RVTT_BH"
{
  rtx live = rvtt_gen_const0_vector();
  rvtt_bh_emit_sfpshft2_e(operands[0], live, operands[1], operands[2]);
  DONE;
})

(define_expand "rvtt_bh_sfpshft2_e_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")] UNSPECV_BH_SFPSHFT2_E_LV))]
  "TARGET_RVTT_BH"
{
  rtx live = operands[1];
  rvtt_bh_emit_sfpshft2_e(operands[0], live, operands[2], operands[3]);
  DONE;
})

(define_insn "rvtt_bh_sfpshft2_e_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")
                          (match_operand:SI    3 "immediate_operand" "M04U, M04U")] UNSPECV_BH_SFPSHFT2_E_INT))]
  "TARGET_RVTT_BH"
  "SFPSHFT2\t%0, %2, 0, %3")

(define_expand "rvtt_bh_sfpstochrnd_i"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:SI    2 "immediate_operand" "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:SI    4 "register_operand"  "")
                          (match_operand:SI    5 "immediate_operand" "")
                          (match_operand:V64SF 6 "register_operand"  "")
                          (match_operand:SI    7 "immediate_operand" "")] UNSPECV_BH_SFPSTOCHRND_I))]
  "TARGET_RVTT_BH"
{
  rtx live = rvtt_gen_const0_vector();
  rvtt_bh_emit_sfpstochrnd_i(operands[0], live, operands[1], operands[2], operands[3],
                                   operands[6], operands[7], operands[4], operands[5]);
  DONE;
})

(define_expand "rvtt_bh_sfpstochrnd_i_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")
                          (match_operand:SI    4 "nonmemory_operand" "")
                          (match_operand:SI    5 "register_operand"  "")
                          (match_operand:SI    6 "immediate_operand" "")
                          (match_operand:V64SF 7 "register_operand"  "")
                          (match_operand:SI    8 "immediate_operand" "")] UNSPECV_BH_SFPSTOCHRND_I_LV))]
  "TARGET_RVTT_BH"
{
  rtx live = operands[2];
  rvtt_bh_emit_sfpstochrnd_i(operands[0], live, operands[1], operands[3], operands[4],
                                   operands[7], operands[8], operands[5], operands[6]);
  DONE;
})

(define_insn "rvtt_bh_sfpstochrnd_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:SI    2 "immediate_operand" "M01U, M01U")
                          (match_operand:SI    3 "immediate_operand" "M05U, M05U")
                          (match_operand:V64SF 4 "register_operand"  "x, x")
                          (match_operand:SI    5 "immediate_operand" "M04U, M04U")] UNSPECV_BH_SFPSTOCHRND_I_INT))]
  "TARGET_RVTT_BH"
  "SFPSTOCHRND\t%0, L0, %4, %5, %2, %3");

(define_expand "rvtt_bh_sfpstochrnd_v"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "immediate_operand" "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:SI    4 "immediate_operand" "")] UNSPECV_BH_SFPSTOCHRND_V))]
  "TARGET_RVTT_BH"
{
  rtx live = rvtt_gen_const0_vector();
  emit_insn (gen_rvtt_bh_sfpstochrnd_v_int(operands[0], live, operands[1], operands[2], operands[3], operands[4]));
  DONE;
})

(define_expand "rvtt_bh_sfpstochrnd_v_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:SI    2 "immediate_operand" "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:V64SF 4 "register_operand"  "")
                          (match_operand:SI    5 "immediate_operand" "")] UNSPECV_BH_SFPSTOCHRND_V_LV))]
  "TARGET_RVTT_BH"
{
  rtx live = operands[1];
  emit_insn (gen_rvtt_bh_sfpstochrnd_v_int(operands[0], live, operands[2], operands[3], operands[4], operands[5]));
  DONE;
})

(define_insn "rvtt_bh_sfpstochrnd_v_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:SI    2 "immediate_operand" "M01U, M01U")
                          (match_operand:V64SF 3 "register_operand"  "x, x")
                          (match_operand:V64SF 4 "register_operand"  "x, x")
                          (match_operand:SI    5 "immediate_operand" "M04U, M04U")] UNSPECV_BH_SFPSTOCHRND_V_INT))]
  "TARGET_RVTT_BH"
  "SFPSTOCHRND\t%0, %3, %4, %5, %2, 0")

(define_int_iterator blackhole_set_float_op_v [UNSPECV_BH_SFPSETEXP_V UNSPECV_BH_SFPSETMAN_V UNSPECV_BH_SFPSETSGN_V])
(define_int_attr blackhole_set_float_name_v [(UNSPECV_BH_SFPSETEXP_V "exp") (UNSPECV_BH_SFPSETMAN_V "man") (UNSPECV_BH_SFPSETSGN_V "sgn")])
(define_int_attr blackhole_set_float_call_v [(UNSPECV_BH_SFPSETEXP_V "EXP") (UNSPECV_BH_SFPSETMAN_V "MAN") (UNSPECV_BH_SFPSETSGN_V "SGN")])
(define_insn "rvtt_bh_sfpset<blackhole_set_float_name_v>_v"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")] blackhole_set_float_op_v))]
  "TARGET_RVTT_BH"
  "SFPSET<blackhole_set_float_call_v>\t%0, %2, 0, 0"
)

(define_int_iterator blackhole_set_float_op_i [UNSPECV_BH_SFPSETEXP_I UNSPECV_BH_SFPSETSGN_I])
(define_int_attr blackhole_set_float_name_i [(UNSPECV_BH_SFPSETEXP_I "exp") (UNSPECV_BH_SFPSETSGN_I "sgn")])
(define_int_attr blackhole_set_float_call_i [(UNSPECV_BH_SFPSETEXP_I "EXP") (UNSPECV_BH_SFPSETSGN_I "SGN")])
(define_expand "rvtt_bh_sfpset<blackhole_set_float_name_i>_i"
  [(set (match_operand:V64SF 0 "register_operand")
        (unspec_volatile [(match_operand:SI    1 "address_operand")
                          (match_operand:SI    2 "nonmemory_operand")
                          (match_operand:SI    3 "register_operand")
                          (match_operand:SI    4 "immediate_operand")
                          (match_operand:V64SF 5 "register_operand")] blackhole_set_float_op_i))]
  "TARGET_RVTT_BH"
{
  rtx live = rvtt_gen_const0_vector();
  if (GET_CODE(operands[2]) == CONST_INT) {
    emit_insn (gen_rvtt_bh_sfpset<blackhole_set_float_name_i>_i_int(operands[0], live,
                                  rvtt_clamp_unsigned(operands[2], 0xFFF), operands[5]));
  } else {
    unsigned long int op = TT_OP_BH_SFPSET<blackhole_set_float_call_i>(0, 0, 0, 1);
    emit_insn (gen_rvtt_sfpnonimm_dst_src(operands[0], operands[1], GEN_INT(0), live,
                                           operands[5], GEN_INT(4), GEN_INT(8), operands[3], GEN_INT(op), operands[4]));
  }
  DONE;
})

(define_int_iterator blackhole_set_float_op_i_lv [UNSPECV_BH_SFPSETEXP_I_LV UNSPECV_BH_SFPSETSGN_I_LV])
(define_int_attr blackhole_set_float_name_i_lv [(UNSPECV_BH_SFPSETEXP_I_LV "exp") (UNSPECV_BH_SFPSETSGN_I_LV "sgn")])
(define_int_attr blackhole_set_float_call_i_lv [(UNSPECV_BH_SFPSETEXP_I_LV "EXP") (UNSPECV_BH_SFPSETSGN_I_LV "SGN")])
(define_expand "rvtt_bh_sfpset<blackhole_set_float_name_i_lv>_i_lv"
  [(set (match_operand:V64SF 0 "register_operand")
        (unspec_volatile [(match_operand:SI    1 "address_operand")
                          (match_operand:V64SF 2 "register_operand")
                          (match_operand:SI    3 "nonmemory_operand")
                          (match_operand:SI    4 "register_operand")
                          (match_operand:SI    5 "immediate_operand")
                          (match_operand:V64SF 6 "register_operand")] blackhole_set_float_op_i_lv))]
  "TARGET_RVTT_BH"
{
  rtx live = operands[2];
  if (GET_CODE(operands[3]) == CONST_INT) {
    emit_insn (gen_rvtt_bh_sfpset<blackhole_set_float_name_i_lv>_i_int(operands[0], live,
                                   rvtt_clamp_unsigned(operands[3], 0xFFF), operands[6]));
  } else {
    unsigned long int op = TT_OP_BH_SFPSET<blackhole_set_float_call_i_lv>(0, 0, 0, 1);
    emit_insn (gen_rvtt_sfpnonimm_dst_src(operands[0], operands[1], GEN_INT(0),
               live, operands[6], GEN_INT(4), GEN_INT(8), operands[4], GEN_INT(op), operands[5]));
  }
  DONE;
})

(define_int_iterator blackhole_set_float_op_i_int [UNSPECV_BH_SFPSETEXP_I_INT UNSPECV_BH_SFPSETSGN_I_INT])
(define_int_attr blackhole_set_float_name_i_int [(UNSPECV_BH_SFPSETEXP_I_INT "exp") (UNSPECV_BH_SFPSETSGN_I_INT "sgn")])
(define_int_attr blackhole_set_float_call_i_int [(UNSPECV_BH_SFPSETEXP_I_INT "EXP") (UNSPECV_BH_SFPSETSGN_I_INT "SGN")])
(define_insn "rvtt_bh_sfpset<blackhole_set_float_name_i_int>_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:SI    2 "immediate_operand" "M12U, M12U")
                          (match_operand:V64SF 3 "register_operand"  "x, x")] blackhole_set_float_op_i_int))]
  "TARGET_RVTT_BH"
  "SFPSET<blackhole_set_float_call_i_int>\t%0, %3, %2, 1"
)

(define_expand "rvtt_bh_sfpsetman_i"
  [(set (match_operand:V64SF 0 "register_operand")
        (unspec_volatile [(match_operand:SI    1 "address_operand")
                          (match_operand:SI    2 "nonmemory_operand")
                          (match_operand:SI    3 "register_operand")
                          (match_operand:SI    4 "immediate_operand")
                          (match_operand:V64SF 5 "register_operand")
                          (match_operand:SI    6 "immediate_operand")] UNSPECV_BH_SFPSETMAN_I))]
  "TARGET_RVTT_BH"
{
  rtx live = rvtt_gen_const0_vector();
  rvtt_bh_emit_sfpsetman(operands[0], live, operands[1], operands[2], operands[5]);
  DONE;
})

(define_expand "rvtt_bh_sfpsetman_i_lv"
  [(set (match_operand:V64SF 0 "register_operand")
        (unspec_volatile [(match_operand:SI    1 "address_operand")
                          (match_operand:V64SF 2 "register_operand")
                          (match_operand:SI    3 "nonmemory_operand")
                          (match_operand:SI    4 "register_operand")
                          (match_operand:SI    5 "immediate_operand")
                          (match_operand:V64SF 6 "register_operand")
                          (match_operand:SI    7 "immediate_operand")] UNSPECV_BH_SFPSETMAN_I_LV))]
  "TARGET_RVTT_BH"
{
  rtx live = operands[2];
  rvtt_bh_emit_sfpsetman(operands[0], live, operands[1], operands[3], operands[6]);
  DONE;
})

(define_insn "rvtt_bh_sfpsetman_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:SI    2 "immediate_operand" "M12U, M12U")
                          (match_operand:V64SF 3 "register_operand"  "x, x")] UNSPECV_BH_SFPSETMAN_I_INT))]
  "TARGET_RVTT_BH"
  "SFPSETMAN\t%0, %3, %2, 1"
)

(define_expand "rvtt_bh_sfpmad"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:SI    4 "immediate_operand" "")] UNSPECV_BH_SFPMAD))]
  "TARGET_RVTT_BH"
{
  rtx live = rvtt_gen_const0_vector();
  emit_insn (gen_rvtt_bh_sfpmad_int(operands[0], live, operands[1], operands[2], operands[3], operands[4]));
  DONE;
})

(define_expand "rvtt_bh_sfpmad_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:V64SF 4 "register_operand"  "")
                          (match_operand:SI    5 "immediate_operand" "")] UNSPECV_BH_SFPMAD_LV))]
  "TARGET_RVTT_BH"
{
  rtx live = operands[1];
  emit_insn (gen_rvtt_bh_sfpmad_int(operands[0], live, operands[2], operands[3], operands[4], operands[5]));
  DONE;
})

(define_insn "rvtt_bh_sfpmad_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "E, 0")
                          (match_operand:V64SF 2 "register_operand"  "x, x")
                          (match_operand:V64SF 3 "register_operand"  "x, x")
                          (match_operand:V64SF 4 "register_operand"  "x, x")
                          (match_operand:SI    5 "immediate_operand" "M04U, M04U")] UNSPECV_BH_SFPMAD_INT))]
  "TARGET_RVTT_BH"
  "SFPMAD\t%0, %2, %3, %4, %5"
)

(define_insn "rvtt_bh_sfpsetcc_i"
  [(unspec_volatile [(match_operand:SI    0 "immediate_operand" "M01U")
                     (match_operand:SI    1 "immediate_operand" "M04U")] UNSPECV_BH_SFPSETCC_I)]
  "TARGET_RVTT_BH"
  "SFPSETCC\tL0, %0, %1"
)

(define_insn "rvtt_bh_sfpsetcc_v"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "x")
                     (match_operand:SI    1 "immediate_operand" "M04U")] UNSPECV_BH_SFPSETCC_V)]
  "TARGET_RVTT_BH"
  "SFPSETCC\t%0, 0, %1"
)

(define_expand "rvtt_bh_sfpxfcmps"
  [(set (match_operand:SI 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"   "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:SI    4 "nonmemory_operand" "")
                          (match_operand:SI    5 "immediate_operand" "")
                          (match_operand:SI    6 "immediate_operand" "")] UNSPECV_BH_SFPXFCMPS))]
  "TARGET_RVTT_BH"
{
  rvtt_bh_emit_sfpxfcmps(operands[1], operands[2], operands[3], operands[6]);
  DONE;
})

(define_expand "rvtt_bh_sfpxfcmpv"
  [(set (match_operand:SI 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")] UNSPECV_BH_SFPXFCMPV))]
  "TARGET_RVTT_BH"
{
  rvtt_bh_emit_sfpxfcmpv(operands[1], operands[2], operands[3]);
  DONE;
})

(define_insn "rvtt_bh_sfpencc"
  [(unspec_volatile [(match_operand:SI 0 "immediate_operand" "M02U")
                     (match_operand:SI 1 "immediate_operand" "M04U")] UNSPECV_BH_SFPENCC)]
  "TARGET_RVTT_BH"
  "SFPENCC\t%0, %1"
)

(define_insn "rvtt_bh_sfpcompc"
  [(unspec_volatile [(const_int 0)] UNSPECV_BH_SFPCOMPC)]
  "TARGET_RVTT_BH"
  "SFPCOMPC"
)

(define_insn "rvtt_bh_sfppushc"
  [(unspec_volatile [(match_operand:SI 0 "immediate_operand" "M04U")] UNSPECV_BH_SFPPUSHC)]
  "TARGET_RVTT_BH"
  "SFPPUSHC\t%0")

(define_insn "rvtt_bh_sfppopc"
  [(unspec_volatile [(match_operand:SI 0 "immediate_operand" "M04U")] UNSPECV_BH_SFPPOPC)]
  "TARGET_RVTT_BH"
  "SFPPOPC\t%0"
)

(define_insn "rvtt_bh_sfplut"
  [(set (match_operand:V64SF 0 "register_operand" "=Q3")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "Q0")
                          (match_operand:V64SF 2 "register_operand"  "Q1")
                          (match_operand:V64SF 3 "register_operand"  "Q2")
                          (match_operand:V64SF 4 "register_operand"  "0")
                          (match_operand:SI    5 "immediate_operand" "M04U")] UNSPECV_BH_SFPLUT))]
  "TARGET_RVTT_BH"
  "SFPLUT\t%0, %5"
)

(define_insn "rvtt_bh_sfplutfp32_3r"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "Q0")
                          (match_operand:V64SF 2 "register_operand"  "Q1")
                          (match_operand:V64SF 3 "register_operand"  "Q2")
                          (match_operand:V64SF 4 "register_operand"  "Q3")
                          (match_operand:SI    5 "immediate_operand" "M04U")] UNSPECV_BH_SFPLUTFP32_3R))
        (clobber (match_scratch:V64SF 6 "=Q7"))
        (match_scratch:SI 7)]
  "TARGET_RVTT_BH"
{
  // Note: this insn must emit 2 insns, ie, this can't be done in an expand as
  // the hard regno is only known at reload time, not at expand time
  // This mean, e.g., the REPLAY pass must know this insn is really 2 insns
  operands[7] = GEN_INT(rvtt_sfpu_regno(operands[0]));
  output_asm_insn("SFPLOADI\t%6, %7, 2", operands);
  return "SFPLUTFP32\t%0, %5";
})

(define_insn "rvtt_bh_sfplutfp32_6r"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "Q0")
                          (match_operand:V64SF 2 "register_operand"  "Q1")
                          (match_operand:V64SF 3 "register_operand"  "Q2")
                          (match_operand:V64SF 4 "register_operand"  "Q4")
                          (match_operand:V64SF 5 "register_operand"  "Q5")
                          (match_operand:V64SF 6 "register_operand"  "Q6")
                          (match_operand:V64SF 7 "register_operand"  "Q3")
                          (match_operand:SI    8 "immediate_operand" "M04U")] UNSPECV_BH_SFPLUTFP32_6R))]
  "TARGET_RVTT_BH"
  "SFPLUTFP32\t%0, %8")

(define_insn "rvtt_bh_sfpconfig_v"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"   "Q0")
                     (match_operand:SI    1 "immediate_operand"  "M04U")] UNSPECV_BH_SFPCONFIG_V)]
  "TARGET_RVTT_BH"
  "SFPCONFIG\t%1, 0, 0")

(define_insn "rvtt_bh_sfpreplay"
  [(unspec_volatile [(match_operand:SI    0 "immediate_operand"  "M04U")
                     (match_operand:SI    1 "immediate_operand"  "MP5U")
                     (match_operand:SI    2 "immediate_operand"  "M01U")
                     (match_operand:SI    3 "immediate_operand"  "M01U")] UNSPECV_BH_SFPREPLAY)]
  "TARGET_RVTT_BH"
  "TTREPLAY\t%0, %1, %2, %3")

(define_insn "rvtt_bh_sfpswap"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"   "+x")
                     (match_operand:V64SF 1 "register_operand"   "+x")
                     (match_operand:SI    2 "immediate_operand"  "M04U")] UNSPECV_BH_SFPSWAP)]
  "TARGET_RVTT_BH"
  "SFPSWAP\t%0, %1, %2")

(define_insn "rvtt_bh_sfptransp"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"   "+Q0")
                     (match_operand:V64SF 1 "register_operand"   "+Q1")
                     (match_operand:V64SF 2 "register_operand"   "+Q2")
                     (match_operand:V64SF 3 "register_operand"   "+Q3")] UNSPECV_BH_SFPTRANSP)]
  "TARGET_RVTT_BH"
  "SFPTRANSP")

(define_insn "rvtt_bh_sfpshft2_g"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"   "+Q0")
                     (match_operand:V64SF 1 "register_operand"   "+Q1")
                     (match_operand:V64SF 2 "register_operand"   "+Q2")
                     (match_operand:V64SF 3 "register_operand"   "+Q3")
                     (match_operand:SI    4 "immediate_operand"  "M04U")] UNSPECV_BH_SFPSHFT2_G)]
  "TARGET_RVTT_BH"
  "SFPSHFT2\t0, L0, L0, %0, %1, %2, %3, %4")

(define_insn "rvtt_bh_sfpshft2_ge"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"   "x")
                     (match_operand:V64SF 1 "register_operand"   "+Q0")
                     (match_operand:V64SF 2 "register_operand"   "+Q1")
                     (match_operand:V64SF 3 "register_operand"   "+Q2")
                     (match_operand:V64SF 4 "register_operand"   "+Q3")] UNSPECV_BH_SFPSHFT2_GE)]
  "TARGET_RVTT_BH"
  "SFPSHFT2\t0, %0, L0, %1, %2, %3, %4, 2")

(define_insn "rvtt_bh_sfpnop"
  [(unspec_volatile [(const_int 0)] UNSPECV_BH_SFPNOP)]
  "TARGET_RVTT_BH"
  "SFPNOP")

(define_insn "rvtt_bh_sfpmul24"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M04U")] UNSPECV_BH_SFPMUL24))]
  "TARGET_RVTT_BH"
  "SFPMUL24\t%0, %1, %2, %3"
)

(define_insn "rvtt_bh_sfpmul24_lv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand" "0")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:V64SF 3 "register_operand"  "x")
                          (match_operand:SI    4 "immediate_operand" "M04U")] UNSPECV_BH_SFPMUL24))]
  "TARGET_RVTT_BH"
  "SFPMUL24\t%0, %2, %3, %4"
)

(define_insn "rvtt_bh_sfparecip"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:SI    2 "immediate_operand" "")] UNSPECV_BH_SFPARECIP))]
  "TARGET_RVTT_BH"
  "SFPARECIP\t%0, %1, %2"
)

(define_insn "rvtt_bh_sfparecip_lv"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "x")
                          (match_operand:SI    3 "immediate_operand" "M04U")] UNSPECV_BH_SFPARECIP))]
  "TARGET_RVTT_BH"
  "SFPARECIP\t%0, %2, %3"
)

(define_insn "rvtt_bh_sfpgt"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:SI    2 "immediate_operand" "M04U")] UNSPECV_BH_SFPGT))]
  "TARGET_RVTT_BH"
  "SFPGT\t%0, %1, 0, %2"
)

(define_insn "rvtt_bh_sfple"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x")
                          (match_operand:SI    2 "immediate_operand" "M04U")] UNSPECV_BH_SFPLE))]
  "TARGET_RVTT_BH"
  "SFPLE\t%0, %1, 0, %2"
)

(include "tt/rvtt-peephole-bh.md")
