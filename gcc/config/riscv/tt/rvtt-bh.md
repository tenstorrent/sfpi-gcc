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
  UNSPECVSFPLOAD_BH
  UNSPECVSFPXLOADI_BH
  UNSPECVSFPSTORE_BH
  UNSPECVSFPMULI_BH
  UNSPECVSFPADDI_BH
  UNSPECVSFPMUL_BH
  UNSPECVSFPADD_BH
  UNSPECVSFPIADD_V_INT_BH
  UNSPECVSFPXIADD_V_BH
  UNSPECVSFPIADD_I_BH
  UNSPECVSFPIADD_I_LV_BH
  UNSPECVSFPXIADD_I_BH
  UNSPECVSFPXIADD_I_LV_BH
  UNSPECVSFPIADD_I_INT_BH
  UNSPECVSFPSETMAN_BH
  UNSPECVSFPSETEXP_BH
  UNSPECVSFPSETSGN_BH
  UNSPECVSFPMAD_BH
  UNSPECVSFPMAD_LV_BH
  UNSPECVSFPMAD_INT_BH
  UNSPECVSFPDIVP2_BH
  UNSPECVSFPXFCMPS_BH
  UNSPECVSFPXFCMPV_BH
  UNSPECVSFPCAST_BH
  UNSPECVSFPSTOCHRND_I_BH
  UNSPECVSFPSTOCHRND_I_LV_BH
  UNSPECVSFPSTOCHRND_I_INT_BH
  UNSPECVSFPSTOCHRND_V_BH
  UNSPECVSFPSTOCHRND_V_LV_BH
  UNSPECVSFPSTOCHRND_V_INT_BH
  UNSPECVSFPCONFIG_V_BH
  UNSPECVSFPMUL24_BH
  UNSPECVSFPARECIP_BH
  UNSPECVSFPGT_BH
  UNSPECVSFPLE_BH
  UNSPECVSFPMOV_CONFIG_BH
])

(define_expand "rvtt_sfpload_bh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI 1 "address_operand")
          (match_operand:SI 2 "const_int_operand")
          (match_operand:SI 3 "const_int_operand")
          (match_operand:SI 4 "reg_or_const_int_operand")
          (match_operand:SI 5 "reg_or_0_operand")
          (match_operand:SI 6 "const_int_operand")
	  ] UNSPECVSFPLOAD_BH))]
  "TARGET_XTT_TENSIX_BH"
{
  rvtt_emit_sfpload_bh (operands[0], rvtt_gen_rtx_noval (XTT32SImode),
  		        operands[1], operands[2], operands[3], operands[4], operands[5], operands[6]);
  DONE;
}
  [(set_attr "type" "tensix")])

(define_expand "rvtt_sfpload_lv_bh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:SI    3 "const_int_operand")
          (match_operand:SI    4 "const_int_operand")
          (match_operand:SI    5 "reg_or_const_int_operand")
          (match_operand:SI    6 "reg_or_0_operand")
          (match_operand:SI    7 "const_int_operand")
	  ] UNSPECVSFPLOAD_BH))]
  "TARGET_XTT_TENSIX_BH"
{
  rvtt_emit_sfpload_bh (operands[0], operands[2],
  		        operands[1], operands[3], operands[4], operands[5], operands[6], operands[7]);
  DONE;
})

(define_insn "rvtt_sfpload_int_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "0,xn")
          (match_operand:SI    2 "const_int_operand" "N04U,N04U")
          (match_operand:SI    3 "const_int_operand" "N03U,N03U")
          (match_operand:SI    4 "const_int_operand" "N13U,N13U")
	  ] UNSPECVSFPLOAD_BH))]
  "TARGET_XTT_TENSIX_BH"
  "SFPLOAD\t%0, %4, %2, %3"
  [(set_attr "type" "tensix")])


;;; SFPLOADI and SFPLOADI_LV
(define_expand "rvtt_sfpxloadi_bh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec:XTT32SI [
	  (match_operand:SI 1 "address_operand")
          (match_operand:SI 2 "const_int_operand")
          (match_operand:SI 3 "reg_or_const_int_operand")
          (match_operand:SI 4 "reg_or_0_operand")
          (match_operand:SI 5 "const_int_operand")
	  ] UNSPECVSFPXLOADI_BH))]
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
	  ] UNSPECVSFPXLOADI_BH))]
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
	  ] UNSPECVSFPXLOADI_BH))]
  "TARGET_XTT_TENSIX_BH"
  "@
  SFPLOADI\t%0, %s3, %2
  SFPLOADI\t%0, %s3, %2
  SFPLOADI\t%0, %u3, %2
  SFPLOADI\t%0, %u3, %2"
  [(set_attr "type" "tensix")])

