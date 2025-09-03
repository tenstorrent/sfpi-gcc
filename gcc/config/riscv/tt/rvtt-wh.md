;; Machine description for Tenstorrent SFPU Wormhole Intrinsics.
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

; & in spec means early clobber, written before inputs are used, cannot reuse input reg

(define_c_enum "unspecv" [
  ;; Tenstorrent SFPU unspecs.
  ;; INT for internal
  ;; LV for keep dst reg alive as input for predicated liveness
  UNSPECV_WH_SFPASSIGN_LV
  UNSPECV_WH_SFPLOAD
  UNSPECV_WH_SFPLOAD_LV
  UNSPECV_WH_SFPLOAD_INT
  UNSPECV_WH_SFPXLOADI
  UNSPECV_WH_SFPXLOADI_LV
  UNSPECV_WH_SFPLOADI_INT
  UNSPECV_WH_SFPSTORE
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
])

(define_insn "rvtt_wh_sfpgccmov_cc"
  [(set (match_operand:V64SF 0 "nonimmediate_operand" "=xr,xr,m")
        (match_operand:V64SF 1 "nonimmediate_operand" " xr,m,xr"))]
  "TARGET_RVTT_WH  &&
   (   register_operand (operands[0], V64SFmode)
    || register_operand (operands[1], V64SFmode))"
  {
    if (which_alternative == 0)
      return "SFPMOV\t%0, %1, 2";

    rvtt_mov_error (insn, which_alternative == 1);
    gcc_unreachable ();
  })

(define_insn "rvtt_wh_sfpassign_lv"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "register_operand"  "0")
                                (match_operand:V64SF 2 "register_operand"  "xr")] UNSPECV_WH_SFPASSIGN_LV))]
  "TARGET_RVTT_WH"
  "SFPMOV\t%0, %2, 0"
)

(define_expand "rvtt_wh_sfpload"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile:V64SF [(match_operand:SI 1 "address_operand"  "")
                                (match_operand:SI 2 "const_int_operand" "")
                                (match_operand:SI 3 "const_int_operand" "")
                                (match_operand:SI 4 "reg_or_const_int_operand" "")
                                (match_operand:SI 5 "reg_or_0_operand" "")
                                (match_operand:SI 6 "const_int_operand" "")] UNSPECV_WH_SFPLOAD))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_vec0_rtx;
  rvtt_wh_emit_sfpload(operands[0], live, operands[1], operands[2], operands[3], operands[4], operands[5], operands[6]);
  DONE;
})

(define_expand "rvtt_wh_sfpload_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile:V64SF [(match_operand:SI    1 "address_operand"  "")
                                (match_operand:V64SF 2 "register_operand"  "")
                                (match_operand:SI    3 "const_int_operand" "")
                                (match_operand:SI    4 "const_int_operand" "")
                                (match_operand:SI    5 "reg_or_const_int_operand" "")
                                (match_operand:SI    6 "reg_or_0_operand" "")
                                (match_operand:SI    7 "const_int_operand" "")] UNSPECV_WH_SFPLOAD_LV))]
  "TARGET_RVTT_WH"
{
  rtx live = operands[2];
  rvtt_wh_emit_sfpload(operands[0], live, operands[1], operands[3], operands[4], operands[5], operands[6], operands[7]);
  DONE;
})

(define_insn "rvtt_wh_sfpload_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr, xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "reg_or_vec0_operand" "xn, 0")
                                (match_operand:SI    2 "const_int_operand" "N04U,N04U")
                                (match_operand:SI    3 "const_int_operand" "N02U,N02U")
                                (match_operand:SI    4 "const_int_operand" "N14U,N14U")] UNSPECV_WH_SFPLOAD_INT))]
  "TARGET_RVTT_WH"
  "@
   SFPLOAD\t%0, %4, %2, %3
   SFPLOAD\t%0, %4, %2, %3")

;;; SFPLOADI and SFPLOADI_LV
(define_expand "rvtt_wh_sfpxloadi"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile:V64SF [(match_operand:SI 1 "address_operand"  "")
                                (match_operand:SI 2 "const_int_operand" "")
                                (match_operand:SI 3 "reg_or_const_int_operand" "")
                                (match_operand:SI 4 "reg_or_0_operand"  "")
                                (match_operand:SI 5 "const_int_operand" "")] UNSPECV_WH_SFPXLOADI))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_vec0_rtx;
  rvtt_wh_emit_sfpxloadi(operands[0], live, operands[1], operands[2], operands[3], operands[4], operands[5]);
  DONE;
})

(define_expand "rvtt_wh_sfpxloadi_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile:V64SF [(match_operand:SI    1 "address_operand"   "")
                                (match_operand:V64SF 2 "register_operand"  "")
                                (match_operand:SI    3 "const_int_operand" "")
                                (match_operand:SI    4 "reg_or_const_int_operand" "")
                                (match_operand:SI    5 "reg_or_0_operand"  "")
                                (match_operand:SI    6 "const_int_operand" "")] UNSPECV_WH_SFPXLOADI_LV))]
  "TARGET_RVTT_WH"
{
  rtx live = operands[2];
  rvtt_wh_emit_sfpxloadi(operands[0], live, operands[1], operands[3], operands[4], operands[5], operands[6]);
  DONE;
})

(define_insn "rvtt_wh_sfploadi_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr,xr,xr,xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "reg_or_vec0_operand" "xn,xn,0,0")
                                (match_operand:SI    2 "const_int_operand" "N04U,N04U,N04U,N04U")
                                (match_operand:SI    3 "const_int_operand" "N16S,N16U,N16S,N16U")] UNSPECV_WH_SFPLOADI_INT))]
  "TARGET_RVTT_WH"
  "@
  SFPLOADI\t%0, %s3, %2
  SFPLOADI\t%0, %u3, %2
  SFPLOADI\t%0, %s3, %2
  SFPLOADI\t%0, %u3, %2")

