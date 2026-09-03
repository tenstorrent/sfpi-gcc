;; Machine description for Tenstorrent SFPU Intrinsics -- extended arithmetic.
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

;; Extended arithmetic: 24-bit multiply, the reciprocal
;; approximation seed, and the nonlinear approximation word.
(define_expand "rvtt_sfpmul24"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand")
          (match_operand:SI    3 "const_int_operand")
	  ] UNSPECV_SFPMUL24))]
  "TARGET_XTT_TENSIX_BH_QSR"
{
  emit_insn (gen_rvtt_sfpmul24_lv
    (operands[0], rvtt_gen_rtx_noval (XTT32SImode),
     operands[1], operands[2], operands[3]));
  DONE;
})

(define_insn "rvtt_sfpmul24_lv"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand" "xn,0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc,xrxc")
          (match_operand:XTT32SI 3 "reg_or_cstlreg_operand"  "xrxc,xrxc")
          (match_operand:SI    4 "const_int_operand" "n,n")
	  ] UNSPECV_SFPMUL24))]
  "TARGET_XTT_TENSIX_BH_QSR"
  "@
   SFPMUL24\t%x0, %x2, %x3, %4
   SFPMUL24\t%x0, %x2, %x3, %4\t# LV:%x1"
  [(set_attr "xtt_subunit" "mad")
   (set_attr "xtt_result_latency" "2")
   (set_attr "xtt_lreg_write_port" "own")
   (set_attr "xtt_lreg_read_ops" "15")
   (set_attr "xtt_lreg_write_ops" "2")
   (set_attr "xtt_cc_effect" "read")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   (set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_delay" "dynamic")
   ;; Lane-gated consumer (typed-effect attributes).
   (set_attr "xtt_lane_gated" "yes")])

(define_expand "rvtt_sfparecip"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
          (match_operand:SI    2 "const_int_operand")
	  ] UNSPECV_SFPARECIP))]
  "TARGET_XTT_TENSIX_BH"
{
  emit_insn (gen_rvtt_sfparecip_lv
    (operands[0], rvtt_gen_rtx_noval (XTT32SImode),
     operands[1], operands[2]));
  DONE;
})

(define_insn "rvtt_sfparecip_lv"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand"  "xn,0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc,xrxc")
          (match_operand:SI    3 "const_int_operand" "n,n")
	  ] UNSPECV_SFPARECIP))]
  "TARGET_XTT_TENSIX_BH"
{
  /* SFPARECIP_MOD1_COND_RECIP reads the condition sign from LReg[VB]
     (ISA: BlackholeA0 SFPARECIP.md functional model), and VB rides the
     imm12 field's low nibble -- which the gas mnemonic FORCES to zero
     (binutils riscv-opc-sfpu-insns.h: match mask 0xfff000).  The sfpi
     surface's single-source approx_recip(src, RecipMode::IfNegative)
     contract is "reciprocal where src is negative", i.e. VB == VC, so
     the mnemonic cannot express it; emit the architecturally-encoded
     raw word instead (TT_OP encoding: op 0x99, imm12<<12 | VC<<8 |
     VD<<4 | Mod1, VB == imm12 low nibble).  Mod1 RECIP and EXP ignore
     VB (functional model reads only LReg[VC]) and keep the mnemonic
     byte-identically.  */
  if (INTVAL (operands[3]) == (int) SFPARECIP_MOD1_COND_RECIP)
    {
      unsigned vd = REGNO (operands[0]) - SFPU_REG_FIRST;
      unsigned vc;
      if (REG_P (operands[2]))
	vc = REGNO (operands[2]) - SFPU_REG_FIRST;
      else
	{
	  gcc_assert (GET_CODE (operands[2]) == UNSPEC
		      && XINT (operands[2], 1) == UNSPEC_SFPCSTLREG);
	  vc = (unsigned) INTVAL (XVECEXP (operands[2], 0, 0));
	}
      unsigned word = (0x99u << 24) | (vc << 12) | (vc << 8) | (vd << 4)
		      | SFPARECIP_MOD1_COND_RECIP;
      static char buf[96];
      snprintf (buf, sizeof (buf),
		".ttinsn\t%u\t# SFPARECIP\tL%u, L%u, %u (VB=L%u)%s",
		word, vd, vc, (unsigned) SFPARECIP_MOD1_COND_RECIP, vc,
		which_alternative == 1 ? " LV" : "");
      return buf;
    }
  return which_alternative == 0 ? "SFPARECIP\t%x0, %x2, %3"
				: "SFPARECIP\t%x0, %x2, %3\t# LV:%x1";
}
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   ;; Migrated effect-override row (now typed-effect attributes): pure value unary,
   ;; never touches CC; lane-gated consumer.
   (set_attr "xtt_lane_local" "yes")
   (set_attr "xtt_cc_write" "no")
   (set_attr "xtt_lane_gated" "yes")])

(define_expand "rvtt_sfpnonlinear"
  [(set (match_operand:XTT32SI 0 "register_operand")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_operand")
          (match_operand:SI    2 "const_int_operand")
	  ] UNSPECV_SFPNONLINEAR))]
  "TARGET_XTT_TENSIX_QSR"
{
  emit_insn (gen_rvtt_sfpnonlinear_lv
    (operands[0], rvtt_gen_rtx_noval (XTT32SImode),
     operands[1], operands[2]));
  DONE;
})

(define_insn "rvtt_sfpnonlinear_lv"
  [(set (match_operand:XTT32SI 0 "register_operand" "=xr,xr")
        (unspec_volatile:XTT32SI [
	  (match_operand:XTT32SI 1 "reg_or_cstlreg_or_noval_operand"  "xn,0")
          (match_operand:XTT32SI 2 "reg_or_cstlreg_operand"  "xrxc,xrxc")
          (match_operand:SI    3 "const_int_operand" "n,n")
	  ] UNSPECV_SFPNONLINEAR))]
  "TARGET_XTT_TENSIX_QSR"
  "@
   SFPNONLINEAR\t%x0, %x2, %3
   SFPNONLINEAR\t%x0, %x2, %3\t# LV:%x1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set (attr "xtt_dynamic_bug") (symbol_ref "xtt_dynamic_bug (XTT_DYNAMIC_BUG_QSR)"))
   ;; Lane-gated consumer (typed-effect attributes).
   (set_attr "xtt_lane_gated" "yes")])

