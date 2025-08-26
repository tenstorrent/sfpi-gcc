;; Machine description for Tenstorrent SFPU Blackhole Intrinsics.
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
  UNSPECV_BH_SFPASSIGN
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
  UNSPECV_BH_SFPAND
  UNSPECV_BH_SFPOR
  UNSPECV_BH_SFPXOR
  UNSPECV_BH_SFPNOT
  UNSPECV_BH_SFPLZ
  UNSPECV_BH_SFPLZ_LV
  UNSPECV_BH_SFPLZ_INT
  UNSPECV_BH_SFPSETMAN
  UNSPECV_BH_SFPSETEXP
  UNSPECV_BH_SFPSETSGN
  UNSPECV_BH_SFPMAD
  UNSPECV_BH_SFPMAD_LV
  UNSPECV_BH_SFPMAD_INT
  UNSPECV_BH_SFPMOV
  UNSPECV_BH_SFPDIVP2
  UNSPECV_BH_SFPEXEXP
  UNSPECV_BH_SFPEXMAN
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
  UNSPECV_BH_SFPSWAP
  UNSPECV_BH_SFPTRANSP
  UNSPECV_BH_SFPSHFT2_G
  UNSPECV_BH_SFPSHFT2_GE
  UNSPECV_BH_SFPMUL24
  UNSPECV_BH_SFPARECIP
  UNSPECV_BH_SFPGT
  UNSPECV_BH_SFPLE
  UNSPECV_BH_SFPMOV_CONFIG
])

(define_insn "rvtt_bh_sfpgccmov_cc"
  [(set (match_operand:V64SF 0 "nonimmediate_operand" "=xr,xr,m")
        (match_operand:V64SF 1 "nonimmediate_operand" " xr,m,xr"))]
  "TARGET_RVTT_BH  &&
   (   register_operand (operands[0], V64SFmode)
    || register_operand (operands[1], V64SFmode))"
  {
    if (which_alternative == 0)
      return "SFPMOV\t%0, %1, 2";

    rvtt_mov_error (insn, which_alternative == 1);
    gcc_unreachable ();
  })

(define_insn "rvtt_bh_sfpassign_lv"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "xr")] UNSPECV_BH_SFPASSIGN))]
  "TARGET_RVTT_BH"
  "SFPMOV\t%0, %2, 0"
)

(define_expand "rvtt_bh_sfpload"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI 1 "address_operand"  "")
                          (match_operand:SI 2 "const_int_operand" "")
                          (match_operand:SI 3 "const_int_operand" "")
                          (match_operand:SI 4 "reg_or_const_int_operand" "")
                          (match_operand:SI 5 "reg_or_0_operand" "")
                          (match_operand:SI 6 "const_int_operand" "")] UNSPECV_BH_SFPLOAD))]
  "TARGET_RVTT_BH"
{
  rvtt_bh_emit_sfpload (operands[0], rvtt_vec0_rtx,
  		        operands[1], operands[2], operands[3], operands[4], operands[5], operands[6]);
  DONE;
})

(define_expand "rvtt_bh_sfpload_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "const_int_operand" "")
                          (match_operand:SI    4 "const_int_operand" "")
                          (match_operand:SI    5 "reg_or_const_int_operand" "")
                          (match_operand:SI    6 "reg_or_0_operand" "")
                          (match_operand:SI    7 "const_int_operand" "")] UNSPECV_BH_SFPLOAD))]
  "TARGET_RVTT_BH"
{
  rvtt_bh_emit_sfpload (operands[0], operands[2],
  		        operands[1], operands[3], operands[4], operands[5], operands[6], operands[7]);
  DONE;
})

(define_insn "rvtt_bh_sfpload_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "reg_or_vec0_operand" "0xn")
                          (match_operand:SI    2 "const_int_operand" "N04U")
                          (match_operand:SI    3 "const_int_operand" "N03U")
                          (match_operand:SI    4 "const_int_operand" "N13U")] UNSPECV_BH_SFPLOAD))]
  "TARGET_RVTT_BH"
  "SFPLOAD\t%0, %4, %2, %3")


;;; SFPLOADI and SFPLOADI_LV
(define_expand "rvtt_bh_sfpxloadi"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec [(match_operand:SI 1 "address_operand"  "")
                          (match_operand:SI 2 "const_int_operand" "")
                          (match_operand:SI 3 "reg_or_const_int_operand" "")
                          (match_operand:SI 4 "reg_or_0_operand"  "")
                          (match_operand:SI 5 "const_int_operand" "")] UNSPECV_BH_SFPXLOADI))]
  "TARGET_RVTT_BH"
{
  rvtt_bh_emit_sfpxloadi (operands[0], rvtt_vec0_rtx,
  			  operands[1], operands[2], operands[3], operands[4], operands[5]);
  DONE;
})

(define_expand "rvtt_bh_sfpxloadi_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec [(match_operand:SI    1 "address_operand"   "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "const_int_operand" "")
                          (match_operand:SI    4 "reg_or_const_int_operand" "")
                          (match_operand:SI    5 "reg_or_0_operand"  "")
                          (match_operand:SI    6 "const_int_operand" "")] UNSPECV_BH_SFPXLOADI))]
  "TARGET_RVTT_BH"
{
  rvtt_bh_emit_sfpxloadi (operands[0], operands[2],
  		          operands[1], operands[3], operands[4], operands[5], operands[6]);
  DONE;
})