(define_expand "rvtt_wh_sfpstore"
  [(unspec_volatile [(match_operand:SI    0 "address_operand"   "")
                     (match_operand:V64SF 1 "register_operand"  "")
                     (match_operand:SI    2 "const_int_operand" "")
                     (match_operand:SI    3 "const_int_operand" "")
                     (match_operand:SI    4 "reg_or_const_int_operand" "")
                     (match_operand:SI    5 "reg_or_0_operand" "")
                     (match_operand:SI    6 "const_int_operand" "")] UNSPECV_WH_SFPSTORE)]
  "TARGET_RVTT_WH"
{
  rtx insn = nullptr;
  if (GET_CODE(operands[4]) == CONST_INT)
    insn = gen_rvtt_wh_sfpstore_int (operands[1], operands[2],operands[3],
				      rvtt_clamp_unsigned (operands[4], 0x3FFF));
  else
    {
      unsigned op = TT_OP_WH_SFPSTORE(0, INTVAL (operands[2]), INTVAL (operands[3]), 0);
      insn = rvtt_sfpsynth_store_insn (operands[0], 0, operands[5], op, operands[6],
				       operands[1], 20);
    }
  emit_insn (insn);
  DONE;
})

;; stores cannot write from L12..L15 due to load macro side loading possibility
(define_insn "rvtt_wh_sfpstore_int"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "xs")
                     (match_operand:SI    1 "const_int_operand" "N04U")
                     (match_operand:SI    2 "const_int_operand" "N02U")
                     (match_operand:SI    3 "const_int_operand" "N14U")] UNSPECV_WH_SFPSTORE)]
  "TARGET_RVTT_WH"
  "SFPSTORE\t%3, %0, %1, %2")

(define_int_iterator wormhole_muliaddi [UNSPECV_WH_SFPMULI UNSPECV_WH_SFPADDI])
(define_int_attr wormhole_muliaddi_name [(UNSPECV_WH_SFPMULI "muli") (UNSPECV_WH_SFPADDI "addi")])
(define_int_attr wormhole_muliaddi_call [(UNSPECV_WH_SFPMULI "MULI") (UNSPECV_WH_SFPADDI "ADDI")])
(define_expand "rvtt_wh_sfp<wormhole_muliaddi_name>"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "reg_or_const_int_operand" "")
                          (match_operand:SI    4 "reg_or_0_operand"  "")
                          (match_operand:SI    5 "const_int_operand" "")
                          (match_operand:SI    6 "const_int_operand" "")] wormhole_muliaddi))]
  "TARGET_RVTT_WH"
{
  rtx insn = nullptr;
  if (GET_CODE(operands[3]) == CONST_INT)
    insn = gen_rvtt_wh_sfp<wormhole_muliaddi_name>_int
      (operands[0], operands[2], rvtt_clamp_unsigned (operands[3], 0xFFFF), operands[6]);
  else
    {
      unsigned op = TT_OP_WH_SFP<wormhole_muliaddi_call>(0, 0, INTVAL(operands[6]));
      insn = rvtt_sfpsynth_insn_dst (operands[1], INSN_SCHED_DYN, operands[4], op, operands[5],
				     operands[0], 4, operands[2]);
    }
  emit_insn (insn);
  DONE;
})

(define_int_iterator wormhole_muliaddi_int [UNSPECV_WH_SFPMULI_INT UNSPECV_WH_SFPADDI_INT])
(define_int_attr wormhole_muliaddi_int_name [(UNSPECV_WH_SFPMULI_INT "muli") (UNSPECV_WH_SFPADDI_INT "addi")])
(define_int_attr wormhole_muliaddi_int_call [(UNSPECV_WH_SFPMULI_INT "MULI") (UNSPECV_WH_SFPADDI_INT "ADDI")])
(define_insn "rvtt_wh_sfp<wormhole_muliaddi_int_name>_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "register_operand"  "0")
                                (match_operand:SI    2 "const_int_operand" "N16U")
                                (match_operand:SI    3 "const_int_operand" "N04U")] wormhole_muliaddi_int))]
  "TARGET_RVTT_WH"
  "SFP<wormhole_muliaddi_int_call>\t%0, %2, %3"
)

(define_expand "rvtt_wh_sfpdivp2"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile:V64SF [(match_operand:SI    1 "address_operand"  "")
                                (match_operand:SI    2 "reg_or_const_int_operand" "")
                                (match_operand:SI    3 "reg_or_0_operand"  "")
                                (match_operand:SI    4 "const_int_operand" "")
                                (match_operand:V64SF 5 "register_operand"  "")
                                (match_operand:SI    6 "const_int_operand" "")] UNSPECV_WH_SFPDIVP2))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_vec0_rtx;
  rvtt_wh_emit_sfpdivp2(operands[0], live, operands[1], operands[2], operands[5], operands[6], operands[3], operands[4]);
  DONE;
})

(define_expand "rvtt_wh_sfpdivp2_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile:V64SF [(match_operand:SI    1 "address_operand"  "")
                                (match_operand:V64SF 2 "register_operand"  "")
                                (match_operand:SI    3 "reg_or_const_int_operand" "")
                                (match_operand:SI    4 "reg_or_0_operand"  "")
                                (match_operand:SI    5 "const_int_operand" "")
                                (match_operand:V64SF 6 "register_operand"  "")
                                (match_operand:SI    7 "const_int_operand" "")] UNSPECV_WH_SFPDIVP2_LV))]
  "TARGET_RVTT_WH"
{
  rtx live = operands[2];
  rvtt_wh_emit_sfpdivp2(operands[0], live, operands[1], operands[3], operands[6], operands[7], operands[4], operands[5]);
  DONE;
})

