;; Machine description for Tenstorrent SFPU Blackhole Intrinsics.
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
  UNSPECV_SFPXLOADI_BH
  UNSPECV_SFPIADD_V_INT_BH
  UNSPECV_SFPXIADD_V_BH
  UNSPECV_SFPIADD_I_BH
  UNSPECV_SFPIADD_I_LV_BH
  UNSPECV_SFPXIADD_I_BH
  UNSPECV_SFPXIADD_I_LV_BH
  UNSPECV_SFPIADD_I_INT_BH
  UNSPECV_SFPSETMAN_BH
  UNSPECV_SFPSETEXP_BH
  UNSPECV_SFPSETSGN_BH
  UNSPECV_SFPXFCMPS_BH
  UNSPECV_SFPXFCMPV_BH
  UNSPECV_SFPCONFIG_V_BH
  UNSPECV_SFPMUL24_BH
  UNSPECV_SFPARECIP_BH
  UNSPECV_SFPGT_BH
  UNSPECV_SFPLE_BH
  UNSPECV_SFPMOV_CONFIG_BH
])

;;; SFPLOADI and SFPLOADI_LV
(define_expand "rvtt_sfpxloadi_bh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec:XTT32SI [
	  (match_operand:SI 1 "address_operand")
          (match_operand:SI 2 "const_int_operand")
          (match_operand:SI 3 "reg_or_const_int_operand")
          (match_operand:SI 4 "reg_or_0_operand")
          (match_operand:SI 5 "const_int_operand")
	  ] UNSPECV_SFPXLOADI_BH))]
  "TARGET_XTT_TENSIX_BH"
{
  rvtt_emit_sfpxloadi_bh (operands[0], rvtt_gen_rtx_noval (XTT32SImode),
  			  operands[1], operands[2], operands[3], operands[4], operands[5]);
  DONE;
})

(define_expand "rvtt_sfpxloadi_lv_bh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:SI    3 "const_int_operand")
          (match_operand:SI    4 "reg_or_const_int_operand")
          (match_operand:SI    5 "reg_or_0_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] UNSPECV_SFPXLOADI_BH))]
  "TARGET_XTT_TENSIX_BH"
{
  rvtt_emit_sfpxloadi_bh (operands[0], operands[2],
  		          operands[1], operands[3], operands[4], operands[5], operands[6]);
  DONE;
})

(define_insn "rvtt_sfploadi_int_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr,xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "0,xn,0,xn")
          (match_operand:SI    2 "const_int_operand" "N04U,N04U,N04U,N04U")
          (match_operand:SI    3 "const_int_operand" "N16S,N16S,N16U,N16U")
	  ] UNSPECV_SFPXLOADI_BH))]
  "TARGET_XTT_TENSIX_BH"
  "@
  SFPLOADI\t%0, %s3, %2
  SFPLOADI\t%0, %s3, %2
  SFPLOADI\t%0, %u3, %2
  SFPLOADI\t%0, %u3, %2"
  [(set_attr "type" "tensix")])

(define_expand "rvtt_sfpmov_config_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI 1 "const_int_operand" "N04U")
	  ] UNSPECV_SFPMOV_CONFIG_BH))]
  "TARGET_XTT_TENSIX_BH"
  {
    emit_insn (gen_rvtt_sfpmov_config_lv_bh
                 (operands[0], rvtt_gen_rtx_noval (XTT32SImode), operands[1]));
    DONE;
  })

(define_insn "rvtt_sfpmov_config_lv_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "0,xn")
          (match_operand:SI 2 "const_int_operand" "N04U,N04U")
	  ] UNSPECV_SFPMOV_CONFIG_BH))]
  "TARGET_XTT_TENSIX_BH"
  "SFPMOV\t%0, L%2, 8"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpiadd_v_int_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand"  "0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xr")
          (match_operand:SI    3 "const_int_operand" "N04U")
	  ] UNSPECV_SFPIADD_V_INT_BH))]
  "TARGET_XTT_TENSIX_BH"
  "SFPIADD\t%0, %x2, 0, %3"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpiadd_i_int_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "0,xn")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc,xrxc")
          (match_operand:SI    3 "const_int_operand" "n,n")
          (match_operand:SI    4 "const_int_operand" "N04U,N04U")
	  ] UNSPECV_SFPIADD_I_INT_BH))]
  "TARGET_XTT_TENSIX_BH"
  "SFPIADD\t%0, %x2, %3, %4"
  [(set_attr "type" "tensix")])