(define_insn "rvtt_bh_sfploadi_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr,xr")
        (unspec_volatile [(match_operand:V64SF 1 "reg_or_vec0_operand" "0xn,0xn")
                          (match_operand:SI    2 "const_int_operand" "N04U,N04U")
                          (match_operand:SI    3 "const_int_operand" "N16S,N16U")] UNSPECV_BH_SFPXLOADI))]
  "TARGET_RVTT_BH"
  "@
  SFPLOADI\t%0, %s3, %2
  SFPLOADI\t%0, %u3, %2")

(define_expand "rvtt_bh_sfpstore"
  [(unspec_volatile [(match_operand:SI    0 "address_operand"   "")
                     (match_operand:V64SF 1 "register_operand"  "")
                     (match_operand:SI    2 "const_int_operand" "")
                     (match_operand:SI    3 "const_int_operand" "")
                     (match_operand:SI    4 "reg_or_const_int_operand" "")
                     (match_operand:SI    5 "reg_or_0_operand" "")
                     (match_operand:SI    6 "const_int_operand" "")] UNSPECV_BH_SFPSTORE)]
  "TARGET_RVTT_BH"
{
  rtx insn = nullptr;
  if (GET_CODE(operands[4]) == CONST_INT)
    insn = gen_rvtt_bh_sfpstore_int (operands[1], operands[2], operands[3],
    	                             rvtt_clamp_unsigned (operands[4], 0x1FFF));
  else
    {
      unsigned op = TT_OP_BH_SFPSTORE (0, INTVAL (operands[2]), INTVAL (operands[3]), 0);
      insn = rvtt_sfpsynth_insn (operands[0], 0, operands[5], op, operands[6], operands[1], 20);
    }
  emit_insn (insn);
  DONE;
})

(define_insn "rvtt_bh_sfpstore_int"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "xr")
                     (match_operand:SI    1 "const_int_operand" "N04U")
                     (match_operand:SI    2 "const_int_operand" "N03U")
                     (match_operand:SI    3 "const_int_operand" "N13U")] UNSPECV_BH_SFPSTORE)]
  "TARGET_RVTT_BH"
  "SFPSTORE\t%3, %0, %1, %2")


(define_int_iterator blackhole_muliaddi_op [UNSPECV_BH_SFPMULI UNSPECV_BH_SFPADDI])
(define_int_attr blackhole_muliaddi_name [(UNSPECV_BH_SFPMULI "muli") (UNSPECV_BH_SFPADDI "addi")])
(define_int_attr blackhole_muliaddi_insn [(UNSPECV_BH_SFPMULI "MULI") (UNSPECV_BH_SFPADDI "ADDI")])
(define_expand "rvtt_bh_sfp<blackhole_muliaddi_name>"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "reg_or_const_int_operand" "")
                          (match_operand:SI    4 "reg_or_0_operand"  "")
                          (match_operand:SI    5 "const_int_operand" "")
                          (match_operand:SI    6 "const_int_operand" "")] blackhole_muliaddi_op))]
  "TARGET_RVTT_BH"
{
  rtx insn = nullptr;
  if (GET_CODE (operands[3]) == CONST_INT)
    insn = gen_rvtt_bh_sfp<blackhole_muliaddi_name>_int (operands[0], operands[2],
               rvtt_clamp_unsigned (operands[3], 0xFFFF), operands[6]);
  else
    {
      unsigned op = TT_OP_BH_SFP<blackhole_muliaddi_insn> (0, 0, INTVAL (operands[6]));
      insn = rvtt_sfpsynth_insn_dst (operands[1], INSN_SCHED_DYN, operands[4], op, operands[5],
				     operands[0], 4, operands[2]);
  }
  emit_insn (insn);
  DONE;
})

(define_insn "rvtt_bh_sfp<blackhole_muliaddi_name>_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:SI    2 "const_int_operand" "N16U")
                          (match_operand:SI    3 "const_int_operand" "N04U")] blackhole_muliaddi_op))]
  "TARGET_RVTT_BH"
  "SFP<blackhole_muliaddi_insn>\t%0, %2, %3"
)

(define_expand "rvtt_bh_sfpdivp2"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:SI    2 "reg_or_const_int_operand" "")
                          (match_operand:SI    3 "reg_or_0_operand"  "")
                          (match_operand:SI    4 "const_int_operand" "")
                          (match_operand:V64SF 5 "register_operand"  "")
                          (match_operand:SI    6 "const_int_operand" "")] UNSPECV_BH_SFPDIVP2))]
  "TARGET_RVTT_BH"
{
  rvtt_bh_emit_sfpdivp2 (operands[0], rvtt_vec0_rtx,
  		         operands[1], operands[2], operands[5], operands[6], operands[3], operands[4]);
  DONE;
})

(define_expand "rvtt_bh_sfpdivp2_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "reg_or_const_int_operand" "")
                          (match_operand:SI    4 "reg_or_0_operand"  "")
                          (match_operand:SI    5 "const_int_operand" "")
                          (match_operand:V64SF 6 "register_operand"  "")
                          (match_operand:SI    7 "const_int_operand" "")] UNSPECV_BH_SFPDIVP2))]
  "TARGET_RVTT_BH"
{
  rvtt_bh_emit_sfpdivp2 (operands[0], operands[2],
  		         operands[1], operands[3], operands[6], operands[7], operands[4], operands[5]);
  DONE;
})