(define_insn "rvtt_wh_sfpdivp2_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr, xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "reg_or_vec0_operand" "xn, 0")
                                (match_operand:SI    2 "const_int_operand" "N12S,N12S")
                                (match_operand:V64SF 3 "register_operand"  "xr, xr")
                                (match_operand:SI    4 "const_int_operand" "N04U,N04U")] UNSPECV_WH_SFPDIVP2_INT))]
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
                          (match_operand:SI    2 "const_int_operand" "")] wormhole_simple_op))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_vec0_rtx;
  emit_insn (gen_rvtt_wh_sfp<wormhole_simple_op_name>_int(operands[0], live, operands[1], operands[2]));
  DONE;
})

(define_expand "rvtt_wh_sfp<wormhole_simple_op_name_lv>_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "const_int_operand" "")] wormhole_simple_op_lv))]
  "TARGET_RVTT_WH"
{
  emit_insn (gen_rvtt_wh_sfp<wormhole_simple_op_name_lv>_int(operands[0], operands[1], operands[2], operands[3]));
  DONE;
})

(define_insn "rvtt_wh_sfp<wormhole_simple_op_name_int>_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr, xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "reg_or_vec0_operand" "xn,0")
                                (match_operand:V64SF 2 "register_operand"  "xr, xr")
                                (match_operand:SI    3 "const_int_operand" "N04U,N04U")] wormhole_simple_op_int))]
  "TARGET_RVTT_WH"
  "SFP<wormhole_simple_op_call_int>\t%0, %2, %3"
)

(define_int_iterator wormhole_muladd [UNSPECV_WH_SFPMUL UNSPECV_WH_SFPADD])
(define_int_attr wormhole_muladd_name [(UNSPECV_WH_SFPMUL "mul") (UNSPECV_WH_SFPADD "add")])
(define_expand "rvtt_wh_sfp<wormhole_muladd_name>"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "register_operand"  "")
                                (match_operand:V64SF 2 "register_operand"  "")
                                (match_operand:SI    3 "const_int_operand" "")] wormhole_muladd))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_vec0_rtx;
  emit_insn (gen_rvtt_wh_sfp<wormhole_muladd_name>_int(operands[0], live, operands[1], operands[2], operands[3]));
  DONE;
})

(define_int_iterator wormhole_muladd_lv [UNSPECV_WH_SFPMUL_LV UNSPECV_WH_SFPADD_LV])
(define_int_attr wormhole_muladd_name_lv [(UNSPECV_WH_SFPMUL_LV "mul") (UNSPECV_WH_SFPADD_LV "add")])
(define_expand "rvtt_wh_sfp<wormhole_muladd_name_lv>_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "register_operand"  "")
                                (match_operand:V64SF 2 "register_operand"  "")
                                (match_operand:V64SF 3 "register_operand"  "")
                                (match_operand:SI    4 "const_int_operand" "")] wormhole_muladd_lv))]
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
  [(set (match_operand:V64SF 0 "register_operand" "=xr, xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "reg_or_vec0_operand" "xn,0")
                                (match_operand:V64SF 2 "register_operand"  "xr, xr")
                                (match_operand:V64SF 3 "register_operand"  "xr, xr")
                                (match_operand:SI    4 "const_int_operand" "N04U,N04U")] wormhole_muladd_int))]
  "TARGET_RVTT_WH"
  "SFP<wormhole_muladd_call_int>, %4"
)

(define_insn "rvtt_wh_sfpiadd_v_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "register_operand"  "0")
                                (match_operand:V64SF 2 "register_operand"  "xr")
                                (match_operand:SI    3 "const_int_operand" "N04U")] UNSPECV_WH_SFPIADD_V_INT))]
  "TARGET_RVTT_WH"
  "SFPIADD\t%0, %2, 0, %3"
)

(define_insn "rvtt_wh_sfpiadd_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr, xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "reg_or_vec0_operand" "xn,0")
                                (match_operand:V64SF 2 "register_operand"  "xr, xr")
                                (match_operand:SI    3 "const_int_operand" "n, n")
                                (match_operand:SI    4 "const_int_operand" "N04U,N04U")] UNSPECV_WH_SFPIADD_I_INT))]
  "TARGET_RVTT_WH"
  "SFPIADD\t%0, %2, %3, %4"
)

(define_expand "rvtt_wh_sfpxiadd_v"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "register_operand"  "0")
                                (match_operand:V64SF 2 "register_operand"  "xr")
                                (match_operand:SI    3 "const_int_operand" "N04U")] UNSPECV_WH_SFPXIADD_V))]
  "TARGET_RVTT_WH"
{
  rvtt_wh_emit_sfpxiadd_v(operands[0], operands[1], operands[2], operands[3]);
  DONE;
})


(define_expand "rvtt_wh_sfpxiadd_i"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile:V64SF [(match_operand:SI    1 "address_operand"  "")
                                (match_operand:V64SF 2 "register_operand"  "")
                                (match_operand:SI    3 "reg_or_const_int_operand" "")
                                (match_operand:SI    4 "const_int_operand" "")
                                (match_operand:SI    5 "reg_or_const_int_operand" "")
                                (match_operand:SI    6 "const_int_operand" "")] UNSPECV_WH_SFPXIADD_I))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_vec0_rtx;
  rvtt_wh_emit_sfpxiadd_i(operands[0], live, operands[1], operands[2], operands[3], operands[6]);
  DONE;
})

