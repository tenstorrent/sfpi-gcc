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
  UNSPECVSFPLOAD_WH
  UNSPECVSFPLOAD_LV_WH
  UNSPECVSFPLOAD_INT_WH
  UNSPECVSFPXLOADI_WH
  UNSPECVSFPXLOADI_LV_WH
  UNSPECVSFPLOADI_INT_WH
  UNSPECVSFPSTORE_WH
  UNSPECVSFPMULI_WH
  UNSPECVSFPMULI_INT_WH
  UNSPECVSFPADDI_WH
  UNSPECVSFPADDI_INT_WH
  UNSPECVSFPMUL_WH
  UNSPECVSFPMUL_LV_WH
  UNSPECVSFPMUL_INT_WH
  UNSPECVSFPADD_WH
  UNSPECVSFPADD_LV_WH
  UNSPECVSFPADD_INT_WH
  UNSPECVSFPIADD_V_INT_WH
  UNSPECVSFPXIADD_V_WH
  UNSPECVSFPIADD_I_WH
  UNSPECVSFPIADD_I_LV_WH
  UNSPECVSFPXIADD_I_WH
  UNSPECVSFPXIADD_I_LV_WH
  UNSPECVSFPIADD_I_INT_WH
  UNSPECVSFPSETMAN_V_WH
  UNSPECVSFPSETMAN_I_WH
  UNSPECVSFPSETMAN_I_LV_WH
  UNSPECVSFPSETMAN_I_INT_WH
  UNSPECVSFPSETEXP_V_WH
  UNSPECVSFPSETEXP_I_WH
  UNSPECVSFPSETEXP_I_LV_WH
  UNSPECVSFPSETEXP_I_INT_WH
  UNSPECVSFPSETSGN_V_WH
  UNSPECVSFPSETSGN_I_WH
  UNSPECVSFPSETSGN_I_LV_WH
  UNSPECVSFPSETSGN_I_INT_WH
  UNSPECVSFPMAD_WH
  UNSPECVSFPMAD_LV_WH
  UNSPECVSFPMAD_INT_WH
  UNSPECVSFPMOV_WH
  UNSPECVSFPMOV_LV_WH
  UNSPECVSFPMOV_INT_WH
  UNSPECVSFPDIVP2_WH
  UNSPECVSFPDIVP2_LV_WH
  UNSPECVSFPDIVP2_INT_WH
  UNSPECVSFPXFCMPS_WH
  UNSPECVSFPXFCMPV_WH
  UNSPECVSFPCAST_WH
  UNSPECVSFPCAST_LV_WH
  UNSPECVSFPCAST_INT_WH
  UNSPECVSFPSTOCHRND_I_WH
  UNSPECVSFPSTOCHRND_I_LV_WH
  UNSPECVSFPSTOCHRND_I_INT_WH
  UNSPECVSFPSTOCHRND_V_WH
  UNSPECVSFPSTOCHRND_V_LV_WH
  UNSPECVSFPSTOCHRND_V_INT_WH
  UNSPECVSFPCONFIG_V_WH
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
	  ] UNSPECVSFPLOAD_WH))]
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
	  ] UNSPECVSFPLOAD_LV_WH))]
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
	  ] UNSPECVSFPLOAD_INT_WH))]
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
	  ] UNSPECVSFPXLOADI_WH))]
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
	  ] UNSPECVSFPXLOADI_LV_WH))]
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
	  ] UNSPECVSFPLOADI_INT_WH))]
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
     ] UNSPECVSFPSTORE_WH)]
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
     ] UNSPECVSFPSTORE_WH)]
  "TARGET_XTT_TENSIX_WH"
  "SFPSTORE\t%3, %x0, %1, %2"
  [(set_attr "type" "tensix")])

(define_int_iterator wormhole_muliaddi [UNSPECVSFPMULI_WH UNSPECVSFPADDI_WH])
(define_int_attr wormhole_muliaddi_name [(UNSPECVSFPMULI_WH "muli") (UNSPECVSFPADDI_WH "addi")])
(define_int_attr wormhole_muliaddi_call [(UNSPECVSFPMULI_WH "MULI") (UNSPECVSFPADDI_WH "ADDI")])
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

(define_int_iterator wormhole_muliaddi_int [UNSPECVSFPMULI_INT_WH UNSPECVSFPADDI_INT_WH])
(define_int_attr wormhole_muliaddi_int_name [(UNSPECVSFPMULI_INT_WH "muli") (UNSPECVSFPADDI_INT_WH "addi")])
(define_int_attr wormhole_muliaddi_int_call [(UNSPECVSFPMULI_INT_WH "MULI") (UNSPECVSFPADDI_INT_WH "ADDI")])
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
	  ] UNSPECVSFPDIVP2_WH))]
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
	  ] UNSPECVSFPDIVP2_LV_WH))]
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
	  ] UNSPECVSFPDIVP2_INT_WH))]
  "TARGET_XTT_TENSIX_WH"
  "SFPDIVP2\t%0, %x3, %2, %4"
  [(set_attr "type" "tensix")])

