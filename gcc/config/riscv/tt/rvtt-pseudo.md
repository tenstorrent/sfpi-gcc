;; Machine description for Tenstorrent SFPU Intrinsics -- frontend pseudo ops.
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

;; Compiler-internal pseudo builtins (the rvtt_sfpx* wrappers the
;; gimple passes expand or rewrite), the lane concat/select
;; carriers, and the SFPNOP and bank-done issue words.
;; These builtins are converted by gimple passes, but the insns are still
;; needed due to the way we expand them.

(define_expand "rvtt_sfpxvif"
  [(set (match_operand:SI 0 "register_operand")
        (unspec:SI [
	  (const_int 0)
	  ] 0))]
  "TARGET_XTT_TENSIX"
  "FAIL;")

(define_expand "rvtt_sfpxbool"
  [(set (match_operand:SI 0 "register_operand")
        (unspec:SI [
          (match_operand:SI 1 "register_operand")
          ] 0))]
  "TARGET_XTT_TENSIX"
  "FAIL;")

(define_expand "rvtt_sfpxcondb"
  [(unspec:SI [
     (match_operand:SI 0 "register_operand")
     (match_operand:SI 1 "register_operand")
     ] 0)]
  "TARGET_XTT_TENSIX"
  "FAIL;")

(define_expand "rvtt_sfpxcondi"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec:SI [
	  (match_operand:SI 1 "register_operand")
	  ] 0))]
  "TARGET_XTT_TENSIX"
  "FAIL;")

(define_expand "rvtt_sfpxicmps"
  [(set (match_operand:SI 0 "register_operand")
        (unspec:SI [
	  (match_operand:SI    1 "reg_or_0_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_0_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] 0))]
  "TARGET_XTT_TENSIX"
  "FAIL;")

(define_expand "rvtt_sfpxicmpv"
  [(set (match_operand:SI 0 "register_operand")
        (unspec:SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "const_int_operand")
	  ] 0))]
  "TARGET_XTT_TENSIX"
  "FAIL;")

(define_expand "rvtt_sfpxfcmps"
  [(set (match_operand:SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "reg_or_0_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "reg_or_const_int_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] 0))]
  "TARGET_XTT_TENSIX"
{
  rvtt_emit_sfpxfcmps (operands[2], operands[3], operands[6]);
  DONE;
})

(define_expand "rvtt_sfpxfcmpv"
  [(set (match_operand:SI 0 "register_operand")
        (unspec_volatile:SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "const_int_operand")
	  ] 0))]
  "TARGET_XTT_TENSIX"
{
  rvtt_emit_sfpxfcmpv (operands[1], operands[2], operands[3]);
  DONE;
})

(define_expand "rvtt_sfpxloadi"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec:XTT32SI [
	  (match_operand:SI 1 "address_operand")
          (match_operand:SI 2 "reg_or_const_int_operand")
          (match_operand:SI 3 "reg_or_0_operand")
          (match_operand:SI 4 "const_int_operand")
          (match_operand:SI 5 "const_int_operand")
	  ] 0))]
  "TARGET_XTT_TENSIX"
{
  rvtt_emit_sfpxloadi (operands[0], rvtt_gen_rtx_noval (XTT32SImode),
			       operands[2]);
  DONE;
})

(define_expand "rvtt_sfpxiadd_v"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "register_operand"  "0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc")
          (match_operand:SI      3 "const_int_operand" "n")
	  ] 0))]
  "TARGET_XTT_TENSIX"
{
  rvtt_emit_sfpxiadd_v (operands[0], operands[1], operands[2], operands[3]);
  DONE;
})

(define_expand "rvtt_sfpxiadd_i"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "reg_or_const_int_operand")
          (match_operand:SI    4 "const_int_operand")
          (match_operand:SI    5 "reg_or_const_int_operand")
          (match_operand:SI    6 "const_int_operand")
	  ] 0))]
  "TARGET_XTT_TENSIX"
{
  emit_insn (gen_rvtt_sfpxiadd_i_lv
    (operands[0], operands[1], rvtt_gen_rtx_noval (XTT32SImode),
     operands[2], operands[3], operands[4], operands[5], operands[6]));
  DONE;
})

(define_expand "rvtt_sfpxiadd_i_lv"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:SI    1 "address_operand")
          (match_operand:XTT32SI 2 "register_operand")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand")
          (match_operand:SI    4 "reg_or_const_int_operand")
          (match_operand:SI    5 "const_int_operand")
          (match_operand:SI    6 "reg_or_const_int_operand")
          (match_operand:SI    7 "const_int_operand")
	  ] 0))]
  "TARGET_XTT_TENSIX"
{
  rvtt_emit_sfpxiadd_i (operands[0], operands[2], operands[1], operands[3], operands[4], operands[7]);
  DONE;
})

(define_insn "rvtt_sfpconcat2"
  [(set (match_operand:XTT64SI 0 "register_operand" "=xr")
     (unspec:XTT64SI [
       (match_operand:XTT32SI 1 "register_operand" "xr")
       (match_operand:XTT32SI 2 "register_operand" "xr")
       ] UNSPEC_SFPCLEAVE))]
  "TARGET_XTT_TENSIX"
  "CONCAT %0, %1, %2"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")])

(define_insn "rvtt_sfpselect2"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
     (unspec:XTT32SI [
       (match_operand:XTT64SI 1 "register_operand" "xr")
       (match_operand:SI 2 "const_int_operand" "n")
       ] UNSPEC_SFPCLEAVE))]
  "TARGET_XTT_TENSIX"
  "SELECT %0, %1, %2"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")])

(define_insn "rvtt_sfpconcat4"
  [(set (match_operand:XTT128SI 0 "register_operand" "=xr")
     (unspec:XTT128SI [
       (match_operand:XTT32SI 1 "register_operand" "xr")
       (match_operand:XTT32SI 2 "register_operand" "xr")
       (match_operand:XTT32SI 3 "register_operand" "xr")
       (match_operand:XTT32SI 4 "register_operand" "xr")
       ] UNSPEC_SFPCLEAVE))]
  "TARGET_XTT_TENSIX"
  "CONCAT %0, %1, %2, %3, %4"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")])

(define_insn "rvtt_sfpselect4"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr")
     (unspec:XTT32SI [
       (match_operand:XTT128SI 1 "register_operand" "xr")
       (match_operand:SI 2 "const_int_operand" "n")
       ] UNSPEC_SFPCLEAVE))]
  "TARGET_XTT_TENSIX"
  "SELECT %0, %1, %2"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")])

(define_insn "rvtt_sfpnop"
  [(unspec_volatile:XTT32SI [
     (const_int 0)
     ] UNSPECV_SFPNOP)]
  "TARGET_XTT_TENSIX"
  "SFPNOP"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_result_latency" "1")
   (set_attr "xtt_lreg_read_ops" "1")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "none")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")])

(define_insn "rvtt_sfpbankdone"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI   0 "const_int_operand" "n")
     (match_operand:SI   1 "const_int_operand" "n")
     (match_operand:SI   2 "const_int_operand" "n")
     ] UNSPECV_SFPBANKDONE)]
  "TARGET_XTT_TENSIX_QSR"
  "SFPNOP\t%0, %1, %2"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")])