(define_expand "rvtt_wh_sfpxiadd_i_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile:V64SF [(match_operand:SI    1 "address_operand"   "")
                                (match_operand:V64SF 2 "register_operand"  "")
                                (match_operand:V64SF 3 "register_operand"  "")
                                (match_operand:SI    4 "reg_or_const_int_operand" "")
                                (match_operand:SI    5 "const_int_operand" "")
                                (match_operand:SI    6 "reg_or_const_int_operand" "")
                                (match_operand:SI    7 "const_int_operand" "")] UNSPECV_WH_SFPXIADD_I_LV))]
  "TARGET_RVTT_WH"
{
  rtx live = operands[2];
  rvtt_wh_emit_sfpxiadd_i(operands[0], live, operands[1], operands[3], operands[4], operands[7]);
  DONE;
})

(define_insn "rvtt_wh_sfpshft_v"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "register_operand"  "0")
                                (match_operand:V64SF 2 "register_operand"  "xr")] UNSPECV_WH_SFPSHFT_V))]
  "TARGET_RVTT_WH"
  "SFPSHFT\t%0, %2, 0, 0"
)

(define_expand "rvtt_wh_sfpshft_i"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile:V64SF [(match_operand:SI    1 "address_operand"  "")
                                (match_operand:V64SF 2 "register_operand"  "")
                                (match_operand:SI    3 "reg_or_const_int_operand" "")
                                (match_operand:SI    4 "reg_or_0_operand" "")
                                (match_operand:SI    5 "const_int_operand" "")] UNSPECV_WH_SFPSHFT_I))]
  "TARGET_RVTT_WH"
{
  rtx insn = nullptr;
  if (GET_CODE(operands[3]) == CONST_INT)
    insn = gen_rvtt_wh_sfpshft_i_int(operands[0], operands[2], rvtt_clamp_signed(operands[3], 0x7FF));
  else
    {
      unsigned op = TT_OP_WH_SFPSHFT(0, 0, 0, 1);
      insn = rvtt_sfpsynth_insn_dst (operands[1], 0, operands[4], op, operands[5],
				     operands[0], 4, operands[2]);
    }
  emit_insn (insn);
  DONE;
})

(define_insn "rvtt_wh_sfpshft_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "register_operand"  "0")
                                (match_operand:SI    2 "const_int_operand" "N12S")] UNSPECV_WH_SFPSHFT_I_INT))]
  "TARGET_RVTT_WH"
  "SFPSHFT\t%0, L0, %2, 1"
)

(define_insn "rvtt_wh_sfpand"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "register_operand"  "0")
                                (match_operand:V64SF 2 "register_operand"  "xr")] UNSPECV_WH_SFPAND))]
  "TARGET_RVTT_WH"
  "SFPAND\t%0, %2"
)

(define_insn "rvtt_wh_sfpor"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "register_operand"  "0")
                                (match_operand:V64SF 2 "register_operand"  "xr")] UNSPECV_WH_SFPOR))]
  "TARGET_RVTT_WH"
  "SFPOR\t%0, %2"
)

(define_insn "rvtt_wh_sfpxor"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "register_operand"  "0")
                                (match_operand:V64SF 2 "register_operand"  "xr")] UNSPECV_WH_SFPXOR))]
  "TARGET_RVTT_WH"
  "SFPXOR\t%0, %2"
)

(define_expand "rvtt_wh_sfpnot"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "register_operand"  "")] UNSPECV_WH_SFPNOT))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_vec0_rtx;
  emit_insn (gen_rvtt_wh_sfpnot_int(operands[0], live, operands[1]));
  DONE;
})

(define_expand "rvtt_wh_sfpnot_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "register_operand"  "")
                                (match_operand:V64SF 2 "register_operand"  "")] UNSPECV_WH_SFPNOT_LV))]
  "TARGET_RVTT_WH"
{
  rtx live = operands[1];
  emit_insn (gen_rvtt_wh_sfpnot_int(operands[0], live, operands[2]));
  DONE;
})

(define_insn "rvtt_wh_sfpnot_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr, xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "reg_or_vec0_operand" "xn,0")
                                (match_operand:V64SF 2 "register_operand"  "xr, xr")] UNSPECV_WH_SFPNOT_INT))]
  "TARGET_RVTT_WH"
  "SFPNOT\t%0, %2"
)

(define_expand "rvtt_wh_sfpcast"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "register_operand"  "")
                                (match_operand:SI    2 "const_int_operand" "")] UNSPECV_WH_SFPCAST))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_vec0_rtx;
  emit_insn (gen_rvtt_wh_sfpcast_int(operands[0], live, operands[1], operands[2]));
  DONE;
})

(define_expand "rvtt_wh_sfpcast_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "register_operand"  "")
                                (match_operand:V64SF 2 "register_operand"  "")
                                (match_operand:SI    3 "const_int_operand" "")] UNSPECV_WH_SFPCAST_LV))]
  "TARGET_RVTT_WH"
{
  rtx live = operands[2];
  emit_insn (gen_rvtt_wh_sfpcast_int(operands[0], live, operands[2], operands[3]));
  DONE;
})

(define_insn "rvtt_wh_sfpcast_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr, xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "reg_or_vec0_operand" "xn,0")
                                (match_operand:V64SF 2 "register_operand"  "xr, xr")
                                (match_operand:SI    3 "const_int_operand" "N04U,N04U")] UNSPECV_WH_SFPCAST_INT))]
  "TARGET_RVTT_WH"
  "SFPCAST %0, %2, %3")