(define_int_iterator wormhole_muladd [UNSPECVSFPMUL_WH UNSPECVSFPADD_WH])
(define_int_attr wormhole_muladd_name [(UNSPECVSFPMUL_WH "mul") (UNSPECVSFPADD_WH "add")])
(define_expand "rvtt_sfp<wormhole_muladd_name>_wh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:SI    3 "const_int_operand")
	  ] wormhole_muladd))]
  "TARGET_XTT_TENSIX_WH"
{
  rtx live = rvtt_gen_rtx_noval (XTT32SImode);
  emit_insn (gen_rvtt_sfp<wormhole_muladd_name>_int_wh (operands[0], live, operands[1], operands[2], operands[3]));
  DONE;
})

(define_int_iterator wormhole_muladd_lv [UNSPECVSFPMUL_LV_WH UNSPECVSFPADD_LV_WH])
(define_int_attr wormhole_muladd_name_lv [(UNSPECVSFPMUL_LV_WH "mul") (UNSPECVSFPADD_LV_WH "add")])
(define_expand "rvtt_sfp<wormhole_muladd_name_lv>_lv_wh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:XTT32SI 3 "register_operand")
          (match_operand:SI    4 "const_int_operand")
	  ] wormhole_muladd_lv))]
  "TARGET_XTT_TENSIX_WH"
{
  rtx live = operands[1];
  emit_insn (gen_rvtt_sfp<wormhole_muladd_name_lv>_int_wh (operands[0], live, operands[2], operands[3], operands[4]));
  DONE;
})

(define_int_iterator wormhole_muladd_int [UNSPECVSFPMUL_INT_WH UNSPECVSFPADD_INT_WH])
(define_int_attr wormhole_muladd_name_int [
  (UNSPECVSFPMUL_INT_WH "mul")
  (UNSPECVSFPADD_INT_WH "add")
  ])
(define_int_attr wormhole_muladd_call_int [
  (UNSPECVSFPMUL_INT_WH "MUL\t%0, %x2, %x3, L9")
  (UNSPECVSFPADD_INT_WH "ADD\t%0, L10, %x2, %x3")
  ])
(define_insn "rvtt_sfp<wormhole_muladd_name_int>_int_wh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr, xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn,0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc, xrxc")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "xrxc, xrxc")
          (match_operand:SI    4 "const_int_operand" "N04U,N04U")
	  ] wormhole_muladd_int))]
  "TARGET_XTT_TENSIX_WH"
  "SFP<wormhole_muladd_call_int>, %4"
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_wh" "dynamic")])

(define_insn "rvtt_sfpiadd_v_int_wh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	(match_operand:XTT32SI 1 "register_operand"  "0")
        (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
        (match_operand:SI    3 "const_int_operand" "N04U")
	] UNSPECVSFPIADD_V_INT_WH))]
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
	  ] UNSPECVSFPIADD_I_INT_WH))]
  "TARGET_XTT_TENSIX_WH"
  "SFPIADD\t%0, %x2, %3, %4"
  [(set_attr "type" "tensix")])

(define_expand "rvtt_sfpxiadd_v_wh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand"  "0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI    3 "const_int_operand" "N04U")
	  ] UNSPECVSFPXIADD_V_WH))]
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
	  ] UNSPECVSFPXIADD_I_WH))]
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
	  ] UNSPECVSFPXIADD_I_LV_WH))]
  "TARGET_XTT_TENSIX_WH"
{
  rtx live = operands[2];
  rvtt_emit_sfpxiadd_i_wh (operands[0], live, operands[1], operands[3], operands[4], operands[7]);
  DONE;
})

(define_expand "rvtt_sfpcast_wh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand")
          (match_operand:SI    2 "const_int_operand")
	  ] UNSPECVSFPCAST_WH))]
  "TARGET_XTT_TENSIX_WH"
{
  rtx live = rvtt_gen_rtx_noval (XTT32SImode);
  emit_insn (gen_rvtt_sfpcast_int_wh (operands[0], live, operands[1], operands[2]));
  DONE;
})