(define_insn "rvtt_bh_sfpdivp2_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "reg_or_vec0_operand" "0xn")
                          (match_operand:SI    2 "const_int_operand" "N12S")
                          (match_operand:V64SF 3 "register_operand"  "xr")
                          (match_operand:SI    4 "const_int_operand" "N04U")] UNSPECV_BH_SFPDIVP2))]
  "TARGET_RVTT_BH"
  "SFPDIVP2\t%0, %3, %2, %4"
)

(define_int_iterator blackhole_src_mod_op
  [UNSPECV_BH_SFPEXEXP
   UNSPECV_BH_SFPEXMAN
   UNSPECV_BH_SFPABS
   UNSPECV_BH_SFPMOV
   UNSPECV_BH_SFPLZ])
(define_int_attr blackhole_src_mod_name
  [(UNSPECV_BH_SFPEXEXP "exexp")
   (UNSPECV_BH_SFPEXMAN "exman")
   (UNSPECV_BH_SFPABS "abs")
   (UNSPECV_BH_SFPMOV "mov")
   (UNSPECV_BH_SFPLZ "lz")])
(define_int_attr blackhole_src_mod_insn
  [(UNSPECV_BH_SFPEXEXP "EXEXP")
   (UNSPECV_BH_SFPEXMAN "EXMAN")
   (UNSPECV_BH_SFPABS "ABS")
   (UNSPECV_BH_SFPMOV "MOV")
   (UNSPECV_BH_SFPLZ "LZ")])

(define_expand "rvtt_bh_sfp<blackhole_src_mod_name>"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "xr")
                          (match_operand:SI    2 "const_int_operand" "N04U")] blackhole_src_mod_op))]
  "TARGET_RVTT_BH"
  {
    emit_insn (gen_rvtt_bh_sfp<blackhole_src_mod_name>_lv
                 (operands[0], rvtt_vec0_rtx, operands[1], operands[2]));
    DONE;
  })

(define_insn "rvtt_bh_sfp<blackhole_src_mod_name>_lv"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "reg_or_vec0_operand"  "0xn")
                          (match_operand:V64SF 2 "register_operand"  "xr")
                          (match_operand:SI    3 "const_int_operand" "N04U")] blackhole_src_mod_op))]
  "TARGET_RVTT_BH"
  "SFP<blackhole_src_mod_insn>\t%0, %2, %3"
)

(define_expand "rvtt_bh_sfpmov_config"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:SI 1 "const_int_operand" "N04U")] UNSPECV_BH_SFPMOV_CONFIG))]
  "TARGET_RVTT_BH"
  {
    emit_insn (gen_rvtt_bh_sfpmov_config_lv
                 (operands[0], rvtt_vec0_rtx, operands[1]));
    DONE;
  })

(define_insn "rvtt_bh_sfpmov_config_lv"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "reg_or_vec0_operand" "0xn")
			  (match_operand:SI 2 "const_int_operand" "N04U")] UNSPECV_BH_SFPMOV_CONFIG))]
  "TARGET_RVTT_BH"
  "SFPMOV\t%0, L%2, 8"
)

(define_int_iterator blackhole_muladd_op [UNSPECV_BH_SFPMUL UNSPECV_BH_SFPADD])
(define_int_attr blackhole_muladd_name [(UNSPECV_BH_SFPMUL "mul") (UNSPECV_BH_SFPADD "add")])
(define_int_attr blackhole_muladd_insn [(UNSPECV_BH_SFPMUL "MUL") (UNSPECV_BH_SFPADD "ADD")])
(define_int_attr blackhole_muladd_ops [(UNSPECV_BH_SFPMUL "%1, %2, L9") (UNSPECV_BH_SFPADD "L10, %1, %2")])
(define_int_attr blackhole_muladd_ops_lv [(UNSPECV_BH_SFPMUL "%2, %3, L9") (UNSPECV_BH_SFPADD "L10, %2, %3")])
(define_insn "rvtt_bh_sfp<blackhole_muladd_name>"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "xr")
                          (match_operand:V64SF 2 "register_operand"  "xr")
                          (match_operand:SI    3 "const_int_operand" "N04U")] blackhole_muladd_op))]
  "TARGET_RVTT_BH"
  "SFP<blackhole_muladd_insn>\t%0, <blackhole_muladd_ops>, %3"
)

(define_insn "rvtt_bh_sfp<blackhole_muladd_name>_lv"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "reg_or_vec0_operand" "0")
                          (match_operand:V64SF 2 "register_operand"  "xr")
                          (match_operand:V64SF 3 "register_operand"  "xr")
                          (match_operand:SI    4 "const_int_operand" "N04U")] blackhole_muladd_op))]
  "TARGET_RVTT_BH"
  "SFP<blackhole_muladd_insn>\t%0, <blackhole_muladd_ops_lv>, %4"
)

(define_insn "rvtt_bh_sfpiadd_v_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "xr")
                          (match_operand:SI    3 "const_int_operand" "N04U")] UNSPECV_BH_SFPIADD_V_INT))]
  "TARGET_RVTT_BH"
  "SFPIADD\t%0, %2, 0, %3"
)