(define_expand "rvtt_wh_sfpshft2_e"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "register_operand"  "")
                                (match_operand:SI    2 "const_int_operand" "")] UNSPECV_WH_SFPSHFT2_E))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_vec0_rtx;
  rvtt_wh_emit_sfpshft2_e(operands[0], live, operands[1], operands[2]);
  DONE;
})

(define_expand "rvtt_wh_sfpshft2_e_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "register_operand"  "")
                                (match_operand:V64SF 2 "register_operand"  "")
                                (match_operand:SI    3 "const_int_operand" "")] UNSPECV_WH_SFPSHFT2_E_LV))]
  "TARGET_RVTT_WH"
{
  rtx live = operands[1];
  rvtt_wh_emit_sfpshft2_e(operands[0], live, operands[2], operands[3]);
  DONE;
})

(define_insn "rvtt_wh_sfpshft2_e_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr, xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "reg_or_vec0_operand" "xn,0")
                                (match_operand:V64SF 2 "register_operand"  "xr, xr")
                                (match_operand:SI    3 "const_int_operand" "N04U,N04U")] UNSPECV_WH_SFPSHFT2_E_INT))]
  "TARGET_RVTT_WH"
  "SFPSHFT2\t%0, %2, 0, %3")

(define_expand "rvtt_wh_sfpstochrnd_i"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile:V64SF [(match_operand:SI    1 "address_operand"  "")
                                (match_operand:SI    2 "const_int_operand" "")
                                (match_operand:SI    3 "reg_or_const_int_operand" "")
                                (match_operand:SI    4 "reg_or_0_operand"  "")
                                (match_operand:SI    5 "const_int_operand" "")
                                (match_operand:V64SF 6 "register_operand"  "")
                                (match_operand:SI    7 "const_int_operand" "")] UNSPECV_WH_SFPSTOCHRND_I))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_vec0_rtx;
  rvtt_wh_emit_sfpstochrnd_i(operands[0], live, operands[1], operands[2], operands[3],
                                   operands[6], operands[7], operands[4], operands[5]);
  DONE;
})

(define_expand "rvtt_wh_sfpstochrnd_i_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile:V64SF [(match_operand:SI    1 "address_operand"  "")
                                (match_operand:V64SF 2 "register_operand"  "")
                                (match_operand:SI    3 "const_int_operand" "")
                                (match_operand:SI    4 "reg_or_const_int_operand" "")
                                (match_operand:SI    5 "reg_or_0_operand"  "")
                                (match_operand:SI    6 "const_int_operand" "")
                                (match_operand:V64SF 7 "register_operand"  "")
                                (match_operand:SI    8 "const_int_operand" "")] UNSPECV_WH_SFPSTOCHRND_I_LV))]
  "TARGET_RVTT_WH"
{
  rtx live = operands[2];
  rvtt_wh_emit_sfpstochrnd_i(operands[0], live, operands[1], operands[3], operands[4],
                                   operands[7], operands[8], operands[5], operands[6]);
  DONE;
})

(define_insn "rvtt_wh_sfpstochrnd_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr, xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "reg_or_vec0_operand" "xn,0")
                                (match_operand:SI    2 "const_int_operand" "N01U,N01U")
                                (match_operand:SI    3 "const_int_operand" "N05U,N05U")
                                (match_operand:V64SF 4 "register_operand"  "xr, xr")
                                (match_operand:SI    5 "const_int_operand" "N04U,N04U")] UNSPECV_WH_SFPSTOCHRND_I_INT))]
  "TARGET_RVTT_WH"
  "SFPSTOCHRND\t%0, L0, %4, %5, %2, %3");

(define_expand "rvtt_wh_sfpstochrnd_v"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile:V64SF [(match_operand:SI    1 "const_int_operand" "")
                                (match_operand:V64SF 2 "register_operand"  "")
                                (match_operand:V64SF 3 "register_operand"  "")
                                (match_operand:SI    4 "const_int_operand" "")] UNSPECV_WH_SFPSTOCHRND_V))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_vec0_rtx;
  emit_insn (gen_rvtt_wh_sfpstochrnd_v_int(operands[0], live, operands[1], operands[2], operands[3], operands[4]));
  DONE;
})

(define_expand "rvtt_wh_sfpstochrnd_v_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "register_operand"  "")
                                (match_operand:SI    2 "const_int_operand" "")
                                (match_operand:V64SF 3 "register_operand"  "")
                                (match_operand:V64SF 4 "register_operand"  "")
                                (match_operand:SI    5 "const_int_operand" "")] UNSPECV_WH_SFPSTOCHRND_V_LV))]
  "TARGET_RVTT_WH"
{
  rtx live = operands[1];
  emit_insn (gen_rvtt_wh_sfpstochrnd_v_int(operands[0], live, operands[2], operands[3], operands[4], operands[5]));
  DONE;
})

(define_insn "rvtt_wh_sfpstochrnd_v_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr, xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "reg_or_vec0_operand" "xn,0")
                                (match_operand:SI    2 "const_int_operand" "N01U,N01U")
                                (match_operand:V64SF 3 "register_operand"  "xr, xr")
                                (match_operand:V64SF 4 "register_operand"  "xr, xr")
                                (match_operand:SI    5 "const_int_operand" "N04U,N04U")] UNSPECV_WH_SFPSTOCHRND_V_INT))]
  "TARGET_RVTT_WH"
  "SFPSTOCHRND\t%0, %3, %4, %5, %2, 0")