(define_expand "rvtt_sfpstore_bh"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "address_operand")
     (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
     (match_operand:SI    2 "const_int_operand")
     (match_operand:SI    3 "const_int_operand")
     (match_operand:SI    4 "reg_or_const_int_operand")
     (match_operand:SI    5 "reg_or_0_operand")
     (match_operand:SI    6 "const_int_operand")
     ] UNSPECVSFPSTORE_BH)]
  "TARGET_XTT_TENSIX_BH"
{
  rtx insn = nullptr;
  if (CONST_INT_P (operands[4]))
    insn = gen_rvtt_sfpstore_int_bh (operands[1], operands[2], operands[3],
    	                             rvtt_clamp_unsigned (operands[4], 0x1FFF));
  else
    {
      unsigned op = TT_OP_BH_SFPSTORE (0, INTVAL (operands[2]), INTVAL (operands[3]), 0);
      insn = rvtt_sfpsynth_store_insn (operands[0], CODE_FOR_rvtt_sfpstore_int_bh,
                                       0, operands[5], op, operands[6],
                                       operands[1], 20);
    }
  emit_insn (insn);
  DONE;
})

;; stores cannot write from L12..L15 due to load macro side loading possibility
(define_insn "rvtt_sfpstore_int_bh"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "reg_or_cstlreg_operand"  "xrxs")
     (match_operand:SI    1 "const_int_operand" "N04U")
     (match_operand:SI    2 "const_int_operand" "N03U")
     (match_operand:SI    3 "const_int_operand" "N13U")
     ] UNSPECVSFPSTORE_BH)]
  "TARGET_XTT_TENSIX_BH"
  "SFPSTORE\t%3, %x0, %1, %2"
  [(set_attr "type" "tensix")])


(define_int_iterator blackhole_muliaddi_op [UNSPECVSFPMULI_BH UNSPECVSFPADDI_BH])
(define_int_attr blackhole_muliaddi_name [(UNSPECVSFPMULI_BH "muli") (UNSPECVSFPADDI_BH "addi")])
(define_int_attr blackhole_muliaddi_insn [(UNSPECVSFPMULI_BH "MULI") (UNSPECVSFPADDI_BH "ADDI")])
(define_expand "rvtt_sfp<blackhole_muliaddi_name>_bh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_0_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] blackhole_muliaddi_op))]
  "TARGET_XTT_TENSIX_BH"
{
  rtx insn = nullptr;
  if (CONST_INT_P (operands[3]))
    insn = gen_rvtt_sfp<blackhole_muliaddi_name>_int_bh (operands[0], operands[2],
               rvtt_clamp_unsigned (operands[3], 0xFFFF), operands[6]);
  else
    {
      unsigned op = TT_OP_BH_SFP<blackhole_muliaddi_insn> (0, 0, INTVAL (operands[6]));
      insn = rvtt_sfpsynth_insn_dst (operands[1], CODE_FOR_rvtt_sfp<blackhole_muliaddi_name>_int_bh,
                                     XTT_DELAY_DYNAMIC, operands[4], op, operands[5],
				     operands[0], 4, operands[2]);
  }
  emit_insn (insn);
  DONE;
})

(define_insn "rvtt_sfp<blackhole_muliaddi_name>_int_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "0")
          (match_operand:SI    2 "const_int_operand" "N16U")
          (match_operand:SI    3 "const_int_operand" "N04U")
	  ] blackhole_muliaddi_op))]
  "TARGET_XTT_TENSIX_BH"
  "SFP<blackhole_muliaddi_insn>\t%0, %2, %3"
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_bh" "dynamic")])