(define_insn "rvtt_bh_sfpiadd_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "reg_or_vec0_operand" "0xn")
                          (match_operand:V64SF 2 "register_operand"  "xr")
                          (match_operand:SI    3 "const_int_operand" "n")
                          (match_operand:SI    4 "const_int_operand" "N04U")] UNSPECV_BH_SFPIADD_I_INT))]
  "TARGET_RVTT_BH"
  "SFPIADD\t%0, %2, %3, %4"
)

(define_expand "rvtt_bh_sfpxiadd_v"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "xr")
                          (match_operand:SI    3 "const_int_operand" "N04U")] UNSPECV_BH_SFPXIADD_V))]
  "TARGET_RVTT_BH"
{
  rvtt_bh_emit_sfpxiadd_v(operands[0], operands[1], operands[2], operands[3]);
  DONE;
})

(define_expand "rvtt_bh_sfpxiadd_i"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "reg_or_const_int_operand" "")
                          (match_operand:SI    4 "const_int_operand" "")
                          (match_operand:SI    5 "reg_or_const_int_operand" "")
                          (match_operand:SI    6 "const_int_operand" "")] UNSPECV_BH_SFPXIADD_I))]
  "TARGET_RVTT_BH"
{
  rtx live = rvtt_vec0_rtx;
  rvtt_bh_emit_sfpxiadd_i(operands[0], live, operands[1], operands[2], operands[3], operands[6]);
  DONE;
})

(define_expand "rvtt_bh_sfpxiadd_i_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"   "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:SI    4 "reg_or_const_int_operand" "")
                          (match_operand:SI    5 "const_int_operand" "")
                          (match_operand:SI    6 "reg_or_const_int_operand" "")
                          (match_operand:SI    7 "const_int_operand" "")] UNSPECV_BH_SFPXIADD_I_LV))]
  "TARGET_RVTT_BH"
{
  rtx live = operands[2];
  rvtt_bh_emit_sfpxiadd_i(operands[0], live, operands[1], operands[3], operands[4], operands[7]);
  DONE;
})

(define_insn "rvtt_bh_sfpshft_v"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "xr")
                          (match_operand:SI 3 "const_int_operand"  "N04U")] UNSPECV_BH_SFPSHFT))]
  "TARGET_RVTT_BH"
  "SFPSHFT\t%0, %2, 0, %3"
)

(define_expand "rvtt_bh_sfpshft_i"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "reg_or_const_int_operand" "")
                          (match_operand:SI    4 "reg_or_0_operand" "")
                          (match_operand:SI    5 "const_int_operand" "")
                          (match_operand:SI    6 "const_int_operand" "")] UNSPECV_BH_SFPSHFT))]
  "TARGET_RVTT_BH"
{
  rtx insn;
  if (GET_CODE (operands[3]) == CONST_INT)
    insn = gen_rvtt_bh_sfpshft_i_int (operands[0], operands[2], rvtt_clamp_signed(operands[3], 0x7FF), operands[6]);
  else {
    unsigned op = TT_OP_BH_SFPSHFT(0, 0, 0, INTVAL (operands[6]) | 5);
    insn = rvtt_sfpsynth_insn_dst (operands[1], 0, operands[4], op, operands[5],
    	   			   operands[2], 8, operands[0], 4, nullptr);
  }
  emit_insn (insn);
  DONE;
})

(define_insn "rvtt_bh_sfpshft_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "xr")
                          (match_operand:SI    2 "const_int_operand" "N12S")
                          (match_operand:SI    3 "const_int_operand" "N04U")] UNSPECV_BH_SFPSHFT))]
  "TARGET_RVTT_BH"
  "SFPSHFT\t%0, %1, %2, %3 | 5"
)

(define_insn "rvtt_bh_sfpand"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "xr")] UNSPECV_BH_SFPAND))]
  "TARGET_RVTT_BH"
  "SFPAND\t%0, %2"
)

(define_insn "rvtt_bh_sfpor"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "xr")] UNSPECV_BH_SFPOR))]
  "TARGET_RVTT_BH"
  "SFPOR\t%0, %2"
)

(define_insn "rvtt_bh_sfpxor"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "xr")] UNSPECV_BH_SFPXOR))]
  "TARGET_RVTT_BH"
  "SFPXOR\t%0, %2"
)

(define_expand "rvtt_bh_sfpnot"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "xr")] UNSPECV_BH_SFPNOT))]
  "TARGET_RVTT_BH"
  {
    emit_insn (gen_rvtt_bh_sfpnot_lv (operands[0], rvtt_vec0_rtx, operands[1]));
    DONE;
  })

(define_insn "rvtt_bh_sfpnot_lv"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "reg_or_vec0_operand"  "0xn")
                          (match_operand:V64SF 2 "register_operand"  "xr")] UNSPECV_BH_SFPNOT))]
  "TARGET_RVTT_BH"
  "SFPNOT\t%0, %2"
)

(define_expand "rvtt_bh_sfpcast"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "xr")
                          (match_operand:SI    2 "const_int_operand" "N04U")] UNSPECV_BH_SFPCAST))]
  "TARGET_RVTT_BH"
  {
    emit_insn (gen_rvtt_bh_sfpcast_lv (operands[0], rvtt_vec0_rtx, operands[1], operands[2]));
    DONE;
  })

(define_insn "rvtt_bh_sfpcast_lv"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "reg_or_vec0_operand"  "0xn")
                          (match_operand:V64SF 2 "register_operand"  "xr")
                          (match_operand:SI    3 "const_int_operand" "N04U")] UNSPECV_BH_SFPCAST))]
  "TARGET_RVTT_BH"
  "SFPCAST\t%0, %2, %3"
)