(define_int_iterator wormhole_set_float_op_v [UNSPECV_WH_SFPSETEXP_V UNSPECV_WH_SFPSETMAN_V UNSPECV_WH_SFPSETSGN_V])
(define_int_attr wormhole_set_float_name_v [(UNSPECV_WH_SFPSETEXP_V "exp") (UNSPECV_WH_SFPSETMAN_V "man") (UNSPECV_WH_SFPSETSGN_V "sgn")])
(define_int_attr wormhole_set_float_call_v [(UNSPECV_WH_SFPSETEXP_V "EXP") (UNSPECV_WH_SFPSETMAN_V "MAN") (UNSPECV_WH_SFPSETSGN_V "SGN")])
(define_insn "rvtt_wh_sfpset<wormhole_set_float_name_v>_v"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "register_operand"  "0")
                                (match_operand:V64SF 2 "register_operand"  "xr")] wormhole_set_float_op_v))]
  "TARGET_RVTT_WH"
  "SFPSET<wormhole_set_float_call_v>\t%0, %2, 0, 0"
)

(define_int_iterator wormhole_set_float_op_i [UNSPECV_WH_SFPSETEXP_I UNSPECV_WH_SFPSETSGN_I])
(define_int_attr wormhole_set_float_name_i [(UNSPECV_WH_SFPSETEXP_I "exp") (UNSPECV_WH_SFPSETSGN_I "sgn")])
(define_int_attr wormhole_set_float_call_i [(UNSPECV_WH_SFPSETEXP_I "EXP") (UNSPECV_WH_SFPSETSGN_I "SGN")])
(define_expand "rvtt_wh_sfpset<wormhole_set_float_name_i>_i"
  [(set (match_operand:V64SF 0 "register_operand")
        (unspec_volatile:V64SF [(match_operand:SI    1 "address_operand")
                                (match_operand:SI    2 "reg_or_const_int_operand")
                                (match_operand:SI    3 "reg_or_0_operand")
                                (match_operand:SI    4 "const_int_operand")
                                (match_operand:V64SF 5 "register_operand")] wormhole_set_float_op_i))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_vec0_rtx;
  rtx insn = nullptr;
  if (GET_CODE(operands[2]) == CONST_INT)
    insn = gen_rvtt_wh_sfpset<wormhole_set_float_name_i>_i_int
      (operands[0], live, rvtt_clamp_unsigned(operands[2], 0xFFF), operands[5]);
  else
    {
      unsigned op = TT_OP_WH_SFPSET<wormhole_set_float_call_i>(0, 0, 0, 1);
      insn = rvtt_sfpsynth_insn_dst (operands[1], 0, operands[3], op, operands[4],
				     operands[5], 4, operands[0], 8, live);
    }
  emit_insn (insn);
  DONE;
})

(define_int_iterator wormhole_set_float_op_i_lv [UNSPECV_WH_SFPSETEXP_I_LV UNSPECV_WH_SFPSETSGN_I_LV])
(define_int_attr wormhole_set_float_name_i_lv [(UNSPECV_WH_SFPSETEXP_I_LV "exp") (UNSPECV_WH_SFPSETSGN_I_LV "sgn")])
(define_int_attr wormhole_set_float_call_i_lv [(UNSPECV_WH_SFPSETEXP_I_LV "EXP") (UNSPECV_WH_SFPSETSGN_I_LV "SGN")])
(define_expand "rvtt_wh_sfpset<wormhole_set_float_name_i_lv>_i_lv"
  [(set (match_operand:V64SF 0 "register_operand")
        (unspec_volatile:V64SF [(match_operand:SI    1 "address_operand")
                                (match_operand:V64SF 2 "register_operand")
                                (match_operand:SI    3 "reg_or_const_int_operand")
                                (match_operand:SI    4 "reg_or_0_operand")
                                (match_operand:SI    5 "const_int_operand")
                                (match_operand:V64SF 6 "register_operand")] wormhole_set_float_op_i_lv))]
  "TARGET_RVTT_WH"
{
  rtx live = operands[2];
  rtx insn = nullptr;
  if (GET_CODE(operands[3]) == CONST_INT)
    insn = gen_rvtt_wh_sfpset<wormhole_set_float_name_i_lv>_i_int
      (operands[0], live, rvtt_clamp_unsigned(operands[3], 0xFFF), operands[6]);
  else
    {
      unsigned op = TT_OP_WH_SFPSET<wormhole_set_float_call_i_lv>(0, 0, 0, 1);
      insn = rvtt_sfpsynth_insn_dst (operands[1], 0, operands[4], op, operands[5],
				     operands[6], 4, operands[0], 8, live);
    }
  emit_insn (insn);
  DONE;
})

(define_int_iterator wormhole_set_float_op_i_int [UNSPECV_WH_SFPSETEXP_I_INT UNSPECV_WH_SFPSETSGN_I_INT])
(define_int_attr wormhole_set_float_name_i_int [(UNSPECV_WH_SFPSETEXP_I_INT "exp") (UNSPECV_WH_SFPSETSGN_I_INT "sgn")])
(define_int_attr wormhole_set_float_call_i_int [(UNSPECV_WH_SFPSETEXP_I_INT "EXP") (UNSPECV_WH_SFPSETSGN_I_INT "SGN")])
(define_insn "rvtt_wh_sfpset<wormhole_set_float_name_i_int>_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr, xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "reg_or_vec0_operand" "xn,0")
                                (match_operand:SI    2 "const_int_operand" "N12U,N12U")
                                (match_operand:V64SF 3 "register_operand"  "xr, xr")] wormhole_set_float_op_i_int))]
  "TARGET_RVTT_WH"
  "SFPSET<wormhole_set_float_call_i_int>\t%0, %3, %2, 1"
)