(define_expand "rvtt_sfpdivp2_bh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:SI    2 "reg_or_const_int_operand")
          (match_operand:SI    3 "reg_or_0_operand")
          (match_operand:SI    4 "const_int_operand")
          (match_operand:XTT32SI 5 "reg_or_cstlreg_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] UNSPECVSFPDIVP2_BH))]
  "TARGET_XTT_TENSIX_BH"
{
  rvtt_emit_sfpdivp2_bh (operands[0], rvtt_gen_rtx_noval (XTT32SImode),
  		         operands[1], operands[2], operands[5], operands[6], operands[3], operands[4]);
  DONE;
})

(define_expand "rvtt_sfpdivp2_lv_bh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_0_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:XTT32SI 6 "reg_or_cstlreg_operand")
          (match_operand:SI    7 "const_int_operand")
	  ] UNSPECVSFPDIVP2_BH))]
  "TARGET_XTT_TENSIX_BH"
{
  rvtt_emit_sfpdivp2_bh (operands[0], operands[2],
  		         operands[1], operands[3], operands[6], operands[7], operands[4], operands[5]);
  DONE;
})

(define_insn "rvtt_sfpdivp2_int_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "0,xn")
          (match_operand:SI    2 "const_int_operand" "N12S,N12S")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "xrxc,xrxc")
          (match_operand:SI    4 "const_int_operand" "N04U,N04U")
	  ] UNSPECVSFPDIVP2_BH))]
  "TARGET_XTT_TENSIX_BH"
  "SFPDIVP2\t%0, %x3, %2, %4"
  [(set_attr "type" "tensix")])

(define_expand "rvtt_sfpmov_config_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI 1 "const_int_operand" "N04U")
	  ] UNSPECVSFPMOV_CONFIG_BH))]
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
	  ] UNSPECVSFPMOV_CONFIG_BH))]
  "TARGET_XTT_TENSIX_BH"
  "SFPMOV\t%0, L%2, 8"
  [(set_attr "type" "tensix")])

(define_int_iterator blackhole_muladd_op [UNSPECVSFPMUL_BH UNSPECVSFPADD_BH])
(define_int_attr blackhole_muladd_name [(UNSPECVSFPMUL_BH "mul") (UNSPECVSFPADD_BH "add")])
(define_int_attr blackhole_muladd_insn [(UNSPECVSFPMUL_BH "MUL") (UNSPECVSFPADD_BH "ADD")])
(define_int_attr blackhole_muladd_ops [(UNSPECVSFPMUL_BH "%x1, %x2, L9") (UNSPECVSFPADD_BH "L10, %x1, %x2")])
(define_int_attr blackhole_muladd_ops_lv [(UNSPECVSFPMUL_BH "%x2, %x3, L9") (UNSPECVSFPADD_BH "L10, %x2, %x3")])
(define_insn "rvtt_sfp<blackhole_muladd_name>_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI    3 "const_int_operand" "N04U")
	  ] blackhole_muladd_op))]
  "TARGET_XTT_TENSIX_BH"
  "SFP<blackhole_muladd_insn>\t%0, <blackhole_muladd_ops>, %3"
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_bh" "dynamic")])

(define_insn "rvtt_sfp<blackhole_muladd_name>_lv_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI    4 "const_int_operand" "N04U")
	  ] blackhole_muladd_op))]
  "TARGET_XTT_TENSIX_BH"
  "SFP<blackhole_muladd_insn>\t%0, <blackhole_muladd_ops_lv>, %4"
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_bh" "dynamic")])

(define_insn "rvtt_sfpiadd_v_int_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand"  "0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xr")
          (match_operand:SI    3 "const_int_operand" "N04U")
	  ] UNSPECVSFPIADD_V_INT_BH))]
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
	  ] UNSPECVSFPIADD_I_INT_BH))]
  "TARGET_XTT_TENSIX_BH"
  "SFPIADD\t%0, %x2, %3, %4"
  [(set_attr "type" "tensix")])

(define_expand "rvtt_sfpxiadd_v_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand"  "0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI    3 "const_int_operand" "N04U")
	  ] UNSPECVSFPXIADD_V_BH))]
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
	  ] UNSPECVSFPXIADD_I_BH))]
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
	  ] UNSPECVSFPXIADD_I_LV_BH))]
  "TARGET_XTT_TENSIX_BH"
{
  rtx live = operands[2];
  rvtt_emit_sfpxiadd_i_bh (operands[0], live, operands[1], operands[3], operands[4], operands[7]);
  DONE;
})

(define_expand "rvtt_sfpcast_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI    2 "const_int_operand" "N04U")
	  ] UNSPECVSFPCAST_BH))]
  "TARGET_XTT_TENSIX_BH"
  {
    emit_insn (gen_rvtt_sfpcast_lv_bh (operands[0], rvtt_gen_rtx_noval (XTT32SImode), operands[1], operands[2]));
    DONE;
  })