(define_expand "rvtt_bh_sfpshft2_e"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:SI    2 "const_int_operand" "")] UNSPECV_BH_SFPSHFT2_E))]
  "TARGET_RVTT_BH"
{
  rvtt_bh_emit_sfpshft2_e(operands[0], rvtt_vec0_rtx, operands[1], operands[2]);
  DONE;
})

(define_expand "rvtt_bh_sfpshft2_e_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "const_int_operand" "")] UNSPECV_BH_SFPSHFT2_E_LV))]
  "TARGET_RVTT_BH"
{
  rvtt_bh_emit_sfpshft2_e(operands[0], operands[1], operands[2], operands[3]);
  DONE;
})

(define_insn "rvtt_bh_sfpshft2_e_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "reg_or_vec0_operand" "0xn")
                          (match_operand:V64SF 2 "register_operand"  "xr")
                          (match_operand:SI    3 "const_int_operand" "N04U")] UNSPECV_BH_SFPSHFT2_E_INT))]
  "TARGET_RVTT_BH"
  "SFPSHFT2\t%0, %2, 0, %3")

(define_expand "rvtt_bh_sfpstochrnd_i"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:SI    2 "const_int_operand" "")
                          (match_operand:SI    3 "reg_or_const_int_operand" "")
                          (match_operand:SI    4 "reg_or_0_operand"  "")
                          (match_operand:SI    5 "const_int_operand" "")
                          (match_operand:V64SF 6 "register_operand"  "")
                          (match_operand:SI    7 "const_int_operand" "")] UNSPECV_BH_SFPSTOCHRND_I))]
  "TARGET_RVTT_BH"
{
  rtx live = rvtt_vec0_rtx;
  rvtt_bh_emit_sfpstochrnd_i(operands[0], live, operands[1], operands[2], operands[3],
                                   operands[6], operands[7], operands[4], operands[5]);
  DONE;
})

(define_expand "rvtt_bh_sfpstochrnd_i_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "const_int_operand" "")
                          (match_operand:SI    4 "reg_or_const_int_operand" "")
                          (match_operand:SI    5 "reg_or_0_operand"  "")
                          (match_operand:SI    6 "const_int_operand" "")
                          (match_operand:V64SF 7 "register_operand"  "")
                          (match_operand:SI    8 "const_int_operand" "")] UNSPECV_BH_SFPSTOCHRND_I_LV))]
  "TARGET_RVTT_BH"
{
  rtx live = operands[2];
  rvtt_bh_emit_sfpstochrnd_i(operands[0], live, operands[1], operands[3], operands[4],
                                   operands[7], operands[8], operands[5], operands[6]);
  DONE;
})

(define_insn "rvtt_bh_sfpstochrnd_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "reg_or_vec0_operand" "0xn")
                          (match_operand:SI    2 "const_int_operand" "N01U")
                          (match_operand:SI    3 "const_int_operand" "N05U")
                          (match_operand:V64SF 4 "register_operand"  "xr")
                          (match_operand:SI    5 "const_int_operand" "N04U")] UNSPECV_BH_SFPSTOCHRND_I_INT))]
  "TARGET_RVTT_BH"
  "SFPSTOCHRND\t%0, L0, %4, %5, %2, %3");

(define_expand "rvtt_bh_sfpstochrnd_v"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "const_int_operand" "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:SI    4 "const_int_operand" "")] UNSPECV_BH_SFPSTOCHRND_V))]
  "TARGET_RVTT_BH"
{
  rtx live = rvtt_vec0_rtx;
  emit_insn (gen_rvtt_bh_sfpstochrnd_v_int(operands[0], live, operands[1], operands[2], operands[3], operands[4]));
  DONE;
})

(define_expand "rvtt_bh_sfpstochrnd_v_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:SI    2 "const_int_operand" "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:V64SF 4 "register_operand"  "")
                          (match_operand:SI    5 "const_int_operand" "")] UNSPECV_BH_SFPSTOCHRND_V_LV))]
  "TARGET_RVTT_BH"
{
  rtx live = operands[1];
  emit_insn (gen_rvtt_bh_sfpstochrnd_v_int(operands[0], live, operands[2], operands[3], operands[4], operands[5]));
  DONE;
})

(define_insn "rvtt_bh_sfpstochrnd_v_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "reg_or_vec0_operand" "0xn")
                          (match_operand:SI    2 "const_int_operand" "N01U")
                          (match_operand:V64SF 3 "register_operand"  "xr")
                          (match_operand:V64SF 4 "register_operand"  "xr")
                          (match_operand:SI    5 "const_int_operand" "N04U")] UNSPECV_BH_SFPSTOCHRND_V_INT))]
  "TARGET_RVTT_BH"
  "SFPSTOCHRND\t%0, %3, %4, %5, %2, 0")

