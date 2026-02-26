;; Machine description for Tenstorrent SFPU Wormhole Intrinsics.
;; Copyright (C) 2022-2026 Tenstorrent Inc.
;; Originated by Paul Keller (pkeller@tenstorrent.com)
;; Rewritten by Nathan Sidwell (nsidwell@tenstorrent.com, nathan@acm.org).

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
  UNSPECV_SFPLOAD_WH
  UNSPECV_SFPLOAD_LV_WH
  UNSPECV_SFPLOAD_INT_WH
  UNSPECV_SFPXLOADI_WH
  UNSPECV_SFPXLOADI_LV_WH
  UNSPECV_SFPLOADI_INT_WH
  UNSPECV_SFPSTORE_WH
  UNSPECV_SFPMULI_WH
  UNSPECV_SFPMULI_INT_WH
  UNSPECV_SFPADDI_WH
  UNSPECV_SFPADDI_INT_WH
  UNSPECV_SFPIADD_V_INT_WH
  UNSPECV_SFPXIADD_V_WH
  UNSPECV_SFPIADD_I_WH
  UNSPECV_SFPIADD_I_LV_WH
  UNSPECV_SFPXIADD_I_WH
  UNSPECV_SFPXIADD_I_LV_WH
  UNSPECV_SFPIADD_I_INT_WH
  UNSPECV_SFPMAD_WH
  UNSPECV_SFPMAD_LV_WH
  UNSPECV_SFPMAD_INT_WH
  UNSPECV_SFPMOV_WH
  UNSPECV_SFPMOV_LV_WH
  UNSPECV_SFPMOV_INT_WH
  UNSPECV_SFPDIVP2_WH
  UNSPECV_SFPDIVP2_LV_WH
  UNSPECV_SFPDIVP2_INT_WH
  UNSPECV_SFPXFCMPS_WH
  UNSPECV_SFPXFCMPV_WH
  UNSPECV_SFPSTOCHRND_I_WH
  UNSPECV_SFPSTOCHRND_I_LV_WH
  UNSPECV_SFPSTOCHRND_I_INT_WH
  UNSPECV_SFPSTOCHRND_V_WH
  UNSPECV_SFPSTOCHRND_V_LV_WH
  UNSPECV_SFPSTOCHRND_V_INT_WH
  UNSPECV_SFPCONFIG_V_WH
])

(define_expand "rvtt_sfpload_wh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI 1 "address_operand")
          (match_operand:SI 2 "const_int_operand")
          (match_operand:SI 3 "const_int_operand")
          (match_operand:SI 4 "reg_or_const_int_operand")
          (match_operand:SI 5 "reg_or_0_operand")
          (match_operand:SI 6 "const_int_operand")
	  ] UNSPECV_SFPLOAD_WH))]
  "TARGET_XTT_TENSIX_WH"
{
  rtx live = rvtt_gen_rtx_noval (XTT32SImode);
  rvtt_emit_sfpload_wh (operands[0], live, operands[1], operands[2],
  		        operands[3], operands[4], operands[5], operands[6]);
  DONE;
})

(define_expand "rvtt_sfpload_lv_wh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "const_int_operand")
          (match_operand:SI    4 "const_int_operand")
          (match_operand:SI    5 "reg_or_const_int_operand")
          (match_operand:SI    6 "reg_or_0_operand")
          (match_operand:SI    7 "const_int_operand")
	  ] UNSPECV_SFPLOAD_LV_WH))]
  "TARGET_XTT_TENSIX_WH"
{
  rtx live = operands[2];
  rvtt_emit_sfpload_wh (operands[0], live, operands[1], operands[3],
  		        operands[4], operands[5], operands[6], operands[7]);
  DONE;
})

(define_insn "rvtt_sfpload_int_wh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr, xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn, 0")
          (match_operand:SI    2 "const_int_operand" "N04U,N04U")
          (match_operand:SI    3 "const_int_operand" "N02U,N02U")
          (match_operand:SI    4 "const_int_operand" "N14U,N14U")
	  ] UNSPECV_SFPLOAD_INT_WH))]
  "TARGET_XTT_TENSIX_WH"
  "@
   SFPLOAD\t%0, %4, %2, %3
   SFPLOAD\t%0, %4, %2, %3"
  [(set_attr "type" "tensix")])