(define_insn "rvtt_sfpcast_lv_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand"  "0,xn")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc,xrxc")
          (match_operand:SI    3 "const_int_operand" "N04U,N04U")
	  ] UNSPECVSFPCAST_BH))]
  "TARGET_XTT_TENSIX_BH"
  "SFPCAST\t%0, %x2, %3"
  [(set_attr "type" "tensix")])

(define_expand "rvtt_sfpstochrnd_i_bh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:SI    2 "const_int_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_0_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:XTT32SI 6 "reg_or_cstlreg_operand")
          (match_operand:SI    7 "const_int_operand")
	  ] UNSPECVSFPSTOCHRND_I_BH))]
  "TARGET_XTT_TENSIX_BH"
{
  rtx live = rvtt_gen_rtx_noval (XTT32SImode);
  rvtt_emit_sfpstochrnd_i_bh (operands[0], live, operands[1], operands[2], operands[3],
                             operands[6], operands[7], operands[4], operands[5]);
  DONE;
})

(define_expand "rvtt_sfpstochrnd_i_lv_bh"
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
	  ] UNSPECVSFPSTOCHRND_I_LV_BH))]
  "TARGET_XTT_TENSIX_BH"
{
  rtx live = operands[2];
  rvtt_emit_sfpstochrnd_i_bh (operands[0], live, operands[1], operands[3], operands[4],
                             operands[7], operands[8], operands[5], operands[6]);
  DONE;
})

(define_insn "rvtt_sfpstochrnd_i_int_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "0,xn")
          (match_operand:SI    2 "const_int_operand" "N01U,N01U")
          (match_operand:SI    3 "const_int_operand" "N05U,N05U")
          (match_operand:XTT32SI 4 "reg_or_cstlreg_operand"  "xrxc,xrxc")
          (match_operand:SI    5 "const_int_operand" "N04U,N04U")
	  ] UNSPECVSFPSTOCHRND_I_INT_BH))]
  "TARGET_XTT_TENSIX_BH"
  "SFPSTOCHRND\t%0, L0, %x4, %5, %2, %3"
  [(set_attr "type" "tensix")])

(define_expand "rvtt_sfpstochrnd_v_bh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "const_int_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
          (match_operand:SI    4 "const_int_operand")
	  ] UNSPECVSFPSTOCHRND_V_BH))]
  "TARGET_XTT_TENSIX_BH"
{
  rtx live = rvtt_gen_rtx_noval (XTT32SImode);
  emit_insn (gen_rvtt_sfpstochrnd_v_int_bh (operands[0], live, operands[1], operands[2], operands[3], operands[4]));
  DONE;
})

(define_expand "rvtt_sfpstochrnd_v_lv_bh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand")
          (match_operand:SI    2 "const_int_operand")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 4 "reg_or_cstlreg_operand")
          (match_operand:SI    5 "const_int_operand")
	  ] UNSPECVSFPSTOCHRND_V_LV_BH))]
  "TARGET_XTT_TENSIX_BH"
{
  rtx live = operands[1];
  emit_insn (gen_rvtt_sfpstochrnd_v_int_bh (operands[0], live, operands[2], operands[3], operands[4], operands[5]));
  DONE;
})

(define_insn "rvtt_sfpstochrnd_v_int_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "0,xn")
          (match_operand:SI    2 "const_int_operand" "N01U,N01U")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "xrxc,xrxc")
          (match_operand:XTT32SI 4 "reg_or_cstlreg_operand"  "xrxc,xrxc")
          (match_operand:SI    5 "const_int_operand" "N04U,N04U")
	  ] UNSPECVSFPSTOCHRND_V_INT_BH))]
  "TARGET_XTT_TENSIX_BH"
  "SFPSTOCHRND\t%0, %x3, %x4, %5, %2, 0"
  [(set_attr "type" "tensix")])

(define_int_iterator blackhole_set_float_op_v [UNSPECVSFPSETEXP_BH UNSPECVSFPSETMAN_BH UNSPECVSFPSETSGN_BH])
(define_int_iterator blackhole_set_float_op_i [UNSPECVSFPSETEXP_BH UNSPECVSFPSETSGN_BH])
(define_int_attr blackhole_set_float_name [
  (UNSPECVSFPSETEXP_BH "exp")
  (UNSPECVSFPSETMAN_BH "man")
  (UNSPECVSFPSETSGN_BH "sgn")
  ])