(define_int_iterator blackhole_set_float_op_v [UNSPECV_BH_SFPSETEXP UNSPECV_BH_SFPSETMAN UNSPECV_BH_SFPSETSGN])
(define_int_iterator blackhole_set_float_op_i [UNSPECV_BH_SFPSETEXP UNSPECV_BH_SFPSETSGN])
(define_int_attr blackhole_set_float_name [(UNSPECV_BH_SFPSETEXP "exp") (UNSPECV_BH_SFPSETMAN "man") (UNSPECV_BH_SFPSETSGN "sgn")])
(define_int_attr blackhole_set_float_insn [(UNSPECV_BH_SFPSETEXP "EXP") (UNSPECV_BH_SFPSETMAN "MAN") (UNSPECV_BH_SFPSETSGN "SGN")])

(define_insn "rvtt_bh_sfpset<blackhole_set_float_name>_v"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "xr")] blackhole_set_float_op_v))]
  "TARGET_RVTT_BH"
  "SFPSET<blackhole_set_float_insn>\t%0, %2, 0, 0"
)

(define_expand "rvtt_bh_sfpset<blackhole_set_float_name>_i"
  [(set (match_operand:V64SF 0 "register_operand")
        (unspec_volatile [(match_operand:SI    1 "address_operand")
                          (match_operand:SI    2 "reg_or_const_int_operand")
                          (match_operand:SI    3 "reg_or_0_operand")
                          (match_operand:SI    4 "const_int_operand")
                          (match_operand:V64SF 5 "register_operand")] blackhole_set_float_op_i))]
  "TARGET_RVTT_BH"
{
  rtx insn;
  if (GET_CODE (operands[2]) == CONST_INT)
    insn = gen_rvtt_bh_sfpset<blackhole_set_float_name>_i_int
      (operands[0], rvtt_clamp_unsigned (operands[2], 0xFFF), operands[5]);
  else
    {
      unsigned op = TT_OP_BH_SFPSET<blackhole_set_float_insn> (0, 0, 0, 1);
      insn = rvtt_sfpsynth_insn_dst (operands[1], 0, operands[3], op, operands[4],
				     operands[5], 4, operands[0], 8, nullptr);
    }
  emit_insn (insn);
  DONE;
})

(define_expand "rvtt_bh_sfpset<blackhole_set_float_name>_i_lv"
  [(set (match_operand:V64SF 0 "register_operand")
        (unspec_volatile [(match_operand:SI    1 "address_operand")
                          (match_operand:V64SF 2 "register_operand")
                          (match_operand:SI    3 "reg_or_const_int_operand")
                          (match_operand:SI    4 "reg_or_0_operand")
                          (match_operand:SI    5 "const_int_operand")
                          (match_operand:V64SF 6 "register_operand")] blackhole_set_float_op_i))]
  "TARGET_RVTT_BH"
{
  rtx insn;
  if (GET_CODE(operands[3]) == CONST_INT)
    insn = gen_rvtt_bh_sfpset<blackhole_set_float_name>_i_lv_int(operands[0], operands[2],
                                   rvtt_clamp_unsigned (operands[3], 0xFFF), operands[6]);
  else
    {
      unsigned op = TT_OP_BH_SFPSET<blackhole_set_float_insn> (0, 0, 0, 1);
      insn = rvtt_sfpsynth_insn_dst (operands[1], 0, operands[4], op, operands[5],
				     operands[6], 4, operands[0], 8, operands[2]);
    }
   emit_insn (insn);
  DONE;
})

(define_insn "rvtt_bh_sfpset<blackhole_set_float_name>_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:SI    1 "const_int_operand" "N12U")
                          (match_operand:V64SF 2 "register_operand"  "xr")] blackhole_set_float_op_i))]
  "TARGET_RVTT_BH"
  "SFPSET<blackhole_set_float_insn>\t%0, %2, %1, 1"
)

(define_insn "rvtt_bh_sfpset<blackhole_set_float_name>_i_lv_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand" "0")
                          (match_operand:SI    2 "const_int_operand" "N12U")
                          (match_operand:V64SF 3 "register_operand"  "xr")] blackhole_set_float_op_i))]
  "TARGET_RVTT_BH"
  "SFPSET<blackhole_set_float_insn>\t%0, %3, %2, 1"
)

(define_expand "rvtt_bh_sfpsetman_i"
  [(set (match_operand:V64SF 0 "register_operand")
        (unspec_volatile [(match_operand:SI    1 "address_operand")
                          (match_operand:SI    2 "reg_or_const_int_operand")
                          (match_operand:SI    3 "reg_or_0_operand")
                          (match_operand:SI    4 "const_int_operand")
                          (match_operand:V64SF 5 "register_operand")
                          (match_operand:SI    6 "const_int_operand")] UNSPECV_BH_SFPSETMAN))]
  "TARGET_RVTT_BH"
{
  rtx live = rvtt_vec0_rtx;
  rvtt_bh_emit_sfpsetman(operands[0], live, operands[1], operands[2], operands[5]);
  DONE;
})

(define_expand "rvtt_bh_sfpsetman_i_lv"
  [(set (match_operand:V64SF 0 "register_operand")
        (unspec_volatile [(match_operand:SI    1 "address_operand")
                          (match_operand:V64SF 2 "register_operand")
                          (match_operand:SI    3 "reg_or_const_int_operand")
                          (match_operand:SI    4 "reg_or_0_operand")
                          (match_operand:SI    5 "const_int_operand")
                          (match_operand:V64SF 6 "register_operand")
                          (match_operand:SI    7 "const_int_operand")] UNSPECV_BH_SFPSETMAN))]
  "TARGET_RVTT_BH"
{
  rtx live = operands[2];
  rvtt_bh_emit_sfpsetman(operands[0], live, operands[1], operands[3], operands[6]);
  DONE;
})