;;; SFPLOADI and SFPLOADI_LV
(define_expand "rvtt_sfpxloadi_wh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI 1 "address_operand")
          (match_operand:SI 2 "const_int_operand")
          (match_operand:SI 3 "reg_or_const_int_operand")
          (match_operand:SI 4 "reg_or_0_operand")
          (match_operand:SI 5 "const_int_operand")
	  ] UNSPECV_SFPXLOADI_WH))]
  "TARGET_XTT_TENSIX_WH"
{
  rtx live = rvtt_gen_rtx_noval (XTT32SImode);
  rvtt_emit_sfpxloadi_wh (operands[0], live, operands[1], operands[2],
  			  operands[3], operands[4], operands[5]);
  DONE;
})

(define_expand "rvtt_sfpxloadi_lv_wh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "const_int_operand")
          (match_operand:SI    4 "reg_or_const_int_operand")
          (match_operand:SI    5 "reg_or_0_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] UNSPECV_SFPXLOADI_LV_WH))]
  "TARGET_XTT_TENSIX_WH"
{
  rtx live = operands[2];
  rvtt_emit_sfpxloadi_wh (operands[0], live, operands[1], operands[3],
  			  operands[4], operands[5], operands[6]);
  DONE;
})

(define_insn "rvtt_sfploadi_int_wh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr,xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn,xn,0,0")
          (match_operand:SI    2 "const_int_operand" "N04U,N04U,N04U,N04U")
          (match_operand:SI    3 "const_int_operand" "N16S,N16U,N16S,N16U")
	  ] UNSPECV_SFPLOADI_INT_WH))]
  "TARGET_XTT_TENSIX_WH"
  "@
  SFPLOADI\t%0, %s3, %2
  SFPLOADI\t%0, %u3, %2
  SFPLOADI\t%0, %s3, %2
  SFPLOADI\t%0, %u3, %2"
  [(set_attr "type" "tensix")])

(define_expand "rvtt_sfpstore_wh"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "address_operand")
     (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
     (match_operand:SI    2 "const_int_operand")
     (match_operand:SI    3 "const_int_operand")
     (match_operand:SI    4 "reg_or_const_int_operand")
     (match_operand:SI    5 "reg_or_0_operand")
     (match_operand:SI    6 "const_int_operand")
     ] UNSPECV_SFPSTORE_WH)]
  "TARGET_XTT_TENSIX_WH"
{
  rtx insn = nullptr;
  if (CONST_INT_P (operands[4]))
    insn = gen_rvtt_sfpstore_int_wh (operands[1], operands[2],operands[3],
				     rvtt_clamp_unsigned (operands[4], 0x3FFF));
  else
    {
      unsigned op = TT_OP_WH_SFPSTORE(0, INTVAL (operands[2]), INTVAL (operands[3]), 0);
      insn = rvtt_sfpsynth_store_insn (operands[0], CODE_FOR_rvtt_sfpstore_int_wh,
      	     			       0, operands[5], op, operands[6],
				       operands[1], 20);
    }
  emit_insn (insn);
  DONE;
})

;; stores cannot write from L12..L15 due to load macro side loading possibility
(define_insn "rvtt_sfpstore_int_wh"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "reg_or_cstlreg_operand"  "xrxs")
     (match_operand:SI    1 "const_int_operand" "N04U")
     (match_operand:SI    2 "const_int_operand" "N02U")
     (match_operand:SI    3 "const_int_operand" "N14U")
     ] UNSPECV_SFPSTORE_WH)]
  "TARGET_XTT_TENSIX_WH"
  "SFPSTORE\t%x0, %3, %1, %2"
  [(set_attr "type" "tensix")])