(define_int_attr blackhole_set_float_insn [
  (UNSPECVSFPSETEXP_BH "EXP")
  (UNSPECVSFPSETMAN_BH "MAN")
  (UNSPECVSFPSETSGN_BH "SGN")
  ])

(define_insn "rvtt_sfpset<blackhole_set_float_name>_v_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand"  "0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
	  ] blackhole_set_float_op_v))]
  "TARGET_XTT_TENSIX_BH"
  "SFPSET<blackhole_set_float_insn>\t%0, %x2, 0, 0"
  [(set_attr "type" "tensix")])

(define_expand "rvtt_sfpset<blackhole_set_float_name>_i_bh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:SI    2 "reg_or_const_int_operand")
          (match_operand:SI    3 "reg_or_0_operand")
          (match_operand:SI    4 "const_int_operand")
          (match_operand:XTT32SI 5 "reg_or_cstlreg_operand")
	  ] blackhole_set_float_op_i))]
  "TARGET_XTT_TENSIX_BH"
{
  rtx insn;
  if (CONST_INT_P (operands[2]))
    insn = gen_rvtt_sfpset<blackhole_set_float_name>_i_int_bh
      (operands[0], rvtt_clamp_unsigned (operands[2], 0xFFF), operands[5]);
  else
    {
      unsigned op = TT_OP_BH_SFPSET<blackhole_set_float_insn> (0, 0, 0, 1);
      insn = rvtt_sfpsynth_insn_dst (operands[1], CODE_FOR_rvtt_sfpset<blackhole_set_float_name>_i_int_bh,
                                     0, operands[3], op, operands[4],
				     operands[5], 4, operands[0], 8, nullptr);
    }
  emit_insn (insn);
  DONE;
})

(define_expand "rvtt_sfpset<blackhole_set_float_name>_i_lv_bh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_0_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:XTT32SI 6 "reg_or_cstlreg_operand")
	  ] blackhole_set_float_op_i))]
  "TARGET_XTT_TENSIX_BH"
{
  rtx insn;
  if (CONST_INT_P (operands[3]))
    insn = gen_rvtt_sfpset<blackhole_set_float_name>_i_lv_int_bh
      (operands[0], operands[2], rvtt_clamp_unsigned (operands[3], 0xFFF), operands[6]);
  else
    {
      unsigned op = TT_OP_BH_SFPSET<blackhole_set_float_insn> (0, 0, 0, 1);
      insn = rvtt_sfpsynth_insn_dst (operands[1], CODE_FOR_rvtt_sfpset<blackhole_set_float_name>_i_lv_int_bh,
      	     			     0, operands[4], op, operands[5],
				     operands[6], 4, operands[0], 8, operands[2]);
    }
   emit_insn (insn);
  DONE;
})

(define_insn "rvtt_sfpset<blackhole_set_float_name>_i_int_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "const_int_operand" "N12U")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
	  ] blackhole_set_float_op_i))]
  "TARGET_XTT_TENSIX_BH"
  "SFPSET<blackhole_set_float_insn>\t%0, %x2, %1, 1"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpset<blackhole_set_float_name>_i_lv_int_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand" "0")
          (match_operand:SI    2 "const_int_operand" "N12U")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "xrxc")
	  ] blackhole_set_float_op_i))]
  "TARGET_XTT_TENSIX_BH"
  "SFPSET<blackhole_set_float_insn>\t%0, %x3, %2, 1"
  [(set_attr "type" "tensix")])

(define_expand "rvtt_sfpsetman_i_bh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:SI    2 "reg_or_const_int_operand")
          (match_operand:SI    3 "reg_or_0_operand")
          (match_operand:SI    4 "const_int_operand")
          (match_operand:XTT32SI 5 "reg_or_cstlreg_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] UNSPECVSFPSETMAN_BH))]
  "TARGET_XTT_TENSIX_BH"
{
  rtx live = rvtt_gen_rtx_noval (XTT32SImode);
  rvtt_emit_sfpsetman_bh (operands[0], live, operands[1], operands[2], operands[5]);
  DONE;
})

(define_expand "rvtt_sfpsetman_i_lv_bh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_0_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:XTT32SI 6 "reg_or_cstlreg_operand")
          (match_operand:SI    7 "const_int_operand")] UNSPECVSFPSETMAN_BH))]
  "TARGET_XTT_TENSIX_BH"
{
  rtx live = operands[2];
  rvtt_emit_sfpsetman_bh (operands[0], live, operands[1], operands[3], operands[6]);
  DONE;
})