(define_expand "rvtt_wh_sfpsetman_i"
  [(set (match_operand:V64SF 0 "register_operand")
        (unspec_volatile:V64SF [(match_operand:SI    1 "address_operand")
                                (match_operand:SI    2 "reg_or_const_int_operand")
                                (match_operand:SI    3 "reg_or_0_operand")
                                (match_operand:SI    4 "const_int_operand")
                                (match_operand:V64SF 5 "register_operand")
                                (match_operand:SI    6 "const_int_operand")] UNSPECV_WH_SFPSETMAN_I))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_vec0_rtx;
  rvtt_wh_emit_sfpsetman(operands[0], live, operands[1], operands[2], operands[5]);
  DONE;
})

(define_expand "rvtt_wh_sfpsetman_i_lv"
  [(set (match_operand:V64SF 0 "register_operand")
        (unspec_volatile:V64SF [(match_operand:SI    1 "address_operand")
                                (match_operand:V64SF 2 "register_operand")
                                (match_operand:SI    3 "reg_or_const_int_operand")
                                (match_operand:SI    4 "reg_or_0_operand")
                                (match_operand:SI    5 "const_int_operand")
                                (match_operand:V64SF 6 "register_operand")
                                (match_operand:SI    7 "const_int_operand")] UNSPECV_WH_SFPSETMAN_I_LV))]
  "TARGET_RVTT_WH"
{
  rtx live = operands[2];
  rvtt_wh_emit_sfpsetman(operands[0], live, operands[1], operands[3], operands[6]);
  DONE;
})

(define_insn "rvtt_wh_sfpsetman_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr, xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "reg_or_vec0_operand" "xn,0")
                                (match_operand:SI    2 "const_int_operand" "N12U,N12U")
                                (match_operand:V64SF 3 "register_operand"  "xr, xr")] UNSPECV_WH_SFPSETMAN_I_INT))]
  "TARGET_RVTT_WH"
  "SFPSETMAN\t%0, %3, %2, 1"
)

(define_expand "rvtt_wh_sfpmad"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "register_operand"  "")
                                (match_operand:V64SF 2 "register_operand"  "")
                                (match_operand:V64SF 3 "register_operand"  "")
                                (match_operand:SI    4 "const_int_operand" "")] UNSPECV_WH_SFPMAD))]
  "TARGET_RVTT_WH"
{
  rtx live = rvtt_vec0_rtx;
  emit_insn (gen_rvtt_wh_sfpmad_int(operands[0], live, operands[1], operands[2], operands[3], operands[4]));
  DONE;
})

(define_expand "rvtt_wh_sfpmad_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "register_operand"  "")
                                (match_operand:V64SF 2 "register_operand"  "")
                                (match_operand:V64SF 3 "register_operand"  "")
                                (match_operand:V64SF 4 "register_operand"  "")
                                (match_operand:SI    5 "const_int_operand" "")] UNSPECV_WH_SFPMAD_LV))]
  "TARGET_RVTT_WH"
{
  rtx live = operands[1];
  emit_insn (gen_rvtt_wh_sfpmad_int(operands[0], live, operands[2], operands[3], operands[4], operands[5]));
  DONE;
})

(define_insn "rvtt_wh_sfpmad_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr, xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "reg_or_vec0_operand" "xn,0")
                                (match_operand:V64SF 2 "register_operand"  "xr, xr")
                                (match_operand:V64SF 3 "register_operand"  "xr, xr")
                                (match_operand:V64SF 4 "register_operand"  "xr, xr")
                                (match_operand:SI    5 "const_int_operand" "N04U,N04U")] UNSPECV_WH_SFPMAD_INT))]
  "TARGET_RVTT_WH"
  "SFPMAD\t%0, %2, %3, %4, %5"
)

(define_insn "rvtt_wh_sfpsetcc_i"
  [(unspec_volatile [(match_operand:SI    0 "const_int_operand" "N01U")
                     (match_operand:SI    1 "const_int_operand" "N04U")] UNSPECV_WH_SFPSETCC_I)]
  "TARGET_RVTT_WH"
  "SFPSETCC\tL0, %0, %1"
)

(define_insn "rvtt_wh_sfpsetcc_v"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "xr")
                     (match_operand:SI    1 "const_int_operand" "N04U")] UNSPECV_WH_SFPSETCC_V)]
  "TARGET_RVTT_WH"
  "SFPSETCC\t%0, 0, %1"
)

(define_expand "rvtt_wh_sfpxfcmps"
  [(set (match_operand:SI 0 "register_operand" "")
        (unspec_volatile:V64SF [(match_operand:SI    1 "address_operand"   "")
                                (match_operand:V64SF 2 "register_operand"  "")
                                (match_operand:SI    3 "reg_or_const_int_operand" "")
                                (match_operand:SI    4 "reg_or_const_int_operand" "")
                                (match_operand:SI    5 "const_int_operand" "")
                                (match_operand:SI    6 "const_int_operand" "")] UNSPECV_WH_SFPXFCMPS))]
  "TARGET_RVTT_WH"
{
  rvtt_wh_emit_sfpxfcmps(operands[1], operands[2], operands[3], operands[6]);
  DONE;
})

(define_expand "rvtt_wh_sfpxfcmpv"
  [(set (match_operand:SI 0 "register_operand" "")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "register_operand"  "")
                                (match_operand:V64SF 2 "register_operand"  "")
                                (match_operand:SI    3 "const_int_operand" "")] UNSPECV_WH_SFPXFCMPV))]
  "TARGET_RVTT_WH"
{
  rvtt_wh_emit_sfpxfcmpv(operands[1], operands[2], operands[3]);
  DONE;
})