(define_int_iterator wormhole_muliaddi [UNSPECV_SFPMULI_WH UNSPECV_SFPADDI_WH])
(define_int_attr wormhole_muliaddi_name [(UNSPECV_SFPMULI_WH "muli") (UNSPECV_SFPADDI_WH "addi")])
(define_int_attr wormhole_muliaddi_call [(UNSPECV_SFPMULI_WH "MULI") (UNSPECV_SFPADDI_WH "ADDI")])
(define_expand "rvtt_sfp<wormhole_muliaddi_name>_wh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_0_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] wormhole_muliaddi))]
  "TARGET_XTT_TENSIX_WH"
{
  rtx insn = nullptr;
  if (CONST_INT_P (operands[3]))
    insn = gen_rvtt_sfp<wormhole_muliaddi_name>_int_wh
      (operands[0], operands[2], rvtt_clamp_unsigned (operands[3], 0xFFFF), operands[6]);
  else
    {
      unsigned op = TT_OP_WH_SFP<wormhole_muliaddi_call> (0, 0, INTVAL(operands[6]));
      insn = rvtt_sfpsynth_insn_dst (operands[1], CODE_FOR_rvtt_sfp<wormhole_muliaddi_name>_int_wh,
      	     			     XTT_DELAY_DYNAMIC, operands[4], op, operands[5],
				     operands[0], 4, operands[2]);
    }
  emit_insn (insn);
  DONE;
})

(define_int_iterator wormhole_muliaddi_int [UNSPECV_SFPMULI_INT_WH UNSPECV_SFPADDI_INT_WH])
(define_int_attr wormhole_muliaddi_int_name [(UNSPECV_SFPMULI_INT_WH "muli") (UNSPECV_SFPADDI_INT_WH "addi")])
(define_int_attr wormhole_muliaddi_int_call [(UNSPECV_SFPMULI_INT_WH "MULI") (UNSPECV_SFPADDI_INT_WH "ADDI")])
(define_insn "rvtt_sfp<wormhole_muliaddi_int_name>_int_wh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand"  "0")
          (match_operand:SI    2 "const_int_operand" "N16U")
          (match_operand:SI    3 "const_int_operand" "N04U")
	  ] wormhole_muliaddi_int))]
  "TARGET_XTT_TENSIX_WH"
  "SFP<wormhole_muliaddi_int_call>\t%0, %2, %3"
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_wh" "dynamic")])

(define_expand "rvtt_sfpdivp2_wh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:SI    2 "reg_or_const_int_operand")
          (match_operand:SI    3 "reg_or_0_operand")
          (match_operand:SI    4 "const_int_operand")
          (match_operand:XTT32SI 5 "register_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] UNSPECV_SFPDIVP2_WH))]
  "TARGET_XTT_TENSIX_WH"
{
  rtx live = rvtt_gen_rtx_noval (XTT32SImode);
  rvtt_emit_sfpdivp2_wh (operands[0], live, operands[1], operands[2],
  			 operands[5], operands[6], operands[3], operands[4]);
  DONE;
})

(define_expand "rvtt_sfpdivp2_lv_wh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_0_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:XTT32SI 6 "register_operand")
          (match_operand:SI    7 "const_int_operand")
	  ] UNSPECV_SFPDIVP2_LV_WH))]
  "TARGET_XTT_TENSIX_WH"
{
  rtx live = operands[2];
  rvtt_emit_sfpdivp2_wh (operands[0], live, operands[1], operands[3],
  			 operands[6], operands[7], operands[4], operands[5]);
  DONE;
})

(define_insn "rvtt_sfpdivp2_int_wh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr, xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn, 0")
          (match_operand:SI    2 "const_int_operand" "N12S,N12S")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "xrxc, xrxc")
          (match_operand:SI    4 "const_int_operand" "N04U,N04U")
	  ] UNSPECV_SFPDIVP2_INT_WH))]
  "TARGET_XTT_TENSIX_WH"
  "SFPDIVP2\t%0, %x3, %2, %4"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpiadd_v_int_wh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	(match_operand:XTT32SI 1 "register_operand"  "0")
        (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
        (match_operand:SI    3 "const_int_operand" "N04U")
	] UNSPECV_SFPIADD_V_INT_WH))]
  "TARGET_XTT_TENSIX_WH"
  "SFPIADD\t%0, %x2, 0, %3"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpiadd_i_int_wh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr, xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn,0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc, xrxc")
          (match_operand:SI    3 "const_int_operand" "n, n")
          (match_operand:SI    4 "const_int_operand" "N04U,N04U")
	  ] UNSPECV_SFPIADD_I_INT_WH))]
  "TARGET_XTT_TENSIX_WH"
  "SFPIADD\t%0, %x2, %3, %4"
  [(set_attr "type" "tensix")])