(define_insn "rvtt_sfpsetman_i_int_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "0,xn")
          (match_operand:SI    2 "const_int_operand" "N12U,N12U")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "xrxc,xrxc")
	  ] UNSPECVSFPSETMAN_BH))]
  "TARGET_XTT_TENSIX_BH"
  "SFPSETMAN\t%0, %x3, %2, 1"
  [(set_attr "type" "tensix")])

(define_expand "rvtt_sfpmad_bh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
          (match_operand:SI    4 "const_int_operand")
	  ] UNSPECVSFPMAD_BH))]
  "TARGET_XTT_TENSIX_BH"
{
  rtx live = rvtt_gen_rtx_noval (XTT32SImode);
  emit_insn (gen_rvtt_sfpmad_int_bh (operands[0], live, operands[1], operands[2], operands[3], operands[4]));
  DONE;
})

(define_expand "rvtt_sfpmad_lv_bh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 4 "reg_or_cstlreg_operand")
          (match_operand:SI    5 "const_int_operand")
	  ] UNSPECVSFPMAD_LV_BH))]
  "TARGET_XTT_TENSIX_BH"
{
  rtx live = operands[1];
  emit_insn (gen_rvtt_sfpmad_int_bh (operands[0], live, operands[2], operands[3], operands[4], operands[5]));
  DONE;
})

(define_insn "rvtt_sfpmad_int_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr, xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn, 0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc, xrxc")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "xrxc, xrxc")
          (match_operand:XTT32SI 4 "reg_or_cstlreg_operand"  "xrxc, xrxc")
          (match_operand:SI    5 "const_int_operand" "N04U,N04U")
	  ] UNSPECVSFPMAD_INT_BH))]
  "TARGET_XTT_TENSIX_BH"
  "SFPMAD\t%0, %x2, %x3, %x4, %5"
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_bh" "dynamic")])

(define_expand "rvtt_sfpxfcmps_bh"
  [(set (match_operand:SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_const_int_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] UNSPECVSFPXFCMPS_BH))]
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
	  ] UNSPECVSFPXFCMPV_BH))]
  "TARGET_XTT_TENSIX_BH"
{
  rvtt_emit_sfpxfcmpv_bh (operands[1], operands[2], operands[3]);
  DONE;
})

(define_insn "rvtt_sfpconfig_v_bh"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "register_operand"   "x0")
     (match_operand:SI    1 "const_int_operand"  "N04U")
     ] UNSPECVSFPCONFIG_V_BH)]
  "TARGET_XTT_TENSIX_BH"
  "SFPCONFIG\t%1, 0, 0"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpmul24_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI    3 "const_int_operand" "N04U")
	  ] UNSPECVSFPMUL24_BH))]
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
	  ] UNSPECVSFPMUL24_BH))]
  "TARGET_XTT_TENSIX_BH"
  "SFPMUL24\t%0, %x2, %x3, %4"
  [(set_attr "type" "tensix")
   (set_attr "xtt_delay_bh" "dynamic")])

(define_insn "rvtt_sfparecip_bh"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
          (match_operand:SI    2 "const_int_operand")
	  ] UNSPECVSFPARECIP_BH))]
  "TARGET_XTT_TENSIX_BH"
  "SFPARECIP\t%0, %x1, %2"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfparecip_lv_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand"  "0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xr")
          (match_operand:SI    3 "const_int_operand" "N04U")
	  ] UNSPECVSFPARECIP_BH))]
  "TARGET_XTT_TENSIX_BH"
  "SFPARECIP\t%0, %x2, %3"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfpgt_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI    2 "const_int_operand" "N04U")
	  ] UNSPECVSFPGT_BH))]
  "TARGET_XTT_TENSIX_BH"
  "SFPGT\t%0, %x1, 0, %2"
  [(set_attr "type" "tensix")])

(define_insn "rvtt_sfple_bh"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI    2 "const_int_operand" "N04U")
	  ] UNSPECVSFPLE_BH))]
  "TARGET_XTT_TENSIX_BH"
  "SFPLE\t%0, %x1, 0, %2"
  [(set_attr "type" "tensix")])