(define_insn "rvtt_wh_sfpencc"
  [(unspec_volatile [(match_operand:SI 0 "const_int_operand" "N02U")
                     (match_operand:SI 1 "const_int_operand" "N04U")] UNSPECV_WH_SFPENCC)]
  "TARGET_RVTT_WH"
  "SFPENCC\t%0, %1"
)

(define_insn "rvtt_wh_sfpcompc"
  [(unspec_volatile [(const_int 0)] UNSPECV_WH_SFPCOMPC)]
  "TARGET_RVTT_WH"
  "SFPCOMPC"
)

(define_insn "rvtt_wh_sfppushc"
  [(unspec_volatile [(match_operand:SI 0 "const_int_operand" "N04U")] UNSPECV_WH_SFPPUSHC)]
  "TARGET_RVTT_WH"
  "SFPPUSHC\t%0")

(define_insn "rvtt_wh_sfppopc"
  [(unspec_volatile [(match_operand:SI 0 "const_int_operand" "N04U")] UNSPECV_WH_SFPPOPC)]
  "TARGET_RVTT_WH"
  "SFPPOPC\t%0"
)

(define_insn "rvtt_wh_sfplut"
  [(set (match_operand:V64SF 0 "register_operand" "=x3")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "register_operand"  "x0")
                                (match_operand:V64SF 2 "register_operand"  "x1")
                                (match_operand:V64SF 3 "register_operand"  "x2")
                                (match_operand:V64SF 4 "register_operand"  "0")
                                (match_operand:SI    5 "const_int_operand" "N04U")] UNSPECV_WH_SFPLUT))]
  "TARGET_RVTT_WH"
  "SFPLUT\t%0, %5"
)

(define_insn "rvtt_wh_sfplutfp32_3r"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "register_operand"  "x0")
                                (match_operand:V64SF 2 "register_operand"  "x1")
                                (match_operand:V64SF 3 "register_operand"  "x2")
                                (match_operand:V64SF 4 "register_operand"  "x3")
                                (match_operand:SI    5 "const_int_operand" "N04U")] UNSPECV_WH_SFPLUTFP32_3R))
        (clobber (match_scratch:V64SF 6 "=x7"))
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
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile:V64SF [(match_operand:V64SF 1 "register_operand"  "x0")
                                (match_operand:V64SF 2 "register_operand"  "x1")
                                (match_operand:V64SF 3 "register_operand"  "x2")
                                (match_operand:V64SF 4 "register_operand"  "x4")
                                (match_operand:V64SF 5 "register_operand"  "x5")
                                (match_operand:V64SF 6 "register_operand"  "x6")
                                (match_operand:V64SF 7 "register_operand"  "x3")
                                (match_operand:SI    8 "const_int_operand" "N04U")] UNSPECV_WH_SFPLUTFP32_6R))]
  "TARGET_RVTT_WH"
  "SFPLUTFP32\t%0, %8")

(define_insn "rvtt_wh_sfpconfig_v"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"   "x0")
                     (match_operand:SI    1 "const_int_operand"  "N04U")] UNSPECV_WH_SFPCONFIG_V)]
  "TARGET_RVTT_WH"
  "SFPCONFIG\t%1, 0, 0")

(define_expand "rvtt_wh_sfpswap"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"   "")
                     (match_operand:V64SF 1 "register_operand"   "")
                     (match_operand:SI    2 "const_int_operand"  "")] UNSPECV_WH_SFPSWAP)]
  "TARGET_RVTT_WH"
{
  emit_insn (gen_rvtt_wh_sfpswap_int(operands[0], operands[1], operands[2]));
  DONE;
})

(define_insn "rvtt_wh_sfpswap_int"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"   "+xr")
                     (match_operand:V64SF 1 "register_operand"   "+xr")
                     (match_operand:SI    2 "const_int_operand"  "N04U")] UNSPECV_WH_SFPSWAP_INT)]
  "TARGET_RVTT_WH"
  "SFPSWAP\t%0, %1, %2")

(define_insn "rvtt_wh_sfptransp"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"   "+x0")
                     (match_operand:V64SF 1 "register_operand"   "+x1")
                     (match_operand:V64SF 2 "register_operand"   "+x2")
                     (match_operand:V64SF 3 "register_operand"   "+x3")] UNSPECV_WH_SFPTRANSP)]
  "TARGET_RVTT_WH"
  "SFPTRANSP")

(define_insn "rvtt_wh_sfpshft2_g"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"   "+x0")
                     (match_operand:V64SF 1 "register_operand"   "+x1")
                     (match_operand:V64SF 2 "register_operand"   "+x2")
                     (match_operand:V64SF 3 "register_operand"   "+x3")
                     (match_operand:SI    4 "const_int_operand"  "N04U")] UNSPECV_WH_SFPSHFT2_G)]
  "TARGET_RVTT_WH"
  "SFPSHFT2\t0, L0, L0, %0, %1, %2, %3, %4")

(define_insn "rvtt_wh_sfpshft2_ge"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"   "xr")
                     (match_operand:V64SF 1 "register_operand"   "+x0")
                     (match_operand:V64SF 2 "register_operand"   "+x1")
                     (match_operand:V64SF 3 "register_operand"   "+x2")
                     (match_operand:V64SF 4 "register_operand"   "+x3")] UNSPECV_WH_SFPSHFT2_GE)]
  "TARGET_RVTT_WH"
  "SFPSHFT2\t0, %0, L0, %1, %2, %3, %4, 2")

(include "tt/rvtt-peephole-wh.md")