(define_expand "rvtt_sfpxiadd_v_wh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand"  "0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI    3 "const_int_operand" "N04U")
	  ] UNSPECV_SFPXIADD_V_WH))]
  "TARGET_XTT_TENSIX_WH"
{
  rvtt_emit_sfpxiadd_v_wh (operands[0], operands[1], operands[2], operands[3]);
  DONE;
})

(define_expand "rvtt_sfpxiadd_i_wh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "const_int_operand")
          (match_operand:SI    5 "reg_or_const_int_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] UNSPECV_SFPXIADD_I_WH))]
  "TARGET_XTT_TENSIX_WH"
{
  rtx live = rvtt_gen_rtx_noval (XTT32SImode);
  rvtt_emit_sfpxiadd_i_wh (operands[0], live, operands[1], operands[2], operands[3], operands[6]);
  DONE;
})

(define_expand "rvtt_sfpxiadd_i_lv_wh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:XTT32SI 3 "register_operand")
          (match_operand:SI    4 "reg_or_const_int_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:SI    6 "reg_or_const_int_operand")
          (match_operand:SI    7 "const_int_operand")
	  ] UNSPECV_SFPXIADD_I_LV_WH))]
  "TARGET_XTT_TENSIX_WH"
{
  rtx live = operands[2];
  rvtt_emit_sfpxiadd_i_wh (operands[0], live, operands[1], operands[3], operands[4], operands[7]);
  DONE;
})

(define_expand "rvtt_sfpstochrnd_i_wh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:SI    2 "const_int_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_0_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:XTT32SI 6 "reg_or_cstlreg_operand")
          (match_operand:SI    7 "const_int_operand")
	  ] UNSPECV_SFPSTOCHRND_I_WH))]
  "TARGET_XTT_TENSIX_WH"
{
  rtx live = rvtt_gen_rtx_noval (XTT32SImode);
  rvtt_emit_sfpstochrnd_i_wh (operands[0], live, operands[1], operands[2], operands[3],
                              operands[6], operands[7], operands[4], operands[5]);
  DONE;
})

(define_expand "rvtt_sfpstochrnd_i_lv_wh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:SI    3 "const_int_operand")
          (match_operand:SI    4 "reg_or_const_int_operand")
          (match_operand:SI    5 "reg_or_0_operand")
          (match_operand:SI    6 "const_int_operand")
          (match_operand:XTT32SI 7 "reg_or_cstlreg_operand")
          (match_operand:SI    8 "const_int_operand")
	  ] UNSPECV_SFPSTOCHRND_I_LV_WH))]
  "TARGET_XTT_TENSIX_WH"
{
  rtx live = operands[2];
  rvtt_emit_sfpstochrnd_i_wh (operands[0], live, operands[1], operands[3], operands[4],
                              operands[7], operands[8], operands[5], operands[6]);
  DONE;
})

(define_insn "rvtt_sfpstochrnd_i_int_wh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr, xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn,0")
          (match_operand:SI    2 "const_int_operand" "N01U,N01U")
          (match_operand:SI    3 "const_int_operand" "N05U,N05U")
          (match_operand:XTT32SI 4 "reg_or_cstlreg_operand"  "xrxc, xrxc")
          (match_operand:SI    5 "const_int_operand" "N04U,N04U")
	  ] UNSPECV_SFPSTOCHRND_I_INT_WH))]
  "TARGET_XTT_TENSIX_WH"
  "SFPSTOCHRND\t%0, L0, %x4, %5, %2, %3"
  [(set_attr "type" "tensix")]);