(define_expand "rvtt_sfpxiadd_v_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand"  "0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI    3 "const_int_operand" "N04U")
	  ] UNSPECV_SFPXIADD_V_BH))]
  "TARGET_XTT_TENSIX_BH"
{
  rvtt_emit_sfpxiadd_v_bh (operands[0], operands[1], operands[2], operands[3]);
  DONE;
})

(define_expand "rvtt_sfpxiadd_i_bh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "const_int_operand")
          (match_operand:SI    5 "reg_or_const_int_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] UNSPECV_SFPXIADD_I_BH))]
  "TARGET_XTT_TENSIX_BH"
{
  rtx live = rvtt_gen_rtx_noval (XTT32SImode);
  rvtt_emit_sfpxiadd_i_bh (operands[0], live, operands[1], operands[2], operands[3], operands[6]);
  DONE;
})

(define_expand "rvtt_sfpxiadd_i_lv_bh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
          (match_operand:SI    4 "reg_or_const_int_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:SI    6 "reg_or_const_int_operand")
          (match_operand:SI    7 "const_int_operand")
	  ] UNSPECV_SFPXIADD_I_LV_BH))]
  "TARGET_XTT_TENSIX_BH"
{
  rtx live = operands[2];
  rvtt_emit_sfpxiadd_i_bh (operands[0], live, operands[1], operands[3], operands[4], operands[7]);
  DONE;
})

(define_expand "rvtt_sfpxfcmps_bh"
  [(set (match_operand:SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_const_int_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] UNSPECV_SFPXFCMPS_BH))]
  "TARGET_XTT_TENSIX_BH"
{
  rvtt_emit_sfpxfcmps_bh (operands[1], operands[2], operands[3], operands[6]);
  DONE;
})

(define_expand "rvtt_sfpxfcmpv_bh"
  [(set (match_operand:SI 0 "register_operand")
        (unspec_volatile:SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "const_int_operand")
	  ] UNSPECV_SFPXFCMPV_BH))]
  "TARGET_XTT_TENSIX_BH"
{
  rvtt_emit_sfpxfcmpv_bh (operands[1], operands[2], operands[3]);
  DONE;
})

(define_insn "rvtt_sfpconfig_v_bh"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "register_operand"   "x0")
     (match_operand:SI    1 "const_int_operand"  "N04U")
     ] UNSPECV_SFPCONFIG_V_BH)]
  "TARGET_XTT_TENSIX_BH"
  "SFPCONFIG\t%1, 0, 0"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpmul24_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI    3 "const_int_operand" "N04U")
	  ] UNSPECV_SFPMUL24_BH))]
  "TARGET_XTT_TENSIX_BH"
  "SFPMUL24\t%0, %x1, %x2, %3"
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_bh" "dynamic")])

(define_insn "rvtt_sfpmul24_lv_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand" "0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI    4 "const_int_operand" "N04U")
	  ] UNSPECV_SFPMUL24_BH))]
  "TARGET_XTT_TENSIX_BH"
  "SFPMUL24\t%0, %x2, %x3, %4"
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_bh" "dynamic")])

(define_insn "rvtt_sfparecip_bh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
          (match_operand:SI    2 "const_int_operand")
	  ] UNSPECV_SFPARECIP_BH))]
  "TARGET_XTT_TENSIX_BH"
  "SFPARECIP\t%0, %x1, %2"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfparecip_lv_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand"  "0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xr")
          (match_operand:SI    3 "const_int_operand" "N04U")
	  ] UNSPECV_SFPARECIP_BH))]
  "TARGET_XTT_TENSIX_BH"
  "SFPARECIP\t%0, %x2, %3"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpgt_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI    2 "const_int_operand" "N04U")
	  ] UNSPECV_SFPGT_BH))]
  "TARGET_XTT_TENSIX_BH"
  "SFPGT\t%0, %x1, 0, %2"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfple_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI    2 "const_int_operand" "N04U")
	  ] UNSPECV_SFPLE_BH))]
  "TARGET_XTT_TENSIX_BH"
  "SFPLE\t%0, %x1, 0, %2"
  [(set_attr "type" "tensix")])