(define_expand "rvtt_sfpcast_lv_wh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:SI    3 "const_int_operand")
	  ] UNSPECVSFPCAST_LV_WH))]
  "TARGET_XTT_TENSIX_WH"
{
  rtx live = operands[2];
  emit_insn (gen_rvtt_sfpcast_int_wh (operands[0], live, operands[2], operands[3]));
  DONE;
})

(define_insn "rvtt_sfpcast_int_wh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr, xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn,0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc, xrxc")
          (match_operand:SI    3 "const_int_operand" "N04U,N04U")
	  ] UNSPECVSFPCAST_INT_WH))]
  "TARGET_XTT_TENSIX_WH"
  "SFPCAST %0, %x2, %3"
  [(set_attr "type" "tensix")])

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
	  ] UNSPECVSFPSTOCHRND_I_WH))]
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
	  ] UNSPECVSFPSTOCHRND_I_LV_WH))]
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
	  ] UNSPECVSFPSTOCHRND_I_INT_WH))]
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
	  ] UNSPECVSFPSTOCHRND_V_WH))]
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
	  ] UNSPECVSFPSTOCHRND_V_LV_WH))]
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
	  ] UNSPECVSFPSTOCHRND_V_INT_WH))]
  "TARGET_XTT_TENSIX_WH"
  "SFPSTOCHRND\t%0, %x3, %x4, %5, %2, 0"
  [(set_attr "type" "tensix")])

(define_int_iterator wormhole_set_float_op_v [UNSPECVSFPSETEXP_V_WH UNSPECVSFPSETMAN_V_WH UNSPECVSFPSETSGN_V_WH])
(define_int_attr wormhole_set_float_name_v [
  (UNSPECVSFPSETEXP_V_WH "exp")
  (UNSPECVSFPSETMAN_V_WH "man")
  (UNSPECVSFPSETSGN_V_WH "sgn")
  ])
(define_int_attr wormhole_set_float_call_v [
  (UNSPECVSFPSETEXP_V_WH "EXP")
  (UNSPECVSFPSETMAN_V_WH "MAN")
  (UNSPECVSFPSETSGN_V_WH "SGN")
  ])
(define_insn "rvtt_sfpset<wormhole_set_float_name_v>_v_wh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand"  "0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
	  ] wormhole_set_float_op_v))]
  "TARGET_XTT_TENSIX_WH"
  "SFPSET<wormhole_set_float_call_v>\t%0, %x2, 0, 0"
  [(set_attr "type" "tensix")])

(define_int_iterator wormhole_set_float_op_i [UNSPECVSFPSETEXP_I_WH UNSPECVSFPSETSGN_I_WH])
(define_int_attr wormhole_set_float_name_i [(UNSPECVSFPSETEXP_I_WH "exp") (UNSPECVSFPSETSGN_I_WH "sgn")])
(define_int_attr wormhole_set_float_call_i [(UNSPECVSFPSETEXP_I_WH "EXP") (UNSPECVSFPSETSGN_I_WH "SGN")])
(define_expand "rvtt_sfpset<wormhole_set_float_name_i>_i_wh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:SI    2 "reg_or_const_int_operand")
          (match_operand:SI    3 "reg_or_0_operand")
          (match_operand:SI    4 "const_int_operand")
          (match_operand:XTT32SI 5 "reg_or_cstlreg_operand")
	  ] wormhole_set_float_op_i))]
  "TARGET_XTT_TENSIX_WH"
{
  rtx live = rvtt_gen_rtx_noval (XTT32SImode);
  rtx insn = nullptr;
  if (CONST_INT_P (operands[2]))
    insn = gen_rvtt_sfpset<wormhole_set_float_name_i>_i_int_wh
      (operands[0], live, rvtt_clamp_unsigned (operands[2], 0xFFF), operands[5]);
  else
    {
      unsigned op = TT_OP_WH_SFPSET<wormhole_set_float_call_i> (0, 0, 0, 1);
      insn = rvtt_sfpsynth_insn_dst (operands[1], CODE_FOR_rvtt_sfpset<wormhole_set_float_name_i>_i_int_wh,
      	     			     0, operands[3], op, operands[4],
				     operands[5], 4, operands[0], 8, live);
    }
  emit_insn (insn);
  DONE;
})