(define_expand "rvtt_sfpstochrnd_v_wh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "const_int_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
          (match_operand:SI    4 "const_int_operand")
	  ] UNSPECV_SFPSTOCHRND_V_WH))]
  "TARGET_XTT_TENSIX_WH"
{
  rtx live = rvtt_gen_rtx_noval (XTT32SImode);
  emit_insn (gen_rvtt_sfpstochrnd_v_int_wh (operands[0], live, operands[1], operands[2], operands[3], operands[4]));
  DONE;
})

(define_expand "rvtt_sfpstochrnd_v_lv_wh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand")
          (match_operand:SI    2 "const_int_operand")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 4 "reg_or_cstlreg_operand")
          (match_operand:SI    5 "const_int_operand")
	  ] UNSPECV_SFPSTOCHRND_V_LV_WH))]
  "TARGET_XTT_TENSIX_WH"
{
  rtx live = operands[1];
  emit_insn (gen_rvtt_sfpstochrnd_v_int_wh (operands[0], live, operands[2], operands[3], operands[4], operands[5]));
  DONE;
})

(define_insn "rvtt_sfpstochrnd_v_int_wh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr, xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn,0")
          (match_operand:SI    2 "const_int_operand" "N01U,N01U")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "xrxc, xrxc")
          (match_operand:XTT32SI 4 "reg_or_cstlreg_operand"  "xrxc, xrxc")
          (match_operand:SI    5 "const_int_operand" "N04U,N04U")
	  ] UNSPECV_SFPSTOCHRND_V_INT_WH))]
  "TARGET_XTT_TENSIX_WH"
  "SFPSTOCHRND\t%0, %x3, %x4, %5, %2, 0"
  [(set_attr "type" "tensix")])

(define_expand "rvtt_sfpmad_wh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
          (match_operand:SI    4 "const_int_operand")
	  ] UNSPECV_SFPMAD_WH))]
  "TARGET_XTT_TENSIX_WH"
{
  rtx live = rvtt_gen_rtx_noval (XTT32SImode);
  emit_insn (gen_rvtt_sfpmad_int_wh (operands[0], live, operands[1], operands[2], operands[3], operands[4]));
  DONE;
})

(define_expand "rvtt_sfpmad_lv_wh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 4 "reg_or_cstlreg_operand")
          (match_operand:SI    5 "const_int_operand")
	  ] UNSPECV_SFPMAD_LV_WH))]
  "TARGET_XTT_TENSIX_WH"
{
  rtx live = operands[1];
  emit_insn (gen_rvtt_sfpmad_int_wh (operands[0], live, operands[2], operands[3], operands[4], operands[5]));
  DONE;
})

(define_insn "rvtt_sfpmad_int_wh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr, xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn,0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc, xrxc")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "xrxc, xrxc")
          (match_operand:XTT32SI 4 "reg_or_cstlreg_operand"  "xrxc, xrxc")
          (match_operand:SI    5 "const_int_operand" "N04U,N04U")
	  ] UNSPECV_SFPMAD_INT_WH))]
  "TARGET_XTT_TENSIX_WH"
  "SFPMAD\t%0, %x2, %x3, %x4, %5"
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_wh" "dynamic")])

(define_expand "rvtt_sfpxfcmps_wh"
  [(set (match_operand:SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_const_int_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] UNSPECV_SFPXFCMPS_WH))]
  "TARGET_XTT_TENSIX_WH"
{
  rvtt_emit_sfpxfcmps_wh (operands[1], operands[2], operands[3], operands[6]);
  DONE;
})

(define_expand "rvtt_sfpxfcmpv_wh"
  [(set (match_operand:SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "const_int_operand")
	  ] UNSPECV_SFPXFCMPV_WH))]
  "TARGET_XTT_TENSIX_WH"
{
  rvtt_emit_sfpxfcmpv_wh (operands[1], operands[2], operands[3]);
  DONE;
})

(define_insn "rvtt_sfpconfig_v_wh"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "register_operand"   "x0")
     (match_operand:SI    1 "const_int_operand"  "N04U")
     ] UNSPECV_SFPCONFIG_V_WH)]
  "TARGET_XTT_TENSIX_WH"
  "SFPCONFIG\t%1, 0, 0"
  [(set_attr "type" "tensix")])