(define_insn "rvtt_bh_sfpsetman_i_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "reg_or_vec0_operand" "0xn")
                          (match_operand:SI    2 "const_int_operand" "N12U")
                          (match_operand:V64SF 3 "register_operand"  "xr")] UNSPECV_BH_SFPSETMAN))]
  "TARGET_RVTT_BH"
  "SFPSETMAN\t%0, %3, %2, 1"
)

(define_expand "rvtt_bh_sfpmad"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:SI    4 "const_int_operand" "")] UNSPECV_BH_SFPMAD))]
  "TARGET_RVTT_BH"
{
  rtx live = rvtt_vec0_rtx;
  emit_insn (gen_rvtt_bh_sfpmad_int(operands[0], live, operands[1], operands[2], operands[3], operands[4]));
  DONE;
})

(define_expand "rvtt_bh_sfpmad_lv"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:V64SF 3 "register_operand"  "")
                          (match_operand:V64SF 4 "register_operand"  "")
                          (match_operand:SI    5 "const_int_operand" "")] UNSPECV_BH_SFPMAD_LV))]
  "TARGET_RVTT_BH"
{
  rtx live = operands[1];
  emit_insn (gen_rvtt_bh_sfpmad_int(operands[0], live, operands[2], operands[3], operands[4], operands[5]));
  DONE;
})

(define_insn "rvtt_bh_sfpmad_int"
  [(set (match_operand:V64SF 0 "register_operand" "=xr, xr")
        (unspec_volatile [(match_operand:V64SF 1 "reg_or_vec0_operand" "xn, 0")
                          (match_operand:V64SF 2 "register_operand"  "xr, xr")
                          (match_operand:V64SF 3 "register_operand"  "xr, xr")
                          (match_operand:V64SF 4 "register_operand"  "xr, xr")
                          (match_operand:SI    5 "const_int_operand" "N04U,N04U")] UNSPECV_BH_SFPMAD_INT))]
  "TARGET_RVTT_BH"
  "SFPMAD\t%0, %2, %3, %4, %5"
)

(define_insn "rvtt_bh_sfpsetcc_i"
  [(unspec_volatile [(match_operand:SI    0 "const_int_operand" "N01U")
                     (match_operand:SI    1 "const_int_operand" "N04U")] UNSPECV_BH_SFPSETCC_I)]
  "TARGET_RVTT_BH"
  "SFPSETCC\tL0, %0, %1"
)

(define_insn "rvtt_bh_sfpsetcc_v"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "xr")
                     (match_operand:SI    1 "const_int_operand" "N04U")] UNSPECV_BH_SFPSETCC_V)]
  "TARGET_RVTT_BH"
  "SFPSETCC\t%0, 0, %1"
)

(define_expand "rvtt_bh_sfpxfcmps"
  [(set (match_operand:SI 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"   "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "reg_or_const_int_operand" "")
                          (match_operand:SI    4 "reg_or_const_int_operand" "")
                          (match_operand:SI    5 "const_int_operand" "")
                          (match_operand:SI    6 "const_int_operand" "")] UNSPECV_BH_SFPXFCMPS))]
  "TARGET_RVTT_BH"
{
  rvtt_bh_emit_sfpxfcmps(operands[1], operands[2], operands[3], operands[6]);
  DONE;
})

(define_expand "rvtt_bh_sfpxfcmpv"
  [(set (match_operand:SI 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "const_int_operand" "")] UNSPECV_BH_SFPXFCMPV))]
  "TARGET_RVTT_BH"
{
  rvtt_bh_emit_sfpxfcmpv(operands[1], operands[2], operands[3]);
  DONE;
})

(define_insn "rvtt_bh_sfpencc"
  [(unspec_volatile [(match_operand:SI 0 "const_int_operand" "N02U")
                     (match_operand:SI 1 "const_int_operand" "N04U")] UNSPECV_BH_SFPENCC)]
  "TARGET_RVTT_BH"
  "SFPENCC\t%0, %1"
)

(define_insn "rvtt_bh_sfpcompc"
  [(unspec_volatile [(const_int 0)] UNSPECV_BH_SFPCOMPC)]
  "TARGET_RVTT_BH"
  "SFPCOMPC"
)

(define_insn "rvtt_bh_sfppushc"
  [(unspec_volatile [(match_operand:SI 0 "const_int_operand" "N04U")] UNSPECV_BH_SFPPUSHC)]
  "TARGET_RVTT_BH"
  "SFPPUSHC\t%0")

(define_insn "rvtt_bh_sfppopc"
  [(unspec_volatile [(match_operand:SI 0 "const_int_operand" "N04U")] UNSPECV_BH_SFPPOPC)]
  "TARGET_RVTT_BH"
  "SFPPOPC\t%0"
)

(define_insn "rvtt_bh_sfplut"
  [(set (match_operand:V64SF 0 "register_operand" "=x3")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x0")
                          (match_operand:V64SF 2 "register_operand"  "x1")
                          (match_operand:V64SF 3 "register_operand"  "x2")
                          (match_operand:V64SF 4 "register_operand"  "0")
                          (match_operand:SI    5 "const_int_operand" "N04U")] UNSPECV_BH_SFPLUT))]
  "TARGET_RVTT_BH"
  "SFPLUT\t%0, %5"
)