(define_int_iterator wormhole_set_float_op_i_lv [UNSPECVSFPSETEXP_I_LV_WH UNSPECVSFPSETSGN_I_LV_WH])
(define_int_attr wormhole_set_float_name_i_lv [(UNSPECVSFPSETEXP_I_LV_WH "exp") (UNSPECVSFPSETSGN_I_LV_WH "sgn")])
(define_int_attr wormhole_set_float_call_i_lv [(UNSPECVSFPSETEXP_I_LV_WH "EXP") (UNSPECVSFPSETSGN_I_LV_WH "SGN")])
(define_expand "rvtt_sfpset<wormhole_set_float_name_i_lv>_i_lv_wh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_0_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:XTT32SI 6 "reg_or_cstlreg_operand")
	  ] wormhole_set_float_op_i_lv))]
  "TARGET_XTT_TENSIX_WH"
{
  rtx live = operands[2];
  rtx insn = nullptr;
  if (CONST_INT_P (operands[3]))
    insn = gen_rvtt_sfpset<wormhole_set_float_name_i_lv>_i_int_wh
      (operands[0], live, rvtt_clamp_unsigned (operands[3], 0xFFF), operands[6]);
  else
    {
      unsigned op = TT_OP_WH_SFPSET<wormhole_set_float_call_i_lv> (0, 0, 0, 1);
      insn = rvtt_sfpsynth_insn_dst (operands[1], CODE_FOR_rvtt_sfpset<wormhole_set_float_name_i_lv>_i_int_wh,
      	     			     0, operands[4], op, operands[5],
				     operands[6], 4, operands[0], 8, live);
    }
  emit_insn (insn);
  DONE;
})

(define_int_iterator wormhole_set_float_op_i_int [UNSPECVSFPSETEXP_I_INT_WH UNSPECVSFPSETSGN_I_INT_WH])
(define_int_attr wormhole_set_float_name_i_int [(UNSPECVSFPSETEXP_I_INT_WH "exp") (UNSPECVSFPSETSGN_I_INT_WH "sgn")])
(define_int_attr wormhole_set_float_call_i_int [(UNSPECVSFPSETEXP_I_INT_WH "EXP") (UNSPECVSFPSETSGN_I_INT_WH "SGN")])
(define_insn "rvtt_sfpset<wormhole_set_float_name_i_int>_i_int_wh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr, xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn,0")
          (match_operand:SI    2 "const_int_operand" "N12U,N12U")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "xrxc, xrxc")
	  ] wormhole_set_float_op_i_int))]
  "TARGET_XTT_TENSIX_WH"
  "SFPSET<wormhole_set_float_call_i_int>\t%0, %x3, %2, 1"
  [(set_attr "type" "tensix")])

(define_expand "rvtt_sfpsetman_i_wh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:SI    2 "reg_or_const_int_operand")
          (match_operand:SI    3 "reg_or_0_operand")
          (match_operand:SI    4 "const_int_operand")
          (match_operand:XTT32SI 5 "reg_or_cstlreg_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] UNSPECVSFPSETMAN_I_WH))]
  "TARGET_XTT_TENSIX_WH"
{
  rtx live = rvtt_gen_rtx_noval (XTT32SImode);
  rvtt_emit_sfpsetman_wh (operands[0], live, operands[1], operands[2], operands[5]);
  DONE;
})

(define_expand "rvtt_sfpsetman_i_lv_wh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_0_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:XTT32SI 6 "reg_or_cstlreg_operand")
          (match_operand:SI    7 "const_int_operand")
	  ] UNSPECVSFPSETMAN_I_LV_WH))]
  "TARGET_XTT_TENSIX_WH"
{
  rtx live = operands[2];
  rvtt_emit_sfpsetman_wh (operands[0], live, operands[1], operands[3], operands[6]);
  DONE;
})

(define_insn "rvtt_sfpsetman_i_int_wh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr, xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn,0")
          (match_operand:SI    2 "const_int_operand" "N12U,N12U")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "xrxc, xrxc")
	  ] UNSPECVSFPSETMAN_I_INT_WH))]
  "TARGET_XTT_TENSIX_WH"
  "SFPSETMAN\t%0, %x3, %2, 1"
  [(set_attr "type" "tensix")])

(define_expand "rvtt_sfpmad_wh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
          (match_operand:SI    4 "const_int_operand")
	  ] UNSPECVSFPMAD_WH))]
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
	  ] UNSPECVSFPMAD_LV_WH))]
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
	  ] UNSPECVSFPMAD_INT_WH))]
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
	  ] UNSPECVSFPXFCMPS_WH))]
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
	  ] UNSPECVSFPXFCMPV_WH))]
  "TARGET_XTT_TENSIX_WH"
{
  rvtt_emit_sfpxfcmpv_wh (operands[1], operands[2], operands[3]);
  DONE;
})

(define_insn "rvtt_sfpconfig_v_wh"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "register_operand"   "x0")
     (match_operand:SI    1 "const_int_operand"  "N04U")
     ] UNSPECVSFPCONFIG_V_WH)]
  "TARGET_XTT_TENSIX_WH"
  "SFPCONFIG\t%1, 0, 0"
  [(set_attr "type" "tensix")])