(define_insn "rvtt_bh_sfplutfp32_3r"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x0")
                          (match_operand:V64SF 2 "register_operand"  "x1")
                          (match_operand:V64SF 3 "register_operand"  "x2")
                          (match_operand:V64SF 4 "register_operand"  "x3")
                          (match_operand:SI    5 "const_int_operand" "N04U")] UNSPECV_BH_SFPLUTFP32_3R))
        (clobber (match_scratch:V64SF 6 "=x7"))
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
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "x0")
                          (match_operand:V64SF 2 "register_operand"  "x1")
                          (match_operand:V64SF 3 "register_operand"  "x2")
                          (match_operand:V64SF 4 "register_operand"  "x4")
                          (match_operand:V64SF 5 "register_operand"  "x5")
                          (match_operand:V64SF 6 "register_operand"  "x6")
                          (match_operand:V64SF 7 "register_operand"  "x3")
                          (match_operand:SI    8 "const_int_operand" "N04U")] UNSPECV_BH_SFPLUTFP32_6R))]
  "TARGET_RVTT_BH"
  "SFPLUTFP32\t%0, %8")

(define_insn "rvtt_bh_sfpconfig_v"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"   "x0")
                     (match_operand:SI    1 "const_int_operand"  "N04U")] UNSPECV_BH_SFPCONFIG_V)]
  "TARGET_RVTT_BH"
  "SFPCONFIG\t%1, 0, 0")

(define_insn "rvtt_bh_sfpswap"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"   "+xr")
                     (match_operand:V64SF 1 "register_operand"   "+xr")
                     (match_operand:SI    2 "const_int_operand"  "N04U")] UNSPECV_BH_SFPSWAP)]
  "TARGET_RVTT_BH"
  "SFPSWAP\t%0, %1, %2")

(define_insn "rvtt_bh_sfptransp"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"   "+x0")
                     (match_operand:V64SF 1 "register_operand"   "+x1")
                     (match_operand:V64SF 2 "register_operand"   "+x2")
                     (match_operand:V64SF 3 "register_operand"   "+x3")] UNSPECV_BH_SFPTRANSP)]
  "TARGET_RVTT_BH"
  "SFPTRANSP")

(define_insn "rvtt_bh_sfpshft2_g"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"   "+x0")
                     (match_operand:V64SF 1 "register_operand"   "+x1")
                     (match_operand:V64SF 2 "register_operand"   "+x2")
                     (match_operand:V64SF 3 "register_operand"   "+x3")
                     (match_operand:SI    4 "const_int_operand"  "N04U")] UNSPECV_BH_SFPSHFT2_G)]
  "TARGET_RVTT_BH"
  "SFPSHFT2\t0, L0, L0, %0, %1, %2, %3, %4")

(define_insn "rvtt_bh_sfpshft2_ge"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"   "xr")
                     (match_operand:V64SF 1 "register_operand"   "+x0")
                     (match_operand:V64SF 2 "register_operand"   "+x1")
                     (match_operand:V64SF 3 "register_operand"   "+x2")
                     (match_operand:V64SF 4 "register_operand"   "+x3")] UNSPECV_BH_SFPSHFT2_GE)]
  "TARGET_RVTT_BH"
  "SFPSHFT2\t0, %0, L0, %1, %2, %3, %4, 2")

(define_insn "rvtt_bh_sfpmul24"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "xr")
                          (match_operand:V64SF 2 "register_operand"  "xr")
                          (match_operand:SI    3 "const_int_operand" "N04U")] UNSPECV_BH_SFPMUL24))]
  "TARGET_RVTT_BH"
  "SFPMUL24\t%0, %1, %2, %3"
)

(define_insn "rvtt_bh_sfpmul24_lv"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand" "0")
                          (match_operand:V64SF 2 "register_operand"  "xr")
                          (match_operand:V64SF 3 "register_operand"  "xr")
                          (match_operand:SI    4 "const_int_operand" "N04U")] UNSPECV_BH_SFPMUL24))]
  "TARGET_RVTT_BH"
  "SFPMUL24\t%0, %2, %3, %4"
)

(define_insn "rvtt_bh_sfparecip"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:SI    2 "const_int_operand" "")] UNSPECV_BH_SFPARECIP))]
  "TARGET_RVTT_BH"
  "SFPARECIP\t%0, %1, %2"
)

(define_insn "rvtt_bh_sfparecip_lv"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "0")
                          (match_operand:V64SF 2 "register_operand"  "xr")
                          (match_operand:SI    3 "const_int_operand" "N04U")] UNSPECV_BH_SFPARECIP))]
  "TARGET_RVTT_BH"
  "SFPARECIP\t%0, %2, %3"
)

(define_insn "rvtt_bh_sfpgt"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "xr")
                          (match_operand:SI    2 "const_int_operand" "N04U")] UNSPECV_BH_SFPGT))]
  "TARGET_RVTT_BH"
  "SFPGT\t%0, %1, 0, %2"
)

(define_insn "rvtt_bh_sfple"
  [(set (match_operand:V64SF 0 "register_operand" "=xr")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "xr")
                          (match_operand:SI    2 "const_int_operand" "N04U")] UNSPECV_BH_SFPLE))]
  "TARGET_RVTT_BH"
  "SFPLE\t%0, %1, 0, %2"
)

(include "tt/rvtt-peephole-bh.md")
